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

#include "caf/net/backend/test.hpp"

#include "caf/expected.hpp"
#include "caf/net/basp/application.hpp"
#include "caf/net/basp/ec.hpp"
#include "caf/net/length_prefix_framing.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/multiplexer.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/net/stream_transport.hpp"
#include "caf/raise_error.hpp"
#include "caf/sec.hpp"
#include "caf/send.hpp"

namespace caf::net::backend {

test::test(middleman& mm)
  : middleman_backend("test"), mm_(mm), proxies_(mm.system(), *this) {
  // nop
}

test::~test() {
  // nop
}

error test::init() {
  return none;
}

void test::stop() {
  for (const auto& p : peers_)
    proxies_.erase(p.first);
  peers_.clear();
}

socket_manager_ptr test::peer(const node_id& id) {
  return std::get<socket_manager_ptr>(get_peer(id));
}

expected<socket_manager_ptr> test::get_or_connect(const uri& locator) {
  if (auto ptr = peer(make_node_id(*locator.authority_only())))
    return ptr;
  return make_error(
    sec::runtime_error,
    "connecting not implemented in test backend. `emplace` first.");
}

void test::resolve(const uri& locator, const actor& listener) {
  if (auto id = locator.authority_only()) {
    auto basp_ptr = std::get<basp::application*>(get_peer(make_node_id(*id)));
    basp_ptr->resolve(locator.path(), listener);
  } else {
    anon_send(listener, error(basp::ec::invalid_locator));
  }
}

strong_actor_ptr test::make_proxy(node_id nid, actor_id aid) {
  auto basp_ptr = std::get<basp::application*>(get_peer(nid));
  return basp_ptr->make_proxy(nid, aid);
}

void test::set_last_hop(node_id*) {
  // nop
}

uint16_t test::port() const noexcept {
  return 0;
}

test::peer_entry& test::emplace(const node_id& peer_id, stream_socket first,
                                stream_socket second) {
  if (auto err = nonblocking(second, true))
    CAF_LOG_ERROR("nonblocking failed: " << err);
  auto& mpx = mm_.mpx();
  auto mgr = make_socket_manager<basp::application, length_prefix_framing,
                                 stream_transport>(second, &mpx, proxies_);
  settings cfg;
  if (auto err = mgr->init(cfg)) {
    CAF_LOG_ERROR("mgr->init() failed: " << err);
    CAF_RAISE_ERROR("mgr->init() failed");
  }
  auto basp_ptr = std::addressof(mgr->top_layer());
  const std::lock_guard<std::mutex> lock(lock_);
  auto& result = peers_[peer_id];
  result = std::make_tuple(first, std::move(mgr), basp_ptr);
  return result;
}

test::peer_entry& test::get_peer(const node_id& id) {
  const std::lock_guard<std::mutex> lock(lock_);
  auto i = peers_.find(id);
  if (i != peers_.end())
    return i->second;
  CAF_LOG_ERROR(make_error(sec::runtime_error, "peer_entry not found"));
  CAF_RAISE_ERROR("peer_entry not found");
}

} // namespace caf::net::backend
