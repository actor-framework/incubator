// clang-format off
// DO NOT EDIT: this file is auto-generated by caf-generate-enum-strings.
// Run the target update-enum-strings if this file is out of sync.
#include "caf/config.hpp"
#include "caf/string_view.hpp"

CAF_PUSH_DEPRECATED_WARNING

#include "caf/net/basp/ec.hpp"

#include <string>

namespace caf {
namespace net {
namespace basp {

std::string to_string(ec x) {
  switch(x) {
    default:
      return "???";
    case ec::invalid_magic_number:
      return "invalid_magic_number";
    case ec::unexpected_number_of_bytes:
      return "unexpected_number_of_bytes";
    case ec::unexpected_payload:
      return "unexpected_payload";
    case ec::missing_payload:
      return "missing_payload";
    case ec::illegal_state:
      return "illegal_state";
    case ec::invalid_handshake:
      return "invalid_handshake";
    case ec::missing_handshake:
      return "missing_handshake";
    case ec::unexpected_handshake:
      return "unexpected_handshake";
    case ec::version_mismatch:
      return "version_mismatch";
    case ec::unimplemented:
      return "unimplemented";
    case ec::app_identifiers_mismatch:
      return "app_identifiers_mismatch";
    case ec::invalid_payload:
      return "invalid_payload";
    case ec::invalid_scheme:
      return "invalid_scheme";
    case ec::invalid_locator:
      return "invalid_locator";
  };
}

bool from_string(string_view in, ec& out) {
  if (in == "invalid_magic_number") {
    out = ec::invalid_magic_number;
    return true;
  } else if (in == "unexpected_number_of_bytes") {
    out = ec::unexpected_number_of_bytes;
    return true;
  } else if (in == "unexpected_payload") {
    out = ec::unexpected_payload;
    return true;
  } else if (in == "missing_payload") {
    out = ec::missing_payload;
    return true;
  } else if (in == "illegal_state") {
    out = ec::illegal_state;
    return true;
  } else if (in == "invalid_handshake") {
    out = ec::invalid_handshake;
    return true;
  } else if (in == "missing_handshake") {
    out = ec::missing_handshake;
    return true;
  } else if (in == "unexpected_handshake") {
    out = ec::unexpected_handshake;
    return true;
  } else if (in == "version_mismatch") {
    out = ec::version_mismatch;
    return true;
  } else if (in == "unimplemented") {
    out = ec::unimplemented;
    return true;
  } else if (in == "app_identifiers_mismatch") {
    out = ec::app_identifiers_mismatch;
    return true;
  } else if (in == "invalid_payload") {
    out = ec::invalid_payload;
    return true;
  } else if (in == "invalid_scheme") {
    out = ec::invalid_scheme;
    return true;
  } else if (in == "invalid_locator") {
    out = ec::invalid_locator;
    return true;
  } else {
    return false;
  }
}

bool from_integer(std::underlying_type_t<ec> in,
                  ec& out) {
  auto result = static_cast<ec>(in);
  switch(result) {
    default:
      return false;
    case ec::invalid_magic_number:
    case ec::unexpected_number_of_bytes:
    case ec::unexpected_payload:
    case ec::missing_payload:
    case ec::illegal_state:
    case ec::invalid_handshake:
    case ec::missing_handshake:
    case ec::unexpected_handshake:
    case ec::version_mismatch:
    case ec::unimplemented:
    case ec::app_identifiers_mismatch:
    case ec::invalid_payload:
    case ec::invalid_scheme:
    case ec::invalid_locator:
      out = result;
      return true;
  };
}

} // namespace basp
} // namespace net
} // namespace caf

CAF_POP_WARNINGS
