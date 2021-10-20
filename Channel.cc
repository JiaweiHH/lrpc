#include "Channel.h"
#include "EventLoop.h"
#include <boost/log/trivial.hpp>

#include <poll.h>

using namespace imitate_muduo;

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kWriteEvent = POLLOUT;

Channel::Channel(EventLoop *loop, int fdArg)
    : loop_(loop), fd_(fdArg), events_(0), revents_(0), index_(-1) {}

void Channel::update() { loop_->updateChannel(this); }

void Channel::handleEvent() {
  // 指定的文件描述符非法
  if (revents_ & POLLNVAL) {
    BOOST_LOG_TRIVIAL(warning) << "Channel::handle_event() POLLNVAL";
  }
  // 指定的文件描述符发生错误 或 指定的文件描述符非法
  if (revents_ & (POLLERR | POLLNVAL)) {
    if (errorCallback_)
      errorCallback_();
  }
  // 有数据可读 或 有紧急数据可读 或 指定的文件描述符挂起事件
  if (revents_ & (POLLIN | POLLPRI | POLLRDHUP)) {
    if (readCallback_)
      readCallback_();
  }
  // 写数据不会导致阻塞
  if (revents_ & POLLOUT) {
    if (writeCallback_)
      writeCallback_();
  }
}