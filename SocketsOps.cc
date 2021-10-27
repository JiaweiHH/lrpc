#include "SocketsOps.h"

#include <boost/log/trivial.hpp>
#include <cstdio>
#include <cstring>
#include <error.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace imitate_muduo;

namespace {

using SA = struct sockaddr;

const SA *sockaddr_cast(const struct sockaddr_in *addr) {
  return reinterpret_cast<const SA *>(addr);
}

SA *sockaddr_cast(struct sockaddr_in *addr) {
  return reinterpret_cast<SA *>(addr);
}

void setNonBlockAndCloseOnExec(int sockfd) {
  // set non-block
  int flags = ::fcntl(sockfd, F_GETFL, 0);
  flags |= O_NONBLOCK;
  ::fcntl(sockfd, F_SETFL, flags);

  // set close-on-exec
  flags = ::fcntl(sockfd, F_GETFL, 0);
  flags |= FD_CLOEXEC;
  ::fcntl(sockfd, F_SETFD, flags);
}

} // namespace

int sockets::createNonblockingOrDie() {
#if VALGRIND
  int sockfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sockfd < 0)
    BOOST_LOG_TRIVIAL(fatal) << "sockets::createNonblockingOrDie";
  setNonBlockAndCloseOnExec(sockfd);
#else
  int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                        IPPROTO_TCP);
  if (sockfd < 0)
    BOOST_LOG_TRIVIAL(fatal) << "sockets::createNonblockingOrDie";
#endif
  return sockfd;
}

void sockets::bindOrDie(int sockfd, const struct sockaddr_in &addr) {
  int ret = ::bind(sockfd, sockaddr_cast(&addr), sizeof addr);
  if (ret < 0)
    BOOST_LOG_TRIVIAL(fatal) << "sockets::bindOrDie";
}

void sockets::listenOrDie(int sockfd) {
  int ret = ::listen(sockfd, SOMAXCONN);
  if (ret < 0)
    BOOST_LOG_TRIVIAL(fatal) << "sockets::listenAddr";
}

int sockets::accept(int sockfd, struct sockaddr_in *addr) {
  socklen_t addrlen = sizeof *addr;
#if VALGRIND
  int connfd = ::accept(sockfd, sockaddr_cast(addr), &addrlen);
  setNonBlockAndCloseOnExec(connfd);
#else
  int connfd = ::accept4(sockfd, sockaddr_cast(addr), &addrlen,
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
#endif
  if (connfd < 0) {
    int savedErrno = errno;
    BOOST_LOG_TRIVIAL(error) << "Socket::accept";
    switch (savedErrno) {
    case EAGAIN:
    case ECONNABORTED:
    case EINTR:
    case EPROTO: // ???
    case EPERM:
    case EMFILE: // per-process lmit of open file desctiptor ???
      // expected errors
      errno = savedErrno;
      break;
    case EBADF:
    case EFAULT:
    case EINVAL:
    case ENFILE:
    case ENOBUFS:
    case ENOMEM:
    case ENOTSOCK:
    case EOPNOTSUPP:
      // unexpected errors
      BOOST_LOG_TRIVIAL(fatal) << "unexpected error of ::accept " << savedErrno;
      break;
    default:
      BOOST_LOG_TRIVIAL(fatal) << "unknown error of ::accept " << savedErrno;
      break;
    }
  }
  return connfd;
}

void sockets::close(int sockfd) {
  if (::close(sockfd) < 0)
    BOOST_LOG_TRIVIAL(error) << "sockets::close";
}

void sockets::shutdownWrite(int sockfd) {
  if (::shutdown(sockfd, SHUT_WR) < 0)
    BOOST_LOG_TRIVIAL(error) << "sockets::shutdownWrite";
}

/// 获取 addr 结构体中的 addr 和 port
void sockets::toHostPort(char *buf, size_t size,
                         const struct sockaddr_in &addr) {
  char host[INET_ADDRSTRLEN] = "INVALID";
  ::inet_ntop(AF_INET, &addr.sin_addr, host, sizeof host);
  uint16_t port = sockets::hostToNetwork16(addr.sin_port);
  sprintf(buf, "%s:%u", host, port);
}

/// 设置 sockaddr_in 类型的 addr 参数
void sockets::fromHostPort(const char *ip, uint16_t port,
                           struct sockaddr_in *addr) {
  addr->sin_family = AF_INET;
  addr->sin_port = sockets::hostToNetwork16(port);
  if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
    BOOST_LOG_TRIVIAL(error) << "sockets::fromHostPort";
}

/// 获取本地 socket 的地址信息
struct sockaddr_in sockets::getLocalAddr(int sockfd) {
  struct sockaddr_in localaddr;
  bzero(&localaddr, sizeof localaddr);
  socklen_t addrlen = sizeof(localaddr);
  if(::getsockname(sockfd, sockaddr_cast(&localaddr), &addrlen) < 0)
    BOOST_LOG_TRIVIAL(error) << "sockets::getLocalAddr";
  return localaddr;
}

int sockets::getSocketError(int sockfd) {
  int optval;
  socklen_t optlen = sizeof optval;

  if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0) {
    return errno;
  } else {
    return optval;
  }
}