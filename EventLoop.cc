#include "EventLoop.h"
#include "Channel.h"
#include "Poller.h"
#include "TimerQueue.h"
#include "TimeHelp.h"

#include <assert.h>
#include <boost/log/trivial.hpp>
#include <poll.h>

using namespace imitate_muduo;

// thread_local 变量记录当前线程的 EventLoop
thread_local EventLoop *t_loopInThisThread = 0;
// IO mutilplexing 超时时间
const int kPollTimeMs = 10000;

EventLoop::EventLoop()
    : looping_(false), quit_(false), threadId_(std::this_thread::get_id()),
      poller_(new Poller(this)), timerQueue_(new TimerQueue(this)) {
  BOOST_LOG_TRIVIAL(trace) << "EventLoop created " << this << " in thread "
                           << threadId_;
  // 检查当前线程是否已经创建了 EventLoop 对象
  if (t_loopInThisThread) {
    BOOST_LOG_TRIVIAL(fatal) << "Another EventLoop " << t_loopInThisThread
                             << " exists in this thread " << threadId_;
  } else {
    t_loopInThisThread = this;
  }
}

EventLoop::~EventLoop() {
  assert(!looping_);
  t_loopInThisThread = nullptr;
}

// 每个线程只有一个 EventLoop，返回这个 EventLoop
// note: 返回值有可能是 nullptr，如果当前线程不是 IO 线程
EventLoop *EventLoop::getEventLoopOfCurrentThread() {
  return t_loopInThisThread;
}

// 执行事件循环
void EventLoop::loop() {
  assert(!looping_);
  assertInLoopThread();
  // note: 不需要加锁保护，因为 EventLoop 只能在当前线程中执行、不存在 race
  // condition
  looping_ = true;
  quit_ = false;

  while(!quit_) {
    // 每一轮事件循环开始的时候需要先清除 activeChannels_
    activeChannels_.clear();
    poller_->poll(kPollTimeMs, activeChannels_);
    // 分发函数
    for(const auto &channel : activeChannels_) {
      channel->handleEvent();
    }
  }

  BOOST_LOG_TRIVIAL(trace) << "EventLoop " << this << " stop looping";
  looping_ = false;
}

// 断言错误的时候打印错误信息
void EventLoop::abortNotInLoopThread() {
  BOOST_LOG_TRIVIAL(fatal) << "EventLoop::abortNotInLoopThread - EventLoop "
                           << this
                           << " was created in threadId_ = " << threadId_
                           << ", current thread id = "
                           << std::this_thread::get_id();
}

void EventLoop::quit() { quit_ = true; }

// EventLoop 不必关心 Poller 是如何管理 Channel 列表的
// 调用 Poller::updateChannel()
void EventLoop::updateChannel(Channel *channel) {
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

// 下面三个函数分别让用户选择：
// 1. 在某一个时间点执行回调
// 2. 在某一个延迟时间之后执行回调
// 3. 每隔一个时间段执行回调
void EventLoop::runAt(const std::chrono::steady_clock::time_point &time,
                      const TimerCallback &cb) {
  timerQueue_->addTimer(cb, time, 0.0);
}
void EventLoop::runAfter(double delay, const TimerCallback &cb) {
  auto when = addTime(std::chrono::steady_clock::now(), delay);
  timerQueue_->addTimer(cb, when, 0.0);
}
void EventLoop::runEvery(double interval, const TimerCallback &cb) {
  auto when = addTime(std::chrono::steady_clock::now(), interval);
  timerQueue_->addTimer(cb, when, interval);
}