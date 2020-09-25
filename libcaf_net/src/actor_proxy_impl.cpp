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

#include "caf/net/actor_proxy_impl.hpp"

#include "caf/actor_system.hpp"
#include "caf/expected.hpp"
#include "caf/logger.hpp"
#include "caf/net/multiplexer.hpp"

namespace caf::net {

actor_proxy_impl::actor_proxy_impl(actor_config& cfg, socket_manager* mgr,
                                   consumer_queue::type& mailbox)
  : super(cfg), mgr_(mgr), mailbox_(mailbox) {
  CAF_ASSERT(mgr_ != nullptr);
  enqueue_event(id());
}

actor_proxy_impl::~actor_proxy_impl() {
  // nop
}

void actor_proxy_impl::enqueue(mailbox_element_ptr msg, execution_unit*) {
  CAF_PUSH_AID(0);
  CAF_ASSERT(msg != nullptr);
  CAF_LOG_SEND_EVENT(msg);
  using message_type = consumer_queue::message;
  auto ptr = new message_type(std::move(msg), ctrl());
  enqueue_impl(ptr);
}

void actor_proxy_impl::kill_proxy(execution_unit* ctx, error rsn) {
  cleanup(std::move(rsn), ctx);
}

void actor_proxy_impl::enqueue_impl(consumer_queue::element* ptr) {
  switch (mailbox_.push_back(ptr)) {
    case intrusive::inbox_result::success:
      break;
    case intrusive::inbox_result::unblocked_reader:
      mgr_->register_writing();
    default:
      break;
  }
}

} // namespace caf::net
