#include "TcpServer.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "SocketsOps.h"
#include <boost/log/trivial.hpp>
#include <cstdio>

using namespace imitate_muduo;

TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr)
    : loop_(loop), name_(listenAddr.toHostPort()),
      acceptor_(new Acceptor(loop, listenAddr)), started_(false),
      nextConnId_(1) {
  acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this,
                                                std::placeholders::_1,
                                                std::placeholders::_2));
}

TcpServer::~TcpServer() {}

/// 将 socket 的 listen 通过 runInLoop 注册到 EventLoop 中去
void TcpServer::start() {
  if (!started_) {
    started_ = true;
  }
  if (!acceptor_->listenning()) {
    loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
  }
}

/// @param sockfd conn_fd
/// @param peerAddr client 地址封装
/// -> 新连接到达 -> Acceptor 回调 newConnection(), 创建 TcpConnection 对象 conn
/// -> 加入 Connection map、设置 callback
/// -> 调用 TcpConnection::connectEstablished(), 在其中回调用户的 Callback
/// 回调函数都是用户在 TcpServer 中提前设置的，然后被传输到 Connection
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr) {
  loop_->assertInLoopThread();
  char buf[32];
  snprintf(buf, sizeof buf, "#%d", nextConnId_);
  ++nextConnId_;
  std::string connName = name_ + buf;

  BOOST_LOG_TRIVIAL(info) << "TcpServer::newConnection [" << name_
                          << "] - new connection [" << connName << "] from "
                          << peerAddr.toHostPort();
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  TcpConnectionPtr conn(
      new TcpConnection(loop_, connName, sockfd, localAddr, peerAddr));
  connections_[connName] = conn;
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));
  conn->connecEstablished();
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn) {
  loop_->assertInLoopThread();
  BOOST_LOG_TRIVIAL(info) << "TcpServer::removeConnection [" << name_
                          << "] - connection " << conn->name();
  size_t n = connections_.erase(conn->name());
  assert(n == 1);
  loop_->queueInLoop(std::bind(&TcpConnection::connectDestoryed, conn));
}