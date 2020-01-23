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

#include <cstdint>
#include <mutex>
#include <vector>

#include "caf/actor_control_block.hpp"
#include "caf/detail/net_export.hpp"
#include "caf/fwd.hpp"
#include "caf/mailbox_element.hpp"
#include "caf/net/endpoint_manager_queue.hpp"
#include "caf/net/fwd.hpp"

namespace caf::net {

/// Enforces strict order of message delivery to a single endpoint_manager
/// i.e., deliver messages in the same order as if they were deserialized by a
/// single thread.
class CAF_NET_EXPORT outgoing_message_queue {
public:
  // -- member types -----------------------------------------------------------

  /// Request for sending a message to an actor at a later time.
  struct serialized_message {
    uint64_t id;
    endpoint_manager_queue::element* content;
  };

  // -- constructors, destructors, and assignment operators --------------------

  explicit outgoing_message_queue();

  // -- mutators ---------------------------------------------------------------

  void init(endpoint_manager* manager);

  /// Adds a new message to the queue or deliver it immediately if possible.
  void push(uint64_t id, endpoint_manager_queue::element* ptr);

  /// Marks given ID as dropped, effectively skipping it without effect.
  void drop(uint64_t id);

  /// Returns the next ascending ID.
  uint64_t new_id();

private:
  // -- member variables -------------------------------------------------------

  /// Protects all other properties.
  std::mutex lock;

  /// The next available ascending ID. The counter is large enough to overflow
  /// after roughly 600 years if we dispatch a message every microsecond.
  uint64_t next_id;

  /// The next ID that we can ship.
  uint64_t next_undelivered;

  /// Keeps messages in sorted order in case a message other than
  /// `next_undelivered` gets ready first.
  std::vector<serialized_message> pending;

  /// The `endpoint_manager` the ordered serialized messages should be
  /// delivered to.
  endpoint_manager* manager_;
};

} // namespace caf::net
