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

#include <memory>

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
#include "picotls/openssl.h"
#include "quicly.h"
#include "quicly/defaults.h"
#include "quicly/streambuf.h"
}
CAF_POP_WARNINGS

#include "caf/span.hpp"

namespace caf {
namespace net {
namespace quic {

// -- callback types -----------------------------------------------------------

/// Holds the received data to pass it from the callback to the transport.
struct received_data {
  received_data(size_t id, span<byte> data) : id(id) {
    auto size = data.size();
    received.reserve(size);
    received.insert(received.begin(), data.begin(), data.end());
  }

  size_t id;
  std::vector<byte> received;
};

template <class Transport>
struct save_resumption_token : quicly_save_resumption_token_t {
  Transport* transport;
};

template <class Transport>
struct generate_resumption_token : quicly_generate_resumption_token_t {
  Transport* transport;
};

template <class Transport>
struct save_session_ticket : ptls_save_ticket_t {
  Transport* transport;
};

template <class Transport>
struct stream_open : public quicly_stream_open_t {
  Transport* transport;
};

template <class Transport>
struct closed_by_peer : public quicly_closed_by_peer_t {
  Transport* transport;
};

struct streambuf : public quicly_streambuf_t {
  std::shared_ptr<std::vector<received_data>> buf;
};

/// convenience struct for making quic context.
struct callbacks {
  callbacks() = default;
  ~callbacks() = default;

  ptls_save_ticket_t* save_ticket;
  quicly_stream_open_t* stream_open;
  quicly_closed_by_peer_t* closed_by_peer;
  quicly_save_resumption_token_t* save_resumption_token;
  quicly_generate_resumption_token_t* generate_resumption_token;
};

// -- pointer aliases ----------------------------------------------------------

// TODO: quicly_free as deleter??
using conn_ptr = std::shared_ptr<quicly_conn_t>;

using stream_ptr = std::unique_ptr<quicly_stream_t>;

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
struct state {
  state(quicly_stream_callbacks_t callbacks, std::string session_file_path);
  ~state() = default;

  char cid_key[17];
  quicly_cid_plaintext_t next_cid;
  ptls_key_exchange_algorithm_t* key_exchanges[128];
  ptls_context_t tlsctx;
  quicly_context_t ctx;
  ptls_handshake_properties_t hs_properties;
  quicly_stream_callbacks_t stream_callbacks;
  quicly_transport_parameters_t resumed_transport_params;
  ptls_iovec_t resumption_token;
  address_token_aead address_token;
  session_info session;
  std::string session_file_path;
};

} // namespace quic
} // namespace net
} // namespace caf
