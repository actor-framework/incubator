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
#include "caf/net/fwd.hpp"
#include "caf/unit.hpp"

namespace caf::net {

/// Implements a worker for transport protocols.
template <class UpperLayer, class IdType>
class transport_worker {
public:
  // -- constructors, destructors, and assignment operators --------------------

  transport_worker(UpperLayer upper_layer, IdType id)
    : upper_layer_(std::move(upper_layer)), id_(std::move(id)) {
    // nop
  }

  ~transport_worker() = default;

  // -- initialization ---------------------------------------------------------

  template <class LowerLayerPtr>
  error
  init(socket_manager* owner, LowerLayerPtr down, const settings& config) {
    CAF_LOG_TRACE("");
    return upper_layer_.init(owner, down, config);
  }

  // -- properties -------------------------------------------------------------

  auto& upper_layer() noexcept {
    return upper_layer_;
  }

  const auto& upper_layer() const noexcept {
    return upper_layer_;
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
  ptrdiff_t
  consume(LowerLayerPtr down, const_byte_span buffer, const_byte_span delta) {
    return upper_layer_.consume(down, buffer, delta);
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
