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

#include "caf/detail/net_export.hpp"
#include "caf/error.hpp"
#include "caf/net/basp/application.hpp"
#include "caf/net/datagram_adaptor.hpp"
#include "caf/net/reliability/delivery.hpp"
#include "caf/net/reliability/ordering.hpp"
#include "caf/proxy_registry.hpp"

namespace caf::net::basp {

/// Factory for datagram oriented basp::applications.
/// @relates transport_worker_dispatcher
class CAF_NET_EXPORT datagram_application_factory {
public:
  using ordering_type = reliability::ordering<basp::application>;

  using delivery_type = reliability::delivery<ordering_type>;

  using application_type = delivery_type;

  datagram_application_factory(proxy_registry& proxies) : proxies_(proxies) {
    // nop
  }

  template <class Parent>
  error init(Parent&) {
    return none;
  }

  application_type make() const {
    return delivery_type{ordering_type{basp::application{proxies_}}};
  }

private:
  proxy_registry& proxies_;
};

} // namespace caf::net::basp
