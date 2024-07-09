#pragma once

#include <chrono>
#include <coroutine>
#include <cstdlib>
#include <memory>
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

class Coroutine {
 public:
  class Awaiter;
  class FinalAwaitable;
  class Promise;
  struct Deleter {
    void operator()(Coroutine::Promise *promise) {
      std::coroutine_handle<Promise>::from_promise(*promise).destroy();
    }
  };

  using promise_type = Promise; // required by the C++ coroutine protocol
  using UniqueHandle= std::unique_ptr<Promise, Deleter>;

 private:
  UniqueHandle promise_;
  
 public:
  explicit Coroutine(UniqueHandle promise);

  Coroutine(Coroutine&&) = default;

  Coroutine() = delete;
  Coroutine(const Coroutine&) = delete;

  Awaiter operator co_await();
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

template <typename... Coroutines>
void run_event_loop(async_context_t *context, Coroutines... toplevel_coroutines);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

class Coroutine::FinalAwaitable {
  std::coroutine_handle<> coroutine_;

 public:
  explicit FinalAwaitable(std::coroutine_handle<> coroutine);

  bool await_ready() noexcept;
  std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept;
  void await_resume() noexcept;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

class Coroutine::Promise {
  // `continuation_` is what runs after we're done.
  // By default, it's a no-op handle, which means "nothing to do after."
  // Sometimes, `Coroutine::Awaiter::await_suspend` will assign a non-no-op
  // handle to `continuation_`.
  // `continuation_` is passed to `FinalAwaitable` in `final_suspend`.
  // `FinalAwaitable` will then return it in `FinalAwaitable::await_suspend`,
  // which will cause it to be `.resume()`d.
  std::coroutine_handle<> continuation_;

  friend class Awaiter;

 public:
  Promise();

  Coroutine get_return_object();
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

class Coroutine::Awaiter {
  Promise *promise_;

 public:
  explicit Awaiter(Promise *promise);

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

// class Coroutine
// ---------------
inline
Coroutine::Coroutine(UniqueHandle promise)
: promise_(std::move(promise)) {}

inline
Coroutine::Awaiter Coroutine::operator co_await() {
  return Coroutine::Awaiter(promise_.get());
}

// class Coroutine::FinalAwaitable
// -------------------------------
inline
Coroutine::FinalAwaitable::FinalAwaitable(std::coroutine_handle<> coroutine)
: coroutine_(coroutine) {}

inline
bool Coroutine::FinalAwaitable::await_ready() noexcept { return false; }

inline
std::coroutine_handle<> Coroutine::FinalAwaitable::await_suspend(std::coroutine_handle<>) noexcept {
  return coroutine_;
}

inline
void Coroutine::FinalAwaitable::await_resume() noexcept {}

// class Coroutine::Promise
// ------------------------
inline
Coroutine::Promise::Promise()
: continuation_(std::noop_coroutine())
{}

inline
Coroutine Coroutine::Promise::get_return_object() {
  return Coroutine(UniqueHandle(this));
}

inline
std::suspend_never Coroutine::Promise::initial_suspend() {
  return std::suspend_never();
}

inline
Coroutine::FinalAwaitable Coroutine::Promise::final_suspend() noexcept {
  return FinalAwaitable(continuation_);
}

inline
void Coroutine::Promise::return_void() {}

inline
std::suspend_always Coroutine::Promise::await_transform(Sleep sleep) {
  ScheduledContinuation::create_and_schedule(sleep.context, std::coroutine_handle<Promise>::from_promise(*this), sleep.deadline);
  return std::suspend_always();
}

template <typename Object>
auto&& Coroutine::Promise::await_transform(Object &&object) const noexcept {
  return std::forward<Object>(object);
}

inline
void Coroutine::Promise::unhandled_exception() {
  std::terminate();
}

// class Coroutine::Awaiter
// ------------------------
inline
Coroutine::Awaiter::Awaiter(Promise *promise)
: promise_(promise) {}

inline
bool Coroutine::Awaiter::await_ready() {
  return false;
}

inline
bool Coroutine::Awaiter::await_suspend(std::coroutine_handle<> continuation) {
  promise_->continuation_ = continuation;
  return true;
}

inline
void Coroutine::Awaiter::await_resume() {}

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
