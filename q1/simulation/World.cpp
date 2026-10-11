
#include "World.h"

#include <algorithm>
#include <cmath>

namespace {

Job make_job(const RequestId& rid, const Stage& s, Time dur) {
  return Job(rid + "/" + s.id, s.resources.gpus > 0, s.resources.ram_gb, dur, {0, 0});
}

std::optional<size_t> submit_to_cluster(Cluster& c, const Job& job) {
  std::vector<int> before;
  for (const Node& n : c.nodes()) before.push_back(n.memUsed());
  if (c.findNodeAndSubmit(job)) return std::nullopt;  // rejected
  for (size_t i = 0; i < c.nodes().size(); ++i)
    if (c.nodes()[i].memUsed() != before[i]) return i;
  throw std::logic_error("findNodeAndSubmit accepted a job but no node's memUsed() changed");
}

}  /*namespace*/

World::World(Factory factory) : factory_(std::move(factory)), ctx_(*this) {
  coordinator_ = factory_();
}

void World::add_cluster(const std::string& name, std::vector<Node> nodes, int adm_rate) {
  std::vector<int> base;  
  for (const Node& n : nodes) base.push_back(n.memUsed());
  mem_base_[name] = std::move(base);
  clusters_.emplace(name, Cluster(std::move(nodes), name, adm_rate));
}

void World::add_cluster(const std::string& name, int n_nodes, int slots, int mem_gb,std::vector<std::string> node_ids, bool gpu) {
  std::vector<Node> nodes;
  for (int i = 0; i < n_nodes; ++i) {
    if (gpu)
      nodes.emplace_back(std::vector<GPU>(slots), 0, mem_gb, node_ids[i], true, 0);
    else
      nodes.emplace_back(std::vector<CPU>(slots), 0, mem_gb, node_ids[i], true, 0);
  }
  add_cluster(name, std::move(nodes));
}

void World::submit_request_at(Time t, const Request& r) {
  arrivals_.push_back(r);
  Event e = make_event(t < now_ ? now_ : t, EvKind::ARRIVAL);
  e.idx = arrivals_.size() - 1;
  q_.push(e);
}

void World::advance(Time dt) {
  Time target = now_ + dt;
  while (!q_.empty() && q_.top().t <= target) step();
  now_ = target;
}

void World::run_until_idle(uint64_t max_events) {
  uint64_t n = 0;
  while (step()) {
    if (++n > max_events) throw std::runtime_error("run_until_idle: event limit exceeded");
  }
}

void World::kill_node(const std::string& cluster, size_t idx) {
  require_node(cluster, idx);
  down_.insert({cluster, idx});
  std::vector<JobId> victims;
  for (const JobId& id : running_) {  
    const JobRecord& j = jobs_.at(id);
    if (j.cluster == cluster && j.node_index == idx) victims.push_back(id);
  }
  for (const JobId& id : victims) fail_job(id);
  check_invariants();
}

void World::restart_node(const std::string& cluster, size_t idx) {
  require_node(cluster, idx);
  down_.erase({cluster, idx});
}

void World::fail_cluster(const std::string& name) {
  for (size_t i = 0; i < clusters_.at(name).nodes().size(); ++i) kill_node(name, i);
}

void World::restore_cluster(const std::string& name) {
  for (size_t i = 0; i < clusters_.at(name).nodes().size(); ++i) restart_node(name, i);
}

void World::crash_coordinator() {
  ++generation_;
  coordinator_ = factory_();
  coordinator_->on_recover(ctx_);
}

Time World::CtxImpl::now() const { return w_.now_; }

SubmitResult World::CtxImpl::submit_job(const std::string& cluster, const RequestId& rid,
                                        const Stage& stage) {
  return w_.do_submit(cluster, rid, stage);
}

void World::CtxImpl::cancel_job(const JobId& id) { w_.do_cancel(id); }

void World::CtxImpl::set_timer(Time delay, const std::string& id) {
  Event e = w_.make_event(w_.now_ + std::max(delay, Time{0}), EvKind::TIMER);
  e.timer = id;
  e.gen = w_.generation_;
  w_.q_.push(e);
}

void World::CtxImpl::durable_write(const std::string& k, const Value& v) { w_.durable_[k] = v; }

std::optional<Value> World::CtxImpl::durable_read(const std::string& k) const {
  auto it = w_.durable_.find(k);
  if (it == w_.durable_.end()) return std::nullopt;
  return it->second;
}

Store World::CtxImpl::read_store(const std::vector<std::string>& keys) const {
  return w_.snapshot(keys);
}

void World::CtxImpl::reply(const RequestId& id, const std::string& result) {
  auto& v = w_.replies_[id];
  v.emplace_back(w_.now_, result);
  if (v.size() > 1) w_.violations_.push_back("duplicate reply for request " + id);
}


World::Event World::make_event(Time t, EvKind k) {
  Event e;
  e.t = t;
  e.seq = next_seq_++;
  e.kind = k;
  return e;
}

void World::require_node(const std::string& cluster, size_t idx) const {
  auto it = clusters_.find(cluster);
  if (it == clusters_.end() || idx >= it->second.nodes().size())
    throw std::invalid_argument("unknown node " + cluster + "[" + std::to_string(idx) + "]");
}

bool World::all_down(const std::string& cluster) const {
  size_t n = clusters_.at(cluster).nodes().size();
  for (size_t i = 0; i < n; ++i)
    if (!down_.count({cluster, i})) return false;
  return n > 0;
}

Node& World::job_node(const JobRecord& j) {
  return clusters_.at(j.cluster).nodes()[j.node_index];
}

const Node& World::job_node(const JobRecord& j) const {
  return clusters_.at(j.cluster).nodes()[j.node_index];
}

Store World::snapshot(const std::vector<std::string>& keys) const {
  Store s;
  for (const auto& k : keys) {
    auto it = store_.find(k);
    if (it != store_.end()) s[k] = it->second;
  }
  return s;
}

void World::arrive(const Request& r) {
  arrival_order_.push_back(r.id);
  requests_[r.id] = r;
  coordinator_->on_request(r, ctx_);
}

SubmitResult World::do_submit(const std::string& cname, const RequestId& rid,
                              const Stage& stage) {
  auto it = clusters_.find(cname);
  if (it == clusters_.end()) return {SubmitStatus::REJECTED_UNKNOWN_CLUSTER, ""};
  Cluster& c = it->second;
  if (all_down(cname)) return {SubmitStatus::REJECTED_CLUSTER_DOWN, ""};

  auto sp = slow_pct_.find(cname);
  Time dur{std::llround(stage.duration) * (sp == slow_pct_.end() ? 100 : sp->second) / 100};
  Job job = make_job(rid, stage, dur);

  auto placed = submit_to_cluster(c, job);
  if (!placed) return {SubmitStatus::REJECTED_NO_CAPACITY, ""};
  if (down_.count({cname, *placed})) { 
    c.nodes()[*placed].release(job);
    return {SubmitStatus::REJECTED_NO_CAPACITY, ""};
  }

  JobId id = std::to_string(next_job_id_++);
  JobRecord rec{.id = id,
                .request_id = rid,
                .stage = stage,
                .job = std::move(job),
                .cluster = cname,
                .node_index = *placed,
                .start = now_,
                .end = now_ + dur,
                .state = RunState::RUNNING,
                .attempt = ++attempts_[{rid, stage.id}],
                .snapshot = snapshot(stage.reads)};  // reads captured at START

  Event e = make_event(rec.end, EvKind::JOB_DONE);
  e.job = id;
  q_.push(e);

  jobs_.emplace(id, std::move(rec));
  running_.insert(id);
  return {SubmitStatus::OK, id};
}

void World::finish(JobRecord& j) {
  job_node(j).release(j.job);
  running_.erase(j.id);
}

void World::enqueue_delivery(const JobRecord& j, JobEventType type) {
  Event e = make_event(now_, EvKind::DELIVER);
  e.je = {j.id, j.request_id, j.stage.id, type};
  e.gen = generation_;
  q_.push(e);
}

void World::fail_job(const JobId& id) {
  JobRecord& j = jobs_.at(id);
  if (j.state != RunState::RUNNING) return;
  finish(j);
  j.state = RunState::FAILED;  
  enqueue_delivery(j, JobEventType::FAILED);
}

void World::do_cancel(const JobId& id) {
  auto it = jobs_.find(id);
  if (it == jobs_.end() || it->second.state != RunState::RUNNING) return;
  finish(it->second);
  it->second.state = RunState::CANCELLED;  
}

void World::apply_effects(const JobRecord& j) {
  const Stage& s = j.stage;
  auto op = find_op(s.regName);
  if (!op) {
    violations_.push_back("unknown op '" + s.regName + "' in stage " + s.id);
    return;
  }
  std::vector<Value> in;
  for (const auto& k : s.reads) {
    auto it = j.snapshot.find(k);
    if (it == j.snapshot.end()) {
      violations_.push_back("stage " + s.id + " of " + j.request_id +
                            " started with missing input '" + k + "'");
      return;
    }
    in.push_back(it->second);
  }
  std::vector<Value> out = op->get()(in, s);
  if (out.size() != s.writes.size()) {
    violations_.push_back("op '" + s.regName + "' returned " + std::to_string(out.size()) +
                          " values for " + std::to_string(s.writes.size()) + " writes");
    return;
  }
  for (size_t i = 0; i < out.size(); ++i) {
    store_[s.writes[i]] = out[i];
    effect_log_.push_back({now_, Effect{j.request_id, s.id, s.writes[i], out[i]}, j.attempt});
  }
}

void World::on_job_done(const JobId& id) {
  JobRecord& j = jobs_.at(id);
  if (j.state != RunState::RUNNING) return;  
  finish(j);
  apply_effects(j);
  j.state = RunState::COMPLETED;
  enqueue_delivery(j, JobEventType::COMPLETED);
}

bool World::step() {
  if (q_.empty()) return false;
  Event e = q_.top();
  q_.pop();
  if (e.t > now_) now_ = e.t;
  switch (e.kind) {
    case EvKind::JOB_DONE:
      on_job_done(e.job);
      break;
    case EvKind::DELIVER:
      if (e.gen != generation_) break;     
      if (drop_.erase(e.je.job_id)) break; 
      coordinator_->on_job_event(e.je, ctx_);
      if (dup_.erase(e.je.job_id)) coordinator_->on_job_event(e.je, ctx_);
      break;
    case EvKind::TIMER:
      if (e.gen != generation_) break;
      coordinator_->on_timer(e.timer, ctx_);
      break;
    case EvKind::ARRIVAL:
      arrive(arrivals_[e.idx]);
      break;
  }
  check_invariants();
  return true;
}

void World::check_invariants() const {
  std::map<std::pair<std::string, size_t>, int> mem;  
  for (const JobId& id : running_) {
    const JobRecord& j = jobs_.at(id);
    if (down_.count({j.cluster, j.node_index}))
      throw std::logic_error("running job on down node " + j.cluster + "[" +
                             std::to_string(j.node_index) + "]");
    mem[{j.cluster, j.node_index}] += j.job.memory();
  }
  for (const auto& [cname, c] : clusters_) {
    const std::vector<int>& base = mem_base_.at(cname);
    for (size_t i = 0; i < c.nodes().size(); ++i) {
      auto it = mem.find({cname, i});
      int held = it == mem.end() ? 0 : it->second;
      if (c.nodes()[i].memUsed() - base[i] != held)
        throw std::logic_error("memory drift on node " + cname + "[" + std::to_string(i) + "]");
    }
  }
}