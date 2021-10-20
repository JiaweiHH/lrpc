#ifndef IMITATE_MUDUO_TIMEHELP_H
#define IMITATE_MUDUO_TIMEHELP_H

#include <chrono>

/// @param interval 时间间隔，单位是秒
std::chrono::steady_clock::time_point
addTime(const std::chrono::steady_clock::time_point &time, double interval) {
  int micro_time = interval * 1000 * 1000;
  auto time_point = time + std::chrono::microseconds(micro_time);
  return time_point;
}

#endif