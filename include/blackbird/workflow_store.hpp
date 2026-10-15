#pragma once
#include "blackbird/context.hpp"
#include <map>
#include <thread>

namespace blackbird {
struct WorkflowCellTag;
using WorkflowCellId = Id<WorkflowCellTag>;
struct WorkflowCellRevision {
  std::uint64_t value = 0;
  bool operator==(const WorkflowCellRevision &) const = default;
};
struct WorkflowCellSchema {
  std::string name;
  std::uint32_t version = 1;
  bool operator==(const WorkflowCellSchema &) const = default;
};
struct WorkflowCell {
  WorkflowCellId id;
  WorkflowCellSchema schema;
  WorkflowCellRevision revision;
  Value value;
};
struct WorkflowCellRead {
  WorkflowCellId id;
  WorkflowCellRevision revision; // Zero requires absence, not a missing check.
};
struct WorkflowCellWrite {
  WorkflowCellId id;
  WorkflowCellSchema schema;
  Value value;
};
struct WorkflowTransaction {
  std::string submission;
  std::vector<WorkflowCellRead> reads;
  std::vector<WorkflowCellWrite> writes;
  std::optional<Value> decision;
};
struct WorkflowCommit {
  bool duplicate;
  std::uint64_t revision;
  RecordReference record;
};
struct WorkflowStoreLimits {
  std::size_t cells = 65536;
  std::size_t submissions = 65536;
  std::size_t batch = 256;
};

// A derived reduction over program records, usable by saved-state maintenance.
// Replay consumes each delta once, in retained order. No external effects occur.
// All access requires the constructing thread/process; this object cannot move.
class WorkflowCellState {
public:
  explicit WorkflowCellState(WorkflowStoreLimits limits = {});
  WorkflowCellState(const WorkflowCellState &) = delete;
  WorkflowCellState &operator=(const WorkflowCellState &) = delete;
  std::optional<WorkflowCell> read(WorkflowCellId id) const;
  std::uint64_t revision() const;
  Value snapshot() const;
  void restore(const Value &snapshot);
  void replay(const Value &packet, RecordReference record, std::size_t ordinal);

private:
  friend class WorkflowStore;
  struct Receipt {
    std::uint64_t revision;
    RecordReference record;
    std::size_t ordinal;
  };
  struct Prepared {
    std::map<WorkflowCellId, WorkflowCell> changed;
    std::map<std::string, Receipt, std::less<>> receipt;
    Value packet;
    std::uint64_t revision;
  };
  void owner() const;
  Prepared prepare(const Value &request, RecordReference record, std::size_t ordinal,
                   const Value &events) const;
  void publish(Prepared &&prepared) noexcept;
  WorkflowStoreLimits limits_;
  std::thread::id thread_;
  std::int64_t process_;
  std::uint64_t revision_ = 0;
  std::map<WorkflowCellId, WorkflowCell> cells_;
  std::map<std::string, Receipt, std::less<>> receipts_;
};

// The caller owns AuditLog/RetainedState for this store's entire lifetime.
// Each write must have exactly one read of the previous revision, and existing
// schemas cannot change. Optional native lifecycle events commit atomically with
// the delta; they are never dispatched here. F1 accepts fresh decisions,
// invocations, admissions, opens, receipts and observations. Other variants
// refuse; existing native identities cannot be reused under a new submission.
class WorkflowStore {
public:
  explicit WorkflowStore(AuditLog &log, WorkflowStoreLimits limits = {});
  std::optional<WorkflowCell> read(WorkflowCellId id) const;
  std::uint64_t revision() const;
  Value snapshot() const;
  WorkflowCommit transact(const WorkflowTransaction &transaction,
                          std::span<const RetainedEvent> events = {},
                          bool settlement = false,
                          std::optional<ApplicationRecordId> reserved_record = {});

private:
  AuditLog &log_;
  WorkflowCellState state_;
  bool committing_ = false;
};
} // namespace blackbird
