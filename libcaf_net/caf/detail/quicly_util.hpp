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

#include <caf/span.hpp>
#include <cstdint>
#include <functional>
#include <memory>

#include "caf/detail/comparable.hpp"
#include "caf/detail/fnv_hash.hpp"
#include "caf/meta/type_name.hpp"

extern "C" {
#include "quicly.h"
}

namespace caf {
namespace detail {

using quicly_conn_ptr = std::shared_ptr<quicly_conn_t>;

quicly_conn_ptr make_quicly_conn_ptr(quicly_conn_t* conn) {
  return std::shared_ptr<quicly_conn_t>(conn, quicly_free);
}

} // namespace detail
} // namespace caf

namespace std {

template <>
struct hash<caf::detail::quicly_conn_ptr> {
  size_t operator()(const caf::detail::quicly_conn_ptr conn) const noexcept {
    return std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(conn.get()));
  }
};

} // namespace std