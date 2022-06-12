#ifndef LRPC_COROUTINE_H
#define LRPC_COROUTINE_H

#include <functional>
#include <map>
#include <memory>
#include <type_traits>
#include <ucontext.h>
#include <vector>

namespace lrpc {

namespace coroutine {

using AnyPointer = std::shared_ptr<void>;
class Coroutine;
using CoroutinePtr = std::shared_ptr<Coroutine>;

class Coroutine {
  // 协程的状态
  enum class State {
    Init,
    Running,
    Finish,
  };

public:
  template <typename F, typename... Args>
  static CoroutinePtr CreateCoroutine(F &&f, Args &&...args) {
    return std::make_shared<Coroutine>(std::forward<F>(f),
                                       std::forward<Args>(args)...);
  }
  static AnyPointer Send(const CoroutinePtr &crt,
                         AnyPointer = AnyPointer(nullptr));
  static AnyPointer Yield(const AnyPointer & = AnyPointer(nullptr));
  static AnyPointer Next(const CoroutinePtr &crt);

public:
  explicit Coroutine(std::size_t stackSize = 0); // 委托构造函数

  // 利用模版的 SFINAE 分别创建执行「带返回值函数」和「不带返回值函数」的协程
  // SFINAE(substitution failure is not an error) 即替换失败不是错误
  template <typename F, typename... Args,
            typename B = typename std::enable_if<std::is_void<
                typename std::result_of<F(Args...)>::type>::value>::type,
            typename Dummy = void>
  Coroutine(F &&f, Args &&...args) : Coroutine(kDefaultStackSize) {
    func_ = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
  }
  // result type is not void
  template <typename F, typename... Args,
            typename B = typename std::enable_if<!std::is_void<
                typename std::result_of<F(Args...)>::type>::value>::type>
  Coroutine(F &&f, Args &&...args) : Coroutine(kDefaultStackSize) {
    using ResultType = typename std::result_of<F(Args...)>::type;
    auto me = this;
    // 创建 lambda 来执行 F，捕获 this 指针，将返回值放到 this->result_
    auto temp = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    func_ = [temp, me]() mutable {
      me->result_ = std::make_shared<ResultType>(temp());
    };
  }
  ~Coroutine() = default;

  // TODO 为什么是 non copy 和 non move
  Coroutine(const Coroutine &) = delete;
  void operator=(const Coroutine &) = delete;

  unsigned int GetID() const { return id_; }
  static unsigned int GetCurrentID() { return current_->id_; }

private:
  AnyPointer _Send(Coroutine *crt, AnyPointer = AnyPointer(nullptr));
  AnyPointer _Yield(const AnyPointer & = AnyPointer(nullptr));
  static void _Run(Coroutine *ctx);

  unsigned int id_;       // 协程编号
  State state_;           // 协程状态
  AnyPointer yieldValue_; // 协程之间传递信息

  static const std::size_t kDefaultStackSize = 8 * 1024; // 默认最小栈大小
  std::vector<char> stack_;                              // coroutine stack

  ucontext_t ctx_;             // coroutine 上下文
  std::function<void()> func_; // 协程执行函数
  AnyPointer result_;          // func_ 执行结果

  static thread_local Coroutine *current_; // 当前协程指针
  static thread_local Coroutine main_;     // 线程的主协程指针
  static thread_local unsigned int sid_;   // 线程内全局编号
};

} // namespace coroutine
} // namespace lrpc

#endif