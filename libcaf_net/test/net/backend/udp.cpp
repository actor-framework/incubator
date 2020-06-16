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

#define CAF_SUITE net.backend.udp

#include "caf/net/backend/udp.hpp"

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include <string>
#include <thread>

#include "caf/actor_system_config.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/net/ip.hpp"
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

struct earth_node {
  uri operator()() {
    return unbox(make_uri("udp://127.0.0.1:12345"));
  }

  uint16_t port() {
    return 12345;
  }
};

struct mars_node {
  uri operator()() {
    return unbox(make_uri("udp://127.0.0.1:12346"));
  }

  uint16_t port() {
    return 12346;
  }
};

template <class Node>
struct config : actor_system_config {
  config() {
    Node this_node;
    put(content, "middleman.udp-port", this_node.port());
    put(content, "middleman.this-node", this_node());
    load<middleman, backend::udp>();
  }
};

class planet_driver {
public:
  virtual ~planet_driver() = default;

  virtual bool handle_io_event() = 0;
};

template <class Node>
class planet : public test_coordinator_fixture<config<Node>> {
public:
  planet(planet_driver& driver)
    : mm(this->sys.network_manager()), mpx(mm.mpx()), driver_(driver) {
    mpx->set_thread_id();
  }

  node_id id() const {
    return this->sys.node();
  }

  bool handle_io_event() override {
    return driver_.handle_io_event();
  }

  uint16_t port() {
    return mm.port("udp");
  }

  uri locator() {
  }

  net::middleman& mm;
  multiplexer_ptr mpx;

private:
  planet_driver& driver_;
};

struct fixture : host_fixture, planet_driver {
  fixture() : earth(*this), mars(*this) {
    earth.run();
    mars.run();
    // CAF_REQUIRE_EQUAL(earth.mpx->num_socket_managers(), 2);
    // CAF_REQUIRE_EQUAL(mars.mpx->num_socket_managers(), 2);
  }

  bool handle_io_event() override {
    return earth.mpx->poll_once(false) || mars.mpx->poll_once(false);
  }

  void run() {
    earth.run();
  }

  planet<earth_node> earth;
  planet<mars_node> mars;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(udp_backend_tests, fixture)

CAF_TEST(worker creation) {
  // CAF_CHECK(earth.mm.backend("udp").emplace(make_node_id()));
}

CAF_TEST(publish) {
  auto dummy = earth.sys.spawn(dummy_actor);
  auto path = "dummy"s;
  CAF_MESSAGE("publishing actor " << CAF_ARG(path));
  earth.mm.publish(dummy, path);
  CAF_MESSAGE("check registry for " << CAF_ARG(path));
  CAF_CHECK_NOT_EQUAL(earth.sys.registry().get(path), nullptr);
}

CAF_TEST(resolve) {
  // nop
}

CAF_TEST_FIXTURE_SCOPE_END()
