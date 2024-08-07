#pragma once

#include <pico/async_context.h>
#include <pico/time.h>

#include <chrono>
#include <coroutine>
#include <cstdlib>

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

  bool await_ready();
  void await_suspend(std::coroutine_handle<> continuation);
  void await_resume();
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

Sleep sleep_for(async_context_t *context, std::chrono::microseconds delay);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - -

class ScheduledContinuation {
  async_at_time_worker_t worker_;
  std::coroutine_handle<> continuation_;

  static void invoke_and_destroy(async_context_t*, async_at_time_worker_t*);

 public:
  static void create_and_schedule(async_context_t *context, std::coroutine_handle<> continuation, absolute_time_t deadline);
};

// Implementations
// ===============

// struct Sleep
// ------------
bool Sleep::await_ready() { return false; }

void Sleep::await_suspend(std::coroutine_handle<> continuation) {
  ScheduledContinuation::create_and_schedule(context, continuation, deadline);
}

void Sleep::await_resume() {}

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


} // namespace picoro
