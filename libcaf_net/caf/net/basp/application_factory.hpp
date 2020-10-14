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

#pragma once

#include "caf/detail/net_export.hpp"
#include "caf/error.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/net/basp/application.hpp"
#include "caf/net/slicing.hpp"
#include "caf/net/transport_worker.hpp"
#include "caf/proxy_registry.hpp"
#include "caf/tag/datagram_oriented.hpp"

namespace caf::net::basp {

/// Factory for basp::application.
class CAF_NET_EXPORT application_factory {
public:
  using input_tag = tag::datagram_oriented;

  using application_type = slicing<basp::application>;

  using worker_type = transport_worker<application_type, ip_endpoint>;

  using worker_ptr = std::shared_ptr<worker_type>;

  application_factory(proxy_registry& proxies) : proxies_(proxies) {
    // nop
  }

  template <class LowerLayerPtr>
  error init(LowerLayerPtr) {
    return none;
  }

  worker_ptr make(ip_endpoint id) const {
    return std::make_shared<worker_type>(id, proxies_);
  }

private:
  proxy_registry& proxies_;
};

} // namespace caf::net::basp
