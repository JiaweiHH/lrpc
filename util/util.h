#ifndef LRPC_UTIL_H
#define LRPC_UTIL_H

#include <functional>
#include <string>
#include <vector>

namespace lrpc {

namespace util {

/// @brief 离开作用域的时候会执行函数
class ExecuteOnScopeExit {
public:
  ExecuteOnScopeExit() = default;

  // movable
  ExecuteOnScopeExit(ExecuteOnScopeExit &&) = default;
  ExecuteOnScopeExit &operator=(ExecuteOnScopeExit &&) = default;

  // non copyable
  ExecuteOnScopeExit(const ExecuteOnScopeExit &e) = delete;
  void operator=(const ExecuteOnScopeExit &f) = delete;

  template <typename F, typename... Args>
  ExecuteOnScopeExit(F &&f, Args &&...args) {
    func_ = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
  }

  ~ExecuteOnScopeExit() noexcept {
    if (func_)
      func_();
  }

private:
  std::function<void()> func_;
};

#define _CONCAT(a, b) a##b
#define _MAKE_DEFER_(line)                                                     \
  lrpc::util::ExecuteOnScopeExit _CONCAT(defer, line) = [&]()

#undef LRPC_DEFER
#define LRPC_DEFER _MAKE_DEFER_(__LINE__)

inline std::vector<std::string> splitString(const std::string &str,
                                            char seperator) {
  std::vector<std::string> results;
  std::string::size_type start = 0, sep = str.find(seperator);
  while (sep != std::string::npos) {
    if (start < sep)
      results.emplace_back(str.substr(start, sep - start));
    start = sep + 1;
    sep = str.find(seperator);
  }
  if (start != str.size())
    results.emplace_back(str.substr(start));
  return results;
}

} // namespace util

} // namespace lrpc

#endif