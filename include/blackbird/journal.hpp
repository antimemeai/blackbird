#pragma once

#include "blackbird/foundation.hpp"

namespace blackbird {

inline constexpr std::size_t journal_header_size = 112;
inline constexpr std::size_t journal_frame_header_size = 32;
struct JournalLimits {
  std::uint32_t max_payload;
  std::uint32_t max_batch_bytes;
  bool operator==(const JournalLimits &) const = default;
};
struct JournalPredecessor {
  AuditStreamId journal;
  std::uint64_t validated_sequence;
  std::uint64_t validated_end_offset;
  bool operator==(const JournalPredecessor &) const = default;
};
struct JournalHeader {
  EnvironmentId environment;
  AuditStreamId journal;
  std::uint64_t issuer_namespace;
  JournalLimits limits;
  std::optional<JournalPredecessor> predecessor;
  bool operator==(const JournalHeader &) const = default;
};
enum class FrameKind : std::uint16_t {
  source = 1,
  semantic = 2,
  raw = 4,
  commit = 65535
};
struct PhysicalJournalRecord {
  EnvironmentId environment;
  AuditStreamId journal;
  FrameKind kind;
  std::uint64_t sequence;
  std::uint64_t batch_first;
  std::uint64_t payload_offset;
  std::uint32_t payload_size;
  std::uint32_t frame_checksum = 0;
  bool operator==(const PhysicalJournalRecord &) const = default;
};
struct JournalFrameView {
  FrameKind kind;
  std::uint64_t sequence;
  std::uint64_t batch_first;
  ByteView payload;
  std::size_t encoded_size;
};

// A previous finalized CRC permits exact incremental byte-stream calculation.
std::uint32_t crc32c(ByteView bytes, std::uint32_t previous = 0) noexcept;
Result<std::array<std::byte, journal_header_size>>
encode_journal_header(const JournalHeader &header);
Result<JournalHeader> decode_journal_header(ByteView bytes);
Result<std::vector<std::byte>>
encode_journal_frame(FrameKind kind, std::uint64_t sequence, std::uint64_t batch_first,
                     ByteView payload, JournalLimits limits);
Result<std::size_t> encode_journal_frame_into(MutableByteView output, FrameKind kind,
                                              std::uint64_t sequence,
                                              std::uint64_t batch_first,
                                              ByteView payload, JournalLimits limits);
// Returned payload borrows input; the owner must live throughout its use.
// Decodes one bounded physical frame. Batch/dependency semantics are checked by
// journal replay before any authoritative publication.
Result<JournalFrameView> decode_journal_frame(ByteView bytes, JournalLimits limits);

} // namespace blackbird
