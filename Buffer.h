#ifndef IMITATE_MUDUO_BUFFER_H
#define IMITATE_MUDUO_BUFFER_H

#include <assert.h>
#include <string>
#include <vector>

namespace imitate_muduo {

/// @code
/// +-------------------+------------------+------------------+
/// | prependable bytes |  readable bytes  |  writable bytes  |
/// |                   |     (CONTENT)    |                  |
/// +-------------------+------------------+------------------+
/// |                   |                  |                  |
/// 0      <=      readerIndex   <=   writerIndex    <=     size

class Buffer {
private:
  /// 获取缓冲区的首地址
  char *begin() { return &*buffer_.begin(); }
  const char *begin() const { return &*buffer_.begin(); }

  /// 扩充缓冲区的大小
  /// @param len 新缓冲区至少需要可写字节数
  void makeSpace(size_t len) {
    if (writeableBytes() + prependableBytes() < len + kCheapPrepend) {
      // 缓冲区总大小 < 新缓冲区需要的大小

      // 那直接扩充 len 字节
      buffer_.resize(writerIndex_ + len);
    } else {
      // 说明当前缓冲区的总大小已经足够了，只是随着时间的推移，前置区域太大了

      // 把可读数据移到前面去
      assert(kCheapPrepend < readerIndex_);
      size_t readable = readableBytes();
      std::copy(begin() + readerIndex_, begin() + writerIndex_,
                begin() + kCheapPrepend);
      writerIndex_ = readerIndex_ + readable;
      assert(readable == readableBytes());
    }
  }

  std::vector<char> buffer_;
  size_t readerIndex_;
  size_t writerIndex_;

public:
  static const size_t kCheapPrepend = 8;
  static const size_t kInitialSize = 1024;

  Buffer()
      : buffer_(kCheapPrepend + kInitialSize), readerIndex_(kCheapPrepend),
        writerIndex_(kCheapPrepend) {
    assert(readableBytes() == 0);
    assert(writeableBytes() == kInitialSize);
    assert(prependableBytes() == kCheapPrepend);
  }

  void swap(Buffer &rhs) {
    buffer_.swap(rhs.buffer_);
    std::swap(readerIndex_, rhs.readerIndex_);
    std::swap(writerIndex_, rhs.writerIndex_);
  }

  /// 获取可读字节
  size_t readableBytes() const { return writerIndex_ - readerIndex_; }
  /// 获取可写字节
  size_t writeableBytes() const { return buffer_.size() - writerIndex_; }
  /// 获取前置区域字节数
  size_t prependableBytes() const { return readerIndex_; }

  /// 可读数据缓冲区的起始地址
  const char *peek() const { return begin() + readerIndex_; }
  /// 可写缓冲区的起始地址
  char *beginWrite() { return begin() + writerIndex_; }
  const char *beginWrite() const { return begin() + writerIndex_; }

  /// 读取 len 长度的数据（索引移动）
  void retrieve(size_t len) {
    assert(len <= readableBytes());
    readerIndex_ += len;
  }
  /// 读取数据一直到 end 这个位置（索引移动）
  void retrieveUntil(const char *end) {
    assert(peek() <= end);
    assert(end <= beginWrite());
    retrieve(end - peek());
  }
  /// 读取全部数据（索引移动）
  void retrieveAll() {
    readerIndex_ = kCheapPrepend;
    writerIndex_ = kCheapPrepend;
  }
  /// 读取数据返回 string
  std::string retrieveAsString() {
    std::string str(peek(), readableBytes());
    retrieveAll();
    return str;
  }

  /// 向后写数据
  void append(const char *data, size_t len) {
    ensureWritableBytes(len);
    std::copy(data, data + len, beginWrite());
    hasWritten(len);
  }
  void append(const void *data, size_t len) {
    append(static_cast<const char *>(data), len);
  }

  /// 向前写数据，会贴着 readerIndex_ 写
  void prepend(const void *data, size_t len) {
    assert(len <= prependableBytes());
    readerIndex_ -= len;
    const char *d = static_cast<const char *>(data);
    std::copy(d, d + len, begin() + readerIndex_);
  }

  /// 缩小缓冲区的大小，新缓冲区大小为 kCheapPrepend + readableBytes + reserve
  void shrink(size_t reserve) {
    std::vector<char> buf(kCheapPrepend + readableBytes() + reserve);
    std::copy(peek(), peek() + readableBytes(), buf.begin() + kCheapPrepend);
    buf.swap(buffer_);
  }

  /// 确保可写缓冲区大小 >= len，如果空间不够则分配空间
  void ensureWritableBytes(size_t len) {
    if (writeableBytes() < len) {
      makeSpace(len);
    }
    assert(writeableBytes() >= len);
  }
  /// 写完数据之后移动 writeIndex_
  void hasWritten(size_t len) { writerIndex_ += len; }

  ssize_t readFd(int fd, int *savedError);
};

} // namespace imitate_muduo

#endif