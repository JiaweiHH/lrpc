#ifndef IMITATE_MUDUO_CHANNEL_H
#define IMITATE_MUDUO_CHANNEL_H

#include "Callback.h"
#include <functional>

namespace imitate_muduo {

class EventLoop;

/// Channel 负责一个 fd 的事件分发
class Channel {
public:
  // TODO 貌似 std::function 的性能不如 lambda，考虑这里能不能优化一下
  using EventCallback = std::function<void()>; // 事件回调类型
  using ReadEventCallback = std::function<void(Timestamp)>;

private:
  void update();
  static const int kNoneEvent;
  static const int kReadEvent;
  static const int kWriteEvent;
  EventLoop *loop_;
  // 每个 Channel 对象只负责一个 fd 的 IO 事件分发，但是并不拥有这个 fd
  const int fd_;
  int events_;  // Channel 关心的 IO 事件
  int revents_; // 目前活动的事件
  int index_;   // 被 Poller 使用

  bool eventHanding_;

  // 事件回调函数
  ReadEventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback errorCallback_;
  EventCallback closeCallback_;

public:
  Channel(const Channel &) = delete;
  Channel &operator=(const Channel &) = delete;
  Channel(EventLoop *loop, int fd);
  ~Channel();

  void handleEvent(Timestamp);

  void setReadCallback(const ReadEventCallback &cb) { readCallback_ = cb; }
  void setWriteCallback(const EventCallback &cb) { writeCallback_ = cb; }
  void setErrorCallback(const EventCallback &cb) { errorCallback_ = cb; }
  void setCloseCallback(const EventCallback &cb) { closeCallback_ = cb; }

  int fd() const { return fd_; }
  int events() const { return events_; }
  void set_revents(int revt) { revents_ = revt; }
  bool isNoneEvent() const { return events_ == kNoneEvent; }

  // 在这里会更新 EventLoop 关心的文件描述符以及其上发生的 IO 事件
  void enableReading() {
    events_ |= kReadEvent;
    update();
  }

  /// 取消一个文件描述符的所有事件
  void disableAll() {
    events_ = kNoneEvent;
    update();
  }

  int index() const { return index_; }
  void set_index(int idx) { index_ = idx; }
  EventLoop *ownerLoop() const { return loop_; }

};

} // namespace imitate_muduo

#endif