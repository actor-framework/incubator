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

#include <cstdint>

namespace caf::net {

/// The header of a slice.
struct slicing_header {
  /// The sequence number of *this* slice.
  size_t no = 0;
  /// The total number of slices.
  size_t of = 0;
};

constexpr size_t slicing_header_size = sizeof(size_t) + sizeof(size_t);

/// @relates slicing_header
template <class Inspector>
bool inspect(Inspector& f, slicing_header& x) {
  return f.object(x).fields(f.field("no", x.no), f.field("of", x.of));
}

} // namespace caf::net
