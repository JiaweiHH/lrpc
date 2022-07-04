#ifndef LRPC_CALL_H
#define LRPC_CALL_H

#include "ClientStub.h"
#include "RpcChannel.h"
#include "RpcException.h"
#include "future.h"
#include <memory>
#include <string>

namespace lrpc {

using google::protobuf::Message;

class Endpoint;
class ClientChannel;

namespace {

template <typename R>
Future<Result<R>> _innerCallServer(ClientStub *stub, const std::string &method,
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
Future<Result<R>> serverCall(const std::string &service, const std::string &method,
                       const std::shared_ptr<Message> &req,
                       const Endpoint &ep = Endpoint::default_instance()) {
  // 找到 clientStub
  auto stub = SERVER.getClientStub(service);
  if (!stub)
    return makeExceptionFuture<Result<R>>(
        Exception(ErrorCode::NoSuchService, service));
  return _innerCallServer<R>(stub, method, req, ep);
}
/// @brief rpc call 接口重载，区别在于这里的 req 是原始 request 的引用
/// 因此需要制作一份拷贝
/// TODO 为啥需要拷贝一份？
template <typename R>
Future<Result<R>> serverCall(const std::string &service, const std::string &method,
                       const Message &req,
                       const Endpoint &ep = Endpoint::default_instance()) {
  auto stub = SERVER.getClientStub(service);
  if (!stub)
    return makeExceptionFuture<Result<R>>(
        Exception(ErrorCode::NoSuchService, service));
  // 拷贝一份 request
  std::shared_ptr<Message> reqCopy(req.New());
  reqCopy->CopyFrom(req);
  return _innerCallServer<R>(stub, method, reqCopy, ep);
}

namespace {

template <typename R>
Future<Result<R>> _innerCallServer(ClientStub *stub, const std::string &method,
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