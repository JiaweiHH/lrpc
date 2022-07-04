/**
 * @file Coder.h
 * @author your name (you@domain.com)
 * @brief Protobuf Message 编解码器
 * @version 0.1
 * @date 2022-06-16
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef LRPC_CODER_H
#define LRPC_CODER_H

#include "Buffer.h"
#include <functional>
#include <google/protobuf/message.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace lrpc {

class RpcMessage;

enum class DecodeState {
  None,
  Waiting,
  Error,
  Ok,
};

extern const int kPbHeaderLen;

/**
 * @brief decode
 *
 */

/// @brief bytes 转化为 Message Decoder 类型
using BytesDecoder =
    std::function<std::shared_ptr<google::protobuf::Message>(const char *&data, size_t len)>;
std::shared_ptr<google::protobuf::Message> bytesDecode(const char *&data, size_t len);
/// @brief Message 转化为 Message Decoder 类型
using MessageDecoder =
    std::function<DecodeState(const google::protobuf::Message &, google::protobuf::Message &)>;
DecodeState messageDecode(const google::protobuf::Message &, google::protobuf::Message &);

/**
 * @brief encode
 *
 */

/// @brief Message -> Frame Encoder, return true if success
using MessageEncoder = std::function<bool(const google::protobuf::Message *, RpcMessage &)>;
bool requestEncode(const google::protobuf::Message *, RpcMessage &);
bool responseEncode(const google::protobuf::Message *, RpcMessage &);
/// @brief Frame -> Bytes Encoder
using BytesEncoder = std::function<lrpc::util::Buffer(const RpcMessage &)>;
lrpc::util::Buffer bytesEncode(const RpcMessage &);

bool hasField(const google::protobuf::Message &msg, const std::string &field);

/**
 * @brief Decoder, Encoder class
 *
 */

class Decoder {
public:
  Decoder();
  void clear();
  void setBytesDecoder(BytesDecoder);
  void setMessageDecoder(MessageDecoder);
  int minLen_;
  BytesDecoder bytesDecoder_;
  MessageDecoder messageDecoder_;

private:
  bool default_;
};

class Encoder {
public:
  Encoder();
  Encoder(MessageEncoder enc);
  void clear();
  void setMessageEncoder(MessageEncoder func);
  void setBytesEncoder(BytesEncoder func);
  MessageEncoder messageEncoder_;
  BytesEncoder bytesEncoder_;

private:
  bool default_;
};

} // namespace lrpc

#endif