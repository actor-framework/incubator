// This example application connects to a WebSocket server and allows users to
// send arbitrary text.

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
#include "caf/net/web_socket/client.hpp"
#include "caf/scoped_actor.hpp"
#include "caf/stateful_actor.hpp"
#include "caf/tag/mixed_message_oriented.hpp"
#include "caf/type_id.hpp"
#include "caf/typed_event_based_actor.hpp"
#include "caf/uri.hpp"

#include <cstdio>
#include <iostream>

#include <termios.h>
#include <unistd.h>

// -- custom actor and message types -------------------------------------------

CAF_BEGIN_TYPE_ID_BLOCK(web_socket_client, caf::first_custom_type_id)

  CAF_ADD_ATOM(web_socket_client, atom, attach)
  CAF_ADD_ATOM(web_socket_client, atom, binary)
  CAF_ADD_ATOM(web_socket_client, atom, detach)
  CAF_ADD_ATOM(web_socket_client, atom, input)
  CAF_ADD_ATOM(web_socket_client, atom, status)
  CAF_ADD_ATOM(web_socket_client, atom, text)

CAF_END_TYPE_ID_BLOCK(web_socket_client)

using terminal_actor = caf::typed_actor<
  // Attaches the terminal to an actor. After receiving this message, the
  // terminal actor forwards all user input to the attached input.
  caf::result<void>(atom::attach, caf::actor),
  // Releases the terminal again if the sender is the currently attached actor.
  caf::result<void>(atom::detach),
  // Displays binary data received from the server.
  caf::result<void>(atom::binary, caf::byte_buffer),
  // Displays a status message from the attached actor.
  caf::result<void>(atom::status, std::string),
  // Displays text data received from the server.
  caf::result<void>(atom::text, std::string),
  // Handles input from the command line.
  caf::result<void>(atom::input, std::string)>;

// -- implementation of our WebSocket application ------------------------------

class app {
public:
  // -- member types -----------------------------------------------------------

  // Tells CAF we expect a transport with text and binary messages.
  using input_tag = caf::tag::mixed_message_oriented;

  // -- constructors, destructors, and assignment operators --------------------

  app(terminal_actor sink) : sink_(sink) {
    // nop
  }

  template <class LowerLayerPtr>
  caf::error init(caf::net::socket_manager* mgr, LowerLayerPtr down,
                  const caf::settings&) {
    self_ = mgr->make_actor_shell(down);
    self_->set_behavior([down](const std::string& line) {
      down->begin_text_message();
      auto& buf = down->text_message_buffer();
      buf.insert(buf.end(), line.begin(), line.end());
      down->end_text_message();
    });
    self_->send(sink_, atom::attach_v, self_.as_actor());
    std::string str = "established new connection on socket ";
    str += std::to_string(down->handle().id);
    self_->send(sink_, atom::status_v, std::move(str));
    return {};
  }

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
  bool done_sending(LowerLayerPtr down) {
    // The lower level calls this function to check whether we are ready to
    // unregister our socket from send events. We must make sure to put our
    // mailbox in to the blocking state in order to get re-registered once new
    // messages arrive.
    return self_->try_block_mailbox();
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr, const caf::error& reason) {
    std::string str = "app::abort called: ";
    str += to_string(reason);
    self_->send(sink_, atom::status_v, std::move(str));
    self_->send(sink_, atom::detach_v);
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume_text(LowerLayerPtr, caf::string_view text) {
    self_->send(sink_, atom::text_v, std::string{text.begin(), text.end()});
    return static_cast<ptrdiff_t>(text.size());
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume_binary(LowerLayerPtr, caf::byte_span bytes) {
    self_->send(sink_, atom::binary_v,
                caf::byte_buffer{bytes.begin(), bytes.end()});
    return static_cast<ptrdiff_t>(bytes.size());
  }

private:
  // Stores a handle to the actor that controls terminal output.
  terminal_actor sink_;

  // Enables the application to send and receive actor messages.
  caf::net::actor_shell_ptr self_;
};

// -- implementation of the terminal actor -------------------------------------

struct terminal_state {
  static inline const char* name = "terminal";

  terminal_actor::pointer self;

  caf::actor client;

  std::string current_line;

  terminal_state(terminal_actor::pointer self) : self(self) {
    // nop
  }

  void render(const char* str) {
    puts(str);
  }

  void render(const std::string& str) {
    render(str.c_str());
  }

  void render(const caf::byte_buffer& buf) {
    for (auto byte : buf)
      printf("%02x", caf::to_integer<int>(byte));
    putc('\n', stdout);
  }

  void detach() {
    puts("*** server disconnected, bye!");
    self->quit();
  }

  template <class T>
  void render_server_message(const char* prefix, const T& msg) {
    fputs(prefix, stdout);
    render(msg);
  }

  terminal_actor::behavior_type make_behavior() {
    self->set_down_handler([this](const caf::down_msg& dm) {
      if (dm.source == client)
        detach();
    });
    return {
      [this](atom::attach, caf::actor& new_client) {
        render_server_message("[MSG] ", "client up and running");
        client = std::move(new_client);
        self->monitor(client);
      },
      [this](atom::detach) {
        if (self->current_sender() == client)
          detach();
      },
      [this](atom::binary, const caf::byte_buffer& buf) {
        render_server_message("[BIN] ", buf);
      },
      [this](atom::status, const std::string& line) {
        render_server_message("[MSG] ", line);
      },
      [this](atom::text, const std::string& line) {
        render_server_message("[TXT] ", line);
      },
      [this](atom::input, std::string& out) {
        if (client) {
          anon_send(client, std::move(out));
        } else {
          puts("[ERR] not connected to a server");
        }
      },
    };
  }
};

using terminal_impl = terminal_actor::stateful_impl<terminal_state>;

// -- main ---------------------------------------------------------------------

struct config : caf::actor_system_config {
  config() {
    opt_group{custom_options_, "global"} //
      .add<caf::uri>("server,s",
                     "locator for the server, e.g., ws://localhost:8080")
      .add<std::string>(
        "protocols",
        "setting for the optional Sec-WebSocket-Protocol header field")
      .add<std::string>(
        "extensions",
        "setting for the optional Sec-WebSocket-Extensions header field");
  }
};

int main_loop(caf::scoped_actor& self, terminal_actor term) {
  std::string line;
  while (std::getline(std::cin, line)) {
    line.push_back('\n');
    bool fail = false;
    self->request(term, caf::infinite, atom::input_v, line)
      .receive([] { /* ok */ }, [&fail](const caf::error&) { fail = true; });
    if (fail)
      return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

int caf_main(caf::actor_system& sys, const config& cfg) {
  using namespace caf;
  using namespace caf::net;
  auto locator = get_or(cfg, "server", uri{});
  if (locator.empty()) {
    std::cerr << "*** mandatory option 'server' missing, use -h for help\n";
    return EXIT_FAILURE;
  } else if (locator.scheme() != "ws") {
    std::cerr << "*** expected a WebSocket URI (ws://...), use -h for help\n";
    return EXIT_FAILURE;
  } else if (!locator.fragment().empty()) {
    std::cerr << "*** sorry: query and fragment components are not supported\n";
    return EXIT_FAILURE;
  } else if (locator.authority().empty()) {
    std::cerr << "*** authority component missing in URI\n";
    return EXIT_FAILURE;
  } else if (auto sock = make_connected_tcp_stream_socket(locator.authority());
             !sock) {
    std::cerr << "*** failed to connect to " << to_string(locator.authority())
              << ": " << to_string(sock.error()) << '\n';
    return EXIT_FAILURE;
  } else {
    // Fill the WebSocket handshake with the mandatory and optional fields.
    web_socket::handshake hs;
    hs.host(to_string(locator.authority()));
    if (locator.path().empty())
      hs.endpoint("/");
    else
      hs.endpoint(to_string(locator.path()));
    if (auto protocols = get_as<std::string>(cfg, "protocols")) {
      hs.protocols(std::move(*protocols));
    }
    if (auto extensions = get_as<std::string>(cfg, "extensions")) {
      hs.extensions(std::move(*extensions));
    }
    // Spin up actors and enter main loop.
    auto term = sys.spawn<terminal_impl>();
    auto mgr = make_socket_manager<app, web_socket::client, stream_transport>(
      *sock, sys.network_manager().mpx_ptr(), std::move(hs), term);
    if (auto err = mgr->init(content(cfg))) {
      std::cerr << "*** failed to initialize the WebSocket client: "
                << to_string(err) << '\n';
      return EXIT_FAILURE;
    }
    scoped_actor self{sys};
    return main_loop(self, term);
  }
}

CAF_MAIN(caf::id_block::web_socket_client, caf::net::middleman)
