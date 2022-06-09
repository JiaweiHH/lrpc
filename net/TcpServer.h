#ifndef IMITATE_MUDUO_TCPSERVER_H
#define IMITATE_MUDUO_TCPSERVER_H

#include "Callback.h"
#include "TcpConnection.h"
#include <map>
#include <memory>

namespace lrpc {
namespace net {

class Acceptor;
class EventLoop;
class EventLoopThreadPool;

/// 管理 accept(2) 获得的 TcpConnection
class TcpServer {
private:
  void newConnection(int sockfd, const InetAddress &);
  void removeConnection(const TcpConnectionPtr &);
  void removeConnectionInLoop(const TcpConnectionPtr &);

  EventLoop *loop_;
  const std::string name_;
  std::unique_ptr<Acceptor> acceptor_;
  std::unique_ptr<EventLoopThreadPool> threadPool_;
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  bool started_;
  int nextConnId_;
  std::map<std::string, TcpConnectionPtr> connections_;

public:
  TcpServer(const TcpServer &) = delete;
  TcpServer &operator=(const TcpServer &) = delete;
  TcpServer(EventLoop *loop, const InetAddress &listenAddr);
  ~TcpServer();

  /// Set the number of threads for handling input.
  ///
  /// Always accepts new connection in loop's thread.
  /// Must be called before @c start
  /// @param numThreads
  /// - 0 means all I/O in loop's thread, no thread will created.
  ///   this is the default value.
  /// - 1 means all I/O in another thread.
  /// - N means a thread pool with N threads, new connections
  ///   are assigned on a round-robin basis.
  void setThreadNum(int numThreads);

  void start();
  void setConnectionCallback(const ConnectionCallback &cb) {
    connectionCallback_ = cb;
  }
  void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
  void setWriteCompleteCallback(const WriteCompleteCallback &cb) {
    writeCompleteCallback_ = cb;
  }
};

} // namespace net
} // namespace lrpc

#endif