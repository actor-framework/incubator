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

#include <limits>

#include "caf/actor_system_config.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/net/defaults.hpp"
#include "caf/net/endpoint_manager_queue.hpp"
#include "caf/net/packet_writer.hpp"
#include "caf/net/packet_writer_decorator.hpp"
#include "caf/net/reliability/ordering_header.hpp"
#include "caf/span.hpp"

namespace {

bool is_greater(
  caf::net::reliability::sequence_type lhs,
  caf::net::reliability::sequence_type rhs,
  caf::net::reliability::sequence_type max_distance
  = std::numeric_limits<caf::net::reliability::sequence_type>::max() / 2) {
  // distance between lhs and rhs is smaller than max_distance.
  return ((lhs > rhs) && (lhs - rhs <= max_distance))
         || ((lhs < rhs) && (rhs - lhs > max_distance));
}

struct sequence_comparator {
  bool operator()(const caf::net::reliability::sequence_type& lhs,
                  const caf::net::reliability::sequence_type& rhs) const {
    return is_greater(rhs, lhs);
  }
};

} // namespace

namespace caf::net::reliability {

/// Implements an application protocol that ensures ordering withing udp
/// datagram communication.
template <class Application>
class ordering {
public:
  // -- constructors, destructors, and assignment operators --------------------

  ordering(Application application)
    : application_(std::move(application)),
      seq_read_(0),
      seq_write_(0),
      max_pending_messages_(0),
      pending_to_(std::chrono::milliseconds(100)) {
    // nop
  }

  // -- interface functions ----------------------------------------------------

  template <class Parent>
  error init(Parent& parent) {
    max_pending_messages_ = get_or(parent->config(),
                                   "middleman.max-pending-messages",
                                   defaults::reliability::max_pending_messages);
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
    binary_serializer sink(&parent.system(), hdr);
    if (auto err = sink(ordering_header{seq_write_++}))
      CAF_LOG_ERROR("could not serialize header" << CAF_ARG(err));
    parent.write_packet(hdr, buffers...);
  }

  template <class Parent>
  error handle_data(Parent& parent, span<const byte> received) {
    if (received.size() < reliability::ordering_header_size)
      return make_error(sec::unexpected_message,
                        "did not receive enough bytes");
    auto writer = make_packet_writer_decorator(*this, parent);
    ordering_header header;
    binary_deserializer source(parent.system(), received);
    if (auto err = source(header))
      return err;
    if (header.sequence == seq_read_) {
      ++seq_read_;
      if (auto err = application_.handle_data(
            writer, make_span(received.data() + ordering_header_size,
                              received.size() - ordering_header_size)))
        return err;
      return deliver_pending(writer);
    } else if (is_greater(header.sequence, seq_read_)) {
      return add_pending(writer, received, header.sequence);
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
  void timeout(Parent& parent, std::string tag, uint64_t id) {
    auto writer = make_packet_writer_decorator(*this, parent);
    application_.timeout(writer, std::move(tag), id);
  }

  void handle_error(sec error) {
    application_.handle_error(error);
  }

private:
  template <class Parent>
  error deliver_pending(Parent& parent) {
    if (pending_.empty()) {
      std::cout << "no pending messages" << std::endl;
      return none;
    }
    std::cout << "deliver " << pending_.count(seq_read_) << " pending messages"
              << std::endl;
    // TODO: cant this be done more efficiently?
    while (pending_.count(seq_read_) > 0) {
      auto& buf = pending_[seq_read_];
      auto err = application_.handle_data(parent, make_span(buf));
      pending_.erase(seq_read_++);
      if (err)
        return err;
    }
    return none;
  }

  template <class Parent>
  error add_pending(Parent& parent, span<const byte> bytes, sequence_type seq) {
    std::cout << "Added pending message with seq = " << std::to_string(seq)
              << std::endl;
    pending_[seq] = byte_buffer(bytes.begin(), bytes.end());
    // TODO: timeouts should be used to deliver pending packets every `x`
    // milliseconds.
    // if (use_timeouts)
    //   parent->set_timeout(pending_to, ordering_atom::value, seq);
    if (pending_.size() > max_pending_messages_) {
      seq_read_ = pending_.begin()->first;
      return deliver_pending(parent);
    }
    return none;
  }

  Application application_;

  sequence_type seq_read_;

  sequence_type seq_write_;

  size_t max_pending_messages_;

  std::chrono::milliseconds pending_to_;

  std::map<sequence_type, byte_buffer, sequence_comparator> pending_;
};

} // namespace caf::net::reliability
