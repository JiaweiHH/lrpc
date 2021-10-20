#include "TimerQueue.h"
#include "EventLoop.h"
#include "Timer.h"

#include <algorithm>
#include <boost/log/trivial.hpp>
#include <iostream>
#include <sys/timerfd.h>

namespace imitate_muduo {

namespace detail {

using steady_time = std::chrono::steady_clock::time_point;

// 创建 timerfd 文件描述符
int createTimerfd() {
  int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerfd < 0) {
    BOOST_LOG_TRIVIAL(fatal) << "Failed in timerfd_create";
  }
  return timerfd;
}

// 获取当前时间到 when 的时间差
std::chrono::microseconds howMuchTimeFromNow(steady_time when) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      when - std::chrono::steady_clock::now());
}

// 读取 timerfd 文件描述符
void readTimerfd(int timerfd, steady_time now) {
  uint64_t howmany;
  // 读取 timerfd 返回超时次数
  // 当 timerfd 是阻塞式的时候则 read 操作将阻塞，否则返回 EAGAIN
  ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
  BOOST_LOG_TRIVIAL(trace) << "TimerQueue::handleRead() " << howmany << " at ";
  //  << now;
  if (n != sizeof howmany) {
    BOOST_LOG_TRIVIAL(error)
        << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
  }
}

// 设置 timerfd 定时事件
void resetTimerfd(int timerfd, steady_time expiration) {
  // 首先获取到期时间点和现在的时间差
  auto value = howMuchTimeFromNow(expiration);
  // BOOST_LOG_TRIVIAL(trace) << value.count();

  // newValue 新的超时时间，oldValue 设置新的超时时间之前的超时时间
  struct itimerspec newValue, oldValue;
  bzero(&newValue, sizeof newValue);
  bzero(&oldValue, sizeof oldValue);

  // 设置时间间隔
  const int microSecondPerSeconds = 1000 * 1000;
  newValue.it_value.tv_sec = value.count() / microSecondPerSeconds; // 设置秒
  newValue.it_value.tv_nsec =
      value.count() % microSecondPerSeconds * 1000; // 设置纳秒

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

// 添加新的到期任务
void TimerQueue::addTimer(const TimerCallback &cb, Timestamp when,
                          double interval) {
  std::unique_ptr timer = std::make_unique<Timer>(cb, when, interval);
  loop_->assertInLoopThread();
  bool earliestChanged = insert(std::move(timer));
  // 之后不应该再使用 timer 这个变量

  if (earliestChanged) {
    // 设置定时事件
    resetTimerfd(timerfd_, when);
  }
}

// 添加新的 Timer
bool TimerQueue::insert(std::unique_ptr<Timer> &&timer) {
  // 记录这个 timer 是不是 timers_ 中最早到期的
  bool earliestChanged = false;
  Timestamp when = timer->expiration();
  if (timers_.empty() || when < timers_.begin()->first)
    earliestChanged = true;

  auto result = timers_.insert(std::make_pair(when, std::move(timer)));
  assert(result.second);
  return earliestChanged;
}

// 处理到期的 Timer
void TimerQueue::handleRead() {
  loop_->assertInLoopThread();
  Timestamp now(std::chrono::steady_clock::now());
  readTimerfd(timerfd_, now);

  // 获取到期的 Timers
  // note: 这里不用担心 unique_ptr 不可拷贝的问题，返回的 vector
  // 是右值，所以会自动进行移动
  std::vector<Entry> expired = getExpired(now);
  // 执行 Timers 的回调
  for (const auto &t : expired) {
    t.second->run();
  }
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
  for (auto itera = timers_.begin(); itera != it; ++itera) {
    // expired.push_back(std::move(*itera)); // note: 记得使用 move

    // 由于 std::set 的元素是 const 类型的，不能随意修改
    // 所以这里使用的方法比较拙劣，直接拷贝 std::set 元素中 unique_ptr
    // 指向的那块内存，创建一个新的 unique_ptr，然后存入 expired 中。

    // 其实这样也不会有什么大问题，因为 std::set
    // 中的元素等下就要直接删除的，只是开销大了一些
    auto &up = it->second;
    expired.push_back(std::make_pair(
        it->first, std::make_unique<imitate_muduo::Timer>(
                       up->callback(), up->expiration(), up->interval())));
  }
  timers_.erase(timers_.begin(), it);

  // 这里编译器会执行 RVO 优化
  return expired;
}

// 检查到期 Timers 中有没有需要重复的，如果需要重复的就重新插入 timers_ 中
// 这里在使用 insert(std::move(...)) 之后 t.second 已经变成了 nullptr
void TimerQueue::reset(std::vector<Entry> &expired, Timestamp now) {

  // 找出到期的，重新插入
  for (auto &t : expired) {
    if (t.second->repeat()) {
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