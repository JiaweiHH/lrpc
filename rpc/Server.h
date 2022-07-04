#ifndef LRPC_RPCCLIENT_H
#define LRPC_RPCCLIENT_H

#include "ClientStub.h"
#include "RpcChannel.h"
#include "RpcException.h"
#include "future.h"
#include "lrpc.pb.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lrpc {

namespace net {
class EventLoop;
class EventLoopThreadPool;
class InetAddress;
class Connector;
} // namespace net

class ClientChannel;
class Endpoint;
class Service;

using google::protobuf::Message;
using namespace net;

/// 1. 提供 connect 的实现
/// 2. 必须是多线程，baseLoop 处理 connect，other loop 处理数据
/// 3. 适配 ClientStub _onNewConnection 的逻辑：在 RpcServer 中先创建好
/// TcpConnection，然后调用 _onNewConnection cb，然后 conn->connecEstablished();
/// 继续执行 _onConnect
class RpcServer {
  friend class Service;

public:
  RpcServer();
  static RpcServer &instance();
  ~RpcServer();

  EventLoop *baseLoop();
  EventLoop *next();
  void connect(const InetAddress &addr, ConnectionCallback success,
               ConnectionFailCallback fail, std::chrono::milliseconds timeout,
               EventLoop *dstLoop);
  void setOnInit(std::function<void()> init);
  void setOnExit(std::function<void()> onExit);
  void setNameServer(const std::string &url);
  void setOnCreateNameServerChannel(std::function<void(ClientChannel *)> cb);

  bool addClientStub(ClientStub *service);
  bool addClientStub(std::unique_ptr<ClientStub> &&service);
  ClientStub *getClientStub(const std::string &name) const;

  bool addService(Service *service);
  bool addService(std::unique_ptr<Service> &&service);

  void setThreadNum(size_t n);
  size_t getThreadNum() const;

  // 启动 rpc client，在这之前需要执行 addClientStub
  void startClient();
  void startServer();
  // void shutdown();

  void newConnection(int sockfd, ConnectionCallback success, EventLoop *loop,
                     std::shared_ptr<Connector>);
  void removeConnectionInLoop(const TcpConnectionPtr &conn);
  // int fetchAddNextConnId() { return nextConnId_++; }  // for service

private:
  std::unordered_map<std::string, std::unique_ptr<Service>> services_;
  std::unordered_map<std::string, std::unique_ptr<ClientStub>> stubs_;
  ClientStub *nameServiceStub_{nullptr};
  std::function<void(ClientChannel *)> onCreateNameServiceChannel_;
  std::vector<KeepaliveInfo> keepaliveInfo_;

  std::function<void()> onInit_;
  std::function<void()> onExit_;

  /// @brief Client 多线程，主线程负责连接各个服务器，连接之后的数据由子线程处理
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  ConnectionFailCallback failCallback_;
  // std::unordered_map<std::string, TcpConnectionPtr> connections_;
  std::unique_ptr<EventLoopThreadPool> threadPool_;
  EventLoop loop_; // base loop 只负责 connect，其他工作由别的 eventloop 执行
  size_t threadNum_{0};

  int nextConnId_; // 只会在 base loop 中 write，不会存在线程安全问题
  std::unordered_map<std::string, TcpConnectionPtr> connections_;

  static RpcServer *s_rpcClient;
};

#define RPC_SERVER ::lrpc::RpcServer::instance()

namespace {

template <typename R>
Future<Result<R>> _innerCall(ClientStub *stub, const std::string &method,
                             const std::shared_ptr<Message> &req,
                             const Endpoint &ep = Endpoint::default_instance());

}

/**
 * @brief rpc call 的接口
 *
 * @tparam R method 的返回值
 * @param service rpc 服务的名称
 * @param method 请求的函数
 * @param req rpc request
 * @param ep server address
 * @return Future<Result<R>> TODO 为啥需要使用 Result 包装，将 Result 去掉试试看
 */
template <typename R>
Future<Result<R>> call(const std::string &service, const std::string &method,
                       const std::shared_ptr<Message> &req,
                       const Endpoint &ep = Endpoint::default_instance()) {
  // 找到 clientStub
  auto stub = RPC_SERVER.getClientStub(service);
  if (!stub)
    return makeExceptionFuture<Result<R>>(
        Exception(ErrorCode::NoSuchService, service));
  return _innerCall<R>(stub, method, req, ep);
}
/// @brief rpc call 接口重载，区别在于这里的 req 是原始 request 的引用
/// 因此需要制作一份拷贝
/// TODO 为啥需要拷贝一份？
template <typename R>
Future<Result<R>> call(const std::string &service, const std::string &method,
                       const Message &req,
                       const Endpoint &ep = Endpoint::default_instance()) {
  auto stub = RPC_SERVER.getClientStub(service);
  if (!stub)
    return makeExceptionFuture<Result<R>>(
        Exception(ErrorCode::NoSuchService, service));
  // 拷贝一份 request
  std::shared_ptr<Message> reqCopy(req.New());
  reqCopy->CopyFrom(req);
  return _innerCall<R>(stub, method, reqCopy, ep);
}

namespace {

/**
 * @brief 建立 rpc connection，发送 request，设置 response 回调函数
 * 返回 Future<Result<R>>，R 是 method 的返回值
 *
 * @tparam R
 * @param stub
 * @param method
 * @param req
 * @param ep
 * @return Future<Result<R>>
 */
template <typename R>
Future<Result<R>> _innerCall(ClientStub *stub, const std::string &method,
                             const std::shared_ptr<Message> &req,
                             const Endpoint &ep) {
  // 等待连接 ep
  auto channelFuture = stub->getChannel(ep);
  // ep 连接成功之后获取连接创建的 Channel，执行 invoke 发送 request 请求
  // 返回一个 Future<R>
  return channelFuture.then([method, req](Result<ClientChannel *> &&chan) {
    try {
      ClientChannel *channel = chan.getValue();
      return channel->invoke<R>(method, req);
    } catch (...) {
      return makeExceptionFuture<Result<R>>(std::current_exception());
    }
  });
}

} // namespace

} // namespace lrpc

#endif