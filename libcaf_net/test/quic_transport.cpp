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

#define CAF_SUITE quic_transport

#include "caf/net/quic_transport.hpp"

#include "caf/test/dsl.hpp"

#include "host_fixture.hpp"

#include "caf/byte.hpp"
#include "caf/detail/ptls_util.hpp"
#include "caf/detail/quicly_util.hpp"
#include "caf/make_actor.hpp"
#include "caf/net/actor_proxy_impl.hpp"
#include "caf/net/endpoint_manager.hpp"
#include "caf/net/make_endpoint_manager.hpp"
#include "caf/net/multiplexer.hpp"
#include "caf/net/udp_datagram_socket.hpp"
#include "caf/serializer_impl.hpp"
#include "caf/span.hpp"

using namespace caf;
using namespace caf::net;

namespace {

constexpr string_view hello_manager = "hello manager!";

struct fixture;

struct quicly_stream_open : public quicly_stream_open_t {
  fixture* state;
};

struct fixture : test_coordinator_fixture<>, host_fixture {
  fixture()
    : transport_buf{std::make_shared<std::vector<byte>>()},
      stream_callbacks{quicly_streambuf_destroy, quicly_streambuf_egress_shift,
                       quicly_streambuf_egress_emit, detail::on_stop_sending,
                       // on_receive()
                       [](quicly_stream_t* stream, size_t off, const void* src,
                          size_t len) -> int {
                         if (auto ret = quicly_streambuf_ingress_receive(stream,
                                                                         off,
                                                                         src,
                                                                         len))
                           return ret;
                         ptls_iovec_t input;
                         if ((input = quicly_streambuf_ingress_get(stream))
                               .len) {
                           string_view message(reinterpret_cast<char*>(
                                                 input.base),
                                               input.len);
                           CAF_MESSAGE("received: " << CAF_ARG(message));
                           quicly_streambuf_ingress_shift(stream, input.len);
                         }
                         return 0;
                       },
                       // on_receive_reset()
                       [](quicly_stream_t* stream, int err) -> int {
                         quicly_close(stream->conn,
                                      QUICLY_ERROR_FROM_APPLICATION_ERROR_CODE(
                                        0),
                                      "received reset");
                         CAF_FAIL("received stream reset.");
                         return 0;
                       }} {
    mpx = std::make_shared<multiplexer>();
    if (auto err = mpx->init())
      CAF_FAIL("mpx->init failed: " << sys.render(err));
    CAF_CHECK_EQUAL(mpx->num_socket_managers(), 1u);
    if (auto err = parse("127.0.0.1:0", recv_ep))
      CAF_FAIL("parse returned an error: " << err);
    auto send_pair = unbox(make_udp_datagram_socket(recv_ep));
    send_sock = send_pair.first;
    if (auto err = nonblocking(send_sock, true))
      CAF_LOG_ERROR("nonblocking returned an error: " << err);
    auto receive_pair = unbox(make_udp_datagram_socket(recv_ep));
    recv_sock = receive_pair.first;
    recv_ep.port(ntohs(receive_pair.second));
    CAF_MESSAGE("sending data to: " << to_string(recv_ep));
    if (auto err = nonblocking(recv_sock, true))
      CAF_LOG_ERROR("nonblocking returned an error: " << err);
    stream_open.state = this;
    stream_open.cb = [](quicly_stream_open_t* self,
                        quicly_stream_t* new_stream) -> int {
      auto tmp = static_cast<quicly_stream_open*>(self);
      return tmp->state->on_stream_open(self, new_stream);
    };
    closed_by_peer.cb = [](quicly_closed_by_peer_t*, quicly_conn_t*, int,
                           uint64_t, const char*, size_t) {
      CAF_FAIL("peer closed connection");
      // nop
    };
  };

  ~fixture() {
    close(send_sock);
    close(recv_sock);
  }

  bool handle_io_event() override {
    mpx->handle_updates();
    return mpx->poll_once(false);
  }

  void quic_setup() {
    ctx = quicly_spec_context;
    ctx.tls = &tlsctx;
    ctx.stream_open = &stream_open;

    memset(&tlsctx, 0, sizeof(ptls_context_t));
    tlsctx.random_bytes = ptls_openssl_random_bytes;
    tlsctx.get_time = &ptls_get_time;
    tlsctx.key_exchanges = key_exchanges;
    tlsctx.cipher_suites = ptls_openssl_cipher_suites;
    tlsctx.require_dhe_on_psk = 1;
    tlsctx.save_ticket = &save_ticket;

    ctx = quicly_spec_context;
    ctx.tls = &tlsctx;
    ctx.stream_open = &stream_open;
    ctx.closed_by_peer = &closed_by_peer;

    detail::setup_session_cache(ctx.tls);
    quicly_amend_ptls_context(ctx.tls);

    key_exchanges[0] = &ptls_openssl_secp256r1;
  }

  void quic_roundtrip() {
    run();
    quic_receive();
    detail::send_pending_datagrams(send_sock, conn_ptr);
  }

  void quic_connect() {
    sockaddr_storage sa = {};
    detail::convert(recv_ep, sa);
    quicly_conn_t* conn = nullptr;
    if (quicly_connect(&conn, &ctx, "localhost",
                       reinterpret_cast<sockaddr*>(&sa), nullptr, &next_cid,
                       resumption_token, &hs_properties,
                       &resumed_transport_params))
      CAF_FAIL("quicly_connect failed");
    conn_ptr = detail::make_quicly_conn_ptr(conn);
    ++next_cid.master_id;
    detail::send_pending_datagrams(send_sock, conn_ptr);
    // do roundtrips until connected
    int i = 0;
    while (quicly_get_state(conn_ptr.get()) != QUICLY_STATE_CONNECTED) {
      if (++i > 5)
        CAF_FAIL("connection process took too many roundtrips");
      quic_roundtrip();
    }
    CAF_MESSAGE("connected after " << CAF_ARG(i) << " rounds");

    quic_roundtrip(); // get new stream!
  }

  /// this function receives data and passes it to the quic stack.
  /// Needed for accepting incoming packets.
  void quic_receive() {
    sockaddr_storage sa = {};
    std::vector<byte> receive_buf(4096);
    auto read_res = read(send_sock, make_span(receive_buf));
    if (auto err = get_if<sec>(&read_res))
      CAF_FAIL("read returned an error: " << err);
    auto read_pair = get<std::pair<size_t, ip_endpoint>>(read_res);
    auto received_bytes = read_pair.first;
    auto ep = read_pair.second;
    detail::convert(ep, sa);
    size_t off = 0;
    while (off < received_bytes) {
      quicly_decoded_packet_t packet;
      auto buf = reinterpret_cast<uint8_t*>(receive_buf.data());
      auto packet_length = quicly_decode_packet(&ctx, &packet, buf + off,
                                                received_bytes - off);
      if (packet_length == SIZE_MAX)
        break;
      quicly_receive(conn_ptr.get(), nullptr, reinterpret_cast<sockaddr*>(&sa),
                     &packet);
      off += packet_length;
    }
  }

  void quic_send(std::string msg) {
  }

  void quicly_test_send(string_view msg) {
    quic_setup();
    quic_connect();
    // quic_send(msg);
  }

  // -- quic callbacks
  // ---------------------------------------------------------

  int on_stream_open(st_quicly_stream_open_t*, st_quicly_stream_t* new_stream) {
    if (quicly_streambuf_create(new_stream, sizeof(quicly_streambuf_t)))
      CAF_FAIL("streambuf_create failed");
    new_stream->callbacks = &stream_callbacks;
    stream = new_stream;
    return 0;
  }

  // -- test state
  // -------------------------------------------------------------

  multiplexer_ptr mpx;
  std::shared_ptr<std::vector<byte>> transport_buf;
  ip_endpoint recv_ep;
  udp_datagram_socket send_sock;
  udp_datagram_socket recv_sock;

  // -- quicly state
  // -----------------------------------------------------------

  bool connected;
  char* cid_key;
  quicly_cid_plaintext_t next_cid;
  ptls_handshake_properties_t hs_properties;
  quicly_transport_parameters_t resumed_transport_params;
  quicly_closed_by_peer_t closed_by_peer;
  quicly_stream_open stream_open;
  ptls_save_ticket_t save_ticket;
  ptls_key_exchange_algorithm_t* key_exchanges[128];
  ptls_context_t tlsctx;
  quicly_context_t ctx;
  quicly_transport_streambuf streambuf;
  ptls_iovec_t resumption_token;
  detail::quicly_conn_ptr conn_ptr;
  quicly_stream_t* stream;
  quicly_stream_callbacks_t stream_callbacks;

}; // namespace

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
    transport.write_packet(span<byte>{}, msg->payload);
  }

  template <class Parent>
  void handle_data(Parent&, span<const byte> data) {
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

struct dummy_application_factory {
  using application_type = dummy_application;

  dummy_application_factory(std::shared_ptr<std::vector<byte>> buf)
    : buf_(buf) {
    // nop
  }

  dummy_application make() {
    return dummy_application{buf_};
  }

private:
  std::shared_ptr<std::vector<byte>> buf_;
};

using transport_type = quic_transport<dummy_application_factory>;

} // namespace

CAF_TEST_FIXTURE_SCOPE(quic_transport_tests, fixture)

CAF_TEST(receive) {
  transport_type transport{recv_sock, dummy_application_factory{transport_buf}};
  transport.configure_read(net::receive_policy::exactly(hello_manager.size()));
  auto mgr = make_endpoint_manager(mpx, sys, std::move(transport));
  CAF_CHECK_EQUAL(mgr->init(), none);
  mpx->handle_updates();
  CAF_CHECK_EQUAL(mpx->num_socket_managers(), 2u);
  quicly_test_send(hello_manager);
  CAF_CHECK_EQUAL(string_view(reinterpret_cast<char*>(transport_buf->data()),
                              transport_buf->size()),
                  hello_manager);
}

// TODO: test is disabled until resolve in transport_worker_dispatcher is
// implemented correctly.
// Idea is to use caf::uri instead of std::string.
/*
CAF_TEST(resolve and proxy communication) {
  using transport_type = datagram_transport<dummy_application_factory>;
  auto buf = std::make_shared<std::vector<byte>>();
  CAF_CHECK_EQUAL(mpx->num_socket_managers(), 1u);
  ip_endpoint ep;
  if (auto err = parse("127.0.0.1:0", ep))
    CAF_FAIL("parse returned an error: " << err);
  auto sender = unbox(make_udp_datagram_socket(ep));
  ep.port(0);
  auto receiver = unbox(make_udp_datagram_socket(ep));
  auto send_guard = make_socket_guard(sender);
  auto receive_guard = make_socket_guard(receiver);
  if (auto err = nonblocking(receiver, true))
    CAF_FAIL("nonblocking() returned an error: " << err);
  auto test_read_res = read(receiver, make_span(*buf));
  if (auto p = get_if<std::pair<size_t, ip_endpoint>>(&test_read_res))
    CAF_CHECK_EQUAL(p->first, 0u);
  else
    CAF_FAIL("read returned an error: " << get<sec>(test_read_res));
  auto mgr = make_endpoint_manager(mpx, sys,
                                   transport_type{sender,
                                                  dummy_application_factory{
                                                    buf}});
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
  auto read_res = read(receiver, make_span(*buf));
  if (!holds_alternative<std::pair<size_t, ip_endpoint>>(read_res))
    CAF_FAIL("read() returned an error: " << sys.render(get<sec>(read_res)));
  buf->resize(get<std::pair<size_t, ip_endpoint>>(read_res).first);
  CAF_MESSAGE("receive buffer contains " << buf->size() << " bytes");
  message msg;
  binary_deserializer source{sys, *buf};
  CAF_CHECK_EQUAL(source(msg), none);
  if (msg.match_elements<std::string>())
    CAF_CHECK_EQUAL(msg.get_as<std::string>(0), "hello proxy!");
  else
    CAF_ERROR("expected a string, got: " << to_string(msg));
}
*/

CAF_TEST_FIXTURE_SCOPE_END()
