#include "../Channel.h"
#include "../EventLoop.h"

#include <sys/timerfd.h>
#include <stdio.h>
#include <iostream>
#include <cstring>
#include <string>

imitate_muduo::EventLoop *g_loop;

void timeout(imitate_muduo::Timestamp recieveTime) {
  time_t time = std::chrono::system_clock::to_time_t(recieveTime);
  std::cout << ctime(&time) << " Timeout!\n";
  g_loop->quit();
}

int main() {
  imitate_muduo::EventLoop loop;
  g_loop = &loop;

  int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  imitate_muduo::Channel channel(&loop, timerfd);
  channel.setReadCallback(timeout);
  channel.enableReading();

  struct itimerspec howlong;
  bzero(&howlong, sizeof howlong);
  howlong.it_value.tv_sec = 5;
  ::timerfd_settime(timerfd, 0, &howlong, nullptr);

  loop.loop();
}