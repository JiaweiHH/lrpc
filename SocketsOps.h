#ifndef IMITATE_MUDUO_SOCKETSOPS_H
#define IMITATE_MUDUO_SOCKETSOPS_H

#include <arpa/inet.h>
#include <endian.h>

namespace imitate_muduo {

/// network tools
namespace sockets {

inline uint64_t hostToNetwork64(uint64_t host64) { return htobe64(host64); }

inline uint32_t hostToNetwork32(uint32_t host32) { return htobe32(host32); }

inline uint16_t hostToNetwork16(uint16_t host16) { return htobe16(host16); }

inline uint64_t networkToHost64(uint64_t net64) { return be64toh(net64); }

inline uint32_t networkToHost32(uint64_t net32) { return be32toh(net32); }

inline uint16_t networkToHost16(uint16_t net16) { return be16toh(net16); }

// create a nonblocking socket file descriptor
// abort if ant error
int createNonblockingOrDie();

int connect(int sockfd, const struct sockaddr_in &addr);
void bindOrDie(int sockfd, const struct sockaddr_in &addr);
void listenOrDie(int sockfd);
int accept(int sockfd, struct sockaddr_in *addr);
void close(int sockfd);
void shutdownWrite(int sockfd);

void toHostPort(char *buf, size_t size, const struct sockaddr_in &addr);
void fromHostPort(const char *ip, uint16_t port, struct sockaddr_in *addr);

struct sockaddr_in getLocalAddr(int sockfd);
struct sockaddr_in getPeerAddr(int sockfd);

int getSocketError(int sockfd);
bool isSelfConnect(int sockfd);
} // namespace sockets

} // namespace imitate_muduo

#endif