#include "Buffer.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "Logging.h"
#include "TcpClient.h"
#include <cstdio>
#include <functional>
#include <string>
#include <unistd.h>

std::string message = "Hello\n";

void onConnection(const lrpc::net::TcpConnectionPtr &conn) {
  if (conn->connected()) {
    printf("onConnection(): new connection [%s] from %s\n",
           conn->name().c_str(), conn->peerAddress().toHostPort().c_str());
    conn->send(message);
  } else {
    printf("onConnection(): connection [%s] is down\n", conn->name().c_str());
  }
}

void onMessage(const lrpc::net::TcpConnectionPtr &conn, lrpc::util::Buffer *buf,
               lrpc::util::Timestamp receiveTime) {
  printf("onMessage(): received %zd bytes from connection [%s] at %s\n",
         buf->readableBytes(), conn->name().c_str(),
         receiveTime.toFormattedString().c_str());
  printf("onMessage(): [%s]\n", buf->retrieveAsString().c_str());
}

int main() {
  lrpc::net::EventLoop loop;
  lrpc::net::InetAddress serverAddr("localhost", 9981);
  lrpc::net::TcpClient client(&loop, serverAddr);

  client.setConnectionCallback(onConnection);
  client.setMessageCallback(onMessage);
  client.enableRetry();
  client.connect();
  loop.loop();
}