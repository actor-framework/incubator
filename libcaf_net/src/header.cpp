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

#include "caf/net/basp/header.hpp"

#include <cstring>

#include "caf/byte.hpp"
#include "caf/detail/network_order.hpp"
#include "caf/span.hpp"

namespace caf {
namespace net {
namespace basp {

int header::compare(header other) const noexcept {
  auto x = to_bytes(*this);
  auto y = to_bytes(other);
  return memcmp(x.data(), y.data(), header_size);
}

header header::from_bytes(span<const byte> bytes) {
  CAF_ASSERT(bytes.size() >= header_size);
  header result;
  auto ptr = bytes.data();
  result.type = *reinterpret_cast<const message_type*>(ptr);
  auto payload_len = *reinterpret_cast<const uint32_t*>(ptr + 1);
  result.payload_len = detail::from_network_order(payload_len);
  auto operation_data = *reinterpret_cast<const uint64_t*>(ptr + 5);
  result.operation_data = detail::from_network_order(operation_data);
  return result;
}

std::array<byte, header_size> to_bytes(header x) {
  std::array<byte, header_size> result;
  auto ptr = result.data();
  *ptr = static_cast<byte>(x.type);
  auto payload_len = detail::to_network_order(x.payload_len);
  memcpy(ptr + 1, &payload_len, sizeof(payload_len));
  auto operation_data = detail::to_network_order(x.operation_data);
  memcpy(ptr + 5, &operation_data, sizeof(operation_data));
  return result;
}

void to_bytes(header x, std::vector<byte>& buf) {
  buf.resize(header_size);
  auto ptr = buf.data();
  *ptr = static_cast<byte>(x.type);
  auto payload_len = detail::to_network_order(x.payload_len);
  memcpy(ptr + 1, &payload_len, sizeof(payload_len));
  auto operation_data = detail::to_network_order(x.operation_data);
  memcpy(ptr + 5, &operation_data, sizeof(operation_data));
}

} // namespace basp
} // namespace net
} // namespace caf
