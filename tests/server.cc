#include "EventLoop.h"
#include "Logging.h"
// #include "RpcServer.h"
#include "Server.h"
#include "RpcService.h"
#include "test_rpc.pb.h"
#include <algorithm>
#include <string>

class TestServiceImpl : public lrpc::test::TestService {
public:
  virtual void ToUpper(::google::protobuf::RpcController *,
                       const ::lrpc::test::EchoRequest *request,
                       ::lrpc::test::EchoResponse *response,
                       ::google::protobuf::Closure *done) override {
    std::string *answer = response->mutable_text();
    answer->resize(request->text().size());
    std::transform(request->text().begin(), request->text().end(),
                   answer->begin(), ::toupper);
    LOG_DEBUG << "Service ToUpper result = " << *answer;
    done->Run();
  }
  virtual void AppendDots(::google::protobuf::RpcController *,
                          const ::lrpc::test::EchoRequest *request,
                          ::lrpc::test::EchoResponse *response,
                          ::google::protobuf::Closure *done) override {
    std::string *answer = response->mutable_text();
    *answer = request->text();
    answer->append("...................");
    // LOG_DEBUG << "Service AppendDots result = " << *answer;
    done->Run();
  }
};

int main() {
  int threads = std::thread::hardware_concurrency() - 1;
  auto testsrv = new lrpc::Service(new TestServiceImpl);
  testsrv->setEndpoint(lrpc::createEndpoint("127.0.0.1:9987"));
  lrpc::RpcServer server;
  server.setThreadNum(threads);
  server.addService(testsrv);
  server.setNameServer("127.0.0.1:6379");
  server.startServer();
}