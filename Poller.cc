#include "Poller.h"
#include "Channel.h"
#include "boost/log/trivial.hpp"

#include <assert.h>
#include <poll.h>

using namespace imitate_muduo;

Poller::Poller(EventLoop *loop) : ownerloop_(loop) {}

// poll 是 Poller 的核心函数，负责调用 poll(2) 获取当前活动的 IO 事件
// 并填充调用方传入的 activeChannels，activeChannels 一开始是空的
// @return poll(2) 返回的时刻，这里使用 steady_clock 这个稳定时钟
std::chrono::steady_clock::time_point Poller::poll(int timeoutMs, std::vector<Channel *> &activeChannels) {
  // poll(2) 系统调用返回发生事件的文件描述符数量
  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs);
  auto now = std::chrono::steady_clock::now();
  if (numEvents > 0) {
    BOOST_LOG_TRIVIAL(trace) << numEvents << " events happened";
    fillActiveChannels(numEvents, activeChannels);
  } else if (numEvents == 0) {
    BOOST_LOG_TRIVIAL(trace) << " nothing happened";
  } else {
    BOOST_LOG_TRIVIAL(error) << "Poller::poll()";
  }
  return now;
}

// 遍历 pollfds_ 数组，找出有活动事件的 fd，并把它对应的 Channel 填入 activeChannels
// 当前活动事件会保存的 Channel 中，供 Channel::handleEvent() 分发
void Poller::fillActiveChannels(int numEvents,
                                std::vector<Channel *> &activeChannels) const {
  for (const auto &pfd : pollfds_) {
    if (numEvents == 0)
      break;
    if (pfd.revents > 0) {
      --numEvents;
      // 找到 channels_ 中保存的 fd 对应的 Channel
      auto ch = channels_.find(pfd.fd);
      assert(ch != channels_.end());
      Channel *channel = ch->second;

      // 设置 Channel 的当前活动事件
      channel->set_revents(pfd.revents);
      activeChannels.push_back(channel);
    }
  }
}

// 负责维护和更新 pollfds_ 数组，例如添加、更新等
// note: 这里不需要传入 fd，因为在 Channel 类中已经包含了
void Poller::updateChannel(Channel *channel) {
  assertInLoopThread();
  BOOST_LOG_TRIVIAL(trace) << "fd = " << channel->fd()
                           << " events = " << channel->events();
  // 添加新的映射
  if (channel->index() < 0) {
    // 确保此前没有建立过映射
    assert(channels_.find(channel->fd()) == channels_.end());

    // 创建新的 pollfd 结构体
    struct pollfd pfd;
    pfd.fd = channel->fd();
    // 设置关心的 IO 事件
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;

    // 设置 channel 编号
    int idx = static_cast<int>(pollfds_.size()) - 1;
    channel->set_index(idx);

    // 创建映射
    pollfds_.push_back(pfd);
    channels_[pfd.fd] = channel;
  }
  // 更新一个已经存在的
  else {
    // 需要确保
    // 1. channels_ 中存在映射
    // 2. channel 的 fd 和待更新的 fd 相同
    // 3. pollfds_ 中保存的那个 pollfd 的 fd 与待更新的相同或者是 -1
    assert(channels_.find(channel->fd()) != channels_.end());
    assert(channels_[channel->fd()] == channel);

    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));

    struct pollfd &pfd = pollfds_[idx];
    // -1 相当于不监听这个文件描述符
    assert(pfd.fd == channel->fd() || pfd.fd == -1);

    // 确保上述断言没有异常之后就可以更新
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    if (channel->isNoneEvent()) {
      pfd.fd = -1;
    }
  }
}