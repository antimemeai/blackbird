#pragma once

#include "blackbird/journal.hpp"
#include "blackbird/journal_storage.hpp"
#include "blackbird/raw_format.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace blackbird {

struct RawAuditCursor {
  AuditStreamId journal;
  std::uint64_t sequence;
  std::uint64_t offset;
  bool operator==(const RawAuditCursor &) const = default;
};
struct RawAuditRead {
  RawOrigin origin;
  ByteView raw;
  RawAuditCursor next;
};
struct RawAuditProgress {
  std::uint64_t start_offset;
  std::uint64_t written_bytes;
  bool sync_attempted;
};

// One recorder execution owner. Caller owns source storage until append success.
// Physical framing runs here, never in a producer. The file retains its storage.
// Producer epoch/ordinal uniqueness is externally fenced. The recorder checks
// source prefixes and replay bytes, without retaining every record identity.
class RawAudit {
public:
  // Successor seeds come from the caller's validated predecessor audit prefix.
  // Their source_start field is that prefix's cumulative end, not a new record.
  static Result<std::unique_ptr<RawAudit>>
  open(JournalDirectory &directory, std::string_view name, JournalHeader header,
       std::size_t max_lanes = 256, SyncStrength strength = SyncStrength::full,
       std::span<const RawOrigin> predecessor_prefixes = {});
  // Prefix must come from recorder publication; a complete unsynced commit alone
  // is not permission for a tertiary reader to observe its records.
  static Result<std::unique_ptr<RawAudit>>
  open_reader(JournalDirectory &directory, std::string_view name, JournalHeader header,
              RawAuditCursor preserved_prefix, std::size_t max_lanes = 256,
              std::span<const RawOrigin> predecessor_prefixes = {});
  ~RawAudit();
  RawAudit(const RawAudit &) = delete;
  RawAudit &operator=(const RawAudit &) = delete;
  Result<void> append(std::span<const RawAuditRecord> records);
  std::uint64_t preserved(const IdentityBytes &lane, RawPartition partition) const;
  RawAuditCursor begin() const noexcept;
  RawAuditCursor cursor() const noexcept { return cursor_; }
  // Borrowed bytes refer to caller output; only the flushed prefix is visible.
  Result<std::optional<RawAuditRead>> read(RawAuditCursor cursor,
                                           MutableByteView output);
  const std::optional<Error> &recovery_error() const noexcept { return problem_; }
  RawAuditProgress progress() const noexcept { return progress_; }
  const JournalHeader &header() const noexcept { return header_; }

private:
  struct LanePrefix {
    IdentityBytes lane;
    RawPartition partition;
    std::uint64_t end;
  };
  RawAudit(JournalHeader header, std::size_t max_lanes, SyncStrength strength);
  Result<void> recover(std::optional<RawAuditCursor> limit = std::nullopt);
  Result<void> seed(std::span<const RawOrigin> predecessor_prefixes);
  Result<void> read_exact(std::uint64_t offset, MutableByteView output);
  Result<void> write_exact(std::uint64_t offset, ByteView bytes);
  Result<void> advance(std::vector<LanePrefix> &lanes, const RawAuditRecord &record);
  Result<bool> duplicate(const RawAuditRecord &record);
  Result<JournalFrameView> frame(std::uint64_t offset, std::uint64_t end);
  Result<RawAuditRecord> raw_record(ByteView bytes);
  JournalHeader header_;
  std::size_t max_lanes_;
  SyncStrength strength_;
  std::unique_ptr<JournalFile> file_;
  bool writer_ = false;
  std::vector<LanePrefix> lanes_;
  std::vector<LanePrefix> staged_;
  std::vector<std::byte> batch_;
  std::unique_ptr<std::byte[]> frame_;
  std::size_t frame_size_ = 0;
  RawAuditCursor cursor_;
  std::optional<Error> problem_;
  RawAuditProgress progress_{};
};

} // namespace blackbird
