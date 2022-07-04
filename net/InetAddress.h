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

  inline friend bool operator==(const InetAddress &lhs,
                                const InetAddress &rhs) {
    return lhs.addr_.sin_family == rhs.addr_.sin_family &&
           lhs.addr_.sin_addr.s_addr == rhs.addr_.sin_addr.s_addr &&
           lhs.addr_.sin_port == rhs.addr_.sin_port;
  }
};

} // namespace net
} // namespace lrpc

namespace std {

template <> struct hash<lrpc::net::InetAddress> {
  std::size_t operator()(const lrpc::net::InetAddress &addr) const noexcept {
    size_t h1 = std::hash<short>{}(addr.getSockAddrInet().sin_family),
           h2 = std::hash<unsigned short>{}(addr.getSockAddrInet().sin_port),
           h3 = std::hash<unsigned int>{}(
               addr.getSockAddrInet().sin_addr.s_addr);
    size_t tmp = h1 ^ (h2 << 1);
    return h3 ^ (tmp << 1);
  }
};

}; // namespace std

#endif