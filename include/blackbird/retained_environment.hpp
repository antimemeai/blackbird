#pragma once

#include "blackbird/environment_head.hpp"
#include "blackbird/retained_state.hpp"

namespace blackbird {
struct EnvironmentBounds {
  JournalLimits framing;
  JournalCapacity segment;
  std::size_t max_segments;
  std::size_t max_history_entries;
  std::uint64_t max_capture_bytes;
};
struct ProposalSubmission {
  JournalCursor cursor;
  bool existing;
  std::optional<ProvisionalCaptureEvent> capture;
  std::vector<Submission> events;
};

// Permanent environment lease and one accumulated semantic owner. Construction
// and replay are being integrated under U1_CONTINUATION_SUBPLAN; full continuation
// and provisional capture support are required before U2 may consume this API.
class RetainedEnvironment {
public:
  static Result<std::unique_ptr<RetainedEnvironment>>
  create(std::unique_ptr<JournalDirectory> directory, JournalHeader root,
         JournalCapacity root_capacity, EnvironmentBounds bounds,
         RawProductStorageOwner raw_owner = {});
  static Result<std::unique_ptr<RetainedEnvironment>>
  open(std::unique_ptr<JournalDirectory> directory, JournalHeader root,
       JournalCapacity root_capacity, EnvironmentBounds bounds,
       const RawProductStorageOwner &raw_owner = {});
  RetainedEnvironment(const RetainedEnvironment &) = delete;
  RetainedEnvironment &operator=(const RetainedEnvironment &) = delete;
  Result<Submission> submit(const RetainedEvent &event);
  Result<ProposalSubmission> submit_proposal(JournalCursor expected,
                                             std::span<const ByteView> sources,
                                             std::span<const RetainedEvent> events);
  Result<DispatchReport> dispatch(OperationAttemptId attempt, EffectBoundary &boundary);
  Result<AttemptState> attempt(OperationAttemptId identity) const;
  Result<std::vector<std::byte>> source(SourceReference reference);
  Result<ObservedJournalBytes>
  read_original_range(AuditStreamId journal, std::uint64_t offset, std::size_t length);
  Result<void> confirm_recovery();
  Result<void> reconcile(CustodyVerifier &verifier);
  template <typename T> Result<T> issue() {
    if (busy_)
      return Result<T>::failure({ErrorCode::busy});
    if (state() != JournalWriterState::live)
      return Result<T>::failure({ErrorCode::audit_unavailable});
    return semantic_->issue<T>();
  }
  JournalCursor cursor() const noexcept { return semantic_->cursor(); }
  JournalWriterState state() const noexcept {
    if (!head_->healthy())
      return JournalWriterState::poisoned;
    for (const auto &journal : semantic_->historical_)
      if (journal->state() == JournalWriterState::poisoned)
        return JournalWriterState::poisoned;
    if (continuation_required_ || busy_)
      return JournalWriterState::blocked;
    return semantic_->state();
  }
  const JournalRecoveryReport &recovery_report() const noexcept {
    return semantic_->recovery_report();
  }
  Result<void> enable_indexed_queries(std::unique_ptr<JournalFile> file,
                                      std::uint64_t max_bytes) {
    if (busy_)
      return Result<void>::failure({ErrorCode::busy});
    return semantic_->enable_indexed_queries(std::move(file), max_bytes);
  }
  Result<RetainedFact> fact(std::size_t ordinal) const {
    return semantic_->fact(ordinal);
  }
  std::size_t fact_count() const noexcept { return semantic_->fact_count(); }
  const SharedSequence<RetainedFact> &committed_facts() const {
    return semantic_->committed_facts();
  }

  // Borrow ends at mutation. Packet contents never grant ordinary authority.
  std::span<const ProvisionalOriginal> provisional_originals() const noexcept {
    return semantic_->visible().provisional_originals;
  }

private:
  struct MaintenanceReplay {
    std::vector<SourceReference> capture_only;
    std::uint64_t end = 1;
  };
  Result<MaintenanceReplay> replay_maintenance(RetainedState::Snapshot &snapshot,
                                               FramedJournal &journal,
                                               const JournalHeader &origin,
                                               JournalCapacity origin_policy,
                                               RetainedEvent choice);
  Result<void> stage_original(RetainedState::Snapshot &snapshot,
                              ProvisionalOriginal original, const JournalHeader &origin,
                              JournalCapacity policy);
  Result<std::optional<ProposalSubmission>>
  existing_proposal(JournalCursor expected, std::span<const ByteView> sources,
                    std::span<const RetainedEvent> events) const;
  RetainedEnvironment(std::unique_ptr<EnvironmentHead> head, JournalHeader root,
                      JournalCapacity root_capacity, EnvironmentBounds bounds,
                      RawRetainedOrigin raw_origin)
      : raw_origin_(std::move(raw_origin)), head_(std::move(head)), root_(root),
        root_capacity_(root_capacity), bounds_(bounds) {}
  Result<void> load_selected();
  mutable RawRetainedOrigin raw_origin_;
  std::unique_ptr<EnvironmentHead> head_; // Outlives all borrowed segments.
  std::unique_ptr<RetainedState> semantic_;
  JournalHeader root_;
  JournalCapacity root_capacity_;
  EnvironmentBounds bounds_;
  bool busy_ = false;
  bool continuation_required_ = false;
};
} // namespace blackbird
