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

#include "caf/net/basp/actor_proxy_impl.hpp"

#include "caf/mailbox_element.hpp"
#include "caf/net/basp/msg.hpp"
#include "caf/send.hpp"

namespace caf::net::basp {

actor_proxy_impl::actor_proxy_impl(actor_config& cfg, actor app)
  : super(cfg), app_(std::move(app)) {
  CAF_ASSERT(app != nullptr);
  anon_send(app, new_proxy_msg{id()});
}

actor_proxy_impl::~actor_proxy_impl() {
  // nop
}

void actor_proxy_impl::enqueue(mailbox_element_ptr msg, execution_unit* eu) {
  msg->stages.push_back(ctrl());
  app_->enqueue(std::move(msg), eu);
}

void actor_proxy_impl::kill_proxy(execution_unit* ctx, error rsn) {
  cleanup(std::move(rsn), ctx);
}

} // namespace caf::net::basp
