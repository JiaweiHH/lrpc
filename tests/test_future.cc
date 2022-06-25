#include "EventLoop.h"
#include "future.h"
#include <iostream>

using namespace lrpc::net;

int func() {
  std::cout << "set value 10\n";
  int v = 10;
  return v;
}

int main() {
  EventLoop *loop = new EventLoop();
  lrpc::Future<int> fut = loop->Execute(func);
  fut.then(loop,
           [](int v) {
             std::cout << "1. then got int value " << v
                       << " and return float 1.0f.\n";
             return 1.0f;
           })
      .then([](float f) {
        std::cout << "2. then got float value " << f << " and return 2.\n";
        return 2;
      });
  std::cout << "begin loop\n";
  loop->runEvery(1, []() { printf("every 1 second\n"); });
  loop->loop();
}