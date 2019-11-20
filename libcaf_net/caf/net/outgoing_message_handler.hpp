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

#include <vector>

#include "caf/actor_control_block.hpp"
#include "caf/actor_proxy.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/config.hpp"
#include "caf/detail/scope_guard.hpp"
#include "caf/detail/sync_request_bouncer.hpp"
#include "caf/execution_unit.hpp"
#include "caf/logger.hpp"
#include "caf/message.hpp"
#include "caf/message_id.hpp"
#include "caf/net/basp/header.hpp"
#include "caf/node_id.hpp"

namespace caf::net {

// TODO: Find better name for this. The whole thing is cloning the
// remote_message_handler design - Maybe find a better name for that too?
template <class Subtype>
class outgoing_message_handler {
public:
  void handle_outgoing_message(execution_unit*) {
    using message_type = endpoint_manager_queue::message;
    auto& elem = this->mailbox_elem_;
    auto content = this->mailbox_elem_->content();
    if (auto payload = sf_(system, content))
      this->manager_.enqueue(new message_type(std::move(elem),
                                              std::move(receiver),
                                              std::move(payload)));
    else
      CAF_LOG_ERROR("unable to serialize payload: "
                    << this->system().render(payload.error()));
  }
};

} // namespace caf::net
