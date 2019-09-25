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

#include "caf/detail/quicly_util.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include <openssl/pem.h>
#include <sys/socket.h>
#include <sys/types.h>
extern "C" {
#include <picotls/minicrypto.h>
#include <picotls/openssl.h>
#include <quicly.h>
}

#include "caf/detail/fnv_hash.hpp"
#include "caf/logger.hpp"
#include "caf/sec.hpp"
#include "caf/variant.hpp"

namespace caf {
namespace detail {

// -- helper functions ---------------------------------------------------------

quicly_conn_ptr make_quicly_conn_ptr(quicly_conn_t* conn) {
  return std::shared_ptr<quicly_conn_t>(conn, quicly_free);
}

size_t convert(quicly_conn_t* ptr) noexcept {
  return reinterpret_cast<size_t>(ptr);
}

size_t convert(quicly_conn_ptr ptr) noexcept {
  return convert(ptr.get());
}

// -- quicly send functions ----------------------------------------------------

variant<size_t, sec> send_quicly_datagram(net::udp_datagram_socket handle,
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

sec send_pending_datagrams(net::udp_datagram_socket handle,
                           detail::quicly_conn_ptr conn) {
  std::vector<quicly_datagram_t*> datagrams(16);
  size_t num_packets;
  do {
    num_packets = datagrams.size() / sizeof(quicly_datagram_t);
    if (quicly_send(conn.get(), datagrams.data(), &num_packets)) {
      for (size_t i = 0; i < num_packets; ++i) {
        auto datagram = datagrams.at(i);
        auto res = send_quicly_datagram(handle, datagram);
        if (auto err = get_if<sec>(&res)) {
          return *err; // how to make error from this sec?
        }
        auto pa = quicly_get_context(conn.get())->packet_allocator;
        pa->free_packet(pa, datagram);
      }
    } else {
      break;
    }
  } while (num_packets == datagrams.size() / sizeof(quicly_datagram_t));
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
                 ptls_handshake_properties_t& hs_properties, std::string path) {
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

} // namespace detail
} // namespace caf
