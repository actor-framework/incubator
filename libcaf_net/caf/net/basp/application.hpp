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

#include <cstdint>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "caf/actor.hpp"
#include "caf/actor_addr.hpp"
#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/byte.hpp"
#include "caf/byte_span.hpp"
#include "caf/callback.hpp"
#include "caf/defaults.hpp"
#include "caf/detail/net_export.hpp"
#include "caf/detail/worker_hub.hpp"
#include "caf/error.hpp"
#include "caf/fwd.hpp"
#include "caf/mailbox_element.hpp"
#include "caf/net/actor_shell.hpp"
#include "caf/net/basp/constants.hpp"
#include "caf/net/basp/ec.hpp"
#include "caf/net/basp/header.hpp"
#include "caf/net/basp/message_type.hpp"
#include "caf/net/basp/msg.hpp"
#include "caf/net/basp/worker.hpp"
#include "caf/net/receive_policy.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/node_id.hpp"
#include "caf/proxy_registry.hpp"
#include "caf/response_promise.hpp"
#include "caf/scoped_execution_unit.hpp"
#include "caf/send.hpp"
#include "caf/tag/message_oriented.hpp"
#include "caf/unit.hpp"

namespace caf::net::basp {

/// An implementation of BASP as an application layer protocol.
class CAF_NET_EXPORT application {
public:
  // -- member types -----------------------------------------------------------

  using input_tag = tag::message_oriented;

  using byte_span = span<const byte>;

  using hub_type = detail::worker_hub<worker>;

  // -- constructors, destructors, and assignment operators --------------------

  explicit application(proxy_registry& proxies);

  // -- static utility functions -----------------------------------------------

  static auto default_app_ids() {
    return std::vector<std::string>{
      to_string(defaults::middleman::app_identifier)};
  }

  // -- interface functions ----------------------------------------------------

  template <class LowerLayerPtr>
  error init(socket_manager* owner, LowerLayerPtr down, const settings& cfg) {
    // Initialize member variables.
    self_ = owner->make_actor_shell(down);
    system_ = &owner->system();
    executor_.system_ptr(system_);
    executor_.proxy_registry_ptr(&proxies_);
    max_throughput_ = get_or(cfg, "caf.scheduler.max-throughput",
                             defaults::scheduler::max_throughput);
    auto workers = get_or<size_t>(
      cfg, "caf.middleman.workers",
      std::min(3u, std::thread::hardware_concurrency() / 4u) + 1);
    for (size_t i = 0; i < workers; ++i)
      hub_.add_new_worker(queue_, proxies_);
    // Install handlers for BASP-internal messages.
    self_->set_behavior(
      [this, down](resolve_request_msg& x) {
        write_resolve_request(down, x.path, x.listener);
      },
      [this, down](new_proxy_msg& x) { new_proxy(down, x.id); },
      [this, down](local_actor_down_msg& x) {
        local_actor_down(down, x.id, std::move(x.reason));
      },
      [this, down](timeout_msg& x) { timeout(down, std::move(x.type), x.id); });
    // Everything else is an outgoing message.
    self_->set_fallback([this, down](message&) -> result<message> {
      handle_message(down, *self_->current_mailbox_element());
      return {};
    });
    // Write handshake.
    auto app_ids = get_or(cfg, "caf.middleman.app-identifiers",
                          application::default_app_ids());
    if (!write_message(down, header{message_type::handshake, version},
                       system().node(), app_ids))
      return down->abort_reason();
    return none;
  }

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr& down) {
    CAF_LOG_TRACE("");
    if (!handshake_complete())
      return true;
    while (down->can_send_more() && self_->consume_message()) {
      // We set abort_reason in our response handlers in case of an error.
      if (down->abort_reason())
        return false;
      // else: repeat.
    }
    return true;
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume(LowerLayerPtr& down, byte_span buffer) {
    CAF_LOG_TRACE(CAF_ARG2("buffer.size", buffer.size()));
    return handle(down, buffer) ? static_cast<ptrdiff_t>(buffer.size()) : -1;
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr&) {
    return self_->try_block_mailbox();
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr&, const error&) {
    CAF_LOG_TRACE("");
    // nop
  }

  void resolve(string_view path, const actor& listener);

  strong_actor_ptr make_proxy(const node_id& nid, const actor_id& aid);

  // -- utility functions ------------------------------------------------------

  strong_actor_ptr resolve_local_path(string_view path);

  /// Writes a message to the message buffer of `down`.
  template <class LowerLayerPtr, class... Ts>
  bool write_message(LowerLayerPtr& down, header hdr, Ts&&... xs) {
    CAF_LOG_TRACE(CAF_ARG(hdr));
    down->begin_message();
    auto& buf = down->message_buffer();
    binary_serializer sink{&executor_, buf};
    if (!sink.apply_objects(hdr, xs...)) {
      down->abort_reason(std::move(sink.get_error()));
      return false;
    }
    down->end_message();
    return true;
  }

  // -- properties -------------------------------------------------------------

  bool handshake_complete() const noexcept {
    return handshake_complete_;
  }

  actor_system& system() const noexcept {
    return *system_;
  }

private:
  // -- handling of outgoing messages and events -------------------------------

  template <class LowerLayerPtr>
  void write_resolve_request(LowerLayerPtr& down, const std::string& path,
                             const actor& listener) {
    CAF_LOG_TRACE(CAF_ARG(path) << CAF_ARG(listener));
    auto req_id = next_request_id_++;
    if (auto err = write_message(
          down, header{message_type::resolve_request, req_id}, path)) {
      anon_send(listener, resolve_atom_v, err);
      return;
    }
    pending_resolves_.emplace(req_id, listener);
  }

  template <class LowerLayerPtr>
  void new_proxy(LowerLayerPtr& down, actor_id aid) {
    CAF_LOG_TRACE(CAF_ARG(aid));
    write_message(down, header{message_type::monitor_message,
                               static_cast<uint64_t>(aid)});
  }

  template <class LowerLayerPtr>
  void local_actor_down(LowerLayerPtr& down, actor_id aid, error reason) {
    CAF_LOG_TRACE(CAF_ARG(aid) << CAF_ARG(reason));
    write_message(
      down, header{message_type::down_message, static_cast<uint64_t>(aid)},
      reason);
  }

  template <class LowerLayerPtr>
  void timeout(LowerLayerPtr& down, std::string type, uint64_t id) {
    CAF_LOG_TRACE(CAF_ARG(type) << CAF_ARG(id));
    down->timeout(std::move(type), id);
  }

  template <class LowerLayerPtr>
  void handle_message(LowerLayerPtr& down, mailbox_element& me) {
    CAF_LOG_TRACE(CAF_ARG2("content", me.content()));
    const auto& src = me.sender;
    // The proxy puts the destination at the back.
    auto dst = std::move(me.stages.back());
    me.stages.pop_back();
    if (dst == nullptr) {
      // TODO: valid?
      return;
    }
    node_id nid{};
    actor_id aid{0};
    if (src != nullptr) {
      auto src_id = src->id();
      system().registry().put(src_id, src);
      nid = src->node();
      aid = src_id;
    }
    if (auto err = write_message(
          down, header{message_type::actor_message, me.mid.integer_value()},
          nid, aid, dst->id(), me.stages, me.payload)) {
      // TODO: better error handling
      CAF_LOG_WARNING("failed to write message: " << err);
    }
  }

  // -- handling of incoming messages ------------------------------------------

  template <class LowerLayerPtr>
  bool handle(LowerLayerPtr& down, byte_span bytes) {
    CAF_LOG_TRACE(CAF_ARG2("bytes.size", bytes.size()));
    if (!handshake_complete_) {
      if (bytes.size() < header_size) {
        down->abort_reason(ec::unexpected_number_of_bytes);
        return false;
      }
      auto hdr = header::from_bytes(bytes);
      if (hdr.type != message_type::handshake) {
        down->abort_reason(ec::missing_handshake);
        return false;
      }
      if (hdr.operation_data != version) {
        down->abort_reason(ec::version_mismatch);
        return false;
      }
      handshake_complete_ = true;
      return handle_handshake(down, hdr, bytes.subspan(header_size));
    }
    if (bytes.size() < header_size) {
      down->abort_reason(ec::unexpected_number_of_bytes);
      return false;
    }
    auto hdr = header::from_bytes(bytes);
    return handle(down, hdr, bytes.subspan(header_size));
  }

  template <class LowerLayerPtr>
  bool handle(LowerLayerPtr& down, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    switch (hdr.type) {
      case message_type::handshake:
        down->abort_reason(ec::unexpected_handshake);
        return false;
      case message_type::actor_message:
        return handle_actor_message(down, hdr, payload);
      case message_type::resolve_request:
        return handle_resolve_request(down, hdr, payload);
      case message_type::resolve_response:
        return handle_resolve_response(down, hdr, payload);
      case message_type::monitor_message:
        return handle_monitor_message(down, hdr, payload);
      case message_type::down_message:
        return handle_down_message(down, hdr, payload);
      case message_type::heartbeat:
        return true;
      default:
        down->abort_reason(ec::unimplemented);
        return false;
    }
  }

  template <class LowerLayerPtr>
  bool handle_handshake(LowerLayerPtr& down, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    if (hdr.type != message_type::handshake) {
      down->abort_reason(ec::missing_handshake);
      return false;
    }
    if (hdr.operation_data != version) {
      down->abort_reason(ec::version_mismatch);
      return false;
    }
    node_id peer_id;
    std::vector<std::string> app_ids;
    binary_deserializer source{&executor_, payload};
    if (!source.apply_objects(peer_id, app_ids)) {
      down->abort_reason(std::move(source.get_error()));
      return false;
    }
    if (!peer_id || app_ids.empty()) {
      down->abort_reason(ec::invalid_handshake);
      return false;
    }
    auto ids = get_or(system().config(), "caf.middleman.app-identifiers",
                      basp::application::default_app_ids());
    auto predicate = [=](const std::string& x) {
      return std::find(ids.begin(), ids.end(), x) != ids.end();
    };
    if (std::none_of(app_ids.begin(), app_ids.end(), predicate)) {
      down->abort_reason(ec::app_identifiers_mismatch);
      return false;
    }
    peer_id_ = std::move(peer_id);
    return true;
  }

  template <class LowerLayerPtr>
  bool handle_actor_message(LowerLayerPtr&, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    auto worker = hub_.pop();
    if (worker != nullptr) {
      CAF_LOG_DEBUG("launch BASP worker for deserializing an actor_message");
      worker->launch(node_id{}, hdr, payload);
    } else {
      CAF_LOG_DEBUG(
        "out of BASP workers, continue deserializing an actor_message");
      // If no worker is available then we have no other choice than to take
      // the performance hit and deserialize in this thread.
      struct handler : remote_message_handler<handler> {
        handler(message_queue* queue, proxy_registry* proxies,
                actor_system* system, node_id last_hop, basp::header& hdr,
                byte_span payload)
          : queue_(queue),
            proxies_(proxies),
            system_(system),
            last_hop_(std::move(last_hop)),
            hdr_(hdr),
            payload_(payload) {
          msg_id_ = queue_->new_id();
        }
        message_queue* queue_;
        proxy_registry* proxies_;
        actor_system* system_;
        node_id last_hop_;
        basp::header& hdr_;
        byte_span payload_;
        uint64_t msg_id_;
      };
      handler f{&queue_, &proxies_, system_, node_id{}, hdr, payload};
      f.handle_remote_message(&executor_);
    }
    return true;
  }

  template <class LowerLayerPtr>
  bool
  handle_resolve_request(LowerLayerPtr& down, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    CAF_ASSERT(hdr.type == message_type::resolve_request);
    size_t path_size = 0;
    binary_deserializer source{&executor_, payload};
    if (!source.begin_sequence(path_size)) {
      down->abort_reason(std::move(source.get_error()));
      return false;
    }
    // We expect the received buffer to contain the path only.
    if (path_size != source.remaining()) {
      down->abort_reason(ec::invalid_payload);
      return false;
    }
    auto remainder = source.remainder();
    string_view path{reinterpret_cast<const char*>(remainder.data()),
                     remainder.size()};
    // Write result.
    auto result = resolve_local_path(path);
    actor_id aid;
    std::set<std::string> ifs;
    if (result) {
      aid = result->id();
      system().registry().put(aid, result);
    } else {
      aid = 0;
    }
    // TODO: figure out how to obtain messaging interface.
    return write_message(
      down, header{message_type::resolve_response, hdr.operation_data}, aid,
      ifs);
  }

  template <class LowerLayerPtr>
  bool
  handle_resolve_response(LowerLayerPtr& down, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    CAF_ASSERT(hdr.type == message_type::resolve_response);
    auto i = pending_resolves_.find(hdr.operation_data);
    if (i == pending_resolves_.end()) {
      CAF_LOG_WARNING("received unknown ID in resolve_response message");
      return true;
    }
    auto guard = detail::make_scope_guard([&] { pending_resolves_.erase(i); });
    actor_id aid;
    std::set<std::string> ifs;
    binary_deserializer source{&executor_, payload};
    if (!source.apply_objects(aid, ifs)) {
      anon_send(i->second, sec::remote_lookup_failed);
      down->abort_reason(std::move(source.get_error()));
      return false;
    }
    if (aid == 0)
      anon_send(i->second, strong_actor_ptr{nullptr}, std::move(ifs));
    else
      anon_send(i->second, proxies_.get_or_put(peer_id_, aid), std::move(ifs));
    return true;
  }

  template <class LowerLayerPtr>
  bool
  handle_monitor_message(LowerLayerPtr& down, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    if (!payload.empty()) {
      down->abort_reason(ec::unexpected_payload);
      return false;
    }
    auto aid = static_cast<actor_id>(hdr.operation_data);
    if (auto hdl = system().registry().get(aid)) {
      auto wself = self_.as_actor_addr();
      hdl->get()->attach_functor([aid, wself](error reason) mutable {
        if (auto sref = actor_cast<actor>(wself))
          anon_send(sref, local_actor_down_msg{aid, std::move(reason)});
      });
    } else {
      error reason = exit_reason::unknown;
      return write_message(
        down, header{message_type::down_message, hdr.operation_data}, reason);
    }
    return true;
  }

  template <class LowerLayerPtr>
  bool handle_down_message(LowerLayerPtr& down, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    error reason;
    binary_deserializer source{&executor_, payload};
    if (!source.apply_objects(reason)) {
      down->abort_reason(std::move(source.get_error()));
      return false;
    }
    proxies_.erase(peer_id_, hdr.operation_data, std::move(reason));
    return true;
  }

  // -- member variables -------------------------------------------------------

  /// Stores a pointer to the parent actor system.
  actor_system* system_ = nullptr;

  /// Stores whether the BASP handshake completed successfully.
  bool handshake_complete_ = false;

  /// Stores the ID of our peer.
  node_id peer_id_;

  /// Tracks which local actors our peer monitors.
  std::unordered_set<actor_addr> monitored_actors_; // TODO: this is unused

  /// Caches actor handles obtained via `resolve`.
  std::unordered_map<uint64_t, actor> pending_resolves_;

  /// Ascending ID generator for requests to our peer.
  uint64_t next_request_id_ = 1;

  /// Points to the factory object for generating proxies.
  proxy_registry& proxies_;

  size_t max_throughput_ = 0;

  /// Provides pointers to the actor system as well as the registry,
  /// serializers and deserializer.
  scoped_execution_unit executor_;

  message_queue queue_;

  caf::net::actor_shell_ptr self_;

  hub_type hub_;
};

} // namespace caf::net::basp
