#include "TcpClient.h"
#include "Connector.h"
#include "EventLoop.h"
#include "Logging.h"
#include "SocketsOps.h"
#include <cstdio>
#include <functional>

namespace lrpc {

namespace detail {
using namespace lrpc::net;

void removeConnection(EventLoop *loop, const TcpConnectionPtr &conn) {
  loop->queueInLoop(std::bind(&TcpConnection::connectDestoryed, conn));
}

void removeConnector(const ConnectorPtr &connector) {}

} // namespace detail

namespace net {

/**
 * @brief Construct a new Tcp Client:: Tcp Client object
 * first create a Tcp Client object, which will initialize the Connector,
 * and set connection connected callback
 *
 * @param loop
 * @param serverAddr
 */
TcpClient::TcpClient(EventLoop *loop, const InetAddress &serverAddr)
    : loop_(CHECK_NOTNULL(loop)), connector_(new Connector(loop, serverAddr)),
      retry_(false), connect_(true), nextConnId_(1) {
  connector_->setNewConnectionCallback(
      std::bind(&TcpClient::newConnection, this, std::placeholders::_1));
  LOG_INFO << "TcpClient::TcpClient[" << this << "] - connector "
           << connector_.get();
}

TcpClient::~TcpClient() {
  LOG_INFO << "TcpClient::~TcpClient[" << this << "] - connector "
           << connector_.get();
  TcpConnectionPtr conn;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    conn = connection_;
  }
  if (conn) {
    CloseCallback cb = std::bind(&lrpc::detail::removeConnection, loop_,
                                 std::placeholders::_1);
    loop_->runInLoop(std::bind(&TcpConnection::setCloseCallback, conn, cb));
  } else {
    connector_->stop();
    loop_->runAfter(1, std::bind(&lrpc::detail::removeConnector, connector_));
  }
}

/// @brief establish socket connection
void TcpClient::connect() {
  LOG_INFO << "TcpCLient::connect[" << this << "] - connecting to "
           << connector_->serverAddress().toHostPort();
  connect_ = true;
  connector_->start();
}

/// @brief turn-off socket connection (write)
void TcpClient::disconnect() {
  connect_ = false;
  std::lock_guard<std::mutex> lk(mutex_);
  if (connection_) {
    connection_->shutdown();
  }
}

/// @brief if connecting, then stop
void TcpClient::stop() {
  connect_ = false;
  connector_->stop();
}

/// When socket connection is completed, this function will be called.
/// It will create TcpConnection object and set its callback.
/// These callbacks come from users, except CloseCallback
void TcpClient::newConnection(int sockfd) {
  loop_->assertInLoopThread();
  // 获取 peer ip 和 port
  InetAddress peerAddr(sockets::getPeerAddr(sockfd));
  char buf[32];
  sprintf(buf, ":%s#%d", peerAddr.toHostPort().c_str(), nextConnId_++);
  string connName = buf;
  // 获取 local ip 和 port
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // 创建 TcpConnection
  TcpConnectionPtr conn(
      new TcpConnection(loop_, connName, sockfd, localAddr, peerAddr));
  // 设置 TcpConnection 回调函数
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(
      std::bind(&TcpClient::removeConnection, this, std::placeholders::_1));

  {
    std::lock_guard<std::mutex> lk(mutex_);
    connection_ = conn;
  }
  // 成功建立连接回调和状态设置
  conn->connecEstablished();
}

/// 连接被关闭的时候会被调用
void TcpClient::removeConnection(const TcpConnectionPtr &conn) {
  loop_->assertInLoopThread();
  assert(loop_ == conn->getLoop());

  {
    std::lock_guard<std::mutex> lk(mutex_);
    assert(connection_ == conn);
    connection_.reset();
  }

  // 从 EventLoop 中移除这条连接的所有事件监听
  loop_->queueInLoop(std::bind(&TcpConnection::connectDestoryed, conn));
  if (retry_ && connect_) {
    LOG_INFO << "TcpClient::connect[" << this << "] - Reconnecting to "
             << connector_->serverAddress().toHostPort();
    connector_->restart();
  }
}

} // namespace net

} // namespace lrpc