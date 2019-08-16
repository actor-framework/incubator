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

#define CAF_SUITE scribe_policy

#include "caf/policy/scribe.hpp"

#include "caf/binary_deserializer.hpp"
#include "caf/byte.hpp"
#include "caf/detail/scope_guard.hpp"
#include "caf/make_actor.hpp"
#include "caf/net/actor_proxy_impl.hpp"
#include "caf/net/endpoint_manager.hpp"
#include "caf/net/make_endpoint_manager.hpp"
#include "caf/net/multiplexer.hpp"
#include "caf/net/stream_socket.hpp"
#include "caf/serializer_impl.hpp"
#include "caf/test/dsl.hpp"
#include "host_fixture.hpp"

using namespace caf;
using namespace caf::net;

namespace {

constexpr string_view hello_manager = "hello manager!";

struct fixture : test_coordinator_fixture<>, host_fixture {
  fixture() {
    mpx = std::make_shared<multiplexer>();
    if (auto err = mpx->init())
      CAF_FAIL("mpx->init failed: " << sys.render(err));
  }

  bool handle_io_event() override {
    mpx->handle_updates();
    return mpx->poll_once(false);
  }

  multiplexer_ptr mpx;
};

class dummy_application {
public:
  dummy_application(std::shared_ptr<std::vector<byte>> rec_buf)
    : rec_buf_(std::move(rec_buf)){
      // nop
    };

  ~dummy_application() = default;

  template <class Parent>
  error init(Parent&) {
    return none;
  }

  template <class Transport>
  void write_message(Transport& transport,
                     std::unique_ptr<endpoint_manager::message> msg) {
    transport.write_packet(msg->payload);
  }

  template <class Parent>
  void handle_data(Parent&, span<byte> data) {
    rec_buf_->clear();
    rec_buf_->insert(rec_buf_->begin(), data.begin(), data.end());
  }

  template <class Manager>
  void resolve(Manager& manager, const std::string& path, actor listener) {
    actor_id aid = 42;
    auto hid = "0011223344556677889900112233445566778899";
    auto nid = unbox(make_node_id(42, hid));
    actor_config cfg;
    auto p = make_actor<actor_proxy_impl, strong_actor_ptr>(aid, nid,
                                                            &manager.system(),
                                                            cfg, &manager);
    anon_send(listener, resolve_atom::value, std::move(path), p);
  }

  template <class Transport>
  void timeout(Transport&, atom_value, uint64_t) {
    // nop
  }

  void handle_error(sec) {
    // nop
  }

  static expected<std::vector<byte>> serialize(actor_system& sys,
                                               const type_erased_tuple& x) {
    std::vector<byte> result;
    serializer_impl<std::vector<byte>> sink{sys, result};
    if (auto err = message::save(sink, x))
      return err;
    return result;
  }

private:
  std::shared_ptr<std::vector<byte>> rec_buf_;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(endpoint_manager_tests, fixture)

CAF_TEST(receive) {
  std::vector<byte> read_buf(1024);
  CAF_CHECK_EQUAL(mpx->num_socket_managers(), 1u);
  auto buf = std::make_shared<std::vector<byte>>();
  auto sockets = unbox(make_stream_socket_pair());
  nonblocking(sockets.second, true);
  CAF_CHECK_EQUAL(read(sockets.second, read_buf.data(), read_buf.size()),
                  sec::unavailable_or_would_block);
  auto guard = detail::make_scope_guard([&] { close(sockets.second); });
  CAF_MESSAGE("configure scribe_policy");
  policy::scribe scribe{sockets.first};
  scribe.configure_read(net::receive_policy::exactly(hello_manager.size()));
  auto mgr = make_endpoint_manager(mpx, sys, scribe, dummy_application{buf});
  CAF_CHECK_EQUAL(mgr->init(), none);
  mpx->handle_updates();
  CAF_CHECK_EQUAL(mpx->num_socket_managers(), 2u);
  CAF_MESSAGE("sending data to scribe_policy");
  CAF_CHECK_EQUAL(write(sockets.second, hello_manager.data(),
                        hello_manager.size()),
                  hello_manager.size());
  run();
  CAF_CHECK_EQUAL(string_view(reinterpret_cast<char*>(buf->data()),
                              buf->size()),
                  hello_manager);
}

CAF_TEST(resolve and proxy communication) {
  std::vector<byte> read_buf(1024);
  auto buf = std::make_shared<std::vector<byte>>();
  auto sockets = unbox(make_stream_socket_pair());
  nonblocking(sockets.second, true);
  auto guard = detail::make_scope_guard([&] { close(sockets.second); });
  auto mgr = make_endpoint_manager(mpx, sys, policy::scribe{sockets.first},
                                   dummy_application{buf});
  CAF_CHECK_EQUAL(mgr->init(), none);
  mpx->handle_updates();
  run();
  mgr->resolve("/id/42", self);
  run();
  self->receive(
    [&](resolve_atom, const std::string&, const strong_actor_ptr& p) {
      CAF_MESSAGE("got a proxy, send a message to it");
      self->send(actor_cast<actor>(p), "hello proxy!");
    },
    after(std::chrono::seconds(0)) >>
      [&] { CAF_FAIL("manager did not respond with a proxy."); });
  run();
  auto read_res = read(sockets.second, read_buf.data(), read_buf.size());
  if (!holds_alternative<size_t>(read_res))
    CAF_FAIL("read() returned an error: " << sys.render(get<sec>(read_res)));
  read_buf.resize(get<size_t>(read_res));
  CAF_MESSAGE("receive buffer contains " << read_buf.size() << " bytes");
  message msg;
  binary_deserializer source{sys, read_buf};
  CAF_CHECK_EQUAL(source(msg), none);
  if (msg.match_elements<std::string>())
    CAF_CHECK_EQUAL(msg.get_as<std::string>(0), "hello proxy!");
  else
    CAF_ERROR("expected a string, got: " << to_string(msg));
}

CAF_TEST_FIXTURE_SCOPE_END()