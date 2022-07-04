#include "RpcServiceRegistry.h"
#include "Logging.h"
#include "SocketsOps.h"
#include "Coder.h"
#include "lrpc.pb.h"

namespace lrpc {

using google::protobuf::Message;

RpcServiceRegistry::RpcServiceRegistry(const InetAddress &listenAddr)
    : loop_(new lrpc::net::EventLoop), name_(listenAddr.toHostPort()),
      acceptor_(new Acceptor(loop_.get(), listenAddr)),
      threadPool_(new EventLoopThreadPool(loop_.get())), started_(false),
      nextConnId_(1) {
  acceptor_->setNewConnectionCallback(
      std::bind(&RpcServiceRegistry::newConnection, this, std::placeholders::_1,
                std::placeholders::_2));
}

void RpcServiceRegistry::setThreadNum(int numThreads) {
  assert(numThreads >= 0);
  threadPool_->setThreadNum(numThreads);
}

void RpcServiceRegistry::newConnection(int sockfd,
                                       const InetAddress &peerAddr) {
  loop_->assertInLoopThread();
  char buf[32];
  snprintf(buf, sizeof buf, "#%d", nextConnId_);
  ++nextConnId_;
  std::string connName = name_ + buf;
  LOG_INFO << "RpcServiceRegistry::newConnection [" << name_
           << "] - new connection [" << connName << "] from "
           << peerAddr.toHostPort();
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  EventLoop *ioLoop = threadPool_->getNextLoop();
  TcpConnectionPtr conn(
      new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
  connections_[connName] = conn;
  conn->setMessageCallback(
      std::bind(&RpcServiceRegistry::handleMessage, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));
  conn->setCloseCallback(std::bind(&RpcServiceRegistry::removeConnection, this,
                                   std::placeholders::_1));
  ioLoop->runInLoop(std::bind(&TcpConnection::connecEstablished, conn));
}

void RpcServiceRegistry::handleNewConnection(const TcpConnectionPtr &conn) {
  // 为 Connection 创建一个 Channel，以便处理完数据的时候可以找到对应的连接，然后发送数据
}

void RpcServiceRegistry::handleMessage(const TcpConnectionPtr &conn,
                                       Buffer *buf, Timestamp recieveTime) {
  // 1. 解码消息
  auto channel = conn->getContext();
  
}

void RpcServiceRegistry::removeConnection(const TcpConnectionPtr &conn) {
  loop_->runInLoop(
      std::bind(&RpcServiceRegistry::removeConnectionInLoop, this, conn));
}

void RpcServiceRegistry::removeConnectionInLoop(const TcpConnectionPtr &conn) {
  loop_->assertInLoopThread();
  LOG_INFO << "RpcServiceRegistry::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  size_t n = connections_.erase(conn->name());
  assert(n == 1);
  EventLoop *ioLoop = conn->getLoop();
  ioLoop->queueInLoop(std::bind(&TcpConnection::connectDestoryed, conn));
}

} // namespace lrpc