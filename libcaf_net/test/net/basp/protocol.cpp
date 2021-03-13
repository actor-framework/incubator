// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#define CAF_SUITE net.basp.protocol

#include "caf/net/basp/protocol.hpp"

#include "net-test.hpp"

#include "caf/net/length_prefix_framing.hpp"

using namespace caf;

namespace {

using string_list = std::vector<std::string>;

class app {
public:
  // -- member types -----------------------------------------------------------

  using input_tag = net::basp::tag;

  // -- initialization ---------------------------------------------------------

  template <class LowerLayerPtr>
  error init(net::socket_manager*, LowerLayerPtr&&, const settings&) {
    return none;
  }

  // -- interface for the lower layer ------------------------------------------

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr&&) {
    return true;
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr&&) {
    return true;
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr&&, const error&) {
    log.emplace_back("abort");
  }

  bool heartbeat() {
    log.emplace_back("heartbeat");
    return true;
  }

  bool pong(const_byte_span) {
    log.emplace_back("pong");
    return true;
  }

  // -- member variables -------------------------------------------------------

  std::vector<std::string> log;
};

struct fixture {
  using basp_app = net::basp::protocol<app>;

  using framed = net::length_prefix_framing<basp_app>;

  mock_stream_transport<framed> uut;

  fixture() {
    if (auto err = uut.init())
      CAF_FAIL("uut.init() failed: " << err);
  }

  auto& log() {
    return uut.upper_layer.upper_layer().upper_layer().log;
  }

  template <class... Ts>
  void virtual_send(const Ts&... xs) {
    byte_buffer buf;
    binary_serializer sink{nullptr, buf};
    sink.skip(sizeof(uint32_t));
    if (!(sink.apply(xs), ...))
      CAF_FAIL("serialization failed: " << sink.get_error());
    sink.seek(0);
    auto msg_size = buf.size() - sizeof(uint32_t);
    if (!sink.apply(static_cast<uint32_t>(msg_size)))
      CAF_FAIL("failed to write message size");
    uut.push(buf);
    auto consumed = uut.handle_input();
    if (consumed != static_cast<ptrdiff_t>(buf.size()))
      CAF_FAIL("UUT failed to handle input");
  }
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(protocol_tests, fixture)

SCENARIO("BASP processes heartbeats") {
  GIVEN("a BASP layer") {
    WHEN("receiving a heartbeat message") {
      virtual_send(net::basp::message_type::heartbeat);
      THEN("BASP calls heartbeat() on the application") {
        CHECK_EQ(log(), string_list({"heartbeat"}));
      }
    }
  }
}

CAF_TEST_FIXTURE_SCOPE_END()
