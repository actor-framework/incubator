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

#include "caf/net/middleman.hpp"

#include "caf/actor_system_config.hpp"
#include "caf/detail/set_thread_name.hpp"
#include "caf/expected.hpp"
#include "caf/init_global_meta_objects.hpp"
#include "caf/net/middleman_backend.hpp"
#include "caf/raise_error.hpp"
#include "caf/sec.hpp"
#include "caf/send.hpp"
#include "caf/uri.hpp"

namespace caf::net {

void middleman::init_global_meta_objects() {
#if CAF_VERSION >= 1800
  caf::init_global_meta_objects<id_block::net_module>();
#endif
}

middleman::middleman(actor_system& sys) : sys_(sys), mpx_(this) {
  // nop
}

middleman::~middleman() {
  // nop
}

void middleman::start() {
  if (!get_or(config(), "caf.middleman.manual-multiplexing", false)) {
    mpx_thread_ = std::thread{[this] {
      CAF_SET_LOGGER_SYS(&sys_);
      detail::set_thread_name("caf.net.mpx");
      sys_.thread_started();
      mpx_.set_thread_id();
      mpx_.run();
      sys_.thread_terminates();
    }};
  }
}

void middleman::stop() {
  for (const auto& backend : backends_)
    backend->stop();
  mpx_.shutdown();
  if (mpx_thread_.joinable())
    mpx_thread_.join();
  else
    mpx_.run();
}

void middleman::init(actor_system_config& cfg) {
  if (auto err = mpx_.init()) {
    CAF_LOG_ERROR("mpx_.init() failed: " << err);
    CAF_RAISE_ERROR("mpx_.init() failed");
  }
  if (auto node_uri = get_if<uri>(&cfg, "caf.middleman.this-node")) {
    auto this_node = make_node_id(std::move(*node_uri));
    sys_.node_.swap(this_node);
  } else {
    // CAF_RAISE_ERROR("no valid entry for caf.middleman.this-node found");
  }
  for (auto& backend : backends_)
    if (auto err = backend->init()) {
      CAF_LOG_ERROR("failed to initialize backend: " << err);
      CAF_RAISE_ERROR("failed to initialize backend");
    }
}

middleman::module::id_t middleman::id() const {
  return module::network_manager;
}

void* middleman::subtype_ptr() {
  return this;
}

void middleman::add_module_options(actor_system_config&) {
  /*
  config_option_adder{cfg.custom_options(), "caf.middleman"}
    .add<std::vector<std::string>>("app-identifiers",
                                   "valid application identifiers of this node")
    .add<uri>("this-node", "locator of this CAF node")
    .add<size_t>("max-consecutive-reads",
                 "max. number of consecutive reads per broker")
    .add<bool>("manual-multiplexing",
               "disables background activity of the multiplexer")
    .add<size_t>("workers", "number of deserialization workers")
    .add<std::string>("network-backend", "legacy option");
  */
}

void middleman::resolve(const uri&, const actor& listener) {
  anon_send(listener, error{sec::feature_disabled});
}

middleman_backend* middleman::backend(string_view) const noexcept {
  return nullptr;
}

expected<uint16_t> middleman::port(string_view) const {
  return sec::feature_disabled;
}

} // namespace caf::net
