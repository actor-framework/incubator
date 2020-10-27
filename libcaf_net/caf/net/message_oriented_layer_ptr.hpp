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

#include "caf/error.hpp"
#include "caf/fwd.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/receive_policy.hpp"

namespace caf::net {

/// Wraps a pointer to a message-oriented layer with a pointer to its lower
/// layer. Both pointers are then used to implement the interface required for a
/// message-oriented layer when calling into its upper layer.
template <class Layer, class LowerLayerPtr>
class message_oriented_layer_ptr {
public:
  class access {
  public:
    access(Layer* layer, LowerLayerPtr down) : lptr_(layer), llptr_(down) {
      // nop
    }

    bool can_send_more() const noexcept {
      return lptr_->can_send_more(llptr_);
    }

    void begin_message() {
      lptr_->begin_message(llptr_);
    }

    byte_buffer& message_buffer() {
      return lptr_->message_buffer(llptr_);
    }

    void end_message() {
      lptr_->end_message(llptr_);
    }

    void abort_reason(error reason) {
      return lptr_->abort_reason(llptr_, std::move(reason));
    }

    const error& abort_reason() {
      return lptr_->abort_reason(llptr_);
    }

    void configure_read(receive_policy policy) {
      lptr_->configure_read(llptr_, policy);
    }

    void timeout(std::string type, uint64_t id) {
      lptr_->timeout(llptr_, std::move(type), id);
    }

  private:
    Layer* lptr_;
    LowerLayerPtr llptr_;
  };

  message_oriented_layer_ptr(Layer* layer, LowerLayerPtr down)
    : access_(layer, down) {
    // nop
  }

  message_oriented_layer_ptr(const message_oriented_layer_ptr&) = default;

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
auto make_message_oriented_layer_ptr(Layer* this_layer, LowerLayerPtr down) {
  return message_oriented_layer_ptr<Layer, LowerLayerPtr>{this_layer, down};
}

} // namespace caf::net
