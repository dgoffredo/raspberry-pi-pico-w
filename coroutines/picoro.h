#pragma once

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

#include "pico/async_context.h"

namespace picoro {
//              _.---..._     
//           ./^         ^-._       
//         ./^C===.         ^\.   /\.
//        .|'     \\        _ ^|.^.|
//   ___.--'_     ( )  .      ./ /||
//  /.---^T\      ,     |     / /|||
// C'   ._`|  ._ /  __,-/    / /-,||
//      \ \/    ;  /O  / _    |) )|,
//       i \./^O\./_,-^/^    ,;-^,'      
//        \ |`--/ ..-^^      |_-^       
//         `|  \^-           /|:       
//          i.  .--         / '|.                                   
//           i   =='       /'  |\._                                 
//         _./`._        //    |.  ^-ooo.._                        
//  _.oo../'  |  ^-.__./X/   . `|    |#######b                  
// d####     |'      ^^^^   /   |    _\#######               
// #####b ^^^^^^^^--. ...--^--^^^^^^^_.d######                
// ######b._         Y            _.d#########              
// ##########b._     |        _.d#############              
//
//                     --- Steven J. Simmons

struct Sleep {
  async_context_t *context;
  absolute_time_t deadline;
};

Sleep sleep_for(async_context_t *context, std::chrono::microseconds delay);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

class ScheduledContinuation {
  async_at_time_worker_t worker_;
  std::coroutine_handle<> continuation_;

  static void invoke_and_destroy(async_context_t*, async_at_time_worker_t*);

 public:
  static void create_and_schedule(async_context_t *context, std::coroutine_handle<> continuation, absolute_time_t deadline);
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

class FinalAwaitable {
  std::coroutine_handle<> coroutine_;

 public:
  explicit FinalAwaitable(std::coroutine_handle<> coroutine);

  bool await_ready() noexcept;
  std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept;
  void await_resume() noexcept;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

template <typename Ret>
class Awaiter;

template <typename Ret>
class Promise;

template <typename Ret>
class Coroutine {
 public:
  struct Deleter {
    void operator()(Promise<Ret> *promise) {
      std::coroutine_handle<Promise<Ret>>::from_promise(*promise).destroy();
    }
  };

  using promise_type = Promise<Ret>; // required by the C++ coroutine protocol
  using UniqueHandle = std::unique_ptr<Promise<Ret>, Deleter>;

 private:
  UniqueHandle promise_;
  
 public:
  explicit Coroutine(UniqueHandle promise);

  Coroutine(Coroutine&&) = default;

  Coroutine() = delete;
  Coroutine(const Coroutine&) = delete;

  Awaiter<Ret> operator co_await();
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

template <typename... Coroutines>
void run_event_loop(async_context_t *context, Coroutines... toplevel_coroutines);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

template <typename Ret>
class Promise {
  // `continuation_` is what runs after we're done.
  // By default, it's a no-op handle, which means "nothing to do after."
  // Sometimes, `Coroutine::Awaiter::await_suspend` will assign a non-no-op
  // handle to `continuation_`.
  // `continuation_` is passed to `FinalAwaitable` in `final_suspend`.
  // `FinalAwaitable` will then return it in `FinalAwaitable::await_suspend`,
  // which will cause it to be `.resume()`d.
  std::coroutine_handle<> continuation_;
  
  // `value_` is where we store the `foo` from `co_return foo;`.
  alignas(Ret) std::byte value_[sizeof(Ret)];

  friend class Awaiter<Ret>;

 public:
  Promise();
  Promise(const Promise&) = delete;
  Promise(Promise&&) = delete;
  ~Promise();

  Coroutine<Ret> get_return_object();
  std::suspend_never initial_suspend();
  FinalAwaitable final_suspend() noexcept;
  void return_value(Ret&&);

  // If we `co_await` a `Sleep`, schedule ourselves as a continuation for when
  // the sleep is done.
  std::suspend_always await_transform(Sleep sleep);

  // If we `co_await` a something else, just use the value as-is.
  template <typename Object>
  auto &&await_transform(Object &&) const noexcept;

  void unhandled_exception();
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

template <>
class Promise<void> {
  // `continuation_` is what runs after we're done.
  // By default, it's a no-op handle, which means "nothing to do after."
  // Sometimes, `Coroutine::Awaiter::await_suspend` will assign a non-no-op
  // handle to `continuation_`.
  // `continuation_` is passed to `FinalAwaitable` in `final_suspend`.
  // `FinalAwaitable` will then return it in `FinalAwaitable::await_suspend`,
  // which will cause it to be `.resume()`d.
  std::coroutine_handle<> continuation_;

  friend class Awaiter<void>;

 public:
  Promise();
  Promise(const Promise&) = delete;
  Promise(Promise&&) = delete;

  Coroutine<void> get_return_object();
  std::suspend_never initial_suspend();
  FinalAwaitable final_suspend() noexcept;
  void return_void();

  // If we `co_await` a `Sleep`, schedule ourselves as a continuation for when
  // the sleep is done.
  std::suspend_always await_transform(Sleep sleep);

  // If we `co_await` a something else, just use the value as-is.
  template <typename Object>
  auto &&await_transform(Object &&) const noexcept;

  void unhandled_exception();
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

template <typename Ret>
class Awaiter {
  Promise<Ret> *promise_;

 public:
  explicit Awaiter(Promise<Ret> *promise);

  bool await_ready();
  bool await_suspend(std::coroutine_handle<> continuation);
  Ret await_resume();
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

template <>
class Awaiter<void> {
  Promise<void> *promise_;

 public:
  explicit Awaiter(Promise<void> *promise);

  bool await_ready();
  bool await_suspend(std::coroutine_handle<> continuation);
  void await_resume();
};

// Implementations
// ===============

// struct Sleep
// ------------
inline
Sleep sleep_for(async_context_t *context, std::chrono::microseconds delay) {
  const uint64_t delay_us = delay / std::chrono::microseconds(1);
  return Sleep{context, make_timeout_time_us(delay_us)};
}

// class ScheduledContinuation
// ---------------------------
inline
void ScheduledContinuation::invoke_and_destroy(async_context_t*, async_at_time_worker_t* worker) {
  auto *scheduled = static_cast<ScheduledContinuation*>(worker->user_data);
  std::coroutine_handle<> handle = scheduled->continuation_;
  delete scheduled;
  handle.resume();
}

inline
void ScheduledContinuation::create_and_schedule(async_context_t *context, std::coroutine_handle<> continuation, absolute_time_t deadline) {
  auto *scheduled = new ScheduledContinuation;
  scheduled->continuation_ = continuation;
  scheduled->worker_.do_work = &ScheduledContinuation::invoke_and_destroy;
  scheduled->worker_.user_data = scheduled;
  const bool added = async_context_add_at_time_worker_at(context, &scheduled->worker_, deadline);
  (void)added;
  assert(added);
}

// class Coroutine<Ret>
// --------------------
template <typename Ret>
inline
Coroutine<Ret>::Coroutine(UniqueHandle promise)
: promise_(std::move(promise)) {}

template <typename Ret>
inline
Awaiter<Ret> Coroutine<Ret>::operator co_await() {
  return Awaiter<Ret>(promise_.get());
}

// class FinalAwaitable
// -------------------------------
inline
FinalAwaitable::FinalAwaitable(std::coroutine_handle<> coroutine)
: coroutine_(coroutine) {}

inline
bool FinalAwaitable::await_ready() noexcept { return false; }

inline
std::coroutine_handle<> FinalAwaitable::await_suspend(std::coroutine_handle<>) noexcept {
  return coroutine_;
}

inline
void FinalAwaitable::await_resume() noexcept {}

// class Promise<Ret>
// ------------------
template <typename Ret>
Promise<Ret>::Promise()
: continuation_(std::noop_coroutine())
{}

template <typename Ret>
Promise<Ret>::~Promise() {
  Ret *value = std::launder(reinterpret_cast<Ret*>(&value_[0]));
  value->~Ret();
}

template <typename Ret>
Coroutine<Ret> Promise<Ret>::get_return_object() {
  return Coroutine<Ret>(typename Coroutine<Ret>::UniqueHandle(this));
}

template <typename Ret>
std::suspend_never Promise<Ret>::initial_suspend() {
  return std::suspend_never();
}

template <typename Ret>
FinalAwaitable Promise<Ret>::final_suspend() noexcept {
  return FinalAwaitable(continuation_);
}

template <typename Ret>
void Promise<Ret>::return_value(Ret&& value) {
  new (&value_[0]) Ret(std::move(value));
}

template <typename Ret>
std::suspend_always Promise<Ret>::await_transform(Sleep sleep) {
  ScheduledContinuation::create_and_schedule(sleep.context, std::coroutine_handle<Promise<Ret>>::from_promise(*this), sleep.deadline);
  return std::suspend_always();
}

template <typename Ret>
template <typename Object>
auto&& Promise<Ret>::await_transform(Object &&object) const noexcept {
  return std::forward<Object>(object);
}

template <typename Ret>
void Promise<Ret>::unhandled_exception() {
  std::terminate();
}

// class Promise<void>
// -------------------
inline
Promise<void>::Promise()
: continuation_(std::noop_coroutine())
{}

inline
Coroutine<void> Promise<void>::get_return_object() {
  return Coroutine<void>(Coroutine<void>::UniqueHandle(this));
}

inline
std::suspend_never Promise<void>::initial_suspend() {
  return std::suspend_never();
}

inline
FinalAwaitable Promise<void>::final_suspend() noexcept {
  return FinalAwaitable(continuation_);
}

inline
void Promise<void>::return_void() {}

inline
std::suspend_always Promise<void>::await_transform(Sleep sleep) {
  ScheduledContinuation::create_and_schedule(sleep.context, std::coroutine_handle<Promise<void>>::from_promise(*this), sleep.deadline);
  return std::suspend_always();
}

template <typename Object>
auto&& Promise<void>::await_transform(Object &&object) const noexcept {
  return std::forward<Object>(object);
}

inline
void Promise<void>::unhandled_exception() {
  std::terminate();
}

// class Awaiter<Ret>
// ------------------
template <typename Ret>
Awaiter<Ret>::Awaiter(Promise<Ret> *promise)
: promise_(promise) {}

template <typename Ret>
bool Awaiter<Ret>::await_ready() {
  auto handle = std::coroutine_handle<Promise<Ret>>::from_promise(*promise_);
  return handle.done();
}

template <typename Ret>
bool Awaiter<Ret>::await_suspend(std::coroutine_handle<> continuation) {
  promise_->continuation_ = continuation;
  return true;
}

template <typename Ret>
Ret Awaiter<Ret>::await_resume() {
  Ret *value = std::launder(reinterpret_cast<Ret*>(&promise_->value_[0]));
  return std::move(*value);
}

// class Awaiter<void>
// -------------------
inline
Awaiter<void>::Awaiter(Promise<void> *promise)
: promise_(promise) {}

inline
bool Awaiter<void>::await_ready() {
  auto handle = std::coroutine_handle<Promise<void>>::from_promise(*promise_);
  return handle.done();
}

inline
bool Awaiter<void>::await_suspend(std::coroutine_handle<> continuation) {
  promise_->continuation_ = continuation;
  return true;
}

inline
void Awaiter<void>::await_resume() {}

// void run_event_loop
// -------------------
template <typename... Coroutines>
void run_event_loop(async_context_t *context, Coroutines... toplevel_coroutines) {
  for (;;) {
    async_context_poll(context);
    async_context_wait_for_work_ms(context, 10 * 1000);
  }
}

}  // namespace picoro
