#ifndef LRPC_ENDPOINT_H
#define LRPC_ENDPOINT_H

#include "InetAddress.h"
#include "lrpc.pb.h"
#include "Logging.h"
#include <string>

namespace lrpc {

inline net::InetAddress getAddrFromEndpoint(const Endpoint &ep) {
  return net::InetAddress(ep.ip().data(), ep.port());
}

inline std::string getStringAddrFromEndpoint(const Endpoint &ep) {
  return ep.ip() + ":" + std::to_string(ep.port());
}

inline Endpoint createEndpoint(const std::string &addr) {
  std::string::size_type p = addr.find_first_of(':');
  assert(p != std::string::npos);
  Endpoint ep;
  ep.set_ip(addr.substr(0, p).data());
  ep.set_port(std::stoi(addr.substr(p + 1)));
  return ep;
}

inline bool operator==(const Endpoint &lhs, const Endpoint &rhs) {
  return lhs.ip() == rhs.ip() && lhs.port() == rhs.port();
}

inline bool isValidEndpoint(const Endpoint &ep) {
  return !ep.ip().empty() && ep.port() > 0;
}

} // namespace lrpc

namespace std {
/// @brief Endpoint 哈希函数
template <> struct hash<lrpc::Endpoint> {
  std::size_t operator()(const lrpc::Endpoint &e) const noexcept {
    size_t h1 = std::hash<int>{}(e.port()),
           h2 = std::hash<std::string>{}(e.ip());
    return h1 ^ (h2 << 1);
  }
};
} // namespace std

#endif