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

extern "C" {
#include "quicly.h"
#include "quicly/defaults.h"
#include "quicly/streambuf.h"
}

#include "caf/byte.hpp"
#include "caf/detail/convert_ip_endpoint.hpp"
#include "caf/detail/ptls_util.hpp"
#include "caf/detail/quicly_util.hpp"
#include "caf/detail/socket_sys_aliases.hpp"
#include "caf/detail/socket_sys_includes.hpp"
#include "caf/error.hpp"
#include "caf/fwd.hpp"
#include "caf/logger.hpp"
#include "caf/net/endpoint_manager.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/receive_policy.hpp"
#include "caf/net/transport_worker_dispatcher.hpp"
#include "caf/net/udp_datagram_socket.hpp"
#include "caf/sec.hpp"
#include "caf/span.hpp"
#include "caf/variant.hpp"
#include "picotls/openssl.h"

namespace caf {
namespace net {

struct received_data {
  received_data(size_t id, span<byte> data) : id(id) {
    received.resize(data.size());
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

  using factory_type = Factory;

  using application_type = typename Factory::application_type;

  using dispatcher_type = transport_worker_dispatcher<factory_type, size_t>;

  // -- constructors, destructors, and assignment operators --------------------

  quic_transport(udp_datagram_socket handle, factory_type factory)
    : dispatcher_(std::move(factory)),
      handle_(handle),
      read_buf_(std::make_shared<std::vector<received_data>>()),
      max_consecutive_reads_(0),
      read_threshold_(1024),
      collected_(0),
      max_(1024),
      rd_flag_(receive_policy_flag::exactly),
      cid_key_{},
      key_exchanges_{},
      next_cid_{},
      tlsctx_{},
      ctx_{},
      hs_properties_{},
      stream_callbacks{
        quicly_streambuf_destroy, quicly_streambuf_egress_shift,
        quicly_streambuf_egress_emit, detail::on_stop_sending,
        // on_receive()
        [](quicly_stream_t* stream, size_t off, const void* src,
           size_t len) -> int {
          if (auto ret = quicly_streambuf_ingress_receive(stream, off, src,
                                                          len))
            return ret;
          ptls_iovec_t input;
          if ((input = quicly_streambuf_ingress_get(stream)).len) {
            CAF_LOG_TRACE("quicly received: " << CAF_ARG(input.len) << "bytes");
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
        }},
      known_conns_{},
      known_streams_{},
      save_resumption_token_{},
      generate_resumption_token_{},
      save_session_ticket_{},
      stream_open_{},
      closed_by_peer_{},
      resumed_transport_params_{},
      resumption_token_{},
      address_token_aead_{},
      session_file_path_{},
      session_info_{} {
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
  }

  // -- public member functions ------------------------------------------------

  template <class Parent>
  error init(Parent& parent) {
    if (auto err = dispatcher_.init(parent))
      return err;
    // initialize tls_context
    tlsctx_.random_bytes = ptls_openssl_random_bytes;
    tlsctx_.get_time = &ptls_get_time;
    tlsctx_.key_exchanges = key_exchanges_;
    tlsctx_.cipher_suites = ptls_openssl_cipher_suites;
    tlsctx_.require_dhe_on_psk = 1;
    tlsctx_.save_ticket = &save_session_ticket_;
    // initialize quicly_context
    ctx_ = quicly_spec_context;
    ctx_.tls = &tlsctx_;
    ctx_.stream_open = &stream_open_;
    ctx_.closed_by_peer = &closed_by_peer_;
    ctx_.save_resumption_token = &save_resumption_token_;
    ctx_.generate_resumption_token = &generate_resumption_token_;
    // initialize session_context
    detail::setup_session_cache(ctx_.tls);
    quicly_amend_ptls_context(ctx_.tls);
    // generate cypher context for en-/decryption
    uint8_t secret[PTLS_MAX_DIGEST_SIZE];
    ctx_.tls->random_bytes(secret, ptls_openssl_sha384.digest_size);
    address_token_aead_.enc = ptls_aead_new(&ptls_openssl_aes128gcm,
                                            &ptls_openssl_sha384, 1, secret,
                                            "");
    address_token_aead_.dec = ptls_aead_new(&ptls_openssl_aes128gcm,
                                            &ptls_openssl_sha384, 0, secret,
                                            "");
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
    if (detail::load_certificate_chain(ctx_.tls, certificate_chain_path))
      return make_error(sec::runtime_error, "failed to load certificate chain: "
                                              + certificate_chain_path);
    if (detail::load_private_key(ctx_.tls, private_key_path))
      return make_error(sec::runtime_error,
                        "failed to load private keys: " + private_key_path);
    key_exchanges_[0] = &ptls_openssl_secp256r1;
    tlsctx_.random_bytes(cid_key_, sizeof(cid_key_) - 1);
    auto iovec = ptls_iovec_init(cid_key_, sizeof(cid_key_) - 1);
    auto cid_cipher = &ptls_openssl_bfecb;
    auto reset_token_cipher = &ptls_openssl_aes128ecb;
    auto hash = &ptls_openssl_sha384;
    auto default_encryptor = quicly_new_default_cid_encryptor(
      cid_cipher, reset_token_cipher, hash, iovec);
    ctx_.cid_encryptor = default_encryptor;
    detail::load_session(&resumed_transport_params_, resumption_token_,
                         hs_properties_, session_file_path_);
    parent.mask_add(operation::read);
    return none;
  }

  template <class Parent>
  bool handle_read_event(Parent& parent) {
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
      auto plen = quicly_decode_packet(&ctx_, &packet, buf + off,
                                       read_pair.first - off);
      if (plen == SIZE_MAX)
        break;
      if (QUICLY_PACKET_IS_LONG_HEADER(packet.octets.base[0])) {
        if (packet.version != QUICLY_PROTOCOL_VERSION) {
          auto rp = quicly_send_version_negotiation(&ctx_,
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
                [&](const std::pair<size_t, detail::quicly_conn_ptr>& p) {
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
          dispatcher_.handle_data(parent, make_span(data.received), data.id);
        read_buf_->clear();
        detail::send_pending_datagrams(handle_, conn);
      } else if (QUICLY_PACKET_IS_LONG_HEADER(packet.octets.base[0])) {
        // new connection
        quicly_address_token_plaintext_t* token = nullptr;
        quicly_address_token_plaintext_t token_buf;
        if (packet.token.len != 0
            && quicly_decrypt_address_token(const_cast<ptls_aead_context_t*>(
                                              address_token_aead_.dec),
                                            &token_buf, packet.token.base,
                                            packet.token.len, 0)
                 == 0
            && detail::validate_token(reinterpret_cast<sockaddr*>(&sa),
                                      packet.cid.src, packet.cid.dest.encrypted,
                                      &token_buf, &ctx_))
          token = &token_buf;

        quicly_conn_t* conn = nullptr;
        int accept_res = quicly_accept(&conn, &ctx_, nullptr,
                                       reinterpret_cast<sockaddr*>(&sa),
                                       &packet, token, &next_cid_, nullptr);
        if (accept_res == 0 && conn) {
          CAF_LOG_TRACE("accepted new quic connection");
          auto id = detail::convert(conn);
          auto conn_ptr = detail::make_quicly_conn_ptr(conn);
          ++next_cid_.master_id;
          if (auto err = dispatcher_.add_new_worker(parent, node_id{}, id)) {
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
          auto dgram = quicly_send_stateless_reset(&ctx_,
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
      if (quicly_get_first_timeout(p.second.get()) <= ctx_.now->cb(ctx_.now)) {
        if (detail::send_pending_datagrams(handle_, p.second) != sec::none) {
          auto id = detail::convert(p.second);
          known_conns_.erase(id);
          known_streams_.erase(id);
        }
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
      auto decorator = make_write_packet_decorator(*this, parent);
      dispatcher_.write_message(decorator, std::move(msg));
    }
    // Write prepared data.
    return write_some();
  }

  template <class Parent>
  void resolve(Parent& parent, const std::string& path, actor listener) {
    dispatcher_.resolve(parent, path, listener);
  }

  template <class Parent>
  void timeout(Parent& parent, atom_value value, uint64_t id) {
    auto decorator = make_write_packet_decorator(*this, parent);
    dispatcher_.timeout(decorator, value, id);
  }

  void set_timeout(uint64_t timeout_id, detail::quicly_conn_ptr ep) {
    dispatcher_.set_timeout(timeout_id, ep);
  }

  void handle_error(sec code) {
    dispatcher_.handle_error(code);
  }

  udp_datagram_socket handle() const noexcept {
    return handle_;
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

  template <class Parent>
  void write_packet(Parent&, span<const byte> header, span<const byte> payload,
                    size_t id) {
    std::vector<byte> buf;
    buf.reserve(header.size() + payload.size());
    buf.insert(buf.end(), header.begin(), header.end());
    buf.insert(buf.end(), payload.begin(), payload.end());
    packet_queue_.emplace_back(id, std::move(buf));
  }

  struct packet {
    size_t id;
    std::vector<byte> bytes;

    packet(size_t id, std::vector<byte> bytes)
      : id(id), bytes(std::move(bytes)) {
      // nop
    }
  };

private:
  bool write_some() {
    if (packet_queue_.empty())
      return false;
    auto& next_packet = packet_queue_.front();
    auto id = next_packet.id;
    auto& bytes = next_packet.bytes;
    auto buf = bytes.data();
    auto len = bytes.size();
    // stream should be opened by now. If not move check back here!
    auto conn_it = known_conns_.find(id);
    if (conn_it == known_conns_.end()) {
      CAF_LOG_ERROR("connection not found.");
      dispatcher_.handle_error(sec::runtime_error);
      return false;
    }
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
    quicly_streambuf_egress_write(stream, buf, len);
    if (detail::send_pending_datagrams(handle_, conn) != sec::none) {
      CAF_LOG_ERROR("send failed" << CAF_ARG(last_socket_error_as_string()));
      dispatcher_.handle_error(sec::socket_operation_failed);
      return false;
    }
    // set_timeout(parent);
    packet_queue_.pop_front();
    return true;
  }

  int on_stream_open(struct st_quicly_stream_open_t*,
                     struct st_quicly_stream_t* stream) {
    CAF_LOG_TRACE("new quic stream opened");
    if (auto ret = quicly_streambuf_create(stream,
                                           sizeof(quicly_transport_streambuf)))
      return ret;
    stream->callbacks = &stream_callbacks;
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
    free(session_info_.address_token.base);
    session_info_.address_token = ptls_iovec_init(malloc(token.len), token.len);
    memcpy(session_info_.address_token.base, token.base, token.len);
    return detail::save_session(quicly_get_peer_transport_parameters(conn),
                                session_file_path_, session_info_);
  }

  int on_generate_resumption_token(ptls_buffer_t* buf,
                                   quicly_address_token_plaintext_t* token) {
    return quicly_encrypt_address_token(tlsctx_.random_bytes,
                                        const_cast<ptls_aead_context_t*>(
                                          address_token_aead_.enc),
                                        buf, buf->off, token);
  }

  int on_save_session_ticket(ptls_t* tls, ptls_iovec_t src) {
    free(session_info_.tls_ticket.base);
    session_info_.tls_ticket = ptls_iovec_init(malloc(src.len), src.len);
    memcpy(session_info_.tls_ticket.base, src.base, src.len);

    auto conn = reinterpret_cast<quicly_conn_t*>(*ptls_get_data_ptr(tls));
    return detail::save_session(quicly_get_peer_transport_parameters(conn),
                                session_file_path_, session_info_);
  }
  dispatcher_type dispatcher_;
  udp_datagram_socket handle_;

  std::shared_ptr<std::vector<received_data>> read_buf_;
  std::deque<packet> packet_queue_;

  size_t max_consecutive_reads_;
  size_t read_threshold_;
  size_t collected_;
  size_t max_;
  receive_policy_flag rd_flag_;

  // -- quicly state -----------------------------------------------------------

  char cid_key_[17];
  quicly_cid_plaintext_t next_cid_;
  ptls_key_exchange_algorithm_t* key_exchanges_[128];
  ptls_context_t tlsctx_;
  quicly_context_t ctx_;
  ptls_handshake_properties_t hs_properties_;

  quicly_stream_callbacks_t stream_callbacks;
  std::unordered_map<size_t, detail::quicly_conn_ptr> known_conns_;
  // TODO: wrapping in smart_ptr is not the right thing.. But what is?
  std::unordered_map<size_t, quicly_stream_t*> known_streams_;

  // -- quicly callbacks -------------------------------------------------------

  quicly_save_resumption_token<factory_type> save_resumption_token_;
  quicly_generate_resumption_token<factory_type> generate_resumption_token_;
  quicly_save_session_ticket<factory_type> save_session_ticket_;
  quicly_stream_open<factory_type> stream_open_;
  quicly_closed_by_peer<factory_type> closed_by_peer_;

  quicly_transport_parameters_t resumed_transport_params_;
  ptls_iovec_t resumption_token_;
  detail::address_token_aead address_token_aead_;
  std::string session_file_path_;
  detail::session_info session_info_;

}; // namespace net

} // namespace net
} // namespace caf