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

#include "caf/actor_config.hpp"
#include "caf/actor_proxy.hpp"
#include "caf/mailbox_element.hpp"
#include "caf/net/endpoint_manager.hpp"

namespace caf::net {

/// Implements a simple proxy forwarding timeouts to a manager.
class timeout_proxy : public actor_proxy {
public:
  using super = actor_proxy;

  timeout_proxy(actor_config& cfg, endpoint_manager_ptr dst);

  ~timeout_proxy() override;

  void enqueue(mailbox_element_ptr msg, execution_unit*) override;

  void kill_proxy(execution_unit*, error) override;

private:
  endpoint_manager_ptr dst_;
};

} // namespace caf::net
