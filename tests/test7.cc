#include "../Acceptor.h"
#include "../EventLoop.h"
#include "../InetAddress.h"
#include "../SocketsOps.h"
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

void newConnection(int sockfd, const imitate_muduo::InetAddress &peerAddr) {
  printf("newConnection(): accepted a new connection from %s\n", peerAddr.toHostPort().c_str());
  ::write(sockfd, "How are you?\n", 13);
  imitate_muduo::sockets::close(sockfd);
}

int main() {
  printf("main(): pid = %d\n", getpid());
  imitate_muduo::InetAddress listenAddr(9981);
  imitate_muduo::EventLoop loop;
  imitate_muduo::Acceptor acceptor(&loop, listenAddr);
  acceptor.setNewConnectionCallback(newConnection);
  acceptor.listen();
  loop.loop();
}