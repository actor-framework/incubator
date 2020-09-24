/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2019 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#define CAF_SUITE basp.application

#include "caf/net/basp/application.hpp"

#include "caf/test/dsl.hpp"

#include <vector>

#include "caf/byte_buffer.hpp"
#include "caf/forwarding_actor_proxy.hpp"
#include "caf/net/basp/connection_state.hpp"
#include "caf/net/basp/constants.hpp"
#include "caf/net/basp/ec.hpp"
#include "caf/net/middleman.hpp"
#include "caf/uri.hpp"

#include "caf/net/length_prefix_framing.hpp"

using namespace caf;
using namespace caf::net;

#define REQUIRE_OK(statement)                                                  \
  if (auto err = statement)                                                    \
    CAF_FAIL("failed to serialize data: " << err);

namespace {

struct dummy_socket_manager : public socket_manager {
  dummy_socket_manager(socket handle, multiplexer* mpx)
    : socket_manager(handle, mpx) {
    // nop
  }

  error init(const settings&) override {
    return none;
  }

  bool handle_read_event() override {
    return false;
  }

  bool handle_write_event() override {
    return false;
  }

  void handle_error(sec) override {
    // nop
  }
};

struct config : actor_system_config {
  config() {
    net::middleman::add_module_options(*this);
  }
};

struct fixture : test_coordinator_fixture<config>,
                 proxy_registry::backend,
                 basp::application::test_tag {
  fixture() : mm(sys), mpx(&mm), proxies(sys, *this), app(proxies) {
    dummy_socket_manager dummy_mgr{socket{42}, &mpx};
    settings cfg;
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, this);
    REQUIRE_OK(app.init(&dummy_mgr, this_layer_ptr, cfg));
    uri mars_uri;
    REQUIRE_OK(parse("tcp://mars", mars_uri));
    mars = make_node_id(mars_uri);
  }

  template <class... Ts>
  byte_buffer to_buf(const Ts&... xs) {
    byte_buffer buf;
    binary_serializer sink{sys, buf};
    REQUIRE_OK(!sink.apply_objects(xs...));
    return buf;
  }

  template <class... Ts>
  void set_input(const Ts&... xs) {
    input = to_buf(xs...);
  }

  void handle_handshake() {
    CAF_CHECK_EQUAL(app.state(), basp::connection_state::await_handshake);
    auto app_ids = basp::application::default_app_ids();
    set_input(basp::header{basp::message_type::handshake, basp::version}, mars,
              app_ids);
    CAF_MESSAGE("set_input done");
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, this);
    CAF_REQUIRE_GREATER_OR_EQUAL(app.consume(this_layer_ptr, input), 0);
    CAF_CHECK_EQUAL(app.state(), basp::connection_state::ready);
  }

  void consume_handshake() {
    if (output.size() < basp::header_size)
      CAF_FAIL("BASP application did not write a handshake header");
    auto hdr = basp::header::from_bytes(output);
    if (hdr.type != basp::message_type::handshake
        || hdr.operation_data != basp::version)
      CAF_FAIL("invalid handshake header");
    node_id nid;
    std::vector<std::string> app_ids;
    binary_deserializer source{sys, output};
    source.skip(basp::header_size);
    if (!source.apply_objects(nid, app_ids))
      CAF_FAIL("unable to deserialize payload: " << source.get_error());
    if (source.remaining() > 0)
      CAF_FAIL("trailing bytes after reading payload");
    output.clear();
  }

  template <class LowerLayerPtr>
  void begin_message(LowerLayerPtr&) {
    // nop
  }

  template <class LowerLayerPtr>
  byte_buffer& message_buffer(LowerLayerPtr&) {
    return output;
  }

  template <class LowerLayerPtr>
  void end_message(LowerLayerPtr&) {
    // nop
  }

  template <class... Ts>
  void configure_read(Ts...) {
    // nop
  }

  template <class LowerLayerPtr>
  void abort_reason(LowerLayerPtr&, const error& err) {
    last_error = err;
  }

  strong_actor_ptr make_proxy(node_id nid, actor_id aid) override {
    using impl_type = forwarding_actor_proxy;
    using hdl_type = strong_actor_ptr;
    actor_config cfg;
    return make_actor<impl_type, hdl_type>(aid, nid, &sys, cfg, self);
  }

  void set_last_hop(node_id*) override {
    // nop
  }

protected:
  middleman mm;

  multiplexer mpx;

  byte_buffer input;

  byte_buffer output;

  node_id mars;

  proxy_registry proxies;

  basp::application app;

  error last_error;
};

} // namespace

#define MOCK(kind, op, ...)                                                    \
  do {                                                                         \
    set_input(basp::header{kind, op}, __VA_ARGS__);                            \
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, this);         \
    if (app.consume(this_layer_ptr, input) < 0)                                \
      CAF_FAIL(                                                                \
        "application-under-test failed to process message: " << last_error);   \
  } while (false)

#define RECEIVE(msg_type, op_data, ...)                                        \
  do {                                                                         \
    binary_deserializer source{sys, output};                                   \
    basp::header hdr;                                                          \
    if (!source.apply_objects(hdr, __VA_ARGS__))                               \
      CAF_FAIL("failed to receive data: " << source.get_error());              \
    if (source.remaining() != 0)                                               \
      CAF_FAIL("unable to read entire message, " << source.remaining()         \
                                                 << " bytes left in buffer");  \
    CAF_CHECK_EQUAL(hdr.type, msg_type);                                       \
    CAF_CHECK_EQUAL(hdr.operation_data, op_data);                              \
    output.clear();                                                            \
  } while (false)

CAF_TEST_FIXTURE_SCOPE(application_tests, fixture)

CAF_TEST(missing handshake) {
  CAF_CHECK_EQUAL(app.state(), basp::connection_state::await_handshake);
  set_input(basp::header{basp::message_type::heartbeat, 0});
  auto this_layer_ptr = make_message_oriented_layer_ptr(this, this);
  CAF_CHECK_LESS(app.consume(this_layer_ptr, input), 0);
  CAF_CHECK_EQUAL(last_error, basp::ec::missing_handshake);
}

CAF_TEST(version mismatch) {
  CAF_CHECK_EQUAL(app.state(), basp::connection_state::await_handshake);
  set_input(basp::header{basp::message_type::handshake, 0});
  auto this_layer_ptr = make_message_oriented_layer_ptr(this, this);
  CAF_CHECK_LESS(app.consume(this_layer_ptr, input), 0);
  CAF_CHECK_EQUAL(last_error, basp::ec::version_mismatch);
}

CAF_TEST(invalid handshake) {
  CAF_CHECK_EQUAL(app.state(), basp::connection_state::await_handshake);
  node_id no_nid;
  std::vector<std::string> no_ids;
  set_input(basp::header{basp::message_type::handshake, basp::version}, no_nid,
            no_ids);
  auto this_layer_ptr = make_message_oriented_layer_ptr(this, this);
  CAF_CHECK_LESS(app.consume(this_layer_ptr, input), 0);
  CAF_CHECK_EQUAL(last_error, basp::ec::invalid_handshake);
}

CAF_TEST(app identifier mismatch) {
  CAF_CHECK_EQUAL(app.state(), basp::connection_state::await_handshake);
  std::vector<std::string> wrong_ids{"YOLO!!!"};
  set_input(basp::header{basp::message_type::handshake, basp::version}, mars,
            wrong_ids);
  auto this_layer_ptr = make_message_oriented_layer_ptr(this, this);
  CAF_CHECK_LESS(app.consume(this_layer_ptr, input), 0);
  CAF_CHECK_EQUAL(last_error, basp::ec::app_identifiers_mismatch);
}

CAF_TEST(repeated handshake) {
  handle_handshake();
  consume_handshake();
  CAF_CHECK_EQUAL(app.state(), basp::connection_state::ready);
  node_id no_nid;
  std::vector<std::string> no_ids;
  set_input(basp::header{basp::message_type::handshake, basp::version}, no_nid,
            no_ids);
  auto this_layer_ptr = make_message_oriented_layer_ptr(this, this);
  CAF_CHECK_LESS(app.consume(this_layer_ptr, input), 0);
  CAF_CHECK_EQUAL(last_error, basp::ec::unexpected_handshake);
}

CAF_TEST(actor message) {
  handle_handshake();
  consume_handshake();
  sys.registry().put(self->id(), self);
  CAF_REQUIRE_EQUAL(self->mailbox().size(), 0u);
  MOCK(basp::message_type::actor_message, make_message_id().integer_value(),
       mars, actor_id{42}, self->id(), std::vector<strong_actor_ptr>{},
       make_message("hello world!"));
  expect((monitor_atom, strong_actor_ptr), from(_).to(self));
  expect((std::string), from(_).to(self).with("hello world!"));
}

CAF_TEST(resolve request without result) {
  handle_handshake();
  consume_handshake();
  CAF_CHECK_EQUAL(app.state(), basp::connection_state::ready);
  MOCK(basp::message_type::resolve_request, 42, std::string{"foo/bar"});
  actor_id aid;
  std::set<std::string> ifs;
  RECEIVE(basp::message_type::resolve_response, 42u, aid, ifs);
  CAF_CHECK_EQUAL(aid, 0u);
  CAF_CHECK(ifs.empty());
}

CAF_TEST(resolve request on id with result) {
  handle_handshake();
  consume_handshake();
  sys.registry().put(self->id(), self);
  auto path = "id/" + std::to_string(self->id());
  CAF_CHECK_EQUAL(app.state(), basp::connection_state::ready);
  MOCK(basp::message_type::resolve_request, 42, path);
  actor_id aid;
  std::set<std::string> ifs;
  RECEIVE(basp::message_type::resolve_response, 42u, aid, ifs);
  CAF_CHECK_EQUAL(aid, self->id());
  CAF_CHECK(ifs.empty());
}

CAF_TEST(resolve request on name with result) {
  handle_handshake();
  consume_handshake();
  sys.registry().put("foo", self);
  std::string path = "name/foo";
  CAF_CHECK_EQUAL(app.state(), basp::connection_state::ready);
  MOCK(basp::message_type::resolve_request, 42, path);
  actor_id aid;
  std::set<std::string> ifs;
  RECEIVE(basp::message_type::resolve_response, 42u, aid, ifs);
  CAF_CHECK_EQUAL(aid, self->id());
  CAF_CHECK(ifs.empty());
}

CAF_TEST(resolve response with invalid actor handle) {
  handle_handshake();
  consume_handshake();
  CAF_CHECK_EQUAL(app.state(), basp::connection_state::ready);
  CAF_CHECK_EQUAL(app.resolve("foo/bar", self), none);
  std::string path;
  RECEIVE(basp::message_type::resolve_request, 1u, path);
  CAF_CHECK_EQUAL(path, "foo/bar");
  actor_id aid = 0;
  std::set<std::string> ifs;
  MOCK(basp::message_type::resolve_response, 1u, aid, ifs);
  self->receive([&](strong_actor_ptr& hdl, std::set<std::string>& hdl_ifs) {
    CAF_CHECK_EQUAL(hdl, nullptr);
    CAF_CHECK_EQUAL(ifs, hdl_ifs);
  });
}

CAF_TEST(resolve response with valid actor handle) {
  handle_handshake();
  consume_handshake();
  CAF_CHECK_EQUAL(app.state(), basp::connection_state::ready);
  CAF_CHECK_EQUAL(app.resolve("foo/bar", self), none);
  std::string path;
  RECEIVE(basp::message_type::resolve_request, 1u, path);
  CAF_CHECK_EQUAL(path, "foo/bar");
  actor_id aid = 42;
  std::set<std::string> ifs;
  MOCK(basp::message_type::resolve_response, 1u, aid, ifs);
  self->receive([&](strong_actor_ptr& hdl, std::set<std::string>& hdl_ifs) {
    CAF_REQUIRE(hdl != nullptr);
    CAF_CHECK_EQUAL(ifs, hdl_ifs);
    CAF_CHECK_EQUAL(hdl->id(), aid);
  });
}

CAF_TEST(heartbeat message) {
  handle_handshake();
  consume_handshake();
  CAF_CHECK_EQUAL(app.state(), basp::connection_state::ready);
  auto bytes = to_bytes(basp::header{basp::message_type::heartbeat, 0});
  set_input(bytes);
  auto this_layer_ptr = make_message_oriented_layer_ptr(this, this);
  CAF_REQUIRE_GREATER(app.consume(this_layer_ptr, input), 0);
  CAF_CHECK_EQUAL(last_error, none);
}

CAF_TEST_FIXTURE_SCOPE_END()
