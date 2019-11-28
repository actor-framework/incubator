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

#include "caf/net/serializing_worker.hpp"

#include "caf/actor_system.hpp"
#include "caf/net/basp/message_queue.hpp"
#include "caf/proxy_registry.hpp"

namespace caf::net {

// -- constructors, destructors, and assignment operators ----------------------

serializing_worker::serializing_worker(hub_type& hub, actor_system& sys,
                                       outgoing_message_queue& queue,
                                       serialize_fun_type sf)
  : msg_id_(0), hub_(&hub), system_(&sys), queue_(&queue), sf_(sf) {
  // nop
}

// -- management ---------------------------------------------------------------

void serializing_worker::launch(mailbox_element_ptr mailbox_elem,
                                strong_actor_ptr ctrl) {
  msg_id_ = queue_->new_id();
  mailbox_elem_ = std::move(mailbox_elem);
  receiver_ = std::move(ctrl);
  ref();
  system_->scheduler().enqueue(this);
}

// -- implementation of resumable ----------------------------------------------

resumable::resume_result serializing_worker::resume(execution_unit*, size_t) {
  this->handle_outgoing_message();
  hub_->push(this);
  return resumable::awaiting_message;
}

} // namespace caf::net
