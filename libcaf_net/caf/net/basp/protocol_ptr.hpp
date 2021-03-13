// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#pragma once

#include "caf/error.hpp"
#include "caf/fwd.hpp"
#include "caf/net/fwd.hpp"

namespace caf::net::basp {

/// Wraps a pointer to a mixed-message-oriented layer with a pointer to its
/// lower layer. Both pointers are then used to implement the interface required
/// for a mixed-message-oriented layer when calling into its upper layer.
template <class Layer, class LowerLayerPtr>
class protocol_ptr {
public:
  class access {
  public:
    access(Layer* layer, LowerLayerPtr down) : lptr_(layer), llptr_(down) {
      // nop
    }

    bool can_send_more() const noexcept {
      return lptr_->can_send_more(llptr_);
    }

    void abort_reason(error reason) {
      return lptr_->abort_reason(llptr_, std::move(reason));
    }

    const error& abort_reason() {
      return lptr_->abort_reason(llptr_);
    }

    [[nodiscard]] bool send_heartbeat() {
      return lptr_->send_heartbeat(llptr_);
    }

    [[nodiscard]] bool send_ping(const_byte_span payload) {
      return lptr_->send_ping(llptr_, payload);
    }

  private:
    Layer* lptr_;
    LowerLayerPtr llptr_;
  };

  protocol_ptr(Layer* layer, LowerLayerPtr down) : access_(layer, down) {
    // nop
  }

  protocol_ptr(const protocol_ptr&) = default;

  explicit operator bool() const noexcept {
    return true;
  }

  access* operator->() const noexcept {
    return &access_;
  }

  access& operator*() const noexcept {
    return access_;
  }

private:
  mutable access access_;
};

template <class Layer, class LowerLayerPtr>
auto make_protocol_ptr(Layer* this_layer, LowerLayerPtr down) {
  using result_t = protocol_ptr<Layer, LowerLayerPtr>;
  return result_t{this_layer, down};
}

} // namespace caf::net::basp
