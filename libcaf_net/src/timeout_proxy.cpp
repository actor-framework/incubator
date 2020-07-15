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

#include "caf/net/timeout_proxy.hpp"

#include "caf/actor_system.hpp"
#include "caf/expected.hpp"
#include "caf/logger.hpp"

namespace caf::net {

timeout_proxy::timeout_proxy(actor_config& cfg, endpoint_manager_ptr dst)
  : super(cfg), dst_(std::move(dst)) {
  CAF_ASSERT(dst_ != nullptr);
  dst_->enqueue_event(node(), id());
}

timeout_proxy::~timeout_proxy() {
  // nop
}

void timeout_proxy::enqueue(mailbox_element_ptr msg, execution_unit*) {
  CAF_PUSH_AID(0);
  CAF_ASSERT(msg != nullptr);
  if (msg->content().match_elements<timeout_msg>()) {
    auto tout = msg->content().get_as<timeout_msg>(0);
    dst_->enqueue_event(tout.type, tout.timeout_id);
  } else {
    CAF_LOG_ERROR("timeout_proxy received wrong message");
  }
}

void timeout_proxy::kill_proxy(execution_unit* ctx, error rsn) {
  cleanup(std::move(rsn), ctx);
}

} // namespace caf::net
