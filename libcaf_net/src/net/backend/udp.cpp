/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2020 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/net/backend/udp.hpp"

#include "caf/net/actor_proxy_impl.hpp"
#include "caf/net/basp/application.hpp"
#include "caf/net/basp/application_factory.hpp"
#include "caf/net/datagram_transport.hpp"
#include "caf/net/make_endpoint_manager.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/socket_guard.hpp"
#include "caf/net/stream_transport.hpp"
#include "caf/net/udp_datagram_socket.hpp"
#include "caf/send.hpp"

namespace caf::net::backend {

udp::udp(middleman& mm)
  : middleman_backend("udp"), mm_(mm), proxies_(mm.system(), *this) {
  // nop
}

udp::~udp() {
  // nop
}

error udp::init() {
  auto conf_port = get_or<uint16_t>(mm_.system().config(), "middleman.udp-port",
                                    defaults::middleman::udp_port);
  ip_address addr;
  if (auto err = parse("0.0.0.0", addr))
    return err;
  auto ep = ip_endpoint(addr, conf_port);
  auto sock = make_udp_datagram_socket(ep, true);
  if (!sock)
    return sock.error();
  auto guard = make_socket_guard(sock->first);
  CAF_LOG_INFO("udp socket spawned on " << CAF_ARG2("port", sock->second));
  emplace(sock->first, sock->second);
  return none;
}

void udp::stop() {
  for (const auto& id : node_ids_)
    proxies_.erase(id);
  ep_manager_.reset();
}

expected<endpoint_manager_ptr> udp::get_or_connect(const uri&) {
  return make_error(sec::runtime_error, "connect called on udp backend");
}

endpoint_manager_ptr udp::peer(const node_id&) {
  return ep_manager_;
}

void udp::resolve(const uri& locator, const actor& listener) {
  ep_manager_->resolve(locator, listener);
}

strong_actor_ptr udp::make_proxy(node_id nid, actor_id aid) {
  using impl_type = actor_proxy_impl;
  using hdl_type = strong_actor_ptr;
  actor_config cfg;
  return make_actor<impl_type, hdl_type>(aid, nid, &mm_.system(), cfg,
                                         peer(nid));
}

void udp::set_last_hop(node_id*) {
  // nop
}

expected<endpoint_manager_ptr> udp::emplace(udp_datagram_socket sock,
                                            uint16_t port) {
  auto guard = make_socket_guard(sock);
  listening_port_ = port;
  if (auto err = nonblocking(guard.socket(), true))
    return err;
  auto& mpx = mm_.mpx();
  const std::lock_guard<std::mutex> lock(lock_);
  ep_manager_ = make_endpoint_manager(
    mpx, mm_.system(),
    datagram_transport{guard.release(),
                       basp::application_factory{proxies_}});
  if (auto err = ep_manager_->init()) {
    CAF_LOG_ERROR("mgr->init() failed: " << err);
    return err;
  }
  return ep_manager_;
}

} // namespace caf::net::backend
