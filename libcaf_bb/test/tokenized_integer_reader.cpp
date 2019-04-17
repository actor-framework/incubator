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
