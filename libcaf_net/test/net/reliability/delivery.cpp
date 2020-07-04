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

#define CAF_SUITE net.reliability.delivery

#include "caf/net/reliability/delivery.hpp"

#include <deque>
#include <initializer_list>

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include "caf/actor_clock.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/mailbox_element.hpp"
#include "caf/net/endpoint_manager_queue.hpp"
#include "caf/net/reliability/delivery_header.hpp"
#include "caf/net/transport_worker.hpp"
#include "caf/string_view.hpp"
#include "caf/timestamp.hpp"

using namespace caf;
using namespace caf::net;
using namespace std::literals::string_literals;

namespace {

constexpr string_view hello_test = "hello test!";

class dummy_application {
public:
  explicit dummy_application(byte_buffer& received_data)
    : received_data_(received_data) {
    // nop
  }

  template <class Parent>
  error init(Parent&) {
    return none;
  }

  template <class Parent>
  error write_message(Parent& parent,
                      std::unique_ptr<endpoint_manager_queue::message> ptr) {
    byte_buffer buf;
    binary_serializer sink(nullptr, buf);
    if (auto err = sink(ptr->msg->content()))
      return err;
    parent.write_packet(buf);
    return none;
  }

  template <class Parent>
  error handle_data(Parent&, span<const byte> data) {
    received_data_.clear();
    received_data_.insert(received_data_.end(), data.begin(), data.end());
    return none;
  }

  template <class Parent>
  void timeout(Parent&, std::string, uint64_t) {
    CAF_FAIL("timeout should be handled by reliability layer");
  }

private:
  byte_buffer& received_data_;
};

template <class Application>
class dummy_transport {
public:
  using transport_type = dummy_transport;

  using application_type = Application;

  dummy_transport(actor_system& sys, Application application,
                  byte_buffer& last_packet)
    : sys_(sys),
      worker_(std::move(application)),
      timeout_id_(0),
      last_packet_(last_packet) {
    // nop
  }

  void init() {
    CAF_REQUIRE_EQUAL(worker_.init(*this), none);
  }

  void handle_data(const byte_buffer& msg) {
    if (auto err = worker_.handle_data(*this, make_span(msg)))
      CAF_FAIL("handle_data failed" << CAF_ARG(err));
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

  void write_message() {
    using message_type = endpoint_manager_queue::message;
    auto msg = make_message(to_string(hello_test));
    auto elem = make_mailbox_element(nullptr, make_message_id(), {}, msg);
    auto dummy_msg = detail::make_unique<message_type>(std::move(elem),
                                                       nullptr);
    worker_.write_message(*this, std::move(dummy_msg));
  }

  template <class IdType>
  void write_packet(IdType, span<byte_buffer*> bufs) {
    last_packet_.clear();
    for (const auto buf : bufs)
      last_packet_.insert(last_packet_.end(), buf->begin(), buf->end());
  }

  template <class... Ts>
  uint64_t set_timeout(actor_clock::time_point, std::string tag, Ts&&...) {
    timeouts_.emplace_back(tag, timeout_id_);
    CAF_MESSAGE("timeout set: " << CAF_ARG(timeout_id_));
    return timeout_id_++;
  }

  void cancel_timeout(std::string, uint64_t id) {
    CAF_MESSAGE("timeout cancelled: " << CAF_ARG(id));
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
  byte_buffer& last_packet_;
};

struct fixture : test_coordinator_fixture<>, host_fixture {
  using reliability_type = reliability::delivery<dummy_application>;

  fixture()
    : trans{sys, reliability_type{dummy_application{received_data}},
            last_packet} {
    trans.init();
  }

  void check_message(byte_buffer& buf, reliability::id_type id) {
    reliability::delivery_header hdr;
    caf::message msg;
    binary_deserializer source(sys, buf);
    if (auto err = source(hdr, msg))
      CAF_FAIL("could not deserialize message " << CAF_ARG(err));
    CAF_CHECK_EQUAL(hdr.id, id);
    CAF_CHECK(!hdr.is_ack);
    auto received_str = msg.get_as<std::string>(0);
    CAF_CHECK_EQUAL(string_view{received_str}, hello_test);
  }

  dummy_transport<reliability_type> trans;
  byte_buffer last_packet;
  byte_buffer received_data;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(delivery_tests, fixture)

CAF_TEST(timeout) {
  trans.write_message();
  check_message(last_packet, 0);
  last_packet.clear();
  trans.trigger_timeout();
  check_message(last_packet, 0);
}

CAF_TEST(ACK cancels timeout) {
  trans.write_message();
  check_message(last_packet, 0);
  last_packet.clear();
  byte_buffer ack;
  binary_serializer sink(sys, ack);
  if (auto err = sink(reliability::delivery_header{0, true}))
    CAF_FAIL("could not serialize ack " << CAF_ARG(err));
  trans.handle_data(ack);
  CAF_CHECK(trans.timeouts_empty());
}

CAF_TEST(handle_data leads to ACK) {
  byte_buffer buf;
  binary_serializer sink(sys, buf);
  if (auto err = sink(reliability::delivery_header{1337, false},
                      make_message(to_string(hello_test))))
    CAF_FAIL("could not serialize message" << CAF_ARG(err));
  CAF_MESSAGE("sending message");
  trans.handle_data(buf);
  reliability::delivery_header hdr;
  binary_deserializer source(sys, last_packet);
  if (auto err = source(hdr))
    CAF_FAIL("could not deserialize header " << err);
  CAF_CHECK(hdr.is_ack);
  CAF_CHECK_EQUAL(hdr.id, reliability::id_type(1337));
  caf::message msg;
  source = binary_deserializer(sys, received_data);
  if (auto err = source(msg))
    CAF_FAIL("could not deserialize header " << err);
  auto received_str = msg.get_as<std::string>(0);
  CAF_CHECK_EQUAL(string_view{received_str}, hello_test);
}

CAF_TEST_FIXTURE_SCOPE_END()
