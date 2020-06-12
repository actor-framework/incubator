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
    : application_(std::move(application)), missing_(0), passed_(0) {
    // nop
  }

  template <class Parent>
  error handle_data(Parent&, span<const byte> received) {
    auto writer = make_packet_writer_decorator(*this, parent);
    if (auto err = application_.handle_data(
          writer_, make_span(received.data(), basp::header_size)))
      return err;
    if (auto err = application_.handle_data(
          writer_, make_span(received.data() + basp::header_size,
                             received.size() - basp::header_size))
      return err;
  }

private:
  Application application_;
};

} // namespace caf::net
