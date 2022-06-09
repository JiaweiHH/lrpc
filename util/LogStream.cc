#include "LogStream.h"

#include <algorithm>
#include <assert.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <thread>
#include <type_traits>

using namespace lrpc::util;
using namespace lrpc::util::detail;

namespace lrpc {
namespace util {

namespace detail {

const char digits[] = "9876543210123456789";
const char *zero = digits + 9;
static_assert(sizeof(digits) == 20);

const char digitsHex[] = "0123456789abcdef";
static_assert(sizeof digitsHex == 17);

/// @brief 将整型的数据转换为字符串
template <typename T> size_t convert(char buf[], T value) {
  T i = value;
  char *p = buf;
  // 转换为字符串
  do {
    int lsd = i % 10;
    i /= 10;
    *p++ = zero[lsd];
  } while (i != 0);
  // 如果是负数，则添加一个负号
  if (value < 0) {
    *p++ = '-';
  }
  *p = '\0';            // 末尾添加 '\0'
  std::reverse(buf, p); // buf 此时是倒序的，转换为正序

  // 返回转换后的字符串长度
  return p - buf;
}

/// https://stackoverflow.com/questions/1845482/what-is-uintptr-t-data-type
/// @brief 保存指针的地址，以十六进制的方式保存
size_t convertHex(char buf[], uintptr_t value) {
  uintptr_t i = value;
  char *p = buf;

  do {
    int lsd = i % 16;
    i /= 16;
    *p++ = digitsHex[lsd];
  } while (i != 0);
  *p = '\0';
  std::reverse(buf, p);

  return p - buf;
}

template <int N> class Convert {
public:
  template <typename T> size_t operator()(char buf[], T value) {
    static_assert(std::is_integral_v<T>);
    static_assert(N > 1);
    T i = value;
    char *p = buf;
    do {
      int lsd = i % N;
      i /= N;
      *p++ = zero[lsd];
    } while (i != 0);
    if (value < 0)
      *p++ = '-';
    *p = '\0';
    std::reverse(buf, p);
    return p = buf;
  }
};

} // namespace detail

} // namespace util
} // namespace lrpc

/// @brief 在 Debug 的时候用到
template <int SIZE> const char *FixedBuffer<SIZE>::debugString() {
  *cur_ = '\0';
  return data_;
}

template <int SIZE> void FixedBuffer<SIZE>::cookieStart() {}
template <int SIZE> void FixedBuffer<SIZE>::cookieEnd() {}

/// @brief 显示实例化定义，编译器直接实例化 FixedBuffer<kSmallBuffer> 和
/// FixedBuffer<kLargeBuffer> 类
template class FixedBuffer<kSmallBuffer>;
template class FixedBuffer<kLargeBuffer>;

void LogStream::staticCheck() {
  // numeric_limits<>::digits10
  // 表示模版形参类型的变量可以在不改变的情况下表示的小数位数
  static_assert(kMaxNumericSize - 10 > std::numeric_limits<double>::digits10);
  static_assert(kMaxNumericSize - 10 >
                std::numeric_limits<long double>::digits10);
  static_assert(kMaxNumericSize - 10 > std::numeric_limits<long>::digits10);
  static_assert(kMaxNumericSize - 10 >
                std::numeric_limits<long long>::digits10);
}

/// @brief 将整型变量转化为字符串，然后添加到缓冲区中
template <typename T> void LogStream::formatInteger(T v) {
  if (buffer_.avail() >= kMaxNumericSize) {
    size_t len = convert(buffer_.current(), v);
    buffer_.add(len);
  }
}

LogStream &LogStream::operator<<(short v) {
  *this << static_cast<int>(v);
  return *this;
}
LogStream &LogStream::operator<<(unsigned short v) {
  *this << static_cast<unsigned int>(v);
  return *this;
}
LogStream &LogStream::operator<<(int v) {
  formatInteger(v);
  return *this;
}
LogStream &LogStream::operator<<(unsigned int v) {
  formatInteger(v);
  return *this;
}
LogStream &LogStream::operator<<(long v) {
  formatInteger(v);
  return *this;
}
LogStream &LogStream::operator<<(unsigned long v) {
  formatInteger(v);
  return *this;
}
LogStream &LogStream::operator<<(long long v) {
  formatInteger(v);
  return *this;
}
LogStream &LogStream::operator<<(unsigned long long v) {
  formatInteger(v);
  return *this;
}

/// @brief 将指针的地址存入日志缓冲区
LogStream &LogStream::operator<<(const void *p) {
  uintptr_t v = reinterpret_cast<uintptr_t>(p);
  if (buffer_.avail() >= kMaxNumericSize) {
    char *buf = buffer_.current();
    buf[0] = '0';
    buf[1] = 'x';
    size_t len = convertHex(buf + 2, v);
    buffer_.add(len + 2);
  }
  return *this;
}

LogStream &LogStream::operator<<(std::thread::id tid) {
  std::ostringstream oss;
  oss << tid;
  *this << oss.str();
  return *this;
}

LogStream &LogStream::operator<<(double v) {
  if (buffer_.avail() >= kMaxNumericSize) {
    int len = snprintf(buffer_.current(), kMaxNumericSize, "%.12g", v);
    buffer_.add(len);
  }
  return *this;
}

template <typename T> Fmt::Fmt(const char *fmt, T val) {
  // 检查型别 T 是不是算术类型（整型或者浮点型）
  static_assert(std::is_arithmetic<T>::value == true);
  length_ = snprintf(buf_, sizeof buf_, fmt, val);
  // 检查是否溢出了，buf_ 只有 32 字节
  assert(static_cast<size_t>(length_) < sizeof buf_);
}

/// @brief 显示实例化
template Fmt::Fmt(const char *fmt, char);
template Fmt::Fmt(const char *fmt, short);
template Fmt::Fmt(const char *fmt, unsigned short);
template Fmt::Fmt(const char *fmt, int);
template Fmt::Fmt(const char *fmt, unsigned int);
template Fmt::Fmt(const char *fmt, long);
template Fmt::Fmt(const char *fmt, unsigned long);
template Fmt::Fmt(const char *fmt, long long);
template Fmt::Fmt(const char *fmt, unsigned long long);
template Fmt::Fmt(const char *fmt, float);
template Fmt::Fmt(const char *fmt, double);