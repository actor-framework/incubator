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

#include "caf/detail/quicly_util.hpp"
#include "caf/net/udp_datagram_socket.hpp"
#include "caf/variant.hpp"
extern "C" {
#include "quicly.h"
#include "quicly/defaults.h"
#include "quicly/streambuf.h"
}

namespace caf {
namespace detail {

// -- callbacks ----------------------------------------------------------------

int on_stop_sending(quicly_stream_t* stream, int err);
int save_ticket_cb(ptls_save_ticket_t* _self, ptls_t* tls, ptls_iovec_t src);

// -- quicly send functions ----------------------------------------------------

variant<size_t, sec> send_quicly_datagram(net::udp_datagram_socket handle,
                                          quicly_datagram_t* p);
sec send_pending_datagrams(net::udp_datagram_socket handle,
                           detail::quicly_conn_ptr conn);

// -- ticket management --------------------------------------------------------

void load_ticket(ptls_handshake_properties_t* hs_properties,
                 quicly_transport_parameters_t* resumed_transport_params);

// -- session handling ---------------------------------------------------------

int save_session(const quicly_transport_parameters_t* transport_params,
                 const std::string& session_file_path, session_info info);

} // namespace detail
} // namespace caf
