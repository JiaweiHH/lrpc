#ifndef IMITATE_MUDUO_INETADDRESS_H
#define IMITATE_MUDUO_INETADDRESS_H

#include <netinet/in.h>
#include <string>

namespace lrpc {
namespace net {

/// 网络地址类
class InetAddress {
private:
  struct sockaddr_in addr_;

public:
  explicit InetAddress(uint16_t port);
  InetAddress(const std::string &ip, uint16_t port);
  InetAddress(const struct sockaddr_in &addr) : addr_(addr) {}

  std::string toHostPort() const;

  const struct sockaddr_in &getSockAddrInet() const { return addr_; }

  void setSockAddrInet(const struct sockaddr_in &addr) { addr_ = addr; }
};

} // namespace net
} // namespace lrpc

#endif