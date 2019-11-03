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

#include "caf/net/quic/quic.hpp"

#include <string>

#include "caf/detail/ptls_util.hpp"
#include "caf/error.hpp"
#include "caf/logger.hpp"
#include "caf/net/udp_datagram_socket.hpp"
#include "caf/sec.hpp"
#include "caf/variant.hpp"

namespace caf {
namespace net {
namespace quic {

/// Creates `quic_context` which is shared between server and client modes.
void make_default_context(state& state, callbacks callbacks) {
  // initialize tls_context
  state.tlsctx.random_bytes = ptls_openssl_random_bytes;
  state.tlsctx.get_time = &ptls_get_time;
  state.tlsctx.key_exchanges = state.key_exchanges;
  state.tlsctx.cipher_suites = ptls_openssl_cipher_suites;
  state.tlsctx.require_dhe_on_psk = 1;
  state.tlsctx.save_ticket = callbacks.save_ticket;
  // initialize quicly_context
  state.ctx = quicly_spec_context;
  state.ctx.tls = &state.tlsctx;
  state.ctx.stream_open = callbacks.stream_open;
  state.ctx.closed_by_peer = callbacks.closed_by_peer;
  state.ctx.save_resumption_token = callbacks.save_resumption_token;
  state.ctx.generate_resumption_token = callbacks.generate_resumption_token;
  // initialize session_context
  detail::setup_session_cache(state.ctx.tls);
  quicly_amend_ptls_context(state.ctx.tls);
  // generate cypher context for en-/decryption
  {
    uint8_t secret[PTLS_MAX_DIGEST_SIZE];
    state.ctx.tls->random_bytes(secret, ptls_openssl_sha256.digest_size);
    state.address_token.enc = ptls_aead_new(&ptls_openssl_aes128gcm,
                                            &ptls_openssl_sha256, 1, secret,
                                            "");
    state.address_token.dec = ptls_aead_new(&ptls_openssl_aes128gcm,
                                            &ptls_openssl_sha256, 0, secret,
                                            "");
  }
}

/// Creates `quic_context` which is used only by server mode.
error make_server_context(state& state, callbacks callbacks) {
  make_default_context(state, callbacks);
  // read certificates from file.
  // TODO: This should come from the caf config.
  std::string path_to_certs;
  if (auto path = getenv("QUICLY_CERTS")) {
    path_to_certs = path;
  } else {
    // try to load default certs
    path_to_certs = "/home/boss/code/quicly/t/assets/";
  }
  auto certificate_chain_path = (path_to_certs + std::string("server.crt"));
  auto private_key_path = (path_to_certs + std::string("server.key"));
  if (detail::load_certificate_chain(state.ctx.tls, certificate_chain_path))
    return make_error(sec::runtime_error, "failed to load certificate chain: "
                                            + certificate_chain_path);
  if (detail::load_private_key(state.ctx.tls, private_key_path))
    return make_error(sec::runtime_error,
                      "failed to load private keys: " + private_key_path);
  CAF_ASSERT(state.ctx.tls->certificates.count != 0
             || state.ctx.tls->sign_certificate != nullptr);
  state.key_exchanges[0] = &ptls_openssl_secp256r1;
  state.tlsctx.random_bytes(state.cid_key, sizeof(state.cid_key) - 1);
  auto iovec = ptls_iovec_init(state.cid_key, sizeof(state.cid_key) - 1);
  auto cid_cipher = &ptls_openssl_bfecb;
  auto reset_token_cipher = &ptls_openssl_aes128ecb;
  auto hash = &ptls_openssl_sha256;
  state.ctx.cid_encryptor = quicly_new_default_cid_encryptor(cid_cipher,
                                                             reset_token_cipher,
                                                             hash, iovec);
  return none;
}

/// Creates `quic_context` which is used only by client mode.
error make_client_context(state& state, callbacks callbacks) {
  make_default_context(state, callbacks);
  state.hs_properties.client.negotiated_protocols.count = 0;
  load_session(&state.resumed_transport_params, state.resumption_token,
               state.hs_properties, state.session_file_path);
  return none;
}

// -- quicly_state constructors ------------------------------------------------

state::state(quicly_stream_callbacks_t callbacks, std::string session_file_path)
  : cid_key{},
    next_cid{},
    key_exchanges{},
    tlsctx{},
    ctx{},
    hs_properties{},
    stream_callbacks{callbacks},
    resumed_transport_params{},
    resumption_token{},
    address_token{},
    session{},
    session_file_path{std::move(session_file_path)} {
}

// -- helper functions ---------------------------------------------------------

conn_ptr make_conn_ptr(quicly_conn_t* conn) {
  return {conn, quicly_free};
}

size_t convert(quicly_conn_t* ptr) noexcept {
  return reinterpret_cast<size_t>(ptr);
}

size_t convert(const conn_ptr& ptr) noexcept {
  return convert(ptr.get());
}

// -- quicly send functions ----------------------------------------------------

// TODO: This needs to be integrated into transport!!
variant<size_t, sec> send_datagram(net::udp_datagram_socket handle,
                                   quicly_datagram_t* datagram) {
  msghdr mess = {};
  iovec vec = {};
  mess.msg_name = &datagram->dest.sa;
  mess.msg_namelen = quicly_get_socklen(&datagram->dest.sa);
  vec.iov_base = datagram->data.base;
  vec.iov_len = datagram->data.len;
  mess.msg_iov = &vec;
  mess.msg_iovlen = 1;
  auto ret = sendmsg(handle.id, &mess, 0);
  return net::check_udp_datagram_socket_io_res(ret);
}

error send_pending_datagrams(net::udp_datagram_socket handle, conn_ptr conn) {
  const size_t max_num_packets = 16;
  int ret = 0;
  std::vector<quicly_datagram_t*> packets(max_num_packets);

  do {
    packets.resize(max_num_packets);
    auto num_packets = packets.size();
    if ((ret = quicly_send(conn.get(), packets.data(), &num_packets)) == 0) {
      packets.resize(num_packets);
      for (auto packet : packets) {
        auto res = send_datagram(handle, packet);
        auto pa = quicly_get_context(conn.get())->packet_allocator;
        pa->free_packet(pa, packet);
        if (auto err = get_if<sec>(&res)) {
          return make_error(*err, "send_datagram_failed");
        }
      }
    } else {
      return make_error(sec::runtime_error, "quicly_send failed");
    }
  } while (ret == 0 && packets.size() == max_num_packets);
  return sec::none;
}

// -- quicly default callbacks -------------------------------------------------

int on_stop_sending(quicly_stream_t*, int err) {
  assert(QUICLY_ERROR_IS_QUIC_APPLICATION(err));
  CAF_LOG_TRACE(
    "received STOP_SENDING: " << CAF_ARG(QUICLY_ERROR_GET_ERROR_CODE(err)));
  return 0;
}

// -- general quicly routines --------------------------------------------------

int validate_token(sockaddr* remote, ptls_iovec_t client_cid,
                   ptls_iovec_t server_cid,
                   quicly_address_token_plaintext_t* token,
                   quicly_context_t* ctx) {
  int64_t age;
  int port_is_equal;
  if ((age = ctx->now->cb(ctx->now) - token->issued_at) < 0)
    age = 0;
  if (remote->sa_family != token->remote.sa.sa_family)
    return 0;
  switch (remote->sa_family) {
    case AF_INET: {
      auto sin = reinterpret_cast<sockaddr_in*>(remote);
      if (sin->sin_addr.s_addr != token->remote.sin.sin_addr.s_addr)
        return 0;
      port_is_equal = sin->sin_port == token->remote.sin.sin_port;
    } break;
    case AF_INET6: {
      auto sin6 = reinterpret_cast<sockaddr_in6*>(remote);
      if (memcmp(&sin6->sin6_addr, &token->remote.sin6.sin6_addr,
                 sizeof(sin6->sin6_addr))
          != 0)
        return 0;
      port_is_equal = sin6->sin6_port == token->remote.sin6.sin6_port;
    } break;
    default:
      return 0;
  }
  if (token->is_retry) {
    if (age > 30000)
      return 0;
    if (!port_is_equal)
      return 0;
    uint64_t cidhash_actual;
    if (quicly_retry_calc_cidpair_hash(&ptls_openssl_sha256, client_cid,
                                       server_cid, &cidhash_actual)
        != 0)
      return 0;
    if (token->retry.cidpair_hash != cidhash_actual)
      return 0;
  } else if (age > 10 * 60 * 1000) {
    return 0;
  }
  return 1;
}

int load_session(quicly_transport_parameters_t* params,
                 ptls_iovec_t& resumption_token,
                 ptls_handshake_properties_t& hs_properties,
                 const std::string& path) {
  static uint8_t buf[65536];
  int ret;
  std::ifstream session_file(path, std::ios::binary);
  if (!session_file.is_open())
    return -1;
  auto session_file_size = session_file.readsome(reinterpret_cast<char*>(buf),
                                                 sizeof(buf));
  if (session_file_size == 0 || !session_file.eof()) {
    CAF_LOG_ERROR("failed to load ticket from file: " << CAF_ARG(path));
    return -2;
  }
  session_file.close();
  const uint8_t *src = buf, *end = buf + session_file_size;
  ptls_iovec_t ticket;
  ptls_decode_open_block(src, end, 2, {
    ticket = ptls_iovec_init(src, end - src);
    src = end;
  });
  ptls_decode_open_block(src, end, 2, {
    if ((ret = quicly_decode_transport_parameter_list(params, nullptr, nullptr,
                                                      1, src, end))
        != 0)
      goto Exit;
    src = end;
  });
  ptls_decode_open_block(src, end, 2, {
    if ((resumption_token.len = end - src) != 0) {
      resumption_token.base = reinterpret_cast<uint8_t*>(
        malloc(resumption_token.len));
      memcpy(resumption_token.base, src, resumption_token.len);
    }
    src = end;
  });
  hs_properties.client.session_ticket = ticket;
Exit:;
  return ret;
}

int save_session(const quicly_transport_parameters_t* transport_params,
                 const std::string& session_file_path, session_info info) {
  ptls_buffer_t buf;
  int ret;
  if (session_file_path.empty())
    return 0;
  std::ofstream session_file(session_file_path,
                             std::ios::out | std::ios::app | std::ios::binary);
  if (!session_file.is_open()) {
    CAF_LOG_ERROR("failed to open file:" << CAF_ARG(session_file_path) << ": "
                                         << CAF_ARG(strerror(errno)));
    ret = PTLS_ERROR_LIBRARY;
    goto Exit;
  }
  ptls_buffer_init(&buf, const_cast<char*>(""), 0);
  // build data (session ticket and transport parameters)
  ptls_buffer_push_block(&buf, 2, {
    ptls_buffer_pushv(&buf, info.tls_ticket.base, info.tls_ticket.len);
  });
  ptls_buffer_push_block(&buf, 2, {
    if ((ret = quicly_encode_transport_parameter_list(&buf, 1, transport_params,
                                                      nullptr, nullptr))
        != 0)
      goto Exit;
  });
  ptls_buffer_push_block(&buf, 2, {
    ptls_buffer_pushv(&buf, info.address_token.base, info.address_token.len);
  });

  // write file
  session_file.write(reinterpret_cast<const char*>(buf.base), buf.off);
  ret = 0;
Exit:
  if (session_file.is_open())
    session_file.close();
  ptls_buffer_dispose(&buf);
  return ret;
}

} // namespace quic
} // namespace net
} // namespace caf
