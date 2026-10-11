// serial_coordinator.h
#include "../simulation/World.h"
#include <deque>
#include <sstream>

class SerialCoordinator : public Coordinator {
 public:
  void on_request(const Request& r, Context& ctx) override {
    persist_request(r, ctx);
    int tail = geti(ctx, "seq/tail");
    ctx.durable_write("slot/" + std::to_string(tail), r.id);
    ctx.durable_write("seq/tail", tail + 1);
    pump(ctx);
  }

  void on_job_event(const JobEvent& ev, Context& ctx) override {
    // Drops duplicates and stale events from cancelled or old attempts.
    if (active_.empty() || ev.request_id != active_ || ev.job_id != cur_job_) return;
    cur_job_.clear();
    ctx.durable_write(k("job"), std::string{});

    if (ev.type == JobEventType::COMPLETED) {
      ctx.durable_write(k("next"), geti(ctx, k("next")) + 1);
      ctx.durable_write(k("att"), 0);
      dispatch(ctx);
    } else {
      retry_or_fail(ctx);
    }
  }

  void on_timer(const std::string& id, Context& ctx) override {
    // Timer ids encode request/stage/attempt so stale timers are ignored.
    if (id != tag(ctx)) return;
    if (id.rfind("to:", 0) == 0) {            // job timeout (lost event, dead node)
      if (!cur_job_.empty()) ctx.cancel_job(cur_job_);
      cur_job_.clear();
      retry_or_fail(ctx);
    } else if (id.rfind("rt:", 0) == 0) {     // backoff elapsed
      dispatch(ctx);
    }
  }

  void on_recover(Context& ctx) override {
    // The old job's fate is unknown, so cancel it and redo the stage.
    active_.clear();
    cur_job_.clear();
    int head = geti(ctx, "seq/head"), tail = geti(ctx, "seq/tail");
    if (head >= tail) return;
    active_ = *gets(ctx, "slot/" + std::to_string(head));
    load(ctx);
    if (auto j = gets(ctx, k("job")); j && !j->empty()) {
      ctx.cancel_job(*j);
      ctx.durable_write(k("job"), std::string{});
    }
    retry_or_fail(ctx);
  }

 private:
  static constexpr int kMaxAttempts = 8;
  static constexpr Time kTimeout{std::chrono::seconds(30)};

  std::string active_, cur_job_;
  Request req_;

  std::string k(const std::string& f) const { return "req/" + active_ + "/" + f; }
  std::string tag(Context& ctx) const {
    return std::to_string(geti(ctx, k("next"))) + ":" + std::to_string(geti(ctx, k("att")));
  }
  static int geti(Context& c, const std::string& key) {
    auto v = c.durable_read(key);
    return v ? std::get<int>(*v) : 0;
  }
  static std::optional<std::string> gets(Context& c, const std::string& key) {
    auto v = c.durable_read(key);
    if (!v) return std::nullopt;
    return std::get<std::string>(*v);
  }

  void pump(Context& ctx) {
    if (!active_.empty()) return;
    int head = geti(ctx, "seq/head"), tail = geti(ctx, "seq/tail");
    if (head >= tail) return;
    active_ = *gets(ctx, "slot/" + std::to_string(head));
    load(ctx);
    dispatch(ctx);
  }

  void dispatch(Context& ctx) {
    size_t i = geti(ctx, k("next"));
    if (i >= req_.stages.size()) return finish(ctx, "OK");

    SubmitResult r = ctx.submit_job(req_.stages[i].cluster, req_.id, req_.stages[i]);
    switch (r.status) {
      case SubmitStatus::OK:
        cur_job_ = r.job_id;
        ctx.durable_write(k("job"), r.job_id);
        ctx.set_timer(kTimeout, "to:" + tag(ctx));
        break;
      case SubmitStatus::REJECTED_UNKNOWN_CLUSTER:
        finish(ctx, "FAILED");                      // not retryable
        break;
      default:                                       // no capacity / cluster down
        retry_or_fail(ctx);
    }
  }

  void retry_or_fail(Context& ctx) {
    int a = geti(ctx, k("att")) + 1;
    if (a > kMaxAttempts) return finish(ctx, "FAILED");
    ctx.durable_write(k("att"), a);
    Time backoff = std::chrono::milliseconds(100) * (1 << std::min(a, 6));
    ctx.set_timer(backoff, "rt:" + tag(ctx));
  }

  void finish(Context& ctx, const std::string& result) {
    ctx.reply(req_.id, result);
    ctx.durable_write("seq/head", geti(ctx, "seq/head") + 1);
    active_.clear();
    cur_job_.clear();
    pump(ctx);
  }

  // ---- durable request storage (needed so recovery can rebuild the Request) ----
  void persist_request(const Request& r, Context& ctx) {
    std::string pre = "req/" + r.id + "/";
    ctx.durable_write(pre + "n", (int)r.stages.size());
    for (size_t i = 0; i < r.stages.size(); ++i)
      ctx.durable_write(pre + "s" + std::to_string(i), encode(r.stages[i]));
  }
  void load(Context& ctx) {
    req_ = Request{};
    req_.id = active_;
    int n = geti(ctx, k("n"));
    for (int i = 0; i < n; ++i)
      req_.stages.push_back(decode(*gets(ctx, k("s" + std::to_string(i)))));
  }
  static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (auto& x : v) s += (s.empty() ? "" : ",") + x;
    return s;
  }
  static std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out; std::string t; std::istringstream is(s);
    while (std::getline(is, t, d)) out.push_back(t);
    return out;
  }
  static std::string encode(const Stage& s) {
    std::ostringstream o;
    o << s.id << '|' << s.cluster << '|' << s.resources.cpu_cores << '|' << s.resources.ram_gb
      << '|' << s.resources.gpus << '|' << s.duration << '|' << s.regName << '|'
      << join(s.reads) << '|' << join(s.writes);
    return o.str();
  }
  static Stage decode(const std::string& e) {
    auto f = split(e, '|');
    f.resize(9);
    Stage s;
    s.id = f[0]; s.cluster = f[1];
    s.resources = {std::stoi(f[2]), std::stoi(f[3]), std::stoi(f[4])};
    s.duration = std::stof(f[5]); s.regName = f[6];
    s.reads = split(f[7], ','); s.writes = split(f[8], ',');
    return s;
  }
};