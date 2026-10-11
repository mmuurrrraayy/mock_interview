#include "Oracle.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace {

std::optional<Value> read_key(const Store& store, const std::string& k) {
  auto it = store.find(k);
  return it == store.end() ? std::nullopt : std::optional<Value>{it->second};
}

std::vector<Value> get_inputs(const Store& store, const Stage& st) {
  std::vector<Value> in;
  in.reserve(st.reads.size());
  for (const auto& key : st.reads) {
    in.push_back(read_key(store, key).value_or(default_value()));
  }
  return in;
}

std::expected<std::vector<Value>, int> run_stage(const Store& store, const Stage& st) {
  auto op = find_op(st.regName);
  if (!op) {
    return std::unexpected(ORACLE_UNKNOWN_OP);
  }

  std::vector<Value> out = (*op)(get_inputs(store, st), st);

  if (out.size() != st.writes.size()) {
    return std::unexpected(ORACLE_BAD_OUTPUT_COUNT);
  }
  return out;
}

}  // namespace

std::expected<OracleResult, int> run_oracle(const std::vector<Request>& requests_in_arrival_order,
                                            Store initial_store) {
  OracleResult res;
  res.final_store = std::move(initial_store);

  std::set<RequestId> seen;
  for (const auto& req : requests_in_arrival_order) {
    if (!seen.insert(req.id).second) {
      return std::unexpected(ORACLE_DUPLICATE_REQUEST);
    }

    Reply reply;
    for (const auto& stage : req.stages) {
      auto outputs = run_stage(res.final_store, stage);
      if (!outputs) {
        return std::unexpected(outputs.error());
      }

      for (std::size_t i = 0; i < stage.writes.size(); ++i) {
        const std::string& key = stage.writes[i];
        res.final_store[key] = (*outputs)[i];
        res.effect_log.push_back({req.id, stage.id, key, (*outputs)[i]});
        reply.writes[key] = (*outputs)[i];
      }
    }
    res.replies[req.id] = std::move(reply);
  }
  return res;
}

std::map<std::string, std::vector<Value>> per_key_writes(const std::vector<Effect>& log) {
  std::map<std::string, std::vector<Value>> out;
  for (const auto& effect : log) {
    out[effect.key].push_back(effect.value);
  }
  return out;
}

std::map<std::string, std::vector<Effect>> per_key_effects(const std::vector<Effect>& log) {
  std::map<std::string, std::vector<Effect>> out;
  for (const auto& effect : log) {
    out[effect.key].push_back(effect);
  }
  return out;
}