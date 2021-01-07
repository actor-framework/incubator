// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#define CAF_SUITE container_source

#include "caf/bb/container_source.hpp"

#include "bb-test.hpp"

#include <vector>

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/attach_stream_sink.hpp"

using namespace caf;

namespace {

using container_type = std::vector<int32_t>;

TESTEE_SETUP();

TESTEE_STATE(container_sink) {
  container_type con;
};

TESTEE(container_sink) {
  return {
    [=](stream<container_type::value_type> in) {
      return attach_stream_sink(
        self,
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
        [=](unit_t&, const error&) {
          CAF_MESSAGE(self->name() << " is done");
        });
    },
  };
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
    // nop
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
