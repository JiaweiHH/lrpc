#ifndef IMITATE_MUDUO_ACCEPTOR_H
#define IMITATE_MUDUO_ACCEPTOR_H

#include "Channel.h"
#include "Socket.h"

namespace lrpc {
namespace net {

class EventLoop;
class InetAddress;

/// 用于 accept 新连接
/// Channel belongs Socket -> wait socket readable event -> call
/// Acceptor::handleRead()
/// -> call accept(2) and call user's functionCallback
class Acceptor : public std::enable_shared_from_this<Acceptor> {
public:
  using NewConnectionCallback = std::function<void(int, const InetAddress &)>;
  Acceptor(const Acceptor &) = delete;
  Acceptor &operator=(const Acceptor &) = delete;
  Acceptor(EventLoop *loop, const InetAddress &listenAddr);

  void setNewConnectionCallback(const NewConnectionCallback &cb) {
    newConnectionCallback_ = cb;
  }

  bool listenning() const { return listenning_; }

  void listen();

private:
  void handleRead();

  EventLoop *loop_;
  Socket acceptSocket_; // server socket
  Channel acceptChannel_;
  NewConnectionCallback newConnectionCallback_;
  bool listenning_;
};

} // namespace net
} // namespace lrpc

#endif