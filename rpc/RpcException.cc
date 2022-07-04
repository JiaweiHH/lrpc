#include "RpcException.h"

namespace lrpc {

lrpcErrorCategory::lrpcErrorCategory() {}

const char *lrpcErrorCategory::name() const noexcept {
  return "lrpc.error";
}

std::string lrpcErrorCategory::message(int ec) const {

  switch (static_cast<ErrorCode>(ec)) {
  case ErrorCode::None:
    return "lrpc.error:OK";

  case ErrorCode::NoSuchService:
    return "lrpc.error:NoSuchService";

  case ErrorCode::NoSuchMethod:
    return "lrpc.error:NoSuchMethod";

  case ErrorCode::ConnectionLost:
    return "lrpc.error:ConnectionLost";

  case ErrorCode::ConnectionReset:
    return "lrpc.error:ConnectionReset";

  case ErrorCode::DecodeFail:
    return "lrpc.error:DecodeFail";

  case ErrorCode::EncodeFail:
    return "lrpc.error:EncodeFail";

  case ErrorCode::Timeout:
    return "lrpc.error:Timeout";

  case ErrorCode::TooLongFrame:
    return "lrpc.error:TooLongFrame";

  case ErrorCode::EmptyRequest:
    return "lrpc.error:EmptyRequest";

  case ErrorCode::MethodUndetermined:
    return "lrpc.error:MethodUndetermined";

  case ErrorCode::ThrowInMethod:
    return "lrpc.error:ThrowInMethod";

  case ErrorCode::NoAvailableEndpoint:
    return "lrpc.error:NoAvailableEndpoint";

  case ErrorCode::ConnectRefused:
    return "lrpc.error:ConnectRefused";

  default:
    break;
  }

  return "Bad lrpc.rpc error code";
}

const std::error_category &lrpcCategory() noexcept {
  static const lrpcErrorCategory cat;
  return cat;
}

std::system_error Exception(ErrorCode e, const std::string &msg) {
  return std::system_error(
      std::error_code(static_cast<int>(e), lrpcCategory()), msg);
}

} // namespace lrpc
