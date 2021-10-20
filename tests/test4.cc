#include "../Timer.h"
#include "../EventLoop.h"

#include <functional>
#include <iostream>
#include <stdio.h>
#include <unistd.h>

int cnt = 0;
imitate_muduo::EventLoop *g_loop;

// 获取系统当前时间
std::string getCurrentSystemTime() {
  auto tt =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  struct tm *ptm = localtime(&tt);
  char date[60] = {0};
  sprintf(date, "%d-%02d-%02d-%02d.%02d.%02d", (int)ptm->tm_year + 1900,
          (int)ptm->tm_mon + 1, (int)ptm->tm_mday, (int)ptm->tm_hour,
          (int)ptm->tm_min, (int)ptm->tm_sec);
  return std::string(date);
}

void printTid() {
  auto id = std::this_thread::get_id();
  printf("pid = %d, tid = %d\n", getpid(), *(uint32_t *)&id);
  printf("now %s\n", getCurrentSystemTime().c_str());
}

void print(const char *msg) {
  printf("msg %s %s\n", getCurrentSystemTime().c_str(), msg);
  if(++cnt == 20)
    g_loop->quit();
}

int main() {
  printTid();
  imitate_muduo::EventLoop loop;
  g_loop = &loop;

  print("main");
  loop.runAfter(1, std::bind(print, "once1"));
  loop.runAfter(1.5, std::bind(print, "once1.5"));
  loop.runAfter(2.5, std::bind(print, "once2.5"));
  loop.runAfter(3.5, std::bind(print, "once3.5"));
  loop.runEvery(2, std::bind(print, "every2"));
  loop.runEvery(3, std::bind(print, "every3"));
  loop.loop();
  print("main loop exits");
  sleep(1);
}