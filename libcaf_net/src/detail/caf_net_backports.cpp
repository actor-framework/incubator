#include "caf/detail/caf_net_backports.hpp"

#include <cstring>

#include "caf/sec.hpp"

namespace caf::detail {

namespace {

constexpr const char base64_tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "abcdefghijklmnopqrstuvwxyz"
                                    "0123456789+/";

} // namespace

std::string encode_base64(string_view str) {
  auto bytes = make_span(reinterpret_cast<const byte*>(str.data()), str.size());
  return encode_base64(bytes);
}

std::string encode_base64(span<const byte> bytes) {
  std::string result;
  // Consumes three characters from input at once.
  auto consume = [&result](const byte* i) {
    auto at = [i](size_t index) { return to_integer<int>(i[index]); };
    int buf[] = {
      (at(0) & 0xfc) >> 2,
      ((at(0) & 0x03) << 4) + ((at(1) & 0xf0) >> 4),
      ((at(1) & 0x0f) << 2) + ((at(2) & 0xc0) >> 6),
      at(2) & 0x3f,
    };
    for (auto x : buf)
      result += base64_tbl[x];
  };
  // Iterate the input in chunks of three bytes.
  auto i = bytes.begin();
  for (; std::distance(i, bytes.end()) >= 3; i += 3)
    consume(i);
  if (i != bytes.end()) {
    // Pad input with zeros.
    byte buf[] = {byte{0}, byte{0}, byte{0}};
    std::copy(i, bytes.end(), buf);
    consume(buf);
    // Override padded bytes (garbage) with '='.
    for (auto j = result.end() - (3 - (bytes.size() % 3)); j != result.end();
         ++j)
      *j = '=';
  }
  return result;
}

} // namespace caf::detail

#define SHA1CircularShift(bits, word)                                          \
  (((word) << (bits)) | ((word) >> (32 - (bits))))

namespace caf::hash {

sha1::sha1() noexcept {
  intermediate_[0] = 0x67452301;
  intermediate_[1] = 0xEFCDAB89;
  intermediate_[2] = 0x98BADCFE;
  intermediate_[3] = 0x10325476;
  intermediate_[4] = 0xC3D2E1F0;
  memset(message_block_.data(), 0, message_block_.size());
}

sha1::result_type sha1::result() noexcept {
  if (!sealed_) {
    pad_message();
    memset(message_block_.data(), 0, message_block_.size());
    length_ = 0;
    sealed_ = true;
  }
  std::array<byte, sha1::hash_size> buf;
  for (size_t i = 0; i < hash_size; ++i) {
    auto tmp = intermediate_[i >> 2] >> 8 * (3 - (i & 0x03));
    buf[i] = static_cast<byte>(tmp);
  }
  return buf;
}

bool sha1::append(const uint8_t* begin, const uint8_t* end) noexcept {
  if (sealed_) {
    return false;
  }
  for (auto i = begin; i != end; ++i) {
    if (length_ >= std::numeric_limits<uint64_t>::max() - 8) {
      return false;
    }
    message_block_[message_block_index_++] = *i;
    length_ += 8;
    if (message_block_index_ == 64)
      process_message_block();
  }
  return true;
}

void sha1::process_message_block() {
  const uint32_t K[] = {
    0x5A827999,
    0x6ED9EBA1,
    0x8F1BBCDC,
    0xCA62C1D6,
  };
  uint32_t W[80];         // Word sequence.
  uint32_t A, B, C, D, E; // Word buffers.
  for (auto t = 0; t < 16; t++) {
    W[t] = message_block_[t * 4] << 24;
    W[t] |= message_block_[t * 4 + 1] << 16;
    W[t] |= message_block_[t * 4 + 2] << 8;
    W[t] |= message_block_[t * 4 + 3];
  }

  for (auto t = 16; t < 80; t++) {
    W[t] = SHA1CircularShift(1, W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16]);
  }
  A = intermediate_[0];
  B = intermediate_[1];
  C = intermediate_[2];
  D = intermediate_[3];
  E = intermediate_[4];
  for (auto t = 0; t < 20; t++) {
    auto tmp = SHA1CircularShift(5, A) + ((B & C) | ((~B) & D)) + E + W[t]
               + K[0];
    E = D;
    D = C;
    C = SHA1CircularShift(30, B);
    B = A;
    A = tmp;
  }
  for (auto t = 20; t < 40; t++) {
    auto tmp = SHA1CircularShift(5, A) + (B ^ C ^ D) + E + W[t] + K[1];
    E = D;
    D = C;
    C = SHA1CircularShift(30, B);
    B = A;
    A = tmp;
  }
  for (auto t = 40; t < 60; t++) {
    auto tmp = SHA1CircularShift(5, A) + ((B & C) | (B & D) | (C & D)) + E
               + W[t] + K[2];
    E = D;
    D = C;
    C = SHA1CircularShift(30, B);
    B = A;
    A = tmp;
  }
  for (auto t = 60; t < 80; t++) {
    auto tmp = SHA1CircularShift(5, A) + (B ^ C ^ D) + E + W[t] + K[3];
    E = D;
    D = C;
    C = SHA1CircularShift(30, B);
    B = A;
    A = tmp;
  }
  intermediate_[0] += A;
  intermediate_[1] += B;
  intermediate_[2] += C;
  intermediate_[3] += D;
  intermediate_[4] += E;
  message_block_index_ = 0;
}

void sha1::pad_message() {
  if (message_block_index_ > 55) {
    message_block_[message_block_index_++] = 0x80;
    while (message_block_index_ < 64)
      message_block_[message_block_index_++] = 0;
    process_message_block();
    while (message_block_index_ < 56)
      message_block_[message_block_index_++] = 0;
  } else {
    message_block_[message_block_index_++] = 0x80;
    while (message_block_index_ < 56)
      message_block_[message_block_index_++] = 0;
  }
  message_block_[56] = static_cast<uint8_t>(length_ >> 56);
  message_block_[57] = static_cast<uint8_t>(length_ >> 48);
  message_block_[58] = static_cast<uint8_t>(length_ >> 40);
  message_block_[59] = static_cast<uint8_t>(length_ >> 32);
  message_block_[60] = static_cast<uint8_t>(length_ >> 24);
  message_block_[61] = static_cast<uint8_t>(length_ >> 16);
  message_block_[62] = static_cast<uint8_t>(length_ >> 8);
  message_block_[63] = static_cast<uint8_t>(length_);
  process_message_block();
}

} // namespace caf::hash
