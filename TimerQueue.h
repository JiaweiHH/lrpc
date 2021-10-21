#ifndef IMITATE_MUDUO_TIMERQUEUE_H
#define IMITATE_MUDUO_TIMERQUEUE_H

#include "Channel.h"

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace imitate_muduo {

class EventLoop;
class Timer;
class TimerId;

/// 定时器
/// TimerQueue 的成员函数只会在其所属的 IO 线程中调用，因此不需要加锁
class TimerQueue {
public:
  using Timestamp = std::chrono::steady_clock::time_point;
  using Entry = std::pair<Timestamp, std::unique_ptr<Timer>>;
  using TimerCallback = std::function<void()>;

private:
  // void addTimerInLoop(std::unique_ptr<Timer> &timer);
  void addTimerInLoop(const TimerCallback &cb, Timestamp when, double interval);
  // 当 timerfd 可读的时候调用
  void handleRead();

  // 移除所有到期的 timer
  std::vector<Entry> getExpired(Timestamp now);
  void reset(std::vector<Entry> &expired, Timestamp now);

  bool insert(std::unique_ptr<Timer> &&);

  EventLoop *loop_;
  const int timerfd_;
  Channel timerfdChannel_;
  // set 中保存 pair<Timestamp, Timer *>，这样即便两个 Timer
  // 的到期时间相同，pair 是不同的
  std::set<Entry> timers_;

public:
  TimerQueue(const TimerQueue &) = delete;
  TimerQueue &operator=(const TimerQueue &) = delete;
  TimerQueue(EventLoop *loop);
  ~TimerQueue();

  void addTimer(const TimerCallback &cb, Timestamp when, double interval);
};

} // namespace imitate_muduo

#endif