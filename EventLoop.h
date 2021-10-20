#ifndef IMITATE_MUDUO_EVENTLOOP_H
#define IMITATE_MUDUO_EVENTLOOP_H

#include <memory>
#include <thread>
#include <vector>
#include <functional>

namespace imitate_muduo {

class Channel;
class Poller;
class TimerQueue;

class EventLoop {
public:
  using TimerCallback = std::function<void()>;
  
private:
  bool looping_;
  bool quit_;
  // bool callingPendingFunctors_;

  // EventLoop 所属线程编号
  std::thread::id threadId_;
  // 记录上一次 Poller::poll 返回的时间
  std::chrono::steady_clock::time_point pollReturnTime_;
  // 执行 IO mutilplexing
  std::unique_ptr<Poller> poller_;
  // TimerQueue
  std::unique_ptr<TimerQueue> timerQueue_;
  // 记录每一次调用 poll 的活动事件
  std::vector<Channel *> activeChannels_;

  // 报错
  void abortNotInLoopThread();

public:
  EventLoop(const EventLoop &) = delete;
  EventLoop &operator=(const EventLoop &) = delete;
  EventLoop();
  ~EventLoop();

  void loop();

  void quit();

  std::chrono::steady_clock::time_point pollReturnTime() const {
    return pollReturnTime_;
  }

  void runAt(const std::chrono::steady_clock::time_point &time, const TimerCallback &cb);
  void runAfter(double delay, const TimerCallback &cb);
  void runEvery(double interval, const TimerCallback &cb);

  // 更新 EventLoop 关心的文件描述符事件
  void updateChannel(Channel *channel);

  // 断言
  void assertInLoopThread() {
    if (!isInLoopThread())
      abortNotInLoopThread();
  }

  // 检查当前线程是不是 EventLoop 所属线程
  bool isInLoopThread() const {
    return std::this_thread::get_id() == threadId_;
  }

  static EventLoop *getEventLoopOfCurrentThread();
};
} // namespace imitate_muduo

#endif