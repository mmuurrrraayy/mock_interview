#ifndef MODEL_H
#define MODEL_H

#include "Job.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using Value = std::variant<int, std::string>;
using RequestId = std::string;
using Store = std::map<std::string, Value>;

struct Resources{
    int cpu_cores{0};
    int ram_gb{0};
    int gpus{0};
};

struct Stage{
    std::string id;
    std::string cluster;
    Resources resources;
    float duration{0};
    std::vector<std::string> reads;
    std::vector<std::string> writes;
    std::string regName;
};

struct Request{
    RequestId id;
    std::string owner;
    std::vector<Stage> stages;
    bool atomic{false};
};

using Op = std::function<std::vector<Value>(const std::vector<Value>& inputs,
            const Stage& stage)>;

std::optional<std::reference_wrapper<const Op>> find_op(const std::string& name);
std::string canonical(const Value& v); /*ai slop*/

#endif /*MODEL_H*/