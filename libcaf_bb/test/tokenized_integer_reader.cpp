// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#define CAF_SUITE tokenized_integer_reader

#include "caf/policy/tokenized_integer_reader.hpp"

#include "caf/test/dsl.hpp"

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"

using namespace caf;

namespace {

using value_type = policy::tokenized_integer_reader<>::value_type;

struct fixture {};

} // namespace

CAF_TEST_FIXTURE_SCOPE(tokenized_integer_reader_tests, fixture)

CAF_TEST(int_policy) {
  policy::tokenized_integer_reader<value_type> pol;
  std::deque<value_type> q;
  std::deque<value_type> test{1, 2, 3, 4};
  downstream<value_type> out(q);
  std::string test_line = "1 2 3 4";
  pol(test_line, out);
  CAF_CHECK_EQUAL(q, test);
}

CAF_TEST(error_int_policy) {
  policy::tokenized_integer_reader<value_type> pol;
  std::deque<value_type> q;
  downstream<value_type> out(q);
  std::string test_line = "1 r 3 4";
  auto count = pol(test_line, out);
  CAF_CHECK_EQUAL(count.error(), pec::unexpected_character);
  std::string test_line2 = "1 -2247483648 3 4";
  count = pol(test_line2, out);
  CAF_CHECK_EQUAL(count.error(), pec::exponent_underflow);
  std::string test_line3 = "1 2147483648 3 4";
  count = pol(test_line3, out);
  CAF_CHECK_EQUAL(count.error(), pec::exponent_overflow);
}

CAF_TEST_FIXTURE_SCOPE_END()
