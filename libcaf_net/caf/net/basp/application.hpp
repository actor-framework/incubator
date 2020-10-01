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
#include <iterator>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "caf/actor.hpp"
#include "caf/actor_addr.hpp"
#include "caf/actor_clock.hpp"
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
#include "caf/intrusive/drr_queue.hpp"
#include "caf/intrusive/fifo_inbox.hpp"
#include "caf/intrusive/singly_linked.hpp"
#include "caf/mailbox_element.hpp"
#include "caf/net/actor_proxy_impl.hpp"
#include "caf/net/basp/constants.hpp"
#include "caf/net/basp/ec.hpp"
#include "caf/net/basp/header.hpp"
#include "caf/net/basp/message_queue.hpp"
#include "caf/net/basp/message_type.hpp"
#include "caf/net/basp/worker.hpp"
#include "caf/net/consumer.hpp"
#include "caf/net/consumer_queue.hpp"
#include "caf/net/multiplexer.hpp"
#include "caf/net/receive_policy.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/node_id.hpp"
#include "caf/policy/normal_messages.hpp"
#include "caf/proxy_registry.hpp"
#include "caf/response_promise.hpp"
#include "caf/scoped_execution_unit.hpp"
#include "caf/send.hpp"
#include "caf/tag/message_oriented.hpp"
#include "caf/unit.hpp"
#include "caf/variant.hpp"

namespace caf::net::basp {

/// An implementation of BASP as an application layer protocol.
class CAF_NET_EXPORT application : public consumer {
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
  error init(socket_manager* owner, LowerLayerPtr down, const settings&) {
    // Initialize member variables.
    owner_ = owner;
    system_ = &owner->mpx().system();
    executor_.system_ptr(system_);
    executor_.proxy_registry_ptr(&proxies_);
    // Allow unit tests to run the application without endpoint manager.
    size_t workers;
    if (auto workers_cfg = get_if<size_t>(&system_->config(),
                                          "caf.middleman.workers"))
      workers = *workers_cfg;
    else
      workers = std::min(3u, std::thread::hardware_concurrency() / 4u) + 1;
    for (size_t i = 0; i < workers; ++i)
      hub_->add_new_worker(*queue_, proxies_);
    // Write handshake.
    return write_message(
      down, header{message_type::handshake, version}, system().node(),
      get_or(system().config(), "caf.middleman.app-identifiers",
             application::default_app_ids()));
  }

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr& down) {
    if (!handshake_complete())
      return true;
    if (auto err = dequeue_events(down)) {
      CAF_LOG_ERROR("handle_events failed: " << CAF_ARG(err));
      down->abort_reason(err);
      return false;
    }
    if (auto err = dequeue_messages(down)) {
      CAF_LOG_ERROR("handle_messages failed: " << CAF_ARG(err));
      down->abort_reason(err);
      return false;
    }
    return true;
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume(LowerLayerPtr& down, byte_span buffer) {
    if (auto err = handle(down, buffer)) {
      CAF_LOG_ERROR("could not handle message: " << CAF_ARG(err));
      down->abort_reason(err);
      return -1;
    }
    return buffer.size();
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr&) {
    if (mailbox_.blocked())
      return true;
    return (mailbox_.empty() && mailbox_.try_block());
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr&, const error&) {
    // nop
  }

  void resolve(string_view path, const actor& listener);

  strong_actor_ptr make_proxy(const node_id& nid, const actor_id& aid);

  // -- utility functions ------------------------------------------------------

  strong_actor_ptr resolve_local_path(string_view path);

  /// Writes a message to the message buffer of `down`.
  template <class LowerLayerPtr, class... Ts>
  error write_message(LowerLayerPtr& down, header hdr, Ts&&... xs) {
    down->begin_message();
    auto& buf = down->message_buffer();
    binary_serializer sink{&executor_, buf};
    if (!sink.apply_object(hdr))
      return sink.get_error();
    if constexpr (sizeof...(xs) >= 1) {
      if (!sink.apply_objects(xs...))
        return sink.get_error();
    }
    down->end_message();
    return none;
  }

  // -- properties -------------------------------------------------------------

  bool handshake_complete() const noexcept {
    return handshake_complete_;
  }

  actor_system& system() const noexcept {
    return *system_;
  }

  // -- mailbox access ---------------------------------------------------------

  void enqueue(mailbox_element_ptr msg, strong_actor_ptr receiver) override;

  bool enqueue(consumer_queue::element* ptr) override;

private:
  consumer_queue::message_ptr next_message() {
    if (mailbox_.blocked())
      return nullptr;
    mailbox_.fetch_more();
    auto& q = std::get<1>(mailbox_.queue().queues());
    auto ts = q.next_task_size();
    if (ts == 0)
      return nullptr;
    q.inc_deficit(ts);
    auto result = q.next();
    if (mailbox_.empty())
      mailbox_.try_block();
    return result;
  }

  // -- handling of outgoing messages and events -------------------------------

  template <class LowerLayerPtr>
  error dequeue_events(LowerLayerPtr& down) {
    if (!mailbox_.blocked()) {
      mailbox_.fetch_more();
      auto& q = std::get<0>(mailbox_.queue().queues());
      do {
        q.inc_deficit(q.total_task_size());
        for (auto ptr = q.next(); ptr != nullptr; ptr = q.next()) {
          auto f = detail::make_overload(
            [&](consumer_queue::event::resolve_request& x) {
              write_resolve_request(down, x.locator, x.listener);
            },
            [&](consumer_queue::event::new_proxy& x) { new_proxy(down, x.id); },
            [&](consumer_queue::event::local_actor_down& x) {
              local_actor_down(down, x.id, std::move(x.reason));
            },
            [&](consumer_queue::event::timeout& x) {
              timeout(down, x.type, x.id);
            });
          visit(f, ptr->value);
        }
      } while (!q.empty());
    }
    return none;
  }

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
  void new_proxy(LowerLayerPtr& down, actor_id id) {
    if (auto err = write_message(down, header{message_type::monitor_message,
                                              static_cast<uint64_t>(id)}))
      down->abort_reason(err);
  }

  template <class LowerLayerPtr>
  void local_actor_down(LowerLayerPtr& down, actor_id id, error reason) {
    if (auto err = write_message(
          down, header{message_type::down_message, static_cast<uint64_t>(id)},
          reason))
      down->abort_reason(err);
  }

  template <class LowerLayerPtr>
  void timeout(LowerLayerPtr& down, std::string type, uint64_t id) {
    down->timeout(std::move(type), id);
  }

  template <class LowerLayerPtr>
  error dequeue_messages(LowerLayerPtr& down) {
    for (size_t count = 0; count < max_consecutive_messages_; ++count) {
      auto ptr = next_message();
      if (ptr == nullptr)
        break;
      CAF_ASSERT(ptr->msg != nullptr);
      CAF_LOG_TRACE(CAF_ARG2("content", ptr->msg->content()));
      const auto& src = ptr->msg->sender;
      const auto& dst = ptr->receiver;
      if (dst == nullptr) {
        // TODO: valid?
        return none;
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
            down,
            header{message_type::actor_message, ptr->msg->mid.integer_value()},
            nid, aid, dst->id(), ptr->msg->stages, ptr->msg->content())) {
        return err;
      }
    }
    return none;
  }

  // -- handling of incoming messages ------------------------------------------

  template <class LowerLayerPtr>
  error handle(LowerLayerPtr& down, byte_span bytes) {
    CAF_LOG_TRACE(CAF_ARG2("bytes.size", bytes.size()));
    if (!handshake_complete_) {
      if (bytes.size() < header_size)
        return ec::unexpected_number_of_bytes;
      auto hdr = header::from_bytes(bytes);
      if (hdr.type != message_type::handshake)
        return ec::missing_handshake;
      if (hdr.operation_data != version)
        return ec::version_mismatch;
      if (auto err = handle_handshake(down, hdr, bytes.subspan(header_size)))
        return err;
      handshake_complete_ = true;
      return none;
    } else {
      if (bytes.size() < header_size)
        return ec::unexpected_number_of_bytes;
      auto hdr = header::from_bytes(bytes);
      return handle(down, hdr, bytes.subspan(header_size));
    }
  }

  template <class LowerLayerPtr>
  error handle(LowerLayerPtr& down, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    switch (hdr.type) {
      case message_type::handshake:
        return ec::unexpected_handshake;
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
        return none;
      default:
        return ec::unimplemented;
    }
  }

  template <class LowerLayerPtr>
  error handle_handshake(LowerLayerPtr&, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    if (hdr.type != message_type::handshake)
      return ec::missing_handshake;
    if (hdr.operation_data != version)
      return ec::version_mismatch;
    node_id peer_id;
    std::vector<std::string> app_ids;
    binary_deserializer source{&executor_, payload};
    if (!source.apply_objects(peer_id, app_ids))
      return source.get_error();
    if (!peer_id || app_ids.empty())
      return ec::invalid_handshake;
    auto ids = get_or(system().config(), "caf.middleman.app-identifiers",
                      basp::application::default_app_ids());
    auto predicate = [=](const std::string& x) {
      return std::find(ids.begin(), ids.end(), x) != ids.end();
    };
    if (std::none_of(app_ids.begin(), app_ids.end(), predicate))
      return ec::app_identifiers_mismatch;
    peer_id_ = std::move(peer_id);
    return none;
  }

  template <class LowerLayerPtr>
  error handle_actor_message(LowerLayerPtr&, header hdr, byte_span payload) {
    auto worker = hub_->pop();
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
      handler f{queue_.get(), &proxies_, system_, node_id{}, hdr, payload};
      f.handle_remote_message(&executor_);
    }
    return none;
  }

  template <class LowerLayerPtr>
  error
  handle_resolve_request(LowerLayerPtr& down, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    CAF_ASSERT(hdr.type == message_type::resolve_request);
    size_t path_size = 0;
    binary_deserializer source{&executor_, payload};
    if (!source.begin_sequence(path_size))
      return source.get_error();
    // We expect the received buffer to contain the path only.
    if (path_size != source.remaining())
      return ec::invalid_payload;
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
  error handle_resolve_response(LowerLayerPtr&, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    CAF_ASSERT(hdr.type == message_type::resolve_response);
    auto i = pending_resolves_.find(hdr.operation_data);
    if (i == pending_resolves_.end()) {
      CAF_LOG_ERROR("received unknown ID in resolve_response message");
      return none;
    }
    auto guard = detail::make_scope_guard([&] { pending_resolves_.erase(i); });
    actor_id aid;
    std::set<std::string> ifs;
    binary_deserializer source{&executor_, payload};
    if (!source.apply_objects(aid, ifs)) {
      anon_send(i->second, sec::remote_lookup_failed);
      return source.get_error();
    }
    if (aid == 0) {
      anon_send(i->second, strong_actor_ptr{nullptr}, std::move(ifs));
      return none;
    }
    anon_send(i->second, proxies_.get_or_put(peer_id_, aid), std::move(ifs));
    return none;
  }

  template <class LowerLayerPtr>
  error
  handle_monitor_message(LowerLayerPtr& down, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    if (!payload.empty())
      return ec::unexpected_payload;
    auto aid = static_cast<actor_id>(hdr.operation_data);
    auto hdl = system().registry().get(aid);
    if (hdl != nullptr) {
      hdl->get()->attach_functor([this, aid](error reason) mutable {
        this->enqueue_event(aid, std::move(reason));
      });
    } else {
      error reason = exit_reason::unknown;
      return write_message(
        down, header{message_type::down_message, hdr.operation_data}, reason);
    }
    return none;
  }

  template <class LowerLayerPtr>
  error handle_down_message(LowerLayerPtr&, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    error reason;
    binary_deserializer source{&executor_, payload};
    if (!source.apply_objects(reason))
      return source.get_error();
    proxies_.erase(peer_id_, hdr.operation_data, std::move(reason));
    return none;
  }

  // -- member variables -------------------------------------------------------

  // Stores incoming actor messages.
  consumer_queue::type mailbox_;

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

  /// Points to the socket manager that owns this applications.
  socket_manager* owner_ = nullptr;

  // Guards access to owner_.
  std::mutex owner_mtx_;

  size_t max_consecutive_messages_ = 20; // TODO: this is a random number

  /// Provides pointers to the actor system as well as the registry,
  /// serializers and deserializer.
  scoped_execution_unit executor_;

  std::unique_ptr<message_queue> queue_;

  std::unique_ptr<hub_type> hub_;
};

} // namespace caf::net::basp
