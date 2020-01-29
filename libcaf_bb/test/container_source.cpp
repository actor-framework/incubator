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

#define CAF_SUITE container_source

#include "caf/bb/container_source.hpp"

#include "caf/test/dsl.hpp"

#include <vector>

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"

using namespace caf;

namespace {

using container_type = std::vector<int>;

TESTEE_SETUP();

TESTEE_STATE(container_sink) {
  container_type con;
};

TESTEE(container_sink) {
  return {[=](stream<container_type::value_type> in) {
    return self->make_sink(
      // input stream
      in,
      // initialize state
      [=](unit_t&) {
        // nop
      },
      // Consumer
      [=](unit_t&, container_type::value_type val) {
        self->state.con.emplace_back(std::move(val));
      },
      // cleanup and produce result message
      [=](unit_t&, const error&) { CAF_MESSAGE(self->name() << " is done"); });
  }};
}

TESTEE_STATE(container_monitor) {
  actor streamer;
};

TESTEE(container_monitor) {
  self->set_down_handler([=](const down_msg& dm) {
    CAF_CHECK_EQUAL(dm.source, self->state.streamer);
  });

  return {[=](join_atom, actor streamer) {
    self->state.streamer = streamer;
    self->monitor(streamer);
  }};
}

struct config : actor_system_config {
  config() {
    add_message_type<container_type::value_type>("value_type");
  }
};

using fixture = test_coordinator_fixture<config>;

} // namespace

CAF_TEST_FIXTURE_SCOPE(container_source_tests, fixture)

CAF_TEST(stream_to_sink) {
  scoped_actor self{sys};
  container_type test_container{0, 1, 2, 3, 4, 5};
  auto sink = sys.spawn(container_sink);
  auto src = sys.spawn(bb::container_source<container_type, actor>,
                       test_container, sink);
  auto mon = sys.spawn(container_monitor);
  self->send(mon, join_atom_v, src);
  run();
  CAF_CHECK_EQUAL(deref<container_sink_actor>(sink).state.con, test_container);
}

CAF_TEST(stream_to_sinks) {
  scoped_actor self{sys};
  container_type test_container{0, 1, 2, 3, 4, 5};
  auto snk1 = sys.spawn(container_sink);
  auto snk2 = sys.spawn(container_sink);
  auto snk3 = sys.spawn(container_sink);
  auto src = sys
               .spawn(bb::container_source<container_type, actor, actor, actor>,
                      test_container, snk1, snk2, snk3);
  auto mon = sys.spawn(container_monitor);
  self->send(mon, join_atom_v, src);
  run();
  CAF_CHECK_EQUAL(deref<container_sink_actor>(snk1).state.con, test_container);
  CAF_CHECK_EQUAL(deref<container_sink_actor>(snk2).state.con, test_container);
  CAF_CHECK_EQUAL(deref<container_sink_actor>(snk3).state.con, test_container);
}

CAF_TEST_FIXTURE_SCOPE_END()
