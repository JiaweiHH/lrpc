#ifndef IMITATE_MUDUO_TCPSERVER_H
#define IMITATE_MUDUO_TCPSERVER_H

#include "Callback.h"
#include "TcpConnection.h"
#include <map>
#include <memory>

namespace imitate_muduo {

class Acceptor;
class EventLoop;

/// 管理 accept(2) 获得的 TcpConnection
class TcpServer {
private:
  void newConnection(int sockfd, const InetAddress &);
  void removeConnection(const TcpConnectionPtr &);

  EventLoop *loop_;
  const std::string name_;
  std::unique_ptr<Acceptor> acceptor_;
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  bool started_;
  int nextConnId_;
  std::map<std::string, TcpConnectionPtr> connections_;

public:
  TcpServer(const TcpServer &) = delete;
  TcpServer &operator=(const TcpServer &) = delete;
  TcpServer(EventLoop *loop, const InetAddress &listenAddr);
  ~TcpServer();

  void start();
  void setConnectionCallback(const ConnectionCallback &cb) {
    connectionCallback_ = cb;
  }
  void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
};

} // namespace imitate_muduo

#endif