/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2020 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#define CAF_SUITE net.reliability.ordering

#include "caf/net/reliability/ordering.hpp"

#include <deque>
#include <initializer_list>
#include <string>
#include <vector>

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/net/reliability/ordering_header.hpp"
#include "caf/net/transport_worker.hpp"
#include "caf/timestamp.hpp"

using namespace caf;
using namespace caf::net;
using namespace std::literals::string_literals;

using message_buffer = std::vector<byte_buffer>;

namespace {

struct fixture : test_coordinator_fixture<>, host_fixture {
  fixture() : all_messages_received(false) {
    // nop
  }

  message_buffer make_unordered_message_sequence(int from, int to) {
    message_buffer messages;
    if (from > to)
      CAF_FAIL("`from` has to be less than `to`");
    for (auto i = to; i >= from; --i)
      messages.emplace_back(make_message(i));
    return messages;
  }

  message_buffer make_ordered_message_sequence(int from, int to) {
    message_buffer messages;
    if (from > to)
      CAF_FAIL("`from` has to be less than `to`");
    for (auto i = from; i <= to; ++i)
      messages.emplace_back(make_message(i));
    return messages;
  }

  byte_buffer make_message(int seq) {
    byte_buffer buf;
    binary_serializer sink(sys, buf);
    reliability::ordering_header hdr{reliability::sequence_type(seq)};
    if (auto err = sink(hdr, uint8_t(seq)))
      CAF_FAIL("could not serialize message" << CAF_ARG(seq));
    return buf;
  }

  bool all_messages_received;
};

class dummy_application {
public:
  dummy_application(bool& success, uint16_t from, uint16_t to,
                    std::initializer_list<uint8_t> missing)
    : success_(success), sequence_(from), sequence_end_(to) {
    missing_.insert(missing_.end(), missing.begin(), missing.end());
  }

  template <class Parent>
  error init(Parent&) {
    return none;
  }

  template <class Parent>
  error handle_data(Parent&, span<const byte> bytes) {
    CAF_CHECK_EQUAL(bytes.size(), size_t(1));
    uint8_t res = 0;
    binary_deserializer source{nullptr, bytes};
    if (auto err = source(res))
      return err;
    CAF_CHECK_EQUAL(res, sequence_);
    success_ = (res == sequence_end_);
    ++sequence_;
    if (std::find_if(missing_.begin(), missing_.end(),
                     [&](const auto& val) { return val == sequence_; })
        != missing_.end())
      ++sequence_;
    return none;
  }

  template <class Parent>
  void timeout(Parent&, std::string, uint64_t) {
    CAF_FAIL("timeout should be handled by ordering layer");
  }

private:
  bool& success_;
  uint8_t sequence_;
  uint8_t sequence_end_;
  std::vector<uint8_t> missing_;
};

template <class Application>
class dummy_transport {
public:
  using transport_type = dummy_transport;

  using application_type = Application;

  dummy_transport(actor_system& sys, Application application)
    : sys_(sys), worker_(std::move(application)), timeout_id_(0) {
    // nop
  }

  void init() {
    CAF_REQUIRE_EQUAL(worker_.init(*this), none);
  }

  void pass(const byte_buffer& msg) {
    if (auto err = worker_.handle_data(*this, make_span(msg)))
      CAF_FAIL("handle_data failed" << CAF_ARG(err));
  }

  void pass(const message_buffer& messages) {
    for (auto& msg : messages)
      pass(msg);
  }

  actor_system& system() {
    return sys_;
  }

  dummy_transport& transport() {
    return *this;
  }

  byte_buffer next_header_buffer() {
    return {};
  }

  byte_buffer next_payload_buffer() {
    return {};
  }

  template <class IdType>
  void write_packet(IdType, span<byte_buffer>) {
    // nop
  }

  template <class... Ts>
  uint64_t set_timeout(timestamp, std::string tag, Ts&&...) {
    timeouts_.emplace_back(tag, timeout_id_);
    return timeout_id_++;
  }

  void cancel_timeout(std::string, uint64_t id) {
    timeouts_.erase(
      std::find_if(timeouts_.begin(), timeouts_.end(),
                   [&](const auto& p) { return p.second == id; }));
  }

  void trigger_timeout() {
    auto [tag, id] = timeouts_.front();
    worker_.timeout(*this, std::move(tag), id);
    timeouts_.pop_front();
  }

  bool timeouts_empty() {
    return timeouts_.empty();
  }

private:
  actor_system& sys_;
  transport_worker<Application> worker_;
  uint64_t timeout_id_;
  std::deque<std::pair<std::string, uint64_t>> timeouts_;
};

#define TEST_DELIVER_PENDING_UNORDERED(from, to)                               \
  using ordering_type = reliability::ordering<dummy_application>;              \
  dummy_transport<ordering_type> trans{                                        \
    sys,                                                                       \
    ordering_type{dummy_application{all_messages_received, from, to, {}}}};    \
  trans.init();                                                                \
  trans.pass(make_unordered_message_sequence(from, to));                       \
  CAF_CHECK(all_messages_received)

} // namespace

CAF_TEST_FIXTURE_SCOPE(ordering_tests, fixture)

CAF_TEST(ordered packets) {
  using ordering_type = reliability::ordering<dummy_application>;
  uint16_t from = 0;
  uint16_t to = 3;
  dummy_transport<ordering_type> trans{
    sys, ordering_type{dummy_application{all_messages_received, from, to, {}}}};
  trans.init();
  trans.pass(make_ordered_message_sequence(from, to));
  CAF_CHECK(all_messages_received);
}

CAF_TEST(unordered packets) {
  TEST_DELIVER_PENDING_UNORDERED(0, 2);
}

CAF_TEST(max pending) {
  uint16_t from = 1;
  auto max_pending_messages
    = get_or(cfg, "middleman.max-pending-messages",
             defaults::reliability::max_pending_messages);
  uint16_t to = max_pending_messages + 1;
  TEST_DELIVER_PENDING_UNORDERED(from, to);
}

CAF_TEST(simple timeout) {
  using ordering_type = reliability::ordering<dummy_application>;
  dummy_transport<ordering_type> trans{
    sys, ordering_type{dummy_application{all_messages_received, 1, 1, {}}}};
  trans.init();
  auto msg = make_message(1);
  trans.pass(msg);
  CAF_CHECK(!trans.timeouts_empty());
  trans.trigger_timeout();
  CAF_CHECK(trans.timeouts_empty());
  CAF_CHECK(all_messages_received);
}

CAF_TEST(cancel timeout) {
  using ordering_type = reliability::ordering<dummy_application>;
  dummy_transport<ordering_type> trans{
    sys, ordering_type{dummy_application{all_messages_received, 0, 4, {}}}};
  trans.init();
  auto first_batch = make_ordered_message_sequence(0, 1);
  auto second_batch = make_ordered_message_sequence(3, 4);
  auto last_message = make_message(2);
  trans.pass(first_batch);
  trans.pass(second_batch);
  CAF_CHECK(!trans.timeouts_empty());
  trans.pass(last_message);
  CAF_CHECK(trans.timeouts_empty());
  CAF_CHECK(all_messages_received);
}

CAF_TEST_FIXTURE_SCOPE_END()
