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

#include <array>
#include <cstdint>

#include "caf/detail/comparable.hpp"
#include "caf/detail/net_export.hpp"
#include "caf/fwd.hpp"
#include "caf/meta/hex_formatted.hpp"
#include "caf/meta/type_name.hpp"
#include "caf/net/basp/constants.hpp"
#include "caf/net/basp/message_type.hpp"

namespace caf::net::basp {

/// @addtogroup BASP

/// The header of a Binary Actor System Protocol (BASP) message.
struct CAF_NET_EXPORT header : detail::comparable<header> {
  // -- constructors, destructors, and assignment operators --------------------

  constexpr header() noexcept
    : type(message_type::handshake), operation_data(0) {
    // nop
  }

  constexpr header(message_type type, uint64_t operation_data) noexcept
    : type(type), operation_data(operation_data) {
    // nop
  }

  header(const header&) noexcept = default;

  header& operator=(const header&) noexcept = default;

  // -- factory functions ------------------------------------------------------

  /// @pre `bytes.size() == header_size`
  static header from_bytes(span<const byte> bytes);

  // -- comparison -------------------------------------------------------------

  int compare(header other) const noexcept;

  // -- member variables -------------------------------------------------------

  /// Denotes the BASP operation and how `operation_data` gets interpreted.
  message_type type;

  /// Stores type-specific information such as the BASP version in handshakes.
  uint64_t operation_data;
};

/// Serializes a header to a byte representation.
/// @relates header
CAF_NET_EXPORT std::array<byte, header_size> to_bytes(header x);

/// Serializes a header to a byte representation.
/// @relates header
CAF_NET_EXPORT void to_bytes(header x, byte_buffer& buf);

/// @relates header
template <class Inspector>
bool inspect(Inspector& f, header& x) {
  return f.object(x).fields(f.field("type", x.type),
                            f.field("operation_data", x.operation_data));
}

/// @}

} // namespace caf::net::basp
