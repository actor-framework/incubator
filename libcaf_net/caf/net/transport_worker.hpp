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

#include "caf/logger.hpp"
#include "caf/net/datagram_oriented_layer_ptr.hpp"
#include "caf/net/fwd.hpp"
#include "caf/unit.hpp"

namespace caf::net {

/// Implements a worker for transport protocols.
template <class UpperLayer, class IdType>
class transport_worker {
public:
  // -- constructors, destructors, and assignment operators --------------------

  template <class... Ts>
  transport_worker(IdType id, Ts&&... xs)
    : upper_layer_(std::forward<Ts>(xs)...), id_(std::move(id)) {
    // nop
  }

  ~transport_worker() = default;

  // -- initialization ---------------------------------------------------------

  template <class LowerLayerPtr>
  error
  init(socket_manager* owner, LowerLayerPtr down, const settings& config) {
    CAF_LOG_TRACE("");
    auto this_layer_ptr = make_datagram_oriented_layer_ptr(this, down);
    return upper_layer_.init(owner, this_layer_ptr, config);
  }

  // -- interface for the upper layer ------------------------------------------

  template <class LowerLayerPtr>
  void begin_datagram(LowerLayerPtr down) {
    down->begin_datagram(id_);
  }

  template <class LowerLayerPtr>
  byte_buffer& datagram_buffer(LowerLayerPtr down) {
    return down->datagram_buffer();
  }

  template <class LowerLayerPtr>
  void end_datagram(LowerLayerPtr down) {
    down->end_datagram();
  }

  template <class LowerLayerPtr>
  bool can_send_more(LowerLayerPtr down) const noexcept {
    return down->can_send_more();
  }

  template <class LowerLayerPtr>
  void abort_reason(LowerLayerPtr down, error reason) {
    down->abort_reason(std::move(reason));
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

  auto top_layer() {
    return upper_layer_.top_layer();
  }

  // -- interface for the lower layer ------------------------------------------

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr down) {
    auto this_layer_ptr = make_datagram_oriented_layer_ptr(this, down);
    return upper_layer_.prepare_send(this_layer_ptr);
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr down) {
    auto this_layer_ptr = make_datagram_oriented_layer_ptr(this, down);
    return upper_layer_.done_sending(this_layer_ptr);
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr down, const error& reason) {
    auto this_layer_ptr = make_datagram_oriented_layer_ptr(this, down);
    upper_layer_.abort(this_layer_ptr, reason);
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume(LowerLayerPtr down, const_byte_span buffer) {
    auto this_layer_ptr = make_datagram_oriented_layer_ptr(this, down);
    return upper_layer_.consume(this_layer_ptr, buffer);
  }

private:
  /// Holds the upper layer.
  UpperLayer upper_layer_;

  /// Holds the id of this worker.
  IdType id_;
};

template <class Application, class IdType = unit_t>
using transport_worker_ptr
  = std::shared_ptr<transport_worker<Application, IdType>>;

} // namespace caf::net
