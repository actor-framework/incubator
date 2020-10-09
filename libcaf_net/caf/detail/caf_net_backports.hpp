#pragma once

#include <type_traits>

#include "caf/config.hpp"

#if CAF_VERSION < 1800

#  include <array>
#  include <cstdint>
#  include <string>

#  include "caf/byte.hpp"
#  include "caf/detail/ieee_754.hpp"
#  include "caf/detail/net_export.hpp"
#  include "caf/span.hpp"
#  include "caf/string_view.hpp"

namespace caf {

using byte_span = span<byte>;

using const_byte_span = span<const byte>;

using byte_buffer = std::vector<byte>;

inline std::string to_string(string_view x) {
  return std::string{x.begin(), x.end()};
}

} // namespace caf

namespace caf::literals {

constexpr string_view operator""_sv(const char* cstr, size_t len) {
  return {cstr, len};
}

} // namespace caf::literals

namespace caf::detail {

CAF_NET_EXPORT std::string encode_base64(string_view str);

CAF_NET_EXPORT std::string encode_base64(span<const byte> bytes);

} // namespace caf::detail

namespace caf::hash {

class CAF_NET_EXPORT sha1 {
public:
  /// Hash size in bytes.
  static constexpr size_t hash_size = 20;

  /// Array type for storing a 160-bit hash.
  using result_type = std::array<byte, hash_size>;

  sha1() noexcept;

  template <class Integral>
  std::enable_if_t<std::is_integral<Integral>::value, bool>
  value(Integral x) noexcept {
    auto begin = reinterpret_cast<const uint8_t*>(&x);
    append(begin, begin + sizeof(Integral));
    return true;
  }

  bool value(bool x) noexcept {
    auto tmp = static_cast<uint8_t>(x);
    return value(tmp);
  }

  bool value(float x) noexcept {
    return value(detail::pack754(x));
  }

  bool value(double x) noexcept {
    return value(detail::pack754(x));
  }

  bool value(string_view x) noexcept {
    auto begin = reinterpret_cast<const uint8_t*>(x.data());
    append(begin, begin + x.size());
    return true;
  }

  bool value(span<const byte> x) noexcept {
    auto begin = reinterpret_cast<const uint8_t*>(x.data());
    append(begin, begin + x.size());
    return true;
  }

  /// Seals this SHA-1 context and returns the 160-bit message digest.
  result_type result() noexcept;

  /// Convenience function for computing a SHA-1 hash value for given arguments
  /// in one shot.
  static result_type compute(const std::string& str) noexcept {
    sha1 f;
    f.value(string_view{str});
    return f.result();
  }

private:
  bool append(const uint8_t* begin, const uint8_t* end) noexcept;

  void process_message_block();

  void pad_message();

  /// Stores whether `result()` has been called.
  bool sealed_ = 0;

  /// Stores the message digest so far.
  std::array<uint32_t, hash_size / 4> intermediate_;

  /// Stores the message length in bits.
  uint64_t length_ = 0;

  /// Stores the current index in `message_block_`.
  int_least16_t message_block_index_ = 0;

  /// Stores 512-bit message blocks.
  std::array<uint8_t, 64> message_block_;
};

} // namespace caf::hash

#else

#  include "caf/byte_buffer.hpp"
#  include "caf/byte_span.hpp"
#  include "caf/detail/encode_base64.hpp"
#  include "caf/hash/sha1.hpp"

#endif

namespace caf::detail {

template <class Inspector>
constexpr bool is_legacy_inspector
  = !std::is_same<typename Inspector::result_type, bool>::value;

constexpr string_view make_string_view(const char* first, const char* last) {
  return {first, static_cast<size_t>(std::distance(first, last))};
}

} // namespace caf::detail
