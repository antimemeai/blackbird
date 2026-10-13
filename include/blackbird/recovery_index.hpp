#pragma once
#include "blackbird/journal.hpp"
#include "blackbird/journal_storage.hpp"
#include "blackbird/raw_retained.hpp"
#include <functional>

namespace blackbird {
// Typed keys sort lexicographically. Callers encode numeric ordering big endian;
// the page representation itself uses checked little endian scalar fields.
using RecoveryKey = std::array<std::byte, 40>;
struct RecoveryPageRef {
  std::uint64_t offset = 0;
  std::uint64_t generation = 0;
  std::uint32_t checksum = 0;
  bool operator==(const RecoveryPageRef &) const = default;
};
struct RecoveryIndexEntry {
  RecoveryKey key;
  std::uint64_t value;
  bool operator==(const RecoveryIndexEntry &) const = default;
};
// One sequential owner. Roots are immutable; rollback selects an earlier root.
// A root is derived, never journal authority. There is no historical page cache.
class RecoveryIndex {
public:
  static constexpr std::size_t page_size = 16384;
  static Result<std::unique_ptr<RecoveryIndex>>
  open(std::unique_ptr<JournalFile> file, std::uint64_t max_bytes,
       const RawRetainedOrigin &raw_origin = {});
  Result<std::optional<std::uint64_t>> find(RecoveryPageRef root,
                                            const RecoveryKey &key) const;
  Result<RecoveryPageRef> put(RecoveryPageRef root, RecoveryKey key,
                              std::uint64_t value);
  // Inclusive lower bound; the callback can stop iteration without extra reads.
  Result<void>
  range(RecoveryPageRef root, const RecoveryKey &lower, std::size_t limit,
        const std::function<Result<bool>(RecoveryIndexEntry)> &visit) const;
  Result<void> synchronize();
  std::uint64_t bytes() const noexcept { return end_; }

private:
  friend class RetainedState;
  struct Node {
    bool leaf = true;
    std::vector<RecoveryIndexEntry> entries;
    std::vector<RecoveryPageRef> children;
  };
  struct Change {
    RecoveryPageRef left;
    std::optional<RecoveryIndexEntry> split;
    RecoveryPageRef right;
  };
  RecoveryIndex(std::unique_ptr<JournalFile> file, std::uint64_t end,
                std::uint64_t max_bytes, RawRetainedOrigin raw_origin)
      : raw_origin_(std::move(raw_origin)), file_(std::move(file)), end_(end),
        max_bytes_(max_bytes) {}
  Result<Node> read(RecoveryPageRef ref) const;
  Result<RecoveryPageRef> write(const Node &node);
  Result<Change> insert(RecoveryPageRef root, RecoveryKey key, std::uint64_t value,
                        std::size_t depth);
  Result<bool> walk(RecoveryPageRef root, const RecoveryKey &lower, std::size_t &left,
                    const std::function<Result<bool>(RecoveryIndexEntry)> &visit,
                    std::size_t depth) const;
  mutable RawRetainedOrigin raw_origin_;
  std::unique_ptr<JournalFile> file_;
  std::uint64_t end_;
  std::uint64_t max_bytes_;
};
} // namespace blackbird
