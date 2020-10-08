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

#pragma once

#include "caf/net/fwd.hpp"

namespace caf::net::basp {

struct resolve_request_msg {
  std::string path;
  actor listener;
};

template <class Inspector>
bool inspect(Inspector& f, resolve_request_msg& x) {
  return f.object(x).fields(f.field("path", x.path),
                            f.field("listener", x.listener));
}

struct new_proxy_msg {
  actor_id id;
};

template <class Inspector>
bool inspect(Inspector& f, new_proxy_msg& x) {
  return f.object(x).fields(f.field("id", x.id));
}

struct local_actor_down_msg {
  actor_id id;
  error reason;
};

template <class Inspector>
bool inspect(Inspector& f, local_actor_down_msg& x) {
  return f.object(x).fields(f.field("id", x.id), f.field("reason", x.reason));
}

struct timeout_msg {
  std::string type;
  uint64_t id;
};

template <class Inspector>
bool inspect(Inspector& f, timeout_msg& x) {
  return f.object(x).fields(f.field("type", x.type), f.field("id", x.id));
}

} // namespace caf::net::basp
