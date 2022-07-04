#ifndef IMITATE_MUDUO_EVENTLOOP_H
#define IMITATE_MUDUO_EVENTLOOP_H

#include "Scheduler.h"
#include "Logging.h"
#include "TimerId.h"
#include "Timestamp.h"
#include "future.h"
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace lrpc {
namespace net {

using namespace util;

class Channel;
class Poller;
class TimerQueue;
class Epoller;

class EventLoop : public Scheduler {
public:
  using TimerCallback = std::function<void()>;
  using Functor = std::function<void()>;

  EventLoop(const EventLoop &) = delete;
  EventLoop &operator=(const EventLoop &) = delete;
  EventLoop();
  ~EventLoop();

  void loop();

  void quit();

  Timestamp pollReturnTime() const { return pollReturnTime_; }

  // 给其他线程调用
  void runInLoop(const Functor &cb);
  // void runInLoop(Functor &&cb);
  /// 将 cb 放入队列并在必要时唤醒 IO 线程
  void queueInLoop(const Functor &cb);
  // void queueInLoop(Functor &&cb);

  void cancel(TimerId timerId);

  void wakeup();

  TimerId runAt(const Timestamp &time, const TimerCallback &cb);
  TimerId runAfter(double delay, const TimerCallback &cb);
  TimerId runEvery(double interval, const TimerCallback &cb);

  /**
   * @brief Execute 是为了 Future.then 设计的
   * SFINAE 决定执行版本 1 还是版本 2
   * 版本 1 返回值为 Future<T>，版本 2 返回值为 Future<void>
   *
   * @tparam F
   * @tparam Args
   * @tparam std::enable_if<
   * !std::is_void<typename std::result_of<F(Args...)>::type>::value,
   * void>::type
   * @tparam std::enable_if<
   * !std::is_void<typename std::result_of<F(Args...)>::type>::value,
   * void>::type,
   * typename
   * @return Future<typename std::result_of<F(Args...)>::type>
   * TODO 第四个参数需要吗？
   */
  /// 版本 1
  template <typename F, typename... Args,
            typename = typename std::enable_if<
                !std::is_void<typename std::result_of<F(Args...)>::type>::value,
                void>::type,
            typename = void>
  auto Execute(F &&, Args &&...)
      -> Future<typename std::result_of<F(Args...)>::type>;
  /// 版本 2
  template <typename F, typename... Args,
            typename = typename std::enable_if<
                std::is_void<typename std::result_of<F(Args...)>::type>::value,
                void>::type>
  auto Execute(F &&, Args &&...) -> Future<void>;

  void Schedule(std::function<void()>) override;
  void ScheduleLater(std::chrono::milliseconds duration,
                     std::function<void()>) override;

  // 更新 EventLoop 关心的文件描述符事件
  void updateChannel(Channel *channel);
  // 移除 channel
  void removeChannel(Channel *channel);

  // 断言
  void assertInLoopThread() {
    if (!isInLoopThread())
      abortNotInLoopThread();
  }

  // 检查当前线程是不是 EventLoop 所属线程
  bool isInLoopThread() const {
    return std::this_thread::get_id() == threadId_;
  }

  int getId() const { return local_id_; }
  unsigned int getConnectionId() const { return ++s_id; }

  static EventLoop *getEventLoopOfCurrentThread();

private:
  bool looping_;
  bool quit_;
  bool callingPendingFunctors_;

  std::thread::id threadId_; // EventLoop 所属线程编号
  Timestamp pollReturnTime_; // 记录上一次 Poller::poll 返回的时间
  std::unique_ptr<Poller> poller_;         // 执行 IO mutilplexing
  std::unique_ptr<TimerQueue> timerQueue_; // TimerQueue
  int wakeupFd_;                           // eventfd，用来唤醒 IO 线程
  std::unique_ptr<Channel> wakeChannel_; // 处理 wakeupFd_ 上的 readable 事件
  std::vector<Channel *> activeChannels_; // 记录每一次调用 poll 的活动事件
  std::mutex mutex_; // 在多线程间保护 pendingFunctors_
  std::vector<Functor> pendingFunctors_;

  static std::atomic<int> sequenceId;    // 用来给 EventLoop 编号
  static thread_local unsigned int s_id; // 用来给 TcpConnection 编号
  int local_id_;                         // EventLoop 编号

  // 报错
  void abortNotInLoopThread();
  void handleRead();
  void doPendingFunctors();
};

template <typename F, typename... Args, typename, typename>
Future<typename std::result_of<F(Args...)>::type>
EventLoop::Execute(F &&f, Args &&...args) {
  using resultType = typename std::result_of<F(Args...)>::type;

  Promise<resultType> promise;
  auto fut = promise.getFuture();
  if (isInLoopThread()) {
    promise.setValue(std::forward<F>(f)(std::forward<Args>(args)...));
  } else {
    auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    auto func = [t = std::move(task), pm = std::move(promise)]() mutable {
      try {
        pm.setValue(Result<resultType>(t()));
      } catch (...) {
        pm.setException(std::current_exception());
      }
    };

    {
      std::lock_guard<std::mutex> lk(mutex_);
      pendingFunctors_.emplace_back(std::move(func));
    }

    wakeup();
  }

  return fut;
}

template <typename F, typename... Args, typename>
Future<void> EventLoop::Execute(F &&f, Args &&...args) {
  using resultType = typename std::result_of<F(Args...)>::type;
  static_assert(std::is_void<resultType>::value, "must be void");

  Promise<void> promise;
  auto fut = promise.getFuture();
  if (isInLoopThread()) {
    std::forward<F>(f)(std::forward<Args>(args)...);
    promise.setValue();
  } else {
    auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    auto func = [t = std::move(task), pm = std::move(promise)]() mutable {
      try {
        t();
        pm.setValue();
      } catch (...) {
        pm.setException(std::current_exception());
      }
    };

    {
      std::lock_guard<std::mutex> lk(mutex_);
      pendingFunctors_.emplace_back(std::move(func));
    }

    wakeup();
  }

  return fut;
}

} // namespace net
} // namespace lrpc

#endif