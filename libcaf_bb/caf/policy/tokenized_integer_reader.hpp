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

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "caf/behavior.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/pec.hpp"
#include "caf/stateful_actor.hpp"
#include "caf/string_algorithms.hpp"
#include "caf/unit.hpp"

namespace caf::policy {

/// Parses whitespace-separated integers from input strings to 'ValueType' and
/// pushes generated integers to a downstream.
template <class ValueType = int32_t>
class tokenized_integer_reader {
public:
  using value_type = ValueType;

  /// Returns the number of parsed integers or an error.
  expected<size_t> operator()(const std::string& line,
                              downstream<value_type> out) {
    size_t count = 0;
    auto i = line.c_str();
    while (*i != '\0') {
      // Parse next integer.
      char* end = nullptr;
      auto value = strtoll(i, &end, 10);
      if (errno == ERANGE) {
        if (value < 0) {
          return make_error(pec::exponent_underflow);
        }
        return make_error(pec::exponent_overflow);
      }
      if (std::numeric_limits<value_type>::min() > value)
        return make_error(pec::exponent_underflow);
      if (value > std::numeric_limits<value_type>::max())
        return make_error(pec::exponent_overflow);
      if (value == 0 && !(*end == ' ' || *end == '\0'))
        return make_error(pec::unexpected_character);
      ++count;
      out.push(value);
      // Advance iterator.
      i = end;
      while (isspace(*i))
        ++i;
    }
    return count;
  }
};

} // namespace caf::policy
