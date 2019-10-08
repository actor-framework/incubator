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
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "caf/actor_addr.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/byte.hpp"
#include "caf/callback.hpp"
#include "caf/error.hpp"
#include "caf/logger.hpp"
#include "caf/net/basp/connection_state.hpp"
#include "caf/net/basp/constants.hpp"
#include "caf/net/basp/header.hpp"
#include "caf/net/basp/message_type.hpp"
#include "caf/net/endpoint_manager.hpp"
#include "caf/net/receive_policy.hpp"
#include "caf/no_stages.hpp"
#include "caf/node_id.hpp"
#include "caf/proxy_registry.hpp"
#include "caf/response_promise.hpp"
#include "caf/serializer_impl.hpp"
#include "caf/span.hpp"
#include "caf/unit.hpp"
#include "ec.hpp"

namespace caf {
namespace net {
namespace basp {

/// An implementation of BASP as an application layer protocol.
class application {
public:
  // -- member types -----------------------------------------------------------

  using buffer_type = std::vector<byte>;

  using byte_span = span<const byte>;

  using write_packet_callback = callback<std::vector<byte>, std::vector<byte>,
                                         std::vector<byte>>;

  using proxy_registry_ptr = std::shared_ptr<proxy_registry>;

  // -- constructors, destructors, and assignment operators --------------------

  explicit application(proxy_registry_ptr proxies);

  // -- interface functions ----------------------------------------------------

  template <class Parent>
  error init(Parent& parent) {
    // Initialize member variables.
    system_ = &parent.system();
    // Write handshake.
    auto header_buf = parent.transport().get_buffer();
    auto payload_elem_buf = parent.transport().get_buffer();
    auto payload_buf = parent.transport().get_buffer();
    if (auto err = generate_handshake(payload_buf))
      return err;
    to_bytes(header{message_type::handshake,
                    static_cast<uint32_t>(payload_buf.size()), version},
             header_buf);
    parent.write_packet(std::move(header_buf), std::move(payload_elem_buf),
                        std::move(payload_buf));
    parent.transport().configure_read(receive_policy::exactly(header_size));
    return none;
  }

  template <class Parent>
  error write_message(Parent& parent,
                      std::unique_ptr<endpoint_manager::message> ptr) {
    auto header_buf = parent.transport().get_buffer();
    auto payload_elem_buf = parent.transport().get_buffer();
    serializer_impl<buffer_type> sink{system(), payload_elem_buf};
    const auto& src = ptr->msg->sender;
    const auto& dst = ptr->receiver;
    if (dst == nullptr) {
      // TODO: valid?
      return none;
    }
    if (src != nullptr) {
      if (auto err = sink(src->node(), src->id(), dst->id(), ptr->msg->stages))
        return err;
    } else {
      if (auto err = sink(node_id{}, actor_id{0}, dst->id(), ptr->msg->stages))
        return err;
    }
    to_bytes(header{message_type::actor_message,
                    static_cast<uint32_t>(payload_elem_buf.size()),
                    ptr->msg->mid.integer_value()},
             header_buf);
    parent.write_packet(std::move(header_buf), std::move(payload_elem_buf),
                        std::move(ptr->payload));
    return none;
  }

  template <class Parent>
  error handle_data(Parent& parent, byte_span bytes) {
    auto write_packet = make_callback([&](std::vector<byte> hdr,
                                          std::vector<byte> payload_elem,
                                          std::vector<byte> payload) {
      parent.write_packet(std::move(hdr), std::move(payload_elem),
                          std::move(payload));
      return none;
    });
    size_t next_read_size = header_size;
    if (auto err = handle(parent, next_read_size, write_packet, bytes))
      return err;
    parent.transport().configure_read(receive_policy::exactly(next_read_size));
    return none;
  }

  template <class Parent>
  void resolve(Parent& parent, string_view path, actor listener) {
    auto write_packet = make_callback([&](std::vector<byte> hdr,
                                          std::vector<byte> payload_elem,
                                          std::vector<byte> payload) {
      parent.write_packet(std::move(hdr), std::move(payload_elem),
                          std::move(payload));
      return none;
    });
    resolve_remote_path(parent, write_packet, path, listener);
  }

  template <class Transport>
  void timeout(Transport&, atom_value, uint64_t) {
    // nop
  }

  void handle_error(sec) {
    // nop
  }

  static expected<std::vector<byte>> serialize(actor_system& sys,
                                               const type_erased_tuple& x);

  // -- utility functions ------------------------------------------------------

  strong_actor_ptr resolve_local_path(string_view path);

  template <class Parent>
  void resolve_remote_path(Parent& parent, write_packet_callback& write_packet,
                           string_view path, actor listener) {
    auto header_buf = parent.transport().get_buffer();
    auto payload_elem_buf = parent.transport().get_buffer();
    auto payload_buf = parent.transport().get_buffer();
    serializer_impl<buffer_type> sink{system(), payload_buf};
    if (auto err = sink(path)) {
      CAF_LOG_ERROR("unable to serialize path");
      return;
    }
    auto req_id = next_request_id_++;
    to_bytes(header{message_type::resolve_request,
                    static_cast<uint32_t>(payload_buf.size()), req_id},
             header_buf);
    if (auto err = write_packet(std::move(header_buf),
                                std::move(payload_elem_buf),
                                std::move(payload_buf))) {
      CAF_LOG_ERROR("unable to write resolve_request header");
      return;
    }
    response_promise rp{nullptr, actor_cast<strong_actor_ptr>(listener),
                        no_stages, make_message_id()};
    pending_resolves_.emplace(req_id, std::move(rp));
  }

  // -- properties -------------------------------------------------------------

  connection_state state() const noexcept {
    return state_;
  }

  actor_system& system() const noexcept {
    return *system_;
  }

private:
  // -- message handling -------------------------------------------------------

  template <class Parent>
  error handle(Parent& parent, size_t& next_read_size,
               write_packet_callback& write_packet, byte_span bytes) {
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
        if (auto err = handle_handshake(write_packet, hdr_, bytes))
          return err;
        state_ = connection_state::await_header;
        return none;
      }
      case connection_state::await_header: {
        if (bytes.size() != header_size)
          return ec::unexpected_number_of_bytes;
        hdr_ = header::from_bytes(bytes);
        if (hdr_.payload_len == 0)
          return handle(parent, write_packet, hdr_, byte_span{});
        next_read_size = hdr_.payload_len;
        state_ = connection_state::await_payload;
        return none;
      }
      case connection_state::await_payload: {
        if (bytes.size() != hdr_.payload_len)
          return ec::unexpected_number_of_bytes;
        state_ = connection_state::await_header;
        return handle(parent, write_packet, hdr_, bytes);
      }
      default:
        return ec::illegal_state;
    }
  }

  template <class Parent>
  error handle(Parent& parent, write_packet_callback& write_packet, header hdr,
               byte_span payload) {
    switch (hdr.type) {
      case message_type::handshake:
        return ec::unexpected_handshake;
      case message_type::actor_message:
        return handle_actor_message(write_packet, hdr, payload);
      case message_type::resolve_request:
        return handle_resolve_request(parent, write_packet, hdr, payload);
      case message_type::resolve_response:
        return handle_resolve_response(write_packet, hdr, payload);
      case message_type::heartbeat:
        return none;
      default:
        return ec::unimplemented;
    }
  }

  error handle_handshake(write_packet_callback& write_packet, header hdr,
                         byte_span payload);

  error handle_actor_message(write_packet_callback& write_packet, header hdr,
                             byte_span payload);

  template <class Parent>
  error handle_resolve_request(Parent& parent,
                               write_packet_callback& write_packet, header hdr,
                               byte_span payload) {
    CAF_ASSERT(hdr.type == message_type::resolve_request);
    size_t path_size = 0;
    binary_deserializer source{system(), payload};
    if (auto err = source.begin_sequence(path_size))
      return err;
    // We expect the payload to consist only of the path.
    if (path_size != source.remaining())
      return ec::invalid_payload;
    auto remainder = source.remainder();
    string_view path{reinterpret_cast<const char*>(remainder.data()),
                     remainder.size()};
    // Write result.
    auto result = resolve_local_path(path);
    actor_id aid = result ? result->id() : 0;
    std::set<std::string> ifs;
    // TODO: figure out how to obtain messaging interface.
    auto header_buf = parent.transport().get_buffer();
    auto payload_elem_buf = parent.transport().get_buffer();
    auto payload_buf = parent.transport().get_buffer();
    serializer_impl<buffer_type> sink{system(), payload_buf};
    if (auto err = sink(aid, ifs))
      return err;
    to_bytes(header{message_type::resolve_response,
                    static_cast<uint32_t>(payload_buf.size()),
                    hdr.operation_data},
             header_buf);
    return write_packet(std::move(header_buf), std::move(payload_elem_buf),
                        std::move(payload_buf));
  }

  error handle_resolve_response(write_packet_callback& write_packet, header hdr,
                                byte_span payload);

  /// Writes the handshake payload to `buf`.
  error generate_handshake(std::vector<byte>& buf);

  // -- member variables -------------------------------------------------------

  /// Stores a pointer to the parent actor system.
  actor_system* system_ = nullptr;

  /// Stores the expected type of the next incoming message.
  connection_state state_ = connection_state::await_handshake_header;

  /// Caches the last header while waiting for the matching payload.
  header hdr_;

  /// Stores our own ID.
  node_id id_;

  /// Stores the ID of our peer.
  node_id peer_id_;

  /// Tracks which local actors our peer monitors.
  std::unordered_set<actor_addr> monitored_actors_;

  /// Caches actor handles obtained via `resolve`.
  std::unordered_map<uint64_t, response_promise> pending_resolves_;

  /// Ascending ID generator for requests to our peer.
  uint64_t next_request_id_ = 1;

  /// Points to the factory object for generating proxies.
  proxy_registry_ptr proxies_;
};

} // namespace basp
} // namespace net
} // namespace caf
