#include "model.h"

#include <stdexcept>
#include <unordered_map>

namespace {

std::string plain(const Value& v) {
    if (auto p = std::get_if<int64_t>(&v)) return std::to_string(*p);
    return std::get<std::string>(v);
}

std::vector<Value> broadcast(Value v, size_t n) { return std::vector<Value>(n, std::move(v)); }

// Sum of integer inputs (strings count as 0). Wraps on overflow, never throws.
std::vector<Value> op_add(const std::vector<Value>& in, const Stage& st) {
    uint64_t sum = 0;
    for (const auto& v : in)
        if (auto p = std::get_if<int64_t>(&v)) sum += static_cast<uint64_t>(*p);
    return broadcast(static_cast<int>(sum), st.writes.size());
}

// Concatenation of inputs' plain text, in read order.
std::vector<Value> op_concat(const std::vector<Value>& in, const Stage& st) {
    std::string s;
    for (const auto& v : in) s += plain(v);
    return broadcast(std::move(s), st.writes.size());
}

// FNV-1a 64 over the stage id and canonical inputs.
std::vector<Value> op_hash_combine(const std::vector<Value>& in, const Stage& st) {
    constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t h = 1469598103934665603ULL;
    auto feed = [&](const std::string& s) {
        for (unsigned char c : s) h = (h ^ c) * kPrime;
        h *= kPrime;  // separator (an FNV step with a zero byte)
    };
    feed(st.id);
    for (const auto& v : in) feed(canonical(v));
    return broadcast(static_cast<int>(h), st.writes.size());
}

// writes[i] = reads[i % reads.size()]; with no reads, writes the default value.
std::vector<Value> op_copy(const std::vector<Value>& in, const Stage& st) {
    std::vector<Value> out;
    out.reserve(st.writes.size());
    for (size_t i = 0; i < st.writes.size(); ++i)
        out.push_back(in.empty() ? default_value() : in[i % in.size()]);
    return out;
}

const std::unordered_map<std::string, Op>& registry() {
    static const std::unordered_map<std::string, Op> r = {
        {"add", op_add}, {"concat", op_concat},
        {"hash_combine", op_hash_combine}, {"copy", op_copy},
    };
    return r;
}

}  // namespace

std::string canonical(const Value& v) {
    return (std::holds_alternative<int>(v) ? "i:" : "s:") + plain(v);
}

std::optional<std::reference_wrapper<const Op>> find_op(const std::string& name) {
    auto it = registry().find(name);
    if (it == registry().end()) return std::nullopt;
    return std::cref(it->second);
}

const Op& lookup_op(const std::string& name) {
    if (auto op = find_op(name)) return *op;
    throw std::out_of_range("unknown op: " + name);
}