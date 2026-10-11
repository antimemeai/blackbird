#include "blackbird/journal.hpp"

#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blackbird;

namespace {
void check(bool condition, const char *expression, int line) {
  if (!condition) {
    throw std::runtime_error(std::to_string(line) + ": " + expression);
  }
}
#define CHECK(expression) check((expression), #expression, __LINE__)
template <typename T> T require(Result<T> result) {
  CHECK(result.has_value());
  return std::move(result).value();
}
template <typename T> void error_is(const Result<T> &result, ErrorCode code) {
  CHECK(!result.has_value());
  CHECK(result.error().code == code);
}
template <typename T> T id(unsigned char value) {
  IdentityBytes bytes{};
  bytes[15] = std::byte{value};
  return require(T::from_bytes(bytes));
}
void little(std::span<std::byte> output, std::uint64_t value) {
  for (auto &byte : output) {
    byte = static_cast<std::byte>(value & 255);
    value >>= 8;
  }
}
void repair_header_checksum(std::array<std::byte, journal_header_size> &bytes) {
  little(std::span{bytes}.subspan(108, 4), crc32c(ByteView{bytes}.first(108)));
}

void checksum_vectors() {
  const std::string text = "123456789";
  CHECK(crc32c(std::as_bytes(std::span{text})) == 0xe3069283);
  std::array<std::byte, 32> bytes{};
  // Published CRC32C vectors: RFC 3720 appendix B.4.
  CHECK(crc32c(bytes) == 0x8a9136aa);
  bytes.fill(std::byte{255});
  CHECK(crc32c(bytes) == 0x62a8ab43);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<std::byte>(i);
  }
  CHECK(crc32c(bytes) == 0x46dd794e);
  CHECK(crc32c({}) == 0);
}

void checksum_specification() {
  const auto bit_serial = [](ByteView bytes, std::uint32_t previous) {
    auto value = ~previous;
    for (auto byte : bytes) {
      value ^= std::to_integer<std::uint32_t>(byte);
      for (unsigned bit = 0; bit < 8; ++bit)
        value = (value & 1) ? (value >> 1) ^ 0x82f63b78U : value >> 1;
    }
    return ~value;
  };
  std::array<std::byte, 1040> storage{};
  std::uint32_t random = 0x179ae051;
  for (auto &byte : storage) {
    random = random * 1664525U + 1013904223U;
    byte = static_cast<std::byte>(random >> 24);
  }
  for (std::size_t offset = 0; offset < 8; ++offset) {
    for (std::size_t length = 0; length <= 1024; ++length) {
      const auto bytes = ByteView{storage}.subspan(offset, length);
      for (auto seed : {0U, 1U, 0xffffffffU, 0x193ad502U}) {
        const auto expected = bit_serial(bytes, seed);
        CHECK(crc32c(bytes, seed) == expected);
        const auto split = length / 2;
        CHECK(crc32c(bytes.subspan(split), crc32c(bytes.first(split), seed)) ==
              expected);
      }
    }
  }
}

void headers() {
  const JournalLimits limits{65536, 1048576};
  const JournalHeader root{id<EnvironmentId>(1), id<AuditStreamId>(2),
                           0x0102030405060708, limits, std::nullopt};
  const auto bytes = require(encode_journal_header(root));
  CHECK(bytes.size() == 112);
  // Independently specified field offsets, endian values and reserved zeros.
  const std::array<std::byte, 8> magic{std::byte{'A'}, std::byte{'R'}, std::byte{'C'},
                                       std::byte{'O'}, std::byte{'J'}, std::byte{'0'},
                                       std::byte{'0'}, std::byte{'1'}};
  CHECK(std::equal(magic.begin(), magic.end(), bytes.begin()));
  CHECK(bytes[23] == std::byte{1} && bytes[39] == std::byte{2});
  for (std::size_t i = 0; i < 8; ++i) {
    CHECK(bytes[40 + i] == static_cast<std::byte>(8 - i));
  }
  for (std::size_t i = 48; i < 80; ++i) {
    CHECK(bytes[i] == std::byte{0});
  }
  CHECK(bytes[80] == std::byte{1} && bytes[86] == std::byte{1});
  CHECK(bytes[90] == std::byte{16} && bytes[92] == std::byte{1});
  for (std::size_t i = 96; i < 108; ++i) {
    CHECK(bytes[i] == std::byte{0});
  }
  const auto decoded = require(decode_journal_header(bytes));
  CHECK(decoded.environment == root.environment && decoded.journal == root.journal);
  CHECK(decoded.issuer_namespace == 0x0102030405060708);
  CHECK(decoded.limits == limits && !decoded.predecessor);
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    error_is(decode_journal_header(ByteView{bytes}.first(length)),
             ErrorCode::incomplete);
  }
  for (std::size_t offset = 0; offset < bytes.size(); ++offset) {
    auto damaged = bytes;
    damaged[offset] ^= std::byte{128};
    CHECK(!decode_journal_header(damaged).has_value());
  }
  for (const auto flags : {0U, 3U, 4U, 0xffffffffU}) {
    auto damaged = bytes;
    little(std::span{damaged}.subspan(92, 4), flags);
    repair_header_checksum(damaged);
    error_is(decode_journal_header(damaged), ErrorCode::corrupt);
  }
  auto invalid_reserved = bytes;
  invalid_reserved[100] = std::byte{1};
  repair_header_checksum(invalid_reserved);
  error_is(decode_journal_header(invalid_reserved), ErrorCode::corrupt);
  const JournalHeader continuation{root.environment, id<AuditStreamId>(3), 9, limits,
                                   JournalPredecessor{root.journal, 17, 4096}};
  const auto next_bytes = require(encode_journal_header(continuation));
  CHECK(next_bytes[63] == std::byte{2} && next_bytes[64] == std::byte{17});
  CHECK(next_bytes[73] == std::byte{16} && next_bytes[92] == std::byte{2});
  const auto next = require(decode_journal_header(next_bytes));
  CHECK(next.predecessor == continuation.predecessor);
  auto bad = root;
  bad.issuer_namespace = 0;
  error_is(encode_journal_header(bad), ErrorCode::invalid_identity);
  bad = root;
  bad.limits.max_payload = 23;
  error_is(encode_journal_header(bad), ErrorCode::invalid_range);
  bad = root;
  bad.limits.max_batch_bytes = 87;
  error_is(encode_journal_header(bad), ErrorCode::invalid_range);
  bad = root;
  bad.limits.max_payload = 2097152;
  error_is(encode_journal_header(bad), ErrorCode::invalid_range);
  bad = continuation;
  bad.predecessor = JournalPredecessor{root.journal, 17, 112};
  error_is(encode_journal_header(bad), ErrorCode::invalid_range);
  bad.predecessor = JournalPredecessor{root.journal, UINT64_MAX, UINT64_MAX};
  error_is(encode_journal_header(bad), ErrorCode::invalid_range);
}

void frames() {
  const JournalLimits limits{64, 512};
  const std::array payload{std::byte{0}, std::byte{255}, std::byte{10}};
  const auto bytes =
      require(encode_journal_frame(FrameKind::source, 7, 7, payload, limits));
  CHECK(bytes.size() == 35);
  CHECK(bytes[4] == std::byte{1} && bytes[6] == std::byte{1});
  CHECK(bytes[8] == std::byte{7} && bytes[16] == std::byte{7});
  CHECK(bytes[24] == std::byte{3});
  // Fixed expected CRC for the specified header and NUL/0xff/LF payload.
  CHECK(bytes[28] == std::byte{0x8c} && bytes[29] == std::byte{0xf2});
  CHECK(bytes[30] == std::byte{0xeb} && bytes[31] == std::byte{0x9d});
  CHECK(std::equal(payload.begin(), payload.end(), bytes.begin() + 32));
  const auto frame = require(decode_journal_frame(bytes, limits));
  CHECK(frame.kind == FrameKind::source && frame.sequence == 7 &&
        frame.batch_first == 7);
  CHECK(frame.encoded_size == 35 && frame.payload.size() == 3);
  CHECK(std::equal(payload.begin(), payload.end(), frame.payload.begin()));
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    error_is(decode_journal_frame(ByteView{bytes}.first(length), limits),
             ErrorCode::incomplete);
  }
  for (std::size_t offset = 0; offset < bytes.size(); ++offset) {
    auto damaged = bytes;
    damaged[offset] ^= std::byte{128};
    CHECK(!decode_journal_frame(damaged, limits).has_value());
  }
  std::array<std::byte, 65> too_large{};
  error_is(encode_journal_frame(FrameKind::source, 1, 1, too_large, limits),
           ErrorCode::capacity);
  error_is(encode_journal_frame(FrameKind::source, 0, 1, payload, limits),
           ErrorCode::invalid_identity);
  error_is(encode_journal_frame(FrameKind::source, 1, 2, payload, limits),
           ErrorCode::invalid_range);
  auto oversized = bytes;
  little(std::span{oversized}.subspan(24, 4), 0xffffffff);
  error_is(decode_journal_frame(oversized, limits), ErrorCode::capacity);
  // Concatenated stream decoding consumes one frame, preserving the next bytes.
  auto stream = bytes;
  stream.insert(stream.end(), bytes.begin(), bytes.end());
  CHECK(require(decode_journal_frame(stream, limits)).encoded_size == 35);
}
void raw_frames_into() {
  const JournalLimits limits{64, 256};
  const std::array<std::byte, 3> payload{std::byte{0}, std::byte{0xff},
                                         std::byte{0x7f}};
  std::array<std::byte, 40> output{};
  output.fill(std::byte{0xa5});
  CHECK(require(encode_journal_frame_into(output, FrameKind::raw, 1, 1, payload,
                                          limits)) == 35);
  CHECK(output[6] == std::byte{4} && output[7] == std::byte{0});
  for (std::size_t index = 35; index < output.size(); ++index)
    CHECK(output[index] == std::byte{0xa5});
  const auto decoded = require(decode_journal_frame(output, limits));
  CHECK(decoded.kind == FrameKind::raw);
  CHECK(std::equal(payload.begin(), payload.end(), decoded.payload.begin(),
                   decoded.payload.end()));
  const auto before = output;
  error_is(encode_journal_frame_into(MutableByteView{output}.first(34), FrameKind::raw,
                                     1, 1, payload, limits),
           ErrorCode::capacity);
  CHECK(output == before);
  error_is(encode_journal_frame_into(output, FrameKind::raw, 0, 1, payload, limits),
           ErrorCode::invalid_identity);
  CHECK(output == before);
  require(encode_journal_frame_into(output, FrameKind::raw, 2, 2,
                                    ByteView{output}.subspan(32, 3), limits));
  CHECK(std::equal(payload.begin(), payload.end(), output.begin() + 32));
  std::copy(payload.begin(), payload.end(), output.begin());
  require(encode_journal_frame_into(output, FrameKind::raw, 3, 3,
                                    ByteView{output}.first(3), limits));
  CHECK(std::equal(payload.begin(), payload.end(), output.begin() + 32));
}
} // namespace

int main() {
  try {
    checksum_vectors();
    checksum_specification();
    headers();
    frames();
    raw_frames_into();
    std::puts("PASS journal CRC vectors, headers and bounded frame bytes");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL journal codec: %s\n", error.what());
    return 1;
  }
}
