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

#include "caf/actor_proxy.hpp"
#include "caf/actor_system.hpp"
#include "caf/expected.hpp"
#include "caf/logger.hpp"

namespace caf::net {

/// Implements a simple proxy forwarding timeouts to a manager.
template <class Destination>
class timeout_proxy : public actor_proxy {
public:
  using super = actor_proxy;

  timeout_proxy(actor_config& cfg, Destination& dst) : super(cfg), dst_(dst) {
    dst_.enqueue_event(node(), id());
  }

  ~timeout_proxy() override{
    // nop
  };

  void enqueue(mailbox_element_ptr msg, execution_unit*) override {
    CAF_PUSH_AID(0);
    CAF_ASSERT(msg != nullptr);
    if (msg->content().match_elements<timeout_msg>()) {
      auto tout = msg->content().get_as<timeout_msg>(0);
      dst_.enqueue_event(tout.type, tout.timeout_id);
    } else {
      CAF_LOG_ERROR("timeout_proxy received wrong message");
    }
  }

  void kill_proxy(execution_unit* ctx, error rsn) override {
    cleanup(std::move(rsn), ctx);
  }

private:
  Destination& dst_;
};

} // namespace caf::net
