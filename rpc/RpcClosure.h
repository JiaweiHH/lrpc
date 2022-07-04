#ifndef LRPC_RPCCLOSURE_H
#define LRPC_RPCCLOSURE_H

#include <functional>
#include <google/protobuf/stubs/callback.h>
#include <type_traits>

namespace lrpc {

class Closure : public ::google::protobuf::Closure {
public:
  template <typename F, typename... Args,
            typename = typename std::enable_if<
                std::is_void<typename std::result_of<F(Args...)>::type>::value,
                void>::type>
  Closure(F &&f, Args &&...args) {
    func_ = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
  }

  void Run() override final {
    func_();
    delete this;
  }

private:
  std::function<void()> func_;
};

} // namespace lrpc

#endif