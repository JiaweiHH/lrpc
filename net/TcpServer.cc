#include "TcpServer.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "SocketsOps.h"
#include "Logging.h"
#include <cstdio>

using namespace lrpc::net;

TcpServer::TcpServer(EventLoop *loop, const InetAddress &listenAddr)
    : loop_(loop), name_(listenAddr.toHostPort()),
      acceptor_(new Acceptor(loop, listenAddr)),
      threadPool_(new EventLoopThreadPool(loop)), started_(false),
      nextConnId_(1) {
  acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this,
                                                std::placeholders::_1,
                                                std::placeholders::_2));
}

TcpServer::~TcpServer() {}

void TcpServer::setThreadNum(int numThreads) {
  assert(0 <= numThreads);
  threadPool_->setThraedNum(numThreads);
}

/// 将 socket 的 listen 通过 runInLoop 注册到 EventLoop 中去
void TcpServer::start() {
  if (!started_) {
    started_ = true;
    threadPool_->start();
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

  LOG_INFO << "TcpServer::newConnection [" << name_
                          << "] - new connection [" << connName << "] from "
                          << peerAddr.toHostPort();
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  // 从 EventLoopThreadPool 获取 ioLoop，单线程是传递 server 自己的 loop
  EventLoop *ioLoop = threadPool_->getNextLoop();
  TcpConnectionPtr conn(
      new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
  connections_[connName] = conn;
  conn->setConnectionCallback(connectionCallback_);
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));
  ioLoop->runInLoop(std::bind(&TcpConnection::connecEstablished, conn));
}

/// 对比单线程的区别在于
/// 现在 removeConnection 是在 TcpConnection 自己的线程被调用的，
/// 但是所有的 connections 都是在 server 线程管理的，所以需要把它已到 server 线程执行
void TcpServer::removeConnection(const TcpConnectionPtr &conn) {
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn) {
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
                          << "] - connection " << conn->name();
  size_t n = connections_.erase(conn->name());
  assert(n == 1);
  EventLoop *ioLoop = conn->getLoop();
  // 这里再把最后的取消 channel 和 pollfd 移回到 ioLoop 中去执行
  ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestoryed, conn));
}