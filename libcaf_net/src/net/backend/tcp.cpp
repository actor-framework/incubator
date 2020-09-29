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

#include "caf/net/backend/tcp.hpp"

#include <mutex>
#include <string>

#include "caf/net/actor_proxy_impl.hpp"
#include "caf/net/basp/application.hpp"
#include "caf/net/basp/ec.hpp"
#include "caf/net/connection_acceptor.hpp"
#include "caf/net/defaults.hpp"
#include "caf/net/ip.hpp"
#include "caf/net/length_prefix_framing.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/socket_guard.hpp"
#include "caf/net/stream_transport.hpp"
#include "caf/net/tcp_accept_socket.hpp"
#include "caf/net/tcp_stream_socket.hpp"
#include "caf/send.hpp"
#include "caf/uri.hpp"

namespace caf::net::backend {

tcp::tcp(middleman& mm)
  : middleman_backend("tcp"), mm_(mm), proxies_(mm.system(), *this) {
  // nop
}

tcp::~tcp() {
  // nop
}

error tcp::init() {
  uint16_t conf_port = get_or<uint16_t>(mm_.system().config(),
                                        "caf.middleman.tcp-port",
                                        defaults::middleman::tcp_port);
  uri::authority_type auth;
  auth.port = conf_port;
  auth.host = std::string("0.0.0.0");
  auto acceptor = make_tcp_accept_socket(auth, false);
  if (!acceptor)
    return acceptor.error();
  auto acc_guard = make_socket_guard(*acceptor);
  if (auto err = nonblocking(acc_guard.socket(), true))
    return err;
  auto port = local_port(*acceptor);
  if (!port)
    return port.error();
  listening_port_ = *port;
  CAF_LOG_INFO("doorman spawned on " << CAF_ARG(*port));
  auto doorman_uri = make_uri("tcp://doorman");
  if (!doorman_uri)
    return doorman_uri.error();
  auto add_conn = [this](tcp_stream_socket sock,
                         multiplexer* mpx) -> socket_manager_ptr {
    return make_socket_manager<basp::application, length_prefix_framing,
                               stream_transport>(sock, mpx, proxies_);
  };
  auto mgr = mm_.make_acceptor(acc_guard.release(), add_conn);
  return none;
}

void tcp::stop() {
  for (const auto& p : peers_)
    proxies_.erase(p.first);
  peers_.clear();
}

expected<socket_manager_ptr> tcp::get_or_connect(const uri& locator) {
  if (auto auth = locator.authority_only()) {
    auto id = make_node_id(*auth);
    if (auto ptr = peer(id))
      return ptr;
    auto host = locator.authority().host;
    if (auto hostname = get_if<std::string>(&host)) {
      for (const auto& addr : ip::resolve(*hostname)) {
        ip_endpoint ep{addr, locator.authority().port};
        auto sock = make_connected_tcp_stream_socket(ep);
        if (!sock)
          continue;
        else
          return emplace(id, *sock);
      }
    }
  }
  return sec::cannot_connect_to_node;
}

socket_manager_ptr tcp::peer(const node_id& id) {
  return get_peer(id);
}

void tcp::resolve(const uri& locator, const actor& listener) {
  if (auto p = get_or_connect(locator))
    (*p)->resolve(locator.path(), listener);
  else
    anon_send(listener, p.error());
}

strong_actor_ptr tcp::make_proxy(node_id nid, actor_id aid) {
  return peer(nid)->make_proxy(nid, aid);
}

void tcp::set_last_hop(node_id*) {
  // nop
}

uint16_t tcp::port() const noexcept {
  return listening_port_;
}

socket_manager_ptr tcp::get_peer(const node_id& id) {
  const std::lock_guard<std::mutex> lock(lock_);
  auto i = peers_.find(id);
  if (i != peers_.end())
    return i->second;
  return nullptr;
}

} // namespace caf::net::backend
