#ifndef IMITATE_MUDUO_EVENTLOOPTHREAD_H
#define IMITATE_MUDUO_EVENTLOOPTHREAD_H

#include <thread>
#include <mutex>
#include <condition_variable>

namespace imitate_muduo {

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
  EventLoopThread& operator=(const EventLoopThread &) = delete;
  EventLoopThread();
  ~EventLoopThread();
  
  EventLoop *startLoop();
};

}

#endif