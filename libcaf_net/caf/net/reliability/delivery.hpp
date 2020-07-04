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

#include <chrono>
#include <type_traits>
#include <unordered_map>

#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/logger.hpp"
#include "caf/net/endpoint_manager_queue.hpp"
#include "caf/net/packet_writer_decorator.hpp"
#include "caf/net/reliability/delivery_header.hpp"
#include "caf/span.hpp"
#include "caf/string_view.hpp"
#include "caf/timestamp.hpp"

namespace caf::net::reliability {

/// Implements an application protocol that ensures reliable communication for
/// unreliable protocols.
template <class Application>
class delivery {
public:
  // -- constructors, destructors, and assignment operators --------------------

  explicit delivery(Application application)
    : application_(std::move(application)), retransmit_timeout_(40) {
    // nop
  }

  // -- interface functions ----------------------------------------------------

  template <class Parent>
  error init(Parent& parent) {
    return application_.init(parent);
  }

  template <class Parent>
  error write_message(Parent& parent,
                      std::unique_ptr<endpoint_manager_queue::message> msg) {
    auto writer = make_packet_writer_decorator(*this, parent);
    return application_.write_message(writer, std::move(msg));
  }

  template <class Parent, class... Ts>
  void write_packet(Parent& parent, Ts&... buffers) {
    auto hdr = parent.next_header_buffer();
    binary_serializer sink(parent.system(), hdr);
    if (auto err = sink(delivery_header{id_write_, false})) {
      CAF_LOG_ERROR("could not serialize header" << CAF_ARG(err));
      return;
    }
    add_unacked(parent, id_write_++, hdr, buffers...);
    parent.write_packet(hdr, buffers...);
  }

  template <class Parent>
  error handle_data(Parent& parent, span<const byte> received) {
    if (received.size() < delivery_header_size)
      return sec::unexpected_message;
    delivery_header hdr;
    binary_deserializer source(parent.system(), received);
    if (auto err = source(hdr))
      return err;
    if (hdr.is_ack) {
      remove_unacked(parent, hdr.id);
    } else {
      // TODO: Piggyback and/or batch ACKs
      // Send ack.
      auto buf = parent.next_header_buffer();
      binary_serializer sink(parent.system(), buf);
      if (auto err = sink(delivery_header{hdr.id, true}))
        return err;
      parent.write_packet(buf);
      auto writer = make_packet_writer_decorator(*this, parent);
      if (auto err = application_.handle_data(
            writer, make_span(received.data() + delivery_header_size,
                              received.size() - delivery_header_size)))
        return err;
    }
    return none;
  }

  template <class Parent>
  void resolve(Parent& parent, string_view path, const actor& listener) {
    auto writer = make_packet_writer_decorator(*this, parent);
    application_.resolve(writer, path, listener);
  }

  template <class Parent>
  void new_proxy(Parent& parent, actor_id id) {
    auto writer = make_packet_writer_decorator(*this, parent);
    application_.new_proxy(writer, id);
  }

  template <class Parent>
  void local_actor_down(Parent& parent, actor_id id, error reason) {
    auto writer = make_packet_writer_decorator(*this, parent);
    application_.local_actor_down(writer, id, std::move(reason));
  }

  template <class Parent>
  void timeout(Parent& parent, std::string tag, uint64_t timeout_id) {
    if (tag_ != tag) {
      auto writer = make_packet_writer_decorator(*this, parent);
      application_.timeout(writer, std::move(tag), timeout_id);
    } else {
      auto retransmit_id = timeouts_.at(timeout_id);
      timeouts_.erase(timeout_id);
      if (unacked_.count(retransmit_id) > 0) {
        // Retransmit the packet.
        auto& packet = unacked_[retransmit_id];
        parent.write_packet(packet);
        auto when = parent.system().clock().now() + retransmit_timeout_;
        auto timeout_id = parent.set_timeout(when, to_string(tag_));
        timeouts_.emplace(timeout_id, retransmit_id);
      }
    }
  }

  void handle_error(sec error) {
    application_.handle_error(error);
  }

private:
  // Inserts variadic template `bufs` into a single buffer `buf`.
  // Necessary for saving unacked packets until they are ACKed.
  template <class... Ts>
  void insert(byte_buffer& buf, Ts&... bufs) {
    static_assert((std::is_same_v<byte, typename Ts::value_type> && ...));
    (buf.insert(buf.end(), bufs.begin(), bufs.end()), ...);
  }

  template <class Parent, class... Ts>
  void add_unacked(Parent& parent, id_type retransmit_id, Ts&... buffers) {
    auto buf = parent.next_payload_buffer();
    insert(buf, buffers...);
    unacked_.emplace(retransmit_id, std::move(buf));
    auto when = parent.system().clock().now() + retransmit_timeout_;
    auto timeout_id = parent.set_timeout(when, to_string(tag_));
    timeouts_.emplace(timeout_id, retransmit_id);
  }

  template <class Parent>
  void remove_unacked(Parent& parent, id_type retransmit_id) {
    unacked_.erase(retransmit_id);
    auto p
      = std::find_if(timeouts_.begin(), timeouts_.end(),
                     [&](const auto& p) { return p.second == retransmit_id; });
    if (p != timeouts_.end()) {
      parent.cancel_timeout(to_string(tag_), p->first);
      timeouts_.erase(p->first);
    }
  }

  Application application_;

  id_type id_write_ = 0;

  std::chrono::milliseconds retransmit_timeout_;

  std::unordered_map<id_type, byte_buffer> unacked_;

  std::unordered_map<uint64_t, id_type> timeouts_;

  static constexpr string_view tag_ = "delivery";
};

} // namespace caf::net::reliability
