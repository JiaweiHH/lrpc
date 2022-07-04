#include "EventLoopThreadPool.h"
#include "EventLoop.h"
#include "EventLoopThread.h"
#include <assert.h>

using namespace lrpc::net;

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop)
    : baseLoop_(baseLoop), started_(false), numThreads_(0), next_(0) {}

EventLoopThreadPool::~EventLoopThreadPool() {}

void EventLoopThreadPool::start() {
  assert(!started_);
  baseLoop_->assertInLoopThread();

  started_ = true;
  for (int i = 0; i < numThreads_; ++i) {
    threads_.emplace_back(new EventLoopThread);
    loops_.push_back(threads_[i]->startLoop());
  }
}

/// server 线程选取一个线程来管理新连接的事件
EventLoop *EventLoopThreadPool::getNextLoop() {
  // baseLoop_->assertInLoopThread();
  EventLoop *loop = baseLoop_;
  if (!loops_.empty()) {
    // round-robin
    loop = loops_[next_];
    ++next_;
    if (static_cast<size_t>(next_) >= loops_.size())
      next_ = 0;
  }
  return loop;
}
