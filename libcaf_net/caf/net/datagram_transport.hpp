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

#include <queue>
#include <vector>

#include "caf/byte_buffer.hpp"
#include "caf/error.hpp"
#include "caf/fwd.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/logger.hpp"
#include "caf/net/datagram_oriented_layer_ptr.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/net/transport_base.hpp"
#include "caf/net/transport_worker_dispatcher.hpp"
#include "caf/net/udp_datagram_socket.hpp"
#include "caf/sec.hpp"
#include "caf/tag/datagram_oriented.hpp"
#include "caf/tag/io_event_oriented.hpp"

namespace caf::net {

/// Implements a udp_transport policy that manages a datagram socket.

template <class Factory>
class datagram_transport {
public:
  // Maximal UDP-packet size
  static constexpr size_t max_datagram_size
    = std::numeric_limits<uint16_t>::max();

  // -- member types -----------------------------------------------------------

  using input_tag = tag::io_event_oriented;

  using output_tag = tag::datagram_oriented;

  using dispatcher_type = transport_worker_dispatcher<Factory, ip_endpoint>;

  using socket_type = udp_datagram_socket;

  // -- constructors, destructors, and assignment operators --------------------

  template <class... Ts>
  explicit datagram_transport(Ts&&... xs)
    : dispatcher_("udp", std::forward<Ts>(xs)...) {
    // nop
  }

  datagram_transport(const datagram_transport&) = delete;

  datagram_transport& operator=(const datagram_transport&) = delete;

  datagram_transport& operator=(datagram_transport&&) = delete;

  datagram_transport(datagram_transport&&) = delete;

  ~datagram_transport() = default;

  // -- interface for stream_oriented_layer_ptr --------------------------------

  template <class ParentPtr>
  bool can_send_more(ParentPtr) const noexcept {
    return datagram_buf_.size() < max_datagram_buf_size_;
  }

  template <class ParentPtr>
  static socket_type handle(ParentPtr parent) noexcept {
    return parent->handle();
  }

  template <class ParentPtr>
  void begin_datagram(ParentPtr parent) {
    if (datagram_buf_.empty())
      parent->register_writing();
    datagram_begin_ = datagram_buf_.size();
  }

  template <class ParentPtr>
  void set_id(ParentPtr, ip_endpoint ep) {
    endpoints_.emplace(ep);
  }

  template <class ParentPtr>
  byte_buffer& datagram_buffer(ParentPtr) {
    return datagram_buf_;
  }

  template <class ParentPtr>
  void end_datagram(ParentPtr) {
    auto datagram_end = datagram_buf_.size();
    datagram_sizes_.emplace(datagram_end - datagram_begin_);
  }

  template <class ParentPtr>
  static void abort_reason(ParentPtr parent, error reason) {
    parent->abort_reason(std::move(reason));
  }

  template <class ParentPtr>
  static const error& abort_reason(ParentPtr parent) {
    return parent->abort_reason();
  }

  template <class ParentPtr>
  void timeout(ParentPtr&, std::string, uint64_t) {
    // nop
  }

  // -- properties -------------------------------------------------------------

  auto& read_buffer() noexcept {
    return read_buf_;
  }

  const auto& read_buffer() const noexcept {
    return read_buf_;
  }

  auto& datagram_buffer() noexcept {
    return datagram_buf_;
  }

  const auto& datagram_buffer() const noexcept {
    return datagram_buf_;
  }

  auto& upper_layer() noexcept {
    return dispatcher_;
  }

  const auto& upper_layer() const noexcept {
    return dispatcher_;
  }

  auto top_layer(node_id nid) {
    return dispatcher_.top_layer(nid);
  }

  auto top_layer(const uri& locator) {
    auto this_layer_ptr = make_datagram_oriented_layer_ptr(this, owner_);
    return dispatcher_.top_layer(this_layer_ptr, locator);
  }

  // -- initialization ---------------------------------------------------------

  template <class ParentPtr>
  error init(socket_manager* owner, ParentPtr parent, const settings& config) {
    CAF_LOG_TRACE("");
    owner_ = owner;
    this_node = owner->mpx().system().node();
    auto default_max_reads
      = static_cast<uint32_t>(defaults::middleman::max_consecutive_reads);
    max_consecutive_reads_ = get_or(
      config, "caf.middleman.max-consecutive-reads", default_max_reads);
    if (auto socket_buf_size = send_buffer_size(parent->handle())) {
      max_datagram_buf_size_ = *socket_buf_size;
      CAF_ASSERT(max_datagram_buf_size_ > 0);
      datagram_buf_.reserve(max_datagram_buf_size_ * 2);
    } else {
      CAF_LOG_ERROR("send_buffer_size: " << socket_buf_size.error());
      return std::move(socket_buf_size.error());
    }
    owner->register_reading();
    auto this_layer_ptr = make_datagram_oriented_layer_ptr(this, parent);
    return dispatcher_.init(owner, this_layer_ptr, config);
  }

  // -- event callbacks --------------------------------------------------------

  template <class ParentPtr>
  bool handle_read_event(ParentPtr parent) {
    CAF_LOG_TRACE(CAF_ARG2("handle", parent->handle().id));
    auto this_layer_ptr = make_datagram_oriented_layer_ptr(this, parent);
    for (size_t reads = 0; reads < max_consecutive_reads_; ++reads) {
      read_buf_.resize(max_datagram_size);
      auto ret = read(parent->handle(), read_buf_);
      if (auto res = get_if<std::pair<size_t, ip_endpoint>>(&ret)) {
        auto& [num_bytes, ep] = *res;
        CAF_LOG_DEBUG("received " << num_bytes << " bytes");
        read_buf_.resize(num_bytes);
        auto consumed = dispatcher_.consume(this_layer_ptr, read_buf_,
                                            std::move(ep));
        if (consumed < 0) {
          dispatcher_.abort(this_layer_ptr,
                            parent->abort_reason_or(caf::sec::runtime_error));
          CAF_LOG_ERROR("consume failed: " << caf::sec::runtime_error);
          return false;
        } else if (static_cast<size_t>(consumed) < read_buf_.size()) {
          CAF_LOG_DEBUG("datagram consumed only partially");
        }
      } else {
        auto err = get<sec>(ret);
        if (err == sec::unavailable_or_would_block) {
          break;
        } else {
          CAF_LOG_DEBUG("read failed" << CAF_ARG(err));
          parent->abort_reason(err);
          dispatcher_.abort(this_layer_ptr, err);
          return false;
        }
      }
    }
    return true;
  }

  template <class ParentPtr>
  bool handle_write_event(ParentPtr parent) {
    CAF_LOG_TRACE(CAF_ARG2("handle", parent->handle().id));
    auto fail = [this, parent](sec reason) {
      CAF_LOG_DEBUG("write failed" << CAF_ARG(reason));
      parent->abort_reason(reason);
      auto this_layer_ptr = make_datagram_oriented_layer_ptr(this, parent);
      dispatcher_.abort(this_layer_ptr, reason);
      return false;
    };
    // Allow the upper layer to add extra data to the write buffer.
    auto this_layer_ptr = make_datagram_oriented_layer_ptr(this, parent);
    if (!dispatcher_.prepare_send(this_layer_ptr)) {
      dispatcher_.abort(this_layer_ptr,
                        parent->abort_reason_or(caf::sec::runtime_error));
      return false;
    }
    while (!datagram_buf_.empty()) {
      auto current_datagram_size = datagram_sizes_.front();
      auto current_endpoint = endpoints_.front();
      auto ret = write(parent->handle(),
                       make_span(datagram_buf_.data(), current_datagram_size),
                       current_endpoint);
      if (auto num_bytes = get_if<size_t>(&ret)) {
        CAF_LOG_DEBUG_IF(*num_bytes
                           < static_cast<size_t>(current_datagram_size),
                         "datagram was written partially");
        datagram_buf_.erase(datagram_buf_.begin(),
                            datagram_buf_.begin() + current_datagram_size);
        datagram_sizes_.pop();
        endpoints_.pop();
      } else {
        return last_socket_error_is_temporary()
                 ? true
                 : fail(sec::socket_operation_failed);
      }
    }
    return !dispatcher_.done_sending(this_layer_ptr);
  }

  template <class ParentPtr>
  void abort(ParentPtr parent, const error& reason) {
    auto this_layer_ptr = make_datagram_oriented_layer_ptr(this, parent);
    dispatcher_.abort(this_layer_ptr, reason);
  }

private:
  /// Holds the dispatching layer.
  dispatcher_type dispatcher_;

  node_id this_node;

  socket_manager* owner_ = nullptr;

  /// Caches the config parameter for limiting max. socket operations.
  size_t max_consecutive_reads_ = 0;

  /// Caches the write buffer size of the socket.
  size_t max_datagram_buf_size_ = 0;

  /// Caches incoming data.
  byte_buffer read_buf_;

  /// Caches outgoing data.
  byte_buffer datagram_buf_;

  /// Caches the sizes of all contained datagrams within the `write_buffer_`.
  std::queue<ptrdiff_t> datagram_sizes_;

  /// Caches the sizes of all contained datagrams within the `write_buffer_`.
  std::queue<ip_endpoint> endpoints_;

  // Caches the write buffer size before adding a datagram to it.
  size_t datagram_begin_ = 0;
};

} // namespace caf::net
