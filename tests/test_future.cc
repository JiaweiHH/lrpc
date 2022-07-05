#include "EventLoop.h"
#include "future.h"
#include "result.h"
#include "EventLoopThreadPool.h"
#include <iostream>

using namespace lrpc::net;

int func() {
  std::cout << "set value 10\n";
  int v = 10;
  return v;
}

int main() {
  EventLoop *loop = new EventLoop();
  EventLoopThreadPool threadPool_(loop);
  threadPool_.setThreadNum(1);
  lrpc::Future<int> fut = loop->Execute(func);
  auto nextLoop = threadPool_.getNextLoop();

  auto f = lrpc::makeReadyFuture(1).then(nextLoop, [](lrpc::Result<int> v) -> lrpc::Future<int> {
    std::cout << "1. then got int value " << v
                       << " and return float 1.0f.\n";
    return lrpc::makeReadyFuture(1);
  });
  // fut.then(loop,
  //          [](int v) {
  //            std::cout << "1. then got int value " << v
  //                      << " and return float 1.0f.\n";
  //            return 1.0f;
  //          })
  //     .then(nextLoop, [](float f) {
  //       std::cout << "2. then got float value " << f << " and return 2.\n";
  //       return 2;
  //     });
  // loop->runEvery(1, []() { printf("every 1 second\n"); });
  threadPool_.start();
  loop->loop();
}