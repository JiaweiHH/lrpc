#include "Connector.h"
#include "Channel.h"
#include "EventLoop.h"
#include "Logging.h"
#include "SocketsOps.h"
#include <cerrno>
#include <functional>

using namespace lrpc::net;

const int Connector::kMaxRetryDelayMs;

Connector::Connector(EventLoop *loop, const InetAddress &serverAddr)
    : loop_(loop), serverAddr_(serverAddr), connect_(false),
      state_(States::kDisconnected), retryDelayMs_(kInitRetryDelayMs) {
  LOG_DEBUG << "ctor[" << this << "]";
}
Connector::~Connector() {
  LOG_DEBUG << "dtor[" << this << "]";
  loop_->cancel(timerId_);
  assert(!channel_);
}

/// @brief 不断尝试建立连接，每次 retry 延迟时间加倍直到最大延迟时间
void Connector::start() {
  connect_ = true;
  loop_->runInLoop(std::bind(&Connector::startInLoop, this));
}
void Connector::startInLoop() {
  loop_->assertInLoopThread();
  assert(state_ == States::kDisconnected);
  if (connect_)
    connect();
  else
    LOG_DEBUG << "do not connect";
}

/// 发起连接
void Connector::connect() {
  int sockfd = sockets::createNonblockingOrDie();
  int ret = sockets::connect(sockfd, serverAddr_.getSockAddrInet());
  int savedErrno = (ret == 0) ? 0 : errno;
  switch (savedErrno) {
  case 0:
  case EINPROGRESS:
  case EINTR:
  case EISCONN:
    connecting(sockfd);
    break;

  case EAGAIN:
  case EADDRINUSE:
  case EADDRNOTAVAIL:
  case ECONNREFUSED:
  case ENETUNREACH:
    retry(sockfd);
    break;

  case EACCES:
  case EPERM:
  case EAFNOSUPPORT:
  case EALREADY:
  case EBADF:
  case EFAULT:
  case ENOTSOCK:
    LOG_ERROR << "connect error in Connector::startInLoop " << savedErrno;
    sockets::close(sockfd);
    break;

  default:
    LOG_ERROR << "Unexpected error in Connector::startInLoop " << savedErrno;
    sockets::close(sockfd);
    // connectErrorCallback_();
    break;
  }
}

void Connector::restart() {
  loop_->assertInLoopThread();
  setState(States::kDisconnected);
  retryDelayMs_ = kInitRetryDelayMs;
  connect_ = true;
  startInLoop();
}

/// 停止连接
void Connector::stop() {
  connect_ = false;
  loop_->cancel(timerId_);
}

/// 等待连接建立，监听 sockfd 连接建立事件
void Connector::connecting(int sockfd) {
  setState(States::kConnecting);
  assert(!channel_);
  channel_.reset(new Channel(loop_, sockfd));
  // 设置回调，等待连接建立成功
  channel_->setWriteCallback(std::bind(&Connector::handleWrite, this));
  channel_->setErrorCallback(std::bind(&Connector::handleError, this));
  channel_->enableWriting();
}

/// 连接建立成功之后会移除 sockfd 的 channel，不再监听连接建立事件
/// 之后会对 sockfd 创建一个新的 channel 用来监听读写事件
int Connector::removeAndResetChannel() {
  channel_->disableAll();
  loop_->removeChannel(channel_.get());
  int sockfd = channel_->fd();
  loop_->queueInLoop(std::bind(&Connector::resetChannel, this));
  return sockfd;
}

void Connector::resetChannel() { channel_.reset(); }

/// 当 sockfd 可写的时候说明连接建立成功了
void Connector::handleWrite() {
  LOG_TRACE << "Connector::handleWrite ";
  if (state_ == States::kConnecting) {
    // 连接已建立，那么取消监听该 sockfd 的连接建立事件
    int sockfd = removeAndResetChannel();
    int err = sockets::getSocketError(sockfd);
    if (err) {
      LOG_WARN << "Connector::handleWrite - SO_ERROR = " << err;
      retry(sockfd);
    } else if (sockets::isSelfConnect(sockfd)) {
      LOG_WARN << "Connector::handleWrite - Self connect";
      retry(sockfd);
    } else {
      setState(States::kConnected);
      if (connect_) {
        newConnectionCallback_(sockfd);
      } else {
        sockets::close(sockfd);
      }
    }
  } else {
    // what happened ?
    assert(state_ == States::kDisconnected);
  }
}

void Connector::handleError() {
  LOG_ERROR << "Connector::handleError";
  assert(state_ == States::kConnecting);
  int sockfd = removeAndResetChannel();
  int err = sockets::getSocketError(sockfd);
  LOG_TRACE << "SO_ERROR = " << err;
  retry(sockfd);
}

void Connector::retry(int sockfd) {
  sockets::close(sockfd);
  setState(States::kDisconnected);
  if (connect_) {
    LOG_INFO << "Connector::retry - Retry connecting to "
             << serverAddr_.toHostPort() << " in " << retryDelayMs_
             << " milliseconds. ";
    // 等待一段时间之后重新尝试建立连接
    timerId_ = loop_->runAfter(retryDelayMs_ / 1000.0,
                               std::bind(&Connector::startInLoop, this));
    retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
  } else {
    LOG_DEBUG << "do not connect";
  }
}