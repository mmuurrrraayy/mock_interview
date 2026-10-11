
// GOAL
//   Run every Request to completion across the clusters and reply to each
//   exactly once, despite node/cluster failures, lost or duplicated job
//   events, and coordinator crashes.
//
// WHAT "CORRECT" MEANS: worked example
//   (Assume op "add1" writes input + 1 for each key a stage writes.)
//
//   Initial store: { a:1, x:0 }
//   Arrivals, in this order:
//     r1: s1 reads a -> writes b,  then s2 reads b -> writes c
//     r2: s1 reads x -> writes x
//     r3: s1 reads x -> writes x
//
//   Correct final store:     { a:1, b:2, c:3, x:2 }
//   Correct writes per key:  b:[2]   c:[3]   x:[1, 2]
//   Correct replies (last value each request wrote to each of its keys):
//     r1 -> { b:2, c:3 }    r2 -> { x:1 }    r3 -> { x:2 }
//
//   Wrong outcomes, even if the run "finishes":
//     x:[1, 1], final x = 1      r2 and r3 read the same x (lost update)
//     x:[1, 2, 3]                a retry re-applied a stage that had already landed
//     r2 -> {x:2}, r3 -> {x:1}   same final store, but arrival order was not respected
//     c never written            a stage was skipped after a crash or duplicate event
//     no reply, or two replies   a request was lost, or answered twice

#pragma once

#include <string>

#include "../simulation/World.h"

class CandidateCoordinator : public Coordinator {
 public:
  // A new request has arrived.
  void on_request(const Request& r, Context& ctx) override {
    (void)r;
    (void)ctx;
    // TODO
  }

  // A job you submitted has completed or failed.
  // Events may be dropped, duplicated, or arrive for jobs you no longer care about.
  void on_job_event(const JobEvent& ev, Context& ctx) override {
    (void)ev;
    (void)ctx;
    // TODO
  }

  // A timer set via ctx.set_timer() has fired.
  void on_timer(const std::string& timer_id, Context& ctx) override {
    (void)timer_id;
    (void)ctx;
    // TODO
  }

  // Called on a fresh instance after the coordinator crashes.
  // In-memory members are gone; only ctx.durable_read/durable_write state survives.
  void on_recover(Context& ctx) override {
    (void)ctx;
    // TODO
  }

 private:
  // TODO: your state here.
};