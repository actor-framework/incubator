// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#pragma once

#include <algorithm>
#include <iterator>

#include "caf/behavior.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/stateful_actor.hpp"
#include "caf/unit.hpp"

namespace caf::bb {

/// @relates container_source
template <class Container>
struct container_source_state {
  // -- constructors, destructors, and assignment operators --------------------

  container_source_state() : name("container-source"), i(xs.end()) {
    // nop
  }

  void init(Container&& elements) {
    xs = std::move(elements);
    i = xs.begin();
  }

  // -- properties -------------------------------------------------------------

  size_t remaining() {
    return static_cast<size_t>(std::distance(i, xs.end()));
  }

  size_t at_end() const {
    return i == xs.end();
  }

  // -- member variables -------------------------------------------------------

  /// Gives this actor a useful name in CAF logs.
  const char* name;

  /// Caches the elements we are about to stream.
  Container xs;

  /// Points at the current streaming position.
  typename Container::iterator i;
};

/// @relates container_source
template <class Container>
using container_source_type = stateful_actor<container_source_state<Container>>;

/// Streams the content of given container `xs` to all given stream sinks.
template <class Container, class Handle, class... Handles>
behavior container_source(container_source_type<Container>* self, Container xs,
                          Handle sink, Handles... sinks) {
  using value_type = typename Container::value_type;
  // Fail early if we got nothing to stream.
  if (xs.empty())
    return {};
  // Spin up stream manager and connect the first sink.
  self->state.init(std::move(xs));
  auto src = attach_stream_source(
    self, std::move(sink),
    [&](unit_t&) {
      // nop
    },
    [self](unit_t&, downstream<value_type>& out, size_t hint) {
      auto& st = self->state;
      auto n = std::min(hint, st.remaining());
      for (size_t pushed = 0; pushed < n; ++pushed)
        out.push(std::move(*st.i++));
    },
    [self](const unit_t&) { return self->state.at_end(); });
  // Add the remaining sinks.
  unit(src.ptr()->add_outbound_path(sinks)...);
  return {};
}

/// Convenience function for spawning container sources.

} // namespace caf::bb
