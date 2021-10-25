#include "Socket.h"
#include "InetAddress.h"
#include "SocketsOps.h"
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>

using namespace imitate_muduo;

Socket::~Socket() { sockets::close(sockfd_); }

void Socket::bindAddress(const InetAddress &addr) {
  sockets::bindOrDie(sockfd_, addr.getSockAddrInet());
}

void Socket::listen() { sockets::listenOrDie(sockfd_); }

int Socket::accept(InetAddress *peeraddr) {
  struct sockaddr_in addr;
  bzero(&addr, sizeof addr);
  int connfd = sockets::accept(sockfd_, &addr);
  if (connfd >= 0) {
    peeraddr->setSockAddrInet(addr);
  }
  return connfd;
}

/// 设置打开或关闭地址复用功能
void Socket::setReuseAddr(bool on) {
  int optval = on ? 1 : 0;
  ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);
}