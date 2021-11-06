#ifndef IMITATE_MUDUO_CONNECTOR_H
#define IMITATE_MUDUO_CONNECTOR_H

#include "InetAddress.h"
#include <functional>
#include <memory>

namespace imitate_muduo {

class Channel;
class EventLoop;

/// Connector 只负责建立 socket 连接，不负责创建 TcpConnection
class Connector {
public:
  using NewConnectionCallback = std::function<void(int sockfd)>;

private:
  enum class States {
    kDisconnected,
    kConnecting,
    kConnected,
  };

  static const int kMaxRetryDelayMs = 30 * 1000;
  static const int kInitRetryDelayMs = 500;

  void setState(States s) { state_ = s; }
  void startInLoop();
  void connect();
  void connecting(int sockfd);
  void handleWrite();
  void handleError();
  void retry(int sockfd);
  void removeAndResetChannel();
  void resetChannel();

  EventLoop *loop_;
  InetAddress serverAddr_;
  bool connect_;
  States state_;
  std::unique_ptr<Channel> channel_;
  NewConnectionCallback newConnectionCallback_;
  int retryDelayMs_;

public:
  Connector(const Connector &) = delete;
  Connector &operator=(const Connector &) = delete;
  Connector(EventLoop *loop, const InetAddress &serverAddr);
  ~Connector();

  void setNewConnectionCallback(const NewConnectionCallback &cb) {
    newConnectionCallback_ = cb;
  }

  void start();
  void restart();
  void stop();

  const InetAddress &serverAddress() const { return serverAddr_; }
};

} // namespace imitate_muduo

#endif