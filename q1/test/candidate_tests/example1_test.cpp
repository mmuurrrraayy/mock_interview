#include <cassert>
#include <iostream>
#include "../../simulation/World.h"
#include "../../simulation/Oracle.h"
#include "../../requests/serial_coordinator.h"

int main() {
  World w([] { return std::make_unique<SerialCoordinator>(); });
  w.add_cluster("ingest-cpu", 2, 4, 32, {"i0", "i1"});
  w.add_cluster("train-gpu",  1, 1, 80, {"g0"}, /*gpu=*/true);
  w.add_cluster("post-cpu",   2, 4, 32, {"p0", "p1"});
  w.set_initial_store({{"raw", 1}, {"counter", 0}});

  auto stage = [](std::string id, std::string cl, Resources r, float dur,
                  std::vector<std::string> rd, std::vector<std::string> wr, std::string op) {
    Stage s; s.id = id; s.cluster = cl; s.resources = r; s.duration = dur;
    s.reads = rd; s.writes = wr; s.regName = op; return s;
  };

  Request pipeline{"r1", "alice", {
  stage("ingest",  "ingest-cpu", {2, 8, 0},  2.0, {"raw"},   {"clean"},   "copy"),
  stage("train",   "train-gpu",  {4, 40, 1}, 10.0, {"clean"}, {"model"},   "hash_combine"),
  stage("publish", "post-cpu",   {1, 4, 0},  1.0, {"model"},  {"release"}, "copy"),
  }};
  Request other{"r2", "bob", {stage("bump", "ingest-cpu", {1, 4, 0}, 1.0, {"counter"}, {"counter"}, "add")}};

  w.submit_request_at(Time{0}, pipeline);
  w.submit_request_at(Time{500'000}, other);

  w.advance(std::chrono::seconds(5));
  w.fail_cluster("train-gpu");
  w.advance(std::chrono::seconds(20));
  w.restore_cluster("train-gpu");
  w.advance(std::chrono::seconds(15));
  w.crash_coordinator();
  w.run_until_idle();

  std::vector<Request> in_order;
  for (auto& id : w.arrival_order()) in_order.push_back(w.requests().at(id));
  auto oracle = run_oracle(in_order, {{"raw", 1}, {"counter", 0}});
  if (!oracle) {
    std::cerr << "oracle error: " << oracle.error() << "\n";
    return 1;
  }

  assert(oracle.has_value());
  assert(w.violations().empty());
  assert(w.store() == oracle->final_store);

  std::vector<Effect> actual;
  for (auto& te : w.effect_log()) actual.push_back(te.effect);
  assert(per_key_effects(actual) == per_key_effects(oracle->effect_log));
  for (auto& [id, rep] : oracle->replies) assert(w.replies().at(id).size() == 1);

  std::cout << "scenario passed\n";
}