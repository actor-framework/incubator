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

#include <unordered_map>

#include "caf/byte_span.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/logger.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/ip.hpp"
#include "caf/net/transport_worker.hpp"
#include "caf/node_id.hpp"
#include "caf/sec.hpp"
#include "caf/send.hpp"
#include "caf/settings.hpp"
#include "caf/tag/datagram_oriented.hpp"

namespace caf::net {

/// Implements a dispatcher that dispatches between transport and workers.
template <class Factory, class IdType>
class transport_worker_dispatcher {
public:
  // -- member types -----------------------------------------------------------

  using input_tag = tag::datagram_oriented;

  using output_tag = tag::datagram_oriented;

  using id_type = IdType;

  using factory_type = Factory;

  using application_type = typename factory_type::application_type;

  using worker_type = transport_worker<application_type, id_type>;

  using worker_ptr = transport_worker_ptr<application_type, id_type>;

  // -- constructors, destructors, and assignment operators --------------------

  template <class... Ts>
  transport_worker_dispatcher(std::string protocol_tag, Ts&&... xs)
    : protocol_tag_(std::move(protocol_tag)),
      factory_(std::forward<Ts>(xs)...) {
    // nop
  }

  ~transport_worker_dispatcher() = default;

  // -- initialization ---------------------------------------------------------

  template <class LowerLayerPtr>
  error init(socket_manager* owner, LowerLayerPtr, const settings& config) {
    CAF_ASSERT(workers_by_id_.empty());
    owner_ = owner;
    config_ = config;
    return factory_.init(config);
  }

  // -- properties -------------------------------------------------------------

  auto& upper_layer(node_id nid) {
    return find_worker(nid)->upper_layer();
  }

  basp::application* top_layer(node_id nid) {
    auto worker = find_worker(nid);
    if (worker)
      return worker->top_layer();
    return nullptr;
  }

  // TODO: Leaving this return type only to get the benchmarks running.
  template <class LowerLayerPtr>
  basp::application* top_layer(LowerLayerPtr down, const uri& locator) {
    auto auth = locator.authority_only();
    if (!auth)
      return nullptr;
    auto nid = make_node_id(*auth);
    auto worker = find_worker(nid);
    if (worker)
      return worker->top_layer();
    if (auto res = emplace(down, locator))
      return (*res)->top_layer();
    return nullptr;
  }

  // -- member functions -------------------------------------------------------

  template <class LowerLayerPtr>
  ptrdiff_t consume(LowerLayerPtr down, const_byte_span bytes, id_type id) {
    if (auto worker = find_worker(id))
      return worker->consume(down, bytes);
    CAF_LOG_TRACE("no worker present for " << CAF_ARG(id));
    if (auto locator = make_uri(protocol_tag_ + "://" + to_string(id))) {
      if (auto worker = add_new_worker(down, make_node_id(*locator), id))
        return (*worker)->consume(down, bytes);
      else
        CAF_LOG_ERROR("could not add new worker " << CAF_ARG(worker.error()));
    } else {
      CAF_LOG_ERROR("could not make uri " << CAF_ARG(locator.error()));
    }
    return -1;
  }

  // -- role: upper layer ------------------------------------------------------

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr down) {
    for (auto& p : workers_by_node_) {
      if (!p.second->prepare_send(down))
        return false;
    }
    return true;
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr down) {
    for (auto& p : workers_by_node_) {
      if (!p.second->done_sending(down))
        return false;
    }
    return true;
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr down, const error& reason) {
    for (auto& p : workers_by_node_)
      p.second->abort(down, reason);
  }

  template <class LowerLayerPtr>
  expected<worker_ptr> emplace(LowerLayerPtr down, const uri& locator) {
    auto& auth = locator.authority();
    ip_address addr;
    if (auto hostname = get_if<std::string>(&auth.host)) {
      auto addrs = ip::resolve(*hostname);
      if (addrs.empty())
        return sec::remote_lookup_failed;
      addr = addrs.at(0);
    } else {
      addr = *get_if<ip_address>(&auth.host);
    }
    return add_new_worker(down, make_node_id(*locator.authority_only()),
                          ip_endpoint{addr, auth.port});
  }

  template <class LowerLayerPtr>
  expected<worker_ptr>
  add_new_worker(LowerLayerPtr down, node_id node, id_type id) {
    CAF_LOG_TRACE(CAF_ARG(node) << CAF_ARG(id));
    auto worker = factory_.make(id);
    workers_by_id_.emplace(std::move(id), worker);
    workers_by_node_.emplace(std::move(node), worker);
    if (auto err = worker->init(owner_, down, config_))
      return err;
    return worker;
  }

private:
  // -- worker lookups ---------------------------------------------------------

  worker_ptr find_worker(const node_id& nid) {
    return find_worker_impl(workers_by_node_, nid);
  }

  worker_ptr find_worker(const id_type& id) {
    return find_worker_impl(workers_by_id_, id);
  }

  template <class Key>
  worker_ptr find_worker_impl(const std::unordered_map<Key, worker_ptr>& map,
                              const Key& key) {
    if (map.count(key) == 0)
      return nullptr;
    return map.at(key);
  }

  std::unordered_map<id_type, worker_ptr> workers_by_id_;

  std::unordered_map<node_id, worker_ptr> workers_by_node_;

  std::unordered_map<uint64_t, worker_ptr> workers_by_timeout_id_;

  socket_manager* owner_ = nullptr;

  settings config_;

  std::string protocol_tag_ = "";

  factory_type factory_;
}; // namespace caf::net

} // namespace caf::net