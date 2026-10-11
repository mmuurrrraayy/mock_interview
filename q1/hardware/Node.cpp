#include "Node.h"

#include <mutex>
#include <thread>

std::optional<Node::Rejection> Node::checkCapacity() {
  if (capacity() >= 100)
    return Rejection::NoCapacity;
  return std::nullopt;
}

std::optional<Node::Rejection> Node::checkRateLimit() {
  const auto now = std::chrono::steady_clock::now();
  while (!recentJobs_.empty() && now - recentJobs_.front() > kWindow)
    recentJobs_.pop_front();

  if (recentJobs_.size() >= kMaxJobsPerWindow)
    return Rejection::RateLimit;
  return std::nullopt;
}

std::optional<Node::Rejection> Node::checkMemory(const Job& job) {
  if (memUsed() + job.memory()>= memory())
    return Rejection::NoMemCapacity;
  return std::nullopt;
}

std::optional<Node::Rejection> Node::queueJob(Job job) {
  {
    std::lock_guard<std::mutex> lock(m_);
    if (auto rejection =
            checkCapacity()
                .or_else([this] { return checkRateLimit(); })
                .or_else([this, &job] { return checkMemory(job); })) {
      return rejection;
    }
    recentJobs_.push_back(std::chrono::steady_clock::now());
    memory_used_ += job.memory();
    q_.push_back(std::move(job));
  }

  cv_.notify_one();
  return std::nullopt;
}

using namespace std::chrono_literals;

void Node::doJob() {
  while (auto job = waitForJob()) {
    std::this_thread::sleep_for(job->duration());
  }
}


std::optional<Job> Node::waitForJob() {
  std::unique_lock<std::mutex> lock(m_);
  cv_.wait(lock, [this] { return !q_.empty() || stopped_; });
  if (q_.empty()) {
    return std::nullopt;
  }
  Job job = std::move(q_.front());
  q_.pop_front();
  return job;
}

void Node::stop() {
  {
    std::lock_guard<std::mutex> lock(m_);
    stopped_ = true;
  }
  cv_.notify_all();
}

size_t Node::slots() const {
  return std::visit([](const auto& v) { return v.size(); }, processors_);
}
bool Node::isGpu() const { return std::holds_alternative<std::vector<GPU>>(processors_); }


void Node::release(const Job& job) {
  memory_used_ -= job.memory();
}