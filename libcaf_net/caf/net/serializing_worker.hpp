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

#include <atomic>
#include <cstdint>
#include <vector>

#include "caf/config.hpp"
#include "caf/detail/abstract_worker.hpp"
#include "caf/detail/worker_hub.hpp"
#include "caf/net/basp/header.hpp"
#include "caf/net/basp/message_queue.hpp"
#include "caf/net/basp/remote_message_handler.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/outgoing_message_handler.hpp"
#include "caf/node_id.hpp"
#include "caf/resumable.hpp"

namespace caf::net {

/// Asynchronously serializes outgoing messages.
class serializing_worker : public detail::abstract_worker,
                           public outgoing_message_handler<serializing_worker> {
public:
  // -- member types -----------------------------------------------------------

  using super = detail::abstract_worker;

  using scheduler_type = scheduler::abstract_coordinator;

  using buffer_type = std::vector<byte>;

  using hub_type = detail::worker_hub<serializing_worker>;

  // -- constructors, destructors, and assignment operators --------------------

  /// Only the ::worker_hub has access to the construtor.
  serializing_worker(hub_type& hub, actor_system& sys);

  ~serializing_worker() override = default;

  // -- management -------------------------------------------------------------

  void launch(mailbox_element_ptr mailbox_elem, actor_control_block* ctrl,
              endpoint_manager* manager);

  // -- implementation of resumable --------------------------------------------

  resume_result resume(execution_unit* ctx, size_t) override;

private:
  // -- constants and assertions -----------------------------------------------

  /// Stores how many bytes the "first half" of this object requires.
  static constexpr size_t pointer_members_size = sizeof(hub_type*)
                                                 + sizeof(proxy_registry*)
                                                 + sizeof(actor_system*);

  static_assert(CAF_CACHE_LINE_SIZE > pointer_members_size,
                "invalid cache line size");

  // -- member variables -------------------------------------------------------

  /// Points to our home hub.
  hub_type* hub_;

  /// Points to the parent system.
  actor_system* system_;

  /// The `mailbox_element` that should be serialized.
  mailbox_element_ptr mailbox_elem_;

  /// Points to the `actor_control_block` of the receiver.
  actor_control_block* ctrl_;

  /// Points to the endpoint_manager that should receive the serialized message.
  endpoint_manager* manager_;
};

} // namespace caf::net
