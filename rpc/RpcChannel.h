#ifndef LRPC_RPCCHANNEL_H
#define LRPC_RPCCHANNEL_H

#include "Buffer.h"
#include "ClientStub.h"
#include "Coder.h"
#include "EventLoop.h"
#include "Logging.h"
#include "RpcException.h"
#include "RpcService.h"
#include "TcpConnection.h"
#include "future.h"
#include <google/protobuf/message.h>
#include <map>
#include <memory>

namespace lrpc {

using google::protobuf::Message;
using namespace util;
using namespace net;

// ------------------ ServerChannel ------------------

class ServerChannel {
  friend class Service;

public:
  ServerChannel(TcpConnectionPtr &conn, Service *service);
  ~ServerChannel() = default;

  void setContext(std::shared_ptr<void> ctx);
  template <typename T> std::shared_ptr<T> getContext() const;
  std::shared_ptr<Message> onData(const char *&data, size_t len);
  bool onMessage(std::shared_ptr<Message> &&req);

private:
  void invoke(const std::string &methodName,
              std::shared_ptr<Message> &&request);
  void handleMethodDone(std::weak_ptr<TcpConnection> wconn, int id,
                        std::shared_ptr<Message> response);
  void _onError(const std::exception &err, int code);

  TcpConnectionPtr conn_;
  Service *const service_;
  std::shared_ptr<void> ctx_;

  Decoder decoder_;
  Encoder encoder_;

  int currentId_{0};
};

template <typename T> std::shared_ptr<T> ServerChannel::getContext() const {
  return std::static_pointer_cast<T>(ctx_);
}

// ------------------ ClientChannel ------------------

/**
 * @brief ClientChannel
 *
 */
class ClientChannel {
  friend class ClientStub;

public:
  ClientChannel(TcpConnectionPtr &&conn, ClientStub *service);
  ~ClientChannel() = default;

  void setContext(std::shared_ptr<void> ctx) { ctx_ = std::move(ctx); }
  void setEncoder(Encoder enc) { encoder_ = std::move(enc); }
  void setDecoder(Decoder dec) { decoder_ = std::move(dec); }

  std::shared_ptr<Message> onData(const char *&data, size_t len);
  bool onMessage(std::shared_ptr<Message> msg);
  void onDestory();

  template <typename T> std::shared_ptr<T> getContext() const;

  template <typename R>
  Future<Result<R>> invoke(const std::string &method,
                           const std::shared_ptr<Message> &request);

private:
  /**
   * @brief 描述每个 request
   *
   */
  struct RequestContext {
    Promise<std::shared_ptr<Message>> promise;
    std::shared_ptr<Message> response;
    Timestamp timestamp;
  };

  template <typename R>
  Future<Result<R>> _invoke(const std::string &method,
                            const std::shared_ptr<Message> &request);
  // 对 rpc request 进行编码
  Buffer _messageToBytesEncoder(std::string &&method, const Message &request);

  // 与服务器连接的 weak_ptr，一个 ClientChannel 对应一个 TcpConnection
  std::weak_ptr<TcpConnection> conn_;
  std::shared_ptr<void> ctx_;
  ClientStub *const service_;
  std::map<int, RequestContext> pendingCalls_;

  Decoder decoder_;
  Encoder encoder_;

  void _checkPendingTimeout();
  TimerId pendingTimeoutId_; // 记录 _checkPendingTimeout 对应的定时事件

  int generateId();
  static thread_local int reqId_;
};

template <typename T> std::shared_ptr<T> ClientChannel::getContext() const {
  return std::static_pointer_cast<T>(ctx_);
}

/// @brief 1. 如果连接失效了，则返回一个异常的 future
///        2. 否则执行 _invoke（在当前线程或者 Loop 线程）
template <typename R>
Future<Result<R>>
ClientChannel::invoke(const std::string &method,
                      const std::shared_ptr<Message> &request) {
  auto conn = conn_.lock();
  if (!conn) {
    // 如果 connection 已经到期了，则抛出异常
    std::string error("Connection lost: method [" + method + "] service [" +
                      service_->fullName() + "]");
    return makeExceptionFuture<Result<R>>(
        Exception(ErrorCode::ConnectionLost, error));
  }
  // 如果在 loop 线程则直接执行，否则的话将任务放到 connection 所属的线程执行
  // 否则直接执行
  if (conn->getLoop()->isInLoopThread())
    return _invoke<R>(method, request);

  auto invoker = std::bind(&ClientChannel::_invoke<R>, this, method, request);
  // invoker return Future<>, Execute return Future<>
  return conn->getLoop()->Execute(std::move(invoker)).unwrap();
}

/**
 * @brief 1. 对 request 进行编码并发送
 * 2. 设置 future 回调函数，回调函数负责解码收到的 response
 *
 * @tparam R
 * @param method
 * @param request
 * @return Future<R> 返回 future<R> 对应解码之后的结果
 */
template <typename R>
Future<Result<R>>
ClientChannel::_invoke(const std::string &method,
                       const std::shared_ptr<Message> &request) {
  // 执行 _invoke 的时候，连接必须存在，并且是 loop thread
  assert(!conn_.expired());
  auto conn = conn_.lock();
  assert(conn->getLoop()->isInLoopThread());
  // 检查 service 中是否存在对应的方法
  if (!service_->getService()->GetDescriptor()->FindMethodByName(method)) {
    std::string error("method [" + method + "], sevice [" +
                      service_->fullName() + "]");
    return makeExceptionFuture<Result<R>>(
        Exception(ErrorCode::NoSuchMethod, error));
  }
  // promise-future 用来等待服务器返回 response
  Promise<std::shared_ptr<Message>> promise;
  auto fut = promise.getFuture();
  // 对 request 进行编码并发送数据
  std::string methodStr = method;
  Buffer bytes = _messageToBytesEncoder(std::move(methodStr), *request);
  if (!conn->send(bytes)) {
    // 发送失败，网络连接被重置
    std::string error("send failed: method [" + method + "], service [" +
                      service_->fullName() + "]");
    return makeExceptionFuture<Result<R>>(
        Exception(ErrorCode::ConnectionReset, error));
  } else {
    // 保存请求上下文
    RequestContext reqContext;
    reqContext.promise = std::move(promise);
    reqContext.response.reset(new R());
    reqContext.timestamp = Timestamp::now();
    R *rsp = (R *)reqContext.response.get();
    // 设置 future 回调函数当收到请求返回结果的时候，对 response 进行解码
    auto decodeFut =
        fut.then([this, rsp](std::shared_ptr<Message> &&msg) -> Result<R> {
          // 对 respnse 解码
          if (decoder_.messageDecoder_) {
            try {
              decoder_.messageDecoder_(*msg, *rsp);
            } catch (const std::exception &exp) {
              return Result<R>(std::current_exception());
            } catch (...) {
              LOG_ERROR << "Unknow exception when messageDecoder_";
              return Result<R>(std::current_exception());
            }
          } else {
            *rsp = std::move(*std::static_pointer_cast<R>(msg));
          }
          return std::move(*rsp);
        });
    // 将已经提交（发送）的请求保存，key 是 reqId_
    pendingCalls_.insert({reqId_, std::move(reqContext)});
    // 返回给客户端 Future<R>，客户端在该 Future 上等待结果
    return decodeFut;
  }
}

} // namespace lrpc
#endif