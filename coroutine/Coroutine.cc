#include "Coroutine.h"
#include <cassert>

// https://zhuanlan.zhihu.com/p/349738180

namespace lrpc {

namespace coroutine {

// static variable initialization
thread_local unsigned int Coroutine::sid_ = 0;
thread_local Coroutine Coroutine::main_;
thread_local Coroutine *Coroutine::current_ = nullptr;

Coroutine::Coroutine(std::size_t stack_size)
    : id_(++sid_), state_(State::Init),
      stack_(stack_size > kDefaultStackSize ? stack_size : kDefaultStackSize) {
  if (this == &main_) {
    std::vector<char>().swap(stack_);
    return;
  }
  if (id_ == main_.id_)
    id_ = ++sid_;

  // 获取当前协程的 context
  int ret = ::getcontext(&ctx_);
  assert(ret == 0);
  // 设置协程的栈
  ctx_.uc_stack.ss_sp = &stack_[0];
  ctx_.uc_stack.ss_size = stack_.size();
  // 不指定当前 context 执行结束之后的下一个 context
  // 若为空，则执行完当前 context 之后退出程序
  ctx_.uc_link = nullptr;
  // 初始化 context，设置 context 的入口函数
  ::makecontext(&ctx_, reinterpret_cast<void (*)(void)>(&Coroutine::_Run), 1,
                this);
}

AnyPointer Coroutine::_Send(Coroutine *crt, AnyPointer param) {
  assert(crt);
  assert(this == current_);
  assert(this != crt);

  current_ = crt;
  if (param) {
    if (crt->state_ == State::Init && crt != &Coroutine::main_)
      throw std::runtime_error(
          "Can't send non-void value to a just-created coroutine");
    this->yieldValue_ = std::move(param);
  }
  int ret = ::swapcontext(&ctx_, &crt->ctx_);
  if (ret != 0) {
    perror("FATAL ERROR: swapcontext");
    throw std::runtime_error("FATAL ERROR: swapcontext failed");
  }
  return std::move(crt->yieldValue_);
}

AnyPointer Coroutine::_Yield(const AnyPointer &param) {
  return _Send(&main_, param);
}

void Coroutine::_Run(Coroutine *crt) {
  assert(&Coroutine::main_ != crt);
  assert(Coroutine::current_ == crt);
  crt->state_ = State::Running;
  if (crt->func_)
    crt->func_();
  crt->state_ = State::Finish;
  crt->_Yield(crt->result_);
}

AnyPointer Coroutine::Send(const CoroutinePtr &crt, AnyPointer param) {
  if (crt->state_ == Coroutine::State::Finish) {
    throw std::runtime_error("Send to a finished coroutine.");
    return AnyPointer();
  }
  if (!Coroutine::current_)
    Coroutine::current_ = &Coroutine::main_;
  return Coroutine::current_->_Send(crt.get(), param);
}

AnyPointer Coroutine::Yield(const AnyPointer &param) {
  return Coroutine::current_->_Yield(param);
}

AnyPointer Coroutine::Next(const CoroutinePtr &crt) {
  return Coroutine::Send(crt);
}

} // namespace coroutine

} // namespace lrpc