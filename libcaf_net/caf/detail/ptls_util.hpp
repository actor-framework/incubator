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
 ******************************************************************************
 *
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

#include <cstdio>
#include <string>
#include <sys/param.h>
#include <sys/socket.h>
extern "C" {
#include <picotls.h>
}

namespace caf {
namespace detail {

// -- struct definitions -------------------------------------------------------

///
struct st_util_save_ticket_t {
  ptls_save_ticket_t super;
  char fn[MAXPATHLEN];
};

///
struct st_util_log_event_t {
  ptls_log_event_t super;
  FILE* fp;
};

///
struct st_util_session_cache_t {
  ptls_encrypt_ticket_t super;
  uint8_t id[32];
  ptls_iovec_t data;
};

// -- picotls util functions ---------------------------------------------------

///
int load_certificate_chain(ptls_context_t* ctx, std::string path);
///
int load_private_key(ptls_context_t* ctx, std::string path);
///
int util_save_ticket_cb(ptls_save_ticket_t* _self, ptls_t* tls,
                        ptls_iovec_t src);
///
int setup_session_file(ptls_context_t* ctx, ptls_handshake_properties_t* hsprop,
                       std::string path);
///
void setup_verify_certificate(ptls_context_t* ctx);
////
int setup_esni(ptls_context_t* ctx, std::string path,
               ptls_key_exchange_context_t** key_exchanges);
///
void log_event_cb(ptls_log_event_t* _self, ptls_t* tls, const char* type,
                  const char* fmt, ...);
///
int setup_log_event(ptls_context_t* ctx, std::string path);
///
int encrypt_ticket_cb(ptls_encrypt_ticket_t* _self, ptls_t* tls, int is_encrypt,
                      ptls_buffer_t* dst, ptls_iovec_t src);
///
void setup_session_cache(ptls_context_t* ctx);
///
int resolve_address(sockaddr* sa, socklen_t* salen, std::string host,
                    uint16_t port, int family, int type, int proto);
/// converts base64 encoded keys to c_strings. Weird function..
int normalize_txt(uint8_t* p, size_t len);
///
ptls_iovec_t resolve_esni_keys(std::string server_name);

} // namespace detail
} // namespace caf
