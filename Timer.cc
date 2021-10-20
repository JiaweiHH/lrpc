#include "Timer.h"

using namespace imitate_muduo;

// 重新设置 Timer 的到期时间
void Timer::restart(Timestamp now) {
  if (interval_ > 0.0) {
    int micro_time = interval_ * 1000 * 1000;
    expiration_ = now + std::chrono::microseconds(micro_time);
  } else {
    expiration_ = now;
  }
}
Timer::~Timer() {}
