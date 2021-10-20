#ifndef IMITATE_MUDUO_POLLER_H
#define IMITATE_MUDUO_POLLER_H

#include "EventLoop.h"

#include <map>
#include <vector>

struct pollfd;  // 来自 poll.h

namespace imitate_muduo {

class Channel;

/// IO multiplexing 的封装
/// Poller 只会在 ownerLoop 中被调用
class Poller {
private:
  void fillActiveChannels(int numEvents,
                          std::vector<Channel *> &activeChannels) const;
  EventLoop *ownerloop_;
  // Poller 关心的 fds 集合
  std::vector<struct pollfd> pollfds_;
  // 每一个 fd 对应一个 Channel
  std::map<int, Channel *> channels_;

public:
  Poller(const Poller &) = delete;
  Poller &operator=(const Poller &) = delete;
  Poller(EventLoop *loop);
  ~Poller() = default;

  std::chrono::steady_clock::time_point poll(int timeoutMS, std::vector<Channel *> &activeChannels);

  void updateChannel(Channel *channel);

  void assertInLoopThread() {
    ownerloop_->assertInLoopThread();
  }
};

} // namespace imitate_muduo

#endif