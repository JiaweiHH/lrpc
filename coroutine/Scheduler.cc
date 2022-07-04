#include "Scheduler.h"
#include "Logging.h"
#include <algorithm>
#include <signal.h>

namespace lrpc {

namespace coroutine {

static thread_local Scheduler *t_scheduler = nullptr;

Scheduler::Scheduler(size_t threadsNum, const std::string &name)
    : name_(name), threadsNum_(threadsNum) {
  t_scheduler = this;
}

Scheduler::~Scheduler() {
  assert(stop_);
  t_scheduler = nullptr;
}

void Scheduler::start() {
  std::lock_guard<std::mutex> lk(mutex_);
  if (stop_ == false)
    return;
  stop_ = false;
  assert(threads_.empty());
  threads_.resize(threadsNum_);
  for (size_t i = 0; i < threadsNum_; ++i) {
    threads_.emplace_back([this] { this->run(); });
    threadIds[i] = threads_[i].get_id();
  }
}

void Scheduler::run() {
  // 忽略 SIGPIPE 信号
  signal(SIGPIPE, SIG_IGN);
}

bool Scheduler::stopping() {
  std::lock_guard<std::mutex> lk(mutex_);
  // 等待任务队列为空，并且所有协程执行完毕
  return stop_ && tasks_.empty() && activeThreads_ == 0;
}

void Scheduler::wait() {
  LOG_INFO << "idle coroutine";
  while (!stopping())
    Coroutine::Yield(); // 让出执行权
  LOG_INFO << "idle coroutine exit";
}

} // namespace coroutine

} // namespace lrpc