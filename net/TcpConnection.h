#ifndef IMITATE_MUDUO_TCPCONNECTION_H
#define IMITATE_MUDUO_TCPCONNECTION_H

#include "Buffer.h"
#include "Callback.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include <memory>
#include <string>

namespace lrpc {
namespace net {

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
    kDisConnecting,
    kDisConnected,
  };

  void setState(StateE s) { state_ = s; }

  void handleRead(Timestamp);
  void handleWrite();
  void handleClose();
  void handleError();

  void sendInLoop(const std::string &message);
  void shutdownInLoop();

  EventLoop *loop_;
  std::string name_;
  StateE state_;
  std::unique_ptr<Socket> socket_;
  std::unique_ptr<Channel> channel_;
  InetAddress localAddr_;
  InetAddress peerAddr_;
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  CloseCallback closeCallback_;
  Buffer inputBuffer_;
  Buffer outputBuffer_;

  std::shared_ptr<void> context_; // 保存 RpcChannel
  unsigned int uniqueId_;

public:
  TcpConnection(const TcpConnection &) = delete;
  TcpConnection &operator=(const TcpConnection &) = delete;
  TcpConnection(EventLoop *loop, const std::string &name, int sockfd,
                const InetAddress &localAddr, const InetAddress &peerAddr);
  ~TcpConnection();

  /// 获取成员变量
  EventLoop *getLoop() const { return loop_; }
  const std::string &name() const { return name_; }
  const InetAddress &localAddress() { return localAddr_; }
  const InetAddress &peerAddress() { return peerAddr_; }
  bool connected() const { return state_ == StateE::kConnected; }

  /// 这两个函数都是线程安全的
  bool send(const std::string &message);
  bool send(Buffer &message);
  void shutdown();
  void setTcpNoDelay(bool on); // 禁用 Nagle 算法，避免连续发包出现延迟

  /// 设置回调函数
  void setConnectionCallback(const ConnectionCallback &cb) {
    connectionCallback_ = cb;
  }
  void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
  void setCloseCallback(const CloseCallback &cb) { closeCallback_ = cb; }
  void setWriteCompleteCallback(const WriteCompleteCallback &cb) {
    writeCompleteCallback_ = cb;
  };

  void connecEstablished(); // 连接建立
  void connectDestoryed();  // 连接断开

  std::string stateEnumToStr(StateE state);

  void setContext(std::shared_ptr<void> user) { context_ = std::move(user); }
  template <typename T> std::shared_ptr<T> getContext() const;
  std::shared_ptr<void> getContext() { return context_; }

  // void setUniqueId(unsigned int id) { uniqueId_ = id; }
  // lazy 赋值，只会在 base loop 被调用
  unsigned int getUniqueId() {
    if (uniqueId_ != 0)
      return uniqueId_;
    // assert(loop_->isInLoopThread());
    uniqueId_ = loop_->getConnectionId();
    return uniqueId_;
  }
};

template <typename T>
inline std::shared_ptr<T> TcpConnection::getContext() const {
  return std::static_pointer_cast<T>(context_);
}

} // namespace net
} // namespace lrpc

#endif