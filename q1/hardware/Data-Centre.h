#ifndef DATA_CENTRE_H
#define DATA_CENTRE_H

#include "Cluster.h"
#include "../requests/Job.h"
#include <set>
#include <vector>

class DataCentre{
    public:
        DataCentre(std::vector<Cluster>clusters):
            clusters_(clusters){}

        bool updateJobs();
        const std::set<Job>& jobs() const { return jobs_; }

    private:
        std::vector<Cluster>clusters_;
        std::set<Job> jobs_;
        
};

#endif /*Data_centre_h*/