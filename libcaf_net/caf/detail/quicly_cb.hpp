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

extern "C" {
#include "quicly.h"
#include "quicly/defaults.h"
#include "quicly/streambuf.h"
}

namespace detail {

int on_stop_sending(quicly_stream_t* stream, int err);
int on_receive_reset(quicly_stream_t* stream, int err);
void on_closed_by_peer(quicly_closed_by_peer_t* self, quicly_conn_t* conn,
                       int err, uint64_t frame_type, const char* reason,
                       size_t reason_len);
int send_one(int fd, quicly_datagram_t* p);
int send_pending(int fd, quicly_conn_t* conn);
int save_ticket_cb(ptls_save_ticket_t* _self, ptls_t* tls, ptls_iovec_t src);
void load_ticket(ptls_handshake_properties_t* hs_properties,
                 quicly_transport_parameters_t* resumed_transport_params);

} // namespace detail
