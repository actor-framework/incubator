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
#include <picotls/openssl.h>
#include <quicly.h>
#include <quicly/defaults.h>
#include <quicly/streambuf.h>
}
CAF_POP_WARNINGS

#include "caf/detail/ptls_util.hpp"
#include "caf/detail/quicly_util.hpp"
#include "caf/error.hpp"
#include "caf/sec.hpp"
#include "caf/span.hpp"

namespace caf {
namespace net {
namespace quic {

struct callbacks {
  callbacks() = default;
  ~callbacks() = default;

  ptls_save_ticket_t* save_ticket;
  quicly_stream_open_t* stream_open;
  quicly_closed_by_peer_t* closed_by_peer;
  quicly_save_resumption_token_t* save_resumption_token;
  quicly_generate_resumption_token_t* generate_resumption_token;
};

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

error make_server_context(detail::quicly_state& state, callbacks callbacks);

} // namespace quic
} // namespace net
} // namespace caf
