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

#include "caf/ip_endpoint.hpp"
#include "caf/logger.hpp"
#include "caf/net/endpoint_manager_queue.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/ip.hpp"
#include "caf/net/packet_writer_decorator.hpp"
#include "caf/net/transport_worker.hpp"
#include "caf/node_id.hpp"
#include "caf/sec.hpp"
#include "caf/send.hpp"

namespace caf::net {

/// Implements a dispatcher that dispatches between transport and workers.
template <class Factory, class IdType>
class transport_worker_dispatcher {
public:
  // -- member types -----------------------------------------------------------

  using id_type = IdType;

  using factory_type = Factory;

  using application_type = typename factory_type::application_type;

  using worker_type = transport_worker<application_type, id_type>;

  using worker_ptr = transport_worker_ptr<application_type, id_type>;

  // -- constructors, destructors, and assignment operators --------------------

  explicit transport_worker_dispatcher(factory_type factory)
    : factory_(std::move(factory)) {
    // nop
  }

  // -- member functions -------------------------------------------------------

  template <class Parent>
  error init(Parent&) {
    CAF_ASSERT(workers_by_id_.empty());
    return none;
  }

  template <class Parent>
  error handle_data(Parent& parent, span<const byte> data, id_type id) {
    if (auto worker = find_worker(id))
      return worker->handle_data(parent, data);
    auto locator = make_uri("udp://" + to_string(id));
    if (!locator)
      return locator.error();
    if (auto worker = add_new_worker(parent, make_node_id(*locator), id))
      return (*worker)->handle_data(parent, data);
    else
      return std::move(worker.error());
  }

  template <class Parent>
  void write_message(Parent& parent,
                     std::unique_ptr<endpoint_manager_queue::message> msg) {
    auto receiver = msg->receiver;
    if (!receiver) {
      CAF_LOG_ERROR("no receiver was specified");
      return;
    }
    auto nid = receiver->node();
    auto worker = find_worker(nid);
    if (!worker)
      CAF_LOG_ERROR("could not find worker for endpoint");
    else
      worker->write_message(parent, std::move(msg));
  }

  template <class Parent>
  void resolve(Parent& parent, const uri& locator, const actor& listener) {
    if (auto auth = locator.authority_only()) {
      if (auto worker = find_worker(make_node_id(*auth))) {
        worker->resolve(parent, locator.path(), listener);
      } else {
        if (auto ret = emplace(parent, locator))
          (*ret)->resolve(parent, locator.path(), listener);
        else
          anon_send(listener, ret.error());
      }
    } else {
      anon_send(listener,
                make_error(sec::runtime_error, "could not get authority"));
    }
  }

  template <class Parent>
  void new_proxy(Parent& parent, const node_id& nid, actor_id id) {
    if (auto worker = find_worker(nid))
      worker->new_proxy(parent, nid, id);
  }

  template <class Parent>
  void local_actor_down(Parent& parent, const node_id& nid, actor_id id,
                        error reason) {
    if (auto worker = find_worker(nid))
      worker->local_actor_down(parent, nid, id, std::move(reason));
  }

  template <class... Ts>
  void set_timeout(uint64_t timeout_id, id_type id, Ts&&...) {
    workers_by_timeout_id_.emplace(timeout_id, workers_by_id_.at(id));
  }

  template <class Parent>
  void timeout(Parent& parent, std::string tag, uint64_t id) {
    if (auto worker = workers_by_timeout_id_.at(id)) {
      worker->timeout(parent, std::move(tag), id);
      workers_by_timeout_id_.erase(id);
    }
  }

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
    if (auto err = worker->init(parent))
      return err;
    workers_by_id_.emplace(std::move(id), worker);
    workers_by_node_.emplace(std::move(node), worker);
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

  factory_type factory_;
};

} // namespace caf::net
