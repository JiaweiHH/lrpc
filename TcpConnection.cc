#include "TcpConnection.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Socket.h"
#include "SocketsOps.h"
#include <boost/log/trivial.hpp>
#include <cerrno>
#include <cstdio>
#include <string>

using namespace imitate_muduo;

std::string TcpConnection::stateEnumToStr(StateE state) {
  std::string str;
  switch (state) {
  case StateE::kConnected:
    str = std::string("kConnected");
    break;
  case StateE::kConnecting:
    str = std::string("kConnecting");
    break;
  case StateE::kDisConnected:
    str = std::string("kDisConnected");
    break;
  default:
    str = std::string("unknow");
    break;
  }
  return str;
}

TcpConnection::TcpConnection(EventLoop *loop, const std::string &nameArg,
                             int sockfd, const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(loop), name_(nameArg), state_(StateE::kConnecting),
      socket_(new Socket(sockfd)), channel_(new Channel(loop, sockfd)),
      localAddr_(localAddr), peerAddr_(peerAddr) {
  BOOST_LOG_TRIVIAL(debug) << "TcpConnection::ctor[" << name_ << "] at" << this
                           << " fd = " << sockfd;
  channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this));
  channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
  channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
  channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));
}

TcpConnection::~TcpConnection() {
  BOOST_LOG_TRIVIAL(debug) << "TcpConnection::dtor[" << name_ << "] at " << this
                           << " fd=" << channel_->fd();
}

/// 由 TcpServer 调用
/// 注册 client socket 到 EventLoop，执行用户回调
void TcpConnection::connecEstablished() {
  loop_->assertInLoopThread();
  assert(state_ == StateE::kConnecting);
  setState(StateE::kConnected);
  channel_->enableReading();

  connectionCallback_(shared_from_this());
}

/// TcpConnection 析构前最后调用的一个函数，通知用户连接已断开
/// 在某些情况下可以不经由 handleClose 而直接调用 connectionDestory()
/// 由 TcpServer 调用, 取消 client socket 上的所有回调，并从 EventLoop 中移除
void TcpConnection::connectDestoryed() {
  loop_->assertInLoopThread();
  assert(state_ == StateE::kConnected);
  setState(StateE::kDisConnected);
  // client 断开链接，所以取消 client socket 上的所有事件
  channel_->disableAll();
  connectionCallback_(shared_from_this());
  // 从 EventLoop 中移除
  loop_->removeChannel(channel_.get());
}

void TcpConnection::handleRead() {
  char buf[65536];
  ssize_t n = ::read(channel_->fd(), buf, sizeof buf);
  if (n > 0) {}
    // messageCallback_(shared_from_this(), buf, n);
  else if (n == 0)
    handleClose();
  else
    handleError();
}

void TcpConnection::handleWrite() {}

void TcpConnection::handleClose() {
  loop_->assertInLoopThread();
  BOOST_LOG_TRIVIAL(trace) << "TcpConnection::handleClose state = " << stateEnumToStr(state_);
  assert(state_ == StateE::kConnected);
  channel_->disableAll();
  // closeCallback_ 绑定到 TcpServer::removeConnection
  closeCallback_(shared_from_this());
}

void TcpConnection::handleError() {
  int err = sockets::getSocketError(channel_->fd());
  char t_errnobuf[512];
  BOOST_LOG_TRIVIAL(error) << "TcpConnection::handleError [" << name_
                           << "] - SO_ERROR = " << err << " "
                           << strerror_r(err, t_errnobuf, sizeof t_errnobuf);
}