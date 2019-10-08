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

#include "caf/net/basp/application.hpp"

#include <vector>

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/byte.hpp"
#include "caf/defaults.hpp"
#include "caf/detail/network_order.hpp"
#include "caf/detail/parse.hpp"
#include "caf/error.hpp"
#include "caf/expected.hpp"
#include "caf/net/basp/constants.hpp"
#include "caf/net/basp/ec.hpp"
#include "caf/no_stages.hpp"
#include "caf/none.hpp"
#include "caf/sec.hpp"
#include "caf/serializer_impl.hpp"
#include "caf/string_algorithms.hpp"
#include "caf/type_erased_tuple.hpp"

namespace caf {
namespace net {
namespace basp {

application::application(proxy_registry_ptr proxies)
  : proxies_(std::move(proxies)) {
  // nop
}

expected<std::vector<byte>> application::serialize(actor_system& sys,
                                                   const type_erased_tuple& x) {
  std::vector<byte> result;
  serializer_impl<std::vector<byte>> sink{sys, result};
  if (auto err = message::save(sink, x))
    return err;
  return result;
}

strong_actor_ptr application::resolve_local_path(string_view path) {
  // We currently support two path formats: `id/<actor_id>` and `name/<atom>`.
  static constexpr string_view id_prefix = "id/";
  if (starts_with(path, id_prefix)) {
    path.remove_prefix(id_prefix.size());
    actor_id aid;
    if (auto err = detail::parse(path, aid))
      return nullptr;
    return system().registry().get(aid);
  }
  static constexpr string_view name_prefix = "name/";
  if (starts_with(path, name_prefix)) {
    path.remove_prefix(name_prefix.size());
    atom_value name;
    if (auto err = detail::parse(path, name))
      return nullptr;
    return system().registry().get(name);
  }
  return nullptr;
}

error application::handle_handshake(write_packet_callback&, header hdr,
                                    byte_span payload) {
  if (hdr.type != message_type::handshake)
    return ec::missing_handshake;
  if (hdr.operation_data != version)
    return ec::version_mismatch;
  node_id peer_id;
  std::vector<std::string> app_ids;
  binary_deserializer source{system(), payload};
  if (auto err = source(peer_id, app_ids))
    return err;
  if (!peer_id || app_ids.empty())
    return ec::invalid_handshake;
  auto ids = get_or(system().config(), "middleman.app-identifiers",
                    defaults::middleman::app_identifiers);
  auto predicate = [=](const std::string& x) {
    return std::find(ids.begin(), ids.end(), x) != ids.end();
  };
  if (std::none_of(app_ids.begin(), app_ids.end(), predicate))
    return ec::app_identifiers_mismatch;
  peer_id_ = std::move(peer_id);
  state_ = connection_state::await_header;
  return none;
}

error application::handle_actor_message(write_packet_callback&, header hdr,
                                        byte_span payload) {
  // Deserialize payload.
  actor_id src_id;
  node_id src_node;
  actor_id dst_id;
  std::vector<strong_actor_ptr> fwd_stack;
  message content;
  binary_deserializer source{system(), payload};
  if (auto err = source(src_node, src_id, dst_id, fwd_stack, content))
    return err;
  // Sanity checks.
  if (dst_id == 0)
    return ec::invalid_payload;
  // Try to fetch the receiver.
  auto dst_hdl = system().registry().get(dst_id);
  if (dst_hdl == nullptr) {
    CAF_LOG_DEBUG("no actor found for given ID, drop message");
    return caf::none;
  }
  // Try to fetch the sender.
  strong_actor_ptr src_hdl;
  if (src_node != none && src_id != 0)
    src_hdl = proxies_->get_or_put(src_node, src_id);
  // Ship the message.
  auto ptr = make_mailbox_element(std::move(src_hdl),
                                  make_message_id(hdr.operation_data),
                                  std::move(fwd_stack), std::move(content));
  dst_hdl->get()->enqueue(std::move(ptr), nullptr);
  return none;
}

error application::handle_resolve_response(write_packet_callback&, header hdr,
                                           byte_span payload) {
  CAF_ASSERT(hdr.type == message_type::resolve_response);
  auto i = pending_resolves_.find(hdr.operation_data);
  if (i == pending_resolves_.end()) {
    CAF_LOG_ERROR("received unknown ID in resolve_response message");
    return none;
  }
  auto guard = detail::make_scope_guard([&] {
    if (i->second.pending())
      i->second.deliver(sec::remote_lookup_failed);
    pending_resolves_.erase(i);
  });
  actor_id aid;
  std::set<std::string> ifs;
  binary_deserializer source{system(), payload};
  if (auto err = source(aid, ifs))
    return err;
  if (aid == 0) {
    i->second.deliver(strong_actor_ptr{nullptr}, std::move(ifs));
    return none;
  }
  i->second.deliver(proxies_->get_or_put(peer_id_, aid), std::move(ifs));
  return none;
}

error application::generate_handshake(std::vector<byte>& buf) {
  serializer_impl<buffer_type> sink{system(), buf};
  return sink(system().node(),
              get_or(system().config(), "middleman.app-identifiers",
                     defaults::middleman::app_identifiers));
}

} // namespace basp
} // namespace net
} // namespace caf
