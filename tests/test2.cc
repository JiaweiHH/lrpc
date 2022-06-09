/**
 * @file test2.cc
 * @author your name (you@domain.com)
 * @brief 负面测试，在主线程中创建 EventLoop，试图在另一个线程执行事件循环
 * @version 0.1
 * @date 2021-10-14
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include "EventLoop.h"
#include <thread>

lrpc::net::EventLoop *g_loop;

void threadFunc() { g_loop->loop(); }

int main() {
  lrpc::net::EventLoop loop;
  g_loop = &loop;
  std::thread t(threadFunc);
  t.join();
}