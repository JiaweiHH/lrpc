#ifndef IMITATE_MUDUO_EPOLLER_H
#define IMITATE_MUDUO_EPOLLER_H

#include "Callback.h"
#include "EventLoop.h"

#include <map>
#include <vector>

struct epoll_event; // 来自 poll.h

namespace lrpc {
namespace net {

class Channel;

/// IO multiplexing 的封装
/// Poller 只会在 ownerLoop 中被调用
class Epoller {
private:
  static const int kInitEventListSize = 16;
  void fillActiveChannels(int numEvents,
                          std::vector<Channel *> &activeChannels) const;
  void update(int operation, Channel *channel);
  EventLoop *ownerloop_;
  int epollfd_;
  // epoll_wait 调用返回的活动 fd 列表
  // epoll_event::epoll_data_t::*ptr 存放 Channel *
  std::vector<struct epoll_event> events_;
  // 每一个 fd 对应一个 Channel
  std::map<int, Channel *> channels_;

public:
  Epoller(const Epoller &) = delete;
  Epoller &operator=(const Epoller &) = delete;
  Epoller(EventLoop *loop);
  ~Epoller();

  Timestamp poll(int timeoutMS, std::vector<Channel *> &activeChannels);

  void updateChannel(Channel *channel);
  void removeChannel(Channel *channel);

  void assertInLoopThread() { ownerloop_->assertInLoopThread(); }
};

} // namespace net
} // namespace lrpc

#endif