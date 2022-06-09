#include "Epoller.h"
#include "Channel.h"
#include "Logging.h"
#include <assert.h>
#include <unistd.h>
#include <cerrno>
#include <poll.h>
#include <sys/epoll.h>

using namespace lrpc::net;

static_assert(EPOLLIN == POLLIN);
static_assert(EPOLLPRI == POLLPRI);
static_assert(EPOLLOUT == POLLOUT);
static_assert(EPOLLRDHUP == POLLRDHUP);
static_assert(EPOLLERR == POLLERR);
static_assert(EPOLLHUP == POLLHUP);

namespace {
/// Epoll 模式下，Channel::index 的意义和 Poll 模式下不一样
/// 在 Poll 模式下，index 表示文件描述符在 pollfd 数组中的索引
/// 在 Epoll 模式下，index 表示该 Channel 的状态
const int kNew = -1;
const int kAdded = 1;
const int kDeleted = 2;

} // namespace

Epoller::Epoller(EventLoop *loop)
    : ownerloop_(loop), epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
      events_(kInitEventListSize) {
  if (epollfd_ < 0)
    LOG_ERROR << "Epoller::Epoller";
}

Epoller::~Epoller() { ::close(epollfd_); }

Timestamp Epoller::poll(int timeoutMs, std::vector<Channel *> &activeChannels) {
  // 接受活动 fd，填充到 events_
  int numEvents = ::epoll_wait(epollfd_, &*events_.begin(),
                               static_cast<int>(events_.size()), timeoutMs);
  auto now(Timestamp::now());
  if (numEvents > 0) {
    LOG_TRACE << numEvents << " events happended";
    fillActiveChannels(numEvents, activeChannels);
    // 如果当前活动 fd 的数目填满了 events_，那么下次就尝试接受更多的活动 fd
    if (static_cast<size_t>(numEvents) == events_.size())
      events_.resize(events_.size() * 2);
  } else if (numEvents == 0) {
    LOG_TRACE << " nothing happended";
  } else {
    LOG_ERROR << "Epoller::poll()";
  }
  return now;
}

/// 将 events_ 中的活动 fd 填入 activeChannels
void Epoller::fillActiveChannels(int numEvents,
                                 std::vector<Channel *> &activeChannels) const {
  assert(static_cast<size_t>(numEvents) <= events_.size());
  for (int i = 0; i < numEvents; ++i) {
    Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
#ifndef NDEBUG
    int fd = channel->fd();
    auto it = channels_.find(fd);
    assert(it != channels_.end());
    assert(it->second == channel);
#endif
    channel->set_revents(events_[i].events);
    activeChannels.push_back(channel);
  }
}

/// 更新 channel，最后会更新 epoll 中的监听事件
void Epoller::updateChannel(Channel *channel) {
  assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd()
                           << " events = " << channel->events();
  const int index = channel->index();
  if (index == kNew || index == kDeleted) {
    // 添加一个新的 Channel
    // 一个全新的 Channel 或者一个已经删除过的 Channel
    int fd = channel->fd();
    if (index == kNew) {
      assert(channels_.find(fd) == channels_.end());
      // 添加新的 fd 和 channel 的映射
      channels_[fd] = channel;
    } else { // index == kDeleted
      assert(channels_.find(fd) != channels_.end());
      assert(channels_[fd] == channel);
    }
    // 完成了
    channel->set_index(kAdded);
    // 更新 epoll 监听列表
    update(EPOLL_CTL_ADD, channel);
  } else {
    // 更新一个已经存在 Channel
    int fd = channel->fd();
    assert(channels_.find(fd) != channels_.end());
    assert(channels_[fd] == channel);
    assert(index == kAdded);
    // 更新 epoll 监听列表
    if (channel->isNoneEvent()) {
      update(EPOLL_CTL_DEL, channel);
      channel->set_index(kDeleted);
    } else {
      update(EPOLL_CTL_MOD, channel);
    }
  }
}

/// 移除 channel_ 中 channel 对应的映射
void Epoller::removeChannel(Channel *channel) {
  assertInLoopThread();
  int fd = channel->fd();
  LOG_TRACE << "fd = " << fd;
  // 条件检查
  assert(channels_.find(fd) != channels_.end());
  assert(channels_[fd] == channel);
  assert(channel->isNoneEvent());

  int index = channel->index();
  assert(index == kAdded || index == kDeleted);
  size_t n = channels_.erase(fd);
  assert(n == 1);

  // 如果 channel 的状态不是 kDeleted，说明 epoll 中还没有删除该文件描述符
  // 那么就会删除文件描述符
  if (index == kAdded)
    update(EPOLL_CTL_DEL, channel);
  channel->set_index(kNew);
}

/// 更新 epoll 监听的文件描述符，包括文件描述符上面监听的事件
void Epoller::update(int operation, Channel *channel) {
  struct epoll_event event;
  bzero(&event, sizeof event);
  event.events = channel->events();
  event.data.ptr = channel;
  int fd = channel->fd();
  if (::epoll_ctl(epollfd_, operation, fd, &event) < 0) {
    if (operation == EPOLL_CTL_DEL)
      LOG_ERROR << "epoll_ctl op=" << operation << " fd=" << fd;
    else
      LOG_FATAL << "epoll_ctl op=" << operation << " fd=" << fd;
  }
}