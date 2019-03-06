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

#include <fstream>
#include <string>

#include "caf/behavior.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/stateful_actor.hpp"
#include "caf/unit.hpp"

namespace caf {
namespace bb {

using file_name = std::string;

/// @relates file-reader
/// The Policy defines how the file-reader pares a line of the given file.
template <class Stream_Output>
class Policy {
public:
  using value_type = typename Stream_Output::value_type;

  /// Returns number of produced elements or an error.
  expected<size_t> operator()(std::string line, downstream<value_type> out) = 0;
};

/// @relates file-reader
struct file_reader_state {
  // -- constructors, destructors, and assignment operators --------------------

  file_reader_state() : name("file-reader") {
    // nop
  }

  void init(file_name fname) {
    file.open(fname);
  }

  // -- properties -------------------------------------------------------------

  size_t at_end() const {
    return file.eof();
  }

  // -- member variables -------------------------------------------------------

  /// Gives this actor a useful name in CAF logs.
  const char* name;

  /// File
  std::ifstream file;

  /// Caches the file line we are about to stream.
  std::string line;
};

/// Streams the content of given file `fname` line by line using the given
/// policy to all given stream sinks.
template <class Policy, class Handle, class... Handles>
behavior file_reader(stateful_actor<file_reader_state>* self, file_name fname,
                     Handle sink, Handles... sinks) {
  using value_type = typename Policy::value_type;
  self->state.init(fname);
  // Fail early if we got nothing to stream.
  if (!self->state.file.is_open())
    return {};
  // Spin up stream manager and connect the first sink.
  auto src = self->make_source(
    std::move(sink),
    [&](unit_t&) {
      // nop
    },
    [self](unit_t&, downstream<value_type>& out, size_t hint) {
      auto& st = self->state;
      Policy pol;
      auto i = 0;
      while (i < hint && getline(st.file, st.line))
        i += pol(st.line, out);
    },
    [self](const unit_t&) { return self->state.at_end(); });
  // Add the remaining sinks.
  std::initializer_list<unit_t>{src.ptr()->add_outbound_path(sinks)...};
  return {};
}

} // namespace bb
} // namespace caf
