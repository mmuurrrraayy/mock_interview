#ifndef CLUSTER_H
#define CLUSTER_H

#include "Node.h"
#include <vector>
#include <string>

class Cluster{
    public:
        Cluster(std::vector<Node>nodes,std::string name,int admRate):
            nodes_(nodes),name_(name),admRate_(admRate){}

            std::string_view name(){ return name_;}
            const std::vector<Node>& nodes() const {return nodes_;}

    private:
        std::vector<Node> nodes_;
        std::string name_;
        int admRate_;

};


#endif /*Cluster_h*/