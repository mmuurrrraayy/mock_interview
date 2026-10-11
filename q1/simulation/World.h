// world.h -- deterministic simulated world, built on Cluster/Node/Job.
//
// Determinism rules:
//  * integer microsecond time; no <chrono> clocks, no rand()
//  * events ordered by (time, seq)
//  * only ordered containers are iterated
//  * Jobs are admitted through Cluster::findNodeAndSubmit and freed with Node::release.
//    Completion is a JOB_DONE event. Node has no online/offline state, so the world
//    tracks down nodes itself (down_).
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../hardware/Cluster.h"
#include "../simulation/Model.h"
#include "../simulation/Oracle.h" 

using Time = std::chrono::microseconds; 


struct TimedEffect {
  Time time{0};
  Effect effect;
  int attempt = 0;
};


class Context {
 public:
  virtual ~Context() = default;
  virtual Time now() const = 0;
  virtual SubmitResult submit_job(const std::string& cluster, const RequestId& request_id,
                                  const Stage& stage) = 0;
  virtual void cancel_job(const JobId& id) = 0;
  virtual void set_timer(Time delay, const std::string& timer_id) = 0;
  virtual void durable_write(const std::string& k, const Value& v) = 0;
  virtual std::optional<Value> durable_read(const std::string& k) const = 0;
  virtual Store read_store(const std::vector<std::string>& keys) const = 0;
  virtual void reply(const RequestId& id, const std::string& result) = 0;
};

class Coordinator {
 public:
  virtual ~Coordinator() = default;
  virtual void on_request(const Request& r, Context& ctx) = 0;
  virtual void on_job_event(const JobEvent& ev, Context& ctx) = 0;
  virtual void on_timer(const std::string& timer_id, Context& ctx) = 0;
  virtual void on_recover(Context& ctx) = 0; 
};


enum class RunState { RUNNING, COMPLETED, FAILED, CANCELLED };

struct JobRecord {
  JobId id;
  RequestId request_id;
  Stage stage;
  Job job; 
  std::string cluster;
  size_t node_index = 0;
  Time start{0};
  Time end{0};
  RunState state = RunState::RUNNING;
  int attempt = 0;  
  Store snapshot;   
};


class World {
 public:
  using Factory = std::function<std::unique_ptr<Coordinator>()>;

  explicit World(Factory factory);
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  /*setup*/
  void add_cluster(const std::string& name, std::vector<Node> nodes, int adm_rate = 0);
  void add_cluster(const std::string& name, int n_nodes, int slots, int mem_gb,std::vector<std::string> node_ids,bool gpu = false);
  void set_initial_store(Store s) { store_ = std::move(s); }

  void submit_request(const Request& r) { arrive(r); }
  void submit_request_at(Time t, const Request& r);
  void advance(Time dt);
  void run_until_idle(uint64_t max_events = 10'000'000);

  Context& context() { return ctx_; }

  void kill_node(const std::string& cluster, size_t idx);
  void restart_node(const std::string& cluster, size_t idx);
  void fail_cluster(const std::string& name);
  void restore_cluster(const std::string& name);

  void crash_coordinator();

  void drop_next_job_event(const JobId& id) { drop_.insert(id); }  
  void duplicate_job_event(const JobId& id) { dup_.insert(id); }  
  void slow_cluster(const std::string& name, int pct) {            // affects jobs submitted AFTER
    slow_pct_[name] = pct;
  }

  Time now() const { return now_; }
  const Store& store() const { return store_; }
  const std::vector<TimedEffect>& effect_log() const { return effect_log_; }
  const Store& durable() const { return durable_; }
  const std::vector<RequestId>& arrival_order() const { return arrival_order_; }
  const std::map<RequestId, Request>& requests() const { return requests_; }
  const std::map<RequestId, std::vector<std::pair<Time, std::string>>>& replies() const {
    return replies_;
  }
  const std::vector<std::string>& violations() const { return violations_; }
  const JobRecord* job(const JobId& id) const {
    auto it = jobs_.find(id);
    return it == jobs_.end() ? nullptr : &it->second;
  }
  const std::map<JobId, JobRecord>& jobs() const { return jobs_; }
  const Cluster& cluster(const std::string& name) const { return clusters_.at(name); }

 private:
  enum class EvKind { JOB_DONE, DELIVER, TIMER, ARRIVAL };

  struct Event {
    Time t{0};
    uint64_t seq = 0;
    EvKind kind = EvKind::JOB_DONE;
    JobId job;
    JobEvent je;
    std::string timer;
    uint64_t gen = 0;
    size_t idx = 0;
  };
  struct Later {
    bool operator()(const Event& a, const Event& b) const {
      return std::tie(a.t, a.seq) > std::tie(b.t, b.seq);
    }
  };

  class CtxImpl : public Context {
   public:
    explicit CtxImpl(World& w) : w_(w) {}
    Time now() const override;
    SubmitResult submit_job(const std::string& cluster, const RequestId& rid,
                            const Stage& stage) override;
    void cancel_job(const JobId& id) override;
    void set_timer(Time delay, const std::string& id) override;
    void durable_write(const std::string& k, const Value& v) override;
    std::optional<Value> durable_read(const std::string& k) const override;
    Store read_store(const std::vector<std::string>& keys) const override;
    void reply(const RequestId& id, const std::string& result) override;

   private:
    World& w_;
  };


  Event make_event(Time t, EvKind k);
  void require_node(const std::string& cluster, size_t idx) const;
  bool all_down(const std::string& cluster) const;
  Node& job_node(const JobRecord& j);
  const Node& job_node(const JobRecord& j) const;
  Store snapshot(const std::vector<std::string>& keys) const;
  void arrive(const Request& r);

  SubmitResult do_submit(const std::string& cname, const RequestId& rid, const Stage& stage);
  void finish(JobRecord& j);
  void enqueue_delivery(const JobRecord& j, JobEventType type);
  void fail_job(const JobId& id);
  void do_cancel(const JobId& id);
  void apply_effects(const JobRecord& j);  
  void on_job_done(const JobId& id);
  bool step();
  void check_invariants() const; 


  Factory factory_;
  CtxImpl ctx_;
  std::unique_ptr<Coordinator> coordinator_;
  uint64_t generation_ = 0;

  Time now_{0};
  uint64_t next_seq_ = 0;
  uint64_t next_job_id_ = 1;
  int next_node_id_ = 0;
  std::priority_queue<Event, std::vector<Event>, Later> q_;

  std::map<std::string, Cluster> clusters_;
  std::set<std::pair<std::string, size_t>> down_;  
  std::map<std::string, std::vector<int>> mem_base_;  /*should be 0*/
  std::map<std::string, int> slow_pct_;  

  std::map<JobId, JobRecord> jobs_;
  std::set<JobId> running_;
  std::map<std::pair<RequestId, std::string>, int> attempts_;

  Store store_;
  std::vector<TimedEffect> effect_log_;
  Store durable_;

  std::vector<Request> arrivals_;
  std::vector<RequestId> arrival_order_;
  std::map<RequestId, Request> requests_;
  std::map<RequestId, std::vector<std::pair<Time, std::string>>> replies_;
  std::vector<std::string> violations_;

  std::set<JobId> drop_;
  std::set<JobId> dup_;
};