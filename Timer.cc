#include "Timer.h"

using namespace imitate_muduo;

std::atomic<int64_t> Timer::s_numCreated_;

// 重新设置 Timer 的到期时间
void Timer::restart(Timestamp now) {
  if (interval_ > 0.0) {
    expiration_ = addTime(now, interval_);
  } else {
    expiration_ = Timestamp::invalid();
  }
}
Timer::~Timer() {}
