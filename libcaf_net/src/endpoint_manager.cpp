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

#include "caf/net/endpoint_manager.hpp"

#include "caf/byte.hpp"
#include "caf/intrusive/inbox_result.hpp"
#include "caf/net/multiplexer.hpp"
#include "caf/sec.hpp"
#include "caf/send.hpp"

namespace caf::net {

endpoint_manager::endpoint_manager(socket handle, const multiplexer_ptr& parent,
                                   actor_system& sys, hub_type& hub)
  : super(handle, parent), sys_(sys), queue_(unit, unit, unit), hub_(hub) {
  queue_.try_block();
}

endpoint_manager_queue::message_ptr endpoint_manager::next_message() {
  if (queue_.blocked())
    return nullptr;
  queue_.fetch_more();
  auto& q = std::get<1>(queue_.queue().queues());
  auto ts = q.next_task_size();
  if (ts == 0)
    return nullptr;
  q.inc_deficit(ts);
  auto result = q.next();
  if (queue_.empty())
    queue_.try_block();
  return result;
}

void endpoint_manager::resolve(uri locator, const actor& listener) {
  using intrusive::inbox_result;
  using event_type = endpoint_manager_queue::event;
  auto ptr = new event_type(std::move(locator), listener);
  if (!enqueue(ptr))
    anon_send(listener, resolve_atom::value,
              make_error(sec::request_receiver_down));
}

void endpoint_manager::enqueue(mailbox_element_ptr msg,
                               actor_control_block* receiver) {
  auto worker = hub_.pop();
  if (worker != nullptr) {
    CAF_LOG_DEBUG("launch serializing worker for serializing an actor_message");
    worker->launch(std::move(msg), receiver->get()->ctrl(), message_queue_,
                   serialize_fun(), next_payload_buffer());
  } else {
    CAF_LOG_DEBUG(
      "out of serializing workers, continue serializing an actor_message");
    // If no worker is available then we have no other choice than to take
    // the performance hit and serialize in this thread.
    struct handler : public outgoing_message_handler<handler> {
      handler(outgoing_message_queue& queue, hub_type& hub, actor_system& sys,
              mailbox_element_ptr mailbox_elem, strong_actor_ptr receiver,
              endpoint_manager::serialize_fun_type sf, std::vector<byte> buf)
        : queue_(&queue),
          hub_(&hub),
          system_(&sys),
          mailbox_elem_(std::move(mailbox_elem)),
          receiver_(std::move(receiver)),
          sf_(sf),
          buf_(std::move(buf)) {
        msg_id_ = queue_->new_id();
      }
      outgoing_message_queue* queue_;
      hub_type* hub_;
      actor_system* system_;
      mailbox_element_ptr mailbox_elem_;
      strong_actor_ptr receiver_;
      serialize_fun_type sf_;
      uint64_t msg_id_;
      std::vector<byte> buf_;
    };
    handler f{message_queue_,
              hub_,
              system(),
              std::move(msg),
              receiver->get()->ctrl(),
              serialize_fun(),
              next_payload_buffer()};
    f.handle_outgoing_message();
  }
}

bool endpoint_manager::enqueue(endpoint_manager_queue::element* ptr) {
  switch (queue_.push_back(ptr)) {
    case intrusive::inbox_result::success:
      return true;
    case intrusive::inbox_result::unblocked_reader: {
      auto mpx = parent_.lock();
      if (mpx) {
        mpx->register_writing(this);
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

} // namespace caf::net
