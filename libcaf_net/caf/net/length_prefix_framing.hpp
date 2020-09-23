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

#include <cstdint>
#include <cstring>
#include <memory>

#include "caf/byte.hpp"
#include "caf/byte_span.hpp"
#include "caf/detail/network_order.hpp"
#include "caf/error.hpp"
#include "caf/net/message_oriented_layer_ptr.hpp"
#include "caf/net/receive_policy.hpp"
#include "caf/net/socket_manager.hpp"
#include "caf/sec.hpp"
#include "caf/span.hpp"
#include "caf/tag/message_oriented.hpp"
#include "caf/tag/stream_oriented.hpp"

namespace {
enum length_prefix_state {
  await_header,
  await_payload,
};
}

namespace caf::net {

/// Length-prefixed message framing for discretizing a Byte stream into messages
/// of varying size. The framing uses 4 Bytes for the length prefix, but
/// messages (including the 4 Bytes for the length prefix) are limited to a
/// maximum size of INT32_MAX. This limitation comes from the POSIX API (recv)
/// on 32-bit platforms.
template <class UpperLayer>
class length_prefix_framing {
public:
  using input_tag = tag::stream_oriented;

  using output_tag = tag::message_oriented;

  using length_prefix_type = uint32_t;

  static constexpr size_t header_length = sizeof(length_prefix_type);

  static constexpr size_t max_message_length = INT32_MAX;

  // -- constructors, destructors, and assignment operators --------------------

  template <class... Ts>
  length_prefix_framing(Ts&&... xs)
    : upper_layer_(std::forward<Ts>(xs)...),
      message_offset_(0),
      state_(length_prefix_state::await_header),
      msg_size_(0) {
    // nop
  }

  virtual ~length_prefix_framing() {
    // nop
  }

  // -- initialization ---------------------------------------------------------

  template <class LowerLayerPtr>
  error
  init(socket_manager* owner, LowerLayerPtr& down, const settings& config) {
    down->configure_read(receive_policy::exactly(header_length));
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, down);
    return upper_layer_.init(owner, this_layer_ptr, config);
  }

  // -- interface for the upper layer ------------------------------------------

  template <class LowerLayerPtr>
  void begin_message(LowerLayerPtr& down) {
    down->begin_output();
    auto& buf = down->output_buffer();
    message_offset_ = buf.size();
    buf.insert(buf.end(), 4, byte{0});
  }

  template <class LowerLayerPtr>
  byte_buffer& message_buffer(LowerLayerPtr& down) {
    return down->output_buffer();
  }

  template <class LowerLayerPtr>
  void end_message(LowerLayerPtr& down) {
    using detail::to_network_order;
    auto& buf = down->output_buffer();
    auto msg_begin = buf.begin() + message_offset_;
    auto msg_size = std::distance(msg_begin + header_length, buf.end());
    if (msg_size > 0 && static_cast<size_t>(msg_size) < max_message_length) {
      auto u32_size = to_network_order(static_cast<uint32_t>(msg_size));
      memcpy(std::addressof(*msg_begin), &u32_size, 4);
      down->end_output();
    } else {
      down->abort_reason(make_error(
        sec::runtime_error, msg_size == 0 ? "logic error: message of size 0"
                                          : "maximum message size exceeded"));
    }
  }

  template <class LowerLayerPtr>
  bool can_send_more(LowerLayerPtr& down) const noexcept {
    return down->can_send_more();
  }

  template <class LowerLayerPtr>
  void abort_reason(LowerLayerPtr& down, error reason) {
    return down->abort_reason(std::move(reason));
  }

  template <class LowerLayerPtr>
  void configure_read(LowerLayerPtr&, receive_policy) {
    // nop
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
  bool prepare_send(LowerLayerPtr& down) {
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, down);
    return upper_layer_.prepare_send(this_layer_ptr);
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr& down) {
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, down);
    return upper_layer_.done_sending(this_layer_ptr);
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr& down, const error& reason) {
    auto this_layer_ptr = make_message_oriented_layer_ptr(this, down);
    upper_layer_.abort(this_layer_ptr, reason);
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume(LowerLayerPtr& down, byte_span buffer, byte_span) {
    switch (state_) {
      case await_header: {
        using detail::from_network_order;
        if (buffer.size() < header_length)
          return 0;
        uint32_t u32_size = 0;
        memcpy(&u32_size, buffer.data(), header_length);
        msg_size_ = static_cast<size_t>(from_network_order(u32_size));
        down->configure_read(receive_policy::exactly(msg_size_));
        state_ = await_payload;
        return header_length;
      }
      case await_payload: {
        if (buffer.size() < msg_size_)
          return 0;
        auto this_layer_ptr = make_message_oriented_layer_ptr(this, down);
        upper_layer_.consume(this_layer_ptr,
                             make_span(buffer.data(), msg_size_));
        down->configure_read(receive_policy::exactly(header_length));
        state_ = await_header;
        return msg_size_;
      }
    }
  }

private:
  UpperLayer upper_layer_;
  size_t message_offset_;
  length_prefix_state state_;
  size_t msg_size_;
};

} // namespace caf::net
