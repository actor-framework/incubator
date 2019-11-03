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

#include "caf/detail/ptls_util.hpp"

#include <arpa/nameser.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <resolv.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
extern "C" {
#include <openssl/pem.h>
#include <picotls/openssl.h>
#include <picotls/pembase64.h>
}

#include "caf/logger.hpp"

namespace caf {
namespace detail {

int load_certificate_chain(ptls_context_t* ctx, std::string path) {
  if (ptls_load_certificates(ctx, path.c_str()) != 0) {
    CAF_LOG_ERROR("failed to load certificate: " << CAF_ARG(path) << ":"
                                                 << CAF_ARG(strerror(errno)));
    return -1;
  }
  return 0;
}

int load_private_key(ptls_context_t* ctx, std::string path) {
  static ptls_openssl_sign_certificate_t sc;
  FILE* fp;
  EVP_PKEY* pkey;
  if ((fp = fopen(path.c_str(), "rb")) == nullptr) {
    CAF_LOG_ERROR("failed to open file: " << CAF_ARG(path) << ":"
                                          << CAF_ARG(strerror(errno)));
    return -1;
  }
  pkey = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
  fclose(fp);
  if (pkey == nullptr) {
    CAF_LOG_ERROR("failed to read private key from file: " << CAF_ARG(path));
    return -2;
  }
  ptls_openssl_init_sign_certificate(&sc, pkey);
  EVP_PKEY_free(pkey);
  ctx->sign_certificate = &sc.super;
  return 0;
}

int util_save_ticket_cb(ptls_save_ticket_t* _self, ptls_t*, ptls_iovec_t src) {
  auto self = reinterpret_cast<st_util_save_ticket_t*>(_self);
  std::ofstream ticket_file(self->fn, std::ios::binary);
  if (!ticket_file.is_open()) {
    CAF_LOG_ERROR("failed to open file: " << CAF_ARG(self->fn) << ":"
                                          << CAF_ARG(strerror(errno)));
    return PTLS_ERROR_LIBRARY;
  }
  ticket_file.write(reinterpret_cast<char*>(src.base), src.len);
  ticket_file.close();
  return 0;
}

int setup_session_file(ptls_context_t* ctx, ptls_handshake_properties_t* hsprop,
                       std::string path) {
  static struct st_util_save_ticket_t st;
  std::ifstream session_file(path, std::ios::binary);
  // setup save_ticket callback
  strcpy(st.fn, path.c_str());
  st.super.cb = util_save_ticket_cb;
  ctx->save_ticket = &st.super;
  // load session ticket if possible
  if (session_file.is_open()) {
    static uint8_t ticket[16384];
    auto ticket_size = session_file.readsome(reinterpret_cast<char*>(ticket),
                                             sizeof(ticket));
    if (ticket_size == 0 || !session_file.eof()) {
      CAF_LOG_ERROR("failed to load ticket from file: " << CAF_ARG(path));
      return -1;
    }
    session_file.close();
    hsprop->client.session_ticket = ptls_iovec_init(ticket, ticket_size);
  } else {
    CAF_LOG_ERROR("failed to open file: " << CAF_ARG(path) << ":"
                                          << CAF_ARG(strerror(errno)));
    return -2;
  }
  return 0;
}

void setup_verify_certificate(ptls_context_t* ctx) {
  static ptls_openssl_verify_certificate_t vc;
  ptls_openssl_init_verify_certificate(&vc, nullptr);
  ctx->verify_certificate = &vc.super;
}

int setup_esni(ptls_context_t* ctx, std::string path,
               ptls_key_exchange_context_t** key_exchanges) {
  uint8_t esnikeys[65536];
  std::ifstream esni_file(path, std::ios::binary);
  if (!esni_file.is_open()) {
    CAF_LOG_ERROR("failed to open file: " << CAF_ARG(path) << ":"
                                          << CAF_ARG(strerror(errno)));
    return -1;
  }
  auto esni_key_size = esni_file.readsome(reinterpret_cast<char*>(esnikeys),
                                          sizeof(esnikeys));
  if (esni_key_size == 0 || !esni_file.eof()) {
    CAF_LOG_ERROR("failed to load ESNI data from file: " << CAF_ARG(path));
    return -2;
  }
  esni_file.close();
  if ((ctx->esni = reinterpret_cast<ptls_esni_context_t**>(
         malloc(sizeof(*ctx->esni) * 2)))
        == nullptr
      || (*ctx->esni = reinterpret_cast<ptls_esni_context_t*>(
            malloc(sizeof(**ctx->esni))))
           == nullptr) {
    CAF_LOG_ERROR("no memory");
    return -3;
  }
  if (auto ret = ptls_esni_init_context(ctx, ctx->esni[0],
                                        ptls_iovec_init(esnikeys,
                                                        esni_key_size),
                                        key_exchanges)) {
    CAF_LOG_ERROR("failed to parse ESNI data of file:"
                  << CAF_ARG(path) << ":error=" << CAF_ARG(ret));
    return -4;
  }
  return 0;
}

void log_event_cb(ptls_log_event_t* _self, ptls_t* tls, const char* type,
                  const char* fmt, ...) {
  auto self = reinterpret_cast<st_util_log_event_t*>(_self);
  char randomhex[PTLS_HELLO_RANDOM_SIZE * 2 + 1];
  va_list args;
  ptls_hexdump(randomhex, ptls_get_client_random(tls).base,
               PTLS_HELLO_RANDOM_SIZE);
  fprintf(self->fp, "%s %s ", type, randomhex);
  va_start(args, fmt);
  vfprintf(self->fp, fmt, args);
  va_end(args);
  fprintf(self->fp, "\n");
  fflush(self->fp);
}

int setup_log_event(ptls_context_t* ctx, std::string path) {
  static struct st_util_log_event_t ls;
  if ((ls.fp = fopen(path.c_str(), "at")) == nullptr) {
    CAF_LOG_ERROR("failed to open file:" << CAF_ARG(path) << ":"
                                         << CAF_ARG(strerror(errno)));
    return -1;
  }
  ls.super.cb = log_event_cb;
  ctx->log_event = &ls.super;
  return 0;
}

int encrypt_ticket_cb(ptls_encrypt_ticket_t* _self, ptls_t* tls, int is_encrypt,
                      ptls_buffer_t* dst, ptls_iovec_t src) {
  auto self = reinterpret_cast<st_util_session_cache_t*>(_self);
  if (is_encrypt) {
    // replace the cached entry along with a newly generated session id
    free(self->data.base);
    if ((self->data.base = reinterpret_cast<uint8_t*>(malloc(src.len)))
        == nullptr)
      return PTLS_ERROR_NO_MEMORY;
    ptls_get_context(tls)->random_bytes(self->id, sizeof(self->id));
    memcpy(self->data.base, src.base, src.len);
    self->data.len = src.len;
    // store the session id in buffer
    if (auto ret = ptls_buffer_reserve(dst, sizeof(self->id)))
      return ret;
    memcpy(dst->base + dst->off, self->id, sizeof(self->id));
    dst->off += sizeof(self->id);
  } else {
    // check if session id is the one stored in cache
    if (src.len != sizeof(self->id))
      return PTLS_ERROR_SESSION_NOT_FOUND;
    if (memcmp(self->id, src.base, sizeof(self->id)) != 0)
      return PTLS_ERROR_SESSION_NOT_FOUND;
    // return the cached value
    if (auto ret = ptls_buffer_reserve(dst, self->data.len))
      return ret;
    memcpy(dst->base + dst->off, self->data.base, self->data.len);
    dst->off += self->data.len;
  }
  return 0;
}

void setup_session_cache(ptls_context_t* ctx) {
  static struct st_util_session_cache_t sc;
  sc.super.cb = encrypt_ticket_cb;
  ctx->ticket_lifetime = 86400;
  ctx->max_early_data_size = 8192;
  ctx->encrypt_ticket = &sc.super;
}

int resolve_address(sockaddr* sa, socklen_t* salen, std::string host,
                    uint16_t port, int family, int type, int proto) {
  addrinfo hints = {};
  addrinfo* res;
  int err;
  hints.ai_family = family;
  hints.ai_socktype = type;
  hints.ai_protocol = proto;
  hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV | AI_PASSIVE;
  if ((err = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                         &res))
        != 0
      || res == nullptr) {
    CAF_LOG_ERROR(
      "failed to resolve address: "
      << CAF_ARG(host) << ":" << CAF_ARG(port) << ":"
      << ((err != 0) ? gai_strerror(err) : "getaddrinfo returned nullptr"));
    return -1;
  }
  memcpy(sa, res->ai_addr, res->ai_addrlen);
  *salen = res->ai_addrlen;
  freeaddrinfo(res);
  return 0;
}

int normalize_txt(uint8_t* p, size_t len) {
  uint8_t *const end = p + len, *dst = p;
  if (p == end)
    return 0;
  do {
    size_t block_len = *p++;
    if (end - p < block_len)
      return 0;
    memmove(dst, p, block_len);
    dst += block_len;
    p += block_len;
  } while (p != end);
  *dst = '\0';
  return 1;
}

ptls_iovec_t resolve_esni_keys(const std::string& server_name) {
  char esni_name[256], *base64;
  uint8_t answer[1024];
  ns_msg msg;
  ns_rr rr;
  ptls_buffer_t decode_buf;
  ptls_base64_decode_state_t ds;
  int answer_len;
  ptls_buffer_init(&decode_buf, const_cast<char*>(""), 0);
  if (snprintf(esni_name, sizeof(esni_name), "_esni.%s", server_name.c_str())
      > sizeof(esni_name) - 1)
    goto Error;
  if ((answer_len = res_query(esni_name, ns_c_in, ns_t_txt, answer,
                              sizeof(answer)))
      <= 0)
    goto Error;
  if (ns_initparse(answer, answer_len, &msg) != 0)
    goto Error;
  if (ns_msg_count(msg, ns_s_an) < 1)
    goto Error;
  if (ns_parserr(&msg, ns_s_an, 0, &rr) != 0)
    goto Error;
  base64 = reinterpret_cast<char*>(const_cast<unsigned char*>(ns_rr_rdata(rr)));
  if (!normalize_txt(reinterpret_cast<uint8_t*>(base64), ns_rr_rdlen(rr)))
    goto Error;

  ptls_base64_decode_init(&ds);
  if (ptls_base64_decode(base64, &ds, &decode_buf) != 0)
    goto Error;
  assert(decode_buf.is_allocated);

  return ptls_iovec_init(decode_buf.base, decode_buf.off);
Error:
  ptls_buffer_dispose(&decode_buf);
  return ptls_iovec_init(nullptr, 0);
}

} // namespace detail
} // namespace caf
