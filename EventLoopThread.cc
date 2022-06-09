#include "EventLoopThread.h"
#include "EventLoop.h"

#include <functional>

using namespace lrpc::net;

EventLoopThread::EventLoopThread() : loop_(nullptr), exiting_(false) {}

EventLoopThread::~EventLoopThread() {
  exiting_ = true;
  loop_->quit();
  thread_.join();
}

/// 启动新线程执行 EventLoop，返回 loop 指针
EventLoop *EventLoopThread::startLoop() {
  thread_ = std::thread(std::bind(&EventLoopThread::threadFunc, this));
  {
    // 主线程在这里等待 EventLoop 对象创建好
    std::unique_lock lk(mutex_);
    cv_.wait(lk, [this] { return loop_ != nullptr; });
  }
  return loop_;
}

/// 线程执行函数，执行事件循环
void EventLoopThread::threadFunc() {
  EventLoop loop;
  {
    std::lock_guard lk(mutex_);
    loop_ = &loop;
    cv_.notify_one();
  }
  loop.loop();
}