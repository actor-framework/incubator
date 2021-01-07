// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#pragma once

#include <cstdint>
#include <limits>
#include <string>

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
