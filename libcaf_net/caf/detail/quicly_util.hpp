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
#include "quicly.h"
}

namespace caf {
namespace detail {

using quicly_conn_ptr = std::shared_ptr<quicly_conn_t>;
using quicly_stream_ptr = std::unique_ptr<quicly_stream_t>;

struct st_util_save_ticket_t {
  ptls_save_ticket_t super;
  char fn[MAXPATHLEN];
};

struct st_util_log_event_t {
  ptls_log_event_t super;
  FILE* fp;
};

/* single-entry session cache */
struct st_util_session_cache_t {
  ptls_encrypt_ticket_t super;
  uint8_t id[32];
  ptls_iovec_t data;
};

size_t convert(quicly_conn_t* ptr);
size_t convert(quicly_conn_ptr ptr);

quicly_conn_ptr make_quicly_conn_ptr(quicly_conn_t* conn);
void load_certificate_chain(ptls_context_t* ctx, const char* fn);
void load_private_key(ptls_context_t* ctx, const char* fn);
// int util_save_ticket_cb(ptls_save_ticket_t* _self, ptls_t*, ptls_iovec_t
// src); void setup_session_file(ptls_context_t* ctx,
//                        ptls_handshake_properties_t* hsprop, const char* fn);
// void setup_verify_certificate(ptls_context_t* ctx);
// void setup_esni(ptls_context_t* ctx, const char* esni_fn,
//                ptls_key_exchange_context_t** key_exchanges);
// int encrypt_ticket_cb(ptls_encrypt_ticket_t* _self, ptls_t* tls, int
// is_encrypt,
//                      ptls_buffer_t* dst, ptls_iovec_t src);
void setup_session_cache(ptls_context_t* ctx);
// ptls_iovec_t resolve_esni_keys(const char* server_name);

} // namespace detail
} // namespace caf

namespace std {

template <>
struct hash<caf::detail::quicly_conn_ptr> {
  size_t operator()(const caf::detail::quicly_conn_ptr conn) const noexcept {
    return std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(conn.get()));
  }
};

} // namespace std