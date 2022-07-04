#ifndef LRPC_SCHEDULER_H
#define LRPC_SCHEDULER_H

#include "Coroutine.h"
#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace lrpc {

namespace coroutine {

class Scheduler {
public:
  using SchedulerPtr = std::shared_ptr<Scheduler>;
  Scheduler(size_t threadNum, const std::string &name = "");
  virtual ~Scheduler();
  const std::string &getName() const { return name_; }
  void start(); // 启动协程调度器
  void stop();  // 停止协程调度器

  // 提交任务到调度器，任务可以是协程或者可调用对象
  template <typename F> Scheduler *submit(F &&f, int thread = -1) {}
  // 批量调度协程
  template <typename InputIterator>
  void submit(InputIterator begin, InputIterator end) {}

private:
  virtual void notify();   // 通知协程调度器有任务了
  virtual bool stopping(); // 返回是否可以停止
  virtual void wait();     // 协程无任务可调度时执行 wait 协程
  void setThis();          // 设置当前的协程调度器
  void run();              // 协程调度函数
  bool hasIdleThreads();   // 是否有空闲线程

  std::string name_;
  std::mutex mutex_;
  // std::list<>

  size_t threadsNum_;                     // 线程数量
  std::atomic<size_t> activeThreads_ = 0; // 活跃线程数
  std::atomic<size_t> idleThreads_ = 0;   // 空闲线程数

  bool stop_ = true;                 // 调度器是否停止
  std::vector<std::thread> threads_; // 调度器线程池
  std::vector<std::thread::id> threadIds;
  std::list<CoroutinePtr> tasks_; // 调度器任务
};

} // namespace coroutine

} // namespace lrpc

#endif