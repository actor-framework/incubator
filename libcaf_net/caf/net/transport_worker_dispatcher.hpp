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

namespace caf::net {

/// Implements a dispatcher that dispatches between transport and workers.
template <class Application, class Factory, class IdType>
class transport_worker_dispatcher {
public:
  // -- member types -----------------------------------------------------------

  using id_type = IdType;

  using factory_type = Factory;

  using worker_type = transport_worker<Application, id_type>;

  using worker_ptr = transport_worker_ptr<Application, id_type>;

  // -- constructors, destructors, and assignment operators --------------------

  explicit transport_worker_dispatcher(std::string protocol_tag,
                                       factory_type factory)
    : factory_(std::move(factory)), protocol_tag_(std::move(protocol_tag)) {
    // nop
  }

  ~transport_worker_dispatcher() = default;

  // -- initialization ---------------------------------------------------------

  template <class LowerLayerPtr>
  error init(socket_manager* owner, LowerLayerPtr, const settings& config) {
    CAF_ASSERT(workers_by_id_.empty());
    owner_ = owner;
    config_ = config;
    return none;
  }

  // -- properties -------------------------------------------------------------

  auto& upper_layer(id_type id) noexcept {
    if (auto worker = find_worker(id))
      return worker;
    CAF_RAISE_ERROR("Worker not found");
  }

  const auto& upper_layer() const noexcept {
    if (auto worker = find_worker(id))
      return worker;
    CAF_RAISE_ERROR("Worker not found");
  }

  // -- member functions -------------------------------------------------------

  template <class LowerLayerPtr>
  error consume(LowerLayerPtr down, const_byte_span data, id_type id) {
    if (auto worker = find_worker(id))
      return worker->consume(down, data, delta);
    auto locator = make_uri(protocol_tag + "://" + to_string(id));
    if (!locator)
      return locator.error();
    if (auto worker = add_new_worker(down, make_node_id(*locator), id))
      return (*worker)->->consume(down, data, delta);
    else
      return std::move(worker.error());
  }

  // -- role: upper layer ------------------------------------------------------

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr down) {
    for (auto& p : workers_by_node_) {
      if (!p->second.prepare_send(down))
        return false;
    }
    return true;
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr& down) {
    for (auto& p : workers_by_node_) {
      if (!p->second.done_sending(down))
        return false;
    }
    return true;
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr& down, const error& reason) {
    for (auto& p : workers_by_node_)
      p->second.abort(down, reason);
  }

  // TODO: This is needed.
  // template <class Parent>
  // void shutdown(Parent& parent) {
  //   for (auto& p : )
  //     p.second->shutdown(parent);
  // }

  void handle_error(sec error) {
    for (const auto& p : workers_by_id_)
      p.second->handle_error(error);
  }

  template <class Parent>
  expected<worker_ptr> emplace(Parent& parent, const uri& locator) {
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
    return add_new_worker(parent, make_node_id(*locator.authority_only()),
                          ip_endpoint{addr, auth.port});
  }

  template <class Parent>
  expected<worker_ptr>
  add_new_worker(Parent& parent, node_id node, id_type id) {
    CAF_LOG_TRACE(CAF_ARG(node) << CAF_ARG(id));
    auto application = factory_.make();
    auto worker = std::make_shared<worker_type>(std::move(application), id);
    workers_by_id_.emplace(std::move(id), worker);
    workers_by_node_.emplace(std::move(node), worker);
    if (auto err = worker->init(parent))
      return err;
    return worker;
  }

private:
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

  // -- worker lookups ---------------------------------------------------------

  std::unordered_map<id_type, worker_ptr> workers_by_id_;

  std::unordered_map<node_id, worker_ptr> workers_by_node_;

  std::unordered_map<uint64_t, worker_ptr> workers_by_timeout_id_;

  socket_manager* owner_;

  const settings config_;

  factory_type factory_;

  std::string protocol_tag = "";
}; // namespace caf::net

} // namespace caf::net