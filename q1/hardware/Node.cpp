#include "Node.h"

#include <thread>

std::optional<Node::Rejection> Node::checkCapacity() {
    if (capacity() >= 100) return Rejection::NoCapacity;
    return std::nullopt;
}

std::optional<Node::Rejection> Node::checkRateLimit() {
    const auto now = std::chrono::steady_clock::now();
    while (!recentJobs_.empty() && now - recentJobs_.front() > kWindow)
        recentJobs_.pop_front();

    if (recentJobs_.size() >= kMaxJobsPerWindow) return Rejection::RateLimit;
    return std::nullopt;
}

std::optional<Node::Rejection> Node::queueJob(Job& job) { 
    auto rejection = checkCapacity()
        .or_else([this] { return checkRateLimit(); });
    if (!rejection) { 
        recentJobs_.push_back(std::chrono::steady_clock::now());
        Node::q_.push_back(job);
    }
    return rejection;
}

using namespace std::chrono_literals;

void Node::doJob(){
    auto& job = Node::q_.front(); 
    Node::q_.pop_front();

    std::this_thread::sleep_for(std::chrono::milliseconds(job.duration()));
}