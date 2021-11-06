#include "Connector.h"
#include "Channel.h"
#include "EventLoop.h"
#include "SocketsOps.h"
#include <boost/log/trivial.hpp>
#include <cerrno>
#include <functional>

using namespace imitate_muduo;

const int Connector::kMaxRetryDelayMs;

Connector::Connector(EventLoop *loop, const InetAddress &serverAddr)
    : loop_(loop), serverAddr_(serverAddr), connect_(false),
      state_(States::kDisconnected), retryDelayMs_(kInitRetryDelayMs) {
  BOOST_LOG_TRIVIAL(debug) << "ctor[" << this << "]";
}
Connector::~Connector() {
  BOOST_LOG_TRIVIAL(debug) << "dtor[" << this << "]";
  
}