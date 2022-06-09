#ifndef IMITATE_MUDUO_TIMERQUEUE_H
#define IMITATE_MUDUO_TIMERQUEUE_H

#include "Callback.h"
#include "Channel.h"
#include "Timestamp.h"
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace lrpc {
namespace net {

class EventLoop;
class Timer;
class TimerId;

/// 定时器
/// TimerQueue 的成员函数只会在其所属的 IO 线程中调用，因此不需要加锁
class TimerQueue {
public:
  TimerQueue(const TimerQueue &) = delete;
  TimerQueue &operator=(const TimerQueue &) = delete;
  TimerQueue(EventLoop *loop);
  ~TimerQueue();

  TimerId addTimer(const TimerCallback &cb, Timestamp when, double interval);
  void cancel(TimerId timerId);

private:
  using Entry = std::pair<Timestamp, Timer *>;
  using ActiveTimer = std::pair<Timer *, int64_t>;

  void addTimerInLoop(Timer *timer);
  void cancelInLoop(TimerId timerId);
  // 当 timerfd 可读的时候调用
  void handleRead();

  // 移除所有到期的 timer
  std::vector<Entry> getExpired(Timestamp now);
  void reset(std::vector<Entry> &expired, Timestamp now);

  bool insert(Timer *);

  EventLoop *loop_;
  const int timerfd_;
  Channel timerfdChannel_;
  // set 中保存 pair<Timestamp, Timer *>，这样即便两个 Timer
  // 的到期时间相同，pair 是不同的，按照到期时间排序
  std::set<Entry> timers_;

  // for cancel
  bool callingExpiredTimers_;
  // 保存目前有效的 Timer 指针，与 timers_ 的区别在于排序方式不同
  // 按照对象地址排序
  std::set<ActiveTimer> activeTimers_;
  std::set<ActiveTimer> cancelingTimers_;
};

} // namespace net
} // namespace lrpc

#endif