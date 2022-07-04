#ifndef LRPC_CLIENTSTUB_H
#define LRPC_CLIENTSTUB_H

#include "Callback.h"
#include "EventLoop.h"
#include "RpcEndpoint.h"
#include "TcpConnection.h"
#include "future.h"
#include <google/protobuf/message.h>
#include <string>

namespace google {

namespace protobuf {

class Service;

}

} // namespace google

namespace lrpc {

using google::protobuf::Message;
using GoogleService = google::protobuf::Service;
using lrpc::net::EventLoop;
using namespace net;

class ClientChannel;

/**
 * @brief RPC ClientStub
 * rpc client 会用到这个类，在启动 main loop 之前，需要创建 client stub
 * 设置需要调用的 service name, name server 或者 service 的地址列表
 *
 */
class ClientStub {
public:
  explicit ClientStub(GoogleService *service);
  ~ClientStub() = default;

  GoogleService *getService() const;
  const std::string &fullName() const;
  /// @brief 设置 service address. 如果调用该方法，rpc call
  /// 直接通过这些地址链接，不会通过 name service
  void setUrlLists(const std::string &hardCodedUrls);
  void setOnCreateChannel(std::function<void(ClientChannel *)>);
  /// @brief get channel by some load balance
  Future<ClientChannel *> getChannel();
  Future<ClientChannel *> getChannel(const Endpoint &ep);

  void onRegister();
  void onRegister(int);

private:
  using EndpointsPtr = std::shared_ptr<std::vector<Endpoint>>;
  using ChannelMap =
      std::unordered_map<Endpoint, std::shared_ptr<ClientChannel>>;
  using ChannelPromise = Promise<ClientChannel *>;

  Future<ClientChannel *> _connect(EventLoop *, const Endpoint &ep);

  // 各种回调函数
  void _onNewConnection(const TcpConnectionPtr &);
  void _onConnect(const TcpConnectionPtr &);
  void _onDisconnect(const TcpConnectionPtr &);
  void _onConnFail(EventLoop *loop, const InetAddress &peer);
  static void _onMessage(const TcpConnectionPtr &, Buffer *, Timestamp);

  // 获取 service endpoints
  Future<EndpointsPtr> _getEndpoints();
  // 调用 _selectEndpoint 和 _makeChannel
  Future<ClientChannel *> _selectChannel(EventLoop *, Result<EndpointsPtr> &&);
  // 尝试通过 endpoint 建立连接
  Future<ClientChannel *> _makeChannel(EventLoop *, const Endpoint &);
  static const Endpoint &_selectEndpoint(EndpointsPtr); // 负载均衡选择 endpoint
  // name server reponse 的 callback
  void _onNewEndpointList(Result<EndpointList> &&);

  // 每个 loop 有一个 ChannelMap，记录 Endpoint 连接建立之后对应的 ClientChannel
  std::vector<ChannelMap> channels_;
  // 每个 loop 有一个 unordered_map. 记录等待 InetAddress 连接建立的所有 promise
  std::vector<std::unordered_map<InetAddress, std::vector<ChannelPromise>>>
      pendingConns_;
  // 记录等待 nameserver 返回 endpoints 的所有 promise
  std::vector<Promise<EndpointsPtr>> pendingEndpoints_;
  // 如果 hardCodedUrls_ 不是空的，则不会去访问 name server
  std::shared_ptr<std::vector<Endpoint>> hardCodedUrls_;
  std::shared_ptr<GoogleService> service_;
  std::string name_;
  std::function<void(ClientChannel *)> onCreateChannel_;
  std::mutex endpointsMutex_;
  EndpointsPtr endpoints_;

  Timestamp refreshTime_;
};

} // namespace lrpc

#endif