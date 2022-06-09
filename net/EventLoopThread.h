#ifndef IMITATE_MUDUO_EVENTLOOPTHREAD_H
#define IMITATE_MUDUO_EVENTLOOPTHREAD_H

#include <condition_variable>
#include <mutex>
#include <thread>

namespace lrpc {
namespace net {

class EventLoop;

class EventLoopThread {
private:
  void threadFunc();

  EventLoop *loop_;
  bool exiting_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;

public:
  EventLoopThread(const EventLoopThread &) = delete;
  EventLoopThread &operator=(const EventLoopThread &) = delete;
  EventLoopThread();
  ~EventLoopThread();

  EventLoop *startLoop();
};

} // namespace net
} // namespace lrpc

#endif