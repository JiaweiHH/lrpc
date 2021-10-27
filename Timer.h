#ifndef IMITATE_MUDUO_TIMER_H
#define IMITATE_MUDUO_TIMER_H

#include "Callback.h"
#include <functional>

namespace imitate_muduo {

class Timer {
private:
  const TimerCallback callback_;
  Timestamp expiration_;  // 到期时间点
  const double interval_; // 重复时间间隔，单位秒

public:
  Timer(const Timer &) = delete;
  Timer &operator=(const Timer &) = delete;
  Timer(const TimerCallback &cb, Timestamp when, double interval)
      : callback_(cb), expiration_(when), interval_(interval) {}
  ~Timer();

  void run() const { callback_(); }

  Timestamp expiration() const { return expiration_; }

  bool repeat() const { return interval_ > 0.0; }

  void restart(Timestamp now);

  TimerCallback callback() const { return callback_; }

  double interval() const { return interval_; }
};

} // namespace imitate_muduo

#endif