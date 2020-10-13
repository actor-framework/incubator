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

#include <deque>
#include <limits>

#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/logger.hpp"
#include "caf/net/defaults.hpp"
#include "caf/net/message_oriented_layer_ptr.hpp"
#include "caf/net/receive_policy.hpp"
#include "caf/net/slicing_header.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/span.hpp"
#include "caf/tag/datagram_oriented.hpp"
#include "caf/tag/message_oriented.hpp"

namespace caf::net {

/// Implements an application protocol that slices and reassembles messages to
/// fit into datagrams.
template <class UpperLayer>
class slicing {
public:
  using input_tag = tag::datagram_oriented;

  using output_tag = tag::message_oriented;

  static constexpr size_t ip_header_size = 20;

  static constexpr size_t udp_header_size = 8;

  static constexpr size_t max_datagram_size
    = std::numeric_limits<uint16_t>::max() - ip_header_size - udp_header_size;

  static constexpr size_t fragment_size = 1500;

  static constexpr size_t max_payload_size = fragment_size
                                             - slicing_header_size;

  using fragment_queue_type = std::deque<byte_buffer>;

  // -- constructors, destructors, and assignment operators --------------------

  template <class... Ts>
  slicing(Ts&&... xs) : upper_layer_(std::forward<Ts>(xs)...) {
    // nop
  }

  ~slicing() = default;

  // -- interface functions ----------------------------------------------------

  template <class LowerLayerPtr>
  error
  init(socket_manager* owner, LowerLayerPtr down, const settings& config) {
    CAF_LOG_TRACE("");
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, down);
    return upper_layer_.init(owner, this_layer_ptr, config);
  }

  // -- interface for the upper layer ------------------------------------------

  template <class LowerLayerPtr>
  void begin_message(LowerLayerPtr down) {
    CAF_LOG_TRACE("");
    down->begin_datagram();
  }

  template <class LowerLayerPtr>
  byte_buffer& message_buffer(LowerLayerPtr) {
    return output_buffer_;
  }

  template <class LowerLayerPtr>
  void end_message(LowerLayerPtr down) {
    CAF_LOG_TRACE("");
    if (output_buffer_.size() <= max_payload_size) {
      // Simply send the message
      auto& buf = down->datagram_buffer();
      binary_serializer sink(nullptr, buf);
      if (!sink.apply_object(slicing_header{1, 1})) {
        CAF_LOG_ERROR("could not serialize header"
                      << CAF_ARG(sink.get_error()));
        down->abort_reason(sink.get_error());
      }
      buf.insert(buf.end(), output_buffer_.begin(), output_buffer_.end());
      down->end_datagram();
    } else {
      // Message is too large, thus it has to be sliced.
      slice(down);
    }
    output_buffer_.clear();
  }

  template <class LowerLayerPtr>
  bool can_send_more(LowerLayerPtr down) const noexcept {
    return down->can_send_more();
  }

  template <class LowerLayerPtr>
  void abort_reason(LowerLayerPtr down, error reason) {
    return down->abort_reason(std::move(reason));
  }

  template <class LowerLayerPtr>
  void configure_read(LowerLayerPtr, receive_policy) {
    // nop
  }

  template <class LowerLayerPtr>
  void timeout(LowerLayerPtr down, std::string type, uint64_t id) {
    down->timeout(std::move(type), id);
  }

  // -- properties -------------------------------------------------------------

  auto& upper_layer() noexcept {
    return upper_layer_;
  }

  const auto& upper_layer() const noexcept {
    return upper_layer_;
  }

  // -- role: upper layer ------------------------------------------------------

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr down) {
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, down);
    return upper_layer_.prepare_send(this_layer_ptr);
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr down) {
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, down);
    return upper_layer_.done_sending(this_layer_ptr);
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr down, const error& reason) {
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, down);
    upper_layer_.abort(this_layer_ptr, reason);
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume(LowerLayerPtr down, const_byte_span buffer) {
    CAF_LOG_TRACE(CAF_ARG2("buffer.size", buffer.size()));
    if (buffer.size() < slicing_header_size) {
      down->abort_reason(
        make_error(sec::unexpected_message, "did not receive enough bytes"));
      return -1;
    }
    slicing_header header;
    binary_deserializer source(nullptr, buffer);
    if (!source.apply_object(header)) {
      down->abort_reason(source.get_error());
      return -1;
    }
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, down);
    if (header.of == 1) {
      return upper_layer_.consume(this_layer_ptr,
                                  buffer.subspan(slicing_header_size));
    } else {
      receive_buf_.insert(receive_buf_.end(),
                          buffer.begin() + slicing_header_size, buffer.end());
      if (header.no == header.of) {
        [[maybe_unused]] auto consumed = upper_layer_.consume(down,
                                                              receive_buf_);
        CAF_ASSERT(consumed == receive_buf_.size());
        receive_buf_.clear();
      }
    }
    return buffer.size();
  }

private:
  template <class LowerLayerPtr>
  void slice(LowerLayerPtr down) {
    auto calc_num_slices = [&]() {
      auto num_slices = output_buffer_.size() / max_payload_size;
      if ((num_slices * max_payload_size) != output_buffer_.size())
        return num_slices + 1;
      else
        return num_slices;
    };
    auto output_span = make_span(output_buffer_);
    auto num_slices = calc_num_slices();
    auto& buf = down->datagram_buffer();
    for (size_t slice = 1; slice <= num_slices; ++slice) {
      if (slice != 1)
        down->begin_datagram();
      binary_serializer sink{nullptr, buf};
      if (!sink.apply_object(slicing_header{slice, num_slices})) {
        CAF_LOG_ERROR(
          "could not serialize header: " << CAF_ARG(sink.get_error()));
        down->abort_reason(sink.get_error());
        return;
      }
      auto slice_size = std::min(max_payload_size, output_span.size());
      buf.insert(buf.end(), output_span.begin(),
                 output_span.begin() + slice_size);
      down->end_datagram();
      output_span = output_span.subspan(slice_size);
    }
    output_buffer_.clear();
  }

  /// Holds the upper layer.
  UpperLayer upper_layer_;

  byte_buffer output_buffer_;

  byte_buffer receive_buf_;

  static constexpr string_view tag_ = "slicing";
};

} // namespace caf::net
