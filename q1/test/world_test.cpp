// world_test.cpp -- tests for World (no framework needed).
//
// Build (C++23, because Oracle.h uses std::expected):
//   g++ -std=c++23 -pthread world_test.cpp Cluster.cpp Node.cpp Model.cpp -o world_test
//
// Ops: the tests use only ops from Model.cpp's registry. "copy" stands in for a constant
// (reads "seven", writes 7) and "add" stands in for increment (reads x and "one", writes
// x + 1). Fixture seeds the store with seven=7 and one=1.
//
// These tests assume Node::queueJob reserves memory (memory_used_ += job.memory(),
// starting from 0) and Node's rate limit never fires; see the notes in the chat.

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "../simulation/World.h"

// ------------------------------------------------------------ tiny test harness
static int g_failures = 0;
#define CHECK(cond)                                                              \
  do {                                                                           \
    if (!(cond)) {                                                               \
      ++g_failures;                                                              \
      std::cerr << "  FAILED " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
    }                                                                            \
  } while (0)

static std::vector<std::pair<std::string, std::function<void()>>>& registry() {
  static std::vector<std::pair<std::string, std::function<void()>>> r;
  return r;
}
struct Reg {
  Reg(const char* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); }
};
#define TEST(name)                       \
  static void name();                    \
  static Reg reg_##name(#name, name);    \
  static void name()

// ------------------------------------------------------------------- fixtures
struct Probe {
  std::vector<JobEvent> events;
  std::vector<std::string> timers;
  int recovers = 0;
  bool rearm = false;  // re-arm every timer forever (for the runaway-loop test)
};

// Runs a request's stages sequentially; replies "done" or "failed".
class TestCoord : public Coordinator {
 public:
  explicit TestCoord(std::shared_ptr<Probe> p) : p_(std::move(p)) {}

  void on_request(const Request& r, Context& ctx) override {
    runs_[r.id] = Run{r};
    submit_next(r.id, ctx);
  }
  void on_job_event(const JobEvent& ev, Context& ctx) override {
    p_->events.push_back(ev);
    if (!runs_.count(ev.request_id)) return;  // job submitted directly by a test
    if (ev.type == JobEventType::FAILED) {
      ctx.reply(ev.request_id, "failed");
      return;
    }
    submit_next(ev.request_id, ctx);
  }
  void on_timer(const std::string& id, Context& ctx) override {
    p_->timers.push_back(id);
    if (p_->rearm) ctx.set_timer(Time{1}, id);
  }
  void on_recover(Context&) override { ++p_->recovers; }

 private:
  struct Run {
    Request req;
    size_t next = 0;
  };

  void submit_next(const RequestId& id, Context& ctx) {
    Run& run = runs_.at(id);
    if (run.next >= run.req.stages.size()) {
      ctx.reply(id, "done");
      return;
    }
    const Stage& s = run.req.stages[run.next++];
    if (!ctx.submit_job(s.cluster, id, s).ok()) ctx.reply(id, "rejected");
  }

  std::shared_ptr<Probe> p_;
  std::map<RequestId, Run> runs_;
};

struct Fixture {
  std::shared_ptr<Probe> probe = std::make_shared<Probe>();
  World w{[p = probe] { return std::make_unique<TestCoord>(p); }};
  Fixture() {
    w.add_cluster("c", /*nodes*/ 1, /*slots*/ 2, /*mem_gb*/ 64, /*node_ids*/ {"n0"});
    seed({});
  }
  // Replaces the store with `s` plus the constants the stand-in ops read.
  void seed(Store s) {
    s["one"] = 1;
    s["seven"] = 7;
    w.set_initial_store(std::move(s));
  }
};

static Stage stage(const std::string& id, float dur_us, int ram = 1,
                   std::vector<std::string> reads = {"seven"},
                   std::vector<std::string> writes = {"x"}, const std::string& op = "copy") {
  Stage s;
  s.id = id;
  s.cluster = "c";
  s.resources = Resources{1, ram, 0};
  s.duration = dur_us;
  s.reads = std::move(reads);
  s.writes = std::move(writes);
  s.regName = op;
  return s;
}
// A stage that reads x and writes x + 1.
static Stage inc_stage(const std::string& id, float dur_us) {
  return stage(id, dur_us, 1, {"x", "one"}, {"x"}, "add");
}
static Request request(const std::string& id, std::vector<Stage> stages) {
  Request r;
  r.id = id;
  r.owner = "test";
  r.stages = std::move(stages);
  return r;
}

// ---------------------------------------------------------------------- tests
TEST(single_stage_completes_and_replies) {
  Fixture f;
  f.w.submit_request(request("r1", {stage("s1", 1000)}));
  f.w.run_until_idle();

  CHECK(f.w.now() == Time{1000});
  CHECK(f.w.store().at("x") == Value{7});
  CHECK(f.w.effect_log().size() == 1);
  CHECK(f.w.effect_log()[0].time == Time{1000});
  CHECK(f.w.effect_log()[0].attempt == 1);
  CHECK(f.probe->events.size() == 1 && f.probe->events[0].type == JobEventType::COMPLETED);
  CHECK(f.w.replies().at("r1").size() == 1 && f.w.replies().at("r1")[0].second == "done");
  CHECK(f.w.violations().empty());
}

TEST(stages_run_in_order) {
  Fixture f;
  f.seed({{"x", 0}});
  f.w.submit_request(request("r1", {inc_stage("a", 100), inc_stage("b", 100)}));
  f.w.run_until_idle();
  CHECK(f.w.store().at("x") == Value{2});
  CHECK(f.w.now() == Time{200});
  CHECK(f.w.violations().empty());
}

TEST(reads_are_snapshotted_at_job_start) {
  // Both jobs start at t=0 and read x=0. The short one commits x=1 at t=50; the long one
  // must still compute from its start snapshot (0), so x ends at 1, not 2.
  Fixture f;
  f.seed({{"x", 0}});
  f.w.submit_request(request("slow", {inc_stage("s", 100)}));
  f.w.submit_request(request("fast", {inc_stage("s", 50)}));
  f.w.run_until_idle();
  CHECK(f.w.store().at("x") == Value{1});
  CHECK(f.w.effect_log().size() == 2);
}

TEST(missing_input_is_a_violation_not_a_crash) {
  Fixture f;  // x is not in the store
  f.w.submit_request(request("r1", {inc_stage("s", 10)}));
  f.w.run_until_idle();
  CHECK(f.w.violations().size() == 1);
  CHECK(f.w.effect_log().empty());
}

TEST(unknown_op_is_a_violation) {
  Fixture f;
  f.w.submit_request(request("r1", {stage("s", 10, 1, {}, {"x"}, "no_such_op")}));
  f.w.run_until_idle();
  CHECK(f.w.violations().size() == 1);
  CHECK(f.w.effect_log().empty());
}

TEST(unknown_cluster_rejected) {
  Fixture f;
  auto r = f.w.context().submit_job("nope", "r1", stage("s", 10));
  CHECK(r.status == SubmitStatus::REJECTED_UNKNOWN_CLUSTER);
  CHECK(!r.ok());
}

TEST(no_capacity_then_capacity_after_completion) {
  Fixture f;  // node has 64 GB
  Context& ctx = f.w.context();
  auto a = ctx.submit_job("c", "r1", stage("s", 100, /*ram*/ 40));
  auto b = ctx.submit_job("c", "r2", stage("s", 100, /*ram*/ 40));
  CHECK(a.ok());
  CHECK(b.status == SubmitStatus::REJECTED_NO_CAPACITY);
  f.w.run_until_idle();  // a finishes and releases its memory
  CHECK(ctx.submit_job("c", "r3", stage("s", 100, 40)).ok());
}

TEST(job_ids_are_unique) {
  Fixture f;
  Context& ctx = f.w.context();
  auto a = ctx.submit_job("c", "r1", stage("s", 10));
  auto b = ctx.submit_job("c", "r2", stage("s", 10));
  CHECK(a.ok() && b.ok() && a.job_id != b.job_id);
  CHECK(f.w.job(a.job_id) != nullptr);
  CHECK(f.w.job("does-not-exist") == nullptr);
}

TEST(slow_cluster_stretches_new_jobs) {
  Fixture f;
  f.w.slow_cluster("c", 200);
  f.w.submit_request(request("r1", {stage("s", 1000)}));
  f.w.run_until_idle();
  CHECK(f.w.now() == Time{2000});
}

TEST(kill_node_fails_running_jobs_without_writes) {
  Fixture f;
  f.w.submit_request(request("r1", {stage("s", 1000)}));
  f.w.advance(Time{500});
  f.w.kill_node("c", 0);
  f.w.run_until_idle();

  CHECK(f.probe->events.size() == 1 && f.probe->events[0].type == JobEventType::FAILED);
  CHECK(f.w.store().count("x") == 0);
  CHECK(f.w.effect_log().empty());
  CHECK(f.w.replies().at("r1")[0].second == "failed");
  CHECK(f.w.jobs().begin()->second.state == RunState::FAILED);
}

TEST(cluster_down_rejects_then_recovers) {
  Fixture f;
  Context& ctx = f.w.context();
  f.w.fail_cluster("c");
  CHECK(ctx.submit_job("c", "r1", stage("s", 10)).status == SubmitStatus::REJECTED_CLUSTER_DOWN);
  f.w.restore_cluster("c");
  CHECK(ctx.submit_job("c", "r1", stage("s", 10)).ok());
}

TEST(kill_node_out_of_range_throws) {
  Fixture f;
  bool threw = false;
  try {
    f.w.kill_node("c", 99);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(cancel_job_gives_no_event_and_no_writes) {
  Fixture f;
  Context& ctx = f.w.context();
  auto r = ctx.submit_job("c", "r1", stage("s", 1000, 40));
  f.w.advance(Time{100});
  ctx.cancel_job(r.job_id);
  f.w.run_until_idle();

  CHECK(f.probe->events.empty());
  CHECK(f.w.effect_log().empty());
  CHECK(f.w.job(r.job_id)->state == RunState::CANCELLED);
  CHECK(ctx.submit_job("c", "r2", stage("s", 10, 40)).ok());  // memory was released
}

TEST(cancel_unknown_or_finished_job_is_noop) {
  Fixture f;
  Context& ctx = f.w.context();
  ctx.cancel_job("nope");
  auto r = ctx.submit_job("c", "r1", stage("s", 10));
  f.w.run_until_idle();
  ctx.cancel_job(r.job_id);
  CHECK(f.w.job(r.job_id)->state == RunState::COMPLETED);
}

TEST(dropped_event_still_commits_work) {
  Fixture f;
  auto r = f.w.context().submit_job("c", "r1", stage("s", 10));
  f.w.drop_next_job_event(r.job_id);
  f.w.run_until_idle();
  CHECK(f.probe->events.empty());
  CHECK(f.w.store().at("x") == Value{7});
}

TEST(duplicated_event_is_delivered_twice) {
  Fixture f;
  auto r = f.w.context().submit_job("c", "r1", stage("s", 10));
  f.w.duplicate_job_event(r.job_id);
  f.w.run_until_idle();
  CHECK(f.probe->events.size() == 2);
  CHECK(f.w.effect_log().size() == 1);  // work itself is applied once
}

TEST(crash_wipes_timers_keeps_durable_and_running_jobs) {
  Fixture f;
  Context& ctx = f.w.context();
  ctx.durable_write("k", Value{5});
  ctx.set_timer(Time{1000}, "old-timer");
  auto r = ctx.submit_job("c", "r1", stage("s", 500));
  f.w.crash_coordinator();
  f.w.context().set_timer(Time{100}, "new-timer");
  f.w.run_until_idle();

  CHECK(f.probe->recovers == 1);
  CHECK(ctx.durable_read("k") == std::optional<Value>{Value{5}});
  CHECK(f.probe->timers == std::vector<std::string>{"new-timer"});  // old generation dropped
  CHECK(f.w.job(r.job_id)->state == RunState::COMPLETED);           // job kept running
  CHECK(f.probe->events.size() == 1);                               // new coordinator hears it
}

TEST(duplicate_reply_is_flagged) {
  Fixture f;
  f.w.context().reply("r1", "a");
  f.w.context().reply("r1", "b");
  CHECK(f.w.violations().size() == 1);
}

TEST(scheduled_arrivals_run_in_time_order) {
  Fixture f;
  f.w.submit_request_at(Time{300}, request("late", {stage("s", 10)}));
  f.w.submit_request_at(Time{100}, request("early", {stage("s", 10)}));
  f.w.run_until_idle();
  CHECK((f.w.arrival_order() == std::vector<RequestId>{"early", "late"}));
  CHECK(f.w.effect_log().size() == 2);
  CHECK(f.w.effect_log()[0].effect.request_id == "early");
}

TEST(arrival_in_the_past_is_clamped_to_now) {
  Fixture f;
  f.w.advance(Time{500});
  f.w.submit_request_at(Time{10}, request("r1", {stage("s", 10)}));
  f.w.run_until_idle();
  CHECK(f.w.effect_log().at(0).time == Time{510});
}

TEST(advance_moves_clock_even_with_no_events) {
  Fixture f;
  f.w.advance(Time{777});
  CHECK(f.w.now() == Time{777});
}

TEST(runaway_timer_loop_hits_event_limit) {
  Fixture f;
  f.probe->rearm = true;
  f.w.context().set_timer(Time{1}, "loop");
  bool threw = false;
  try {
    f.w.run_until_idle(1000);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(identical_runs_produce_identical_logs) {
  auto run = [] {
    Fixture f;
    f.seed({{"x", 0}});
    for (int i = 0; i < 5; ++i)
      f.w.submit_request_at(Time{i * 37}, request("r" + std::to_string(i),
                                                  {inc_stage("a", 100 + 13 * i), inc_stage("b", 50)}));
    f.w.run_until_idle();
    std::vector<std::tuple<long long, std::string, std::string, int>> log;
    for (const auto& e : f.w.effect_log())
      log.emplace_back(e.time.count(), e.effect.request_id + "/" + e.effect.stage_id,
                       canonical(e.effect.value), e.attempt);
    return log;
  };
  CHECK(run() == run());
}

// ----------------------------------------------------------------------- main
int main() {
  for (auto& [name, fn] : registry()) {
    int before = g_failures;
    try {
      fn();
    } catch (const std::exception& e) {
      ++g_failures;
      std::cerr << "  EXCEPTION in " << name << ": " << e.what() << "\n";
    }
    std::cout << (g_failures == before ? "[ ok ] " : "[FAIL] ") << name << "\n";
  }
  std::cout << registry().size() << " tests, " << g_failures << " failed checks\n";
  return g_failures ? 1 : 0;
}