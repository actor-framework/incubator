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

#include "caf/net/endpoint_manager_queue.hpp"

namespace caf::net {

template <class Subtype>
class outgoing_message_handler {
public:
  void handle_outgoing_message() {
    auto& dref = static_cast<Subtype&>(*this);
    auto& content = dref.mailbox_elem_->content();
    if (auto err = dref.sf_(*dref.system_, content, dref.buf_))
      CAF_LOG_ERROR(
        "unable to serialize payload: " << dref.system_->render(err));
    else
      dref.queue_
        ->push(dref.msg_id_,
               new endpoint_manager_queue::message{std::move(
                                                     dref.mailbox_elem_),
                                                   std::move(dref.receiver_),
                                                   std::move(dref.buf_)});
  }
};

} // namespace caf::net
