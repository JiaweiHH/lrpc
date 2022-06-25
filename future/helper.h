#ifndef LRPC_HELPER_H
#define LRPC_HELPER_H

#include <memory>
#include <mutex>
#include <tuple>
#include <vector>

namespace lrpc {

template <typename T> class Future;
template <typename T> class Promise;
template <typename T> class Result;
template <typename T> class ResultWrapper;

/// @brief F(Args...) 返回值别名
template <typename F, typename... Args>
using ResultOf = decltype(std::declval<F>()(std::declval<Args>()...));
/// @brief 包装返回值
template <typename F, typename... Args> struct ResultOfWrapper {
  using Type = ResultOf<F, Args...>;
};

template <typename F, typename... Args> struct CanCallWith {
  /// @brief 检查 F 是否可以接受 Args... 参数
  template <typename T, typename = ResultOf<T, Args...>>
  static constexpr std::true_type check(std::nullptr_t dummy) {
    return std::true_type{};
  }
  template <typename T> static constexpr std::false_type check(...) {
    return std::false_type{};
  }
  // 如果可以调用 F(Args) type::value = true
  typedef decltype(check<F>(nullptr)) type;
  static constexpr bool value = type::value;
};
/// 定义 CanCallWith 中的静态变量
template <typename F, typename... Args>
constexpr bool CanCallWith<F, Args...>::value;

/**
 * @brief 判断模版参数 T 是不是 Future 类型，如果是的话获取 Future<T> 的模版形参
 * 1. 泛化模版，T 匹配任意类型的. Inner -> T
 * 2. 特化模版，T = Future<> 优先匹配该模版. Inner -> Future<> 的具体类型
 *
 * @tparam T
 */
template <typename T> struct isFuture : std::false_type { using Inner = T; };
template <typename T> struct isFuture<Future<T>> : std::true_type {
  using Inner = T;
};

/// @brief 该模版主要用来对 F(T) 的执行结果进行 Future 封装
/// 如果 F 可以直接无参数执行则无参数，否则保存 F(T) 的结果型别
/// 这种没有参数的情景
template <typename F, typename T> struct CallableResult {
  /// @brief 获取 F(T) 的返回值
  /// 1. if can call with F(void) -> result of F()
  /// 2. else, if can call with F(T&&) -> result of F(T&&)
  /// 3. else -> result of F(T&)
  typedef typename std::conditional<
      CanCallWith<F>::value, ResultOfWrapper<F>,
      typename std::conditional<CanCallWith<F, T &&>::value,
                                ResultOfWrapper<F, T &&>,
                                ResultOfWrapper<F, T &>>::type>::type Res;
  /// @brief 获取 Res 的内部类型（不管 Res 是否是 Future 包装）
  typedef isFuture<typename Res::Type> isReturnsFuture;
  /// @brief returnFutureType 表示的一定是一个 Future<T> 类型
  typedef Future<typename isReturnsFuture::Inner> returnFutureType;
};

template <typename F> struct CanCallWith<F, void> {
  typedef typename std::conditional<
      CanCallWith<F>::value, ResultOfWrapper<F>,
      typename std::conditional<CanCallWith<F, Result<void> &&>::value,
                                ResultOfWrapper<F, Result<void> &&>,
                                ResultOfWrapper<F, Result<void> &>>::type>::type
      Res;
  typedef isFuture<typename Res::Type> isReturnsFuture;
  typedef Future<typename isReturnsFuture::Inner> returnFutureType;
};

/**
 * @brief when_all 会使用到
 *
 * @tparam ELEM : n 个 Future<> 型别
 */
template <typename... ELEM> struct CollectAllVariadicContext {
  CollectAllVariadicContext() {}
  ~CollectAllVariadicContext() {}
  /// @brief 禁止拷贝
  CollectAllVariadicContext(const CollectAllVariadicContext &) = delete;
  void operator=(const CollectAllVariadicContext &) = delete;

  /// @brief 设置第 I 个 future 准备就绪
  template <typename T, size_t I>
  inline void setPartialResult(typename ResultWrapper<T>::Type &&r) {
    std::lock_guard<std::mutex> lk(mutex_);
    std::get<I>(results_) = std::move(r);
    collects_.push_back(I);
    // 如果 collects_ 的大小等于 results_ 中元素的数量，则所有 future 都准备好了
    // 设置 promise 准备就绪
    if (collects_.size() == std::tuple_size<decltype(results_)>::value) {
      promise_.setValue(std::move(results_));
    }
  }

// 多个不同的需要等待的 future 的执行结果，Result<> 类型
#define _RESULTELEM_ typename ResultWrapper<ELEM>::Type...

  std::mutex mutex_;                 // 保护 results_ 和 collects_
  std::tuple<_RESULTELEM_> results_; // 已经准备就绪 future 的 Result<>
  std::vector<size_t> collects_;     // 记录已经完成的 Future
  Promise<std::tuple<_RESULTELEM_>> promise_;
  // 返回的新 Future 的型别，新 Future 包含一个 tuple 类型
  // tuple 内部是多个不同的需要等待的 Future<>
  typedef Future<std::tuple<_RESULTELEM_>> FutureType;

#undef _RESULTELEM_
};

/// @brief 模版递归展开，类似 std::tuple 的展开一样
/// 递归设置每个 Future 的回调函数，回调函数负责在 context 标记该 Future 完成
/// 递归中止模版
template <template <typename...> class CTX, typename... Rs>
void CollectAllVariadicHelper(const std::shared_ptr<CTX<Rs...>> &) {}
/// 泛化模版. RHead: first future, RTail... : other future
template <template <typename...> class CTX, typename... Rs, typename RHead,
          typename... RTail>
void CollectAllVariadicHelper(const std::shared_ptr<CTX<Rs...>> &ctx,
                              RHead &&head, RTail &&...tail) {
  using InnerResult = typename ResultWrapper<typename RHead::InnerType>::Type;
  // 当第一个 future 准备就绪的时候，设置回调函数
  // 该回调函数负责设置 context 的该 future 准备就绪标记
  head.Then([ctx](InnerResult &&r) {
    ctx->template setPartialResult<InnerResult,
                                   sizeof...(Rs) - sizeof...(RTail) - 1>(
        std::move(r));
  });
  CollectAllVariadicHelper(ctx, std::forward<RTail>(tail)...);
}

} // namespace lrpc

#endif