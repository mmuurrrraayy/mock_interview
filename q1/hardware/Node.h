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
#include <condition_variable>
#include <mutex>


class Node {
public:
    Node(std::vector<CPU> cpus, int storage, int memory,std::string id,bool online,int rateLimit)
        : processors_(std::move(cpus)),  storage_(storage), memory_(memory),id_(id),online_(online),rateLimit_(rateLimit) {
           
        }

    Node(std::vector<GPU> gpus, int storage, int memory,std::string id,bool online,int rateLimit)
        : processors_(std::move(gpus)),  storage_(storage), memory_(memory),id_(id),online_(online), rateLimit_(rateLimit){
        }

    Node(Node&& o) noexcept
    : processors_(std::move(o.processors_)), storage_(o.storage_), memory_(o.memory_),
      memory_used_(o.memory_used_), id_(o.id_), online_(o.online_),
      pro_capacity_(o.pro_capacity_), rateLimit_(o.rateLimit_),
      recentJobs_(std::move(o.recentJobs_)), q_(std::move(o.q_)),
      stopped_(o.stopped_) {}

    enum class Rejection{
        NoCapacity,
        RateLimit,
        NoMemCapacity
    };

    uint8_t capacity() const {return pro_capacity_;}
    int rateLimit() const {return rateLimit_;}
    int memUsed() const {return memory_used_;}
    int memory() const{return memory_;}
    size_t slots() const;
    bool isGpu() const;

    std::optional<Rejection> checkCapacity();
    std::optional<Rejection> checkRateLimit();
    std::optional<Rejection> checkMemory(const Job& job);

    void doJob(Job& job);
    std::optional<Rejection> queueJob(Job job); 
    void doJob();
    std::optional<Job> waitForJob();
    void stop();
    void release(const Job& job);

private:
    std::variant<std::vector<CPU>,
                 std::vector<GPU>> processors_;
    int storage_;
    int memory_; /*total memory available*/
    int memory_used_=0;
    std::string id_;
    bool online_;
    uint8_t pro_capacity_{0}; /*100% is max*/
    int rateLimit_;

    std::deque<std::chrono::steady_clock::time_point> recentJobs_;
    std::deque<Job> q_;
    static constexpr size_t kMaxJobsPerWindow = 100;
    static constexpr std::chrono::seconds kWindow{1};
    std::condition_variable cv_;
    std::mutex m_;
    bool stopped_;
 
   
};

#endif /* NODE_H */