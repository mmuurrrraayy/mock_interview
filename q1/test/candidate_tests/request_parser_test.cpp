#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "../../requests/request_parser.h"

using std::chrono::microseconds;

// ---------- Minimal test harness ----------

namespace {

struct TestCase { std::string name; std::function<void()> fn; };
std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
int g_failures_in_test = 0;

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

}  // namespace

#define TEST(name)                                            \
    static void name();                                       \
    static Registrar registrar_##name(#name, name);           \
    static void name()

// Non-fatal check: records failure and keeps going.
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++g_failures_in_test;                                            \
            std::cerr << "    CHECK failed: " #cond " (line " << __LINE__    \
                      << ")\n";                                              \
        }                                                                    \
    } while (0)

// Fatal check: aborts the current test (used before indexing into results).
#define REQUIRE(cond)                                                        \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++g_failures_in_test;                                            \
            std::cerr << "    REQUIRE failed: " #cond " (line " << __LINE__  \
                      << ")\n";                                              \
            return;                                                          \
        }                                                                    \
    } while (0)

// Passes only if the expression throws exactly std::invalid_argument
// (or a subclass). Any other exception type, or no exception, is a failure.
#define CHECK_THROWS_INVALID(expr)                                           \
    do {                                                                     \
        bool ok = false;                                                     \
        try { (void)(expr); }                                                \
        catch (const std::invalid_argument&) { ok = true; }                  \
        catch (...) {}                                                       \
        if (!ok) {                                                           \
            ++g_failures_in_test;                                            \
            std::cerr << "    expected std::invalid_argument from: " #expr   \
                      << " (line " << __LINE__ << ")\n";                     \
        }                                                                    \
    } while (0)

// ---------- Test data helpers ----------

namespace {

const char* kSample = R"({
  "id": "req-0042",
  "tenant": "tenant-7",
  "atomic": true,
  "stages": [
    { "id": "s1", "type": "CPU",
      "resources": { "cores": 16, "ram_gb": 64, "gpus": 0 },
      "duration": 200, "reads": ["raw"], "writes": ["clean"],
      "op": "copy", "dependents": [3] },
    { "id": "s2", "type": "CPU",
      "resources": { "cores": 8, "ram_gb": 2048, "gpus": 0 },
      "duration": 500, "reads": ["ref_table"], "writes": ["joined_ref"],
      "op": "copy", "dependents": [4] },
    { "id": "s3", "cluster": "B", "type": "GPU",
      "resources": { "cores": 0, "ram_gb": 128, "gpus": 4 },
      "duration": 50, "reads": ["clean"], "writes": ["model"],
      "op": "hash_combine", "dependents": [4] },
    { "id": "s4", "type": "CPU",
      "resources": { "cores": 32, "ram_gb": 128, "gpus": 0 },
      "duration": 200, "reads": ["model", "joined_ref"], "writes": ["report"],
      "op": "concat", "dependents": [] }
  ]
})";

std::string wrap(const std::string& stages_body) {
    return R"({"id":"r","tenant":"t","atomic":false,"stages":[)" + stages_body + "]}";
}

std::string stage(const std::string& id, const std::string& type, int ram,
                  int duration, const std::string& deps) {
    return R"({"id":")" + id + R"(","type":")" + type +
           R"(","resources":{"cores":1,"ram_gb":)" + std::to_string(ram) +
           R"(,"gpus":0},"duration":)" + std::to_string(duration) +
           R"(,"reads":[],"writes":[],"op":"copy","dependents":)" + deps + "}";
}

using Deps = std::pair<int, int>;

}  // namespace

// ---------- Happy path ----------

TEST(ReturnsOneJobPerStageInOrder) {
    auto jobs = parse_request(kSample);
    REQUIRE(jobs.size() == 4u);
    CHECK(jobs[0].id() == "s1");
    CHECK(jobs[1].id() == "s2");
    CHECK(jobs[2].id() == "s3");
    CHECK(jobs[3].id() == "s4");
}

TEST(MapsProcessorType) {
    auto jobs = parse_request(kSample);
    REQUIRE(jobs.size() == 4u);
    CHECK(jobs[0].processor() == false);
    CHECK(jobs[1].processor() == false);
    CHECK(jobs[2].processor() == true);  // GPU
    CHECK(jobs[3].processor() == false);
}

TEST(MapsMemoryFromRamGb) {
    auto jobs = parse_request(kSample);
    REQUIRE(jobs.size() == 4u);
    CHECK(jobs[0].memory() == 64);
    CHECK(jobs[1].memory() == 2048);
    CHECK(jobs[2].memory() == 128);
    CHECK(jobs[3].memory() == 128);
}

TEST(ConvertsMillisecondsToMicroseconds) {
    auto jobs = parse_request(kSample);
    REQUIRE(jobs.size() == 4u);
    CHECK(jobs[0].duration() == microseconds(200000));
    CHECK(jobs[1].duration() == microseconds(500000));
    CHECK(jobs[2].duration() == microseconds(50000));
    CHECK(jobs[3].duration() == microseconds(200000));
}

TEST(MapsDependentsToOneBasedIndices) {
    auto jobs = parse_request(kSample);
    REQUIRE(jobs.size() == 4u);
    CHECK(jobs[0].dependents() == Deps(3, 0));
    CHECK(jobs[1].dependents() == Deps(4, 0));
    CHECK(jobs[2].dependents() == Deps(4, 0));
    CHECK(jobs[3].dependents() == Deps(0, 0));  // none
}

TEST(TwoDependentsPreserveOrder) {
    auto jobs = parse_request(wrap(stage("a", "CPU", 1, 1, "[3,2]") + "," +
                                   stage("b", "CPU", 1, 1, "[]") + "," +
                                   stage("c", "CPU", 1, 1, "[]")));
    REQUIRE(jobs.size() == 3u);
    CHECK(jobs[0].dependents() == Deps(3, 2));
}

TEST(EmptyStagesGivesEmptyVector) {
    CHECK(parse_request(wrap("")).empty());
}

TEST(IgnoresUnknownFields) {
    auto jobs = parse_request(R"({"id":"r","surprise":{"nested":[1,2,3]},"stages":[
        {"id":"x","type":"CPU","resources":{"ram_gb":4,"weird":true},
         "duration":1,"dependents":[],"future_field":"hello"}]})");
    REQUIRE(jobs.size() == 1u);
    CHECK(jobs[0].id() == "x");
}

TEST(StageOrderDefinesIndexNotId) {
    // Ids are arbitrary; "1-based position in the array" is the contract.
    auto jobs = parse_request(wrap(stage("zebra", "CPU", 1, 1, "[2]") + "," +
                                   stage("apple", "GPU", 1, 1, "[]")));
    REQUIRE(jobs.size() == 2u);
    CHECK(jobs[0].id() == "zebra");
    CHECK(jobs[0].dependents() == Deps(2, 0));
}

// ---------- Error handling ----------

TEST(MalformedJsonThrows) {
    CHECK_THROWS_INVALID(parse_request("{ not json"));
    CHECK_THROWS_INVALID(parse_request(""));
}

TEST(MissingStagesThrows) {
    CHECK_THROWS_INVALID(parse_request(R"({"id":"r"})"));
}

TEST(StagesNotAnArrayThrows) {
    CHECK_THROWS_INVALID(parse_request(R"({"id":"r","stages":{}})"));
}

TEST(UnknownTypeThrows) {
    CHECK_THROWS_INVALID(parse_request(wrap(stage("a", "TPU", 1, 1, "[]"))));
}

TEST(MissingResourcesThrows) {
    CHECK_THROWS_INVALID(parse_request(
        wrap(R"({"id":"a","type":"CPU","duration":1,"dependents":[]})")));
}

TEST(MissingRamThrows) {
    CHECK_THROWS_INVALID(parse_request(
        wrap(R"({"id":"a","type":"CPU","resources":{"cores":1},
                 "duration":1,"dependents":[]})")));
}

TEST(MissingDurationThrows) {
    CHECK_THROWS_INVALID(parse_request(
        wrap(R"({"id":"a","type":"CPU","resources":{"ram_gb":1},"dependents":[]})")));
}

TEST(MissingIdThrows) {
    CHECK_THROWS_INVALID(parse_request(
        wrap(R"({"type":"CPU","resources":{"ram_gb":1},
                 "duration":1,"dependents":[]})")));
}

TEST(MoreThanTwoDependentsThrows) {
    CHECK_THROWS_INVALID(parse_request(
        wrap(stage("a", "CPU", 1, 1, "[2,3,4]") + "," +
             stage("b", "CPU", 1, 1, "[]") + "," +
             stage("c", "CPU", 1, 1, "[]") + "," +
             stage("d", "CPU", 1, 1, "[]"))));
}

TEST(DependentOutOfRangeThrows) {
    CHECK_THROWS_INVALID(parse_request(
        wrap(stage("a", "CPU", 1, 1, "[5]") + "," +
             stage("b", "CPU", 1, 1, "[]"))));
}

TEST(ZeroOrNegativeDependentThrows) {
    // 0 is the "none" sentinel in Job, so it must not appear in the input.
    CHECK_THROWS_INVALID(parse_request(wrap(stage("a", "CPU", 1, 1, "[0]"))));
    CHECK_THROWS_INVALID(parse_request(wrap(stage("a", "CPU", 1, 1, "[-1]"))));
}

// ---------- Runner ----------

int main() {
    int failed = 0;
    for (const auto& t : registry()) {
        g_failures_in_test = 0;
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_failures_in_test;
            std::cerr << "    unexpected exception: " << e.what() << "\n";
        } catch (...) {
            ++g_failures_in_test;
            std::cerr << "    unexpected non-std exception\n";
        }
        std::cout << (g_failures_in_test ? "[FAIL] " : "[ OK ] ") << t.name << "\n";
        if (g_failures_in_test) ++failed;
    }
    std::cout << "\n" << (registry().size() - failed) << "/" << registry().size()
              << " tests passed\n";
    return failed ? 1 : 0;
}