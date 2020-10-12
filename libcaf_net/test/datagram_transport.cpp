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

#define CAF_SUITE net.datagram_transport

#include "caf/net/datagram_transport.hpp"

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include "caf/binary_serializer.hpp"
#include "caf/byte.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/make_actor.hpp"
#include "caf/net/actor_proxy_impl.hpp"
#include "caf/net/ip.hpp"
#include "caf/net/multiplexer.hpp"
#include "caf/net/socket_guard.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/net/udp_datagram_socket.hpp"
#include "caf/span.hpp"
#include "caf/tag/datagram_oriented.hpp"

using namespace caf;
using namespace caf::net;

namespace {

using byte_buffer_ptr = std::shared_ptr<byte_buffer>;

constexpr string_view hello_manager = "hello manager!";

class dummy_application {
public:
  using input_tag = tag::datagram_oriented;

  explicit dummy_application(byte_buffer_ptr recv_buf)
    : recv_buf_(std::move(recv_buf)){
      // nop
    };

  ~dummy_application() = default;

  template <class LowerLayerPtr>
  error init(socket_manager*, LowerLayerPtr, const settings&) {
    return none;
  }

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr down) {
    CAF_MESSAGE("prepare_send called");
    down->begin_datagram();
    auto& buf = down->datagram_buffer();
    auto data = as_bytes(make_span(hello_manager));
    buf.insert(buf.end(), data.begin(), data.end());
    down->end_datagram();
    return true;
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr) {
    CAF_MESSAGE("done_sending called");
    return true;
  }

  template <class LowerLayerPtr>
  size_t consume(LowerLayerPtr, const_byte_span data) {
    recv_buf_->clear();
    recv_buf_->insert(recv_buf_->begin(), data.begin(), data.end());
    CAF_MESSAGE("Received " << recv_buf_->size()
                            << " bytes in dummy_application");
    return recv_buf_->size();
  }

  static void handle_error(sec code) {
    CAF_FAIL("handle_error called with " << CAF_ARG(code));
  }

  template <class LowerLayerPtr>
  static void abort(LowerLayerPtr, const error& reason) {
    CAF_FAIL("abort called with " << CAF_ARG(reason));
  }

private:
  byte_buffer_ptr recv_buf_;
};

class dummy_application_factory {
public:
  using input_tag = tag::datagram_oriented;

  using application_type = dummy_application;

  explicit dummy_application_factory(byte_buffer_ptr recv_buf)
    : recv_buf_(std::move(recv_buf)) {
    // nop
  }

  dummy_application make() {
    return dummy_application{recv_buf_};
  }

private:
  byte_buffer_ptr recv_buf_;
};

struct fixture : test_coordinator_fixture<>, host_fixture {
  fixture()
    : mpx(nullptr),
      send_buf(std::make_shared<byte_buffer>(1024)),
      recv_buf(std::make_shared<byte_buffer>(1024)) {
    if (auto err = mpx.init())
      CAF_FAIL("mpx.init failed: " << CAF_ARG(err));
    mpx.set_thread_id();
    CAF_CHECK_EQUAL(mpx.num_socket_managers(), 1u);
    auto addresses = ip::local_addresses("localhost");
    CAF_CHECK(!addresses.empty());
    ep = ip_endpoint(*addresses.begin(), 0);
    auto send_pair = unbox(make_udp_datagram_socket(ep));
    send_socket = make_socket_guard(send_pair.first);
    auto receive_pair = unbox(make_udp_datagram_socket(ep));
    recv_socket = make_socket_guard(receive_pair.first);
    ep.port(receive_pair.second);
    CAF_MESSAGE("sending message to " << CAF_ARG(ep));
    if (auto err = nonblocking(recv_socket.socket(), true))
      CAF_FAIL("nonblocking() returned an error: " << err);
  }

  bool handle_io_event() override {
    return mpx.poll_once(false);
  }

  multiplexer mpx;
  byte_buffer_ptr send_buf;
  byte_buffer_ptr recv_buf;
  ip_endpoint ep;
  socket_guard<udp_datagram_socket> send_socket;
  socket_guard<udp_datagram_socket> recv_socket;
  settings config;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(datagram_transport_tests, fixture)

CAF_TEST(receive) {
  if (auto err = nonblocking(recv_socket.socket(), true))
    CAF_FAIL("nonblocking() returned an error: " << err);
  auto mgr = make_socket_manager<dummy_application_factory, datagram_transport>(
    recv_socket.release(), &mpx, recv_buf);
  CAF_CHECK_EQUAL(mgr->init(config), none);
  CAF_CHECK_EQUAL(mpx.num_socket_managers(), 2u);
  auto write_res = write(send_socket.socket(),
                         as_bytes(make_span(hello_manager)), ep);
  if (auto err = get_if<sec>(&write_res))
    CAF_FAIL("write_failed " << CAF_ARG2("error", *err));
  auto written = get<size_t>(write_res);
  CAF_CHECK_EQUAL(hello_manager.size(), written);
  CAF_MESSAGE("wrote " << written << " bytes.");
  run();
  CAF_CHECK_EQUAL(string_view(reinterpret_cast<char*>(recv_buf->data()),
                              recv_buf->size()),
                  hello_manager);
}

CAF_TEST(send) {
  byte_buffer buf;
  auto mgr = make_socket_manager<dummy_application_factory, datagram_transport>(
    send_socket.release(), &mpx, recv_buf);
  CAF_CHECK_EQUAL(mgr->init(config), none);
  CAF_CHECK_EQUAL(mpx.num_socket_managers(), 2u);
  auto& dispatcher = mgr->protocol().upper_layer();
  auto worker = dispatcher.add_new_worker(this, node_id{}, ep);
  if (!worker)
    CAF_FAIL("add_new_worker failed " << CAF_ARG2("err", worker.error()));
  mgr->register_writing();
  while (handle_io_event())
    ;
  buf.resize(hello_manager.size());
  auto read_res = read(recv_socket.socket(), make_span(buf));
  if (auto err = get_if<sec>(&read_res))
    CAF_FAIL("read failed" << CAF_ARG(*err));
  auto [received_bytes, ep] = get<std::pair<size_t, ip_endpoint>>(read_res);
  CAF_MESSAGE("received " << received_bytes << " bytes");
  buf.resize(received_bytes);
  CAF_CHECK_EQUAL(string_view(reinterpret_cast<char*>(buf.data()), buf.size()),
                  hello_manager);
}

CAF_TEST_FIXTURE_SCOPE_END()
