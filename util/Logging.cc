#include "Logging.h"
#include "Timestamp.h"
#include <iostream>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <thread>

namespace lrpc {
namespace util {

/**
 * @brief 整个 Logger 完成日志输出的过程
 * LOG_WARN << "Channel::handle_event() POLLNVAL";
 * 1. LOG_WARN 宏构造了 Logger 临时对象，在构造 Logger 临时对象的时候首先构造了内部的嵌套对象 Impl
 * 2. 在构造 Impl 的时候会首先执行 timeFormat 格式化并输出当前时间，紧接着 Impl 类的构造函数会输出当前线程的 id，最后在 Impl 构造函数结束的时候如果有错误码就输出错误码对应的描述
 * 3. 然后回到 Logger 的构造函数，因为 TRACE、DEBUG 宏都会携带 __func__ 参数，因此在这一类构函数中还会首先输出函数的名字
 * 4. 紧接着 LOG_WARN 会返回 Logger::Impl::LogStream 对象，LogStream 对象重载了 operator<< 操作符，并且其内部的 Buffer 用来保存用户传入的日志内容
 * 5. 最后在该条语句结束的时候 Logger 会析构，首先会执行 Impl::finish() 输出设备文件名字，basename_ 成员变量
 * 6. 紧接着会通过 g_output 函数将 Buffer 中的数据输出到设备文件中
 * 7. 完成析构
 */

thread_local char t_errnobuf[512]; // 用来保存 errno
thread_local char t_time[32];      // 保存格式化的时间字符串
thread_local time_t t_lastSecond;  // 保存当前时间所在的秒

/// @brief 保存 errno
const char *strerror_tl(int saveErrno) {
  // strerror_r 返回一个描述错误代码的字符串，并保存到 t_errnobuf 中
  return strerror_r(saveErrno, t_errnobuf, sizeof t_errnobuf);
}

/// @brief 根据环境变量初始化日志等级
Logger::LogLevel initLogLevel() {
  if (::getenv("IMITATE_MUDUO_LOG_TRACE"))
    return Logger::TRACE;
  else
    return Logger::DEBUG;
}
Logger::LogLevel g_logLevel = initLogLevel();

/// @brief 根据 enum 返回对应的日志等级描述
const char *LogLevelName[Logger::NUM_LOG_LEVELS] = {
    "TRACE ", "DEBUG ", "INFO  ", "WARN  ", "ERROR ", "FATAL ",
};

void defaultOutput(const char *msg, int len) {
  // fwrite 将 msg 重新解释为一个个大小为 1 字节的 object，写入到 stdout 中
  fwrite(msg, 1, len, stdout);
}

void defaultFlush() {
  // 刷新 stdout 缓冲区中的数据到关联的 device 中
  // fflush 的参数如果是
  // null，则会刷新所有打开的输出流，包括在库中操作的流或程序无法直接访问的流
  fflush(stdout);
}

Logger::OutputFunc g_output = defaultOutput;
Logger::FlushFunc g_flush = defaultFlush;

} // namespace util
} // namespace lrpc

using namespace lrpc::util;

/// @brief Impl 会先于 Logger 构造，formatTime 会最先输出
Logger::Impl::Impl(LogLevel level, int savedErrno, const char *file, int line)
    : time_(Timestamp::now()), stream_(), level_(level), line_(line),
      fullname_(file), basename_(nullptr) {
  // 返回路径中的最后一级名字，返回结果包含 '/' 开头
  const char *path_sep_pos = strrchr(fullname_, '/');
  // 如果只有一级路径，那么直接返回 fullname_，否则使用 path_sep_pos
  basename_ = (path_sep_pos != nullptr) ? path_sep_pos + 1 : fullname_;
  formatTime(); // 格式化并输出当前时间
  // Fmt tid("%5d ", std::this_thread::get_id());  // TODO
  // assert(tid.length() == 6);
  if (savedErrno != 0)  // 说明有错误，那就输出错误描述
    stream_ << strerror_tl(savedErrno) << " (errno=" << savedErrno << ") ";
}

/// @brief 格式化时间字符串，输出到日志中
void Logger::Impl::formatTime() {
  int64_t microSecondsSinceEpoch = time_.microSecondsSinceEpoch();
  time_t seconds = static_cast<time_t>(microSecondsSinceEpoch / 1000000);
  int microseconds = static_cast<int>(microSecondsSinceEpoch % 1000000);
  if (seconds != t_lastSecond) {
    t_lastSecond = seconds;
    struct tm tm_time;
    // gmtime_r 将日历时间转换为 UTC 时间，并保存到 tm_time 中
    ::gmtime_r(&seconds, &tm_time);
    // 格式化时间字符串
    int len =
        snprintf(t_time, sizeof(t_time), "%4d%02d%02d %02d:%02d:%02d",
                 tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
                 tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
    assert(len == 17);
  }
  Fmt us(".%06dZ ", microseconds);
  // std::cout << us.length() << ", microSecondsSinceEpoch = " << microSecondsSinceEpoch << ", fmt.data(): " << us.data() << "\n";
  assert(us.length() == 9);
  // 输出时间，剩余的毫秒时间也一起输出
  stream_ << T(t_time, 17) << T(us.data(), 9);
}

void Logger::Impl::finish() {
  stream_ << " - " << basename_ << ":" << line_ << "\n";
}

/// @brief 默认的日志等级是 INFO
Logger::Logger(const char *file, int line) : impl_(INFO, 0, file, line) {}
Logger::Logger(const char *file, int line, LogLevel level, const char *func)
    : impl_(level, 0, file, line) {
  impl_.stream_ << func << " ";
}
Logger::Logger(const char *file, int line, LogLevel level)
    : impl_(level, 0, file, line) {}
Logger::Logger(const char *file, int line, bool toAbort)
    : impl_(toAbort ? FATAL : ERROR, errno, file, line) {}

/// @brief 析构的时候将 Buffer 中的内容实用 g_output 输出，用户传入的所有输出日志是在析构的时候输出的
Logger::~Logger() {
  impl_.finish();
  const LogStream::Buffer &buf(stream().buffer());
  g_output(buf.data(), buf.length());
  if (impl_.level_ == FATAL) {
    g_flush();
    abort();
  }
}

/// @brief 返回日志等级
Logger::LogLevel Logger::logLevel() { return g_logLevel; }
/// @brief 设置日志等级
void Logger::setLogLevel(Logger::LogLevel level) { g_logLevel = level; }
/// @brief 设置 Buffer 输出函数
void Logger::setOutput(OutputFunc out) { g_output = out; }
/// @brief 设置设备缓冲区刷新函数
void Logger::setFlush(FlushFunc flush) { g_flush = flush; }