#ifndef IMITATE_MUDUO_TCPCONNECTION_H
#define IMITATE_MUDUO_TCPCONNECTION_H

#include "Callback.h"
#include "InetAddress.h"
#include <memory>
#include <string>

namespace imitate_muduo {

class Channel;
class EventLoop;
class Socket;

/// TcpConnection 里面保存 client 的 fd
/// 本地网络地址和对方的网络地址
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
private:
  enum class StateE {
    kConnecting,
    kConnected,
    kDisConnected,
  };

  void setState(StateE s) { state_ = s; }

  void handleRead();
  void handleWrite();
  void handleClose();
  void handleError();

  EventLoop *loop_;
  std::string name_;
  StateE state_;
  std::unique_ptr<Socket> socket_;
  std::unique_ptr<Channel> channel_;
  InetAddress localAddr_;
  InetAddress peerAddr_;
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  CloseCallback closeCallback_;

public:
  TcpConnection(const TcpConnection &) = delete;
  TcpConnection &operator=(const TcpConnection &) = delete;
  TcpConnection(EventLoop *loop, const std::string &name, int sockfd,
                const InetAddress &localAddr, const InetAddress &peerAddr);
  ~TcpConnection();

  EventLoop *getLoop() const { return loop_; }
  const std::string &name() const { return name_; }
  const InetAddress &localAddress() { return localAddr_; }
  const InetAddress &peerAddress() { return peerAddr_; }
  bool connected() const { return state_ == StateE::kConnected; }

  void setConnectionCallback(const ConnectionCallback &cb) {
    connectionCallback_ = cb;
  }
  void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
  void setCloseCallback(const CloseCallback &cb) { closeCallback_ = cb; }

  void connecEstablished(); // 连接建立
  void connectDestoryed();  // 连接断开

  std::string stateEnumToStr(StateE state);
};

} // namespace imitate_muduo

#endif