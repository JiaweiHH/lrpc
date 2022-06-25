#ifndef LRPC_SCHEDULER_H
#define LRPC_SCHEDULER_H

#include <functional>
#include <chrono>

namespace lrpc {

namespace util {

class Scheduler {
public:
  ~Scheduler() {}
  virtual void Schedule(std::function<void()> f) = 0;
  virtual void ScheduleLater(std::chrono::milliseconds duration,
                             std::function<void()> f) = 0;
};

} // namespace util

} // namespace lrpc

#endif