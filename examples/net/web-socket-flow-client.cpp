// This example application connects to a WebSocket server and allows users to
// send arbitrary text.

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/async/iostream.hpp"
#include "caf/byte_span.hpp"
#include "caf/caf_main.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/net/actor_shell.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/web_socket/flow.hpp"
#include "caf/scoped_actor.hpp"
#include "caf/stateful_actor.hpp"
#include "caf/type_id.hpp"
#include "caf/typed_event_based_actor.hpp"
#include "caf/uri.hpp"

#include <cstdio>
#include <iostream>

// -- custom actor and message types -------------------------------------------

CAF_BEGIN_TYPE_ID_BLOCK(web_socket_flow_client, caf::first_custom_type_id)

  CAF_ADD_ATOM(web_socket_flow_client, atom, attach)
  CAF_ADD_ATOM(web_socket_flow_client, atom, binary)
  CAF_ADD_ATOM(web_socket_flow_client, atom, detach)
  CAF_ADD_ATOM(web_socket_flow_client, atom, input)
  CAF_ADD_ATOM(web_socket_flow_client, atom, status)
  CAF_ADD_ATOM(web_socket_flow_client, atom, text)

CAF_END_TYPE_ID_BLOCK(web_socket_flow_client)

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

// Converts text and binary messages from the WebSocket to CAF messages.
class reader {
  using value_type = caf::message;

  bool deserialize_text(caf::string_view text, caf::message& msg) {
    msg = caf::make_message(atom::text, std::string{text.begin(), text.end()});
    return true;
  }

  bool deserialize_binary(caf::byte_span bytes, caf::message& msg) {
    msg = caf::make_message(atom::binary,
                            caf::byte_buffer{bytes.begin(), bytes.end()});
    return true;
  }

  caf::error init(const settings&) {
    return caf::none;
  }
};

class writer {
  using value_type = caf::message;

  bool is_text_message(const caf::message&) {
    return true;
  }

  bool serialize_text(const caf::message& msg, std::vector<char>& buf) {
    if (auto view = make_const_typed_message_view<std::string>(msg)) {
      auto& str = caf::get<0>(view);
      buf.assign(str.begin(), str.end());
    } else {
      return false;
    }
  }

  bool serialize_binary(const caf::message& msg, caf::byte_buffer& buf) {
    return false;
  }

  caf::error init(const settings&) {
    return caf::none;
  }
};

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
  // Forward whatever lines we read from std::cin.
  auto lines = async::cin_line_reader(sys, [](const std::string& line) {
    return caf::make_message(line);
  });
  if (auto pub = web_socket::flow_connect_bidir(sys, content(cfg), locator,
                                                lines, reader{}, writer{})) {
    // Print whatever we read from the WebSocket to std::cout.
    pub->subscribe(async::cout_line_writer<message>(sys));
  } else {
    std::cerr << "*** failed to connect to " << to_string(locator) << ": "
              << to_string(pub.error()) << '\n';
    return EXIT_FAILURE;
  }
}

CAF_MAIN(caf::id_block::web_socket_flow_client, caf::net::middleman)
