#pragma once

// Minimal lazy coroutine task with symmetric transfer. The EU is written as
// ordinary sequential code that suspends on queue bytes, bus cycles and EU
// clocks; the core resumes the innermost suspended coroutine on CLK edges.

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace i8086::model::detail {

template <class T = void>
class Task;

namespace task_impl {

struct PromiseBase {
  std::coroutine_handle<> continuation;
  std::exception_ptr exception;

  std::suspend_always initial_suspend() noexcept { return {}; }

  struct FinalAwaiter {
    bool await_ready() noexcept { return false; }
    template <class P>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) noexcept {
      if (auto c = h.promise().continuation) return c;
      return std::noop_coroutine();
    }
    void await_resume() noexcept {}
  };
  FinalAwaiter final_suspend() noexcept { return {}; }
  void unhandled_exception() { exception = std::current_exception(); }
};

template <class T>
struct Promise : PromiseBase {
  std::optional<T> value;
  Task<T> get_return_object();
  void return_value(T v) { value = std::move(v); }
  T take() {
    if (exception) std::rethrow_exception(exception);
    return std::move(*value);
  }
};

template <>
struct Promise<void> : PromiseBase {
  Task<void> get_return_object();
  void return_void() {}
  void take() {
    if (exception) std::rethrow_exception(exception);
  }
};

}  // namespace task_impl

template <class T>
class Task {
 public:
  using promise_type = task_impl::Promise<T>;
  using Handle = std::coroutine_handle<promise_type>;

  Task() = default;
  explicit Task(Handle h) : h_(h) {}
  Task(Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
  Task& operator=(Task&& o) noexcept {
    if (this != &o) {
      reset();
      h_ = std::exchange(o.h_, {});
    }
    return *this;
  }
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() { reset(); }

  void reset() {
    if (h_) h_.destroy();
    h_ = {};
  }
  Handle handle() const { return h_; }
  bool done() const { return !h_ || h_.done(); }
  std::exception_ptr exception() const { return h_ ? h_.promise().exception : nullptr; }

  bool await_ready() const noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
    h_.promise().continuation = caller;
    return h_;
  }
  T await_resume() { return h_.promise().take(); }

 private:
  Handle h_;
};

namespace task_impl {
template <class T>
Task<T> Promise<T>::get_return_object() {
  return Task<T>{std::coroutine_handle<Promise<T>>::from_promise(*this)};
}
inline Task<void> Promise<void>::get_return_object() {
  return Task<void>{std::coroutine_handle<Promise<void>>::from_promise(*this)};
}
}  // namespace task_impl

}  // namespace i8086::model::detail
