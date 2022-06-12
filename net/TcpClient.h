#ifndef LRPC_NET_TCPCLIENT_H
#define LRPC_NET_TCPCLIENT_H

#include "TcpConnection.h"
#include <mutex>

namespace lrpc {

namespace net {

class Connector;
using ConnectorPtr = std::shared_ptr<Connector>;

class TcpClient {
public:
  TcpClient(const TcpClient &) = delete;
  TcpClient &operator=(const TcpClient &) = delete;

  TcpClient(EventLoop *loop, const InetAddress &serverAddr);
  ~TcpClient();

  void connect();
  void disconnect();
  void stop();

  TcpConnectionPtr connection() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return connection_;
  }
  bool retry() const;
  void enableRetry() { retry_ = true; }

  // 设置回调函数
  void setConnectionCallback(const ConnectionCallback &cb) {
    connectionCallback_ = cb;
  }
  void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
  void setWriteCompleteCallback(const WriteCompleteCallback &cb) {
    writeCompleteCallback_ = cb;
  }

private:
  void newConnection(int sockfd);
  void removeConnection(const TcpConnectionPtr &conn);

  EventLoop *loop_;
  ConnectorPtr connector_;
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  TcpConnectionPtr connection_;
  bool retry_;
  bool connect_;
  int nextConnId_;
  mutable std::mutex mutex_;
};

} // namespace net

} // namespace lrpc

#endif