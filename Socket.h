#ifndef IMITATE_MUDUO_SOCKET_H
#define IMITATE_MUDUO_SOCKET_H

namespace imitate_muduo {

class InetAddress;

/// socket RAII 类
class Socket {
private:
  const int sockfd_;

public:
  explicit Socket(int sockfd) : sockfd_(sockfd) {}
  ~Socket();

  int fd() const { return sockfd_; }

  void bindAddress(const InetAddress &localaddr);
  void listen();
  int accept(InetAddress *peeraddr);

  // Enable/Disable SO_REUSEADDR
  void setReuseAddr(bool on);

  void shutdownWrite();
  void setTcpNoDelay(bool on);
};

} // namespace imitate_muduo

#endif