#include "ClientStub.h"
#include "InetAddress.h"
#include "Logging.h"
// #include "RpcClient.h"
#include "Server.h"
#include "util.h"
#include <iostream>

// TODO 修改回来：RPC_SERVER -> RPC_CLIENT

namespace lrpc {

ClientStub::ClientStub(GoogleService *service) {
  // 每个 ClientStub 对应一个 service
  service_.reset(service);
  name_ = service_->GetDescriptor()->full_name();
}

GoogleService *ClientStub::getService() const { return service_.get(); }

const std::string &ClientStub::fullName() const { return name_; }

/// @brief 设置 service 的 url lists
void ClientStub::setUrlLists(const std::string &serviceUrls) {
  assert(!hardCodedUrls_); // 只能被设置一次
  hardCodedUrls_ = std::make_shared<std::vector<Endpoint>>();
  auto urls = util::splitString(serviceUrls, ';');
  hardCodedUrls_->reserve(urls.size());
  for (const auto &url : urls) {
    Endpoint ep = createEndpoint(url);
    if (!ep.ip().empty())
      hardCodedUrls_->push_back(ep);
  }
  if (hardCodedUrls_->empty())
    LOG_WARN << "no valid url : " << serviceUrls;
}

/**
 * @brief 连接 service endpoint，返回 Future<ClientChannel *>
 * 版本 1: 没有指定 service 特定的 endpoint，则会首先去获取 ep
 * 版本 2: 指定 endpoint，如果 ep 有效则直接使用，否则调用版本 1
 *
 * @return Future<ClientChannel *>
 */
Future<ClientChannel *> ClientStub::getChannel() {
  auto loop = EventLoop::getEventLoopOfCurrentThread();
  // 选择一个 EventLoop 处理连接的后续消息等事件
  if (!loop || loop == RPC_SERVER.baseLoop())
    loop = RPC_SERVER.next();
  // 获取 ClientStub 对应的 service 的 endpoints，获取之后执行回调函数 func.
  // func 绑定 _selectChannel，用来选择其中一个 endpoints，并发起连接
  // 返回一个等待连接创建成功的 Future<ClientChannel *>
  auto func =
      std::bind(&ClientStub::_selectChannel, this, loop, std::placeholders::_1);
  if (loop->isInLoopThread())
    return _getEndpoints().then(std::move(func));
  else
    return _getEndpoints().then(loop, std::move(func));
}
Future<ClientChannel *> ClientStub::getChannel(const Endpoint &ep) {
  if (isValidEndpoint(ep)) {
    auto loop = EventLoop::getEventLoopOfCurrentThread();
    if (!loop || loop == RPC_SERVER.baseLoop())
      loop = RPC_SERVER.next();
    if (loop->isInLoopThread())
      return _makeChannel(loop, ep);
    else
      return loop->Execute(std::bind(&ClientStub::_makeChannel, this, loop, ep))
          .unwrap();
  } else {
    return getChannel();
  }
}

/// @brief 请求 nameserver 获取 service 的 endpoints
Future<ClientStub::EndpointsPtr> ClientStub::_getEndpoints() {
  if (hardCodedUrls_ && !hardCodedUrls_->empty())
    return makeReadyFuture(hardCodedUrls_);

  std::unique_lock<std::mutex> lk(endpointsMutex_);
  if (endpoints_ && !endpoints_->empty()) {
    LOG_INFO << std::this_thread::get_id() << " endpoints is not empty";
    auto now = Timestamp::now();
    // endpoints_ 是最近 60s 时间内缓存的，则继续使用
    if (timeDifference(now, refreshTime_) < 60) {
      LOG_INFO << "is in 60s";
      return makeReadyFuture(endpoints_);
    }
    else
      refreshTime_ = now;
  }
  Promise<EndpointsPtr> promise;
  auto fut = promise.getFuture();
  // 如果在 nameserver 回复之前还有新的 EventLoop 请求 ClientStub 对应的 service
  // 此时不会继续对 nameserver 发起 GetEndpoints 请求
  bool needVisit = pendingEndpoints_.empty();
  pendingEndpoints_.emplace_back(std::move(promise));
  lk.unlock();

  if (needVisit) {
    ServiceName name;
    name.set_name(fullName());
    // 获取当前线程的 EventLoop，如果当前线程不是 EventLoop 线程
    // 则获取 baseLoop
    auto scheduler = EventLoop::getEventLoopOfCurrentThread();
    if (!scheduler)
      scheduler = RPC_SERVER.baseLoop();
    assert(scheduler);
    // 向 name service 发起 GetEndpoints 请求，设置回调函数为 _onNewEndpointList
    // 设置超时时间为 2s，超时回调函数
    lrpc::call<EndpointList>("lrpc.NameService", "GetEndpoints", name)
        .then(scheduler, std::bind(&ClientStub::_onNewEndpointList, this,
                                   std::placeholders::_1))
        .onTimeout(
            std::chrono::seconds(2),
            [this]() {
              // 超时回调函数，如果 endpoints_ 有数据则使用缓存的数据
              // 否则设置 promise exception
              std::lock_guard<std::mutex> lk(endpointsMutex_);
              if (!endpoints_ || endpoints_->empty()) {
                for (auto &promise : pendingEndpoints_)
                  promise.setException(std::make_exception_ptr(
                      Exception(ErrorCode::Timeout, "GetEndpoints")));
              } else {
                for (auto &promise : pendingEndpoints_)
                  promise.setValue(endpoints_);
              }
              pendingEndpoints_.clear();
            },
            scheduler);
  }
  // fut 对应的 promise 在 pendingEndpoints_ 中
  return fut;
}

/// @brief nameserver 返回 endpoints 之后的回调函数，唤醒等待的 promise
void ClientStub::_onNewEndpointList(Result<EndpointList> &&endpoints) {
  try {
    // 从 nameserver 返回的 endpoints 中提取 endpoint 到 ClientStub
    EndpointList eps = std::move(endpoints);
    LOG_DEBUG << "From nameserver, GetEndpoints got: " << eps.DebugString() << ", " << std::this_thread::get_id();
    auto newEndpoints = std::make_shared<std::vector<Endpoint>>();
    for (int i = 0; i < eps.endpoints_size(); ++i) {
      const auto &e = eps.endpoints(i);
      newEndpoints->push_back(e);
    }
    {
      // 更新 ClientStub 的 endpoints_
      LOG_INFO << "----------- update endpoints -----------, " << eps.endpoints_size();
      std::lock_guard<std::mutex> lk(endpointsMutex_);
      this->endpoints_ = newEndpoints;
    }
    // 设置正在等待从 nameserver 获取 endpoints 的 promise.
    // 这里不加锁会不会出现冲突，因为前面已经加锁设置了 this->endpoints_.
    // 在设置了 endpoints_ 之后，后续一段时间内使用 ClientStub service 都不会对
    // nameserver 发起请求
    for (auto &promise : pendingEndpoints_)
      promise.setValue(newEndpoints);
    pendingEndpoints_.clear();
  } catch (const std::exception &e) {
    LOG_ERROR << "GetEndpoints exception: " << e.what();
    // 这里需要加锁，否则会出现冲突
    std::lock_guard<std::mutex> lk(endpointsMutex_);
    // use cached endpoints
    auto endpoints = this->endpoints_;
    for (auto &promise : pendingEndpoints_)
      promise.setValue(endpoints);
    pendingEndpoints_.clear();
  }
}

/// @brief 根据负载均衡策略选择一个 Endpoint，并发起连接
Future<ClientChannel *> ClientStub::_selectChannel(EventLoop *loop,
                                                   Result<EndpointsPtr> &&eps) {
  assert(loop->isInLoopThread());
  try {
    return _makeChannel(loop, _selectEndpoint(eps));
  } catch (...) {
    return makeExceptionFuture<ClientChannel *>(std::current_exception());
  }
}

/// @brief 获取 service 对应的 channel，返回 Future<ClientChannel *>，
/// 等待连接建立成功，ClientChannel 被创建好
Future<ClientChannel *> ClientStub::_makeChannel(EventLoop *loop,
                                                 const Endpoint &ep) {
  LOG_INFO << "make channel";
  assert(loop->isInLoopThread());
  // service 没有 endpoint，返回一个异常 future
  if (!isValidEndpoint(ep))
    return makeExceptionFuture<ClientChannel *>(
        Exception(ErrorCode::NoAvailableEndpoint, fullName()));
  // service 存在有效的 endpoint: 说明之前客户端已经对这个服务发起过连接了，
  // 并且现在连接已经被创建好了，此时直接返回 ready future，设置之前创建的
  // ClientChannel. 否则的话就尝试去连接 service
  const auto &channels = channels_[loop->getId()];
  auto it = channels.find(ep);
  if (it != channels.end()) {
    return makeReadyFuture(it->second.get());
  }
  LOG_INFO << std::this_thread::get_id() << ", need connect";
  return _connect(loop, ep);
}

/// @brief 默认负载均衡策略 round-robin
const Endpoint &ClientStub::_selectEndpoint(EndpointsPtr eps) {
  if (!eps || eps->empty())
    return Endpoint::default_instance();
  // 临时的负载均衡策略，round-robin
  static int current = 0;
  const int lucky = current++ % static_cast<int>(eps->size());
  return (*eps)[lucky];
}

/**
 * @brief 连接 service 的 endpoint，返回一个 Future<ClientChannel *>
 *
 *
 * @param loop
 * @param ep
 * @return Future<ClientChannel *>
 */
Future<ClientChannel *> ClientStub::_connect(EventLoop *loop,
                                             const Endpoint &ep) {
  assert(loop->isInLoopThread());
  ChannelPromise promise;
  auto fut = promise.getFuture();
  // 获取 InetAddress
  const InetAddress addr = getAddrFromEndpoint(ep);
  // 获取 loop 对应的 InetAddr -> vector<ChannelPromise> 映射
  auto &pendingConns = pendingConns_[loop->getId()];
  // 如果没有在 pendingConns 中找到 addr，说明还没有建立连接，需要创建连接.
  // 否则的话说明正在建立连接，则将 promise 放到 loop 对应的 promise 队列中，
  // 之后就直接返回. 等到连接建立的时候会唤醒 loop 上所有等待 addr 的 promise
  bool needConnect = (pendingConns.find(addr) == pendingConns.end());
  pendingConns[addr].emplace_back(std::move(promise)); // 插入 promise
  if (needConnect) {
    // 连接服务器，并设置连接成功和连接失败的回调函数
    RPC_SERVER.connect(
        addr,
        std::bind(&ClientStub::_onNewConnection, this, std::placeholders::_1),
        std::bind(&ClientStub::_onConnFail, this, std::placeholders::_1,
                  std::placeholders::_2),
        std::chrono::milliseconds(3000), loop);
  }
  return fut;
}

/// @brief 连接建立成功回调函数，负责设置连接的各种回调函数
/// 相当于 TcpClient::newConnection 里面的逻辑都放到这里
/// TODO 在 RpcClient 中先创建好 TcpConnection，然后调用 _onNewConnection cb
/// 然后 conn->connecEstablished(); 继续执行 _onConnect (大体可以这样设计)
void ClientStub::_onNewConnection(const TcpConnectionPtr &conn) {
  // assert(conn->getLoop()->isInLoopThread());
  RPC_SERVER.baseLoop()->assertInLoopThread();
  auto _ = conn;
  // 创建新的 ClientChannel，设置到 TcpConnection
  auto channel = std::make_shared<ClientChannel>(std::move(_), this);
  conn->setContext(channel);
  {
    // 创建 ep -> ClientChannel 映射，表示已经建立好连接了
    Endpoint ep;
    std::string addr = conn->peerAddress().toHostPort();
    int pos = addr.find(':');
    ep.set_ip(addr.substr(0, pos));
    ep.set_port(static_cast<uint16_t>(atoi(addr.substr(pos + 1).c_str())));
    auto &channelMap = channels_[conn->getLoop()->getId()];
    bool succ = channelMap.insert({ep, channel}).second;
    assert(succ);
  }
  if (onCreateChannel_) // 回调函数
    onCreateChannel_(channel.get());
  conn->setConnectionCallback( // 连接完全初始化好之后回调
      std::bind(&ClientStub::_onConnect, this, std::placeholders::_1));
  conn->setCloseCallback( // 连接关闭回调
      std::bind(&ClientStub::_onDisconnect, this, std::placeholders::_1));
  conn->setMessageCallback( // 连接有消息到达回调
      std::bind(&ClientStub::_onMessage, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));
}

void ClientStub::_onConnect(const TcpConnectionPtr &conn) {
  // see conn->connecEstablished()
  assert(conn->getLoop()->isInLoopThread());
  // 获取 loop 所属的 unordered_map
  auto &pendingConns = pendingConns_[conn->getLoop()->getId()];
  // 检查是否存在 peerAddress()
  auto req = pendingConns.find(conn->peerAddress());
  assert(req != pendingConns.end());

  // 设置所有等待在该 ep 上的 promise
  std::vector<ChannelPromise> promises(std::move(req->second));
  pendingConns.erase(req);
  for (auto &pm : promises)
    pm.setValue(conn->getContext<ClientChannel>().get());
}

/// @brief 连接断开的时候执行
void ClientStub::_onDisconnect(const TcpConnectionPtr &conn) {
  Endpoint ep;
  std::string addr = conn->peerAddress().toHostPort();
  int pos = addr.find(':');
  ep.set_ip(addr.substr(0, pos));
  ep.set_port(static_cast<uint16_t>(atoi(addr.substr(pos + 1).c_str())));

  // 销毁 channels_ 中的 ClientChannel，执行 channel onDestory 函数
  auto &channelMap = channels_[conn->getLoop()->getId()];
  auto it = channelMap.find(ep);
  assert(it != channelMap.end());
  it->second->onDestory();
  channelMap.erase(it);

  // baseLoop 处理
  RPC_SERVER.baseLoop()->queueInLoop(
      std::bind(&RpcServer::removeConnectionInLoop, &RPC_SERVER, conn));
  // conn->connectDestoryed();
}

void ClientStub::_onMessage(const TcpConnectionPtr &conn, Buffer *buffer,
                            Timestamp) {
  ////// TODO 需要保证消息的完整性
  auto channel = conn->getContext<ClientChannel>();
  const char *data = buffer->peek(), *const start = data;
  while (buffer->readableBytes() >= static_cast<size_t>(kPbHeaderLen)) {
    try {
      // 解析 buffer bytes 数据
      // 如果成功解析出 Message 设置等待 Message 的 promise.
      // 否则的话说明 bytes 长度不足一条完整的消息，先返回不做处理
      auto msg = channel->onData(data, buffer->readableBytes());
      if (msg) {
        channel->onMessage(std::move(msg));
        buffer->retrieve(data - start);
      } else {
        break;
      }
    } catch (const std::exception &e) {
      // 出现异常，关闭连接
      LOG_ERROR << "some exception onData: " << e.what();
      conn->shutdown();
      break;
    }
  }
}

/// @brief 连接 service 出错，设置在该 peer 上等待的 pm 为异常值
void ClientStub::_onConnFail(EventLoop *loop, const InetAddress &peer) {
  assert(loop->isInLoopThread());
  auto pendingConns = pendingConns_[loop->getId()];
  auto req = pendingConns.find(peer);
  if (req != pendingConns.end()) {
    for (auto &pm : req->second)
      pm.setException(std::make_exception_ptr(
          Exception(ErrorCode::ConnectRefused, peer.toHostPort())));
    pendingConns.erase(req);
  }
}

void ClientStub::setOnCreateChannel(std::function<void(ClientChannel *)> cb) {
  onCreateChannel_ = std::move(cb);
}

void ClientStub::onRegister() {
  channels_.resize(RPC_SERVER.getThreadNum());
  pendingConns_.resize(RPC_SERVER.getThreadNum());
}

/// TODO for RPC SERVER register nameserver stub
/// 设计可以更改
void ClientStub::onRegister(int num) {
  channels_.resize(num);
  pendingConns_.resize(num);
}

} // namespace lrpc