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

#pragma once

#include <map>
#include <mutex>

#include "caf/detail/net_export.hpp"
#include "caf/error.hpp"
#include "caf/expected.hpp"
#include "caf/net/basp/application.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/make_endpoint_manager.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/middleman_backend.hpp"
#include "caf/net/multiplexer.hpp"
#include "caf/net/udp_datagram_socket.hpp"
#include "caf/node_id.hpp"

namespace caf::net::backend {

/// Minimal backend for udp communication.
class CAF_NET_EXPORT udp : public middleman_backend {
public:
  // -- constructors, destructors, and assignment operators --------------------

  udp(middleman& mm);

  ~udp() override;

  // -- interface functions ----------------------------------------------------

  error init() override;

  void stop() override;

  expected<endpoint_manager_ptr> get_or_connect(const uri& locator) override;

  endpoint_manager_ptr peer(const node_id&) override;

  void resolve(const uri& locator, const actor& listener) override;

  strong_actor_ptr make_proxy(node_id nid, actor_id aid) override;

  void set_last_hop(node_id*) override;

  // -- properties -------------------------------------------------------------

  uint16_t port() const noexcept override {
    return listening_port_;
  }

  expected<endpoint_manager_ptr> emplace(udp_datagram_socket sock,
                                         uint16_t port);

private:
  middleman& mm_;

  endpoint_manager_ptr ep_manager_;

  std::vector<node_id> node_ids_;

  proxy_registry proxies_;

  uint16_t listening_port_;

  std::mutex lock_;
};

} // namespace caf::net::backend
