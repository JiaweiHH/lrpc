#ifndef IMITATE_MUDUO_CALLBACK_H
#define IMITATE_MUDUO_CALLBACK_H

#include <functional>
#include <memory>

namespace imitate_muduo {

class TcpConnection;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using TimerCallback = std::function<void()>;
using ConnectionCallback = std::function<void(const TcpConnectionPtr &)>;
using MessageCallback =
    std::function<void(const TcpConnectionPtr &, const char *, ssize_t)>;
using CloseCallback = std::function<void(const TcpConnectionPtr &)>;

} // namespace imitate_muduo

#endif