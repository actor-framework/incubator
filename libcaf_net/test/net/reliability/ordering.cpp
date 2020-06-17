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

#include <string>

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/net/reliability/ordering_header.hpp"
#include "caf/net/transport_worker.hpp"

using namespace caf;
using namespace caf::net;
using namespace std::literals::string_literals;

namespace {

struct fixture : test_coordinator_fixture<>, host_fixture {
  fixture() {
    messages.resize(10);
    for (int i = 9; i >= 0; --i) {
      binary_serializer sink(sys, messages[i]);
      reliability::ordering_header hdr;
      hdr.sequence = reliability::sequence_type(i);
      if (auto err = sink(hdr, uint8_t(i)))
        CAF_FAIL("could not serialize message" << CAF_ARG(i));
    }
  }

  std::vector<byte_buffer> messages;
};

class dummy_application {
public:
  dummy_application(bool& success) : success_(success), sequence_(0) {
    // nop
  }

  template <class Parent>
  error handle_data(Parent&, span<const byte> bytes) {
    uint8_t res = 0;
    binary_deserializer source{nullptr, bytes};
    CAF_CHECK_EQUAL(source(res), none);
    CAF_CHECK_EQUAL(res, sequence_);
    CAF_MESSAGE("got message with " << CAF_ARG(res));
    success_ = (res == 9);
    ++sequence_;
    return none;
  }

private:
  bool& success_;
  uint8_t sequence_;
};

template <class Application>
class dummy_transport {
public:
  using transport_type = dummy_transport;

  using application_type = Application;

  dummy_transport(actor_system& sys, Application application)
    : sys_(sys), worker_(std::move(application)) {
    // nop
  }

  void pass_message(byte_buffer& msg) {
    CAF_CHECK_EQUAL(worker_.handle_data(*this, make_span(msg)), none);
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

private:
  actor_system& sys_;

  transport_worker<Application> worker_;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(ordering_tests, fixture)

CAF_TEST(ordering) {
  using ordering_type = reliability::ordering<dummy_application>;
  bool success = false;
  dummy_transport<ordering_type> trans{
    sys, ordering_type{dummy_application{success}}};
  for (auto& message : messages)
    trans.pass_message(message);
  CAF_CHECK(success);
}

CAF_TEST_FIXTURE_SCOPE_END()
