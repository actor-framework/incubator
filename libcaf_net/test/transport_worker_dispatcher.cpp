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

#define CAF_SUITE net.transport_worker_dispatcher

#include "caf/net/transport_worker_dispatcher.hpp"

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include "caf/byte_buffer.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/make_actor.hpp"
#include "caf/monitorable_actor.hpp"
#include "caf/node_id.hpp"
#include "caf/uri.hpp"

using namespace caf;
using namespace caf::net;

namespace {

using byte_buffer_ptr = std::shared_ptr<byte_buffer>;

class dummy_application {
public:
  dummy_application(byte_buffer_ptr rec_buf, uint8_t id)
    : rec_buf_(std::move(rec_buf)),
      id_(id){
        // nop
      };

  ~dummy_application() = default;

  template <class LowerLayerPtr>
  error init(socket_manager*, LowerLayerPtr, const settings&) {
    rec_buf_->push_back(static_cast<byte>(id_));
    return none;
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume(LowerLayerPtr, const_byte_span bytes, const_byte_span) {
    rec_buf_->push_back(static_cast<byte>(id_));
    return bytes.size();
  }

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr) {
    rec_buf_->push_back(static_cast<byte>(id_));
    return true;
  }

  template <class LowerLayerPtr>
  bool done_sending(LowerLayerPtr) {
    rec_buf_->push_back(static_cast<byte>(id_));
    return true;
  }

  template <class LowerLayerPtr>
  void abort(LowerLayerPtr, const error&) {
    rec_buf_->push_back(static_cast<byte>(id_));
  }

  uint8_t id() {
    return id_;
  }

private:
  byte_buffer_ptr rec_buf_;
  uint8_t id_;
};

struct dummy_application_factory {
public:
  using application_type = dummy_application;

  dummy_application_factory(byte_buffer_ptr buf)
    : buf_(std::move(buf)), application_cnt_(0) {
    // nop
  }

  dummy_application make() {
    return dummy_application{buf_, application_cnt_++};
  }

private:
  byte_buffer_ptr buf_;
  uint8_t application_cnt_;
};

struct testdata {
  testdata(uint8_t worker_id, node_id id, ip_endpoint ep)
    : worker_id(worker_id), nid(std::move(id)), ep(ep) {
    // nop
  }

  uint8_t worker_id;
  node_id nid;
  ip_endpoint ep;
};

// TODO: switch to std::operator""s when switching to C++14
ip_endpoint operator"" _ep(const char* cstr, size_t cstr_len) {
  ip_endpoint ep;
  string_view str(cstr, cstr_len);
  if (auto err = parse(str, ep))
    CAF_FAIL("parse returned error: " << err);
  return ep;
}

uri operator"" _u(const char* cstr, size_t cstr_len) {
  uri result;
  string_view str{cstr, cstr_len};
  auto err = parse(str, result);
  if (err)
    CAF_FAIL("error while parsing " << str << ": " << to_string(err));
  return result;
}

struct fixture : host_fixture {
  using dispatcher_type
    = transport_worker_dispatcher<dummy_application_factory, ip_endpoint>;

  fixture() : buf{std::make_shared<byte_buffer>()}, dispatcher{"test", buf} {
    add_new_workers();
  }

  bool contains(byte x) {
    return std::count(buf->begin(), buf->end(), x) > 0;
  }

  void add_new_workers() {
    for (auto& data : test_data) {
      auto worker = dispatcher.add_new_worker(this, data.nid, data.ep);
      if (!worker)
        CAF_FAIL("add_new_worker returned an error: " << worker.error());
    }
    buf->clear();
  }

  byte_buffer_ptr buf;
  dispatcher_type dispatcher;

  std::vector<testdata> test_data{
    {0, make_node_id("http:file"_u), "[::1]:1"_ep},
    {1, make_node_id("http:file?a=1&b=2"_u), "[fe80::2:34]:12345"_ep},
    {2, make_node_id("http:file#42"_u), "[1234::17]:4444"_ep},
    {3, make_node_id("http:file?a=1&b=2#42"_u), "[2332::1]:12"_ep},
  };
};

#define CHECK_CONSUME(testcase)                                                \
  CAF_CHECK_EQUAL(dispatcher.consume(this, const_byte_span{},                  \
                                     const_byte_span{}, testcase.ep),          \
                  none);                                                       \
  CAF_CHECK_EQUAL(buf->size(), 1u);                                            \
  CAF_CHECK_EQUAL(static_cast<byte>(testcase.worker_id), buf->at(0));          \
  buf->clear();

#define CHECK_DISPATCHING(function)                                            \
  function;                                                                    \
  CAF_CHECK(contains(byte(0)));                                                \
  CAF_CHECK(contains(byte(1)));                                                \
  CAF_CHECK(contains(byte(2)));                                                \
  CAF_CHECK(contains(byte(3)))

#define CHECK_UPPER_LAYER(no)                                                  \
  {                                                                            \
    auto& res = dispatcher.upper_layer(test_data.at(no).nid);                  \
    CAF_CHECK_EQUAL(res.id(), no);                                             \
    res = dispatcher.upper_layer(test_data.at(no).ep);                         \
    CAF_CHECK_EQUAL(res.id(), no);                                             \
  }

} // namespace

CAF_TEST_FIXTURE_SCOPE(transport_worker_dispatcher_test, fixture)

CAF_TEST(init) {
  const settings cfg;
  CAF_CHECK_EQUAL(dispatcher.init(nullptr, this, cfg), none);
}

CAF_TEST(upper layer) {
  CHECK_UPPER_LAYER(0);
  CHECK_UPPER_LAYER(1);
  CHECK_UPPER_LAYER(2);
  CHECK_UPPER_LAYER(3);
}

CAF_TEST(consume) {
  CHECK_CONSUME(test_data.at(0));
  CHECK_CONSUME(test_data.at(1));
  CHECK_CONSUME(test_data.at(2));
  CHECK_CONSUME(test_data.at(3));
}

CAF_TEST(prepare_send) {
  CHECK_DISPATCHING(dispatcher.prepare_send(this));
}

CAF_TEST(done_sending) {
  CHECK_DISPATCHING(dispatcher.done_sending(this));
}

CAF_TEST(abort) {
  CHECK_DISPATCHING(dispatcher.abort(this, make_error(sec::runtime_error)));
}

CAF_TEST_FIXTURE_SCOPE_END()
