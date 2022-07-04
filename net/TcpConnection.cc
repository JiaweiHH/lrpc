#include "TcpConnection.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Socket.h"
#include "SocketsOps.h"
#include "Logging.h"
#include <cerrno>
#include <unistd.h>
#include <cstdio>
#include <string>

using namespace lrpc::net;

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
      localAddr_(localAddr), peerAddr_(peerAddr), uniqueId_(0) {
  LOG_DEBUG << "TcpConnection::ctor[" << name_ << "] at " << this
                           << " fd = " << sockfd;
  channel_->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
  channel_->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
  channel_->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
  channel_->setErrorCallback(std::bind(&TcpConnection::handleError, this));
}

TcpConnection::~TcpConnection() {
  LOG_DEBUG << "TcpConnection::dtor[" << name_ << "] at " << this
                           << " fd=" << channel_->fd();
}

/// 由 TcpServer 调用
/// 注册 client socket 到 EventLoop，执行用户回调
void TcpConnection::connecEstablished() {
  loop_->assertInLoopThread();
  assert(state_ == StateE::kConnecting);
  setState(StateE::kConnected);
  channel_->enableReading();
  if (connectionCallback_)
    connectionCallback_(shared_from_this());
}

/// TcpConnection 析构前最后调用的一个函数，通知用户连接已断开
/// 在某些情况下可以不经由 handleClose 而直接调用 connectionDestory()
/// 由 TcpServer 调用, 取消 client socket 上的所有回调，并从 EventLoop 中移除
void TcpConnection::connectDestoryed() {
  loop_->assertInLoopThread();
  assert(state_ == StateE::kConnected || state_ == StateE::kDisConnecting);
  setState(StateE::kDisConnected);
  // client 断开链接，所以取消 client socket 上的所有事件
  channel_->disableAll();
  
  // if (connectionCallback_)
  //   connectionCallback_(shared_from_this());

  // 从 EventLoop 中移除
  loop_->removeChannel(channel_.get());
}

/// 客户端有新的数据发送过来，服务端的 client socket 可读
/// 读取这部分数据到 inputBuffer_
void TcpConnection::handleRead(Timestamp recieveTime) {
  int savedErrno = 0;
  ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
  if (n > 0) {
    messageCallback_(shared_from_this(), &inputBuffer_, recieveTime);
  } else if (n == 0) {
    handleClose();
  } else {
    errno = savedErrno;
    LOG_ERROR << "TcpConnection::handleRead";
    handleError();
  }
}

/// Channel 变的可写的时候会调用 TcpConnection::handleWrite
/// 发送 ouputBuffer_ 中的数据
/// 一旦数据发送完毕就立刻停止观察 writable 事件，避免 busy loop
void TcpConnection::handleWrite() {
  loop_->assertInLoopThread();
  if (channel_->isWriting()) {
    ssize_t n = ::write(channel_->fd(), outputBuffer_.peek(), outputBuffer_.readableBytes());
    if (n > 0) {
      outputBuffer_.retrieve(n);
      // 如果数据写完了，则需要关闭 kWriteEvent 事件，并执行 writeCompleteCallback_ 回调
      if (outputBuffer_.readableBytes() == 0) {
        channel_->disableWriting();
        if (writeCompleteCallback_) {
          loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
        }
        if (state_ == StateE::kDisConnecting)
          shutdownInLoop();
      } else {
        LOG_TRACE << "I am going to write more data";
      }
    } else {
      LOG_ERROR << "TcpConnection::handleWrite";
    }
  } else {
    LOG_TRACE << "Connection is down, no more writing";
  }
}

void TcpConnection::handleClose() {
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpConnection::handleClose state = " << stateEnumToStr(state_);
  assert(state_ == StateE::kConnected || state_ == StateE::kDisConnecting);
  channel_->disableAll();
  // closeCallback_ 绑定到 TcpServer::removeConnection
  closeCallback_(shared_from_this());
}

void TcpConnection::handleError() {
  int err = sockets::getSocketError(channel_->fd());
  char t_errnobuf[512];
  LOG_ERROR << "TcpConnection::handleError [" << name_
                           << "] - SO_ERROR = " << err << " "
                           << strerror_r(err, t_errnobuf, sizeof t_errnobuf);
}

/// 关闭 client sockets 的写
void TcpConnection::shutdown() {
  if (state_ == StateE::kConnected) {
    setState(StateE::kDisConnecting);
    loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
  }
}
void TcpConnection::shutdownInLoop() {
  loop_->assertInLoopThread();
  if (!channel_->isWriting())
    socket_->shutdownWrite();
}

/// 发送数据
/// 如果在非 IO 线程调用，会把 message 复制一份传递给 IO 线程中的 sendInLoop 来发送
bool TcpConnection::send(const std::string &message) {
  if (state_ == StateE::kConnected) {
    if (loop_->isInLoopThread()) {
      sendInLoop(message);
    } else {
      // message 会被移动到 std::bind 对象的成员变量中
      loop_->runInLoop(std::bind(&TcpConnection::sendInLoop, this, std::move(message)));
    }
    return true;
  }
  return false;
}
bool TcpConnection::send(Buffer &message) {
  return send(message.retrieveAsString());
}
void TcpConnection::sendInLoop(const std::string &message) {
  loop_->assertInLoopThread();
  ssize_t nwrote = 0;
  // 如果没有等待发送的数据，尝试直接发送数据
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
    nwrote = ::write(channel_->fd(), message.data(), message.size());
    if (nwrote >= 0) {
      // 数据没有发送完全
      if (static_cast<size_t>(nwrote) < message.size()) {
        LOG_TRACE << "I am going to write more data";
      } else if (writeCompleteCallback_) {
        loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
      }
    } else {
      nwrote = 0;
      if (errno != EWOULDBLOCK) {
        LOG_ERROR << "TcpConnection::sendInLoop";
      }
    }
  }

  assert(nwrote >= 0);
  // 数据没有发送完全，则先放到 outputBuffer_ 中，并监听 write 事件
  // 当 socket 变得可写的时候 Channel 会调用 TcpConnection::handleWrite()
  if (static_cast<size_t>(nwrote) < message.size()) {
    outputBuffer_.append(message.data() + nwrote, message.size() - nwrote);
    if (!channel_->isWriting()) {
      channel_->enableWriting();
    }
  }
}

void TcpConnection::setTcpNoDelay(bool on) {
  socket_->setTcpNoDelay(on);
}