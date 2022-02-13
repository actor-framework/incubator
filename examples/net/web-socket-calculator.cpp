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
// line1 = '{ "@type": "addition", "x": 17, "y": 8 }\n'
// line2 = '{ "@type": "subtraction", "x": 17, "y": 8 }\n'
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
#include "caf/byte_span.hpp"
#include "caf/caf_main.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/json_reader.hpp"
#include "caf/json_writer.hpp"
#include "caf/mtl.hpp"
#include "caf/net/actor_shell.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/net/tcp_accept_socket.hpp"
#include "caf/net/web_socket/server.hpp"
#include "caf/tag/mixed_message_oriented.hpp"
#include "caf/typed_actor.hpp"
#include "caf/typed_event_based_actor.hpp"

#include <cstdint>

// -- custom actor and message types -------------------------------------------

// Internal interface to the worker.
using calculator_actor
  = caf::typed_actor<caf::result<int32_t>(caf::add_atom, int32_t, int32_t),
                     caf::result<int32_t>(caf::sub_atom, int32_t, int32_t)>;

// Utility for assigning JSON "type names" to CAF atoms.
template <class Atom>
struct json_name;

template <>
struct json_name<caf::add_atom> {
  static constexpr caf::string_view value = "addition";
};

template <>
struct json_name<caf::sub_atom> {
  static constexpr caf::string_view value = "subtraction";
};

// Internally, we use atom-prefixed message. Externally, we exchange JSON
// objects over a WebSocket. This adapter translates between an external client
// and a `calculator_actor`.
struct adapter {
  // The `calculator_actor` always receives three arguments: one atom (add_atom
  // or sub_atom) and two integers (x and y). When reading from the inspector,
  // we require an object with a matching type name that has two fields: x and
  // y. When used with a `json_reader`, the format we accept from clients is:
  // `{"@type": <addition-or-subtraction>, "x": <int32>, "y": <int32>}`.
  template <class Inspector, class Atom>
  bool read(Inspector& f, Atom, int32_t& x, int32_t& y) const {
    auto type_annotation = json_name<Atom>::value;
    return f.assert_next_object_name(type_annotation)
           && f.virtual_object(type_annotation)
                .fields(f.field("x", x), f.field("y", y));
  }

  // The `calculator_actor` always returns an integer result. We simply apply
  // this to the inspector. For a `json_writer`, this simply converts the
  // integer to a string.
  template <class Inspector>
  bool write(Inspector& f, int32_t result) const {
    return f.apply(result);
  }
};

// -- implementation of the calculator actor -----------------------------------

struct calculator_state {
  static inline const char* name = "calculator";

  calculator_actor::behavior_type make_behavior() {
    return {
      [](caf::add_atom, int32_t x, int32_t y) { return x + y; },
      [](caf::sub_atom, int32_t x, int32_t y) { return x - y; },
    };
  }
};

using calculator_impl = calculator_actor::stateful_impl<calculator_state>;

// -- implementation of the WebSocket application ------------------------------

class app {
public:
  // -- member types -----------------------------------------------------------

  // Tells CAF we expect a transport with text and binary messages.
  using input_tag = caf::tag::mixed_message_oriented;

  // -- constants --------------------------------------------------------------

  // Restricts our buffer to a maximum of 1MB.
  static constexpr size_t max_buf_size = 1024 * 1024;

  // -- constructors, destructors, and assignment operators --------------------

  explicit app(calculator_actor worker) : worker_(std::move(worker)) {
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
  static void send_error(LowerLayerPtr down, const caf::error& err) {
    auto str = to_string(err);
    down->begin_text_message();
    auto& buf = down->text_message_buffer();
    buf.insert(buf.end(), str.begin(), str.end());
    down->end_text_message();
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
      // Deserialize config value / message from received line.
      auto num_bytes = std::distance(buf_.begin(), i) + 1;
      caf::string_view line{buf_.data(), static_cast<size_t>(num_bytes) - 1};
      std::cout << "*** [socket " << down->handle().id << "] INPUT: " << line
                << "\n";
      if (reader.load(line)) {
        auto on_result = [this, down](auto&... xs) {
          // Simply respond with the value as string, wrapped into a WebSocket
          // text message frame.
          writer.reset();
          if (!adapter{}.write(writer, xs...)) {
            std::cerr << "*** [socket " << down->handle().id
                      << "] failed to generate JSON response: "
                      << to_string(writer.get_error()) << "\n";
            down->abort_reason(caf::sec::runtime_error);
            return;
          }
          auto str_response = writer.str();
          std::cout << "*** [socket " << down->handle().id
                    << "] OUTPUT: " << str_response << "\n";
          down->begin_text_message();
          auto& buf = down->text_message_buffer();
          buf.insert(buf.end(), str_response.begin(), str_response.end());
          down->end_text_message();
        };
        auto on_error = [down](caf::error& err) {
          send_error(down, err);
          down->abort_reason(std::move(err));
        };
        auto mtl = caf::make_mtl(self_.get(), adapter{}, &reader);
        if (!mtl.try_request(worker_, std::chrono::seconds(1), on_result,
                             on_error)) {
          std::cerr << "*** [socket " << down->handle().id
                    << "] unable to deserialize a message from the received "
                       "JSON line\n";
          auto reason = make_error(caf::sec::runtime_error,
                                   "found no matching handler");
          send_error(down, reason);
        }
      } else {
        std::cerr << "*** [socket " << down->handle().id
                  << "] unable to parse received JSON line\n";
        send_error(down, reader.get_error());
      }
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
  calculator_actor worker_;

  // Enables the application to send and receive actor messages.
  caf::net::actor_shell_ptr self_;

  // Enables us to read JSON input from a WebSocket connection.
  caf::json_reader reader;

  // Enables us to write JSON output to the WebSocket connection.
  caf::json_writer writer;
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
  auto worker = sys.spawn<calculator_impl>();
  auto add_conn = [worker](tcp_stream_socket sock, multiplexer* mpx) {
    using stack_t = stream_transport<web_socket::server<app>>;
    return make_socket_manager<stack_t>(sock, mpx, worker);
  };
  sys.network_manager().make_acceptor(sock, add_conn);
  return EXIT_SUCCESS;
}

CAF_MAIN(caf::net::middleman)
