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

#include <map>
#include <mutex>
#include <utility>

#include "caf/detail/net_export.hpp"
#include "caf/error.hpp"
#include "caf/expected.hpp"
#include "caf/logger.hpp"
#include "caf/net/basp/application.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/length_prefix_framing.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/middleman_backend.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/net/stream_transport.hpp"
#include "caf/node_id.hpp"

namespace caf::net::backend {

/// Minimal backend for tcp communication.
class CAF_NET_EXPORT tcp : public middleman_backend {
public:
  using peer_entry = std::pair<socket_manager_ptr, basp::application*>;

  using peer_map = std::map<node_id, peer_entry>;

  using emplace_return_type = std::pair<peer_map::iterator, bool>;

  // -- constructors, destructors, and assignment operators --------------------

  tcp(middleman& mm);

  ~tcp() override;

  // -- interface functions ----------------------------------------------------

  error init() override;

  void stop() override;

  expected<socket_manager_ptr> get_or_connect(const uri& locator) override;

  socket_manager_ptr peer(const node_id& id) override;

  void resolve(const uri& locator, const actor& listener) override;

  strong_actor_ptr make_proxy(node_id nid, actor_id aid) override;

  void set_last_hop(node_id*) override;

  // -- properties -------------------------------------------------------------

  uint16_t port() const noexcept override;

  template <class Handle>
  expected<peer_entry> emplace(const node_id& peer_id, Handle sock) {
    CAF_LOG_TRACE(CAF_ARG(peer_id) << CAF_ARG2("handle", sock.id));
    auto mpx = &mm_.mpx();
    basp::application app{proxies_};
    auto mgr = make_socket_manager<basp::application, length_prefix_framing,
                                   stream_transport>(std::move(sock), mpx,
                                                     proxies_);
    if (auto err = mgr->init(content(mpx->system().config()))) {
      CAF_LOG_ERROR("mgr->init() failed: " << err);
      return err;
    }
    auto basp_ptr = std::addressof(mgr->top_layer());
    emplace_return_type res;
    {
      const std::lock_guard<std::mutex> lock(lock_);
      res = peers_.emplace(peer_id, std::make_pair(std::move(mgr), basp_ptr));
    }
    if (res.second)
      return res.first->second;
    else
      return make_error(sec::runtime_error, "peer_id already exists");
  }

private:
  expected<peer_entry> connect(const uri& nid);

  peer_entry* get_peer(const node_id& nid);

  middleman& mm_;

  peer_map peers_;

  proxy_registry proxies_;

  uint16_t listening_port_;

  std::mutex lock_;
};

} // namespace caf::net::backend
