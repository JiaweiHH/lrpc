#ifndef IMITATE_MUDUO_TIMER_H
#define IMITATE_MUDUO_TIMER_H

#include "Callback.h"
#include "Timestamp.h"
#include <functional>
#include <atomic>

namespace imitate_muduo {

class Timer {
private:
  const TimerCallback callback_;
  Timestamp expiration_;  // 到期时间点
  const double interval_; // 重复时间间隔，单位秒
  // 需要同时根据 Timer 地址和 seq 才能唯一确定一个定时器
  const int64_t sequence_;

  static std::atomic<int64_t> s_numCreated_;

public:
  Timer(const Timer &) = delete;
  Timer &operator=(const Timer &) = delete;
  Timer(const TimerCallback &cb, Timestamp when, double interval)
      : callback_(cb), expiration_(when), interval_(interval), sequence_(s_numCreated_++) {}
  ~Timer();

  void run() const { callback_(); }

  Timestamp expiration() const { return expiration_; }
  bool repeat() const { return interval_ > 0.0; }
  TimerCallback callback() const { return callback_; }
  double interval() const { return interval_; }
  int64_t sequence() const { return sequence_; }

  void restart(Timestamp now);
};

} // namespace imitate_muduo

#endif