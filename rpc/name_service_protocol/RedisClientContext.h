#ifndef LRPC_ANANAS_RPC_REDISCLIENTCONTEXT_H
#define LRPC_ANANAS_RPC_REDISCLIENTCONTEXT_H

#include "RedisProtocol.h"
#include <memory>
#include <queue>

namespace google {
namespace protobuf {

class Message;
class Service;

} // namespace protobuf
} // namespace google

namespace lrpc {

class RpcMessage;
class ClientChannel;

class RedisClientContext {
public:
  bool M2FEncode(const google::protobuf::Message *msg, lrpc::RpcMessage &frame);
  std::shared_ptr<google::protobuf::Message> B2MDecode(const char *&data,
                                                       size_t len);

private:
  ClientProtocol proto_;

  enum class Oper {
    GET_ENDPOINTS,
    KEEPALIVE,
  };
  std::queue<Oper> operates_;
};

extern void onCreateRedisChannel(lrpc::ClientChannel *chan);

} // namespace lrpc

#endif
