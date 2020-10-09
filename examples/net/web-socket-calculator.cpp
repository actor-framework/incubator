// This example application wraps a simple calculator actor and allows clients
// to communicate to this worker via JSON-ish messages over a WebSocket
// connection.
//
// To run the server at port 4242 (defaults to 8080):
//
// ~~~
// web-socket-calculator -p 4242
// ~~~
//
// Once started, the application waits for incoming WebSocket connections that
// send text frames. A simple WebSocket client written in Python could look as
// follows:
//
// ~~~(py)
// #!/usr/bin/env python
//
// import asyncio
// import websockets
//
// line1 = '{ values = [ { "@type" = "task::addition", x = 17, y = 8 } ] }\n'
// line2 = '{ values = [ { "@type" = "task::subtraction", x = 17, y = 8 } ] }\n'
//
// async def hello():
//     uri = "ws://localhost:8080"
//     async with websockets.connect(uri) as websocket:
//         await websocket.send(line1)
//         print(f"> {line1}")
//         response = await websocket.recv()
//         print(f"< {response}")
//         await websocket.send(line2)
//         print(f"> {line2}")
//         response = await websocket.recv()
//         print(f"< {response}")
//
// asyncio.get_event_loop().run_until_complete(hello())
// ~~~

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/exec_main.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/ipv4_address.hpp"
#include "caf/net/actor_shell.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/net/tcp_accept_socket.hpp"
#include "caf/net/web_socket_server.hpp"
#include "caf/tag/mixed_message_oriented.hpp"

#include <cstdint>
#include <regex>
#include <string>

// -- custom message types -----------------------------------------------------

// Usually, we prefer atoms to prefix certain operations. However, using custom
// message types provides a nicer interface for the text-based WebSocket
// communication.

namespace task {

struct addition {
  int32_t x;
  int32_t y;
};

template <class Inspector>
typename Inspector::result_type inspect(Inspector& f, addition& x) {
  return f(x.x, x.y);
}

struct subtraction {
  int32_t x;
  int32_t y;
};

template <class Inspector>
typename Inspector::result_type inspect(Inspector& f, subtraction& x) {
  return f(x.x, x.y);
}

caf::error parse_msg(const caf::string_view& line, caf::message& msg) {
  std::regex rx{R"__(.+"@type" = "(\S+)", x = (\d+), y = (\d+).+)__"};
  std::cmatch pieces;
  if (std::regex_match(line.begin(), line.end(), pieces, rx)) {
    auto t = pieces[1].str();
    auto x = pieces[2].str();
    auto y = pieces[3].str();
    if (t == "task::addition") {
      msg = caf::make_message(addition{std::stoi(x), std::stoi(y)});
      return caf::none;
    }
    if (t == "task::subtraction") {
      msg = caf::make_message(subtraction{std::stoi(x), std::stoi(y)});
      return caf::none;
    }
  }
  return caf::sec::invalid_argument;
}

} // namespace task

CAF_BEGIN_TYPE_ID_BLOCK(web_socket_calculator, caf::first_custom_type_id)

  CAF_ADD_TYPE_ID(web_socket_calculator, (task::addition))
  CAF_ADD_TYPE_ID(web_socket_calculator, (task::subtraction))

CAF_END_TYPE_ID_BLOCK(web_socket_calculator)

// -- implementation of the calculator actor -----------------------------------

caf::behavior calculator() {
  return {
    [](task::addition input) { return input.x + input.y; },
    [](task::subtraction input) { return input.x - input.y; },
  };
}

// -- implementation of the WebSocket application ------------------------------

class app {
public:
  // -- member types -----------------------------------------------------------

  // We expect a stream-oriented interface to the lower communication layers.
  using input_tag = caf::tag::mixed_message_oriented;

  // -- constants --------------------------------------------------------------

  // Restricts our buffer to a maximum of 1MB.
  static constexpr size_t max_buf_size = 1024 * 1024;

  // -- constructors, destructors, and assignment operators --------------------

  explicit app(caf::actor worker) : worker_(std::move(worker)) {
    // nop
  }

  template <class LowerLayerPtr>
  caf::error init(caf::net::socket_manager* mgr, LowerLayerPtr down,
                  const caf::settings&) {
    buf_.reserve(max_buf_size);
    // Create the actor-shell wrapper for this application.
    self_ = mgr->make_actor_shell(down);
    std::cout << "*** established new connection on socket "
              << down->handle().id << "\n";
    return caf::none;
  }

  // -- mixed_message_oriented interface functions -----------------------------

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr down) {
    // The lower level calls this function whenever a send event occurs, but
    // before performing any socket I/O operation. We use this as a hook for
    // constantly checking our mailbox. Returning `false` here aborts the
    // application and closes the socket.
    while (self_->consume_message()) {
      // We set abort_reason in our response handlers in case of an error.
      if (down->abort_reason())
        return false;
      // else: repeat.
    }
    return true;
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr) {
    // The lower level calls this function to check whether we are ready to
    // unregister our socket from send events. We must make sure to put our
    // mailbox in to the blocking state in order to get re-registered once new
    // messages arrive.
    return self_->try_block_mailbox();
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr down, const caf::error& reason) {
    std::cout << "*** lost connection on socket " << down->handle().id << ": "
              << to_string(reason) << "\n";
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume_text(LowerLayerPtr down, caf::string_view text) {
    // The other functions in this class provide mostly boilerplate code. Here
    // comes our main logic. In this function, we receive a text data frame from
    // the WebSocket. We don't assume that the client sends one line per
    // frame, so we buffer all incoming text until finding a newline character.
    if (buf_.size() + text.size() > max_buf_size) {
      auto err = caf::make_error(caf::sec::runtime_error,
                                 "exceeded max text buffer size");
      down->abort_reason(std::move(err));
      return -1;
    }
    buf_.insert(buf_.end(), text.begin(), text.end());
    auto nl_pos = [this] { return std::find(buf_.begin(), buf_.end(), '\n'); };
    for (auto i = nl_pos(); i != buf_.end(); i = nl_pos()) {
      // Skip empty lines.
      if (i == buf_.begin()) {
        buf_.erase(buf_.begin(), buf_.begin() + 1);
        continue;
      }
      // Deserialize message from received line.
      auto num_bytes = std::distance(buf_.begin(), i) + 1;
      caf::string_view line{buf_.data(), static_cast<size_t>(num_bytes) - 1};
      std::cout << "*** [socket " << down->handle().id << "] INPUT: " << line
                << "\n";
      caf::message msg;
      if (auto err = task::parse_msg(line, msg)) {
        down->abort_reason(std::move(err));
        return -1;
      }
      // Dispatch message to worker.
      self_->request(worker_, std::chrono::seconds{1}, std::move(msg))
        .then(
          [this, down](int32_t value) {
            // Simply respond with the value as string, wrapped into a WebSocket
            // text message frame.
            auto str_response = std::to_string(value);
            std::cout << "*** [socket " << down->handle().id
                      << "] OUTPUT: " << str_response << "\n";
            str_response += '\n';
            down->begin_text_message();
            auto& buf = down->text_message_buffer();
            buf.insert(buf.end(), str_response.begin(), str_response.end());
            down->end_text_message();
          },
          [down](caf::error& err) { down->abort_reason(std::move(err)); });
      // Erase consumed data from the buffer.
      buf_.erase(buf_.begin(), i + 1);
    }
    return static_cast<ptrdiff_t>(text.size());
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume_binary(LowerLayerPtr down, caf::byte_span) {
    // Reject binary input.
    auto err = caf::make_error(caf::sec::runtime_error,
                               "received binary WebSocket frame (unsupported)");
    down->abort_reason(std::move(err));
    return -1;
  }

private:
  // Caches incoming text data until finding a newline character.
  std::vector<char> buf_;

  // Stores a handle to our worker.
  caf::actor worker_;

  // Enables the application to send and receive actor messages.
  caf::net::actor_shell_ptr self_;
};

// -- main ---------------------------------------------------------------------

static constexpr uint16_t default_port = 8080;

struct config : caf::actor_system_config {
  config() {
    opt_group{custom_options_, "global"} //
      .add<uint16_t>("port,p", "port to listen for incoming connections");
  }
};

int caf_main(caf::actor_system& sys, const config& cfg) {
  using namespace caf;
  using namespace caf::net;
  // Open up a TCP port for incoming connections.
  auto port = get_or(cfg, "port", default_port);
  tcp_accept_socket sock;
  if (auto sock_res = make_tcp_accept_socket({ipv4_address{}, port})) {
    std::cout << "*** started listening for incoming connections on port "
              << port << '\n';
    sock = std::move(*sock_res);
  } else {
    std::cerr << "*** unable to open port " << port << ": "
              << to_string(sock_res.error()) << '\n';
    return EXIT_FAILURE;
  }
  // Spawn our worker actor and initiate the protocol stack.
  auto worker = sys.spawn(calculator);
  auto add_conn = [worker](tcp_stream_socket sock, multiplexer* mpx) {
    return make_socket_manager<app, web_socket_server, stream_transport>(
      sock, mpx, worker);
  };
  sys.network_manager().make_acceptor(sock, add_conn);
  return EXIT_SUCCESS;
}

CAF_MAIN(caf::id_block::web_socket_calculator, caf::net::middleman)
