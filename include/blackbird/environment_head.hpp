#pragma once

#include "blackbird/journal.hpp"
#include "blackbird/journal_storage.hpp"
#include "blackbird/raw_retained.hpp"

namespace blackbird {
inline constexpr std::size_t head_selector_size = 72;
struct HeadSelector {
  EnvironmentId environment;
  AuditStreamId root;
  AuditStreamId active;
  std::uint64_t generation;
  bool operator==(const HeadSelector &) const = default;
};
Result<std::array<std::byte, head_selector_size>>
encode_head_selector(const HeadSelector &selector);
Result<HeadSelector> decode_head_selector(ByteView bytes);

// Selection/custody metadata, not journal validation or dispatch permission.
// The complete environment owner validates and synchronizes a continuation first.
class EnvironmentHead final {
public:
  static Result<std::unique_ptr<EnvironmentHead>>
  create(std::unique_ptr<JournalDirectory> directory, const JournalHeader &root,
         SyncStrength strength, const RawRetainedOrigin &raw_origin = {});
  // Internal creation phases: hold the permanent authority/directory while the
  // environment prepares its root, then publish once. Preparation is not healthy
  // selection or admission permission. Failed publication requires reopen.
  static Result<std::unique_ptr<EnvironmentHead>>
  prepare(std::unique_ptr<JournalDirectory> directory, const JournalHeader &root,
          SyncStrength strength, const RawRetainedOrigin &raw_origin = {});
  Result<void> publish_initial(SyncStrength strength);
  static Result<std::unique_ptr<EnvironmentHead>>
  open(std::unique_ptr<JournalDirectory> directory, const JournalHeader &root,
       SyncStrength strength, const RawRetainedOrigin &raw_origin = {});
  EnvironmentHead(const EnvironmentHead &) = delete;
  EnvironmentHead &operator=(const EnvironmentHead &) = delete;
  EnvironmentHead(EnvironmentHead &&) = delete;
  EnvironmentHead &operator=(EnvironmentHead &&) = delete;
  const HeadSelector &selection() const noexcept { return selection_; }
  bool healthy() const noexcept { return healthy_; }
  Result<void> replace(const HeadSelector &expected, AuditStreamId next,
                       SyncStrength strength);
  // Borrowed for the complete environment owner's segment operations.
  JournalDirectory &directory() noexcept { return *directory_; }

private:
  friend class RetainedEnvironment;
  EnvironmentHead(std::unique_ptr<JournalDirectory> directory,
                  const JournalHeader &root, RawRetainedOrigin raw_origin)
      : raw_origin_(std::move(raw_origin)), directory_(std::move(directory)),
        root_(root), selection_{root.environment, root.journal, root.journal, 1} {}
  Result<std::pair<std::unique_ptr<JournalFile>, HeadSelector>> read_selector();
  Result<void> write_selector(const HeadSelector &next, SyncStrength strength,
                              bool &attempted);
  RawRetainedOrigin raw_origin_;
  std::unique_ptr<JournalDirectory> directory_;
  std::unique_ptr<JournalFile>
      authority_; // Destroyed before directory, never replaced.
  JournalHeader root_;
  HeadSelector selection_;
  bool healthy_ = false;
  bool initial_prepared_ = false;
  bool in_transaction_ = false;
};
} // namespace blackbird
