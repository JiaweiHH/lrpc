#include "Channel.h"
#include "EventLoop.h"
#include "Logging.h"

#include <poll.h>

using namespace lrpc::net;

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kWriteEvent = POLLOUT;

Channel::Channel(EventLoop *loop, int fdArg)
    : loop_(loop), fd_(fdArg), events_(0), revents_(0), index_(-1),
      eventHanding_(false) {}

Channel::~Channel() { assert(!eventHanding_); }

void Channel::update() { loop_->updateChannel(this); }

void Channel::handleEvent(Timestamp recieveTime) {
  eventHanding_ = true;
  // 指定的文件描述符非法
  if (revents_ & POLLNVAL) {
    LOG_WARN << "Channel::handle_event() POLLNVAL";
  }

  if ((revents_ & POLLHUP) && !(revents_ & POLLIN)) {
    LOG_WARN << "Channel::handle_event() POLLHUP";
    if (closeCallback_)
      closeCallback_();
  }

  // 指定的文件描述符发生错误 或 指定的文件描述符非法
  if (revents_ & (POLLERR | POLLNVAL)) {
    if (errorCallback_)
      errorCallback_();
  }
  // 有数据可读 或 有紧急数据可读 或 指定的文件描述符挂起事件
  if (revents_ & (POLLIN | POLLPRI | POLLRDHUP)) {
    if (readCallback_)
      readCallback_(recieveTime);
  }
  // 写数据不会导致阻塞
  if (revents_ & POLLOUT) {
    if (writeCallback_)
      writeCallback_();
  }
  eventHanding_ = false;
}