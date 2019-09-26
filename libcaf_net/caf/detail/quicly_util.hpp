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

/*
 * Copyright (c) 2016,2017 DeNA Co., Ltd., Kazuho Oku, Fastly
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include <functional>
#include <memory>
#include <sys/param.h>
extern "C" {
#include <quicly.h>
}

#include "caf/fwd.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/udp_datagram_socket.hpp"

namespace caf {
namespace detail {

// -- type aliases -------------------------------------------------------------

using quicly_conn_ptr = std::shared_ptr<quicly_conn_t>;

using quicly_stream_ptr = std::unique_ptr<quicly_stream_t>;

// -- needed struct definitions ------------------------------------------------

/// stores informations about a quic session.
struct session_info {
  ptls_iovec_t tls_ticket;
  ptls_iovec_t address_token;
};

/// stores encryption and decryption contexts.
struct address_token_aead {
  ptls_aead_context_t* enc;
  ptls_aead_context_t* dec;
};

// -- useful struct definitions ------------------------------------------------

/// stores all necessary fields for a quicly connection.
struct quicly_state {
  explicit quicly_state(quicly_stream_callbacks_t callbacks);
  ~quicly_state() = default;

  char cid_key[17];
  quicly_cid_plaintext_t next_cid;
  ptls_key_exchange_algorithm_t* key_exchanges[128];
  ptls_context_t tlsctx;
  quicly_context_t ctx;
  ptls_handshake_properties_t hs_properties;
  quicly_stream_callbacks_t stream_callbacks;
  quicly_transport_parameters_t resumed_transport_params;
  ptls_iovec_t resumption_token;
  detail::address_token_aead address_token_aead;
  detail::session_info session_info;
};

// -- helper functions ---------------------------------------------------------

/// makes a `quicly_conn_ptr` aka `std::shared_ptr<quicly_conn_t>` from given
/// `quicly_conn_t*`
quicly_conn_ptr make_quicly_conn_ptr(quicly_conn_t* conn);
/// converts a `quicly_conn_t*` to `size_t` for use as key/id_type.
size_t convert(quicly_conn_t* ptr) noexcept;
/// converts a `quicly_conn_ptr` to `size_t` for use as key/id_type.
size_t convert(quicly_conn_ptr ptr) noexcept;

// -- quicly send functions ----------------------------------------------------

/// sends a single `quicly_datagram_t` to its endpoint.
variant<size_t, sec> send_quicly_datagram(net::udp_datagram_socket handle,
                                          quicly_datagram_t* p);
/// sends pending `quicly_datagram_t` for given connection to their endpoint.
error send_pending_datagrams(net::udp_datagram_socket handle,
                             detail::quicly_conn_ptr conn);

// -- quicly default callbacks -------------------------------------------------

///
int on_stop_sending(quicly_stream_t* stream, int err);

// -- general quicly routines --------------------------------------------------

///
int validate_token(sockaddr* remote, ptls_iovec_t client_cid,
                   ptls_iovec_t server_cid,
                   quicly_address_token_plaintext_t* token,
                   quicly_context_t* ctx);
///
int load_session(quicly_transport_parameters_t* params,
                 ptls_iovec_t& resumption_token,
                 ptls_handshake_properties_t& hs_properties, std::string path);
///
int save_session(const quicly_transport_parameters_t* transport_params,
                 const std::string& session_file_path, session_info info);

} // namespace detail
} // namespace caf
