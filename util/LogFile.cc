#include "LogFile.h"
#include <assert.h>
#include <cstdio>
#include <cstring>
#include <ctime>

using namespace lrpc::util;

class LogFile::File {
public:
  File(const File &) = delete;
  File &operator=(const File &) = delete;
  explicit File(const string &filename)
      : fp_(::fopen(filename.c_str(), "ae")), writtenBytes_(0) {
    ::setbuffer(fp_, buffer_, sizeof buffer_);
  }
  ~File() { ::fclose(fp_); }

  void append(const char *logline, const size_t len) {
    size_t n = write(logline, len);
    size_t remain = len - n;
    while (remain > 0) { // 如果没有写完 len 字节，那么再次尝试写入剩余的部分
      size_t x = write(logline + n, remain);
      if (x == 0) { // 如果再次尝试还是没有将剩余的部分写入，那么就打印错误信息
        int err = ferror(fp_);
        if (err) {
          char buf[128];
          strerror_r(err, buf, sizeof buf); // 获取错误代码描述字符串
          fprintf(stderr, "LogFile::File::append() failed %s\n", buf);
        }
        break;
      }
      // 写入成功，继续判断是否还有没写完的
      n += x;
      remain = len - n;
    }
    writtenBytes_ += len;
  }

  /// 刷新文件描述符，写入到磁盘
  void flush() { ::fflush(fp_); }

  size_t writtenBytes() const { return writtenBytes_; }

private:
  size_t write(const char *logline, size_t len) {
#undef fwrite_unlocked
    return ::fwrite_unlocked(logline, 1, len, fp_);
  }

  FILE *fp_; // 文件描述符
  char buffer_[64 * 1024];
  size_t writtenBytes_;
};