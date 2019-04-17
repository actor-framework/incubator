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
template <class InputStream>
struct stream_reader_state {
  // -- constructors, destructors, and assignment operators --------------------

  stream_reader_state() : name("stream-reader") {
    // nop
  }

  void init(std::unique_ptr<InputStream> input) {
    this->input = std::move(input);
  }

  // -- properties -------------------------------------------------------------

  size_t at_end() const {
    return !(*input);
  }

  // -- member variables -------------------------------------------------------

  /// Gives this actor a useful name in CAF logs.
  const char* name;

  /// Input stream we are about to stream
  // TODO: change after having raised the minimum GCC version to 5.
  std::unique_ptr<InputStream> input;

  /// Caches the line we are about to parse.
  std::string line;
};

/// @relates stream_reader
template <class InputStream>
using stream_source_type = stateful_actor<stream_reader_state<InputStream>>;

/// Streams the content of given 'src_stream' line by line using the given
/// policy to all given stream sinks.
template <class Policy, class InputStream, class Handle, class... Handles>
void stream_reader(stream_source_type<InputStream>* self,
                   std::unique_ptr<InputStream> input, Handle sink,
                   Handles... sinks) {
  using value_type = typename Policy::value_type;
  self->state.init(std::move(input));
  // Fail early if we got nothing to stream.
  if (self->state.at_end())
    self->quit();
  // Spin up stream manager and connect the first sink.
  auto src = self->make_source(
    std::move(sink),
    [&](Policy& pol) {
      // nop
    },
    [self](Policy& pol, downstream<value_type>& out, size_t hint) {
      auto& st = self->state;
      size_t i = 0;
      while (i < hint && getline(*(st.input), st.line)) {
        if (auto count = pol(st.line, out)) {
          i += *count;
        } else {
          self->quit(count.error());
        }
      }
    },
    [self](const Policy& pol) { return self->state.at_end(); });
  // Add the remaining sinks.
  unit(src.ptr()->add_outbound_path(sinks)...);
  self->quit();
}

} // namespace bb
} // namespace caf
