#ifndef JOB_H
#define JOB_H

enum class JobState;

class Job{
    public:
        Job(int id, bool processor, int memory, long long duration):
        id_(id), processor_(processor),memory_(memory),duration_(duration){}

         int id() const {return id_;}
        const JobState& state() const {return state_;}
         long long duration() const {return duration_;}

    private:
        int id_;
        JobState state_;

        /*resources needed*/
        bool processor_; /*1: GPU, 0:CPU*/
        int memory_; /*GB*/
        long long duration_;
};

enum class JobState{
    Pending,
    inProgress,
    Complete,
    Failed
};

#endif 