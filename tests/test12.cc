#include "../Connector.h"
#include "../EventLoop.h"
#include <cstdio>

imitate_muduo::EventLoop *g_loop;

void connectCallback(int sockfd) {
  printf("connected.\n");
  g_loop->quit();
}

int main() {
  imitate_muduo::EventLoop loop;
  g_loop = &loop;
  imitate_muduo::InetAddress addr("127.0.0.1", 9981);
  imitate_muduo::ConnectorPtr connector(new imitate_muduo::Connector(&loop, addr));
  connector->setNewConnectionCallback(connectCallback);
  connector->start();
  loop.loop();
}