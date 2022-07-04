#include "RpcServer.h"
#include "ClientStub.h"
#include "EventLoopThreadPool.h"
#include "Logging.h"
#include "RpcClient.h"
#include "RpcService.h"
#include "name_service_protocol/RedisClientContext.h"

namespace lrpc {

RpcServer *RpcServer::s_rpcServer = nullptr;

RpcServer::RpcServer()
    : threadPool_(new EventLoopThreadPool(&loop_)), threadNum_(0),
      nextConnId_(1) {
  assert(!s_rpcServer);
  s_rpcServer = this;
}

RpcServer::~RpcServer() {}

RpcServer &RpcServer::instance() { return *RpcServer::s_rpcServer; }

void RpcServer::setThreadNum(size_t n) {
  assert(n >= 0);
  threadPool_->setThreadNum(n);
}

size_t RpcServer::getThreadNum() const { return threadNum_; }

EventLoop *RpcServer::baseLoop() { return &loop_; }

EventLoop *RpcServer::next() { return threadPool_->getNextLoop(); }

/// @brief 添加 service
bool RpcServer::addService(Service *service) {
  auto googleService = service->getService();
  const auto &name = googleService->GetDescriptor()->full_name();
  LOG_INFO << "addService " << googleService->GetDescriptor()->name().data();
  std::unique_ptr<Service> svr(service);
  if (services_.insert({name, std::move(svr)}).second) {
    service->onRegister();
    return true;
  }
  return false;
}
bool RpcServer::addService(std::unique_ptr<Service> &&service) {
  auto srv = service.get();
  auto googleService = service->getService();
  const auto name = googleService->GetDescriptor()->full_name();
  if (services_.insert({name, std::move(service)}).second) {
    srv->onRegister(); // 初始化 Service
    return true;
  }
  return false;
}

/// @brief 设置 name server
void RpcServer::setNameServer(const std::string &url) {
  assert(!nameServiceStub_);
  nameServiceStub_ = new ClientStub(new NameService_Stub(nullptr));
  nameServiceStub_->setUrlLists(url);
  if (onCreateNameServiceChannel_)
    nameServiceStub_->setOnCreateChannel(onCreateNameServiceChannel_);
  else
    nameServiceStub_->setOnCreateChannel(onCreateRedisChannel);
  LOG_DEBUG << "setNameServer " << nameServiceStub_->fullName();
  nameServiceStub_->onRegister(RPC_SERVER.getThreadNum()); // 初始化 name server
}

void RpcServer::start() {
  // 启动所有服务
  for (const auto &srv : services_) {
    if (srv.second->start()) {
      LOG_INFO << "start success service " << srv.first.data();
    } else {
      LOG_ERROR << "start failed service " << srv.first.data();
      return;
    }
  }
  if (nameServiceStub_)
    LOG_DEBUG << "use nameservice " << nameServiceStub_->fullName();
  if (services_.empty()) {
    LOG_WARN << "warning: no available service";
  } else if (nameServiceStub_) {
    double period = 3.0;
    baseLoop()->runEvery(period, [this]() {
      // 设置心跳信息
      if (keepaliveInfo_.size() == 0) {
        for (const auto &srv : services_) {
          KeepaliveInfo info;
          info.set_servicename(srv.first);
          info.mutable_endpoint()->CopyFrom(srv.second->getEndpoint());
          keepaliveInfo_.push_back(info);
        }
      }
      LOG_DEBUG << "call Keepalive";
      // 发送心跳信息
      for (const auto &e : keepaliveInfo_)
        call<Status>("lrpc.NameService", "Keepalive", e);
    });
  }
  threadPool_->start(); // 启动线程池
  baseLoop()->loop();   // 启动 baseLoop
}

/// @brief 根据 name 获取 nameService
ClientStub *RpcServer::getNameService(const std::string &name) const {
  if (nameServiceStub_ &&
      nameServiceStub_->getService()->GetDescriptor()->full_name() == name)
    return nameServiceStub_;
  return nullptr;
}
ClientStub *RpcServer::getClientStub(const std::string &name) const {
  return getNameService(name);
}

} // namespace lrpc