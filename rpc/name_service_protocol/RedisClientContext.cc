#include <algorithm>
#include <ctype.h>
#include <string>

#include "ClientStub.h"
#include "RedisClientContext.h"
#include "RedisProtocol.h"
#include "RpcChannel.h"
#include "RpcEndpoint.h"
#include "lrpc.pb.h"
#include "util.h"
// #include "util/Util.h"

namespace lrpc {

// hset -> keepalive
// hgetall -> getendpoints

using namespace net;

/// @brief encode 之后会将消息发送给 name service
bool RedisClientContext::M2FEncode(const google::protobuf::Message *msg,
                                   lrpc::RpcMessage &frame) {
  auto request = frame.mutable_request();
  std::string *result = request->mutable_serialized_request();
  auto req = dynamic_cast<const lrpc::ServiceName *>(msg);
  if (req) {
    // GetEndpoints
    *result = "hgetall " + req->name();
    operates_.push(Oper::GET_ENDPOINTS);
  } else {
    auto req = dynamic_cast<const lrpc::KeepaliveInfo *>(msg);
    if (!req)
      return false;

    // Keepalive
    *result = "hset " + req->servicename() + " " +
              getAddrFromEndpoint(req->endpoint()).toHostPort() + " " +
              std::to_string(time(nullptr));
    operates_.push(Oper::KEEPALIVE);
  }

  *result += "\r\n";
  return true;
}

/// @brief name service 返回消息的 decode 函数，负责解析 name service 返回的
/// response
std::shared_ptr<google::protobuf::Message>
RedisClientContext::B2MDecode(const char *&data, size_t len) {
  auto state = proto_.Parse(data, data + len);
  if (state == ParseResult::ok) {
    LRPC_DEFER {
      operates_.pop();
      proto_.Reset();
    };

    switch (operates_.front()) {
    case Oper::GET_ENDPOINTS: {
      time_t now = time(nullptr);
      auto msg = std::make_shared<lrpc::EndpointList>();
      // hgetall ep time ep time
      for (auto it(proto_.params_.begin()); it != proto_.params_.end();) {
        Endpoint ep = createEndpoint(*it);
        ++it;
        time_t t = std::stoi(*it++);
        // TODO kick timeout
        if (t + 30 > now)
          *msg->add_endpoints() = ep;
      }

      return std::static_pointer_cast<lrpc::EndpointList>(msg);
    }
    case Oper::KEEPALIVE: {
      auto msg = std::make_shared<lrpc::Status>();
      msg->set_result(0);
      return std::static_pointer_cast<google::protobuf::Message>(msg);
    } break;
    }
    return std::shared_ptr<google::protobuf::Message>();
  } else if (state == ParseResult::wait) {
    return std::shared_ptr<google::protobuf::Message>();
  }

  return std::shared_ptr<google::protobuf::Message>();
}

/// @brief name service 创建的时候，会设置 channel 的 encode 和 decode
void onCreateRedisChannel(lrpc::ClientChannel *chan) {
  auto ctx = std::make_shared<RedisClientContext>();
  chan->setContext(ctx);

  lrpc::Encoder encoder;
  encoder.setMessageEncoder(std::bind(&RedisClientContext::M2FEncode, ctx.get(),
                                      std::placeholders::_1,
                                      std::placeholders::_2));
  chan->setEncoder(std::move(encoder));

  lrpc::Decoder decoder;
  decoder.setBytesDecoder(std::bind(&RedisClientContext::B2MDecode, ctx.get(),
                                    std::placeholders::_1,
                                    std::placeholders::_2));
  chan->setDecoder(std::move(decoder));
}

} // namespace lrpc
