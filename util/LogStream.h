#ifndef IMITATE_MUDUO_BASE_LOGSTREAM_H
#define IMITATE_MUDUO_BASE_LOGSTREAM_H

#include <assert.h>
#include <cstring>
#include <string>
#include <thread>

namespace lrpc {
namespace util {

using std::string;

namespace detail {

const int kSmallBuffer = 4000;
const int kLargeBuffer = 4000 * 1000;

template <int SIZE> class FixedBuffer {
public:
  FixedBuffer(const FixedBuffer &rhs) = delete;
  FixedBuffer &operator=(const FixedBuffer &rhs) = delete;
  FixedBuffer() : cur_(data_) { setCookie(cookieStart); }
  ~FixedBuffer() { setCookie(cookieEnd); }

  // 缓冲区追加新的数据
  void append(const char *buf, int len) {
    if (avail() > len) {
      memcpy(cur_, buf, len);
      cur_ += len;
    }
  }

  const char *data() const { return data_; } // 获取缓冲区
  int length() const { return cur_ - data_; } // 获取当前已缓冲字符大小

  char *current() { return cur_; } // 返回当前缓冲区指针
  // 返回缓冲区剩余可用大小
  int avail() const { return static_cast<int>(end() - cur_); }
  void add(size_t len) { cur_ += len; } // 移动当前缓冲区指针

  void reset() { cur_ = data_; }                 // 重置缓冲区
  void bzero() { ::bzero(data_, sizeof data_); } // 初始化缓冲区所有字符为 0

  // GDB 使用
  const char *debugString();
  void setCookie(void (*cookie)()) { cookie_ = cookie; }
  // 单元测试的时候用
  string asString() const { return string(data_, length()); }

private:
  // 返回缓冲区的结束地址
  const char *end() const { return data_ + sizeof data_; }
  static void cookieStart();
  static void cookieEnd();

  void (*cookie_)();
  char data_[SIZE]; // 缓冲区
  char *cur_;       // 当前缓冲区指针
};
} // namespace detail

class T {
public:
  T(const char *str, int len) : str_(str), len_(len) {
    assert(strlen(str) == len_);
  }

  const char *str_;
  const size_t len_;
};

/// @brief 日志流输出类
class LogStream {
  using self = LogStream;

public:
  LogStream(const LogStream &) = delete;
  LogStream &operator=(const LogStream &) = delete;
  LogStream() = default;
  using Buffer = detail::FixedBuffer<detail::kSmallBuffer>;

  self &operator<<(bool v) {
    buffer_.append(v ? "1" : "0", 1);
    return *this;
  }

  // 常规类型的输出
  self &operator<<(short);
  self &operator<<(unsigned short);
  self &operator<<(int);
  self &operator<<(unsigned int);
  self &operator<<(long);
  self &operator<<(unsigned long);
  self &operator<<(long long);
  self &operator<<(unsigned long long);
  self &operator<<(std::thread::id);

  // 指针类型输出
  self &operator<<(const void *);

  // 浮点数类型输出
  self &operator<<(float v) {
    *this << static_cast<double>(v);
    return *this;
  }
  self &operator<<(double);

  // 字符类型输出
  self &operator<<(char v) {
    buffer_.append(&v, 1);
    return *this;
  }

  // C 字符串类型输出
  self &operator<<(const char *v) {
    buffer_.append(v, strlen(v));
    return *this;
  }

  self &operator<<(const T &v) {
    buffer_.append(v.str_, v.len_);
    return *this;
  }

  // 字符串类型输出
  self &operator<<(const string &v) {
    buffer_.append(v.c_str(), v.size());
    return *this;
  }

  // 追加数据
  void append(const char *data, int len) { buffer_.append(data, len); }
  const Buffer &buffer() const { return buffer_; } // 返回缓冲区
  void resetBuffer() { buffer_.reset(); }          // 重置缓冲区

private:
  void staticCheck();
  template <typename T> void formatInteger(T);

  Buffer buffer_;
  static const int kMaxNumericSize = 32; // 数字类型格式化字符串的长度
};

/// @brief 用于保存整型变量的格式化输出日志字符串
class Fmt {
public:
  template <typename T> Fmt(const char *fmt, T val);

  const char *data() const { return buf_; }
  int length() const { return length_; }

private:
  char buf_[32];
  int length_;
};

/// @brief 日志流类，直接输出格式化 class Fmt
inline LogStream &operator<<(LogStream &s, const Fmt &fmt) {
  s.append(fmt.data(), fmt.length());
  return s;
}

} // namespace util
} // namespace lrpc

#endif