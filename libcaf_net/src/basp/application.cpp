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

#include "caf/defaults.hpp"
#include "caf/detail/parse.hpp"
#include "caf/logger.hpp"
#include "caf/net/basp/actor_proxy_impl.hpp"
#include "caf/no_stages.hpp"
#include "caf/string_algorithms.hpp"

namespace caf::net::basp {

application::application(proxy_registry& proxies) : proxies_(proxies) {
  // nop
}

void application::resolve(string_view path, const actor& listener) {
  CAF_LOG_TRACE(CAF_ARG(path) << CAF_ARG(listener));
  anon_send(self_.as_actor(),
            resolve_request_msg{to_string(path), std::move(listener)});
}

strong_actor_ptr application::make_proxy(const node_id& nid,
                                         const actor_id& aid) {
  CAF_LOG_TRACE(CAF_ARG(nid) << CAF_ARG(aid));
  using impl_type = actor_proxy_impl;
  using handle_type = strong_actor_ptr;
  actor_config cfg;
  return make_actor<impl_type, handle_type>(aid, nid, system_, cfg,
                                            self_.as_actor());
}

strong_actor_ptr application::resolve_local_path(string_view path) {
  CAF_LOG_TRACE(CAF_ARG(path));
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
    std::string name;
    if (auto err = detail::parse(path, name))
      return nullptr;
    return system().registry().get(name);
  }
  return nullptr;
}

} // namespace caf::net::basp
