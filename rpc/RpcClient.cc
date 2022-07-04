#include "RpcClient.h"
#include "Connector.h"
#include "EventLoopThreadPool.h"
#include "InetAddress.h"
#include "Logging.h"
#include "SocketsOps.h"
#include "TcpConnection.h"
#include "name_service_protocol/RedisClientContext.h"
#include <assert.h>

namespace lrpc {

RpcClient *RpcClient::s_rpcClient = nullptr;

RpcClient::RpcClient()
    : threadPool_(new EventLoopThreadPool(&loop_)), threadNum_(0),
      nextConnId_(1) {
  assert(!s_rpcClient);
  s_rpcClient = this;
}

RpcClient::~RpcClient() {}

RpcClient &RpcClient::instance() { return *RpcClient::s_rpcClient; }

void RpcClient::setThreadNum(size_t n) {
  assert(n >= 0);
  threadPool_->setThreadNum(n);
}

size_t RpcClient::getThreadNum() const { return threadNum_; }

EventLoop *RpcClient::baseLoop() { return &loop_; }

EventLoop *RpcClient::next() { return threadPool_->getNextLoop(); }

/// @brief 添加需要 request 的 client stub
bool RpcClient::addClientStub(ClientStub *service) {
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
bool RpcClient::addClientStub(std::unique_ptr<ClientStub> &&service) {
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
ClientStub *RpcClient::getClientStub(const std::string &name) const {
  auto it = stubs_.find(name);
  return it == stubs_.end() ? nullptr : it->second.get();
}

/// @brief 设置 name server
void RpcClient::setNameServer(const std::string &url) {
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

void RpcClient::setOnCreateNameServerChannel(
    std::function<void(ClientChannel *)> cb) {
  onCreateNameServiceChannel_ = std::move(cb);
}

void RpcClient::setOnInit(std::function<void()> init) {
  onInit_ = std::move(init);
}

void RpcClient::setOnExit(std::function<void()> onexit) {
  onExit_ = std::move(onexit);
}

/// @brief 连接服务器，connect 操作在 base loop 中执行，
/// 连接成功之后将 TcpConnection 移至 ioLoop 管理
void RpcClient::connect(const InetAddress &addr, ConnectionCallback success,
                        ConnectionFailCallback fail,
                        std::chrono::milliseconds timeout, EventLoop *ioLoop) {
  // 获取 base loop，connect 操作在 base loop 中执行
  auto loop = baseLoop();
  // TODO 暂时不考虑连接超时的问题
  auto newConnectionCallback =
      std::bind(&RpcClient::newConnection, this, std::placeholders::_1,
                std::move(success), ioLoop);
  loop->Execute([loop, addr, newConnectionCallback] {
    // 创建连接器，设置回调函数，然后开始连接
    std::shared_ptr<Connector> connector(new Connector(loop, addr));
    connector->setNewConnectionCallback(newConnectionCallback);
    connector->start();
  });
}

/// When socket connection is completed, this function will be called.
/// It will create TcpConnection object and set its callback.
/// These callbacks come from users, except CloseCallback

/// @brief 逻辑与 TcpClient 中的逻辑类似，只不过将 conn 的回调函数移至
/// ClientStub 传递过来的 onNewConnection 执行
void RpcClient::newConnection(int sockfd, ConnectionCallback onNewConnection,
                              EventLoop *ioLoop) {
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
  ioLoop->runInLoop(std::bind(&TcpConnection::connecEstablished, conn));
}

/// @brief 启动 EventLoop
/// TODO EventLoop stop
void RpcClient::start() {
  if (nameServiceStub_)
    LOG_DEBUG << "use nameservice " << nameServiceStub_->fullName();
  if (onInit_) // 初始化函数
    onInit_();
  threadPool_->start(); // 启动线程池
  baseLoop()->loop();   // 启动 baseLoop
  if (onExit_)          // 结尾函数
    onExit_();
}

} // namespace lrpc