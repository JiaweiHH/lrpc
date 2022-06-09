#include "../Connector.h"
#include "../EventLoop.h"
#include <cstdio>

lrpc::net::EventLoop *g_loop;

void connectCallback(int sockfd) {
  printf("connected.\n");
  g_loop->quit();
}

int main() {
  lrpc::net::EventLoop loop;
  g_loop = &loop;
  lrpc::net::InetAddress addr("127.0.0.1", 9981);
  lrpc::net::ConnectorPtr connector(new lrpc::net::Connector(&loop, addr));
  connector->setNewConnectionCallback(connectCallback);
  connector->start();
  loop.loop();
}