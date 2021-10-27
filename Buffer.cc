#include "Buffer.h"
#include "SocketsOps.h"
#include <boost/log/trivial.hpp>
#include <cerrno>
#include <memory.h>
#include <sys/uio.h>

using namespace imitate_muduo;

/// 直接从文件读取数据到 buffer
ssize_t Buffer::readFd(int fd, int *savedErrno) {
  // 通过 readv 将数据读到不连续的内存
  char extrabuf[65536];
  struct iovec vec[2];
  const size_t writable = writeableBytes();
  vec[0].iov_base = begin() + writerIndex_;
  vec[0].iov_len = writable;
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof extrabuf;
  const ssize_t n = readv(fd, vec, 2);  // n 文件中的字节数
  // 数据已经读完，接下来移动 writeIndex_
  if (n < 0) {
    *savedErrno = errno;
  } else if (static_cast<size_t>(n) <= writable) {
    // 文件数据小于可写缓冲区大小

    writerIndex_ += n;
  } else {
    // 文件数据大于可写缓冲区大小，只读取缓冲区大小的数据

    // 先把 writeIndex_ 移到缓冲区最后
    writerIndex_ = buffer_.size();
    // 往后追加 n - writable 字节数据
    append(extrabuf, n - writable);
  }
  return n;
}