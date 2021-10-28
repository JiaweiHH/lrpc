#include "EventLoop.h"
#include "InetAddress.h"
#include "TcpServer.h"
#include <cstdio>
#include <string>
#include <unistd.h>

std::string message;

void onConnection(const imitate_muduo::TcpConnectionPtr &conn) {
  if (conn->connected()) {
    printf("onConnection(): new connection [%s] from %s\n",
           conn->name().c_str(), conn->peerAddress().toHostPort().c_str());
    conn->send(message);
  } else {
    printf("onConnection(): connection [%s] is down\n", conn->name().c_str());
  }
}

void onWriteComplete(const imitate_muduo::TcpConnectionPtr &conn) {
  conn->send(message);
}

void onMessage(const imitate_muduo::TcpConnectionPtr &conn,
               imitate_muduo::Buffer *buf,
               imitate_muduo::Timestamp recieveTime) {
  time_t time = std::chrono::system_clock::to_time_t(recieveTime);
  printf("onMessage(): recieved %zd bytes from connection [%s] at %s\n",
         buf->readableBytes(), conn->name().c_str(), ctime(&time));
  buf->retrieveAll();
}

int main() {
  printf("main(): pid = %d\n", getpid());
  std::string line;
  for(int i = 33; i < 127; ++i) {
    line.push_back(char(i));
  }
  line += line;
  for(size_t i = 0; i < 127 - 33; ++i) {
    message += line.substr(i, 72) + '\n';
  }
  imitate_muduo::InetAddress listenAddr(9981);
  imitate_muduo::EventLoop loop;
  imitate_muduo::TcpServer server(&loop, listenAddr);
  server.setConnectionCallback(onConnection);
  server.setMessageCallback(onMessage);
  server.setWriteCompleteCallback(onWriteComplete);

  server.start();
  loop.loop();
}