#ifndef IMITATE_MUDUO_CALLBACK_H
#define IMITATE_MUDUO_CALLBACK_H

#include "Timestamp.h"
#include <chrono>
#include <functional>
#include <memory>

namespace lrpc {
namespace net {

using namespace util;

class TcpConnection;
class Buffer;

// using Timestamp = std::chrono::system_clock::time_point;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using TimerCallback = std::function<void()>;
using ConnectionCallback = std::function<void(const TcpConnectionPtr &)>;

/// @param Timestamp ::poll 返回的时刻
/// 即消息到达的时刻，可以准确测量程序处理消息的内部延迟
using MessageCallback =
    std::function<void(const TcpConnectionPtr &, Buffer *, Timestamp)>;
using CloseCallback = std::function<void(const TcpConnectionPtr &)>;
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr &)>;

} // namespace net
} // namespace lrpc

#endif