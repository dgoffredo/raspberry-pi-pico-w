#pragma once

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "pico/async_context.h"

#include <algorithm>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <queue>
#include <string_view>
#include <tuple>

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

const char *lwip_describe(err_t error);

void defer(async_context_t *context, std::coroutine_handle<> continuation);

class DeferredContinuation {
  async_when_pending_worker_t worker_;
  std::coroutine_handle<> continuation_;

  static void invoke_and_destroy(async_context_t*, async_when_pending_worker_t*);

 public:
  static void create_and_defer(async_context_t *context, std::coroutine_handle<> continuation);
};

class Listener;
class Connection;
class AcceptAwaiter;
class SendAwaiter;
class RecvAwaiter;

class Listener {
 public:
  struct State {
    async_context_t *context;
    err_t error;
    tcp_pcb *pcb; // 👾 🤖 PROTOCOL CONTROL BLOCK 🤖 👾
    std::size_t backlog;
    std::queue<tcp_pcb*> unaccepted;
    std::queue<AcceptAwaiter*> accepters;
  };

 private:
  std::unique_ptr<State> state_;

  friend class AcceptAwaiter;

  static err_t on_accept(void *user_data, tcp_pcb *client_pcb, err_t err);

 public:
  explicit Listener(async_context_t *context, int port, int backlog);
  Listener(Listener&&) = default;
  ~Listener();

  AcceptAwaiter accept();
  err_t close();

  err_t error() const;
};

std::tuple<Listener, err_t> listen(async_context_t *, int port, int backlog);

class Connection {
 public:
  struct State {
    async_context_t *context;
    tcp_pcb *client_pcb; // 👾 🤖 PROTOCOL CONTROL BLOCK 🤖 👾
    std::string received; // TODO: consider making a `dequeue<char>`
    std::queue<SendAwaiter*> senders;
    std::queue<RecvAwaiter*> receivers;
  };
  
 private:
  std::unique_ptr<State> state_;
  
  static err_t on_recv(void *user_data, tcp_pcb *client_pcb, pbuf *buffer, err_t error);
  static err_t on_sent(void *user_data, tcp_pcb *client_pcb, u16_t length);
  static void on_err(void *user_data, err_t err);

 public:
  explicit Connection(async_context_t *context, tcp_pcb *client_pcb);
  Connection(Connection&&) = default;
  ~Connection();

  SendAwaiter send(std::string_view data);
  RecvAwaiter recv(char *destination, int size);
  err_t close();
};

class AcceptAwaiter {
  Listener::State *listener_;
  
  tcp_pcb *client_pcb; // 👾 🤖 PROTOCOL CONTROL BLOCK 🤖 👾
  err_t error;
  std::coroutine_handle<> continuation;
  
  friend class Listener;

 public:
  explicit AcceptAwaiter(Listener::State *listener);

  bool await_ready();
  void await_suspend(std::coroutine_handle<> continuation);
  std::tuple<Connection, err_t> await_resume();
};

class RecvAwaiter {
  Connection::State *connection;
  char *destination;
  std::size_t length;
  std::size_t remaining;
  err_t error;
  std::coroutine_handle<> continuation;

  friend class Connection;

 public:
  RecvAwaiter(Connection::State *connection, char *destination, int size);

  bool await_ready();
  void await_suspend(std::coroutine_handle<> continuation);
  std::tuple<int, err_t> await_resume();
};

class SendAwaiter {
  Connection::State *connection;
  std::size_t length;
  std::size_t remaining;
  err_t error;
  std::coroutine_handle<> continuation;

  friend class Connection;

 public:
  SendAwaiter(Connection::State *connection, std::string_view data);

  bool await_ready();
  void await_suspend(std::coroutine_handle<> continuation);
  std::tuple<int, err_t> await_resume();
};

// Implementations
// ===============

// const char *lwip_describe(err_t error)
// --------------------------------------
inline
const char *lwip_describe(err_t error) {
    switch (error) {
    case ERR_OK: return "[ERR_OK] No error, everything OK";
    case ERR_MEM: return "[ERR_MEM] Out of memory";
    case ERR_BUF: return "[ERR_BUF] Buffer error";
    case ERR_TIMEOUT: return "[ERR_TIMEOUT] Timeout";
    case ERR_RTE: return "[ERR_RTE] Routing problem";
    case ERR_INPROGRESS: return "[ERR_INPROGRESS] Operation in progress";
    case ERR_VAL: return "[ERR_VAL] Illegal value";
    case ERR_WOULDBLOCK: return "[ERR_WOULDBLOCK] Operation would block";
    case ERR_USE: return "[ERR_USE] Address in use";
    case ERR_ALREADY: return "[ERR_ALREADY] Already connecting";
    case ERR_ISCONN: return "[ERR_ISCONN] Conn already established";
    case ERR_CONN: return "[ERR_CONN] Not connected";
    case ERR_IF: return "[ERR_IF] Low-level netif error";
    case ERR_ABRT: return "[ERR_ABRT] Connection aborted";
    case ERR_RST: return "[ERR_RST] Connection reset";
    case ERR_CLSD: return "[ERR_CLSD] Connection closed";
    case ERR_ARG: return "[ERR_ARG] Illegal argument";
    }
    return "Unknown lwIP error code";
}

// void defer(async_context_t*, std::coroutine_handle<>)
// -----------------------------------------------------
inline
void defer(async_context_t *context, std::coroutine_handle<> continuation) {
  DeferredContinuation::create_and_defer(context, continuation);
}

// class DeferredContinuation
// --------------------------
inline
void DeferredContinuation::create_and_defer(async_context_t *context, std::coroutine_handle<> continuation) {
  auto *deferred = new DeferredContinuation;
  deferred->continuation_ = continuation;
  deferred->worker_.do_work = &DeferredContinuation::invoke_and_destroy;
  deferred->worker_.user_data = deferred;
  (void) async_context_add_when_pending_worker(context, &deferred->worker_);
  async_context_set_work_pending(context, &deferred->worker_);
}

inline
void DeferredContinuation::invoke_and_destroy(async_context_t *context, async_when_pending_worker_t *worker) {
  auto *deferred = static_cast<DeferredContinuation*>(worker->user_data);
  std::coroutine_handle<> continuation = deferred->continuation_;
  (void) async_context_remove_when_pending_worker(context, worker);
  delete deferred;
  continuation.resume();
}

// class Listener
// --------------
inline
Listener::Listener(async_context_t *context, int port, int backlog)
: state_(std::make_unique<Listener::State>()) {
  state_->context = context;
  state_->error = ERR_OK;
  state_->pcb = nullptr;
  state_->backlog = backlog;

  state_->pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (!state_->pcb) {
    state_->error = ERR_MEM;
    return;
  }
  
  if ((state_->error = tcp_bind(state_->pcb, IP_ADDR_ANY, port))) {
    return;
  }
  state_->pcb = tcp_listen_with_backlog_and_err(state_->pcb, backlog, &state_->error);
  if (state_->error) {
    return;
  }
  tcp_arg(state_->pcb, state_.get());
  tcp_accept(state_->pcb, &Listener::on_accept);
}

inline
Listener::~Listener() {
  (void) close();
}

inline
AcceptAwaiter Listener::accept() {
  return AcceptAwaiter(state_.get());
}

inline
err_t Listener::close() {
  if (!state_ || !state_->pcb) {
    // We were moved from, closed, or otherwise invalid.
    return ERR_ARG;
  }

  // Abort any connections that we received but didn't accept().
  while (!state_->unaccepted.empty()) {
    tcp_pcb *client_pcb = state_->unaccepted.front();
    state_->unaccepted.pop();
    tcp_abort(client_pcb);
  }

  // Return an error to any pending accept()ers.
  while (!state_->accepters.empty()) {
    AcceptAwaiter *accepter = state_->accepters.front();
    state_->accepters.pop();
    accepter->error = ERR_CLSD; // TODO: technically not "connection closed"
    defer(state_->context, accepter->continuation);
  }
  
  // Close our  👾 🤖 PROTOCOL CONTROL BLOCK 🤖 👾.
  // For client sockets, setting the callbacks to nullptr prevents a crash,
  // even though the pattern is undocumented.  For a listening socket, I'm not
  // sure, but let's do it to be safe.
  tcp_accept(state_->pcb, nullptr);
  state_->error = tcp_close(state_->pcb);
  state_->pcb = nullptr;
  
  return state_->error;
}

inline
err_t Listener::error() const {
  return state_->error;
}

inline
err_t Listener::on_accept(void *user_data, tcp_pcb *client_pcb, err_t error) {
  auto *state = static_cast<Listener::State*>(user_data);

  if (!state->accepters.empty()) {
    AcceptAwaiter *accepter = state->accepters.front();
    state->accepters.pop();
    if (error) {
      accepter->error = error;
    } else {
      accepter->client_pcb = client_pcb;
    }
    defer(state->context, accepter->continuation);
    return ERR_OK;
  }

  // There are no accepters. Add `client_pcb` to the backlog unless there isn't
  // any space.
  if (state->unaccepted.size() == state->backlog) {
    tcp_abort(client_pcb);
    return ERR_ABRT;
  }

  state->unaccepted.push(client_pcb);
  return ERR_OK;
}

inline
std::tuple<Listener, err_t> listen(async_context_t *context, int port, int backlog) {
  Listener listener(context, port, backlog);
  err_t error = listener.error();
  return {std::move(listener), error};
}

// class AcceptAwaiter
// -------------------
inline
AcceptAwaiter::AcceptAwaiter(Listener::State *listener)
: listener_(listener), client_pcb(nullptr), error(ERR_OK) {
  // If the listener has a connection ready for us, then take it.
  // Otherwise, await_ready() will subsequently return false, and then
  // await_suspend() will enqueue us onto listener->accepters.
  if (listener->unaccepted.empty() || !listener->accepters.empty()) {
    return;
  }

  client_pcb = listener->unaccepted.front();
  listener->unaccepted.pop();
}

inline
bool AcceptAwaiter::await_ready() {
  return client_pcb != nullptr;
}

inline
void AcceptAwaiter::await_suspend(std::coroutine_handle<> continuation) {
  this->continuation = continuation;
  listener_->accepters.push(this);
}

inline
std::tuple<Connection, err_t> AcceptAwaiter::await_resume() {
  return {Connection(listener_->context, client_pcb), error};
}


// class Connection
// ----------------
inline
Connection::Connection(async_context_t *context, tcp_pcb *client_pcb) {
  if (!client_pcb) {
    return;
  }
  state_ = std::make_unique<Connection::State>();
  state_->context = context;
  state_->client_pcb = client_pcb;
  
  tcp_arg(client_pcb, state_.get());
  tcp_sent(client_pcb, &Connection::on_sent);
  tcp_recv(client_pcb, &Connection::on_recv);
  tcp_err(client_pcb, &Connection::on_err);
}

inline
Connection::~Connection() {
  (void) close();
}

inline
err_t Connection::on_recv(void *user_data, tcp_pcb *, pbuf *buffer, err_t error) {
  auto *state = static_cast<Connection::State*>(user_data);

  struct Guard {
    pbuf *buffer;
    ~Guard() {
      if (buffer) {
        pbuf_free(buffer);
      }
    }
  } guard{buffer};

  if (error || buffer == nullptr) {
    // error or connection closed
    // TODO: Can `buffer` have data in it if `error != ERR_OK`? If so, should
    // we deliver the data to receivers?
    while (!state->receivers.empty()) {
      RecvAwaiter *receiver = state->receivers.front();
      state->receivers.pop();
      receiver->error = error;
      defer(state->context, receiver->continuation);
    }
    return ERR_OK;
  }

  // data received
  // First, append it to the end of `state->received`.
  // Then look for receivers to fill up with data from the beginning of
  // `state->received`.
  std::string& received = state->received; // brevity
  const std::size_t old_size = received.size();
  received.resize(old_size + buffer->tot_len);
  const u16_t buffer_offset = 0;
  const u16_t copied = pbuf_copy_partial(buffer, received.data() + old_size, buffer->tot_len, buffer_offset);
  received.resize(old_size + copied);
  // Deal out data from the beginning of `received` until we're either out of
  // data or out of receivers.
  std::size_t i = 0;
  while (i < received.size() && !state->receivers.empty()) {
    RecvAwaiter *receiver = state->receivers.front();
    const auto to_copy = std::min<std::size_t>(receiver->remaining, received.size() - i);
    std::copy_n(received.begin() + i, to_copy, receiver->destination);
    receiver->destination += to_copy;
    receiver->remaining -= to_copy;
    i += to_copy;
    if (receiver->remaining == 0) {
      // This receiver has received all of its requested data, and so now can
      // be resumed.  Also, we can tell lwIP that we've "processed" however
      // much data the receiver requested.
      state->receivers.pop();
      defer(state->context, receiver->continuation);
      tcp_recved(state->client_pcb, receiver->length);
    }
  }
  received.erase(0, i);

  return ERR_OK;
}

inline
err_t Connection::on_sent(void *user_data, tcp_pcb *, u16_t length) {
  auto *state = static_cast<Connection::State*>(user_data);

  // Resume any senders that are "filled up" by the client's acknowledgement of
  // `length` bytes.
  while (length && !state->senders.empty()) {
    SendAwaiter *sender = state->senders.front();
    const auto to_ack = std::min<std::size_t>(sender->remaining, length);
    length -= to_ack;
    sender->remaining -= to_ack;
    if (sender->remaining == 0) {
      state->senders.pop();
      defer(state->context, sender->continuation);
    }
  }

  return ERR_OK;
}

inline
void Connection::on_err(void *user_data, err_t error) {
  auto *state = static_cast<Connection::State*>(user_data);

  // The pcb is already freed (per lwIP's documentation), so set it to null in
  // `state`. This way, we won't try to `tcp_close` it in the future.
  state->client_pcb = nullptr;

  // Convey the error to all senders and all receivers.
  const auto consume = [error, context = state->context](auto& queue) {
    while (queue.empty()) {
      auto *awaiter = queue.front();
      queue.pop();
      awaiter->error = error;
      defer(context, awaiter->continuation);
    }
  };

  consume(state->senders);
  consume(state->receivers);
}

inline
SendAwaiter Connection::send(std::string_view data) {
  return SendAwaiter(state_.get(), data);
}

inline
RecvAwaiter Connection::recv(char *destination, int size) {
  return RecvAwaiter(state_.get(), destination, size);
}

inline
err_t Connection::close() {
  if (!state_) {
    // We were moved from or invalid to begin with.
    return ERR_ARG;
  }

  if (state_->client_pcb == nullptr) {
    // We were already closed.
    return ERR_CLSD;
  }

  const err_t error = tcp_close(state_->client_pcb);
  state_->client_pcb = nullptr;

  // Wake up senders and receivers. Deliver a ERR_CLSD (connection closed)
  // error to them.
  const auto consume = [context = state_->context](auto &queue) {
    while (!queue.empty()) {
      auto *awaiter = queue.front();
      queue.pop();
      awaiter->error = ERR_CLSD;
      defer(context, awaiter->continuation);
    }
  };

  consume(state_->senders);
  consume(state_->receivers);

  return error; // whatever `tcp_close` returned
}

// class RecvAwaiter
// -----------------
inline
RecvAwaiter::RecvAwaiter(Connection::State *connection, char *destination, int size)
: connection(connection), destination(destination), length(size), remaining(size), error(ERR_OK) {
  // If `connection` is null, we return (0, ERR_CLSD) without suspending.
  if (!connection) {
    error = ERR_CLSD;
    return;
  }

  // If nobody else is enqueued to consume received data from the connection,
  // then consume data if it's already available.  If we end up consuming `size`
  // bytes, then we won't even have to suspend.
  if (connection->receivers.empty()) {
    const auto to_consume = std::min<std::size_t>(connection->received.size(), size);
    std::copy_n(connection->received.begin(), to_consume, destination);
    this->destination += to_consume;
    remaining -= to_consume;
    connection->received.erase(0, to_consume);
  }
}

inline
bool RecvAwaiter::await_ready() {
  return error || remaining == 0;
}

void RecvAwaiter::await_suspend(std::coroutine_handle<> continuation) {
  this->continuation = continuation;
  assert(connection);
  connection->receivers.push(this);
}

inline
std::tuple<int, err_t> RecvAwaiter::await_resume() {
  return {length - remaining, error};
}

// class SendAwaiter
// -----------------
inline
SendAwaiter::SendAwaiter(Connection::State *connection, std::string_view data)
: connection(connection), length(data.size()), remaining(data.size()), error(ERR_OK) {
  // If `connection` is null, we return (0, ERR_CLSD) without suspending.
  if (!connection) {
    error = ERR_CLSD;
    return;
  }

  const u8_t flags = TCP_WRITE_FLAG_COPY;
  error = tcp_write(connection->client_pcb, data.data(), data.size(), flags);
  // `tcp_write` enqueues data for sending "later." `tcp_output` actually tries
  // to send the data.
  if (error == ERR_OK) {
    error = tcp_output(connection->client_pcb);
  }
}

inline
bool SendAwaiter::await_ready() {
  return error;
}

inline
void SendAwaiter::await_suspend(std::coroutine_handle<> continuation) {
  this->continuation = continuation;
  assert(connection);
  connection->senders.push(this);
}

inline
std::tuple<int, err_t> SendAwaiter::await_resume() {
  return {length - remaining, error};
}

} // namespace picoro
