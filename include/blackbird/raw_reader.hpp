#pragma once

#include "blackbird/raw_audit.hpp"

#include <string>

namespace blackbird {

// Consumption resource bounds, independent of recorder admission. A caller can
// raise these explicitly; exceeding one rejects the snapshot without changing it.
struct RawReaderLimits {
  std::size_t max_segments = 65536;
  std::size_t max_origins = 4096;
  std::size_t max_descriptions = 16 * 1024 * 1024;
};
struct RawRunCursor {
  AuditStreamId run;
  std::uint32_t descriptions_crc;
  std::uint64_t segment;
  RawAuditCursor audit;
  bool operator==(const RawRunCursor &) const = default;
};
inline constexpr std::size_t raw_run_cursor_size = 64;
std::array<std::byte, raw_run_cursor_size>
encode_raw_run_cursor(const RawRunCursor &cursor) noexcept;
Result<RawRunCursor> decode_raw_run_cursor(ByteView bytes);

struct RawSegmentSnapshot {
  JournalHeader header;
  RawAuditCursor preserved;
  std::vector<RawOrigin> seeds;
  // Bytes beyond the permitted prefix remain unexposed, whether valid or damaged.
  std::uint64_t unexposed_bytes;
};
struct RawRunSnapshot {
  AuditStreamId run;
  std::uint32_t descriptions_crc;
  std::vector<std::byte> descriptions;
  std::vector<RawSegmentSnapshot> segments;
  bool closed;
};
struct RawRunEntry {
  std::string name;
  std::optional<RawRunSnapshot> snapshot;
  std::optional<Error> error;
};
struct RawRunRead {
  RawOrigin origin;
  ByteView raw; // Caller output storage owns these bytes.
  RawRunCursor next;
};

class RawRunReader {
public:
  static Result<std::unique_ptr<RawRunReader>>
  open(std::string_view run_directory,
       std::optional<RawRunCursor> resume = std::nullopt, RawReaderLimits limits = {});
  static Result<std::unique_ptr<RawRunReader>>
  open(NativeJournalDirectory directory,
       std::optional<RawRunCursor> resume = std::nullopt, RawReaderLimits limits = {});
  const RawRunSnapshot &snapshot() const noexcept { return snapshot_; }
  const RawRunCursor &cursor() const noexcept { return cursor_; }
  Result<std::optional<RawRunRead>> read(MutableByteView output);

private:
  RawRunReader(NativeJournalDirectory directory, RawRunSnapshot snapshot,
               RawReaderLimits limits);
  Result<void> load(std::uint64_t segment);
  Result<void> position(const RawRunCursor &cursor);
  NativeJournalDirectory directory_;
  RawRunSnapshot snapshot_;
  RawReaderLimits limits_;
  RawRunCursor cursor_;
  std::unique_ptr<RawAudit> audit_;
  std::optional<Error> problem_;
};

// An unfinished/corrupt run is an entry with an error; it cannot hide other runs.
// No raw directory is a successful empty result. Does not create anything.
Result<std::vector<RawRunEntry>> discover_raw_runs(std::string_view session_directory,
                                                   RawReaderLimits limits = {});
Result<std::vector<RawRunEntry>>
discover_raw_runs(NativeJournalDirectory session_directory,
                  RawReaderLimits limits = {});

} // namespace blackbird
