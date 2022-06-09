#ifndef IMITATE_MUDUO_BASE_LOGGING_H
#define IMITATE_MUDUO_BASE_LOGGING_H

#include "LogStream.h"
#include "Timestamp.h"
#include <memory>

namespace lrpc {
namespace util {

/// @brief 用户使用的日志类
class Logger {
public:
  // 日志输出等级
  enum LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
    NUM_LOG_LEVELS,
  };

  Logger(const char *file, int line);
  Logger(const char *file, int line, LogLevel level);
  Logger(const char *file, int line, LogLevel level, const char *func);
  Logger(const char *file, int line, bool toAbort);
  ~Logger();

  LogStream &stream() { return impl_.stream_; }

  static LogLevel logLevel();
  static void setLogLevel(LogLevel level);

  using OutputFunc = void (*)(const char *msg, int len);
  using FlushFunc = void (*)();
  static void setOutput(OutputFunc);
  static void setFlush(FlushFunc);

private:
  class Impl {
  public:
    using LogLevel = Logger::LogLevel;
    Impl(LogLevel level, int old_errno, const char *file, int line);
    void formatTime();
    void finish();

    Timestamp time_;
    LogStream stream_;
    LogLevel level_;
    int line_;
    const char *fullname_;
    const char *basename_;
  };
  Impl impl_;
};

/// @brief 下面的宏根据日志等级，构造一个临时的日志输出类 Logger
#define LOG_TRACE                                                              \
  if (lrpc::util::Logger::logLevel() <= lrpc::util::Logger::TRACE)               \
  lrpc::util::Logger(__FILE__, __LINE__, lrpc::util::Logger::TRACE, __func__)    \
      .stream()
#define LOG_DEBUG                                                              \
  if (lrpc::util::Logger::logLevel() <= lrpc::util::Logger::DEBUG)               \
  lrpc::util::Logger(__FILE__, __LINE__, lrpc::util::Logger::DEBUG, __func__)    \
      .stream()
#define LOG_INFO                                                               \
  if (lrpc::util::Logger::logLevel() <= lrpc::util::Logger::INFO)                \
  lrpc::util::Logger(__FILE__, __LINE__).stream()
#define LOG_WARN                                                               \
  lrpc::util::Logger(__FILE__, __LINE__, lrpc::util::Logger::WARN).stream()
#define LOG_ERROR                                                              \
  lrpc::util::Logger(__FILE__, __LINE__, lrpc::util::Logger::ERROR).stream()
#define LOG_FATAL                                                              \
  lrpc::util::Logger(__FILE__, __LINE__, lrpc::util::Logger::FATAL).stream()
#define LOG_STSERR lrpc::util::Logger(__FILE__, __LINE__, false).stream()
#define LOG_STSFATAL lrpc::util::Logger(__FILE__, __LINE__, true).stream()

const char *strerror_tl(int saveErrno);

/// @brief 检查指针是不是 nullptr，如果是 nullptr 就输出到日志中
template <typename T>
T *CheckNotNULL(const char *file, int line, const char *names, T *ptr) {
  if (ptr == nullptr)
    Logger(file, line, Logger::FATAL).stream() << names;
  return ptr;
}
#define CHECK_NOTNULL(val)                                                     \
  ::lrpc::util::CheckNotNULL(__FILE__, __LINE__, "'" #val "' Must be non NULL", \
                            (val))

/// @brief 将类型 From 转换为类型 To
template <typename To, typename From> inline To implicit_cast(From const &f) {
  return f;
}

} // namespace util
} // namespace lrpc

#endif