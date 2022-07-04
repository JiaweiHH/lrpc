BINARIES = test_client test_server
LIB_SRC = ../net/Channel.cc ../net/EventLoop.cc ../net/Poller.cc ../net/Timer.cc ../net/TimerQueue.cc ../net/EventLoopThread.cc \
../net/SocketsOps.cc ../net/Socket.cc ../net/InetAddress.cc ../net/Acceptor.cc ../net/TcpConnection.cc ../net/EventLoopThreadPool.cc \
../net/TcpServer.cc ../net/TcpClient.cc ../util/Buffer.cc ../util/Timestamp.cc ../net/Connector.cc ../util/LogFile.cc ../util/LogStream.cc ../util/Logging.cc \
../rpc/Coder.cc ../rpc/lrpc.pb.cc ../rpc/RpcException.cc ../rpc/RpcService.cc  ../rpc/ClientStub.cc ../rpc/RpcChannel.cc ../rpc/Server.cc\
../rpc/name_service_protocol/RedisProtocol.cc ../rpc/name_service_protocol/RedisClientContext.cc \
./test_rpc.pb.cc

CXXFLAGS = -std=c++17 -O0 -g  -Wall -pthread -I../net -I../util -I../future -I../rpc
LDFLAGS = -lprotobuf -lpthread

all: $(BINARIES)

$(BINARIES):
	g++ $(CXXFLAGS) -o $@ $(LIB_SRC) $(filter %.cc,$^) $(LDFLAGS)

test1: test1.cc
test2: test2.cc
test3: test3.cc
test4: test4.cc
test5: test5.cc
test6: test6.cc
test7: test7.cc
test8: test8.cc
test9: test9.cc
test10: test10.cc
test11: test11.cc
test12: test12.cc
test13: test13.cc
test_future: test_future.cc
test_client: client.cc
test_server: server.cc

clean:
	rm -rf $(BINARIES) core