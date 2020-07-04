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

#include "caf/net/packet_writer.hpp"

#include "caf/actor_clock.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/span.hpp"

namespace {

// This is necessary to dispatch the function `id()` to either `object` or
// `parent`.
// TODO: This could be done with `CAF_HAS_MEMBER_TRAIT` from `type_traits.hpp`
// but the macro is undefined at the end of the file.
template <class T>
class has_id_member {
private:
  template <class U>
  static auto sfinae(U* x) -> decltype(x->id(), std::true_type());

  template <class U>
  static auto sfinae(...) -> std::false_type;

  using sfinae_type = decltype(sfinae<T>(nullptr));

public:
  static constexpr bool value = sfinae_type::value;
};

} // namespace

namespace caf::net {

/// Implements the interface for transport and application policies and
/// dispatches member functions either to `object` or `parent`.
template <class Object, class Parent>
class packet_writer_decorator {
public:
  // -- member types -----------------------------------------------------------

  using transport_type = typename Parent::transport_type;

  using application_type = typename Parent::application_type;

  // -- constructors, destructors, and assignment operators --------------------

  packet_writer_decorator(Object& object, Parent& parent)
    : object_(object), parent_(parent) {
    // nop
  }

  // -- properties -------------------------------------------------------------

  actor_system& system() {
    return parent_.system();
  }

  transport_type& transport() {
    return parent_.transport();
  }

  endpoint_manager& manager() {
    return parent_.manager();
  }

  byte_buffer next_header_buffer() {
    return transport().next_header_buffer();
  }

  byte_buffer next_payload_buffer() {
    return transport().next_payload_buffer();
  }

  auto id() {
    if constexpr (has_id_member<Parent>::value)
      return parent_.id();
    else
      return object_.id();
  }

  // -- member functions -------------------------------------------------------

  void cancel_timeout(std::string tag, uint64_t id) {
    parent_.cancel_timeout(std::move(tag), id);
  }

  template <class... Ts>
  uint64_t
  set_timeout(actor_clock::time_point tout, std::string tag, Ts&&... xs) {
    return parent_.set_timeout(tout, std::move(tag), id(),
                               std::forward<Ts>(xs)...);
  }

  /// Convenience function to write a packet consisting of multiple buffers.
  /// @param buffers all buffers for the packet. The first buffer is a header
  ///                buffer, the other buffers are payload buffer.
  /// @warning this function takes ownership of `buffers`.
  template <class... Ts>
  void write_packet(Ts&... buffers) {
    object_.write_packet(parent_, buffers...);
  }

private:
  Object& object_;
  Parent& parent_;
};

template <class Object, class Parent>
packet_writer_decorator<Object, Parent>
make_packet_writer_decorator(Object& object, Parent& parent) {
  return {object, parent};
}

} // namespace caf::net
