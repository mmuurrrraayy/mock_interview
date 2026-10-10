#ifndef NODE_H
#define NODE_H

#include "Hardware.h"
#include "../requests/Job.h"

#include <cstdint>
#include <utility>
#include <variant>
#include <vector>
#include <string>
#include <optional>
#include <chrono>
#include<deque>
#include <expected>
#include <queue>

class Node {
public:
    Node(std::vector<CPU> cpus, int storage, int memory,int id,bool online,int rateLimit)
        : processors_(std::move(cpus)),  storage_(storage), memory_(memory),id_(id),online_(online),rateLimit_(rateLimit) {}

    Node(std::vector<GPU> gpus, int storage, int memory,int id,bool online,int rateLimit)
        : processors_(std::move(gpus)),  storage_(storage), memory_(memory),id_(id),online_(online), rateLimit_(rateLimit){}

    enum class Rejection{
        NoCapacity,
        RateLimit
    };

    uint8_t capacity() const {return capacity_;}
    int rateLimit() const {return rateLimit_;}
    std::optional<Rejection> checkCapacity();
    std::optional<Rejection> checkRateLimit();

    JobState executeJob(Job& job);
    std::optional<Rejection> queueJob(Job& job); 
    void doJob();

private:
    std::variant<std::vector<CPU>,
                 std::vector<GPU>> processors_;
    int storage_;
    int memory_;
    int id_;
    bool online_;
    uint8_t capacity_{0}; /*100 is max*/
    int rateLimit_;

    std::deque<std::chrono::steady_clock::time_point> recentJobs_;
    std::deque<Job> q_;
    static constexpr size_t kMaxJobsPerWindow = 100;
    static constexpr std::chrono::seconds kWindow{1};
   
};

#endif /* NODE_H */