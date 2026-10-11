#ifndef CLUSTER_H
#define CLUSTER_H

#include "Node.h"
#include <vector>
#include <string>

class Cluster{
    public:
        Cluster(std::vector<Node>nodes,std::string name,int admRate):
            nodes_(std::move(nodes)),
            name_(std::move(name)),
            admRate_(admRate) {}

        enum class ClusterRejection { NoFreeNodes };

        std::string_view name() { return name_; }
        std::vector<Node>& nodes() { return nodes_; }
        const std::vector<Node>& nodes() const { return nodes_; }   

        std::optional<ClusterRejection> findNodeAndSubmit(const Job job);

      private:
        std::vector<Node> nodes_;
        std::string name_;
        int admRate_;

};


#endif /*Cluster_h*/