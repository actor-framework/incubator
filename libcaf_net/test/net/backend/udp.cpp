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

#define CAF_SUITE net.backend.udp

#include "caf/net/backend/udp.hpp"

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include <string>
#include <thread>

#include "caf/actor_system_config.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/socket_guard.hpp"
#include "caf/uri.hpp"

using namespace caf;
using namespace caf::net;
using namespace std::literals::string_literals;

namespace {

behavior dummy_actor(event_based_actor*) {
  return {
    // nop
  };
}

struct config : actor_system_config {
  config() {
    ip_endpoint ep;
    CAF_REQUIRE_EQUAL(detail::parse("127.0.0.1:0"s, ep), none);
    auto ret = unbox(make_udp_datagram_socket(ep));
    sock = ret.first;
    port = ret.second;
    this_node_str = "udp://127.0.0.1:"s + std::to_string(port);
    auto this_node = unbox(make_uri(this_node_str));
    CAF_MESSAGE("datagram_socket spawned on " << CAF_ARG(this_node) << " "
                                              << CAF_ARG(sock.id));
    put(content, "middleman.this-node", this_node);
    load<middleman, backend::udp>();
  }

  udp_datagram_socket sock;
  uint16_t port;
  std::string this_node_str;
};

class planet : public test_coordinator_fixture<config> {
public:
  planet() : mm(this->sys.network_manager()), mpx(mm.mpx()) {
    mpx->set_thread_id();
    backend()->emplace(cfg.sock, cfg.port);
  }

  std::string locator_str() {
    return cfg.this_node_str;
  }

  net::backend::udp* backend() {
    return dynamic_cast<net::backend::udp*>(mm.backend("udp"));
  }

  net::middleman& mm;
  multiplexer_ptr mpx;
};

struct fixture : host_fixture {
  fixture() {
    // nop
  }

  bool handle_io_event() {
    return mars.mpx->poll_once(false) || earth.mpx->poll_once(false);
  }

  planet earth;
  planet mars;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(udp_backend_tests, fixture)

CAF_TEST(resolve) {
  auto dummy = earth.sys.spawn(dummy_actor);
  auto path = "dummy"s;
  CAF_MESSAGE("publishing actor " << CAF_ARG(path));
  earth.mm.publish(dummy, path);
  auto locator = unbox(make_uri(earth.locator_str() + "/name/" + path));
  CAF_MESSAGE("resolving " << CAF_ARG(locator));
  mars.mm.resolve(locator, mars.self);
  while (handle_io_event())
    ;
  mars.self->receive(
    [](strong_actor_ptr& ptr, const std::set<std::string>&) {
      CAF_MESSAGE("resolved actor!");
      CAF_CHECK_NOT_EQUAL(ptr, nullptr);
    },
    [](const error& err) {
      CAF_FAIL("got error while resolving: " << CAF_ARG(err));
    },
    after(std::chrono::seconds(0)) >>
      [] { CAF_FAIL("manager did not respond with a proxy."); });
}

CAF_TEST_FIXTURE_SCOPE_END()
