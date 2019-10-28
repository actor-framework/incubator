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

#include "caf/net/test/host_fixture.hpp"

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

struct stream_open : public quicly_stream_open_t {
  fixture* state;
};

struct fixture : test_coordinator_fixture<>, host_fixture {
  fixture()
    : transport_buf{std::make_shared<std::vector<byte>>()},
      stream_{},
      quicly_state_{
        {quicly_streambuf_destroy, quicly_streambuf_egress_shift,
         quicly_streambuf_egress_emit, detail::on_stop_sending,
         // on_receive()
         [](quicly_stream_t* stream, size_t off, const void* src,
            size_t len) -> int {
           if (auto ret = quicly_streambuf_ingress_receive(stream, off, src,
                                                           len))
             return ret;
           ptls_iovec_t input;
           if ((input = quicly_streambuf_ingress_get(stream)).len) {
             string_view message(reinterpret_cast<char*>(input.base),
                                 input.len);
             CAF_MESSAGE("received: " << CAF_ARG(message));
             quicly_streambuf_ingress_shift(stream, input.len);
           }
           return 0;
         },
         // on_receive_reset()
         [](quicly_stream_t* stream, int) -> int {
           quicly_close(stream->conn,
                        QUICLY_ERROR_FROM_APPLICATION_ERROR_CODE(0),
                        "received reset");
           CAF_FAIL("received stream reset.");
           return 0;
         }}},
      save_resumption_token_{},
      generate_resumption_token_{},
      save_ticket_{},
      stream_open_{},
      closed_by_peer_{} {
    mpx = std::make_shared<multiplexer>();
    if (auto err = mpx->init())
      CAF_FAIL("mpx->init failed: " << sys.render(err));
    mpx->set_thread_id();
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
    stream_open_.state = this;
    stream_open_.cb = [](quicly_stream_open_t* self,
                         quicly_stream_t* new_stream) -> int {
      auto tmp = static_cast<stream_open*>(self);
      return tmp->state->on_stream_open(self, new_stream);
    };
    save_resumption_token_.cb = [](quicly_save_resumption_token_t*,
                                   quicly_conn_t*,
                                   ptls_iovec_t) -> int { return 0; };
    generate_resumption_token_.cb =
      [](quicly_generate_resumption_token_t*, quicly_conn_t*, ptls_buffer_t*,
         quicly_address_token_plaintext_t*) -> int { return 0; };
    save_ticket_.cb = [](ptls_save_ticket_t*, ptls_t*, ptls_iovec_t) -> int {
      return 0;
    };
    closed_by_peer_.cb = [](quicly_closed_by_peer_t*, quicly_conn_t*, int,
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
    return mpx->poll_once(false);
  }

  void quic_setup() {
    quicly_state_.tlsctx.random_bytes = ptls_openssl_random_bytes;
    quicly_state_.tlsctx.get_time = &ptls_get_time;
    quicly_state_.tlsctx.key_exchanges = quicly_state_.key_exchanges;
    quicly_state_.tlsctx.cipher_suites = ptls_openssl_cipher_suites;
    quicly_state_.tlsctx.require_dhe_on_psk = 1;
    quicly_state_.tlsctx.save_ticket = &save_ticket_;
    // initialize quicly_context
    quicly_state_.ctx = quicly_spec_context;
    quicly_state_.ctx.tls = &quicly_state_.tlsctx;
    quicly_state_.ctx.stream_open = &stream_open_;
    quicly_state_.ctx.closed_by_peer = &closed_by_peer_;
    quicly_state_.ctx.save_resumption_token = &save_resumption_token_;
    quicly_state_.ctx.generate_resumption_token = &generate_resumption_token_;
    // initialize session_context
    detail::setup_session_cache(quicly_state_.ctx.tls);
    quicly_amend_ptls_context(quicly_state_.ctx.tls);
    // generate cypher context for en-/decryption
    uint8_t secret[PTLS_MAX_DIGEST_SIZE];
    quicly_state_.ctx.tls->random_bytes(secret,
                                        ptls_openssl_sha256.digest_size);
    quicly_state_.address_token_aead.enc = ptls_aead_new(
      &ptls_openssl_aes128gcm, &ptls_openssl_sha256, 1, secret, "");
    quicly_state_.address_token_aead.dec = ptls_aead_new(
      &ptls_openssl_aes128gcm, &ptls_openssl_sha256, 0, secret, "");
    // read certificates from file.
    std::string path_to_certs;
    if (auto path = getenv("QUICLY_CERTS")) {
      path_to_certs = path;
    } else {
      // try to load default certs
      path_to_certs = "/home/jakob/code/quicly/t/assets/";
    }
    auto certificate_chain_path = (path_to_certs + std::string("server.crt"));
    auto private_key_path = (path_to_certs + std::string("server.key"));
    if (detail::load_certificate_chain(quicly_state_.ctx.tls,
                                       certificate_chain_path))
      CAF_FAIL("failed to load certificate chain: "
               << CAF_ARG(certificate_chain_path));
    if (detail::load_private_key(quicly_state_.ctx.tls, private_key_path))
      CAF_FAIL("failed to load private keys: " << CAF_ARG(private_key_path));
    CAF_ASSERT(quicly_state_.ctx.tls->certificates.count != 0
               || quicly_state_.ctx.tls->sign_certificate != nullptr);
    quicly_state_.key_exchanges[0] = &ptls_openssl_secp256r1;
    quicly_state_.tlsctx.random_bytes(quicly_state_.cid_key,
                                      sizeof(quicly_state_.cid_key) - 1);
    auto iovec = ptls_iovec_init(quicly_state_.cid_key,
                                 sizeof(quicly_state_.cid_key) - 1);
    auto cid_cipher = &ptls_openssl_bfecb;
    auto reset_token_cipher = &ptls_openssl_aes128ecb;
    auto hash = &ptls_openssl_sha256;
    auto default_encryptor = quicly_new_default_cid_encryptor(
      cid_cipher, reset_token_cipher, hash, iovec);
    quicly_state_.ctx.cid_encryptor = default_encryptor;
  }

  void quic_roundtrip() {
    run();
    quic_receive();
    CAF_CHECK_EQUAL(detail::send_pending_datagrams(send_sock, connection_),
                    none);
  }

  void quic_connect() {
    sockaddr_storage sa = {};
    detail::convert(recv_ep, sa);
    quicly_conn_t* conn = nullptr;
    if (quicly_connect(&conn, &quicly_state_.ctx, "localhost",
                       reinterpret_cast<sockaddr*>(&sa), nullptr,
                       &quicly_state_.next_cid, quicly_state_.resumption_token,
                       &quicly_state_.hs_properties,
                       &quicly_state_.resumed_transport_params))
      CAF_FAIL("quicly_connect failed");
    connection_ = detail::make_quicly_conn_ptr(conn);
    ++quicly_state_.next_cid.master_id;
    CAF_CHECK_EQUAL(detail::send_pending_datagrams(send_sock, connection_),
                    none);
    // do roundtrips until connected
    int i = 0;
    while (quicly_get_state(connection_.get()) != QUICLY_STATE_CONNECTED) {
      if (++i > 5)
        CAF_FAIL("connection process took too many roundtrips");
      quic_roundtrip();
    }
    CAF_MESSAGE("connected after " << CAF_ARG(i) << " rounds");
    if (quicly_open_stream(connection_.get(), &stream_, 0)) {
      CAF_FAIL("quicly_open_stream failed");
    }
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
      auto packet_length = quicly_decode_packet(&quicly_state_.ctx, &packet,
                                                buf + off,
                                                received_bytes - off);
      if (packet_length == SIZE_MAX)
        break;
      quicly_receive(connection_.get(), nullptr,
                     reinterpret_cast<sockaddr*>(&sa), &packet);
      off += packet_length;
    }
  }

  void quic_send(string_view msg) {
    quicly_streambuf_egress_write(stream_, msg.data(), msg.length());
    CAF_CHECK_EQUAL(detail::send_pending_datagrams(send_sock, connection_),
                    none);
  }

  void quicly_test_send(string_view msg) {
    quic_setup();
    quic_connect();
    CAF_CHECK_EQUAL(quicly_get_state(connection_.get()),
                    QUICLY_STATE_CONNECTED);
    quic_send(msg);
    for (int i = 0; i < 2; ++i)
      quic_roundtrip();
  }

  // -- quic callbacks ---------------------------------------------------------

  int on_stream_open(st_quicly_stream_open_t*, st_quicly_stream_t* new_stream) {
    if (quicly_streambuf_create(new_stream, sizeof(quicly_streambuf_t)))
      CAF_FAIL("streambuf_create failed");
    new_stream->callbacks = &quicly_state_.stream_callbacks;
    return 0;
  }

  multiplexer_ptr mpx;
  std::shared_ptr<std::vector<byte>> transport_buf;
  ip_endpoint recv_ep;
  udp_datagram_socket send_sock;
  udp_datagram_socket recv_sock;

private:
  // quicly connection/stream
  detail::quicly_conn_ptr connection_;
  quicly_stream_t* stream_;

  // quicly connection state
  detail::quicly_state quicly_state_;

  // quicly callbacks
  quicly_save_resumption_token_t save_resumption_token_;
  quicly_generate_resumption_token_t generate_resumption_token_;
  ptls_save_ticket_t save_ticket_;
  stream_open stream_open_;
  quicly_closed_by_peer_t closed_by_peer_;
}; // namespace

class dummy_application {
  using buffer_type = std::vector<byte>;

  using buffer_ptr = std::shared_ptr<buffer_type>;

public:
  dummy_application(buffer_ptr rec_buf)
    : rec_buf_(std::move(rec_buf)){
      // nop
    };

  ~dummy_application() = default;

  template <class Parent>
  error init(Parent&) {
    return none;
  }

  template <class Parent>
  void write_message(Parent& parent,
                     std::unique_ptr<endpoint_manager_queue::message> msg) {
    parent.write_packet(msg->payload);
  }

  template <class Parent>
  error handle_data(Parent&, span<const byte> data) {
    rec_buf_->clear();
    rec_buf_->insert(rec_buf_->begin(), data.begin(), data.end());
    return none;
  }

  template <class Parent>
  void resolve(Parent& parent, string_view path, const actor& listener) {
    actor_id aid = 42;
    auto uri = unbox(make_uri("test:/id/42"));
    auto nid = make_node_id(uri);
    actor_config cfg;
    endpoint_manager_ptr ptr{&parent.manager()};
    auto p = make_actor<actor_proxy_impl, strong_actor_ptr>(aid, nid,
                                                            &parent.system(),
                                                            cfg,
                                                            std::move(ptr));
    anon_send(listener, resolve_atom::value,
              std::string{path.begin(), path.end()}, p);
  }

  template <class Parent>
  void new_proxy(Parent&, actor_id) {
    // nop
  }

  template <class Parent>
  void local_actor_down(Parent&, actor_id, error) {
    // nop
  }

  template <class Parent>
  void timeout(Parent&, atom_value, uint64_t) {
    // nop
  }

  void handle_error(sec sec) {
    CAF_FAIL("handle_error called: " << to_string(sec));
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

class dummy_application_factory {
  using buffer_ptr = std::shared_ptr<std::vector<byte>>;

public:
  using application_type = dummy_application;

  dummy_application_factory(buffer_ptr buf) : buf_(buf) {
    // nop
  }

  dummy_application make() {
    return dummy_application{buf_};
  }

private:
  std::shared_ptr<std::vector<byte>> buf_;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(quic_transport_tests, fixture)

CAF_TEST(receive) {
  using transport_type = quic_transport<dummy_application_factory>;
  auto mgr = make_endpoint_manager(mpx, sys,
                                   transport_type{recv_sock,
                                                  dummy_application_factory{
                                                    transport_buf}});
  CAF_CHECK_EQUAL(mgr->init(), none);
  auto mgr_impl = mgr.downcast<endpoint_manager_impl<transport_type>>();
  auto& transport = mgr_impl->transport();
  transport.configure_read(net::receive_policy::exactly(hello_manager.size()));
  CAF_CHECK_EQUAL(mpx->num_socket_managers(), 2u);
  quicly_test_send(hello_manager);
  auto received_str = string_view(reinterpret_cast<char*>(
                                    transport_buf->data()),
                                  transport_buf->size());
  CAF_MESSAGE("recived: " << CAF_ARG(received_str));
  CAF_CHECK_EQUAL(hello_manager.length(), transport_buf->size());
  CAF_CHECK_EQUAL(received_str, hello_manager);
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
