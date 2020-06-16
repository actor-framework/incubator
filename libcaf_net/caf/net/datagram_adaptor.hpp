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

#include "caf/byte_buffer.hpp"
#include "caf/net/basp/header.hpp"

namespace caf::net {

/// Implements an adaption layer for datagram oriented transport protocols.
template <class Application>
class datagram_adaptor {
public:
  datagram_adaptor(Application application)
    : application_(std::move(application)) {
    // nop
  }

  template <class Parent>
  error init(Parent& parent) {
    return application_.init(parent);
  }

  template <class Parent>
  error write_message(Parent& parent,
                      std::unique_ptr<endpoint_manager_queue::message> msg) {
    return application_.write_message(parent, std::move(msg));
  }

  template <class Parent>
  error handle_data(Parent& parent, span<const byte> received) {
    if (auto err = application_.handle_data(
          parent, make_span(received.data(), basp::header_size)))
      return err;
    auto data = received.data() + basp::header_size;
    auto size = received.size() - basp::header_size;
    if (size > 0)
      return application_.handle_data(parent, make_span(data, size));
    return none;
  }

  template <class Parent>
  void resolve(Parent& parent, string_view path, const actor& listener) {
    application_.resolve(parent, path, listener);
  }

  template <class Parent>
  void new_proxy(Parent& parent, actor_id id) {
    application_.new_proxy(parent, id);
  }

  template <class Parent>
  void local_actor_down(Parent& parent, actor_id id, error reason) {
    application_.local_actor_down(parent, id, std::move(reason));
  }

  template <class Parent>
  void timeout(Parent& parent, std::string tag, uint64_t id) {
    application_.timeout(parent, std::move(tag), id);
  }

  void handle_error(sec error) {
    application_.handle_error(error);
  }

private:
  Application application_;
};

} // namespace caf::net
