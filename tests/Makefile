BINARIES = test4
LIB_SRC = ../Channel.cc ../EventLoop.cc ../Poller.cc ../Timer.cc ../TimerQueue.cc

CXXFLAGS = -std=c++17 -O0 -g  -Wall -pthread -DBOOST_LOG_DYN_LINK -I..
LDFLAGS = -lpthread -lboost_log

all: $(BINARIES)

$(BINARIES):
	g++ $(CXXFLAGS) -o $@ $(LIB_SRC) $(filter %.cc,$^) $(LDFLAGS)

test1: test1.cc
test2: test2.cc
test3: test3.cc
test4: test4.cc

clean:
	rm -rf $(BINARIES) core