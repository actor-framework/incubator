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

#pragma once

#include <arpa/nameser.h>
#include <deque>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/pem.h>
#include <resolv.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unordered_map>

#include "caf/config.hpp"

CAF_PUSH_WARNINGS
#ifdef CAF_GCC
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#elif defined(CAF_CLANG)
#  pragma clang diagnostic ignored "-Wunused-parameter"
#  pragma clang diagnostic ignored "-Wmissing-field-initializers"
#  pragma clang diagnostic ignored "-Wsign-compare"
#endif
extern "C" {
#include <picotls/openssl.h>
#include <quicly.h>
#include <quicly/defaults.h>
#include <quicly/streambuf.h>
}
CAF_POP_WARNINGS

#include "caf/byte.hpp"
#include "caf/detail/convert_ip_endpoint.hpp"
#include "caf/detail/ptls_util.hpp"
#include "caf/detail/quicly_util.hpp"
#include "caf/detail/socket_sys_aliases.hpp"
#include "caf/detail/socket_sys_includes.hpp"
#include "caf/error.hpp"
#include "caf/expected.hpp"
#include "caf/fwd.hpp"
#include "caf/logger.hpp"
#include "caf/net/defaults.hpp"
#include "caf/net/endpoint_manager.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/ip.hpp"
#include "caf/net/receive_policy.hpp"
#include "caf/net/transport_worker_dispatcher.hpp"
#include "caf/net/udp_datagram_socket.hpp"
#include "caf/sec.hpp"
#include "caf/span.hpp"
#include "caf/variant.hpp"

namespace caf {
namespace net {

struct received_data {
  received_data(size_t id, span<byte> data) : id(id) {
    auto size = data.size();
    received.reserve(size);
    received.insert(received.begin(), data.begin(), data.end());
  }

  size_t id;
  std::vector<byte> received;
};

template <class Factory>
struct quicly_save_resumption_token : quicly_save_resumption_token_t {
  quic_transport<Factory>* transport;
};

template <class Factory>
struct quicly_generate_resumption_token : quicly_generate_resumption_token_t {
  quic_transport<Factory>* transport;
};

template <class Factory>
struct quicly_save_session_ticket : ptls_save_ticket_t {
  quic_transport<Factory>* transport;
};

template <class Factory>
struct quicly_stream_open : public quicly_stream_open_t {
  quic_transport<Factory>* transport;
};

template <class Factory>
struct quicly_closed_by_peer : public quicly_closed_by_peer_t {
  quic_transport<Factory>* transport;
};

struct quicly_transport_streambuf : public quicly_streambuf_t {
  std::shared_ptr<std::vector<received_data>> buf;
};

/// Implements a quic transport policy that manages a datagram socket.
template <class Factory>
class quic_transport {
public:
  // -- member types -----------------------------------------------------------

  using id_type = size_t;

  using buffer_type = std::vector<byte>;

  using buffer_cache_type = std::vector<buffer_type>;

  using factory_type = Factory;

  using transport_type = quic_transport;

  using dispatcher_type = transport_worker_dispatcher<transport_type, id_type>;

  using application_type = typename Factory::application_type;

  // -- properties -------------------------------------------------------------

  udp_datagram_socket handle() const noexcept {
    return handle_;
  }

  actor_system& system() {
    return manager().system();
  }

  application_type& application() {
    // TODO: This wont work. We need information on which application is wanted
    return application_type{};
    // dispatcher_.application();
  }

  transport_type& transport() {
    return *this;
  }

  endpoint_manager& manager() {
    return *manager_;
  }

  // -- constructors, destructors, and assignment operators --------------------

  quic_transport(udp_datagram_socket handle, factory_type factory)
    : dispatcher_(*this, std::move(factory)),
      handle_(handle),
      read_buf_(std::make_shared<std::vector<received_data>>()),
      max_consecutive_reads_(0),
      read_threshold_(1024),
      collected_(0),
      max_(1024),
      rd_flag_(receive_policy_flag::exactly),
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
             CAF_LOG_TRACE("quicly received: " << CAF_ARG(input.len)
                                               << "bytes");
             auto buf = reinterpret_cast<quicly_transport_streambuf*>(
                          stream->data)
                          ->buf;
             auto id = detail::convert(stream->conn);
             buf->emplace_back(id, as_writable_bytes(
                                     make_span(input.base, input.len)));
             quicly_streambuf_ingress_shift(stream, input.len);
           }
           return 0;
         },
         // on_receive_reset()
         [](quicly_stream_t* stream, int err) -> int {
           CAF_LOG_TRACE("quicly_received reset-stream: "
                         << CAF_ARG(QUICLY_ERROR_GET_ERROR_CODE(err)));
           return quicly_close(stream->conn,
                               QUICLY_ERROR_FROM_APPLICATION_ERROR_CODE(0),
                               "received reset");
         }}},
      known_conns_{},
      known_streams_{},
      save_resumption_token_{},
      generate_resumption_token_{},
      save_session_ticket_{},
      stream_open_{},
      closed_by_peer_{},
      session_file_path_{} {
    // nop
  }

  // -- public member functions ------------------------------------------------

  template <class Parent>
  error init(Parent& parent) {
    manager_ = &parent;
    auto& cfg = system().config();
    auto max_header_bufs = get_or(cfg, "middleman.max-header-buffers",
                                  defaults::middleman::max_header_buffers);
    header_bufs_.reserve(max_header_bufs);
    auto max_payload_bufs = get_or(cfg, "middleman.max-payload-buffers",
                                   defaults::middleman::max_payload_buffers);
    payload_bufs_.reserve(max_payload_bufs);
    // TODO: think of a more general way to initialize these callbacks
    stream_open_.transport = this;
    stream_open_.cb = [](quicly_stream_open_t* self,
                         quicly_stream_t* stream) -> int {
      auto tmp = static_cast<quicly_stream_open<factory_type>*>(self);
      return tmp->transport->on_stream_open(self, stream);
    };
    closed_by_peer_.transport = this;
    closed_by_peer_.cb = [](quicly_closed_by_peer_t* self, quicly_conn_t* conn,
                            int, uint64_t, const char*, size_t) {
      auto tmp = static_cast<quicly_closed_by_peer<factory_type>*>(self);
      tmp->transport->on_closed_by_peer(conn);
    };
    save_resumption_token_.transport = this;
    save_resumption_token_.cb = [](quicly_save_resumption_token_t* self,
                                   quicly_conn_t* conn,
                                   ptls_iovec_t token) -> int {
      auto tmp = static_cast<quicly_save_resumption_token<factory_type>*>(self);
      return tmp->transport->on_save_resumption_token(conn, token);
    };
    generate_resumption_token_.transport = this;
    generate_resumption_token_.cb =
      [](quicly_generate_resumption_token_t* self, quicly_conn_t*,
         ptls_buffer_t* buf, quicly_address_token_plaintext_t* token) -> int {
      auto tmp = static_cast<quicly_generate_resumption_token<factory_type>*>(
        self);
      return tmp->transport->on_generate_resumption_token(buf, token);
    };
    save_session_ticket_.transport = this;
    save_session_ticket_.cb = [](ptls_save_ticket_t* self, ptls_t* tls,
                                 ptls_iovec_t src) -> int {
      auto tmp = static_cast<quicly_save_session_ticket<factory_type>*>(self);
      return tmp->transport->on_save_session_ticket(tls, src);
    };
    // initialize tls_context
    quicly_state_.tlsctx.random_bytes = ptls_openssl_random_bytes;
    quicly_state_.tlsctx.get_time = &ptls_get_time;
    quicly_state_.tlsctx.key_exchanges = quicly_state_.key_exchanges;
    quicly_state_.tlsctx.cipher_suites = ptls_openssl_cipher_suites;
    quicly_state_.tlsctx.require_dhe_on_psk = 1;
    quicly_state_.tlsctx.save_ticket = &save_session_ticket_;
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
      return make_error(sec::runtime_error, "failed to load certificate chain: "
                                              + certificate_chain_path);
    if (detail::load_private_key(quicly_state_.ctx.tls, private_key_path))
      return make_error(sec::runtime_error,
                        "failed to load private keys: " + private_key_path);
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
    return dispatcher_.init(*this);
  }

  template <class Parent>
  bool handle_read_event(Parent&) {
    CAF_LOG_TRACE(CAF_ARG(handle_.id));
    uint8_t buf[4096];
    auto ret = read(handle_, as_writable_bytes(make_span(buf, sizeof(buf))));
    if (auto err = get_if<sec>(&ret)) {
      CAF_LOG_DEBUG("read failed" << CAF_ARG(*err));
      dispatcher_.handle_error(*err);
      return false;
    }
    auto read_pair = get<std::pair<size_t, ip_endpoint>>(ret);
    auto read_res = read_pair.first;
    auto ep = read_pair.second;
    sockaddr_storage sa = {};
    detail::convert(ep, sa);
    size_t off = 0;
    while (off != read_res) {
      quicly_decoded_packet_t packet;
      auto plen = quicly_decode_packet(&quicly_state_.ctx, &packet, buf + off,
                                       read_pair.first - off);
      if (plen == SIZE_MAX)
        break;
      if (QUICLY_PACKET_IS_LONG_HEADER(packet.octets.base[0])) {
        if (packet.version != QUICLY_PROTOCOL_VERSION) {
          auto rp = quicly_send_version_negotiation(&quicly_state_.ctx,
                                                    reinterpret_cast<sockaddr*>(
                                                      &sa),
                                                    packet.cid.src, nullptr,
                                                    packet.cid.dest.encrypted);
          CAF_ASSERT(rp != nullptr);
          auto send_res = detail::send_quicly_datagram(handle_, rp);
          if (auto err = get_if<sec>(&send_res))
            CAF_LOG_ERROR("send_quicly_datagram failed" << CAF_ARG(*err));
          break;
        }
        // there is no way to send response to these v1 packets
        if (packet.cid.dest.encrypted.len > QUICLY_MAX_CID_LEN_V1
            || packet.cid.src.len > QUICLY_MAX_CID_LEN_V1)
          break;
      }
      auto conn_it = std::
        find_if(known_conns_.begin(), known_conns_.end(),
                [&](const std::pair<id_type, detail::quicly_conn_ptr>& p) {
                  return quicly_is_destination(p.second.get(), nullptr,
                                               reinterpret_cast<sockaddr*>(&sa),
                                               &packet);
                });
      if (conn_it != known_conns_.end()) {
        // already accepted connection
        auto& conn = conn_it->second;
        quicly_receive(conn.get(), nullptr, reinterpret_cast<sockaddr*>(&sa),
                       &packet);
        for (auto& data : *read_buf_)
          dispatcher_.handle_data(*this, make_span(data.received), data.id);
        read_buf_->clear();
        detail::send_pending_datagrams(handle_, conn);
      } else if (QUICLY_PACKET_IS_LONG_HEADER(packet.octets.base[0])) {
        // new connection
        quicly_address_token_plaintext_t* token = nullptr;
        quicly_address_token_plaintext_t token_buf;
        if (packet.token.len != 0
            && quicly_decrypt_address_token(const_cast<ptls_aead_context_t*>(
                                              quicly_state_.address_token_aead
                                                .dec),
                                            &token_buf, packet.token.base,
                                            packet.token.len, 0)
                 == 0
            && detail::validate_token(reinterpret_cast<sockaddr*>(&sa),
                                      packet.cid.src, packet.cid.dest.encrypted,
                                      &token_buf, &quicly_state_.ctx))
          token = &token_buf;

        quicly_conn_t* conn = nullptr;
        int accept_res = quicly_accept(&conn, &quicly_state_.ctx, nullptr,
                                       reinterpret_cast<sockaddr*>(&sa),
                                       &packet, token, &quicly_state_.next_cid,
                                       nullptr);
        if (accept_res == 0 && conn) {
          CAF_LOG_TRACE("accepted new quic connection");
          auto id = detail::convert(conn);
          auto conn_ptr = detail::make_quicly_conn_ptr(conn);
          ++quicly_state_.next_cid.master_id;
          // TODO: node_id is missing here
          if (auto err = dispatcher_.add_new_worker(*this, node_id{}, id)) {
            CAF_LOG_ERROR("add_new_worker returned an error: " << CAF_ARG(err));
            return false;
          }
          quicly_stream_t* stream = nullptr;
          if (quicly_open_stream(conn, &stream, 0)) {
            CAF_LOG_ERROR("quicly_open_stream failed");
            return false;
          }
          known_streams_.emplace(id, stream);
          known_conns_.emplace(id, conn_ptr);
          detail::send_pending_datagrams(handle_, conn_ptr);
        } else {
          CAF_LOG_ERROR("could not accept new connection");
          return false;
        }
      } else {
        /* short header packet; potentially a dead connection. No need to check
         * the length of the incoming packet, because loop is prevented by
         * authenticating the CID (by checking node_id and thread_id). If the
         * peer is also sending a reset, then the next CID is highly likely to
         * contain a non-authenticating CID, ... */
        if (packet.cid.dest.plaintext.node_id == 0
            && packet.cid.dest.plaintext.thread_id == 0) {
          auto dgram = quicly_send_stateless_reset(&quicly_state_.ctx,
                                                   reinterpret_cast<sockaddr*>(
                                                     &sa),
                                                   nullptr,
                                                   packet.cid.dest.encrypted
                                                     .base);
          auto send_res = detail::send_quicly_datagram(handle_, dgram);
          if (auto err = get_if<sec>(&send_res)) {
            CAF_LOG_ERROR("send_quicly_datagram failed" << CAF_ARG(*err));
            return false;
          }
        }
      }
      off += plen;
    }
    for (const auto& p : known_conns_) {
      if (auto err = detail::send_pending_datagrams(handle_, p.second)) {
        CAF_LOG_ERROR("send_pending failed: " << CAF_ARG(err));
        auto id = detail::convert(p.second);
        known_conns_.erase(id);
        known_streams_.erase(id);
      }
    }
    return true;
  }

  template <class Parent>
  bool handle_write_event(Parent& parent) {
    CAF_LOG_TRACE(CAF_ARG(handle_.id)
                  << CAF_ARG2("queue-size", packet_queue_.size()));
    // Try to write leftover data.
    write_some();
    // Get new data from parent.
    for (auto msg = parent.next_message(); msg != nullptr;
         msg = parent.next_message()) {
      dispatcher_.write_message(*this, std::move(msg));
    }
    // Write prepared data.
    return write_some();
  }

  template <class Parent>
  void resolve(Parent&, const uri& locator, const actor& listener) {
    auto nid = make_node_id(locator);
    if (!dispatcher_.contains(nid)) {
      auto& authority = locator.authority();
      auto port = authority.port;
      ip_address ip;
      ip_endpoint ep;
      if (auto hostname = get_if<std::string>(&authority.host)) {
        auto ips = ip::resolve(*hostname);
        // TODO: check for correct address family here
        ip = ips.at(0);
      } else if (auto ip_ptr = get_if<ip_address>(&authority.host)) {
        ip = *ip_ptr;
      } else {
        CAF_LOG_ERROR("received malformed uri: " << CAF_ARG(locator));
        return;
      }
      ep = ip_endpoint(ip, port);
      auto conn_res = connect(ep);
      if (conn_res) {
        CAF_LOG_ERROR("connect failed: " << CAF_ARG(conn_res));
        return;
      }
      auto connection = *conn_res;
      auto id = detail::convert(connection);
      if (auto err = dispatcher_.add_new_worker(*this, nid, id)) {
        CAF_LOG_ERROR("add_new_worker_failed" << err);
      }
      // TODO: shouldn't this be triggered again later on?
      dispatcher_.resolve(*this, locator, listener);
      known_conns_.emplace(id, std::move(connection));
    } else {
      dispatcher_.resolve(*this, locator, listener);
    }
  }

  template <class Parent>
  void new_proxy(Parent&, const node_id& peer, actor_id id) {
    dispatcher_.new_proxy(*this, peer, id);
  }

  template <class Parent>
  void local_actor_down(Parent&, const node_id& peer, actor_id id,
                        error reason) {
    dispatcher_.local_actor_down(*this, peer, id, std::move(reason));
  }

  template <class Parent>
  void timeout(Parent&, atom_value value, uint64_t id) {
    dispatcher_.timeout(*this, value, id);
  }

  void set_timeout(uint64_t timeout_id, id_type id) {
    dispatcher_.set_timeout(timeout_id, id);
  }

  void handle_error(sec code) {
    dispatcher_.handle_error(code);
  }

  error add_new_worker(node_id node, id_type id) {
    return dispatcher_.add_new_worker(*this, node, id);
  }

  void prepare_next_read() {
    read_buf_->clear();
    collected_ = 0;
    // This cast does nothing, but prevents a weird compiler error on GCC
    // <= 4.9.
    // TODO: remove cast when dropping support for GCC 4.9.
    switch (static_cast<receive_policy_flag>(rd_flag_)) {
      case receive_policy_flag::exactly:
        if (read_buf_->size() != max_)
          read_buf_->resize(max_);
        read_threshold_ = max_;
        break;
      case receive_policy_flag::at_most:
        if (read_buf_->size() != max_)
          read_buf_->resize(max_);
        read_threshold_ = 1;
        break;
      case receive_policy_flag::at_least: {
        // read up to 10% more, but at least allow 100 bytes more
        auto max_size = max_ + std::max<size_t>(100, max_ / 10);
        if (read_buf_->size() != max_size)
          read_buf_->resize(max_size);
        read_threshold_ = max_;
        break;
      }
    }
  }

  void configure_read(receive_policy::config cfg) {
    rd_flag_ = cfg.first;
    max_ = cfg.second;
  }

  void write_packet(id_type ep, span<buffer_type*> buffers) {
    CAF_ASSERT(!buffers.empty());
    if (packet_queue_.empty())
      manager().register_writing();
    // By convention, the first buffer is a header buffer. Every other buffer is
    // a payload buffer.
    packet_queue_.emplace_back(ep, buffers);
  }

  // -- buffer management ------------------------------------------------------

  buffer_type next_header_buffer() {
    return next_buffer_impl(header_bufs_);
  }

  buffer_type next_payload_buffer() {
    return next_buffer_impl(payload_bufs_);
  }

  /// Helper struct for managing outgoing packets
  struct packet {
    id_type id;
    buffer_cache_type bytes;
    size_t size;

    packet(size_t destination, span<buffer_type*> bufs) : id(destination) {
      size = 0;
      for (auto buf : bufs) {
        size += buf->size();
        bytes.emplace_back(std::move(*buf));
      }
    }
  };

private:
  // -- utility functions ------------------------------------------------------

  static buffer_type next_buffer_impl(buffer_cache_type cache) {
    if (cache.empty()) {
      return {};
    }
    auto buf = std::move(cache.back());
    cache.pop_back();
    return buf;
  }

  bool write_some() {
    CAF_LOG_TRACE(CAF_ARG(handle_.id));
    // Helper function to sort empty buffers back into the right caches.
    auto recycle = [&]() {
      auto& front = packet_queue_.front();
      auto& bufs = front.bytes;
      auto it = bufs.begin();
      if (header_bufs_.size() < header_bufs_.capacity()) {
        it->clear();
        header_bufs_.emplace_back(std::move(*it++));
      }
      for (;
           it != bufs.end() && payload_bufs_.size() < payload_bufs_.capacity();
           ++it) {
        it->clear();
        payload_bufs_.emplace_back(std::move(*it));
      }
      packet_queue_.pop_front();
    };
    // Write as many bytes as possible.
    while (!packet_queue_.empty()) {
      auto& packet = packet_queue_.front();
      auto id = packet.id;
      // find connection
      auto conn_it = known_conns_.find(id);
      if (conn_it == known_conns_.end()) {
        CAF_LOG_ERROR("connection not found.");
        dispatcher_.handle_error(sec::runtime_error);
        return false;
      }
      // find_stream
      auto stream_it = known_streams_.find(id);
      if (stream_it == known_streams_.end()) {
        // stream was not opened. Something went wrong!
        CAF_LOG_ERROR("stream was not opened before");
        dispatcher_.handle_error(sec::runtime_error);
        return false;
      }
      auto stream = stream_it->second;
      auto conn = conn_it->second;
      // write data to stream
      for (auto buf : packet.bytes)
        // TODO: How to check for write_errors??
        quicly_streambuf_egress_write(stream, buf.data(), buf.size());
      if (detail::send_pending_datagrams(handle_, conn) != sec::none) {
        CAF_LOG_ERROR("send failed" << CAF_ARG(last_socket_error_as_string()));
        dispatcher_.handle_error(sec::socket_operation_failed);
        return false;
      }
      // TODO: set_timeout(parent);
      recycle();
    }
    return false;
  }

  expected<detail::quicly_conn_ptr> connect(ip_endpoint ep) {
    sockaddr_storage sa = {};
    detail::convert(ep, sa);
    quicly_conn_t* conn = nullptr;
    if (quicly_connect(&conn, &quicly_state_.ctx, nullptr,
                       reinterpret_cast<sockaddr*>(&sa), nullptr,
                       &quicly_state_.next_cid, quicly_state_.resumption_token,
                       &quicly_state_.hs_properties,
                       &quicly_state_.resumed_transport_params))
      return make_error(sec::runtime_error, "quicly_connect failed");
    auto connection = detail::make_quicly_conn_ptr(conn);
    ++quicly_state_.next_cid.master_id;
    if (auto err = detail::send_pending_datagrams(handle_, connection))
      return err;
    return connection;
  }

  // -- quicly callbacks -------------------------------------------------------

  int on_stream_open(struct st_quicly_stream_open_t*,
                     struct st_quicly_stream_t* stream) {
    CAF_LOG_TRACE("new quic stream opened");
    if (auto ret = quicly_streambuf_create(stream,
                                           sizeof(quicly_transport_streambuf)))
      return ret;
    stream->callbacks = &quicly_state_.stream_callbacks;
    reinterpret_cast<quicly_transport_streambuf*>(stream->data)->buf
      = read_buf_;
    return 0;
  }

  void on_closed_by_peer(quicly_conn_t* conn) {
    auto id = detail::convert(conn);
    known_conns_.erase(detail::convert(conn));
    known_streams_.erase(id);
    // TODO: delete worker that handles this connection.
  }

  int on_save_resumption_token(quicly_conn_t* conn, ptls_iovec_t token) {
    free(quicly_state_.session_info.address_token.base);
    quicly_state_.session_info.address_token = ptls_iovec_init(malloc(
                                                                 token.len),
                                                               token.len);
    memcpy(quicly_state_.session_info.address_token.base, token.base,
           token.len);
    return detail::save_session(quicly_get_peer_transport_parameters(conn),
                                session_file_path_, quicly_state_.session_info);
  }

  int on_generate_resumption_token(ptls_buffer_t* buf,
                                   quicly_address_token_plaintext_t* token) {
    return quicly_encrypt_address_token(quicly_state_.tlsctx.random_bytes,
                                        const_cast<ptls_aead_context_t*>(
                                          quicly_state_.address_token_aead.enc),
                                        buf, buf->off, token);
  }

  int on_save_session_ticket(ptls_t* tls, ptls_iovec_t src) {
    free(quicly_state_.session_info.tls_ticket.base);
    quicly_state_.session_info.tls_ticket = ptls_iovec_init(malloc(src.len),
                                                            src.len);
    memcpy(quicly_state_.session_info.tls_ticket.base, src.base, src.len);

    auto conn = reinterpret_cast<quicly_conn_t*>(*ptls_get_data_ptr(tls));
    return detail::save_session(quicly_get_peer_transport_parameters(conn),
                                session_file_path_, quicly_state_.session_info);
  }

  // -- transport state --------------------------------------------------------

  dispatcher_type dispatcher_;
  udp_datagram_socket handle_;

  buffer_cache_type header_bufs_;
  buffer_cache_type payload_bufs_;

  std::shared_ptr<std::vector<received_data>> read_buf_;
  std::deque<packet> packet_queue_;

  size_t max_consecutive_reads_;
  size_t read_threshold_;
  size_t collected_;
  size_t max_;
  receive_policy_flag rd_flag_;

  endpoint_manager* manager_;

  // -- quicly state -----------------------------------------------------------

  detail::quicly_state quicly_state_;

  std::unordered_map<size_t, detail::quicly_conn_ptr> known_conns_;
  // TODO: wrapping in smart_ptr is not the right thing.. But what is?
  std::unordered_map<size_t, quicly_stream_t*> known_streams_;

  // callbacks
  quicly_save_resumption_token<factory_type> save_resumption_token_;
  quicly_generate_resumption_token<factory_type> generate_resumption_token_;
  quicly_save_session_ticket<factory_type> save_session_ticket_;
  quicly_stream_open<factory_type> stream_open_;
  quicly_closed_by_peer<factory_type> closed_by_peer_;

  // -- file paths -------------------------------------------------------------
  std::string session_file_path_;
}; // namespace net

} // namespace net
} // namespace caf