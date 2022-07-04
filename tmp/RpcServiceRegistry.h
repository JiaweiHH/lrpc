#ifndef LRPC_REGISTRY_H
#define LRPC_REGISTRY_H

#include "Acceptor.h"
#include "Buffer.h"
#include "Callback.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "TcpConnection.h"
#include "TcpServer.h"
#include "Timestamp.h"
#include <memory>
#include <unordered_map>

using namespace lrpc::net;
using namespace lrpc::util;

namespace lrpc {

class RpcServiceRegistry {
public:
  RpcServiceRegistry(const RpcServiceRegistry &) = delete;
  RpcServiceRegistry &operator=(const RpcServiceRegistry &) = delete;
  RpcServiceRegistry(const InetAddress &listenAddr);
  ~RpcServiceRegistry();

  void setThreadNum(int numThreads);
  void start();

private:
  void newConnection(int sockfd, const InetAddress &);
  void removeConnection(const TcpConnectionPtr &);
  void removeConnectionInLoop(const TcpConnectionPtr &);
  void handleMessage(const TcpConnectionPtr &, Buffer *, Timestamp);
  void handleNewConnection(const TcpConnectionPtr &);
  const std::string name_;
  std::shared_ptr<EventLoop> loop_;
  std::unique_ptr<Acceptor> acceptor_;
  std::unique_ptr<EventLoopThreadPool> threadPool_;
  bool started_;
  int nextConnId_;
  std::unordered_map<std::string, TcpConnectionPtr> connections_;
  /**
   * @brief 服务名到服务地址的多重映射
   * 
   */
  std::unordered_multimap<std::string, std::string> services_;
  std::map<std::string, std::vector<std::multimap<std::string, std::string>::iterator>> iters_;
};

} // namespace lrpc
#endif