/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2018 Dominik Charousset                                     *
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

#include "caf/byte.hpp"
#include "caf/error.hpp"
#include "caf/expected.hpp"
#include "caf/fwd.hpp"
#include "caf/logger.hpp"
#include "caf/net/endpoint_manager.hpp"
#include "caf/net/receive_policy.hpp"
#include "caf/net/stream_socket.hpp"
#include "caf/net/transport_worker.hpp"
#include "caf/sec.hpp"
#include "caf/span.hpp"
#include "caf/variant.hpp"

namespace caf {
namespace net {

/// Implements a stream_transport that manages a stream socket.
template <class Application>
class stream_transport {
public:
  // -- member types -----------------------------------------------------------

  using application_type = Application;

  using transport_type = stream_transport;

  using worker_type = transport_worker<application_type>;

  // -- constructors, destructors, and assignment operators --------------------

  stream_transport(stream_socket handle, application_type application)
    : worker_(std::move(application)),
      handle_(handle),
      // max_consecutive_reads_(0),
      read_threshold_(1024),
      collected_(0),
      max_(1024),
      rd_flag_(net::receive_policy_flag::exactly),
      manager_(nullptr) {
    // nop
  }

  // -- properties -------------------------------------------------------------

  stream_socket handle() const noexcept {
    return handle_;
  }

  actor_system& system() {
    return manager().system();
  }

  application_type& application() {
    return worker_.application();
  }

  transport_type& transport() {
    return *this;
  }

  endpoint_manager& manager() {
    return *manager_;
  }

  // -- member functions -------------------------------------------------------

  template <class Parent>
  error init(Parent& parent) {
    manager_ = &parent;
    if (auto err = worker_.init(*this))
      return err;
    parent.mask_add(operation::read);
    return none;
  }

  template <class Parent>
  bool handle_read_event(Parent&) {
    auto buf = read_buf_.data() + collected_;
    size_t len = read_threshold_ - collected_;
    CAF_LOG_TRACE(CAF_ARG(handle_.id) << CAF_ARG(len));
    auto ret = read(handle_, make_span(buf, len));
    // Update state.
    if (auto num_bytes = get_if<size_t>(&ret)) {
      CAF_LOG_DEBUG(CAF_ARG(len) << CAF_ARG(handle_.id) << CAF_ARG(*num_bytes));
      collected_ += *num_bytes;
      if (collected_ >= read_threshold_) {
        worker_.handle_data(*this, read_buf_);
        prepare_next_read();
      }
      return true;
    } else {
      auto err = get<sec>(ret);
      CAF_LOG_DEBUG("receive failed" << CAF_ARG(err));
      worker_.handle_error(err);
      return false;
    }
  }

  template <class Parent>
  bool handle_write_event(Parent& parent) {
    // Try to write leftover data.
    write_some();
    // Get new data from parent.
    // TODO: dont read all messages at once - get one by one.
    for (auto msg = parent.next_message(); msg != nullptr;
         msg = parent.next_message()) {
      worker_.write_message(*this, std::move(msg));
    }
    // Write prepared data.
    return write_some();
  }

  template <class Parent>
  void resolve(Parent&, const std::string& path, actor listener) {
    worker_.resolve(*this, path, listener);
  }

  template <class... Ts>
  void set_timeout(uint64_t, Ts&&...) {
    // nop
  }

  template <class Parent>
  void timeout(Parent&, atom_value value, uint64_t id) {
    worker_.timeout(*this, value, id);
  }

  void handle_error(sec code) {
    worker_.handle_error(code);
  }

  void prepare_next_read() {
    read_buf_.clear();
    collected_ = 0;
    // This cast does nothing, but prevents a weird compiler error on GCC
    // <= 4.9.
    // TODO: remove cast when dropping support for GCC 4.9.
    switch (static_cast<net::receive_policy_flag>(rd_flag_)) {
      case net::receive_policy_flag::exactly:
        if (read_buf_.size() != max_)
          read_buf_.resize(max_);
        read_threshold_ = max_;
        break;
      case net::receive_policy_flag::at_most:
        if (read_buf_.size() != max_)
          read_buf_.resize(max_);
        read_threshold_ = 1;
        break;
      case net::receive_policy_flag::at_least: {
        // read up to 10% more, but at least allow 100 bytes more
        auto max_size = max_ + std::max<size_t>(100, max_ / 10);
        if (read_buf_.size() != max_size)
          read_buf_.resize(max_size);
        read_threshold_ = max_;
        break;
      }
    }
  }

  void configure_read(receive_policy::config cfg) {
    rd_flag_ = cfg.first;
    max_ = cfg.second;
    prepare_next_read();
  }

  void write_packet(typename worker_type::id_type, std::vector<byte> header,
                    std::vector<byte> payload_elem, std::vector<byte> payload) {
    if (write_queue_.empty())
      manager().mask_add(operation::write);
    write_queue_.emplace_back(std::move(header));
    write_queue_.emplace_back(std::move(payload_elem));
    write_queue_.emplace_back(std::move(payload));
  }

  std::vector<byte> get_buffer() {
    if (empty_buffers_.empty()) {
      return {};
    } else {
      auto buf = std::move(empty_buffers_.front());
      empty_buffers_.pop_front();
      return buf;
    }
  }

private:
  // -- private member functions -----------------------------------------------

  bool write_some() {
    auto recycle = [&]() {
      empty_buffers_.front().clear();
      empty_buffers_.emplace_back(std::move(*write_queue_.begin()));
      write_queue_.pop_front();
    };
    // nothing to write
    if (write_queue_.empty())
      return false;
    do {
      if (write_queue_.front().empty()) {
        recycle();
        continue;
      }
      // get size of send buffer
      auto ret = send_buffer_size(handle_);
      if (!ret) {
        CAF_LOG_ERROR("send_buffer_size returned an error" << CAF_ARG(ret));
        return false;
      }
      // is send buffer of socket full?
      if (write_queue_.begin()->size() > *ret)
        return true;
      CAF_LOG_TRACE(CAF_ARG(handle_.id));
      auto write_ret = write(handle_, make_span(*write_queue_.begin()));
      if (auto num_bytes = get_if<size_t>(&write_ret)) {
        CAF_LOG_DEBUG(CAF_ARG(handle_.id) << CAF_ARG(*num_bytes));
        if (*num_bytes >= write_queue_.begin()->size()) {
          recycle();
        }
      } else {
        auto err = get<sec>(write_ret);
        CAF_LOG_DEBUG("send failed" << CAF_ARG(err));
        worker_.handle_error(err);
        return false;
      }
    } while (!write_queue_.empty());
    return false;
  }

  worker_type worker_;
  stream_socket handle_;

  std::vector<byte> read_buf_;
  std::deque<std::vector<byte>> write_queue_;
  std::deque<std::vector<byte>> empty_buffers_;

  // TODO implement retries using this member!
  // size_t max_consecutive_reads_;
  size_t read_threshold_;
  size_t collected_;
  size_t max_;
  receive_policy_flag rd_flag_;

  endpoint_manager* manager_;
};

} // namespace net
} // namespace caf
