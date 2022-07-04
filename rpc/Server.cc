#include "Server.h"
#include "Connector.h"
#include "EventLoopThreadPool.h"
#include "InetAddress.h"
#include "Logging.h"
#include "SocketsOps.h"
#include "TcpConnection.h"
#include "name_service_protocol/RedisClientContext.h"
#include <assert.h>

namespace lrpc {

RpcServer *RpcServer::s_rpcClient = nullptr;

RpcServer::RpcServer()
    : threadPool_(new EventLoopThreadPool(&loop_)), threadNum_(1),
      nextConnId_(1) {
  assert(!s_rpcClient);
  s_rpcClient = this;
}

RpcServer::~RpcServer() {}

RpcServer &RpcServer::instance() { return *RpcServer::s_rpcClient; }

void RpcServer::setThreadNum(size_t n) {
  assert(n >= 0);
  threadNum_ = n + 1;
  threadPool_->setThreadNum(n);
}

size_t RpcServer::getThreadNum() const { return threadNum_; }

EventLoop *RpcServer::baseLoop() { return &loop_; }

EventLoop *RpcServer::next() { return threadPool_->getNextLoop(); }

/// @brief 添加需要 request 的 client stub
bool RpcServer::addClientStub(ClientStub *service) {
  auto googleService = service->getService();
  const std::string &name = googleService->GetDescriptor()->full_name();
  LOG_INFO << "addClientStub " << googleService->GetDescriptor()->name().data();
  std::unique_ptr<ClientStub> stub(service);
  if (stubs_.insert({name, std::move(stub)}).second) {
    service->onRegister();
    return true;
  }
  return false;
}
bool RpcServer::addClientStub(std::unique_ptr<ClientStub> &&service) {
  auto srv = service.get();
  auto googleService = service->getService();
  const std::string &name = googleService->GetDescriptor()->full_name();
  if (stubs_.insert({name, std::move(service)}).second) {
    srv->onRegister();
    return true;
  }
  return false;
}

/// @brief 根据 service name 获取 ClientStub
ClientStub *RpcServer::getClientStub(const std::string &name) const {
  auto it = stubs_.find(name);
  return it == stubs_.end() ? nullptr : it->second.get();
}

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
  this->addClientStub(nameServiceStub_);
}

void RpcServer::setOnCreateNameServerChannel(
    std::function<void(ClientChannel *)> cb) {
  onCreateNameServiceChannel_ = std::move(cb);
}

void RpcServer::setOnInit(std::function<void()> init) {
  onInit_ = std::move(init);
}

void RpcServer::setOnExit(std::function<void()> onexit) {
  onExit_ = std::move(onexit);
}

/// @brief 连接服务器，connect 操作在 base loop 中执行，
/// 连接成功之后将 TcpConnection 移至 ioLoop 管理
void RpcServer::connect(const InetAddress &addr, ConnectionCallback success,
                        ConnectionFailCallback fail,
                        std::chrono::milliseconds timeout, EventLoop *ioLoop) {
  // 获取 base loop，connect 操作在 base loop 中执行
  auto loop = baseLoop();
  // TODO 暂时不考虑连接超时的问题
  auto newConnectionCallback =
      std::bind(&RpcServer::newConnection, this, std::placeholders::_1,
                std::move(success), ioLoop, std::placeholders::_2);
  loop->Execute([loop, addr, newConnectionCallback] {
    // 创建连接器，设置回调函数，然后开始连接
    std::shared_ptr<Connector> connector(new Connector(loop, addr));
    connector->setNewConnectionCallback(
        std::bind(newConnectionCallback, std::placeholders::_1, connector));
    connector->start();
  });
}

/// When socket connection is completed, this function will be called.
/// It will create TcpConnection object and set its callback.
/// These callbacks come from users, except CloseCallback

/// @brief 逻辑与 TcpClient 中的逻辑类似，只不过将 conn 的回调函数移至
/// ClientStub 传递过来的 onNewConnection 执行
void RpcServer::newConnection(int sockfd, ConnectionCallback onNewConnection,
                              EventLoop *ioLoop,
                              std::shared_ptr<net::Connector>) {
  baseLoop()->assertInLoopThread();
  InetAddress peerAddr(sockets::getPeerAddr(sockfd));
  char buf[32];
  sprintf(buf, ":%s#%d", peerAddr.toHostPort().c_str(), nextConnId_++);
  string connName = buf;
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  TcpConnectionPtr conn(
      new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
  // 设置 conn 的回调函数，由 ClientStub 传递进来
  onNewConnection(conn);
  // 成功建立连接回调和状态设置
  connections_.insert({connName, conn});
  ioLoop->runInLoop(std::bind(&TcpConnection::connecEstablished, conn));
}

void RpcServer::removeConnectionInLoop(const TcpConnectionPtr &conn) {
  baseLoop()->assertInLoopThread();
  size_t n = connections_.erase(conn->name());
  assert(n == 1);
  // ioLoop 处理
  LOG_INFO << "RpcServer::removeConnectionInLoop " << conn->name();
  conn->getLoop()->queueInLoop(std::bind(&TcpConnection::connectDestoryed, conn));
}

/// @brief 启动 EventLoop
/// TODO EventLoop stop
void RpcServer::startClient() {
  if (nameServiceStub_)
    LOG_DEBUG << "use nameservice " << nameServiceStub_->fullName();
  if (onInit_) // 初始化函数
    onInit_();
  threadPool_->start(); // 启动线程池
  baseLoop()->loop();   // 启动 baseLoop
  if (onExit_)          // 结尾函数
    onExit_();
}

void RpcServer::startServer() {
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

} // namespace lrpc