#ifndef IMITATE_MUDUO_TIMERID_H
#define IMITATE_MUDUO_TIMERID_H

#include <stdint-gcc.h>

namespace imitate_muduo {

class Timer;

class TimerId {
public:
  TimerId(Timer *timer = nullptr, int64_t seq = 0)
      : timer_(timer), sequence_(seq) {}
  friend class TimerQueue;
private:
  // 需要同时根据 Timer* 地址和 seq 才能唯一确定定时器
  Timer *timer_;
  int64_t sequence_;
};

} // namespace imitate_muduo

#endif