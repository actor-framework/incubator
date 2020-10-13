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

#define CAF_SUITE net.slicing

#include "caf/net/slicing.hpp"

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include <numeric>

#include "caf/actor_clock.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/byte_span.hpp"
#include "caf/net/slicing_header.hpp"
#include "caf/tag/message_oriented.hpp"

using namespace caf;
using namespace caf::net;
using namespace std::literals::string_literals;

namespace {

using byte_buffer_ptr = std::shared_ptr<byte_buffer>;

#define CHECK_SLICE_HEADER(buf, expected_num, expected_of)                     \
  {                                                                            \
    slicing_header header{0, 0};                                               \
    binary_deserializer source{sys, buf};                                      \
    if (!source.apply_object(header))                                          \
      CAF_FAIL("could not deserialize header"                                  \
               << CAF_ARG2("error", source.get_error()));                      \
    CAF_CHECK_EQUAL(header.no, expected_num);                                  \
    CAF_CHECK_EQUAL(header.of, expected_of);                                   \
  }

class dummy_application {
public:
  dummy_application(byte_buffer_ptr send_buf, byte_buffer_ptr recv_buf)
    : send_buf_(std::move(send_buf)), recv_buf_(std::move(recv_buf)) {
    // nop
  }

  template <class LowerLayerPtr>
  error init(socket_manager*, LowerLayerPtr, const settings&) {
    return none;
  }

  template <class LowerLayerPtr>
  bool prepare_send(LowerLayerPtr down) {
    down->begin_message();
    auto& buf = down->message_buffer();
    buf.insert(buf.end(), send_buf_->begin(), send_buf_->end());
    down->end_message();
    return true;
  }

  template <class LowerLayerPtr>
  ptrdiff_t consume(LowerLayerPtr, const_byte_span bytes) {
    recv_buf_->insert(recv_buf_->end(), bytes.begin(), bytes.end());
    return bytes.size();
  }

  byte_buffer_ptr send_buf_;

  byte_buffer_ptr recv_buf_;
};

struct fixture : test_coordinator_fixture<>, host_fixture {
  fixture()
    : send_buf(std::make_shared<byte_buffer>()),
      recv_buf(std::make_shared<byte_buffer>()) {
    // nop
  }

  void begin_datagram() {
    datagram_begin_ = write_buffer.size();
  }

  byte_buffer& datagram_buffer() {
    return write_buffer;
  }

  void end_datagram() {
    datagram_sizes.emplace_back(write_buffer.size() - datagram_begin_);
  }

  static void abort_reason(error reason) {
    CAF_FAIL("abort reason called with " << CAF_ARG(reason));
  }

  byte_buffer write_buffer;

  std::vector<size_t> datagram_sizes;

  byte_buffer_ptr send_buf;

  byte_buffer_ptr recv_buf;

private:
  size_t datagram_begin_ = 0;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(ordering_tests, fixture)

CAF_TEST(slicing) {
  using slicing_type = slicing<dummy_application>;
  slicing_type slicing_layer{send_buf, recv_buf};
  send_buf->resize(10000);
  slicing_layer.prepare_send(this);
  CAF_CHECK_EQUAL(datagram_sizes.size(), 7u);
  auto data = make_span(write_buffer);
  CAF_CHECK_EQUAL(data.size(), send_buf->size() + 7 * slicing_header_size);
  for (size_t i = 1; i <= datagram_sizes.size(); ++i) {
    auto current_size = datagram_sizes.at(i - 1);
    CAF_CHECK_LESS_OR_EQUAL(current_size, slicing_type::fragment_size);
    CHECK_SLICE_HEADER(make_span(data.data(), current_size), i, 7u);
    data = data.subspan(current_size);
  }
}

CAF_TEST(slicing multiple messages) {
  using slicing_type = slicing<dummy_application>;
  slicing_type slicing_layer{send_buf, recv_buf};
  send_buf->resize(5000);
  slicing_layer.prepare_send(this);
  slicing_layer.prepare_send(this);
  CAF_REQUIRE(datagram_sizes.size(), 8u);
  auto data = make_span(write_buffer);
  CAF_CHECK_EQUAL(data.size(), 2 * send_buf->size() + 8 * slicing_header_size);
  for (size_t i = 1; i <= 4u; ++i) {
    auto current_size = datagram_sizes.at(i - 1);
    CHECK_SLICE_HEADER(make_span(data.data(), current_size), i, 4u);
    data = data.subspan(current_size);
  }
  for (size_t i = 1; i <= 4u; ++i) {
    auto current_size = datagram_sizes.at(i + 3);
    CHECK_SLICE_HEADER(make_span(data.data(), current_size), i, 4u);
    data = data.subspan(current_size);
  }
}

CAF_TEST(no slicing) {
  using slicing_type = slicing<dummy_application>;
  slicing_type slicing_layer{send_buf, recv_buf};
  send_buf->resize(10);
  slicing_layer.prepare_send(this);
  CAF_CHECK_EQUAL(datagram_sizes.size(), 1u);
  CAF_CHECK_EQUAL(write_buffer.size(), send_buf->size() + slicing_header_size);
  auto current_size = datagram_sizes.front();
  CAF_CHECK_LESS_OR_EQUAL(current_size, slicing_type::fragment_size);
  CHECK_SLICE_HEADER(make_span(write_buffer.data(), current_size), 1u, 1u);
}

CAF_TEST(no slicing multiple buffers) {
  using slicing_type = slicing<dummy_application>;
  slicing_type slicing_layer{send_buf, recv_buf};
  send_buf->resize(10);
  slicing_layer.prepare_send(this);
  slicing_layer.prepare_send(this);
  CAF_CHECK_EQUAL(datagram_sizes.size(), 2u);
  CAF_CHECK_EQUAL(write_buffer.size(),
                  2 * send_buf->size() + 2 * slicing_header_size);
  auto data = make_span(write_buffer);
  auto current_size = datagram_sizes.at(0);
  CHECK_SLICE_HEADER(make_span(data.data(), current_size), 1u, 1u);
  data = data.subspan(current_size);
  current_size = datagram_sizes.at(1);
  CHECK_SLICE_HEADER(make_span(data.data(), current_size), 1u, 1u);
}

CAF_TEST(reassembling) {
  using slicing_type = slicing<dummy_application>;
  slicing_type slicing_layer{send_buf, recv_buf};
  uint8_t current_value = 0;
  for (size_t i = 1; i <= 7; ++i) {
    byte_buffer buf;
    binary_serializer sink{sys, buf};
    CAF_CHECK(sink.apply_object(slicing_header{i, 7}));
    for (int i = 0; i < 10; ++i)
      buf.emplace_back(byte{current_value++});
    slicing_layer.consume(this, buf);
    if (i < 7)
      CAF_CHECK(recv_buf->empty());
  }
  CAF_CHECK_EQUAL(recv_buf->size(), 70u);
}

CAF_TEST_FIXTURE_SCOPE_END()
