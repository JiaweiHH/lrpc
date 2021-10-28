BINARIES = test11
LIB_SRC = ../Channel.cc ../EventLoop.cc ../Poller.cc ../Timer.cc ../TimerQueue.cc ../EventLoopThread.cc ../SocketsOps.cc ../Socket.cc ../InetAddress.cc ../Acceptor.cc ../TcpConnection.cc ../TcpServer.cc ../Buffer.cc

CXXFLAGS = -std=c++17 -O0 -g  -Wall -pthread -DBOOST_LOG_DYN_LINK -I..
LDFLAGS = -lpthread -lboost_log

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

clean:
	rm -rf $(BINARIES) core