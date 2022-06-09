#ifndef IMITATE_MUDUO_EVENTLOOPTHREADPOOL_H
#define IMITATE_MUDUO_EVENTLOOPTHREADPOOL_H

#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace lrpc {
namespace net {

class EventLoop;
class EventLoopThread;

class EventLoopThreadPool {
private:
  EventLoop *baseLoop_;
  bool started_;
  int numThreads_;
  int next_;
  std::vector<std::unique_ptr<EventLoopThread>> threads_;
  std::vector<EventLoop *> loops_;

public:
  EventLoopThreadPool(const EventLoopThreadPool &) = delete;
  EventLoopThreadPool &operator=(const EventLoopThreadPool &) = delete;
  EventLoopThreadPool(EventLoop *baseLoop);
  ~EventLoopThreadPool();

  void setThraedNum(int numThraeds) { numThreads_ = numThraeds; }
  void start();
  EventLoop *getNextLoop();
};

} // namespace net
} // namespace lrpc

#endif