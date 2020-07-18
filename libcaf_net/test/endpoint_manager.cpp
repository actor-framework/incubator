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

#define CAF_SUITE endpoint_manager

#include "caf/net/endpoint_manager.hpp"

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/detail/scope_guard.hpp"
#include "caf/make_actor.hpp"
#include "caf/net/actor_proxy_impl.hpp"
#include "caf/net/make_endpoint_manager.hpp"
#include "caf/net/multiplexer.hpp"
#include "caf/net/stream_socket.hpp"
#include "caf/node_id.hpp"
#include "caf/span.hpp"

using namespace caf;
using namespace caf::net;
using namespace std::chrono_literals;

namespace {

using byte_buffer_ptr = std::shared_ptr<byte_buffer>;

string_view hello_manager{"hello manager!"};

string_view hello_test{"hello test!"};

struct fixture : test_coordinator_fixture<>, host_fixture {
  fixture() : timeout_set(false), timeout_triggered(false) {
    mpx = std::make_shared<multiplexer>();
    mpx->set_thread_id();
    if (auto err = mpx->init())
      CAF_FAIL("mpx->init failed: " << err);
    if (mpx->num_socket_managers() != 1)
      CAF_FAIL("mpx->num_socket_managers() != 1");
  }

  bool handle_io_event() override {
    return mpx->poll_once(false);
  }

  multiplexer_ptr mpx;

  bool timeout_set;

  bool timeout_triggered;
};

class dummy_application {
  // nop
};

class dummy_transport {
public:
  using application_type = dummy_application;

  dummy_transport(stream_socket handle, byte_buffer_ptr data, bool& timeout_set,
                  bool& timeout_triggered)
    : handle_(handle),
      data_(data),
      read_buf_(1024),
      timeout_set_(timeout_set),
      timeout_triggered_(timeout_triggered) {
    // nop
  }

  stream_socket handle() {
    return handle_;
  }

  template <class Manager>
  error init(Manager& manager) {
    auto test_bytes = as_bytes(make_span(hello_test));
    buf_.insert(buf_.end(), test_bytes.begin(), test_bytes.end());
    manager.register_writing();
    return none;
  }

  template <class Manager>
  bool handle_read_event(Manager&) {
    auto res = read(handle_, read_buf_);
    if (auto num_bytes = get_if<size_t>(&res)) {
      data_->insert(data_->end(), read_buf_.begin(),
                    read_buf_.begin() + *num_bytes);
      return true;
    }
    return get<sec>(res) == sec::unavailable_or_would_block;
  }

  template <class Manager>
  bool handle_write_event(Manager& mgr) {
    for (auto x = mgr.next_message(); x != nullptr; x = mgr.next_message()) {
      binary_serializer sink{mgr.system(), buf_};
      if (auto err = sink(x->msg->payload))
        CAF_FAIL("serializing failed: " << err);
    }
    auto res = write(handle_, buf_);
    if (auto num_bytes = get_if<size_t>(&res)) {
      buf_.erase(buf_.begin(), buf_.begin() + *num_bytes);
      return buf_.size() > 0;
    }
    return get<sec>(res) == sec::unavailable_or_would_block;
  }

  void handle_error(sec) {
    // nop
  }

  template <class Manager>
  void resolve(Manager& mgr, const uri& locator, const actor& listener) {
    actor_id aid = 42;
    auto hid = string_view("0011223344556677889900112233445566778899");
    auto nid = unbox(make_node_id(42, hid));
    actor_config cfg;
    auto p = make_actor<actor_proxy_impl, strong_actor_ptr>(
      aid, nid, &mgr.system(), cfg, &mgr);
    std::string path{locator.path().begin(), locator.path().end()};
    anon_send(listener, resolve_atom_v, std::move(path), p);
  }

  template <class Manager>
  void timeout(Manager&, const std::string& tag, uint64_t timeout_id) {
    CAF_MESSAGE("timeout triggered " << CAF_ARG(tag) << " "
                                     << CAF_ARG(timeout_id));
    timeout_triggered_ = true;
  }

  template <class... Ts>
  void set_timeout(uint64_t timeout_id, Ts&&...) {
    CAF_MESSAGE("timeout set " << CAF_ARG(timeout_id));
    timeout_set_ = true;
  }

  template <class Parent>
  void new_proxy(Parent&, const node_id&, actor_id) {
    // nop
  }

  template <class Parent>
  void local_actor_down(Parent&, const node_id&, actor_id, error) {
    // nop
  }

private:
  stream_socket handle_;

  byte_buffer_ptr data_;

  byte_buffer read_buf_;

  byte_buffer buf_;

  bool& timeout_set_;

  bool& timeout_triggered_;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(endpoint_manager_tests, fixture)

CAF_TEST(send and receive) {
  byte_buffer read_buf(1024);
  auto buf = std::make_shared<byte_buffer>();
  auto sockets = unbox(make_stream_socket_pair());
  CAF_CHECK_EQUAL(nonblocking(sockets.second, true), none);
  CAF_CHECK_EQUAL(read(sockets.second, read_buf),
                  sec::unavailable_or_would_block);
  auto guard = detail::make_scope_guard([&] { close(sockets.second); });
  auto mgr = make_endpoint_manager(
    mpx, sys,
    dummy_transport{sockets.first, buf, timeout_set, timeout_triggered});
  CAF_CHECK_EQUAL(mgr->mask(), operation::none);
  CAF_CHECK_EQUAL(mgr->init(), none);
  CAF_CHECK_EQUAL(mgr->mask(), operation::read_write);
  CAF_CHECK_EQUAL(mpx->num_socket_managers(), 2u);
  CAF_CHECK_EQUAL(write(sockets.second, as_bytes(make_span(hello_manager))),
                  hello_manager.size());
  run();
  CAF_CHECK_EQUAL(string_view(reinterpret_cast<char*>(buf->data()),
                              buf->size()),
                  hello_manager);
  CAF_CHECK_EQUAL(read(sockets.second, read_buf), hello_test.size());
  CAF_CHECK_EQUAL(string_view(reinterpret_cast<char*>(read_buf.data()),
                              hello_test.size()),
                  hello_test);
}

CAF_TEST(resolve and proxy communication) {
  byte_buffer read_buf(1024);
  auto buf = std::make_shared<byte_buffer>();
  auto sockets = unbox(make_stream_socket_pair());
  CAF_CHECK_EQUAL(nonblocking(sockets.second, true), none);
  auto guard = detail::make_scope_guard([&] { close(sockets.second); });
  auto mgr = make_endpoint_manager(
    mpx, sys,
    dummy_transport{sockets.first, buf, timeout_set, timeout_triggered});
  CAF_CHECK_EQUAL(mgr->init(), none);
  CAF_CHECK_EQUAL(mgr->mask(), operation::read_write);
  run();
  CAF_CHECK_EQUAL(read(sockets.second, read_buf), hello_test.size());
  mgr->resolve(unbox(make_uri("test:id/42")), self);
  run();
  self->receive(
    [&](resolve_atom, const std::string&, const strong_actor_ptr& p) {
      CAF_MESSAGE("got a proxy, send a message to it");
      self->send(actor_cast<actor>(p), "hello proxy!");
    },
    after(std::chrono::seconds(0)) >>
      [&] { CAF_FAIL("manager did not respond with a proxy."); });
  run();
  auto read_res = read(sockets.second, read_buf);
  if (!holds_alternative<size_t>(read_res)) {
    CAF_ERROR("read() returned an error: " << get<sec>(read_res));
    return;
  }
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

CAF_TEST(timeout) {
  byte_buffer read_buf(1024);
  auto buf = std::make_shared<byte_buffer>();
  auto sockets = unbox(make_stream_socket_pair());
  CAF_CHECK_EQUAL(nonblocking(sockets.second, true), none);
  auto guard = detail::make_scope_guard([&] { close(sockets.second); });
  auto mgr = make_endpoint_manager(
    mpx, sys,
    dummy_transport{sockets.first, buf, timeout_set, timeout_triggered});
  CAF_CHECK_EQUAL(mgr->init(), none);
  auto& mgr_impl
    = *reinterpret_cast<endpoint_manager_impl<dummy_transport>*>(mgr.get());
  auto when = sys.clock().now();
  mgr_impl.set_timeout(when, "dummy");
  CAF_CHECK(timeout_set);
  trigger_timeout();
  while (handle_io_event())
    ;
  CAF_CHECK(timeout_triggered);
}

CAF_TEST(multiple timeouts) {
  byte_buffer read_buf(1024);
  auto buf = std::make_shared<byte_buffer>();
  auto sockets = unbox(make_stream_socket_pair());
  CAF_CHECK_EQUAL(nonblocking(sockets.second, true), none);
  auto guard = detail::make_scope_guard([&] { close(sockets.second); });
  auto mgr = make_endpoint_manager(
    mpx, sys,
    dummy_transport{sockets.first, buf, timeout_set, timeout_triggered});
  CAF_CHECK_EQUAL(mgr->init(), none);
  auto& mgr_impl
    = *reinterpret_cast<endpoint_manager_impl<dummy_transport>*>(mgr.get());
  auto when = sys.clock().now();
  for (int i = 0; i < 10; ++i) {
    mgr_impl.set_timeout(when, "dummy");
    CAF_CHECK(timeout_set);
    timeout_set = false;
    when += 10s;
  }
  CAF_MESSAGE("triggering timeouts");
  advance_time(1s);
  for (int i = 0; i < 10; ++i) {
    while (handle_io_event())
      ;
    CAF_CHECK(timeout_triggered);
    timeout_triggered = false;
    advance_time(10s);
  }
}

CAF_TEST_FIXTURE_SCOPE_END()
