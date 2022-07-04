/**
 * @file RpcService.h
 * @author your name (you@domain.com)
 * @brief 每个 Service 会提供一类方法供客户端使用
 * @version 0.1
 * @date 2022-06-16
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef LRPC_SERVICE_H
#define LRPC_SERVICE_H

#include "Callback.h"
#include "RpcEndpoint.h"
#include "lrpc.pb.h"
#include <functional>
#include <google/protobuf/message.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace google {

namespace protobuf {

class Service;

}

} // namespace google

namespace lrpc {

namespace net {

class Acceptor;

}

class Request;
class ServerChannel;

using GoogleService = google::protobuf::Service;
using google::protobuf::Message;
using namespace net;

class Service {
  friend class ServerChannel;

public:
  explicit Service(GoogleService *service);
  ~Service() = default;

  GoogleService *getService() const;   // 获取服务指针
  const std::string &fullName() const; // 获取服务名称
  const Endpoint &getEndpoint() const; // 获取服务 endpoint
  // 设置服务监听 endpoint
  void setEndpoint(const Endpoint &ep);

  bool start(); // 启动 service，called by RpcServer

  void onNewConnection(int sockfd, const InetAddress &peerAddr, std::shared_ptr<Acceptor>);
  void onRegister();

  /// @brief If the third party protocol, this func tell ananas to invoke which
  /// method. The argument is a pointer to request of type Message, return value
  /// is a string, it should be a valid method name for this service!
  void setMethodSelector(std::function<std::string(const Message *)>);
  /// @brief Callback when a Rpc ServerChannel created, if you use user-defined
  /// protocol, the callback should call SetEncoder/SetDecoder for this channel.
  void setOnCreateChannel(std::function<void(ServerChannel *)>);

private:
  using ChannelMap = std::unordered_map<unsigned int, ServerChannel *>;

  static void _onMessage(const TcpConnectionPtr &, Buffer *, Timestamp);
  void _onDisconnect(const TcpConnectionPtr &conn);

  std::function<void(ServerChannel *)> onCreateChannel_;
  std::function<std::string(const Message *)> methodSelector_;
  std::unique_ptr<GoogleService> service_;
  Endpoint endpoint_;
  std::string name_;
  // 每个 service 有很多个 TcpConnection，每个 EventLoop 有它自己的 ChannelMap
  // 保存每个 loop 中每个连接的 TcpConnection -> ServerChannel 的映射
  std::vector<ChannelMap> channels_;
};

} // namespace lrpc

#endif