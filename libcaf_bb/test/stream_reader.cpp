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

#define CAF_SUITE stream_reader

#include "caf/bb/stream_reader.hpp"

#include "caf/test/dsl.hpp"

#include <sstream>
#include <string>
#include <vector>

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"

using namespace caf;

namespace {

using stream_type = std::istringstream;
using value_type = bb::tokenized_integer_reader<>::value_type;

TESTEE_SETUP();

TESTEE_STATE(stream_reader_sink) {
  std::vector<value_type> vec;
};

TESTEE(stream_reader_sink) {
  return {[=](stream<value_type> in) {
    return self->make_sink(
      // input stream
      in,
      // initialize state
      [=](unit_t&) {
        // nop
      },
      // Consumer
      [=](unit_t&, value_type val) {
        CAF_MESSAGE(self->name() << val);
        self->state.vec.emplace_back(std::move(val));
      },
      // cleanup and produce result message
      [=](unit_t&, const error&) { CAF_MESSAGE(self->name() << " is done"); });
  }};
}

TESTEE_STATE(stream_monitor) {
  actor streamer;
};

TESTEE(stream_monitor) {
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
    add_message_type<value_type>("value_type");
  }
};

using fixture = test_coordinator_fixture<config>;

} // namespace

CAF_TEST_FIXTURE_SCOPE(stream_reader_tests, fixture)

CAF_TEST(int_policy) {
  bb::tokenized_integer_reader<value_type> pol;
  std::deque<value_type> q;
  std::deque<value_type> test{1, 2, 3, 4};
  downstream<value_type> out(q);
  std::string test_line = "1 2 3 4";
  pol(test_line, out);
  CAF_CHECK_EQUAL(q, test);
}

CAF_TEST(stream_to_sink) {
  scoped_actor self{sys};
  std::string test_stringvalues = "1 2 3 4 5 6 7 78 1254 1 20\n4 56 78 95";
  std::istringstream test_stream(test_stringvalues);
  std::vector<value_type> test_container{1,    2, 3,  4, 5,  6,  7, 78,
                                         1254, 1, 20, 4, 56, 78, 95};
  auto sink = sys.spawn(stream_reader_sink);
  auto src
    = sys.spawn(bb::stream_reader<bb::tokenized_integer_reader<value_type>,
                                  std::istringstream, actor>,
                std::move(test_stream), sink);
  auto mon = sys.spawn(stream_monitor);
  self->send(mon, join_atom::value, src);
  run();
  CAF_CHECK_EQUAL(deref<stream_reader_sink_actor>(sink).state.vec,
                  test_container);
}

CAF_TEST(stream_to_sinks) {
  scoped_actor self{sys};
  std::string test_stringvalues = "1 2 3 4 5 6 7 78 1254 1 20\n4 56 78 95";
  std::istringstream test_stream(test_stringvalues);
  std::vector<value_type> test_container{1,    2, 3,  4, 5,  6,  7, 78,
                                         1254, 1, 20, 4, 56, 78, 95};
  auto snk1 = sys.spawn(stream_reader_sink);
  auto snk2 = sys.spawn(stream_reader_sink);
  auto snk3 = sys.spawn(stream_reader_sink);
  auto src
    = sys.spawn(bb::stream_reader<bb::tokenized_integer_reader<value_type>,
                                  std::istringstream, actor, actor, actor>,
                std::move(test_stream), snk1, snk2, snk3);
  auto mon = sys.spawn(stream_monitor);
  self->send(mon, join_atom::value, src);
  run();
  CAF_CHECK_EQUAL(deref<stream_reader_sink_actor>(snk1).state.vec,
                  test_container);
  CAF_CHECK_EQUAL(deref<stream_reader_sink_actor>(snk2).state.vec,
                  test_container);
  CAF_CHECK_EQUAL(deref<stream_reader_sink_actor>(snk3).state.vec,
                  test_container);
}

CAF_TEST_FIXTURE_SCOPE_END()