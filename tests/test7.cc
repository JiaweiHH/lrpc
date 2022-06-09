#include "../Acceptor.h"
#include "../EventLoop.h"
#include "../InetAddress.h"
#include "../SocketsOps.h"
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

void newConnection(int sockfd, const lrpc::net::InetAddress &peerAddr) {
  printf("newConnection(): accepted a new connection from %s\n", peerAddr.toHostPort().c_str());
  ::write(sockfd, "How are you?\n", 13);
  lrpc::net::sockets::close(sockfd);
}

int main() {
  printf("main(): pid = %d\n", getpid());
  lrpc::net::InetAddress listenAddr(9981);
  lrpc::net::EventLoop loop;
  lrpc::net::Acceptor acceptor(&loop, listenAddr);
  acceptor.setNewConnectionCallback(newConnection);
  acceptor.listen();
  loop.loop();
}