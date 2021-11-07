#include "TimerQueue.h"
#include "EventLoop.h"
#include "Timer.h"
#include "TimerId.h"

#include <algorithm>
#include <boost/log/trivial.hpp>
#include <iostream>
#include <sys/timerfd.h>

namespace imitate_muduo {

namespace detail {

// 创建 timerfd 文件描述符
int createTimerfd() {
  int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerfd < 0) {
    BOOST_LOG_TRIVIAL(fatal) << "Failed in timerfd_create";
  }
  return timerfd;
}

// 获取当前时间到 when 的时间差
struct timespec howMuchTimeFromNow(Timestamp when) {
  int64_t microseconds =
      when.microSecondsSinceEpoch() - Timestamp::now().microSecondsSinceEpoch();
  if (microseconds < 100)
    microseconds = 100;
  struct timespec ts;
  // 设置秒和纳秒
  ts.tv_sec = static_cast<time_t>(microseconds / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(microseconds % Timestamp::kMicroSecondsPerSecond * 1000);
  return ts;
}

// 读取 timerfd 文件描述符
void readTimerfd(int timerfd, Timestamp now) {
  uint64_t howmany;
  // 读取 timerfd 返回超时次数
  // 当 timerfd 是阻塞式的时候则 read 操作将阻塞，否则返回 EAGAIN
  ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
  BOOST_LOG_TRIVIAL(trace) << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
  //  << now;
  if (n != sizeof howmany) {
    BOOST_LOG_TRIVIAL(error)
        << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
  }
}

// 设置 timerfd 定时事件
void resetTimerfd(int timerfd, Timestamp expiration) {
  // newValue 新的超时时间，oldValue 设置新的超时时间之前的超时时间
  struct itimerspec newValue, oldValue;
  bzero(&newValue, sizeof newValue);
  bzero(&oldValue, sizeof oldValue);

  // 设置时间间隔
  newValue.it_value = howMuchTimeFromNow(expiration);
  // 执行系统调用，设置定时器
  int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
  if (ret) {
    BOOST_LOG_TRIVIAL(error) << "timerfd_settime()";
  }
}

} // namespace detail

} // namespace imitate_muduo

using namespace imitate_muduo;
using namespace imitate_muduo::detail;

TimerQueue::TimerQueue(EventLoop *loop)
    : loop_(loop), timerfd_(createTimerfd()), timerfdChannel_(loop, timerfd_),
      timers_() {
  timerfdChannel_.setReadCallback(std::bind(&TimerQueue::handleRead, this));
  timerfdChannel_.enableReading();
}

/// 创建新的 timer 定时任务，然后调用 EventLoop 的 runInLoop
/// 这样，当调用方不是 IO 线程的时候现在可以将这个工作移动到 IO 线程中了，就不会产生错误
/// addTimer 是线程安全的，并且不需要加锁
TimerId TimerQueue::addTimer(const TimerCallback &cb, Timestamp when,
                          double interval) {
  Timer *timer = new Timer(cb, when, interval);
  loop_->runInLoop(std::bind(&TimerQueue::addTimerInLoop, this, timer));
  return TimerId(timer, timer->sequence());
}
/// addTimer 的实际工作，只能在 IO 线程中做
void TimerQueue::addTimerInLoop(Timer *timer) {
  loop_->assertInLoopThread();
  bool earliestChanged = insert(timer);
  if (earliestChanged) {
    // 设置定时事件
    resetTimerfd(timerfd_, timer->expiration());
  }
}

// 添加新的 Timer
bool TimerQueue::insert(Timer *timer) {
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  // 记录这个 timer 是不是 timers_ 中最早到期的
  bool earliestChanged = false;
  Timestamp when = timer->expiration();
  if (timers_.empty() || when < timers_.begin()->first)
    earliestChanged = true;
  
  // 插入 timers_
  {
    auto result = timers_.insert(std::make_pair(when, std::move(timer)));
    assert(result.second);
  }
  // 插入 activeTimers_
  {
    auto result = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
    assert(result.second);
  }

  return earliestChanged;
}

// 处理到期的 Timer
void TimerQueue::handleRead() {
  loop_->assertInLoopThread();
  auto now = Timestamp::now();
  readTimerfd(timerfd_, now);

  // 获取到期的 Timers
  // note: 这里不用担心 unique_ptr 不可拷贝的问题，返回的 vector
  // 是右值，所以会自动进行移动
  std::vector<Entry> expired = getExpired(now);

  callingExpiredTimers_ = true;
  cancelingTimers_.clear();

  // 执行 Timers 的回调
  for (const auto &t : expired) {
    t.second->run();
  }
  callingExpiredTimers_ = false;
  reset(expired, now);
}

// 获取已经到期的 Timer
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now) {
  std::vector<Entry> expired;

  // 返回第一个未到期的 Timer 迭代器
  Entry sentry = std::make_pair(now, nullptr);
  auto it = timers_.lower_bound(sentry);
  assert(it == timers_.end() || now < it->first);
  // 将 [begin,it) 的元素移入 expired
  std::copy(timers_.begin(), it, std::back_inserter(expired));
  timers_.erase(timers_.begin(), it);

  // 从 activeTimers 列表中移除
  for (auto entry : expired) {
    ActiveTimer timer(entry.second, entry.second->sequence());
    size_t n = activeTimers_.erase(timer);
    assert(n == 1);
  }
  assert(timers_.size() == activeTimers_.size());
  // 这里编译器会执行 RVO 优化
  return expired;
}

// 检查到期 Timers 中有没有需要重复的，如果需要重复的就重新插入 timers_ 中
// 这里在使用 insert(std::move(...)) 之后 t.second 已经变成了 nullptr
void TimerQueue::reset(std::vector<Entry> &expired, Timestamp now) {
  // 找出到期的，重新插入
  for (auto &t : expired) {
    ActiveTimer timer(t.second, t.second->sequence());
    // 检查定时器不再已经注销的定时器列表中
    if (t.second->repeat() && cancelingTimers_.find(timer) == cancelingTimers_.end()) {
      t.second->restart(now); // 这里更新它的新的到期时间
      insert(std::move(t.second));
    } else {
      // 不需要手动执行释放 t.second 操作，由智能指针管理
    }
  }

  // 重新插入之后最近的到期时间需要重新计算
  // 并重新设置 timerfd_ 文件描述符的到期时间
  Timestamp nextExpired;
  if (!timers_.empty()) {
    nextExpired = timers_.begin()->second->expiration();
  }
  resetTimerfd(timerfd_, nextExpired);
}

TimerQueue::~TimerQueue() {}

void TimerQueue::cancel(TimerId timerId) {
  loop_->runInLoop(std::bind(&TimerQueue::cancelInLoop, this, timerId));
}
void TimerQueue::cancelInLoop(TimerId timerId) {
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  ActiveTimer timer(timerId.timer_, timerId.sequence_);
  auto it = activeTimers_.find(timer);
  if (it != activeTimers_.end()) {
    size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
    assert(n == 1);
    delete it->first; // 释放内存空间
    activeTimers_.erase(it);
  } else if (callingExpiredTimers_) {
    // 自注销的场景
    cancelingTimers_.insert(timer);
  }
  assert(timers_.size() == activeTimers_.size());
}
