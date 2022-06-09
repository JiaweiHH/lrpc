/**
 * @file test1.cc
 * @author your name (you@domain.com)
 * @brief 主线程和子线程分别创建一个时间循环，程序正常退出
 * @version 0.1
 * @date 2021-10-14
 *
 * @copyright Copyright (c) 2021
 *
 */

#include "EventLoop.h"
#include <stdio.h>
#include <thread>
#include <iostream>

void threadFunc() {
  std::cout << std::this_thread::get_id() << "\n";
  // printf("threadFunc(): %d\n", std::this_thread::get_id());
  lrpc::net::EventLoop loop;
  loop.loop();
}

int main() {
  std::cout << std::this_thread::get_id() << "\n";
  // printf("main(): %d\n", std::this_thread::get_id());
  lrpc::net::EventLoop loop;
  std::thread t(threadFunc);
  loop.loop();
  t.join();
}