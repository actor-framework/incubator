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

#include <arpa/nameser.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/pem.h>
#include <resolv.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "caf/detail/comparable.hpp"
#include "caf/detail/fnv_hash.hpp"
#include "caf/meta/type_name.hpp"
#include "picotls/openssl.h"
#include "picotls/pembase64.h"

namespace caf {
namespace detail {

size_t convert(quicly_conn_t* ptr) {
  return reinterpret_cast<size_t>(ptr);
}

size_t convert(quicly_conn_ptr ptr) {
  return convert(ptr.get());
}

quicly_conn_ptr make_quicly_conn_ptr(quicly_conn_t* conn) {
  return std::shared_ptr<quicly_conn_t>(conn, quicly_free);
}

void load_certificate_chain(ptls_context_t* ctx, const char* fn) {
  if (ptls_load_certificates(ctx, (char*) fn) != 0) {
    fprintf(stderr, "failed to load certificate:%s:%s\n", fn, strerror(errno));
    exit(1);
  }
}

void load_private_key(ptls_context_t* ctx, const char* fn) {
  static ptls_openssl_sign_certificate_t sc;
  FILE* fp;
  EVP_PKEY* pkey;
  if ((fp = fopen(fn, "rb")) == nullptr) {
    fprintf(stderr, "failed to open file:%s:%s\n", fn, strerror(errno));
    exit(1);
  }
  pkey = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
  fclose(fp);
  if (pkey == nullptr) {
    fprintf(stderr, "failed to read private key from file:%s\n", fn);
    exit(1);
  }
  ptls_openssl_init_sign_certificate(&sc, pkey);
  EVP_PKEY_free(pkey);
  ctx->sign_certificate = &sc.super;
}

int util_save_ticket_cb(ptls_save_ticket_t* _self, ptls_t*, ptls_iovec_t src) {
  auto self = reinterpret_cast<st_util_save_ticket_t*>(_self);
  FILE* fp;
  if ((fp = fopen(self->fn, "wb")) == nullptr) {
    fprintf(stderr, "failed to open file:%s:%s\n", self->fn, strerror(errno));
    return PTLS_ERROR_LIBRARY;
  }
  fwrite(src.base, 1, src.len, fp);
  fclose(fp);

  return 0;
}

/*void setup_session_file(ptls_context_t* ctx,
                        ptls_handshake_properties_t* hsprop, const char* fn) {
  static struct st_util_save_ticket_t st;
  FILE* fp;

  // setup save_ticket callback
  strcpy(st.fn, fn);
  st.super.cb = util_save_ticket_cb;
  ctx->save_ticket = &st.super;

  // load session ticket if possible
  if ((fp = fopen(fn, "rb")) != nullptr) {
    static uint8_t ticket[16384];
    size_t ticket_size = fread(ticket, 1, sizeof(ticket), fp);
    if (ticket_size == 0 || !feof(fp)) {
      fprintf(stderr, "failed to load ticket from file:%s\n", fn);
      exit(1);
    }
    fclose(fp);
    hsprop->client.session_ticket = ptls_iovec_init(ticket, ticket_size);
  }
}

void setup_verify_certificate(ptls_context_t* ctx) {
  static ptls_openssl_verify_certificate_t vc;
  ptls_openssl_init_verify_certificate(&vc, nullptr);
  ctx->verify_certificate = &vc.super;
}

void setup_esni(ptls_context_t* ctx, const char* esni_fn,
                ptls_key_exchange_context_t** key_exchanges) {
  uint8_t esnikeys[65536];
  size_t esnikeys_len;
  int ret = 0;

  { // read esnikeys
    FILE* fp;
    if ((fp = fopen(esni_fn, "rb")) == nullptr) {
      fprintf(stderr, "failed to open file:%s:%s\n", esni_fn, strerror(errno));
      exit(1);
    }
    esnikeys_len = fread(esnikeys, 1, sizeof(esnikeys), fp);
    if (esnikeys_len == 0 || !feof(fp)) {
      fprintf(stderr, "failed to load ESNI data from file:%s\n", esni_fn);
      exit(1);
    }
    fclose(fp);
  }

  if ((ctx->esni = reinterpret_cast<ptls_esni_context_t**>(
         malloc(sizeof(*ctx->esni) * 2)))
        == nullptr
      || (*ctx->esni = reinterpret_cast<ptls_esni_context_t*>(
            malloc(sizeof(**ctx->esni))))
           == nullptr) {
    fprintf(stderr, "no memory\n");
    exit(1);
  }

  if ((ret = ptls_esni_init_context(ctx, ctx->esni[0],
                                    ptls_iovec_init(esnikeys, esnikeys_len),
                                    key_exchanges))
      != 0) {
    fprintf(stderr, "failed to parse ESNI data of file:%s:error=%d\n", esni_fn,
            ret);
    exit(1);
  }
}*/

int encrypt_ticket_cb(ptls_encrypt_ticket_t* _self, ptls_t* tls, int is_encrypt,
                      ptls_buffer_t* dst, ptls_iovec_t src) {
  auto self = reinterpret_cast<st_util_session_cache_t*>(_self);
  int ret;

  if (is_encrypt) {
    /* replace the cached entry along with a newly generated session id */
    free(self->data.base);
    if ((self->data.base = reinterpret_cast<uint8_t*>(malloc(src.len)))
        == nullptr)
      return PTLS_ERROR_NO_MEMORY;

    ptls_get_context(tls)->random_bytes(self->id, sizeof(self->id));
    memcpy(self->data.base, src.base, src.len);
    self->data.len = src.len;

    /* store the session id in buffer */
    if ((ret = ptls_buffer_reserve(dst, sizeof(self->id))) != 0)
      return ret;
    memcpy(dst->base + dst->off, self->id, sizeof(self->id));
    dst->off += sizeof(self->id);

  } else {
    /* check if session id is the one stored in cache */
    if (src.len != sizeof(self->id))
      return PTLS_ERROR_SESSION_NOT_FOUND;
    if (memcmp(self->id, src.base, sizeof(self->id)) != 0)
      return PTLS_ERROR_SESSION_NOT_FOUND;

    /* return the cached value */
    if ((ret = ptls_buffer_reserve(dst, self->data.len)) != 0)
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

/*ptls_iovec_t resolve_esni_keys(const char* server_name) {
  char esni_name[256], *base64;
  uint8_t answer[1024];
  ns_msg msg;
  ns_rr rr;
  ptls_buffer_t decode_buf;
  ptls_base64_decode_state_t ds;
  int answer_len;
  char none[] = "";

  ptls_buffer_init(&decode_buf, reinterpret_cast<void*>(none), 0);

  if (snprintf(esni_name, sizeof(esni_name), "_esni.%s", server_name)
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
}*/

} // namespace detail
} // namespace caf
