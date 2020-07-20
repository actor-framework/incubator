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

#include "caf/actor_addr.hpp"
#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/byte.hpp"
#include "caf/callback.hpp"
#include "caf/defaults.hpp"
#include "caf/detail/net_export.hpp"
#include "caf/detail/worker_hub.hpp"
#include "caf/error.hpp"
#include "caf/fwd.hpp"
#include "caf/net/basp/connection_state.hpp"
#include "caf/net/basp/constants.hpp"
#include "caf/net/basp/ec.hpp"
#include "caf/net/basp/header.hpp"
#include "caf/net/basp/message_queue.hpp"
#include "caf/net/basp/message_type.hpp"
#include "caf/net/basp/worker.hpp"
#include "caf/net/endpoint_manager.hpp"
#include "caf/net/receive_policy.hpp"
#include "caf/node_id.hpp"
#include "caf/proxy_registry.hpp"
#include "caf/response_promise.hpp"
#include "caf/scoped_execution_unit.hpp"
#include "caf/send.hpp"
#include "caf/unit.hpp"

namespace caf::net::basp {

/// An implementation of BASP as an application layer protocol.
class CAF_NET_EXPORT application {
public:
  // -- member types -----------------------------------------------------------

  using byte_span = span<const byte>;

  using hub_type = detail::worker_hub<worker>;

  struct test_tag {};

  // -- constructors, destructors, and assignment operators --------------------

  explicit application(proxy_registry& proxies);

  // -- static utility functions -----------------------------------------------

  static auto default_app_ids() {
    return std::vector<std::string>{
      to_string(defaults::middleman::app_identifier)};
  }

  // -- interface functions ----------------------------------------------------

  template <class Parent>
  error init(Parent& parent) {
    // Initialize member variables.
    system_ = &parent.system();
    executor_.system_ptr(system_);
    executor_.proxy_registry_ptr(&proxies_);
    // TODO: use `if constexpr` when switching to C++17.
    // Allow unit tests to run the application without endpoint manager.
    if (!std::is_base_of<test_tag, Parent>::value)
      manager_ = &parent.manager();
    size_t workers;
    if (auto workers_cfg = get_if<size_t>(&system_->config(),
                                          "middleman.workers"))
      workers = *workers_cfg;
    else
      workers = std::min(3u, std::thread::hardware_concurrency() / 4u) + 1;
    for (size_t i = 0; i < workers; ++i)
      hub_->add_new_worker(*queue_, proxies_);
    // Write handshake.
    auto hdr = parent.next_header_buffer();
    auto payload = parent.next_payload_buffer();
    if (auto err = generate_handshake(payload))
      return err;
    to_bytes(header{message_type::handshake,
                    static_cast<uint32_t>(payload.size()), version},
             hdr);
    parent.write_packet(hdr, payload);
    parent.transport().configure_read(receive_policy::exactly(header_size));
    return none;
  }

  template <class Parent>
  error write_message(Parent& parent,
                      std::unique_ptr<endpoint_manager_queue::message> ptr) {
    CAF_ASSERT(ptr != nullptr);
    CAF_ASSERT(ptr->msg != nullptr);
    CAF_LOG_TRACE(CAF_ARG2("content", ptr->msg->content()));
    const auto& src = ptr->msg->sender;
    const auto& dst = ptr->receiver;
    if (dst == nullptr) {
      // TODO: valid?
      return none;
    }
    auto payload_buf = parent.next_payload_buffer();
    binary_serializer sink{system(), payload_buf};
    if (src != nullptr) {
      auto src_id = src->id();
      system().registry().put(src_id, src);
      if (auto err = sink(src->node(), src_id, dst->id(), ptr->msg->stages))
        return err;
    } else {
      if (auto err = sink(node_id{}, actor_id{0}, dst->id(), ptr->msg->stages))
        return err;
    }
    if (auto err = sink(ptr->msg->content()))
      return err;
    auto hdr = parent.next_header_buffer();
    to_bytes(header{message_type::actor_message,
                    static_cast<uint32_t>(payload_buf.size()),
                    ptr->msg->mid.integer_value()},
             hdr);
    parent.write_packet(hdr, payload_buf);
    return none;
  }

  template <class Parent>
  error handle_data(Parent& parent, byte_span bytes) {
    size_t next_read_size = header_size;
    if (auto err = handle(next_read_size, parent, bytes))
      return err;
    parent.transport().configure_read(receive_policy::exactly(next_read_size));
    return none;
  }

  template <class Parent>
  void resolve(Parent& parent, string_view path, const actor& listener) {
    CAF_LOG_TRACE(CAF_ARG(path) << CAF_ARG(listener));
    auto payload = parent.next_payload_buffer();
    binary_serializer sink{&executor_, payload};
    if (auto err = sink(path)) {
      CAF_LOG_ERROR("unable to serialize path" << CAF_ARG(err));
      return;
    }
    auto req_id = next_request_id_++;
    auto hdr = parent.next_header_buffer();
    to_bytes(header{message_type::resolve_request,
                    static_cast<uint32_t>(payload.size()), req_id},
             hdr);
    parent.write_packet(hdr, payload);
    pending_resolves_.emplace(req_id, listener);
  }

  template <class Parent>
  static void new_proxy(Parent& parent, actor_id id) {
    auto hdr = parent.next_header_buffer();
    to_bytes(
      header{message_type::monitor_message, 0, static_cast<uint64_t>(id)}, hdr);
    parent.write_packet(hdr);
  }

  template <class Parent>
  void local_actor_down(Parent& parent, actor_id id, error reason) {
    auto payload = parent.next_payload_buffer();
    binary_serializer sink{system(), payload};
    if (auto err = sink(reason))
      CAF_RAISE_ERROR("unable to serialize an error");
    auto hdr = parent.next_header_buffer();
    to_bytes(header{message_type::down_message,
                    static_cast<uint32_t>(payload.size()),
                    static_cast<uint64_t>(id)},
             hdr);
    parent.write_packet(hdr, payload);
  }

  template <class Parent>
  void timeout(Parent&, const std::string&, uint64_t) {
    // nop
  }

  void handle_error(sec) {
    // nop
  }

  // -- utility functions ------------------------------------------------------

  strong_actor_ptr resolve_local_path(string_view path);

  // -- properties -------------------------------------------------------------

  connection_state state() const noexcept {
    return state_;
  }

  actor_system& system() const noexcept {
    return *system_;
  }

private:
  // -- handling of incoming messages ------------------------------------------

  template <class Parent>
  error handle(size_t& next_read_size, Parent& parent, byte_span bytes) {
    CAF_LOG_TRACE(CAF_ARG(state_) << CAF_ARG2("bytes.size", bytes.size()));
    switch (state_) {
      case connection_state::await_handshake_header: {
        if (bytes.size() != header_size)
          return ec::unexpected_number_of_bytes;
        hdr_ = header::from_bytes(bytes);
        if (hdr_.type != message_type::handshake)
          return ec::missing_handshake;
        if (hdr_.operation_data != version)
          return ec::version_mismatch;
        if (hdr_.payload_len == 0)
          return ec::missing_payload;
        state_ = connection_state::await_handshake_payload;
        next_read_size = hdr_.payload_len;
        return none;
      }
      case connection_state::await_handshake_payload: {
        if (auto err = handle_handshake(parent, hdr_, bytes)) {
          return err;
        }
        state_ = connection_state::await_header;
        return none;
      }
      case connection_state::await_header: {
        if (bytes.size() != header_size)
          return ec::unexpected_number_of_bytes;
        hdr_ = header::from_bytes(bytes);
        if (hdr_.payload_len == 0)
          return handle(parent, hdr_, byte_span{});
        next_read_size = hdr_.payload_len;
        state_ = connection_state::await_payload;
        auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch());
        std::cout << "basp.handle_header: " << std::to_string(ts.count())
                  << ", ";
        return none;
      }
      case connection_state::await_payload: {
        if (bytes.size() != hdr_.payload_len)
          return ec::unexpected_number_of_bytes;
        state_ = connection_state::await_header;
        return handle(parent, hdr_, bytes);
      }
      default:
        return ec::illegal_state;
    }
  }

  template <class Parent>
  error handle(Parent& parent, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    switch (hdr.type) {
      case message_type::handshake:
        return ec::unexpected_handshake;
      case message_type::actor_message:
        return handle_actor_message(parent, hdr, payload);
      case message_type::resolve_request:
        return handle_resolve_request(parent, hdr, payload);
      case message_type::resolve_response:
        return handle_resolve_response(parent, hdr, payload);
      case message_type::monitor_message:
        return handle_monitor_message(parent, hdr, payload);
      case message_type::down_message:
        return handle_down_message(parent, hdr, payload);
      case message_type::heartbeat:
        return none;
      default:
        return ec::unimplemented;
    }
  }

  template <class Parent>
  error handle_handshake(Parent&, header hdr, byte_span payload) {
    CAF_LOG_TRACE(CAF_ARG(hdr) << CAF_ARG2("payload.size", payload.size()));
    if (hdr.type != message_type::handshake)
      return ec::missing_handshake;
    if (hdr.operation_data != version)
      return ec::version_mismatch;
    node_id peer_id;
    std::vector<std::string> app_ids;
    binary_deserializer source{&executor_, payload};
    if (auto err = source(peer_id, app_ids))
      return err;
    if (!peer_id || app_ids.empty())
      return ec::invalid_handshake;
    auto ids = get_or(system().config(), "middleman.app-identifiers",
                      basp::application::default_app_ids());
    auto predicate = [=](const std::string& x) {
      return std::find(ids.begin(), ids.end(), x) != ids.end();
    };
    if (std::none_of(app_ids.begin(), app_ids.end(), predicate))
      return ec::app_identifiers_mismatch;
    peer_id_ = std::move(peer_id);
    state_ = connection_state::await_header;
    return none;
  }

  template <class Parent>
  error handle_actor_message(Parent&, header hdr, byte_span payload) {
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
    auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now().time_since_epoch());
    std::cout << "basp.handle_payload: " << std::to_string(ts.count()) << ", ";
    return none;
  }

  template <class Parent>
  error
  handle_resolve_request(Parent& parent, header rec_hdr, byte_span received) {
    CAF_LOG_TRACE(CAF_ARG(rec_hdr)
                  << CAF_ARG2("received.size", received.size()));
    CAF_ASSERT(rec_hdr.type == message_type::resolve_request);
    size_t path_size = 0;
    binary_deserializer source{&executor_, received};
    if (auto err = source.begin_sequence(path_size))
      return err;
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
    auto payload = parent.next_payload_buffer();
    binary_serializer sink{&executor_, payload};
    if (auto err = sink(aid, ifs))
      return err;
    auto hdr = parent.next_header_buffer();
    to_bytes(header{message_type::resolve_response,
                    static_cast<uint32_t>(payload.size()),
                    rec_hdr.operation_data},
             hdr);
    parent.write_packet(hdr, payload);
    return none;
  }

  template <class Parent>
  error
  handle_resolve_response(Parent&, header received_hdr, byte_span received) {
    CAF_LOG_TRACE(CAF_ARG(received_hdr)
                  << CAF_ARG2("received.size", received.size()));
    CAF_ASSERT(received_hdr.type == message_type::resolve_response);
    auto i = pending_resolves_.find(received_hdr.operation_data);
    if (i == pending_resolves_.end()) {
      CAF_LOG_ERROR("received unknown ID in resolve_response message");
      return none;
    }
    auto guard = detail::make_scope_guard([&] { pending_resolves_.erase(i); });
    actor_id aid;
    std::set<std::string> ifs;
    binary_deserializer source{&executor_, received};
    if (auto err = source(aid, ifs)) {
      anon_send(i->second, sec::remote_lookup_failed);
      return err;
    }
    if (aid == 0) {
      anon_send(i->second, strong_actor_ptr{nullptr}, std::move(ifs));
      return none;
    }
    anon_send(i->second, proxies_.get_or_put(peer_id_, aid), std::move(ifs));
    return none;
  }

  template <class Parent>
  error handle_monitor_message(Parent& parent, header received_hdr,
                               byte_span received) {
    CAF_LOG_TRACE(CAF_ARG(received_hdr)
                  << CAF_ARG2("received.size", received.size()));
    if (!received.empty())
      return ec::unexpected_payload;
    auto aid = static_cast<actor_id>(received_hdr.operation_data);
    auto hdl = system().registry().get(aid);
    if (hdl != nullptr) {
      endpoint_manager_ptr mgr = manager_;
      auto nid = peer_id_;
      hdl->get()->attach_functor([mgr, nid, aid](error reason) mutable {
        mgr->enqueue_event(std::move(nid), aid, std::move(reason));
      });
    } else {
      error reason = exit_reason::unknown;
      auto payload = parent.next_payload_buffer();
      binary_serializer sink{&executor_, payload};
      if (auto err = sink(reason))
        return err;
      auto hdr = parent.next_header_buffer();
      to_bytes(header{message_type::down_message,
                      static_cast<uint32_t>(payload.size()),
                      received_hdr.operation_data},
               hdr);
      parent.write_packet(hdr, payload);
    }
    return none;
  }

  template <class Parent>
  error handle_down_message(Parent&, header received_hdr, byte_span received) {
    CAF_LOG_TRACE(CAF_ARG(received_hdr)
                  << CAF_ARG2("received.size", received.size()));
    error reason;
    binary_deserializer source{&executor_, received};
    if (auto err = source(reason))
      return err;
    proxies_.erase(peer_id_, received_hdr.operation_data, std::move(reason));
    return none;
  }

  /// Writes the handshake payload to `buf_`.
  error generate_handshake(byte_buffer& buf);

  // -- member variables -------------------------------------------------------

  /// Stores a pointer to the parent actor system.
  actor_system* system_ = nullptr;

  /// Stores the expected type of the next incoming message.
  connection_state state_ = connection_state::await_handshake_header;

  /// Caches the last header while waiting for the matching payload.
  header hdr_;

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

  /// Points to the endpoint manager that owns this applications.
  endpoint_manager* manager_ = nullptr;

  /// Provides pointers to the actor system as well as the registry,
  /// serializers and deserializer.
  scoped_execution_unit executor_;

  std::unique_ptr<message_queue> queue_;

  std::unique_ptr<hub_type> hub_;
};

} // namespace caf::net::basp
