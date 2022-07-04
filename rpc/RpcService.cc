#include "RpcService.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "Logging.h"
#include "RpcChannel.h"
// #include "RpcServer.h"
#include "Server.h"
#include "SocketsOps.h"
#include "TcpConnection.h"

namespace lrpc {

Service::Service(GoogleService *service)
    : service_(service), name_(service->GetDescriptor()->full_name()) {}

/// @brief 设置 service 的 endpoints
void Service::setEndpoint(const Endpoint &ep) {
  assert(!ep.ip().empty());
  endpoint_ = ep;
}

void Service::setMethodSelector(
    std::function<std::string(const Message *)> selector) {
  methodSelector_ = std::move(selector);
}

/// @brief ServerChannel 创建时候的回调函数
void Service::setOnCreateChannel(std::function<void(ServerChannel *)> cb) {
  onCreateChannel_ = std::move(cb);
}

/// @brief 获取 service 内部的 GoogleService
GoogleService *Service::getService() const { return service_.get(); }

/// @brief 获取 service name
const std::string &Service::fullName() const { return name_; }

/// @brief 获取 service endpoint
const Endpoint &Service::getEndpoint() const { return endpoint_; }

bool Service::start() {
  if (endpoint_.ip().empty())
    return false;
  InetAddress listenAddr(endpoint_.ip(), endpoint_.port());
  auto loop = RPC_SERVER.baseLoop();
  auto newConnectionCallback =
      std::bind(&Service::onNewConnection, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3);
  loop->Execute([loop, listenAddr, newConnectionCallback]() {
    std::shared_ptr<Acceptor> acceptor(new Acceptor(loop, listenAddr));
    acceptor->setNewConnectionCallback(
        std::bind(newConnectionCallback, std::placeholders::_1,
                  std::placeholders::_2, acceptor));
    acceptor->listen();
  });
  return true;
}

/// @brief 连接建立成功之后第一个被调用的函数，该函数随后会调用 onNewConnection
void Service::onNewConnection(int sockfd, const InetAddress &peerAddr,
                              std::shared_ptr<Acceptor>) {
  RPC_SERVER.baseLoop()->assertInLoopThread();
  char buf[32];
  sprintf(buf, ":%s#%d", peerAddr.toHostPort().c_str(),
          RPC_SERVER.nextConnId_++);
  string connName = buf;
  InetAddress localAddr(sockets::getLocalAddr(sockfd));
  EventLoop *ioLoop = RPC_SERVER.next(); // 获取 Connection 的 ioLoop
  TcpConnectionPtr conn(
      new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));

  // 连接建立成功，创建 ServerChannel
  auto channel = std::make_shared<ServerChannel>(conn, this);
  conn->setContext(channel);
  assert(conn->getLoop()->getId() < static_cast<int>(channels_.size()));
  // 保存 eventloop 下面的 TcpConnection -> ServerChannel 映射
  auto &channelMap = channels_[conn->getLoop()->getId()];
  bool succ = channelMap.insert({conn->getUniqueId(), channel.get()}).second;
  assert(succ);
  // 执行回调函数
  if (onCreateChannel_)
    onCreateChannel_(channel.get());
  // 设置回调函数
  conn->setMessageCallback(&Service::_onMessage);
  conn->setCloseCallback(
      std::bind(&Service::_onDisconnect, this, std::placeholders::_1));

  // 成功建立连接回调和状态设置
  RPC_SERVER.connections_.insert({connName, conn});
  ioLoop->runInLoop(std::bind(&TcpConnection::connecEstablished, conn));
}

/// @brief 初始化 channel_
void Service::onRegister() { channels_.resize(RPC_SERVER.getThreadNum()); }

/// @brief 收到 request 消息的时候执行，解析 request，
/// 调用 ServerChannel::onMessge 执行 request method（执行完毕会发送数据）
void Service::_onMessage(const TcpConnectionPtr &conn, Buffer *buffer,
                         Timestamp) {
  auto channel = conn->getContext<ServerChannel>();
  const char *data = buffer->peek(), *const start = data;
  while (buffer->readableBytes() >= static_cast<size_t>(kPbHeaderLen)) {
    try {
      auto msg = channel->onData(data, buffer->readableBytes());
      if (msg) {
        // message 解析成功，移动 read 指针
        buffer->retrieve(data - start);
        try {
          channel->onMessage(std::move(msg));
        } catch (const std::system_error &e) {
          // 异常处理
          auto code = e.code();
          assert(code.category() == lrpcCategory());
          channel->_onError(e, code.value());
          switch (code.value()) {
          case static_cast<int>(ErrorCode::NoSuchService):
          case static_cast<int>(ErrorCode::NoSuchMethod):
          case static_cast<int>(ErrorCode::EmptyRequest):
          case static_cast<int>(ErrorCode::ThrowInMethod):
            LOG_WARN << "RecovableException " << code.message();
            break;
          case static_cast<int>(ErrorCode::DecodeFail):
          case static_cast<int>(ErrorCode::MethodUndetermined):
            LOG_ERROR << "FatalException " << code.message();
            conn->shutdown();
            break;
          default:
            LOG_ERROR << "Unknown Exception " << code.message();
            conn->shutdown();
            break;
          }
          break;
        } catch (...) {
          LOG_ERROR << "onMessage: Unknown error";
          conn->shutdown();
          break;
        }
      } else {
        // 不足一条消息，结束消息处理
        break;
      }
    } catch (const std::system_error &e) {
      LOG_ERROR << "some exception onData " << e.what();
      conn->shutdown();
      break;
    } catch (const std::exception &e) {
      LOG_ERROR << "unknown exception onData " << e.what();
      conn->shutdown();
      break;
    }
  }
  // return
}

/// @brief 连接断开回调函数
void Service::_onDisconnect(const TcpConnectionPtr &conn) {
  auto &channelMap = channels_[conn->getLoop()->getId()];
  bool succ = channelMap.erase(conn->getUniqueId());
  assert(succ);

  // 最后销毁 TcpConnection，取消 channel 和 pollfd 移回到 ioLoop 中去执行
  RPC_SERVER.baseLoop()->queueInLoop(
      std::bind(&RpcServer::removeConnectionInLoop, &RPC_SERVER, conn));
  // conn->connectDestoryed();
}

} // namespace lrpc