#ifndef LRPC_RPCSERVER_H
#define LRPC_RPCSERVER_H

#include "Callback.h"
#include "EventLoop.h"
#include "lrpc.pb.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lrpc {

namespace net {
class EventLoop;
class EventLoopThreadPool;
} // namespace net

class ClientChannel;
class Service;
class ClientStub;

using google::protobuf::Message;
using namespace net;

class RpcServer {
public:
  RpcServer();
  static RpcServer &instance();
  ~RpcServer();

  EventLoop *baseLoop();
  EventLoop *next();
  void setOnInit(std::function<void()> init) { onInit_ = std::move(init); }
  void setOnExit(std::function<void()> onExit) { onExit_ = std::move(onExit); }
  void setNameServer(const std::string &url);
  void setOnCreateNameServerChannel(std::function<void(ClientChannel *)> cb) {
    onCreateNameServiceChannel_ = std::move(cb);
  }
  // void setHealthService

  bool addService(Service *service);
  bool addService(std::unique_ptr<Service> &&service);
  bool addClientStub(ClientStub *service);
  bool addClientStub(std::unique_ptr<ClientStub> &&service);
  ClientStub *getClientStub(const std::string &name) const;

  void setThreadNum(size_t n);
  size_t getThreadNum() const;

  // 启动 rpc client，在这之前需要执行 addServiceStub
  void start();
  void shutdown();

  int fetchAddNextConnId() { return nextConnId_++; }

private:
  ClientStub *getNameService(const std::string &name) const;

  std::unordered_map<std::string, std::unique_ptr<Service>> services_;
  ClientStub *nameServiceStub_{nullptr};
  std::function<void(ClientChannel *)> onCreateNameServiceChannel_;
  std::function<void()> onInit_, onExit_;
  std::vector<KeepaliveInfo> keepaliveInfo_;
  static RpcServer *s_rpcServer;

  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  ConnectionFailCallback failCallback_;
  std::unique_ptr<EventLoopThreadPool> threadPool_;
  EventLoop loop_; // base loop 只负责 connect，其他工作由别的 eventloop 执行
  size_t threadNum_{0};
  int nextConnId_; // 只会在 base loop 中 write，不会存在线程安全问题
};

#define RPC_SERVER ::lrpc::RpcServer::instance()

} // namespace lrpc

#endif