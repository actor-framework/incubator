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

#include "caf/net/quic/quic.hpp"

namespace caf {
namespace net {
namespace quic {

error make_server_context(detail::quicly_state& state, callbacks callbacks) {
  // initialize tls_context
  state.tlsctx.random_bytes = ptls_openssl_random_bytes;
  state.tlsctx.get_time = &ptls_get_time;
  state.tlsctx.key_exchanges = state.key_exchanges;
  state.tlsctx.cipher_suites = ptls_openssl_cipher_suites;
  state.tlsctx.require_dhe_on_psk = 1;
  state.tlsctx.save_ticket = callbacks.save_ticket;
  // initialize quicly_context
  state.ctx = quicly_spec_context;
  state.ctx.tls = &state.tlsctx;
  state.ctx.stream_open = callbacks.stream_open;
  state.ctx.closed_by_peer = callbacks.closed_by_peer;
  state.ctx.save_resumption_token = callbacks.save_resumption_token;
  state.ctx.generate_resumption_token = callbacks.generate_resumption_token;
  // initialize session_context
  detail::setup_session_cache(state.ctx.tls);
  quicly_amend_ptls_context(state.ctx.tls);
  // generate cypher context for en-/decryption
  uint8_t secret[PTLS_MAX_DIGEST_SIZE];
  state.ctx.tls->random_bytes(secret, ptls_openssl_sha256.digest_size);
  state.address_token_aead.enc = ptls_aead_new(&ptls_openssl_aes128gcm,
                                               &ptls_openssl_sha256, 1, secret,
                                               "");
  state.address_token_aead.dec = ptls_aead_new(&ptls_openssl_aes128gcm,
                                               &ptls_openssl_sha256, 0, secret,
                                               "");
  // read certificates from file.
  // TODO: This should come from the caf config.
  std::string path_to_certs;
  if (auto path = getenv("QUICLY_CERTS")) {
    path_to_certs = path;
  } else {
    // try to load default certs
    path_to_certs = "/home/jakob/code/quicly/t/assets/";
  }
  auto certificate_chain_path = (path_to_certs + std::string("server.crt"));
  auto private_key_path = (path_to_certs + std::string("server.key"));
  if (detail::load_certificate_chain(state.ctx.tls, certificate_chain_path))
    return make_error(sec::runtime_error, "failed to load certificate chain: "
                                            + certificate_chain_path);
  if (detail::load_private_key(state.ctx.tls, private_key_path))
    return make_error(sec::runtime_error,
                      "failed to load private keys: " + private_key_path);
  CAF_ASSERT(state.ctx.tls->certificates.count != 0
             || state.ctx.tls->sign_certificate != nullptr);
  state.key_exchanges[0] = &ptls_openssl_secp256r1;
  state.tlsctx.random_bytes(state.cid_key, sizeof(state.cid_key) - 1);
  auto iovec = ptls_iovec_init(state.cid_key, sizeof(state.cid_key) - 1);
  auto cid_cipher = &ptls_openssl_bfecb;
  auto reset_token_cipher = &ptls_openssl_aes128ecb;
  auto hash = &ptls_openssl_sha256;
  state.ctx.cid_encryptor = quicly_new_default_cid_encryptor(cid_cipher,
                                                             reset_token_cipher,
                                                             hash, iovec);
  return none;
}

error make_client_context(detail::quicly_state& state, callbacks callbacks) {
  // initialize tls_context
  state.tlsctx.random_bytes = ptls_openssl_random_bytes;
  state.tlsctx.get_time = &ptls_get_time;
  state.tlsctx.key_exchanges = state.key_exchanges;
  state.tlsctx.cipher_suites = ptls_openssl_cipher_suites;
  state.tlsctx.require_dhe_on_psk = 1;
  state.tlsctx.save_ticket = callbacks.save_ticket;
  // initialize quicly_context
  state.ctx = quicly_spec_context;
  state.ctx.tls = &state.tlsctx;
  state.ctx.stream_open = callbacks.stream_open;
  state.ctx.closed_by_peer = callbacks.closed_by_peer;
  state.ctx.save_resumption_token = callbacks.save_resumption_token;
  state.ctx.generate_resumption_token = callbacks.generate_resumption_token;
  // initialize session_context
  detail::setup_session_cache(state.ctx.tls);
  quicly_amend_ptls_context(state.ctx.tls);
  // generate cypher context for en-/decryption
  {
    uint8_t secret[PTLS_MAX_DIGEST_SIZE];
    state.ctx.tls->random_bytes(secret, ptls_openssl_sha256.digest_size);
    state.address_token_aead.enc = ptls_aead_new(&ptls_openssl_aes128gcm,
                                                 &ptls_openssl_sha256, 1,
                                                 secret, "");
    state.address_token_aead.dec = ptls_aead_new(&ptls_openssl_aes128gcm,
                                                 &ptls_openssl_sha256, 0,
                                                 secret, "");
  }
  state.hs_properties.client.negotiated_protocols.count = 0;
  detail::load_session(&state.resumed_transport_params, state.resumption_token,
                       state.hs_properties, state.session_file_path);
  return none;
}

} // namespace quic
} // namespace net
} // namespace caf
