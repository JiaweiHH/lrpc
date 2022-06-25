#ifndef LRPC_FUTURE_H
#define LRPC_FUTURE_H

#include "Scheduler.h"
#include "helper.h"
#include "result.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>

// lambda mutable 用来保证可以修改按值捕获的变量

/**
 * @brief then callback 返回 future 类型
 * 
 */
#define _CALLBACK_ decltype(f(r.template get<Args>()...)) innerFuture; \
if (r.hasException()) { \
  innerFuture = f(typename ResultWrapper<typename std::decay<Args...>::type>::Type(r.getException())); \
} else { \
  innerFuture = f(r.template get<Args>()...); \
} \
if (!innerFuture.valid()) { \
  return; \
} \
\
std::unique_lock<std::mutex> lk(innerFuture.state_->mutex_); \
if (innerFuture.state_->progress_ == Progress::Timeout) { \
  throw std::runtime_error("Wrong state : Timeout"); \
} else if (innerFuture.state_->progress_ == Progress::Done) { \
  typename ResultWrapper<FReturnType>::Type r; \
  try { \
    r = std::move(innerFuture.state_->value_); \
  } catch (const std::exception &e) { \
    r = decltype(r)(std::current_exception()); \
  } \
  lk.unlock(); \
  pm.setValue(std::move(r)); \
} else { \
  innerFuture._setCallback([pm = std::move(pm)](typename ResultWrapper<FReturnType>::Type &&r) mutable { \
    pm.setValue(std::move(r)); \
  }); \
}

namespace lrpc {

using lrpc::util::Scheduler;

enum class Progress {
  None,
  Timeout,
  Done,
  Retrived,
};

using TimeoutCallback = std::function<void()>;

/**
 * @brief State 封装了 Result 返回值的状态
 *
 * @tparam T
 */
template <typename T> struct State {
  static_assert(std::is_same_v<T, void> || std::is_copy_constructible<T>() ||
                    std::is_move_constructible<T>(),
                "must be copyable or movable or void");
  using ValueType = typename ResultWrapper<T>::Type;

  State() : progress_(Progress::None), retrieved_{false} {}

  std::mutex mutex_;
  // State 对应的 Future 返回值
  // 如果 T 是普通型别，则 value 型别是 Future<T>
  // 如果 T 是 Future 型别，则 value 型别是 Future<T>
  ValueType value_;
  Progress progress_;
  std::atomic<bool> retrieved_;
  std::function<void(ValueType &&)> then_;
  std::function<void(TimeoutCallback &&)> onTimeout_;
};

template <typename T> class Future;

/**
 * @brief Promise
 *
 * @tparam T
 */
template <typename T> class Promise {
public:
  Promise() : state_(std::make_shared<State<T>>()) {}
  Promise(const Promise &) = default;
  Promise(Promise &&) = default;

  Promise &operator=(const Promise &) = default;
  Promise &operator=(Promise &&) = default;

  /// @brief 设置 future 返回值为异常
  void setException(std::exception_ptr e) {
    {
      std::lock_guard<std::mutex> lk(state_->mutex_);
      // 说明 promise 已经被使用过了
      if (state_->progress_ != Progress::None)
        return;
      state_->progress_ = Progress::Done;
      state_->value_ = typename State<T>::ValueType(std::move(e));
    }
    // 如果有设置后续的回调函数，则继续执行
    if (state_->then_)
      state_->then_(std::move(state_->value_));
  }

  /// @brief 设置 V 类型返回值, SFINAE
  template <typename V = T>
  typename std::enable_if<!std::is_void<V>::value, void>::type setValue(V &&v) {
    {
      std::lock_guard<std::mutex> lk(state_->mutex_);
      if (state_->progress_ != Progress::None)
        return;
      state_->progress_ = Progress::Done;
      state_->value_ = std::forward<V>(v);
    }
    // 执行回调函数
    if (state_->then_)
      state_->then_(std::move(state_->value_));
  }
  /// @brief 设置 void 类型返回值, SFINAE
  template <typename V = T>
  typename std::enable_if<std::is_void<V>::value, void>::type setValue() {
    {
      std::lock_guard<std::mutex> lk(state_->mutex_);
      if (state_->progress_ != Progress::None)
        return;
      state_->progress_ = Progress::Done;
      state_->value_ = Result<void>();
    }
    if (state_->then_)
      state_->then_(std::move(state_->value_));
  }

  /// @brief 获取 Future
  Future<T> getFuture() {
    bool except = false;
    // 如果 retrieved_ != except，即 retrieved_ = true
    // 则说明 Future 已经获取过一次了，此时抛出异常，不允许重复获取
    if (!state_->retrieved_.compare_exchange_strong(except, true)) {
      throw std::runtime_error("Future already retrieved");
    }
    return Future<T>(state_);
  }

  /// @brief 检查 future 是否已经准备好了
  bool isReady() const { return state_->progress_ != Progress::None; }

private:
  std::shared_ptr<State<T>> state_;
};

template <typename T> Future<T> makeExceptionFuture(std::exception_ptr &&);

template <typename T> class Future {
public:
  using InnerType = T;
  template <typename U> friend class Future;

  Future() = default;
  Future(const Future &) = delete;
  explicit Future(std::shared_ptr<State<T>> state) : state_(std::move(state)) {}

  /// @brief 只允许移动语义
  Future(Future &&) = default;
  Future &operator=(Future &&fut) = default;
  void operator=(const Future &) = delete;

  bool valid() const { return state_ != nullptr; }

  /**
   * @brief 等待 future 的返回值
   * template args T: future 类型
   *
   * @param timeout
   * @return State<T>::ValueType
   */
  typename State<T>::ValueType wait(const std::chrono::milliseconds &timeout =
                                        std::chrono::milliseconds(24 * 3600 *
                                                                  1000)) {
    {
      // 检查 future 的状态
      std::lock_guard<std::mutex> lk(state_->mutex_);
      switch (state_->progress_) {
      case Progress::None: // future 还没准备好
        break;
      case Progress::Timeout: // future 已经超时
        throw std::runtime_error("Future timeout");
      case Progress::Done:
        // 设置 future 状态为 Retrived，表示已经被获取了
        // 后续再次尝试获取这个 future 会抛出异常
        state_->progress_ = Progress::Retrived;
        return std::move(state_->value_);
      default:
        // 尝试获取一个已经被提取的 future 会抛出异常
        throw std::runtime_error("Future already retrieved");
      }
    }

    // future 还没有准备好，需要等待 future
    auto cond(std::make_shared<std::condition_variable>());
    auto mutex(std::make_shared<std::mutex>());
    bool ready = false;
    typename State<T>::ValueType value;
    // 设置回调函数，等待 Future 准备好的时候会唤醒 wait 的线程
    // 回调函数的参数需要是当前 Future 内部的 State 内部保存的 value 类型
    this->then([&value, &ready,
                wcond = std::weak_ptr<std::condition_variable>(cond),
                wmutex = std::weak_ptr<std::mutex>(mutex)](
                   typename State<T>::ValueType &&v) {
      // cond 或者 mutex 可能已经超时被释放了
      if (wcond.expired() || wmutex.expired()) {
        return;
      }
      auto cond = wcond.lock();
      auto mutex = wmutex.lock();
      // 唤醒正在等待的线程
      std::lock_guard<std::mutex> lk(*mutex);
      value = std::move(v);
      ready = true;
      cond->notify_one();
    });
    // 等待 future 准备好
    std::unique_lock<std::mutex> lk(*mutex);
    bool success = cond->wait_for(lk, timeout, [&ready]() { return ready; });
    if (success)
      return std::move(value);
    else
      throw std::runtime_error("Future wait_for timeout");
  }

  /**
   * @brief 指定 future 就绪之后需要执行的任务. 可以选择在某个指定的 Scheduler
   * 上执行后续任务，如果没有特别指定 sched 的话，默认在当前 Scheduler 继续执行
   *
   * @tparam F : future 准备好之后，准备执行的回调函数
   * @tparam R : then 操作需要返回一个新的 future
   * @tparam T>
   * @param f 回调函数
   * @return R::returnFutureType 返回值是一个 future
   * 如果 F 不需要上一个 future 的执行结果，则 R = Future<void>
   * 否则的话 R = Future<F(T)>
   */
  template <typename F, typename R = CallableResult<F, T>>
  auto then(F &&f) -> typename R::returnFutureType {
    typedef typename R::Res Arguments;  // F 的返回值
    return _thenImpl<F, R>(nullptr, std::forward<F>(f), Arguments());
  }
  template <typename F, typename R = CallableResult<F, T>>
  auto then(Scheduler *sched, F &&f) -> typename R::returnFutureType {
    typedef typename R::Res Arguments;
    return _thenImpl<F, R>(sched, std::forward<F>(f), Arguments());
  }

  /**
   * @brief 设置 future 的超时回调函数
   * 
   * When register callbacks and timeout for a future like this:
   *      Future<int> f;
   *      f.Then(xx).OnTimeout(yy);
   *
   * There will be one future object created except f, we call f as root future.
   * The yy callback is registed on the last future, here are the possiblities:
   * 1. xx is called, and yy is not called.
   * 2. xx is not called, and yy is called.
   *
   * BUT BE CAREFUL BELOW:
   *
   *      Future<int> f;
   *      f.Then(xx).Then(yy).OnTimeout(zz);
   *  TODO 为啥有 3 个 future 被创建？
   * There will be 3 future objects created except f, we call f as root future.
   * The zz callback is registed on the last future, here are the possiblities:
   * 1. xx is called, and zz is called, yy is not called.
   * 2. xx and yy are called, and zz is called, aha, it's rarely happend but...
   * 3. xx and yy are called, it's the normal case.
   * So, you may shouldn't use OnTimeout with chained futures!!!
   * 
   * @param duration 超时时间
   * @param f 超时回调函数
   * @param sched EventLoop
   */
  void onTimeout(std::chrono::milliseconds duration, TimeoutCallback f, Scheduler *sched) {
    sched->ScheduleLater(duration, [state = state_, cb = std::move(f)]() mutable {
      {
        // 如果 state_ 的状态不是 None，则说明在超时之前要么已经被提取 value 了
        // 要么发生异常了，要么已经有 value 但是还没有被提取
        std::lock_guard<std::mutex> lk(state->mutex_);
        if (state->progress_ != Progress::None)
          return;
        state->progress_ = Progress::Timeout;
      }
      cb();
    });
  }

private:
  /**
   * @brief 如果 F(T) 的执行结果不返回 future 则会执行这个 _thenImpl 模版
   *
   * @tparam F : then 要执行的函数型别
   * @tparam R : CallableResult<F, T> 即函数 F(T) 的执行结果的包装
   * @tparam Args : F(T) 的执行结果，只能是 void 或者 Future<>
   * @param sched :
   * @param f : then 的回调函数
   *
   * @return std::enable_if<!R::isReturnsFuture::value,
   * typename R::returnFutureType>::type
   * 使用 Future<> 包装的函数返回值
   */
  template <typename F, typename R, typename... Args>
  typename std::enable_if<!R::isReturnsFuture::value,
                          typename R::returnFutureType>::type
  _thenImpl(Scheduler *sched, F &&f, ResultOfWrapper<F, Args...>) {
    static_assert(sizeof...(Args) <= 1, "then must take zero/one argument");

    // Future<T> 的 T，也就是 _thenImpl 等会需要返回的 Future 的内部类型
    using FReturnType = typename R::isReturnsFuture::Inner;
    using FuncType = typename std::decay<F>::type; // 移除 F 属性，获取 F 型别

    // 新的异步 future
    Promise<FReturnType> promise;
    auto nextFuture = promise.getFuture();

    std::unique_lock<std::mutex> lk(state_->mutex_);
    if (state_->progress_ == Progress::Timeout) {
      // 1. prev future 已经超时了
      throw std::runtime_error("wrong state : Timeout");
    } else if (state_->progress_ == Progress::Done) {
      // 2. 当前 future 已经完成，并且还没有被获取

      // 先获取当前 future 的执行结果 value_，使用 Result 封装
      typename ResultWrapper<T>::Type r;
      try {
        // TODO 这里为啥要 try catch
        r = std::move(state_->value_);
      } catch (const std::exception &e) {
        r = decltype(r)(std::current_exception());
      }
      lk.unlock();

      // 然后执行 f(r)，如果有 EventLoop 传递进来的话就放到 EventLoop 执行
      // 否则直接在当前线程执行
      if (sched) {
        sched->Schedule([r = std::move(r), f = std::forward<FuncType>(f),
                         pm = std::move(promise)]() mutable {
          // 执行 f(r) 对 f(r) 的返回值进行 Result 包装
          // 这里是用到了 Result 的隐式类型转换，自动将 Result<T> -> T
          auto result = WrapWithResult(f, std::move(r));
          // 设置 nextFuture 已经完成
          pm.setValue(std::move(result));
        });
      } else {
        auto result = WrapWithResult(f, std::move(r));
        promise.setValue(std::move(result));
      }
    } else {
      // None 的分支，这里不会出现 Retrieved，future 只能被一个线程持有，
      // 而当这个线程对 future 执行 then 操作的时候，这个 future 不可能执行
      // wait. 又因为 future 只有在 wait 里面判断出 Done 的时候才会设置
      // Retrieved，因此这里不会出现 Retrieved 的状态

      // 3. 设置 future::state_ 的 then_ 函数
      // 在 Promise setValue 或者 setException 的时候会执行 then_.
      // then_ 的逻辑和前面 Done 的分支一样，如果有 EventLoop 则放到 EventLoop
      // 中执行， 否则的话直接执行
      _setCallback(
          [sched, func = std::forward<FuncType>(f), pm = std::move(promise)](
              typename ResultWrapper<T>::Type &&r) mutable {
            if (sched) {
              sched->Schedule([func = std::move(func), r = std::move(r),
                               pm = std::move(pm)]() mutable {
                auto result = WrapWithResult(func, std::move(r));
                pm.setValue(std::move(result));
              });
            } else {
              auto result = WrapWithResult(func, std::move(r));
              pm.setValue(std::move(result));
            }
          });
    }
    // 返回 nextFuture，供客户端后续执行 wait 或者继续执行 then
    return std::move(nextFuture);
  }
  /**
   * @brief 如果 F(T) 的执行结果返回 future 则会执行这个 _thenImpl 模版
   * 
   * @tparam F : then 要执行的函数型别
   * @tparam R : CallableResult<F, T> 即函数 F(T) 的执行结果的包装
   * @tparam Args
   * @param sched
   * @param f : then 的回调函数
   * @return std::enable_if<R::isReturnsFuture::value,
   * typename R::ReturnFutureType>::type
   * 使用 Future<> 包装的函数返回值
   */
  template <typename F, typename R, typename... Args>
  typename std::enable_if<R::isReturnsFuture::value,
                          typename R::ReturnFutureType>::type
  _thenImpl(Scheduler *sched, F &&f, ResultOfWrapper<F, Args...>) {
    static_assert(sizeof...(Args) <= 1, "then must take zero/one argument");

    using FReturnType = typename R::isReturnsFuture::Inner; // Future<T> 的 T
    using FuncType = typename std::decay<F>::type;

    Promise<FReturnType> promise;
    auto nextFuture = promise.getFuture();

    std::unique_lock<std::mutex> lk(state_->mutex_);
    if (state_->progress_ == Progress::Timeout) {
      // 1. 当前 future 已经超时了
      throw std::runtime_error("Wrong state : Timeout");
    } else if (state_->progress_ == Progress::Done) {
      // 2. 当前 future 执行完毕，并且还没有被获取
      typename ResultWrapper<T>::Type r;
      try {
        r = std::move(state_->value);
      } catch (const std::exception &e) {
        r = decltype(r)(std::current_exception());
      }
      lk.unlock();

      auto cb = [r = std::move(r), f = std::forward<FuncType>(f),
                 pm = std::move(promise)]() mutable { _CALLBACK_ };

      if (sched)
        sched->Schedule(std::move(cb));
      else
        cb();
    } else {
      // 3. 设置 future 的 then_ 函数
      // 等待 promise setValue 或者 setException 的时候执行
      // 这里回调函数的执行逻辑和 Done 分支一样
      _setCallback([sched = sched, func = std::forward<FuncType>(f),
                    pm = std::move(promise)](
                       typename ResultWrapper<T>::Type &&r) mutable {
        auto cb = [f = std::move(func), r = std::move(r),
                   pm = std::move(pm)]() mutable { _CALLBACK_ };
        if (sched)
          sched->Schedule(std::move(cb));
        else
          cb();
      });
    }
    // 返回 next future
    return std::move(nextFuture);
  }

  /// @brief 设置 state_ 的 then_ 回调函数，promise 在 set 的时候会执行
  void
  _setCallback(std::function<void(typename ResultWrapper<T>::Type &&)> &&func) {
    state_->then_ = std::move(func);
  }
  void _setOnTimeout(std::function<void(TimeoutCallback &&)> &&func) {
    state_->onTimeout_ = std::move(func);
  }

  // future 内部等待的数据状态，包括是否被获取了，函数的执行结果等等
  std::shared_ptr<State<T>> state_;
};

/// @brief 返回一个准备就绪的 Future<T>
/// 1. 泛化版本
/// 2. void 重载版本
template <typename T>
inline Future<typename std::decay_t<T>> makeReadyFuture(T &&value) {
  Promise<typename std::decay_t<T>> promise;
  promise.setValue(std::forward<T>(value));
  return promise.getFuture();
}
inline Future<void> makeReadyFuture() {
  Promise<void> promise;
  promise.setValue();
  return promise.getFuture();
}

/// @brief 返回一个异常的 Future<T>
template <typename T, typename E>
inline Future<T> makeExceptionFuture(E &&exp) {
  Promise<T> promise;
  promise.setException(std::make_exception_ptr(std::forward<E>(exp)));
  return promise.getFuture();
}
template <typename T>
inline Future<T> makeExceptionFuture(std::exception_ptr &&eptr) {
  Promise<T> promise;
  promise.setException(std::move(eptr));
  return promise.getFuture();
}

/// ---------------- when all ----------------
/**
 * @brief when_all 语义形式 1
 * 函数参数为 n 个 future，返回值是一个 future<tuple>
 * 
 * @tparam FT: 所有 future 型别
 * @param futures: 需要等待的所有 future
 * @return CollectAllVariadicContext<
 * typename std::decay_t<FT>::InnerType...>::FutureType 
 * 返回值是一个 Future<tuple>，包含了每个 Future 的返回值
 */
template <typename... FT>
typename CollectAllVariadicContext<
    typename std::decay_t<FT>::InnerType...>::FutureType
whenAll(FT &&...futures) {
  auto ctx = std::make_shared<
      CollectAllVariadicContext<typename std::decay_t<FT>::InnerType...>>();
  // 设置每个 Future 的回调函数，在 Future 准备就绪的时候会在 context 上标记
  CollectAllVariadicHelper<CollectAllVariadicContext>(
      ctx, std::forward<typename std::decay_t<FT>>(futures)...);
  // 返回一个总的 Future，该 Future 的内部值是一个 tuple
  // tuple 包含了每个 Future 的返回值
  return ctx->promise_.getFuture();
}
/**
 * @brief when_all 语义 2
 * 函数参数为一个迭代器区间，返回值是一个 Future<std::vector>
 * 
 * @tparam InputIterator: 迭代器型别
 * @param first: 首地址
 * @param last: 尾地址
 * [first, last)
 * 
 * @return Future<std::vector<typename ResultWrapper<
 * typename std::iterator_traits<InputIterator>::value_type::InnerType>::Type>> 
 */
template <typename InputIterator>
Future<std::vector<typename ResultWrapper<
    typename std::iterator_traits<InputIterator>::value_type::InnerType>::Type>>
whenAll(InputIterator first, InputIterator last) {
  using ResultT = typename ResultWrapper<typename std::iterator_traits<
      InputIterator>::value_type::InnerType>::Type;
  // 没有需要等待的 future，直接返回一个准备就绪的 future
  if (first == last)
    return makeReadyFuture(std::vector<ResultT>());
  // 所有 future 的 context
  struct AllContext {
    AllContext(int n) : results_(n) {}
    ~AllContext() {}
    Promise<std::vector<ResultT>> promise_;
    std::vector<ResultT> results_;
    std::atomic<size_t> collected_{0};
  };
  auto ctx = std::make_shared<AllContext>(std::distance(first, last));

  // 设置每个 Future 的回调函数
  // 当 future 准备就绪的时候，将返回值保存到 context::vector 中
  for (size_t i = 0; first != last; ++first, ++i) {
    first->then([ctx, i](ResultT &&r) {
      ctx->results_[i] = std::move(r);
      // 所有 future 都已经准备就绪，set promise value
      if (ctx->results_.size() - 1 ==
          std::atomic_fetch_add(&ctx->collected_, std::size_t(1))) {
        ctx->promise_.setValue(std::move(ctx->results_));
      }
    });
  }
  return ctx->promise_.getFuture();
}

/// ---------------- when any ----------------
/**
 * @brief 等待一组 Future 中的任意一个完成
 * 返回 {i, future<i>::value}
 *
 * @tparam InputIterator
 * @param first
 * @param last
 * [first, last)
 * 
 * @return Future<std::pair<size_t, typename ResultWrapper<typename
 * std::iterator_traits<InputIterator>::value_type::InnerType>::Type>>
 * 返回值为 std::pair<>, first: 第 i 个 future, second: future 的返回值
 */
template <typename InputIterator>
Future<std::pair<size_t, typename ResultWrapper<typename std::iterator_traits<
                             InputIterator>::value_type::InnerType>::Type>>
whenAny(InputIterator first, InputIterator last) {
  using T = typename std::iterator_traits<InputIterator>::value_type::InnerType;
  using ResultT = typename ResultWrapper<T>::Type;

  if (first == last)
    return makeReadyFuture(std::pair(size_t(0), ResultT()));

  struct AnyContext {
    AnyContext(){};
    Promise<std::pair<size_t, ResultT>> promise_;
    std::atomic<bool> done_{false};
  };

  auto ctx = std::make_shared<AnyContext>();
  for (size_t i = 0; first != last; ++first, ++i) {
    first->then([ctx, i](ResultT &&r) {
      if (!ctx->done_.exchange(true))
        ctx->promise_.setValue(std::make_pair(i, std::move(r)));
    });
  }
  return ctx->promise_.getFuture();
}

/// ---------------- when N ------------------
/**
 * @brief 等待 N 个 Future 准备就绪
 * 返回一个 vector<>，vector 的每个元素都是 pair<>
 * pair<> first: 第 i 个 future, second: 第 i 个 future 的结果
 *
 * @tparam InputIterator
 * @param N
 * @param first
 * @param last
 * @return Future<std::vector<std::pair<size_t, typename ResultWrapper<typename
 * std::iterator_traits<InputIterator>::value_type::InnerType>::Type>>>
 */
template <typename InputIterator>
Future<std::vector<
    std::pair<size_t, typename ResultWrapper<typename std::iterator_traits<
                          InputIterator>::value_type::InnerType>::Type>>>
whenN(size_t N, InputIterator first, InputIterator last) {
  using T = typename std::iterator_traits<InputIterator>::value_type::InnerType;
  using ResultT = typename ResultWrapper<T>::Type;

  size_t nFutures = std::distance(first, last);
  const size_t needCollect = std::min<size_t>(nFutures, N);
  if (needCollect == 0)
    return makeReadyFuture(std::vector<std::pair<size_t, ResultT>>());

  struct NContext {
    NContext(size_t needs) : needs_(needs) {}

    Promise<std::vector<std::pair<size_t, ResultT>>> promise_;
    std::mutex mutex_;
    std::vector<std::pair<size_t, ResultT>> results_;
    const size_t needs_;
    bool done_{false};
  };
  auto ctx = std::make_shared<NContext>(needCollect);
  // 设置每个 future 的回调函数
  for (size_t i = 0; first != last; ++first, ++i) {
    first->then([ctx, i](ResultT &&r) {
      std::lock_guard<std::mutex> lk(ctx->mutex_);
      if (ctx->done_)
        return;
      // 保存 future 的返回值
      ctx->results_.push_back(std::make_pair(i, std::move(r)));
      // 如果已经有 needs_ 个 future 准备就绪，则设置 promise_
      if (ctx->needs_ == ctx->results_.size()) {
        ctx->done_ = true;
        ctx->promise_.setValue(std::move(ctx->results_));
      }
    });
  }
  return ctx->promise_.getFuture();
}

/// ---------------- when if any ------------------
/**
 * @brief 当一组 future 中有一个 future 完成并且满足 cond 的条件的时候，设置
 * promise. 该函数返回一个 future<pair<size_t, Result>>
 * first: 第 i 个 future 满足条件, second: 第 i 个 future 的返回值
 *
 * @param cond 判断条件回调函数
 * @return Future<std::pair<size_t, typename ResultWrapper<typename
 * std::iterator_traits< InputIterator>::value_type::InnerType>::Type>>
 */
template <typename InputIterator>
Future<std::pair<size_t, typename ResultWrapper<typename std::iterator_traits<
                             InputIterator>::value_type::InnerType>::Type>>
whenIfAny(InputIterator first, InputIterator last,
          std::function<bool(const Result<typename std::iterator_traits<
                                 InputIterator>::value_type::InnerType> &)>
              cond) {
  using T = typename std::iterator_traits<InputIterator>::value_type::InnerType;
  using ResultT = typename ResultWrapper<T>::Type;
  // 如果没有 future 需要等待，直接返回一个就绪的空 future
  if (first == last)
    return makeReadyFuture(std::make_pair(size_t(0), ResultT()));

  const size_t nFutures = std::distance(first, last);
  struct IfAnyContext {
    IfAnyContext() {}
    Promise<std::pair<size_t, ResultT>> promise_;
    std::atomic<size_t> returned_{0}; // 记录一共有多少个 future 完成了
    std::atomic<bool> done_{false};   // 记录是否有 future 达成条件
  };
  auto ctx = std::make_shared<IfAnyContext>();
  // 设置每个 future 的回调函数
  for (size_t i = 0; first != last; ++first, ++i) {
    first->then([ctx, i, nFutures, cond](ResultT &&r) {
      // 如果已经有 future 达成条件了，简单的增加 future 计数
      if (ctx->done_) {
        ctx->returned_.fetch_add(1);
        return;
      }
      // 该 future 的返回值没有达成条件
      if (!cond(r)) {
        // 增加 future 计数值
        const size_t returned = ctx->returned_.fetch_add(1) + 1;
        // 如果所有 future 都已经判断完毕，并且还没有达成条件，则抛出异常
        if (returned == nFutures) {
          if (!ctx->done_.exchange(true)) {
            try {
              throw std::runtime_error(
                  "when if any failed, no true condition.");
            } catch (...) {
              ctx->promise_.setException(std::current_exception());
            }
          }
        }
        return;
      }
      // 条件达成，设置 promise_，然后增加计数值
      if (!ctx->done_.exchange(true))
        ctx->promise_.setValue(std::make_pair(i, std::move(r)));
      ctx->returned_.fetch_add(1);
    });
  }
  return ctx->promise_.getFuture();
}

/// ---------------- when if N ------------------
/**
 * @brief 当一组 future 有 N 个就绪并且满足 cond 条件的时候，设置 promise.
 * 该函数返回 std::vector<std::pair<size_t, Result>>
 * first: 第 i 个就绪的满足 cond 的 future，second: 第 i 个 future 的值
 *
 * @param cond 判断函数
 * @return Future<std::vector<
 * std::pair<size_t, typename ResultWrapper<typename std::iterator_traits<
 * InputIterator>::value_type::InnerType>::Type>>>
 */
template <typename InputIterator>
Future<std::vector<
    std::pair<size_t, typename ResultWrapper<typename std::iterator_traits<
                          InputIterator>::value_type::InnerType>::Type>>>
whenIfN(size_t N, InputIterator first, InputIterator last,
        std::function<bool(const Result<typename std::iterator_traits<
                               InputIterator>::value_type::InnerType> &)>
            cond) {
  using T = typename std::iterator_traits<InputIterator>::value_type::InnerType;
  using ResultT = typename ResultWrapper<T>::Type;

  size_t nFutures = std::distance(first, last);
  const size_t needCollect = std::min<size_t>(nFutures, N);
  if (needCollect == 0)
    return makeReadyFuture(std::vector<std::pair<size_t, ResultT>>());

  struct IfNContext {
    IfNContext(size_t needs) : needs_(needs) {}

    Promise<std::vector<std::pair<size_t, ResultT>>> promise_;
    std::mutex mutex_;
    std::vector<std::pair<size_t, ResultT>> results_;
    size_t returned_{0};
    const size_t needs_;
    bool done_{false};
  };

  auto ctx = std::make_shared<IfNContext>(needCollect);
  // 设置每个 future 就绪的回调函数
  for (size_t i = 0; first != last; ++first, ++i) {
    first->then([ctx, i, nFutures, cond](ResultT &&r) {
      std::lock_guard<std::mutex> lk(ctx->mutex_);
      ++ctx->returned_; // 增加就绪 future 的计数
      // 已经有 N 个 future 满足条件了，直接返回
      if (ctx->done_)
        return;
      // 如果当前就绪的 future 不满足条件，并且所有 future 都执行完毕
      // 则抛出异常
      if (!cond(r)) {
        if (ctx->returned_ == nFutures) {
          try {
            throw std::runtime_error(
                "when if N failed, not enough true condition.");
          } catch (...) {
            ctx->done_ = true;
            ctx->promise_.setException(std::current_exception());
          }
        }
        return;
      }
      // 当前 future 满足条件，将其结果放置到 vector 中
      // 如果有 N 个 future 满足条件了，则设置 promise_
      ctx->results_.push_back(std::make_pair(i, std::move(r)));
      if (ctx->needs_ == ctx->results_.size()) {
        ctx->done_ = true;
        ctx->setValue(std::move(ctx->results_));
      }
    });
  }
  return ctx->promise_.getFuture();
}

} // namespace lrpc

#endif