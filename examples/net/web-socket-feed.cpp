// This example application wraps a simple feed actor and allows clients to
// "subscribe" to it via WebSocket connection.
//
// To run the server at port 4242 (defaults to 8080):
//
// ~~~
// web-socket-calculator -p 4242
// ~~~
//
// Once started, the application waits for incoming WebSocket connections and
// sends it randomly-generated stock infos. A simple WebSocket client written in
// Python could look as follows:
//
// ~~~(py)
// #!/usr/bin/env python
//
// import asyncio
// import websockets
//
// async def run():
//     uri = "ws://localhost:8080"
//     async with websockets.connect(uri) as websocket:
//         while True:
//             info = await websocket.recv()
//             print(f"{info}")
//
// asyncio.get_event_loop().run_until_complete(run())
// ~~~

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/byte_span.hpp"
#include "caf/caf_main.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/net/actor_shell.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/net/tcp_accept_socket.hpp"
#include "caf/net/web_socket/server.hpp"
#include "caf/stateful_actor.hpp"
#include "caf/tag/mixed_message_oriented.hpp"
#include "caf/typed_event_based_actor.hpp"

#include <cstdint>

// -- custom message types -----------------------------------------------------

// Usually, we prefer atoms to prefix certain operations. However, using custom
// message types provides a nicer interface for the text-based WebSocket
// communication.

namespace stock {

struct info {
  std::string symbol;
  std::string currency;
  double current;
  double open;
  double high;
  double low;
};

template <class Inspector>
bool inspect(Inspector& f, info& x) {
  return f.object(x).fields(f.field("symbol", x.symbol),
                            f.field("currency", x.currency),
                            f.field("open", x.open), f.field("high", x.high),
                            f.field("low", x.low));
}

using listener = caf::typed_actor<caf::result<void>(info)>;

using feed = caf::typed_actor<caf::result<void>(caf::subscribe_atom, listener)>;

} // namespace stock

CAF_BEGIN_TYPE_ID_BLOCK(web_socket_feed, caf::first_custom_type_id)

  // -- message types ----------------------------------------------------------

  CAF_ADD_TYPE_ID(web_socket_feed, (stock::info))

  // -- actor interfaces -------------------------------------------------------

  CAF_ADD_TYPE_ID(web_socket_feed, (stock::listener))
  CAF_ADD_TYPE_ID(web_socket_feed, (stock::feed))

CAF_END_TYPE_ID_BLOCK(web_socket_feed)

// -- implementation of the feed actor -----------------------------------------

using random_feed = stock::feed::extend<caf::result<void>(caf::update_atom)>;

struct random_feed_state {
  random_feed_state() : val_dist(0, 100000), index_dist(0, 19) {
    // Init random number generator.
    std::random_device rd;
    rng.seed(rd());
    // Fill vector with some stuff.
    for (size_t i = 0; i < 20; ++i) {
      std::uniform_int_distribution<int> char_dist{'A', 'Z'};
      std::string symbol;
      for (size_t j = 0; j < 5; ++j)
        symbol += static_cast<char>(char_dist(rng));
      auto val = next_value();
      infos.emplace_back(
        stock::info{std::move(symbol), "USD", val, val, val, val});
    }
  }

  // Picks a random stock, assigns a new value to it, and returns it.
  stock::info& update() {
    auto& x = infos[index_dist(rng)];
    auto val = next_value();
    x.current = val;
    x.high = std::max(x.high, val);
    x.low = std::min(x.high, val);
    return x;
  }

  double next_value() {
    return val_dist(rng) / 100.0;
  }

  std::vector<stock::info> infos;
  std::vector<stock::listener> listeners;
  std::minstd_rand rng;
  std::uniform_int_distribution<int> val_dist;
  std::uniform_int_distribution<size_t> index_dist;
};

random_feed::behavior_type
feed_impl(random_feed::stateful_pointer<random_feed_state> self) {
  self->set_down_handler([self](const caf::down_msg& dm) {
    auto is_addr = [&dm](const auto& hdl) { return hdl == dm.source; };
    auto& xs = self->state.listeners;
    xs.erase(std::remove_if(xs.begin(), xs.end(), is_addr), xs.end());
    std::cout << "Removed listener from feed.\n";
  });
  self->delayed_send(self, std::chrono::seconds(1), caf::update_atom_v);
  return {
    [self](caf::update_atom) {
      auto& st = self->state;
      auto& x = st.update();
      for (auto& listener : st.listeners)
        self->send(listener, x);
      self->delayed_send(self, std::chrono::seconds(1), caf::update_atom_v);
    },
    [self](caf::subscribe_atom, stock::listener hdl) {
      std::cout << "Added new listener to feed.\n";
      self->monitor(hdl);
      auto& st = self->state;
      st.listeners.emplace_back(hdl);
      for (auto& info : st.infos)
        self->send(hdl, info);
    },
  };
}

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

  explicit app(stock::feed feed) : feed_(std::move(feed)) {
    // nop
  }

  template <class LowerLayerPtr>
  caf::error init(caf::net::socket_manager* mgr, LowerLayerPtr down,
                  const caf::settings&) {
    // Create the actor-shell wrapper for this application.
    self_ = mgr->make_actor_shell(down);
    self_->set_behavior([down](stock::info& x) {
      // Send stock info object as JSON-style text.
      down->begin_text_message();
      auto& buf = down->text_message_buffer();
      // This utility creates a lot of unnecessary heap-allocated strings. Good
      // enough for an example, but not efficient enough for production.
      auto append = [&buf](std::initializer_list<std::string> strs) {
        for (auto& str : strs)
          buf.insert(buf.end(), str.begin(), str.end());
      };
      append({"{\n"});
      append({"  symbol: \"", x.symbol, "\",\n"});
      append({"  currency: \"", x.currency, "\",\n"});
      append({"  current: ", std::to_string(x.current), ",\n"});
      append({"  open: ", std::to_string(x.open), ",\n"});
      append({"  high: ", std::to_string(x.high), ",\n"});
      append({"  low: ", std::to_string(x.low), "\n"});
      append({"}\n"});
      down->end_text_message();
    });
    // We promise to implement the listener interface and register at the feed.
    auto self_hdl = caf::actor_cast<stock::listener>(self_.as_actor());
    self_->send(feed_, caf::subscribe_atom_v, self_hdl);
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
  void abort(LowerLayerPtr, const caf::error&) {
    // nop
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume_text(LowerLayerPtr, caf::string_view text) {
    // Discard any input.
    return static_cast<ptrdiff_t>(text.size());
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume_binary(LowerLayerPtr, caf::byte_span bytes) {
    // Discard any input.
    return static_cast<ptrdiff_t>(bytes.size());
  }

private:
  // Stores a handle to our worker.
  stock::feed feed_;

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
  // Spawn our feed actor and initiate the protocol stack.
  stock::feed feed = sys.spawn(feed_impl);
  auto add_conn = [feed](tcp_stream_socket sock, multiplexer* mpx) {
    return make_socket_manager<app, web_socket::server, stream_transport>(
      sock, mpx, feed);
  };
  sys.network_manager().make_acceptor(sock, add_conn);
  return EXIT_SUCCESS;
}

CAF_MAIN(caf::id_block::web_socket_feed, caf::net::middleman)
