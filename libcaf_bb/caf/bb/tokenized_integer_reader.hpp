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

#include <string>
#include <vector>

#include "caf/behavior.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/stateful_actor.hpp"
#include "caf/string_algorithms.hpp"
#include "caf/unit.hpp"

namespace caf {
namespace bb {

/// @relates stream_reader
/// The error codes for the stream_reader policy.
enum class stream_reader_policy_error : uint8_t { out_of_range = 1 };

error make_error(stream_reader_policy_error x) {
  return {static_cast<uint8_t>(x), atom("stream")};
}

/// @relates stream_reader
/// The Policy defines how the stream_reader pares a line of the given stream to
/// integers.
template <class ValueType = int32_t>
class tokenized_integer_reader {
public:
  using value_type = ValueType;

  /// Returns number of produced elements or an error.
  expected<size_t> operator()(const std::string& line,
                              downstream<value_type> out) {
    size_t count = 0;
    auto i = line.c_str();
    while (*i != '\0') {
      // Parse next integer.
      char* end = nullptr;
      auto value = strtoll(i, &end, 10);
      if (errno == ERANGE) {
        return make_error(stream_reader_policy_error::out_of_range);
      }
      ++count;
      out.push(value);
      // TODO: check whether value fits into value_type
      // Advance iterator.
      i = end;
      while (isspace(*i))
        ++i;
    }
    return count;
  }
};

} // namespace bb
} // namespace caf
