#include <functional>

struct Defer {
  std::move_only_function<void()> func;
  Defer(std::move_only_function<void()> func) : func{std::move(func)} {}
  Defer(const Defer&) = delete;
  Defer(Defer&& other) = default;
  Defer& operator=(const Defer&) = delete;
  Defer& operator=(Defer&& other) = default;
  ~Defer() {
    if (func) {
      func();
    }
  }
};
struct DeferHelper {
  template <typename F>
  Defer operator+(F&& f) {
    return Defer{std::forward<F>(f)};
  }
};
#define defer auto _defer_helper##__LINE__ = DeferHelper() + [&]()
#define move_defer DeferHelper() + [&]()