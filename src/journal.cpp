#include "blackbird/journal.hpp"

#include <algorithm>
#include <cstring>

#if defined(__ARM_FEATURE_CRC32) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#include <arm_acle.h>
#endif

namespace blackbird {

namespace {
constexpr std::array<std::byte, 8> header_magic{
    std::byte{'A'}, std::byte{'R'}, std::byte{'C'}, std::byte{'O'},
    std::byte{'J'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'}};
constexpr std::array<std::byte, 4> frame_magic{std::byte{'A'}, std::byte{'R'},
                                               std::byte{'J'}, std::byte{'1'}};

void put(std::span<std::byte> bytes, std::uint64_t value) noexcept {
  for (auto &byte : bytes) {
    byte = static_cast<std::byte>(value & 255);
    value >>= 8;
  }
}
std::uint64_t get(ByteView bytes) noexcept {
  std::uint64_t value = 0;
  for (std::size_t offset = 0; offset < bytes.size(); ++offset) {
    value |= static_cast<std::uint64_t>(std::to_integer<unsigned int>(bytes[offset]))
             << (offset * 8);
  }
  return value;
}
bool valid_limits(JournalLimits limits) noexcept {
  return limits.max_payload >= 24 && limits.max_batch_bytes >= 88 &&
         limits.max_payload <= limits.max_batch_bytes - 88;
}
bool valid_kind(FrameKind kind) noexcept {
  return kind == FrameKind::source || kind == FrameKind::semantic ||
         kind == FrameKind::raw || kind == FrameKind::commit;
}
Result<void> validate_header(const JournalHeader &header) {
  if (header.issuer_namespace == 0) {
    return Result<void>::failure({ErrorCode::invalid_identity});
  }
  if (!valid_limits(header.limits)) {
    return Result<void>::failure({ErrorCode::invalid_range});
  }
  if (header.predecessor) {
    const auto &prior = *header.predecessor;
    if (prior.journal == header.journal ||
        prior.validated_end_offset < journal_header_size ||
        (prior.validated_sequence == 0 &&
         prior.validated_end_offset != journal_header_size) ||
        (prior.validated_end_offset - journal_header_size) / journal_frame_header_size <
            prior.validated_sequence) {
      return Result<void>::failure({ErrorCode::invalid_range});
    }
  }
  return Result<void>::success();
}
template <typename IdType> Result<IdType> decode_identity(ByteView bytes) {
  IdentityBytes value{};
  std::copy(bytes.begin(), bytes.end(), value.begin());
  const auto decoded = IdType::from_bytes(value);
  if (!decoded.has_value()) {
    return Result<IdType>::failure({ErrorCode::corrupt});
  }
  return decoded;
}
} // namespace

#if !defined(__ARM_FEATURE_CRC32) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
namespace {
// Reflected Castagnoli polynomial. Generate eight byte-position tables rather
// than doing eight polynomial steps for every retained byte at runtime.
constexpr auto crc_tables = [] {
  std::array<std::array<std::uint32_t, 256>, 8> tables{};
  for (std::size_t i = 0; i < 256; ++i) {
    auto state = static_cast<std::uint32_t>(i);
    for (unsigned bit = 0; bit < 8; ++bit)
      state = (state >> 1) ^ ((state & 1) ? 0x82f63b78U : 0U);
    tables[0][i] = state;
  }
  for (std::size_t slice = 1; slice < tables.size(); ++slice) {
    for (std::size_t i = 0; i < 256; ++i) {
      const auto state = tables[slice - 1][i];
      tables[slice][i] = (state >> 8) ^ tables[0][state & 255];
    }
  }
  return tables;
}();
} // namespace
#endif

std::uint32_t crc32c(ByteView bytes, std::uint32_t previous) noexcept {
  auto state = previous ^ UINT32_MAX;
#if defined(__ARM_FEATURE_CRC32) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  // Enabled only when the compiler's target guarantees the CRC extension.
  // memcpy permits unaligned bytes without aliasing violations.
  while (bytes.size() >= sizeof(std::uint64_t)) {
    std::uint64_t word;
    std::memcpy(&word, bytes.data(), sizeof(word));
    state = __crc32cd(state, word);
    bytes = bytes.subspan(sizeof(word));
  }
  for (auto byte : bytes)
    state = __crc32cb(state, std::to_integer<std::uint8_t>(byte));
#else
  while (bytes.size() >= 8) {
    // Explicit byte loads work for unaligned input and either host byte order.
    for (unsigned i = 0; i < 4; ++i)
      state ^= std::to_integer<std::uint32_t>(bytes[i]) << (8 * i);
    state = crc_tables[7][state & 255] ^ crc_tables[6][(state >> 8) & 255] ^
            crc_tables[5][(state >> 16) & 255] ^ crc_tables[4][state >> 24] ^
            crc_tables[3][std::to_integer<unsigned>(bytes[4])] ^
            crc_tables[2][std::to_integer<unsigned>(bytes[5])] ^
            crc_tables[1][std::to_integer<unsigned>(bytes[6])] ^
            crc_tables[0][std::to_integer<unsigned>(bytes[7])];
    bytes = bytes.subspan(8);
  }
  for (const auto byte : bytes)
    state =
        (state >> 8) ^ crc_tables[0][(state ^ std::to_integer<unsigned>(byte)) & 255];
#endif
  return state ^ UINT32_MAX;
}

Result<std::array<std::byte, journal_header_size>>
encode_journal_header(const JournalHeader &header) {
  using Encoded = std::array<std::byte, journal_header_size>;
  const auto valid = validate_header(header);
  if (!valid.has_value()) {
    return Result<Encoded>::failure(valid.error());
  }
  Encoded output{};
  const auto bytes = std::span{output};
  std::copy(header_magic.begin(), header_magic.end(), bytes.begin());
  std::copy(header.environment.bytes().begin(), header.environment.bytes().end(),
            bytes.begin() + 8);
  std::copy(header.journal.bytes().begin(), header.journal.bytes().end(),
            bytes.begin() + 24);
  put(bytes.subspan(40, 8), header.issuer_namespace);
  if (header.predecessor) {
    const auto &prior = *header.predecessor;
    std::copy(prior.journal.bytes().begin(), prior.journal.bytes().end(),
              bytes.begin() + 48);
    put(bytes.subspan(64, 8), prior.validated_sequence);
    put(bytes.subspan(72, 8), prior.validated_end_offset);
  }
  put(bytes.subspan(80, 4), 1);
  put(bytes.subspan(84, 4), header.limits.max_payload);
  put(bytes.subspan(88, 4), header.limits.max_batch_bytes);
  put(bytes.subspan(92, 4), header.predecessor ? 2 : 1);
  put(bytes.subspan(108, 4), crc32c(ByteView{output}.first(108)));
  return Result<Encoded>::success(output);
}

Result<JournalHeader> decode_journal_header(ByteView bytes) {
  if (bytes.size() < journal_header_size) {
    return Result<JournalHeader>::failure({ErrorCode::incomplete});
  }
  if (bytes.size() != journal_header_size) {
    return Result<JournalHeader>::failure({ErrorCode::invalid_range});
  }
  if (!std::equal(header_magic.begin(), header_magic.end(), bytes.begin()) ||
      crc32c(bytes.first(108)) != get(bytes.subspan(108, 4))) {
    return Result<JournalHeader>::failure({ErrorCode::corrupt});
  }
  if (get(bytes.subspan(80, 4)) != 1) {
    return Result<JournalHeader>::failure({ErrorCode::unsupported});
  }
  const auto flags = get(bytes.subspan(92, 4));
  if ((flags != 1 && flags != 2) ||
      std::any_of(bytes.begin() + 96, bytes.begin() + 108,
                  [](std::byte byte) { return byte != std::byte{0}; })) {
    return Result<JournalHeader>::failure({ErrorCode::corrupt});
  }
  const auto environment = decode_identity<EnvironmentId>(bytes.subspan(8, 16));
  const auto journal = decode_identity<AuditStreamId>(bytes.subspan(24, 16));
  if (!environment.has_value() || !journal.has_value()) {
    return Result<JournalHeader>::failure({ErrorCode::corrupt});
  }
  std::optional<JournalPredecessor> predecessor;
  if (flags == 2) {
    const auto prior = decode_identity<AuditStreamId>(bytes.subspan(48, 16));
    if (!prior.has_value()) {
      return Result<JournalHeader>::failure(prior.error());
    }
    predecessor = JournalPredecessor{prior.value(), get(bytes.subspan(64, 8)),
                                     get(bytes.subspan(72, 8))};
  } else if (std::any_of(bytes.begin() + 48, bytes.begin() + 80,
                         [](std::byte byte) { return byte != std::byte{0}; })) {
    return Result<JournalHeader>::failure({ErrorCode::corrupt});
  }
  JournalHeader decoded{environment.value(),
                        journal.value(),
                        get(bytes.subspan(40, 8)),
                        {static_cast<std::uint32_t>(get(bytes.subspan(84, 4))),
                         static_cast<std::uint32_t>(get(bytes.subspan(88, 4)))},
                        predecessor};
  if (!validate_header(decoded).has_value()) {
    return Result<JournalHeader>::failure({ErrorCode::corrupt});
  }
  return Result<JournalHeader>::success(decoded);
}

Result<std::size_t> encode_journal_frame_into(MutableByteView output, FrameKind kind,
                                              std::uint64_t sequence,
                                              std::uint64_t batch_first,
                                              ByteView payload, JournalLimits limits) {
  if (sequence == 0 || batch_first == 0)
    return Result<std::size_t>::failure({ErrorCode::invalid_identity});
  if (batch_first > sequence || !valid_limits(limits))
    return Result<std::size_t>::failure({ErrorCode::invalid_range});
  if (!valid_kind(kind))
    return Result<std::size_t>::failure({ErrorCode::unsupported});
  if (payload.size() > limits.max_payload ||
      output.size() < journal_frame_header_size ||
      payload.size() > output.size() - journal_frame_header_size)
    return Result<std::size_t>::failure({ErrorCode::capacity});
  const auto size = journal_frame_header_size + payload.size();
  const auto bytes = output.first(size);
  // Move first: input may overlap even the header we are about to replace.
  if (!payload.empty())
    std::memmove(bytes.data() + journal_frame_header_size, payload.data(),
                 payload.size());
  std::copy(frame_magic.begin(), frame_magic.end(), bytes.begin());
  put(bytes.subspan(4, 2), 1);
  put(bytes.subspan(6, 2), static_cast<std::uint16_t>(kind));
  put(bytes.subspan(8, 8), sequence);
  put(bytes.subspan(16, 8), batch_first);
  put(bytes.subspan(24, 4), payload.size());
  put(bytes.subspan(28, 4), crc32c(ByteView{bytes}.subspan(journal_frame_header_size),
                                   crc32c(ByteView{bytes}.first(28))));
  return Result<std::size_t>::success(size);
}

Result<std::vector<std::byte>>
encode_journal_frame(FrameKind kind, std::uint64_t sequence, std::uint64_t batch_first,
                     ByteView payload, JournalLimits limits) {
  using Encoded = std::vector<std::byte>;
  if (sequence == 0 || batch_first == 0)
    return Result<Encoded>::failure({ErrorCode::invalid_identity});
  if (batch_first > sequence || !valid_limits(limits))
    return Result<Encoded>::failure({ErrorCode::invalid_range});
  if (!valid_kind(kind))
    return Result<Encoded>::failure({ErrorCode::unsupported});
  Encoded output;
  if (payload.size() > limits.max_payload ||
      payload.size() > output.max_size() - journal_frame_header_size)
    return Result<Encoded>::failure({ErrorCode::capacity});
  try {
    output.resize(journal_frame_header_size + payload.size());
    const auto encoded =
        encode_journal_frame_into(output, kind, sequence, batch_first, payload, limits);
    if (!encoded.has_value())
      return Result<Encoded>::failure(encoded.error());
    return Result<Encoded>::success(std::move(output));
  } catch (const std::bad_alloc &) {
    return Result<Encoded>::failure({ErrorCode::allocation});
  }
}

Result<JournalFrameView> decode_journal_frame(ByteView bytes, JournalLimits limits) {
  if (!valid_limits(limits)) {
    return Result<JournalFrameView>::failure({ErrorCode::invalid_range});
  }
  if (bytes.size() < journal_frame_header_size) {
    return Result<JournalFrameView>::failure({ErrorCode::incomplete});
  }
  if (!std::equal(frame_magic.begin(), frame_magic.end(), bytes.begin())) {
    return Result<JournalFrameView>::failure({ErrorCode::corrupt});
  }
  if (get(bytes.subspan(4, 2)) != 1) {
    return Result<JournalFrameView>::failure({ErrorCode::unsupported});
  }
  const auto raw_kind = get(bytes.subspan(6, 2));
  if (raw_kind != 1 && raw_kind != 2 && raw_kind != 4 && raw_kind != 65535) {
    return Result<JournalFrameView>::failure({ErrorCode::unsupported});
  }
  const auto kind = static_cast<FrameKind>(raw_kind);
  const auto length = get(bytes.subspan(24, 4));
  if (length > limits.max_payload) {
    return Result<JournalFrameView>::failure({ErrorCode::capacity});
  }
  if (length > bytes.size() - journal_frame_header_size) {
    return Result<JournalFrameView>::failure({ErrorCode::incomplete});
  }
  const auto payload =
      bytes.subspan(journal_frame_header_size, static_cast<std::size_t>(length));
  if (crc32c(payload, crc32c(bytes.first(28))) != get(bytes.subspan(28, 4))) {
    return Result<JournalFrameView>::failure({ErrorCode::corrupt});
  }
  const auto sequence = get(bytes.subspan(8, 8));
  const auto first = get(bytes.subspan(16, 8));
  if (sequence == 0 || first == 0 || first > sequence) {
    return Result<JournalFrameView>::failure({ErrorCode::corrupt});
  }
  return Result<JournalFrameView>::success(
      {kind, sequence, first, payload, journal_frame_header_size + payload.size()});
}

} // namespace blackbird
