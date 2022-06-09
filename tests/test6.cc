#include "../EventLoop.h"
#include "../EventLoopThread.h"
#include <stdio.h>
#include <unistd.h>

using namespace std::chrono_literals;

/// 测试跨线程调用 runInLoop 和 runAfter

void runInThread() {
  auto tid = std::this_thread::get_id();
  printf("runInThread(): pid = %d, tid = 0x%lx\n", getpid(), *(uint64_t *)&tid);
}

int main() {
  auto tid = std::this_thread::get_id();
  printf("main(): pid = %d, tid = 0x%lx\n", getpid(), *(uint64_t *)&tid);

  lrpc::net::EventLoopThread loopThread;
  lrpc::net::EventLoop *loop = loopThread.startLoop();
  loop->runInLoop(runInThread);
  std::this_thread::sleep_for(1s);
  loop->runAfter(2, runInThread);
  loop->quit();
  printf("exit main().\n");
}