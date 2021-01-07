// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#define CAF_SUITE stream_reader

#include "caf/bb/stream_reader.hpp"

#include "bb-test.hpp"

#include <memory>
#include <string>
#include <vector>

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/attach_stream_sink.hpp"
#include "caf/policy/tokenized_integer_reader.hpp"

using namespace caf;

namespace {

using stream_type = std::istringstream;
using value_type = policy::tokenized_integer_reader<>::value_type;

TESTEE_SETUP();

TESTEE_STATE(stream_reader_sink) {
  std::vector<value_type> vec;
};

TESTEE(stream_reader_sink) {
  return {
    [=](stream<value_type> in) {
      return attach_stream_sink(
        self,
        // input stream
        in,
        // initialize state
        [=](unit_t&) {
          // nop
        },
        // consume values
        [=](unit_t&, value_type val) {
          self->state.vec.emplace_back(std::move(val));
        },
        // cleanup and produce result message
        [=](unit_t&, const error& e) {
          if (e) {
            CAF_MESSAGE(self->name() << " " << e);
          } else {
            CAF_MESSAGE(self->name() << " is done");
          }
        });
    },
  };
}

TESTEE_STATE(stream_monitor) {
  actor streamer;
};

TESTEE(stream_monitor) {
  self->set_down_handler([=](const down_msg& dm) {
    CAF_CHECK_EQUAL(dm.source, self->state.streamer);
    if (dm.reason)
      CAF_CHECK_EQUAL(dm.reason, pec::unexpected_character);
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

CAF_TEST_FIXTURE_SCOPE(stream_reader_tests, fixture)

CAF_TEST(stream_to_sink) {
  scoped_actor self{sys};
  std::string test_stringvalues = "1 2 3 4 5 6 7 78 1254 1 20\n4 56 78 95";
  std::unique_ptr<stream_type> ptr_test_stream{
    new stream_type(test_stringvalues)};
  std::vector<value_type> test_container{1,    2, 3,  4, 5,  6,  7, 78,
                                         1254, 1, 20, 4, 56, 78, 95};
  auto sink = sys.spawn(stream_reader_sink);
  auto src = sys.spawn(bb::stream_reader<
                         policy::tokenized_integer_reader<value_type>,
                         stream_type, actor>,
                       std::move(ptr_test_stream), sink);
  auto mon = sys.spawn(stream_monitor);
  self->send(mon, join_atom_v, src);
  run();
  CAF_CHECK_EQUAL(deref<stream_reader_sink_actor>(sink).state.vec,
                  test_container);
}

CAF_TEST(stream_to_sinks) {
  scoped_actor self{sys};
  std::string test_stringvalues = "1 2 3 4 5 6 7 78 1254 1 20\n4 56 78 95";
  std::unique_ptr<stream_type> ptr_test_stream{
    new stream_type(test_stringvalues)};
  std::vector<value_type> test_container{1,    2, 3,  4, 5,  6,  7, 78,
                                         1254, 1, 20, 4, 56, 78, 95};
  auto snk1 = sys.spawn(stream_reader_sink);
  auto snk2 = sys.spawn(stream_reader_sink);
  auto snk3 = sys.spawn(stream_reader_sink);
  auto src = sys.spawn(bb::stream_reader<
                         policy::tokenized_integer_reader<value_type>,
                         stream_type, actor, actor, actor>,
                       std::move(ptr_test_stream), snk1, snk2, snk3);
  auto mon = sys.spawn(stream_monitor);
  self->send(mon, join_atom_v, src);
  run();
  CAF_CHECK_EQUAL(deref<stream_reader_sink_actor>(snk1).state.vec,
                  test_container);
  CAF_CHECK_EQUAL(deref<stream_reader_sink_actor>(snk2).state.vec,
                  test_container);
  CAF_CHECK_EQUAL(deref<stream_reader_sink_actor>(snk3).state.vec,
                  test_container);
}

CAF_TEST(error_stream_to_sink) {
  scoped_actor self{sys};
  std::string test_stringvalues = "1 2 3 4 5 6 7 rr 1254";
  std::unique_ptr<stream_type> ptr_test_stream{
    new stream_type(test_stringvalues)};
  auto sink = sys.spawn(stream_reader_sink);
  auto src = sys.spawn(bb::stream_reader<
                         policy::tokenized_integer_reader<value_type>,
                         stream_type, actor>,
                       std::move(ptr_test_stream), sink);
  auto mon = sys.spawn(stream_monitor);
  self->send(mon, join_atom_v, src);
  run();
}

CAF_TEST_FIXTURE_SCOPE_END()
