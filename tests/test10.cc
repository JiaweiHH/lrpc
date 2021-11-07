#include "EventLoop.h"
#include "InetAddress.h"
#include "TcpServer.h"
#include <stdio.h>
#include <unistd.h>

using namespace std::chrono_literals;

std::string message1;
std::string message2;

void onConnection(const imitate_muduo::TcpConnectionPtr &conn) {
  if (conn->connected()) {
    printf("onConnection(): new connection [%s] from %s\n",
           conn->name().c_str(), conn->peerAddress().toHostPort().c_str());
    std::this_thread::sleep_for(5s);
    conn->send(message1);
    conn->send(message2);
    conn->shutdown();
  } else {
    printf("onConnection(): connection [%s] is down\n", conn->name().c_str());
  }
}

void onMessage(const imitate_muduo::TcpConnectionPtr &conn,
               imitate_muduo::Buffer *buf,
               imitate_muduo::Timestamp recieveTime) {
  printf("onMessage(): recieved %zd bytes from connection [%s] at %s",
         buf->readableBytes(), conn->name().c_str(),
         recieveTime.toFormattedString().c_str());
  buf->retrieveAll();
}

int main(int argc, char *argv[]) {
  printf("main(): pid = %d\n", getpid());
  int len1 = 100;
  int len2 = 200;
  if (argc > 2) {
    len1 = atoi(argv[1]);
    len2 = atoi(argv[2]);
  }
  message1.resize(len1);
  message2.resize(len2);

  std::fill(message1.begin(), message1.end(), 'A');
  std::fill(message2.begin(), message2.end(), 'B');

  imitate_muduo::InetAddress listenAddr(9981);
  imitate_muduo::EventLoop loop;
  imitate_muduo::TcpServer server(&loop, listenAddr);
  server.setConnectionCallback(onConnection);
  server.setMessageCallback(onMessage);
  if (argc > 3) {
    server.setThreadNum(atoi(argv[3]));
  }
  server.start();
  loop.loop();
}