#pragma once
#include "blackbird/value.hpp"
#include <functional>
#include <string>
#include <vector>

namespace blackbird {
enum class WorkflowJoinMode : std::uint8_t {
  all,
  all_success,
  first,
  first_success,
  quorum,
  select,
  collect
};
enum class WorkflowJoinOutcome : std::uint8_t { success, failure, cancelled, unknown };
enum class WorkflowJoinReason : std::uint8_t { satisfied, impossible, timed_out };
struct WorkflowJoinAcceptance {
  // Owner-assigned retained acceptance order, not worker-reported completion time.
  // Equal sequences identify one batch and require equal epoch-local timestamps.
  // Untimed {sequence,0,0} explicitly uses retained order only, and requires no
  // cutoff. It cannot mix with measured observations or claim physical time.
  std::uint64_t sequence = 0;
  std::uint64_t clock_epoch = 0;
  std::int64_t monotonic_ns = 0;
  bool operator==(const WorkflowJoinAcceptance &) const = default;
};
struct WorkflowJoinCutoff {
  std::uint64_t clock_epoch = 0;
  std::int64_t monotonic_ns = 0;
  bool operator==(const WorkflowJoinCutoff &) const = default;
};
struct WorkflowJoinObservation {
  std::string member;
  std::uint64_t result_revision = 0;
  WorkflowJoinOutcome outcome = WorkflowJoinOutcome::unknown;
  WorkflowJoinAcceptance acceptance;
  Value result;
  // Logical result finality, not physical execution settlement. A final unknown
  // is observable by first/all/collect; an unresolved unknown has final=false.
  bool final = true;
  bool operator==(const WorkflowJoinObservation &) const = default;
};
struct WorkflowJoinDescriptor {
  std::string id;
  WorkflowJoinMode mode = WorkflowJoinMode::all;
  std::vector<std::string> members;
  bool membership_closed = true;
  std::size_t max_members = 4096;
  std::size_t max_observations = 16384;
  std::size_t quorum = 1;
  std::size_t collect_limit = 4096;
  std::string predicate_revision;
  std::optional<WorkflowJoinCutoff> cutoff;
  bool operator==(const WorkflowJoinDescriptor &) const = default;
};
class WorkflowJoinSelection {
public:
  const std::string &join_id() const noexcept { return join_id_; }
  std::uint64_t base_revision() const noexcept { return base_revision_; }
  WorkflowJoinReason reason() const noexcept { return reason_; }
  const std::vector<WorkflowJoinObservation> &observations() const noexcept {
    return observations_;
  }
  const std::optional<WorkflowJoinAcceptance> &timeout() const noexcept {
    return timeout_;
  }
  Value snapshot() const;
  bool operator==(const WorkflowJoinSelection &) const = default;

private:
  friend class WorkflowJoin;
  WorkflowJoinSelection(std::string join_id, std::uint64_t revision,
                        WorkflowJoinReason reason,
                        std::vector<WorkflowJoinObservation> observations,
                        std::optional<WorkflowJoinAcceptance> timeout);
  std::string join_id_;
  std::uint64_t base_revision_;
  WorkflowJoinReason reason_;
  std::vector<WorkflowJoinObservation> observations_;
  std::optional<WorkflowJoinAcceptance> timeout_;
};

// Single-owner pure reducer. Caller pins predicate_revision and a pure predicate.
// Root must durably retain the exact proposal BEFORE accept_committed; this class
// performs no persistence, cancellation, producer closure or settlement actions.
// A cutoff pins a measured clock epoch; otherwise the first observation pins
// measured single-epoch OR explicit untimed ordering. Measured timer recovery
// across epochs requires the owner's explicit successor-wait protocol.
class WorkflowJoin {
public:
  using Predicate = std::function<bool(const WorkflowJoinObservation &)>;
  WorkflowJoin(const WorkflowJoin &) = default;
  WorkflowJoin &operator=(const WorkflowJoin &) = default;
  WorkflowJoin(WorkflowJoin &&) noexcept = default;
  WorkflowJoin &operator=(WorkflowJoin &&) noexcept = default;
  static Result<WorkflowJoin> create(WorkflowJoinDescriptor descriptor,
                                     Predicate predicate = {});
  static Result<WorkflowJoin> restore(const Value &snapshot, Predicate predicate = {});
  Result<void> add_member(std::string member);
  Result<void> close_membership();
  Result<void> observe(WorkflowJoinObservation observation);
  Result<void> observe_timeout(WorkflowJoinAcceptance acceptance);
  // Caller first supplies its complete selected owner-acceptance input batch.
  // Completion at/after cutoff is ineligible; only an observed timeout can time out.
  Result<std::optional<WorkflowJoinSelection>> propose() const;
  // Prepare all selection/predicate/allocation work on an unpublished candidate.
  // Commit candidate.snapshot(), then adopt with noexcept move assignment. The
  // live reducer and its committed_selection remain unchanged before adoption.
  Result<WorkflowJoin> prepare_acceptance(const WorkflowJoinSelection &selection) const;
  // May allocate and invoke the pinned predicate. Owner must stop/recover on
  // postcommit application failure, or stage an unpublished candidate precommit.
  Result<void> accept_committed(const WorkflowJoinSelection &selection);
  const std::optional<WorkflowJoinSelection> &committed_selection() const noexcept {
    return selection_;
  }
  const WorkflowJoinDescriptor &descriptor() const noexcept { return descriptor_; }
  std::uint64_t revision() const noexcept { return revision_; }
  Value snapshot() const;

private:
  WorkflowJoin(WorkflowJoinDescriptor descriptor, Predicate predicate);
  Result<void> validate_acceptance(const WorkflowJoinAcceptance &acceptance) const;
  bool qualifies(const WorkflowJoinObservation &observation) const;
  bool timely(const WorkflowJoinObservation &observation) const;
  std::vector<WorkflowJoinObservation> current_observations() const;
  WorkflowJoinDescriptor descriptor_;
  Predicate predicate_;
  struct RecordedObservation {
    WorkflowJoinObservation observation;
    std::uint64_t state_revision;
  };
  std::vector<RecordedObservation> history_;
  std::optional<WorkflowJoinAcceptance> timeout_;
  std::uint64_t timeout_revision_ = 0;
  std::optional<WorkflowJoinSelection> selection_;
  std::uint64_t revision_ = 0;
};
} // namespace blackbird
