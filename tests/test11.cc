#include "EventLoop.h"
#include "InetAddress.h"
#include "TcpServer.h"
#include <cstdio>
#include <string>
#include <unistd.h>

std::string message;

void onConnection(const lrpc::net::TcpConnectionPtr &conn) {
  if (conn->connected()) {
    printf("onConnection(): new connection [%s] from %s\n",
           conn->name().c_str(), conn->peerAddress().toHostPort().c_str());
    conn->send(message);
  } else {
    printf("onConnection(): connection [%s] is down\n", conn->name().c_str());
  }
}

void onWriteComplete(const lrpc::net::TcpConnectionPtr &conn) {
  conn->send(message);
}

void onMessage(const lrpc::net::TcpConnectionPtr &conn, lrpc::net::Buffer *buf,
               lrpc::net::Timestamp recieveTime) {
  printf("onMessage(): recieved %zd bytes from connection [%s] at %s\n",
         buf->readableBytes(), conn->name().c_str(),
         recieveTime.toFormattedString().c_str());
  buf->retrieveAll();
}

int main(int argc, char *argv[]) {
  printf("main(): pid = %d\n", getpid());
  std::string line;
  for (int i = 33; i < 127; ++i) {
    line.push_back(char(i));
  }
  line += line;
  for (size_t i = 0; i < 127 - 33; ++i) {
    message += line.substr(i, 72) + '\n';
  }
  lrpc::net::InetAddress listenAddr(9981);
  lrpc::net::EventLoop loop;
  lrpc::net::TcpServer server(&loop, listenAddr);
  server.setConnectionCallback(onConnection);
  server.setMessageCallback(onMessage);
  server.setWriteCompleteCallback(onWriteComplete);
  if (argc > 1) {
    server.setThreadNum(atoi(argv[1]));
  }
  server.start();
  loop.loop();
}