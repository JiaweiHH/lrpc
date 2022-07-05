#include "ClientStub.h"
#include "EventLoop.h"
#include "Logging.h"
// #include "RpcClient.h"
#include "RpcService.h"
#include "Server.h"
#include "lrpc.pb.h"
#include "test_rpc.pb.h"
#include <atomic>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace lrpc;
using namespace lrpc::net;
using namespace lrpc::util;

Timestamp start, end;
int newCount = 0;
const int totalCount = 200 * 10000;

std::shared_ptr<lrpc::test::EchoRequest> req;
const char *g_text = "hello lrpc";

void onCreateChannel(lrpc::ClientChannel *chan) {
  LOG_DEBUG << "onCreateRpcChannel " << (void *)chan;
}
void onResponse(lrpc::Result<lrpc::test::EchoResponse> &&response) {
  try {
    lrpc::test::EchoResponse resp = std::move(response);
    // LOG_INFO << "recieve: " << resp.text();
    ++newCount;
    if (newCount == totalCount) {
      end = Timestamp::now();
      LOG_DEBUG << "done onResponse avg "
                << (totalCount * 0.1f / (timeDifference(end, start))) << " W/s";
    } else if (newCount < totalCount) {
      call<lrpc::test::EchoResponse>("lrpc.test.TestService", "AppendDots", req)
          .then(onResponse);
    }
    if (newCount % 50000 == 0) {
      end = Timestamp::now();
      LOG_INFO << "onResponse avg "
               << (newCount * 0.1f / (timeDifference(end, start))) << " W/s";
    }
  } catch (const std::system_error &exp) {
    assert(exp.code().category() == lrpc::lrpcCategory());
    int errcode = exp.code().value();
    LOG_INFO << "lrpc exception " << exp.what() << ", err " << errcode;
  } catch (const std::exception &exp) {
    LOG_INFO << "onResponse unknown exception " << exp.what();
  }
}

int main() {
  int threads = 1;
  auto testStub = new ClientStub(new lrpc::test::TestService_Stub(nullptr));
  testStub->setOnCreateChannel(onCreateChannel);

  lrpc::RpcServer client;
  client.setThreadNum(threads);
  client.addClientStub(testStub);
  client.setNameServer("127.0.0.1:6379");

  req = std::make_shared<lrpc::test::EchoRequest>();
  req->set_text(g_text);

  auto starter = [&]() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    start = Timestamp::now();
    for (int i = 0; i < threads; ++i) {
      call<lrpc::test::EchoResponse>("lrpc.test.TestService", "AppendDots", req)
          .then(onResponse);
    };
  };

  std::thread t(starter);
  client.startClient();
}