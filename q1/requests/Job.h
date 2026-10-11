#ifndef JOB_H
#define JOB_H

#include <chrono>


class Job{
    public:
        Job(std::string id,bool processor, int memory, std::chrono::microseconds duration,std::pair<int,int> dependents):
        id_(id),processor_(processor),memory_(memory),duration_(duration),dependents_(dependents){}

        std::string id() const {return id_;}

         std::chrono::microseconds duration() const {return duration_;}
         int memory() const {return memory_;}
         bool processor() const { return processor_; } 
         std::pair<int,int> dependents() const { return dependents_; }

    private:
        std::string id_;

        /*resources needed*/
        bool processor_; /*1: GPU, 0:CPU*/
        int memory_; /*GB*/
        std::chrono::microseconds duration_;
        std::pair<int,int> dependents_; /*0 if no dependent, at most 2 dependents*/
};



#endif 