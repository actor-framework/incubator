// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#pragma once

#include "caf/error.hpp"
#include "caf/net/basp/message_type.hpp"
#include "caf/net/basp/protocol_ptr.hpp"
#include "caf/net/basp/tag.hpp"
#include "caf/net/fwd.hpp"
#include "caf/sec.hpp"
#include "caf/tag/message_oriented.hpp"

namespace caf::net::basp {

/// Implements serialization and deserialization of BASP messages.
template <class UpperLayer>
class protocol {
public:
  // -- member types -----------------------------------------------------------

  using input_tag = caf::tag::message_oriented;

  using output_tag = tag;

  // -- constructors, destructors, and assignment operators --------------------

  template <class... Ts>
  explicit protocol(Ts&&... xs) : upper_layer_(std::forward<Ts>(xs)...) {
    // nop
  }

  // -- initialization ---------------------------------------------------------

  template <class LowerLayerPtr>
  error init(socket_manager* owner, LowerLayerPtr down, const settings& cfg) {
    return upper_layer_.init(owner, this_layer_ptr(down), cfg);
  }

  // -- properties -------------------------------------------------------------

  auto& upper_layer() noexcept {
    return upper_layer_;
  }

  const auto& upper_layer() const noexcept {
    return upper_layer_;
  }

  // -- interface for protocol_ptr ---------------------------------------

  template <class LowerLayerPtr>
  static bool can_send_more(LowerLayerPtr down) noexcept {
    return down->can_send_more();
  }

  template <class LowerLayerPtr>
  static void abort_reason(LowerLayerPtr down, error reason) {
    return down->abort_reason(std::move(reason));
  }

  template <class LowerLayerPtr>
  static const error& abort_reason(LowerLayerPtr down) {
    return down->abort_reason();
  }

  template <class LowerLayerPtr>
  bool send_heartbeat(LowerLayerPtr down) {
    return send_message(down, [](byte_buffer& buf) {
      buf.push_back(to_byte(message_type::heartbeat));
    });
  }

  template <class LowerLayerPtr>
  bool send_ping(LowerLayerPtr down, const_byte_span payload) {
    return send_message(down, [payload](byte_buffer& buf) {
      buf.push_back(to_byte(message_type::ping));
      buf.insert(buf.end(), payload.begin(), payload.end());
    });
  }

  // -- interface for the lower layer ------------------------------------------

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr down) {
    return upper_layer_.prepare_send(down);
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr down) {
    return upper_layer_.done_sending(down);
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr down, const error& reason) {
    upper_layer_.abort(down, reason);
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume(LowerLayerPtr down, byte_span msg) {
    if (msg.empty()) {
      return 0;
    } else {
      switch (static_cast<message_type>(static_cast<uint8_t>(msg[0]))) {
        case message_type::heartbeat:
          if (upper_layer_.heartbeat())
            return static_cast<ptrdiff_t>(msg.size());
          else
            return -1;
        case message_type::ping:
          if (handle_ping(down, msg.subspan(1)))
            return static_cast<ptrdiff_t>(msg.size());
          else
            return -1;
        case message_type::pong:
          if (upper_layer_.pong(msg.subspan(1)))
            return static_cast<ptrdiff_t>(msg.size());
          else
            return -1;
        // TODO: implement remaining types
        default:
          down->abort_reason(
            make_error(sec::runtime_error, "invalid BASP message type"));
          return -1;
      }
    }
  }

private:
  // -- implementation details -------------------------------------------------

  template <class LowerLayerPtr>
  auto this_layer_ptr(LowerLayerPtr down) {
    return make_protocol_ptr(this, down);
  }

  template <class LowerLayerPtr, class F>
  bool send_message(LowerLayerPtr down, F f) {
    down->begin_message();
    f(down->message_buffer());
    return down->end_message();
  }

  template <class LowerLayerPtr>
  bool handle_ping(LowerLayerPtr down, byte_span payload) {
    return send_message(down, [payload](byte_buffer& buf) {
      buf.push_back(to_byte(message_type::pong));
      buf.insert(buf.end(), payload.begin(), payload.end());
    });
  }

  // -- member variables -------------------------------------------------------

  UpperLayer upper_layer_;
};

} // namespace caf::net::basp
