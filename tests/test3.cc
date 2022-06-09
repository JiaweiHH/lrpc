#include "../Channel.h"
#include "../EventLoop.h"

#include <sys/timerfd.h>
#include <stdio.h>
#include <iostream>
#include <cstring>
#include <string>

lrpc::net::EventLoop *g_loop;

void timeout(lrpc::net::Timestamp recieveTime) {
  std::cout << recieveTime.toFormattedString() << "\n";
  g_loop->quit();
}

int main() {
  lrpc::net::EventLoop loop;
  g_loop = &loop;

  int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  lrpc::net::Channel channel(&loop, timerfd);
  channel.setReadCallback(timeout);
  channel.enableReading();

  struct itimerspec howlong;
  bzero(&howlong, sizeof howlong);
  howlong.it_value.tv_sec = 5;
  ::timerfd_settime(timerfd, 0, &howlong, nullptr);

  loop.loop();
}