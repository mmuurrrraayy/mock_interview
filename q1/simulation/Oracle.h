#ifndef ORACLE_H
#define ORACLE_H

#include "Model.h"
#include <string>
#include <expected>

struct Effect {
    RequestId request_id;
    std::string stage_id;
    std::string key;
    Value value;
    bool operator==(const Effect&) const = default; 
};

struct Reply {
    // Last value this request wrote to each key it wrote.
    std::map<std::string, Value> writes;
    bool operator==(const Reply&) const = default;
};
 
struct OracleResult {
    Store final_store;
    std::vector<Effect> effect_log; // global order = execution order
    std::map<RequestId, Reply> replies;
};

constexpr int ORACLE_DUPLICATE_REQUEST = -1;
constexpr int ORACLE_UNKNOWN_OP        = -2;
constexpr int ORACLE_BAD_OUTPUT_COUNT  = -3;

std::map<std::string, std::vector<Effect>> per_key_effects(const std::vector<Effect>& log);

/*runs every request's stages in order of arrival against a copy of the initial store*/
std::expected<OracleResult,int> run_oracle(const std::vector<Request>& requests_in_arrival_order,
                        Store initial_store);
/*compare oracle and candidate*/
std::map<std::string, std::vector<Value>> per_key_writes(const std::vector<Effect>& log);

#endif /*ORACLE_H*/