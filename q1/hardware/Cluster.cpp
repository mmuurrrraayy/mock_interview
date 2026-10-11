#include "Cluster.h"

std::optional<Cluster::ClusterRejection> Cluster::findNodeAndSubmit(const Job job) {
    for (auto& node : nodes_) {
        if (!node.queueJob(std::move(job))) return std::nullopt;  // no rejection => accepted
    }
    return ClusterRejection::NoFreeNodes;
}