#ifndef LRPC_LOGFILE_H
#define LRPC_LOGFILE_H

#include <memory>
#include <mutex>
#include <string>

namespace lrpc {
namespace util {

using std::string;

class LogFile {
public:
  LogFile(const LogFile &rhs) = delete;
  LogFile &operator=(const LogFile &rhs) = delete;
  LogFile(const string &basename, size_t rollSize, bool threadSafe = false,
          int flushInterval = 3);
  ~LogFile();
  void append(const char *logline, int len);
  void flush();

private:
  void append_unlocked(const char *logline, int line);
  static string getLogFileName(const string &basename, time_t *now);
  void rollFile();

  const string basename_;
  const size_t rollSize_;
  const int flushInterval_;
  int count_;
  std::unique_ptr<std::mutex> mutex_;
  time_t startOfPeriod_;
  time_t lastRoll_;
  time_t lastFlush_;
  class File;
  std::unique_ptr<File> file_;
  const static int kCheckTimeRoll_ = 1024;
  const static int kRollPerSeconds_ = 60 * 60 * 24;
};

} // namespace util
} // namespace lrpc

#endif