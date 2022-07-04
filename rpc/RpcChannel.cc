#include "RpcChannel.h"
#include "EventLoop.h"
#include "Logging.h"
#include "RpcClosure.h"
#include "RpcException.h"

namespace lrpc {

/// -------------- ServerChannel --------------

ServerChannel::ServerChannel(TcpConnectionPtr &conn, Service *service)
    : conn_(conn), service_(service), encoder_(responseEncode) {}

void ServerChannel::setContext(std::shared_ptr<void> ctx) {
  ctx_ = std::move(ctx);
}

std::shared_ptr<Message> ServerChannel::onData(const char *&data, size_t len) {
  return decoder_.bytesDecoder_(data, len);
}

/// @brief 处理解析得到的 Message 请求
bool ServerChannel::onMessage(std::shared_ptr<Message> &&req) {
  std::string method;
  // 解析函数名
  RpcMessage *msg = dynamic_cast<RpcMessage *>(req.get());
  if (msg) {
    if (msg->has_request()) {
      currentId_ = msg->request().id();
      method = msg->request().method_name();
      if (msg->request().service_name() != service_->fullName()) {
        throw Exception(ErrorCode::NoSuchService,
                        msg->request().service_name() + " got, but expect [" +
                            service_->fullName() + "]");
      }
    } else {
      throw Exception(ErrorCode::EmptyRequest,
                      "Service  [" + service_->fullName() +
                          "] expect request from " +
                          conn_->peerAddress().toHostPort());
    }
  } else {
    currentId_ = -1;
    if (!service_->methodSelector_) {
      LOG_ERROR << "How to get method name from message? You forget to set "
                   "methodSelector";
      throw Exception(ErrorCode::MethodUndetermined,
                      "methodSelector not set for [" + service_->fullName() +
                          "]");
    } else {
      // TODO methodSelector 的作用？
      method = service_->methodSelector_(req.get());
    }
  }
  invoke(method, std::move(req));
  return true;
}

void ServerChannel::invoke(const std::string &methodName,
                           std::shared_ptr<Message> &&req) {
  const auto googleService = service_->getService();
  auto method = googleService->GetDescriptor()->FindMethodByName(methodName);
  if (!method) {
    LOG_ERROR << "Invoke No Such Method [" << methodName;
    throw Exception(ErrorCode::NoSuchMethod,
                    "Not find method [" + methodName + "]");
  }

  // TODO 为啥要 MessageToMessage decoder
  if (decoder_.messageDecoder_) {
    std::unique_ptr<Message> request(
        googleService->GetRequestPrototype(method).New());
    decoder_.messageDecoder_(*req, *request);
    req.reset(request.release());
  }
  /**
   * @brief protobuf callMethod 函数接受 raw pointer
   * 因此会导致内存泄漏，这里在 Closure::Run 执行结束的时候 delete this
   */
  std::shared_ptr<Message> response(
      googleService->GetResponsePrototype(method).New());
  std::weak_ptr<TcpConnection> wconn(conn_->shared_from_this());
  // 绑定 CallMethod 回调函数
  auto done = new Closure(&ServerChannel::handleMethodDone, this, wconn,
                          currentId_, response);
  // 执行函数
  googleService->CallMethod(method, nullptr, req.get(), response.get(), done);
}

/// @brief rpc call 执行结束的时候会调用
void ServerChannel::handleMethodDone(std::weak_ptr<TcpConnection> wconn, int id,
                                     std::shared_ptr<Message> response) {
  // 判断连接是否已经断开
  if (!wconn.lock())
    return;
  auto conn = wconn.lock();
  // 解析 Protobuf ResponseMessage
  RpcMessage message;
  Response *resp = message.mutable_response();
  if (id >= 0)
    resp->set_id(id);
  bool success = encoder_.messageEncoder_(response.get(), message);
  assert(success);

  if (encoder_.bytesEncoder_) {
    Buffer bytes = encoder_.bytesEncoder_(message);
    conn->send(bytes.retrieveAsString());
  } else {
    // TODO serialized_response 什么意思
    const auto &bytes = resp->serialized_response();
    conn->send(bytes);
  }
}

/// @brief rpc call 出现错误的时候会被调用
void ServerChannel::_onError(const std::exception &err, int code) {
  assert(conn_->getLoop()->isInLoopThread());
  RpcMessage message;
  Response *resp = message.mutable_response();
  if (currentId_ != -1)
    resp->set_id(currentId_);
  resp->mutable_error()->set_msg(err.what());
  resp->mutable_error()->set_errnum(code);
  bool success = encoder_.messageEncoder_(nullptr, message);
  assert(success);
  if (encoder_.bytesEncoder_) {
    Buffer bytes = encoder_.bytesEncoder_(message);
    conn_->send(bytes.retrieveAsString());
  } else {
    // TODO serialized_response 什么意思
    const auto &bytes = resp->serialized_response();
    conn_->send(bytes);
  }
}

/// -------------- ClientChannel --------------

static const std::string idStr("id");
static const std::string svrStr("service_name");
static const std::string methodStr("method_name");
thread_local int ClientChannel::reqId_{0};

/// @brief 构造函数，启动一个 run every 事件，用来检查过期的请求
ClientChannel::ClientChannel(TcpConnectionPtr &&conn, ClientStub *service)
    : conn_(std::move(conn)), service_(service), encoder_(requestEncode) {
  pendingTimeoutId_ = conn->getLoop()->runEvery(
      1.0, std::bind(&ClientChannel::_checkPendingTimeout, this));
}

int ClientChannel::generateId() { return ++reqId_; }

/// @brief 对客户端请求编码
Buffer ClientChannel::_messageToBytesEncoder(std::string &&method,
                                             const Message &request) {
  RpcMessage rpcMsg;
  encoder_.messageEncoder_(&request, rpcMsg);
  // mutable 方法的含义
  Request *req = rpcMsg.mutable_request();
  if (!hasField(*req, svrStr))
    req->set_service_name(service_->fullName());
  if (!hasField(*req, methodStr))
    req->set_method_name(std::move(method));
  if (!hasField(*req, idStr))
    req->set_id(generateId()); // id 唯一标识了 request
  else
    reqId_ = req->id();

  if (encoder_.bytesEncoder_)
    return encoder_.bytesEncoder_(rpcMsg);
  else {
    Buffer bytes;
    const auto &data = req->serialized_request();
    bytes.append(data.data(), data.size());
    return bytes;
  }
}

/// @brief 收到消息的时候会被调用，返回解码之后的消息
std::shared_ptr<Message> ClientChannel::onData(const char *&data, size_t len) {
  return decoder_.bytesDecoder_(data, len); // bytes -> Message
}

/// @brief 在 onData 之后会被调用，
bool ClientChannel::onMessage(std::shared_ptr<Message> msg) {
  RpcMessage *frame = dynamic_cast<RpcMessage *>(msg.get());
  if (frame) {
    assert(hasField(frame->response(), idStr));
    // 找到请求对应的 RequestContext，如果没有则可能这个请求已经超时了
    const int id = frame->response().id();
    auto it = pendingCalls_.find(id);
    if (it != pendingCalls_.end()) {
      // 设置 request 对应的 promise
      it->second.promise.setValue(std::move(msg));
    } else {
      LOG_ERROR << "ClientChannel::onMessage can not find " << id
                << ", maybe TIMEOUT already";
    }
    // 从 waiting list 中删除这个请求
    pendingCalls_.erase(it);
    return true;
  }
  // TODO Message 无法向下转型
  LOG_INFO << "Don't panic: RpcMessage bad_cast, may be text message";
  auto it = pendingCalls_.begin();
  it->second.promise.setValue(std::move(msg));
  pendingCalls_.erase(it);
  return false;
}

/// @brief channel 被销毁的时候会执行
/// 如果有定时任务并且 connection 还没有销毁，则取消定时任务
void ClientChannel::onDestory() {
  if (!pendingTimeoutId_.empty()) {
    auto conn = conn_.lock();
    if (conn)
      conn->getLoop()->cancel(pendingTimeoutId_);
    pendingTimeoutId_.reset();
  }
}

void ClientChannel::_checkPendingTimeout() {
  auto now = Timestamp::now();
  for (auto it = pendingCalls_.begin(); it != pendingCalls_.end();) {
    // it 后续的都不会到期，按照时间排序
    if (now < addTime(it->second.timestamp, 60.0))
      return;
    if (it->second.promise.isReady()) { // 提示删除了已经准备好的 context
      LOG_DEBUG << "checkout for erase pending call id: " << it->first;
    } else { // 提示删除了超时的 context
      LOG_ERROR << "TIMEOUT: Checkout for pending call id: " << it->first;
    }
    // 从 waiting list 中移除 context
    it = pendingCalls_.erase(it);
  }
}

} // namespace lrpc