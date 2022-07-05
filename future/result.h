#ifndef LRPC_RESULT_H
#define LRPC_RESULT_H

#include "result.h"
#include <assert.h>
#include <cstdint>
#include <exception>

namespace lrpc {

template <typename T> class Result {
public:
  enum class State {
    None,
    Exception,
    Value,
  };

  /// @brief 普通构造函数
  Result() : state_(State::None) {}
  Result(const T &val) : state_(State::Value), value_(val) {}
  Result(T &&val) : state_(State::Value), value_(std::move(val)) {}
  Result(std::exception_ptr e)
      : state_(State::Exception), exception_(std::move(e)) {}
  /// @brief 移动构造函数
  Result(Result<T> &&r) : state_(r.state_) {
    if (state_ == State::Value)
      new (&value_) T(std::move(r.value_));
    else if (state_ == State::Exception)
      new (&exception_) std::exception_ptr(std::move(r.exception_));
  }
  /// @brief 拷贝构造函数
  Result(const Result<T> &r) : state_(r.state_) {
    if (state_ == State::Value)
      new (&value_) T(r.value_);
    else if (state_ == State::Exception)
      new (&exception_) std::exception_ptr(r.exception_);
  }

  ~Result() {
    if (state_ == State::Exception)
      exception_.~exception_ptr();
    else if (state_ == State::Value)
      value_.~T();
  }

  /// @brief 移动赋值
  Result<T> &operator=(Result<T> &&r) {
    if (this == &r)
      return *this;
    // 首先对自身进行析构
    this->~Result();
    // 根据 Result 的 State 进行值移动
    state_ = r.state_;
    if (state_ == State::Value)
      new (&value_) T(std::move(r.value_));
    else if (state_ == State::Exception)
      new (&exception_) std::exception_ptr(std::move(r.exception_));
    return *this;
  }
  /// @brief 拷贝赋值
  Result<T> &operator=(const Result<T> &r) {
    // 避免自赋值
    if (this == &r)
      return *this;
    this->~Result();

    state_ = r.state_;
    if (state_ == State::Value)
      new (&value_) T(r.value_);
    else if (state_ == State::Exception)
      new (&exception_) std::exception_ptr(r.exception_);
    return *this;
  }

  /// @brief 隐式类型转换
  operator const T &() const & { return getValue(); }
  operator T &() & { return getValue(); }
  operator T &&() && { return std::move(getValue()); }

  /// @brief 获取 value_
  const T &getValue() const & {
    check();
    return value_;
  }
  T &getValue() & {
    check();
    return value_;
  }
  T &&getValue() const && {
    check();
    return std::move(value_);
  }

  /// @brief 获取 exception_
  const std::exception_ptr &getException() const & {
    if (!hasException())
      throw std::runtime_error("Not exception state");
    return exception_;
  }
  std::exception_ptr &getException() & {
    if (!hasException())
      throw std::runtime_error("Not exception state");
    return exception_;
  }
  std::exception_ptr &&getException() && {
    if (!hasException())
      throw std::runtime_error("Not exception state");
    return std::move(exception_);
  }

  bool hasValue() const { return state_ == State::Value; }
  bool hasException() const { return state_ == State::Exception; }

  /// @brief 检查 state_ 状态，如果保存了异常则再次抛出，以便外面可以捕获
  struct UninitializedResult {};
  void check() const {
    if (state_ == State::Exception)
      std::rethrow_exception(exception_);
    else if (state_ == State::None)
      throw UninitializedResult();
  }
  template <typename R> R get() { return std::forward<R>(getValue()); }

private:
  State state_;
  union {
    T value_;
    std::exception_ptr exception_;
  };
};

/**
 * @brief void 模版特化
 *
 * @tparam
 */
template <> class Result<void> {
  enum class State {
    Exception,
    Value,
  };

public:
  /// @brief 构造
  Result() : state_(State::Value) {}
  explicit Result(std::exception_ptr e)
      : state_(State::Exception), exception_(std::move(e)) {}
  Result(Result<void> &&r) : state_(r.state_) {
    if (state_ == State::Exception)
      new (&exception_) std::exception_ptr(std::move(r.exception_));
  }
  Result(const Result<void> &r) : state_(r.state_) {
    if (state_ == State::Exception)
      new (&exception_) std::exception_ptr(r.exception_);
  }

  /// @brief 析构
  ~Result() {
    if (state_ == State::Exception)
      exception_.~exception_ptr();
  }

  /// @brief operator=
  Result<void> &operator=(Result<void> &&r) {
    if (this == &r)
      return *this;
    this->~Result();
    state_ = r.state_;
    if (state_ == State::Exception)
      new (&exception_) std::exception_ptr(std::move(r.exception_));
    return *this;
  }
  Result<void> &operator=(const Result<void> &r) {
    if (this == &r)
      return *this;
    this->~Result();
    state_ = r.state_;
    if (state_ == State::Exception)
      new (&exception_) std::exception_ptr(r.exception_);
    return *this;
  }

  const std::exception_ptr &getException() const & {
    if (!hasException())
      throw std::runtime_error("Not exception state");
    return exception_;
  }
  std::exception_ptr &getException() & {
    if (!hasException())
      throw std::runtime_error("Not exception state");
    return exception_;
  }
  std::exception_ptr &&getException() && {
    if (!hasException())
      throw std::runtime_error("Not exception state");
    return std::move(exception_);
  }

  bool hasValue() const { return state_ == State::Value; }
  bool hasException() const { return state_ == State::Exception; }

  void check() const {
    if (state_ == State::Exception)
      std::rethrow_exception(exception_);
  }
  template <typename R> R get() { return std::forward<R>(*this); }

private:
  State state_;
  std::exception_ptr exception_;
};

/**
 * @brief 如果 ResultWrapper 模版形参是 Result type 则 Type = Result<T>
 * 如果 ResultWrapper 模版形参是 Result<T> 则 Type = Result<T>
 *
 * 1. 主模版
 * 2. 特化模版: 模版形参是 Result<T>
 *
 * @tparam T
 */
template <typename T> struct ResultWrapper { using Type = Result<T>; };
template <typename T> struct ResultWrapper<Result<T>> {
  using Type = Result<T>;
};

/// @brief 包装 F(Args...) 返回值
/// 1. 如果返回值是某个单类型 T 则用 Result<> 包装
/// 2. 如果返回值就是 Result<> 类型，则不包装
template <typename F, typename... Args>
typename std::enable_if<
    !std::is_same<typename std::result_of<F(Args...)>::type, void>::value,
    typename ResultWrapper<typename std::result_of<F(Args...)>::type>::Type>::
    type
    WrapWithResult(F &&f, Args &&...args) {
  using Type = typename std::result_of<F(Args...)>::type;
  // 执行函数 f(args...) 并返回 Result<T> 类型的返回值
  try {
    return typename ResultWrapper<Type>::Type(
        std::forward<F>(f)(std::forward<Args>(args)...));
  } catch (std::exception &e) {
    return typename ResultWrapper<Type>::Type(std::current_exception());
  }
}
/// @brief 同上，包装返回值为 Result<void>
template <typename F, typename... Args>
typename std::enable_if<
    std::is_same<typename std::result_of<F(Args...)>::type, void>::value,
    Result<void>>::type
WrapWithResult(F &&f, Args &&...args) {
  // 构造返回值
  try {
    // 隐式类型转换，Result<T> -> T
    std::forward<F>(f)(std::forward<Args>(args)...);
    return Result<void>();
  } catch (std::exception &e) {
    return Result<void>(std::current_exception());
  }
}

} // namespace lrpc

#endif