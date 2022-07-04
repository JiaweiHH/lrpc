#include "Coder.h"
#include "RpcException.h"
#include "lrpc.pb.h"

using google::protobuf::Message;

namespace lrpc {

const int kPbHeaderLen = 4;

bool hasField(const Message &msg, const std::string &field) {
  const auto descriptor = msg.GetDescriptor();
  assert(descriptor);
  const auto fieldDesc = descriptor->FindFieldByName(field);
  if (!fieldDesc)
    return false;
  const auto reflection = msg.GetReflection();
  return reflection->HasField(msg, fieldDesc);
}

static int getLength(const char *&data) {
  int len;
  memcpy(&len, data, kPbHeaderLen);
  return len;
}

/**
 * @brief 消息解码函数，支持从 bytes 和 Message 数据中解析
 */

std::shared_ptr<Message> bytesDecode(const char *&data, size_t len) {
  assert(len >= kPbHeaderLen);
  const int totalLen = getLength(data);
  // 长度超出限制
  if (totalLen <= kPbHeaderLen || totalLen >= 256 * 1024 * 1024)
    throw Exception(ErrorCode::TooLongFrame,
                    "abnormal totalLen:" + std::to_string(totalLen));
  if (static_cast<int>(len) < totalLen) // 还没有一条完整的消息
    return nullptr;

  RpcMessage *frame = nullptr;
  std::shared_ptr<Message> res(frame = new RpcMessage);
  // 解析 data 中的数据
  if (!frame->ParseFromArray(data + kPbHeaderLen, totalLen - kPbHeaderLen))
    throw Exception(ErrorCode::DecodeFail, "ParseFromArray failed");
  data += totalLen;

  return res;
}
DecodeState messageDecode(const Message &rpcMsg, Message &msg) {
  const RpcMessage &frame = dynamic_cast<const RpcMessage &>(rpcMsg);
  if (frame.has_request()) {
    // message 是 rpc request 类型
    msg.ParseFromString(frame.request().serialized_request());
  } else if (frame.has_response()) {
    // message 是 rpc response 类型
    const auto &response = frame.response();
    if (hasField(response, "serialized_response")) {
      msg.ParseFromString(response.serialized_response());
    } else if (hasField(response, "error")) {
      int err =
          hasField(response.error(), "errnum") ? response.error().errnum() : 0;
      const auto msg =
          hasField(response.error(), "msg") ? response.error().msg() : "";
      throw Exception(static_cast<ErrorCode>(err), msg);
    } else {
      throw Exception(ErrorCode::DecodeFail, "EmptyReponse");
    }
  } else {
    throw Exception(ErrorCode::DecodeFail, "MessageDecode failed");
  }
  return DecodeState::Ok;
}

/**
 * @brief 消息编码函数
 *
 */

bool requestEncode(const Message *msg, RpcMessage &frame) {
  Request *req = frame.mutable_request();
  if (msg)
    return msg->SerializeToString(req->mutable_serialized_request());
  else
    return true;
}
bool responseEncode(const Message *msg, RpcMessage &frame) {
  Response *rsp = frame.mutable_response();
  if (msg)
    return msg->SerializeToString(rsp->mutable_serialized_response());
  else
    return true;
}
lrpc::util::Buffer bytesEncode(const RpcMessage &rpcMsg) {
  const int bodyLen = rpcMsg.ByteSize();
  const int totalLen = kPbHeaderLen + bodyLen;
  lrpc::util::Buffer bytes;
  bytes.ensureWritableBytes(sizeof totalLen + bodyLen); // 取保缓冲区大小足够
  bytes.append(&totalLen, sizeof totalLen);             // 写入消息长度
  // 写入消息主体
  if (!rpcMsg.SerializeToArray(bytes.beginWrite(), bodyLen)) {
    bytes.retrieveAll();
    throw Exception(ErrorCode::EncodeFail);
  } else {
    bytes.hasWritten(bodyLen);
  }
  return bytes;
}

/**
 * @brief Decoder
 *
 */

Decoder::Decoder()
    : minLen_(kPbHeaderLen), bytesDecoder_(bytesDecode),
      messageDecoder_(messageDecode), default_(true) {}

void Decoder::clear() {
  minLen_ = 0;
  BytesDecoder().swap(bytesDecoder_);
  MessageDecoder().swap(messageDecoder_);
  default_ = false;
}

void Decoder::setBytesDecoder(BytesDecoder decoder) {
  if (default_)
    clear();
  bytesDecoder_ = std::move(decoder);
}

void Decoder::setMessageDecoder(MessageDecoder decoder) {
  messageDecoder_ = std::move(decoder);
}

/**
 * @brief Encoder
 *
 */

Encoder::Encoder() : default_(false) {}

Encoder::Encoder(MessageEncoder encoder)
    : messageEncoder_(std::move(encoder)), bytesEncoder_(bytesEncode),
      default_(true) {}

void Encoder::clear() {
  default_ = false;
  MessageEncoder().swap(messageEncoder_);
  BytesEncoder().swap(bytesEncoder_);
}

void Encoder::setMessageEncoder(MessageEncoder encoder) {
  messageEncoder_ = std::move(encoder);
}
void Encoder::setBytesEncoder(BytesEncoder encoder) {
  bytesEncoder_ = std::move(encoder);
}

} // namespace lrpc