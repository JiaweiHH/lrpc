#include "EventLoop.h"
#include "Channel.h"
#include "Epoller.h"
#include "Logging.h"
#include "Poller.h"
#include "TimerQueue.h"
#include "signal.h"
#include <assert.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>

using namespace lrpc::net;

// thread_local 变量记录当前线程的 EventLoop
thread_local EventLoop *t_loopInThisThread = 0;
thread_local unsigned int EventLoop::s_id = 0;
std::atomic<int> EventLoop::sequenceId{0};
// IO mutilplexing 超时时间
const int kPollTimeMs = 10000;

static int createEventfd() {
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0) {
    LOG_ERROR << "Failed in eventfd";
    abort();
  }
  return evtfd;
}

/// 用来处理在客户端断开连接之后继续发送数据，导致服务器出错的情况
/// 在构造函数中忽略 SIGPIPE 信号
/// !@param SIGPIPE write on a pipe with no reader
class IgnoreSigPipe {
public:
  IgnoreSigPipe() { ::signal(SIGPIPE, SIG_IGN); }
};
IgnoreSigPipe initObj;

EventLoop::EventLoop()
    : looping_(false), quit_(false), callingPendingFunctors_(false),
      threadId_(std::this_thread::get_id()), poller_(new Poller(this)),
      timerQueue_(new TimerQueue(this)), wakeupFd_(createEventfd()),
      wakeChannel_(new Channel(this, wakeupFd_)) {
  LOG_TRACE << "EventLoop created " << this << " in thread " << threadId_;
  // 检查当前线程是否已经创建了 EventLoop 对象
  if (t_loopInThisThread) {
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  } else {
    t_loopInThisThread = this;
  }
  local_id_ = sequenceId++;
  wakeChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
  wakeChannel_->enableReading();
}

EventLoop::~EventLoop() {
  assert(!looping_);
  t_loopInThisThread = nullptr;
}

/// 每个线程只有一个 EventLoop，返回这个 EventLoop
/// note: 返回值有可能是 nullptr，如果当前线程不是 IO 线程
EventLoop *EventLoop::getEventLoopOfCurrentThread() {
  return t_loopInThisThread;
}

// 执行事件循环
void EventLoop::loop() {
  assert(!looping_);
  assertInLoopThread();
  // note: 不需要加锁保护，因为 EventLoop 只能在当前线程中执行、不存在 race
  // condition
  looping_ = true;
  quit_ = false;

  while (!quit_) {
    // 每一轮事件循环开始的时候需要先清除 activeChannels_
    activeChannels_.clear();
    auto pollReturnTime = poller_->poll(kPollTimeMs, activeChannels_);
    // 分发函数
    for (const auto &channel : activeChannels_) {
      channel->handleEvent(pollReturnTime);
    }
    doPendingFunctors();
  }

  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}

/// 断言错误的时候打印错误信息
void EventLoop::abortNotInLoopThread() {
  // TODO
  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " << std::this_thread::get_id();
}

void EventLoop::quit() {
  quit_ = true;
  // 1. 在 IO 线程调用 quit 就不需要 wakeup 了
  //    因为在 IO 线程中执行 quit 就说明此时线程正在执行时间回调函数
  //    在完成事件回调函数之后会重新判断循环的 quit_
  // 2. 但是在别的线程就需要 wakeup
  //    因为此时 IO 线程可能睡眠在 ::poll 调用中，wakeup 之后可以唤醒 ::poll
  //    中的睡眠 其实就算没有 wakeup 也会超时唤醒
  if (!isInLoopThread())
    wakeup();
}

/// EventLoop 不必关心 Poller 是如何管理 Channel 列表的
/// 调用 Poller::updateChannel()
void EventLoop::updateChannel(Channel *channel) {
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel *channel) {
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->removeChannel(channel);
}

/// 下面三个函数分别让用户选择：
/// 1. 在某一个时间点执行回调
/// 2. 在某一个延迟时间之后执行回调
/// 3. 每隔一个时间段执行回调
TimerId EventLoop::runAt(const Timestamp &time, const TimerCallback &cb) {
  return timerQueue_->addTimer(cb, time, 0.0);
}
TimerId EventLoop::runAfter(double delay, const TimerCallback &cb) {
  auto when = addTime(Timestamp::now(), delay);
  return timerQueue_->addTimer(cb, when, 0.0);
}
TimerId EventLoop::runEvery(double interval, const TimerCallback &cb) {
  auto when = addTime(Timestamp::now(), interval);
  return timerQueue_->addTimer(cb, when, interval);
}

/// 如果用户在 IO 线程中调用，则回调会同步进行
/// 如果用户在其他线程调用，cb 会被加入队列，IO 线程会被唤醒来调用这个 Functor
/// runInLoop 可以轻易在线程间调配任务
void EventLoop::runInLoop(const Functor &cb) {
  if (isInLoopThread()) {
    cb();
  } else {
    queueInLoop(cb);
  }
}
// void EventLoop::runInLoop(Functor &&cb) {
//   if(isInLoopThread()) {
//     cb();
//   } else {
//     queueInLoop(std::move(cb));
//   }
// }

/// 将 Functor 插入队列中，并在必要时唤醒 IO 线程
/// 这个函数也可以被 IO 线程自己调用
void EventLoop::queueInLoop(const Functor &cb) {
  {
    std::lock_guard<std::mutex> lk(mutex_);
    pendingFunctors_.push_back(cb);
  }
  // 1. 如果调用 queueInLoop 的不是 IO 线程
  // 2. 如果此时正在执行 pending functor
  // 第二点的原因是，在 doPendingFunctors 中执行 functor 的时候也可能会调用
  // queueInLoop， 此时如果不 wakeup，则在下一轮的 ::poll 中就不能及时得到有新的
  // functor 的事件信息， 那么新的 functor 也就不能及时得到执行了
  if (!isInLoopThread() || callingPendingFunctors_)
    wakeup();
}
// void EventLoop::queueInLoop(Functor &&cb) {
//   {
//     std::lock_guard lk(mutex_);
//     pendingFunctors_.push_back(std::move(cb));
//   }
//   if (!isInLoopThread() || callingPendingFunctors_)
//     wakeup();
// }

/// 执行其他线程放入的 pending functor
void EventLoop::doPendingFunctors() {
  // 该函数没有反复执行，直到 pendingFunctors_ 为空
  // 因为这样可能会让 IO 线程陷入死循环，那么久无法处理 IO 事件了

  // 将 pengdingFunctors 中的元素移动到局部变量 functors 中
  // 减少临界区长度，不会阻塞其他线程调用 queueInLoop()
  // 也避免了死锁，因为 Functor 可能会调用 queueInLoop()
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;
  {
    std::lock_guard lk(mutex_);
    functors.swap(pendingFunctors_);
  }
  for (auto &func : functors) {
    func();
  }
  callingPendingFunctors_ = false;
}

/// 往 eventfd 中写入数据用来唤醒 IO 线程
void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = ::write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
    LOG_ERROR << "EventLoop::wake() writes " << n << " bytes instead of 8";
}
/// 读取 eventfd
void EventLoop::handleRead() {
  // note: 这里没有选择在 handleRead 中执行 doPendingFunctors 而是放到 loop
  // 中，原因如下
  // 1. 原因在于如果在 handleRead 中执行 doPendingFunctors，
  //    那么在 IO 线程内注册了回调函数并且没有调用 EventLoop::wakeup()，
  //    回调函数不会被立即得到执行，必须等到 wakeup 被调用后才能执行
  // 2. pending functor 的优先级低于其他事件
  // 3. 如果 doPendingFunctors() 放到 EventLoop::handleRead()
  // 中，就必须调用weakup()，
  //    这样流程就涉及三个系统调用 write->poll->read，在 IO
  //    线程内注册回调函数这种情况下，其用不着花费三个系统 放到 loop 中就可以在
  //    IO 线程中注册回调的时候省略三个系统调用
  uint64_t one = 1;
  ssize_t n = ::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
    LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
}

void EventLoop::cancel(TimerId timerId) { timerQueue_->cancel(timerId); }

void EventLoop::Schedule(std::function<void()> f) { Execute(std::move(f)); }

void EventLoop::ScheduleLater(std::chrono::milliseconds duration,
                              std::function<void()> f) {
  double delay = duration.count();
  auto when = addTime(Timestamp::now(), delay / 1000.0);
  timerQueue_->addTimer(f, when, 0.0);
}