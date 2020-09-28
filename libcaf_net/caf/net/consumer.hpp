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

#include "caf/detail/net_export.hpp"
#include "caf/net/consumer_queue.hpp"

namespace caf::net {

/// An implementation of BASP as an application layer protocol.
class CAF_NET_EXPORT consumer {
public:
  /// Enqueues an event to the mailbox.
  template <class... Ts>
  void enqueue_event(Ts&&... xs) {
    enqueue(new consumer_queue::event(std::forward<Ts>(xs)...));
  }

  virtual void enqueue(mailbox_element_ptr msg, strong_actor_ptr receiver) = 0;

  virtual bool enqueue(consumer_queue::element* ptr) = 0;
};

} // namespace caf::net
