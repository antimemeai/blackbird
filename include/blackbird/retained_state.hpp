#pragma once

#include "blackbird/journal_writer.hpp"
#include "blackbird/raw_retained.hpp"
#include "blackbird/recovery_index.hpp"
#include "blackbird/retained_events.hpp"
#include "blackbird/shared_sequence.hpp"
#include "blackbird/value.hpp"

namespace blackbird {
struct SavedState;
struct RawEventRef;
class RawCapacityClaim;
class ArchiveCatalog;
struct RecordReference {
  AuditStreamId journal;
  std::uint64_t sequence;
  bool operator==(const RecordReference &) const = default;
};
struct RetainedFact {
  RecordReference record;
  RetainedEvent event;
  RetainedEvidence evidence;
};
struct Submission {
  bool existing;
  RecordReference record;
  RetainedEvidence evidence;
};
struct PendingProposal {
  ProvisionalCaptureEvent capture;
  std::vector<std::byte> bytes;
};
struct ProvisionalOriginal {
  RecordReference marker;
  ProvisionalCaptureEvent capture;
  std::vector<std::byte> bytes;
};
struct AttemptState {
  AttemptAdmissionEvent admission;
  bool opened;
  std::optional<AttemptObservationEvent> observation;
  std::optional<AdapterReceiptEvent> receipt;
  RetainedEvidence evidence;
  bool reconciliation_required;
};
class CustodyVerifier {
public:
  virtual ~CustodyVerifier() = default;
  // U3 implements actual resource/authority checks. Success never grants an old
  // recovered attempt permission to dispatch anew.
  virtual Result<void> verify(std::span<const AttemptState> attempts) = 0;
};
struct DispatchReport {
  bool dispatched;
  Result<void> effect;
  Result<void> recording;
};

// One sequential custodian owner, including reentrant effect callbacks. Caches
// are derived from this same journal. Ordinary model bindings must route external
// effects through dispatch; appending a fact alone never invokes an effect seam.
class RetainedState {
public:
  static Result<std::unique_ptr<RetainedState>>
  create(std::unique_ptr<JournalDirectory> directory, std::string_view name,
         JournalHeader header, JournalCapacity capacity,
         RawProductStorageOwner raw_owner = {});
  static Result<std::unique_ptr<RetainedState>>
  open(std::unique_ptr<JournalDirectory> directory, std::string_view name,
       JournalHeader header, JournalCapacity capacity, bool use_scan_checkpoint = false,
       bool allow_full_replay = true, std::uint64_t max_full_replay_bytes = UINT64_MAX,
       RawProductStorageOwner raw_owner = {});
  Result<void> publish_scan_checkpoint() {
    if (state() != JournalWriterState::live || in_transaction_ || prepared_ ||
        !reconciled_)
      return Result<void>::failure({ErrorCode::audit_unavailable});
    return journal_->publish_scan_checkpoint();
  }
  // Live, reconciled, and no transaction or prepared state open: a derived
  // snapshot taken now covers exactly the committed journal.
  bool quiescent() const noexcept {
    return state() == JournalWriterState::live && !in_transaction_ && !prepared_ &&
           reconciled_;
  }
  bool used_scan_checkpoint() const noexcept {
    return journal_->used_scan_checkpoint();
  }
  EnvironmentId environment() const noexcept { return journal_->header().environment; }
  ~RetainedState();
  // Current projection only. Full ledger replay remains the safe fallback until
  // archive-locator migration covers all semantic predicates.
  const SavedState *saved_state() const noexcept { return saved_.get(); }
  Result<void> save_current_state(const Value &context);
  RawStorageSelection raw_storage_selection() const noexcept;
  Result<std::unique_ptr<RawRetainedTransfer>>
  prepare_raw_transfer(const RawThreadHandle &child, RawWorkerDispatch dispatch);
  RawRetainedStopBundle *raw_stop_bundle() noexcept { return raw_stop_.get(); }
  bool raw_owner_blocked() const noexcept { return raw_owner_blocked_; }

  Result<void> store_diagnostic(std::string_view name, ByteView bytes,
                                std::uint64_t now) {
    return directory_->store_diagnostic(name, bytes, now);
  }
  Result<std::vector<std::byte>> read_diagnostic(std::string_view name,
                                                 std::uint64_t now) {
    return directory_->read_diagnostic(name, now);
  }
  Result<ScratchCleanup> expire_diagnostics(std::uint64_t now) {
    return directory_->expire_diagnostics(now);
  }
  std::weak_ptr<void> lifetime() const noexcept { return lifetime_; }
  void set_maintenance_checkpoint(
      std::function<Result<void>(const RawEventRef &)> checkpoint) {
    maintenance_checkpoint_ = std::move(checkpoint);
  }
  const std::optional<Error> &maintenance_error() const noexcept {
    return maintenance_error_;
  }
  Value::Array current_programs() const;
  RetainedState(const RetainedState &) = delete;
  RetainedState &operator=(const RetainedState &) = delete;
  // Native-only, borrowed lifetime: owner must outlive the scope. No nested reset.
  class SettlementScope {
  public:
    SettlementScope(const SettlementScope &) = delete;
    SettlementScope &operator=(const SettlementScope &) = delete;
    ~SettlementScope();

  private:
    friend class RetainedState;
    explicit SettlementScope(RetainedState &owner) : owner_(owner) {}
    RetainedState &owner_;
    std::unique_ptr<RawRetainedSettlementEnd> raw_end_;
    bool accepted_ = false;
  };
  Result<std::unique_ptr<SettlementScope>> protect_settlement(JournalCapacity credit);
  Result<void> refresh_settlement(JournalCapacity credit);
  std::optional<JournalCapacity> protected_settlement() const noexcept {
    return settlement_credit_;
  }
  // Only receipts and terminal observations. Exact successful cost consumes credit.
  // Without a scope this is an ordinary submission, never extra physical capacity.
  Result<Submission>
  submit_settlement(const RetainedEvent &event,
                    std::span<PhysicalJournalRecord> acknowledged = {},
                    bool *proposed_batch_acknowledged = nullptr);
  // Exact submit duplicates are zero-cost; append is a physical batch, including
  // repeated events. Removing drafts would invalidate caller source sequences.
  Result<Submission> submit(const RetainedEvent &event);
  Result<JournalCursor> append(JournalCursor expected,
                               std::span<const ByteView> sources,
                               std::span<const RetainedEvent> events,
                               std::span<PhysicalJournalRecord> acknowledged = {},
                               bool *proposed_batch_acknowledged = nullptr);
  // Atomic application result plus terminal observations. No effect release.
  Result<JournalCursor>
  append_settlement(JournalCursor expected, std::span<const RetainedEvent> events,
                    std::span<PhysicalJournalRecord> acknowledged = {},
                    bool *proposed_batch_acknowledged = nullptr);
  Result<DispatchReport> dispatch(OperationAttemptId attempt, EffectBoundary &boundary);
  Result<DispatchReport> dispatch(OperationAttemptId attempt, EffectBoundary &boundary,
                                  RawRetainedExec *raw_exec);
  Result<JournalCursor> append_exec(JournalCursor expected,
                                    std::span<const RetainedEvent> events,
                                    RawRetainedExec &raw_exec);
  Result<AttemptState> attempt(OperationAttemptId identity) const;
  Result<std::vector<AttemptState>> unresolved_attempts() const;
  Result<DecisionEvent> decision(DecisionId identity) const;
  Result<InvocationEvent> invocation(InvocationId identity) const;
  Result<std::vector<std::byte>> source(SourceReference reference);
  // Unvalidated bounded inspection remains available while admission is closed.
  Result<ObservedJournalBytes> read_original_range(std::uint64_t offset,
                                                   std::size_t length) {
    return journal_->read_original_range(offset, length);
  }
  Result<void> confirm_recovery();
  Result<void> reconcile(CustodyVerifier &verifier);
  // A failed application-level recovery must close admission until reopen.
  void block_admission() noexcept { recording_failed_ = true; }
  template <typename T> Result<T> issue() {
    auto reserved = reserve_identity();
    if (!reserved.has_value()) {
      return Result<T>::failure(reserved.error());
    }
    return T::from_bytes(std::move(reserved).value());
  }
  JournalCursor cursor() const noexcept { return journal_->cursor(); }
  JournalLimits journal_limits() const noexcept { return journal_->header().limits; }
  JournalCapacity journal_capacity() const noexcept { return capacity_; }
  JournalUsage journal_usage() const {
    auto result = journal_->usage();
    result.state = state();
    return result;
  }
  JournalWriterState state() const noexcept {
    return recording_failed_ && journal_->state() == JournalWriterState::live
               ? JournalWriterState::blocked
               : journal_->state();
  }
  const JournalRecoveryReport &recovery_report() const noexcept {
    return journal_->recovery_report();
  }
  // Explicit prerequisite mode: populate a derived paged index from full replay.
  // Not checkpoint recovery: attachment does not skip any authoritative replay.
  Result<void> enable_indexed_queries(std::unique_ptr<JournalFile> file,
                                      std::uint64_t max_bytes);
  bool indexed_queries() const noexcept { return query_index_ != nullptr; }
  Result<RetainedFact> fact(std::size_t ordinal) const;
  Result<std::vector<RetainedFact>> fact_page(std::size_t first,
                                              std::size_t limit) const;
  struct FactHistory {
    std::size_t count = 0;
    std::function<Result<RetainedFact>(std::size_t)> read;
    std::shared_ptr<RawRetainedReadOrigin> raw_origin = {};
    Result<std::vector<RetainedFact>> page(std::size_t first, std::size_t limit) const;
  };
  // Owns immutable derived-file/byte readers, never the custodian or a lease.
  // Full-replay fallback may retain its legacy facts; saved history owns only
  // the selected generation handles and the bounded suffix overlay.
  FactHistory history_snapshot() const;
  std::size_t fact_count() const noexcept {
    return committed_.archived_facts + committed_.facts.size();
  }
  // Borrow lasts until the next mutation; no staged/uncertain facts appear here.
  const SharedSequence<RetainedFact> &tail_facts() const noexcept {
    return committed_.facts;
  }
  bool compact_recovery() const noexcept { return compact_; }
  const SharedSequence<RetainedFact> &committed_facts() const {
    if (compact_)
      throw Error{ErrorCode::unsupported};
    return committed_.facts;
  }
  // Durable/pending upper reservation bound, not the volatile last-issued ID.
  std::uint64_t issuer_counter() const noexcept;
  // Surviving RAM originals, not committed sources or permission. Borrow ends
  // at the next mutation; reopening cannot recreate lost process memory.
  const std::optional<PendingProposal> &pending_proposal() const noexcept {
    return pending_proposal_;
  }

private:
  friend class RawRetainedScope;
  friend class RawRetainedTransfer;
  friend class RawRetainedExec;
  friend class RawRetainedJobLoan;
  friend class RetainedEnvironment;
  friend class CodingEngine;
  friend class ContextStore;
  class MaintenanceScope {
  public:
    explicit MaintenanceScope(RetainedState &owner)
        : owner_(owner), previous_(owner.maintenance_) {
      owner_.maintenance_ = true;
    }
    ~MaintenanceScope() { owner_.maintenance_ = previous_; }

  private:
    RetainedState &owner_;
    bool previous_;
  };
  RawProductStorageOwner raw_storage_owner_;
  std::unique_ptr<RawRetainedOwnerEnd> raw_end_;
  mutable RawRetainedOrigin raw_origin_;
  std::unique_ptr<RawRetainedStopBundle> raw_stop_;
  RawThreadHandle raw_joined_owner_;
  bool raw_owner_blocked_ = false;
  Result<std::unique_ptr<RawRetainedStopBundle>>
  prepare_raw_stop(JournalCapacity credit, RawStorageHandoff *handoff = nullptr,
                   RawThreadHandle issuer = {});
  void block_raw_owner() noexcept;
  Result<void> restore_raw_owner();
  std::shared_ptr<void> lifetime_ = std::make_shared<int>(0);
  std::function<Result<void>(const RawEventRef &)> maintenance_checkpoint_;
  std::optional<Error> maintenance_error_;
  std::uint64_t maintenance_retry_sequence_ = 0;
  bool checkpoint_running_ = false;
  bool maintenance_ = false;
  bool compact_ = false;
  std::uint64_t live_from_sequence_ = 1;
  struct Snapshot {
    std::size_t archived_facts = 0;
    std::size_t archived_records = 0;
    SharedSequence<RetainedFact> facts;
    SharedSequence<PhysicalJournalRecord> sources;
    std::vector<RetainedFact> uncertain_facts;
    std::vector<ProvisionalOriginal> provisional_originals;
    RecoveryPageRef query_root;
    std::uint64_t issuer_namespace = 0;
    std::uint64_t counter = 0;
    void swap(Snapshot &other) noexcept;
  };
  RetainedState(std::unique_ptr<JournalDirectory> directory, JournalCapacity capacity);
  static Result<std::unique_ptr<RetainedState>>
  allocate(std::unique_ptr<JournalDirectory> directory, JournalCapacity capacity);
  Result<void> rebuild();
  Result<void> replay(Snapshot &snapshot, FramedJournal &journal,
                      std::span<const SourceReference> capture_only = {},
                      std::uint64_t maintenance_end = 0);
  bool has_room(const Snapshot &snapshot, std::size_t count) const noexcept;
  FramedJournal *segment(AuditStreamId identity) const noexcept;
  std::optional<RetainedFact> lookup(const Snapshot &snapshot, unsigned char family,
                                     RetainedKind kind, const IdentityBytes &identity,
                                     const IdentityBytes &second = {}) const;
  void index_fact(Snapshot &snapshot, std::size_t ordinal);
  void index_source(Snapshot &snapshot, std::size_t ordinal);
  void populate_index(Snapshot &snapshot);
  std::unique_ptr<RecoveryIndex> query_index_;
  struct ReplayIndex;
  Result<void> apply(Snapshot &snapshot, RetainedEvent event, RecordReference record,
                     RetainedEvidence evidence, ReplayIndex *index = nullptr);
  Result<JournalCursor> append_impl(JournalCursor expected,
                                    std::span<const ByteView> sources,
                                    std::span<const RetainedEvent> events,
                                    bool diagnostic, bool settlement = false,
                                    std::span<PhysicalJournalRecord> acknowledged = {},
                                    bool *proposed_batch_acknowledged = nullptr,
                                    RawRetainedExec *raw_exec = nullptr);
  Result<Submission> submit_impl(const RetainedEvent &event, bool settlement,
                                 std::span<PhysicalJournalRecord> acknowledged = {},
                                 bool *proposed_batch_acknowledged = nullptr);
  Result<void> retain_rejection(JournalCursor expected,
                                std::span<const ByteView> sources,
                                std::span<const std::vector<std::byte>> events,
                                Error reason, const RetainedEvent *conflict,
                                RawCapacityClaim *raw_bank);
  std::optional<RetainedFact>
  existing(const Snapshot &snapshot, const RetainedEvent &event,
           std::optional<std::uint64_t> issuer_namespace = std::nullopt,
           const ReplayIndex *index = nullptr) const;
  const RetainedFact *existing_uncertain(
      const Snapshot &snapshot, const RetainedEvent &event,
      std::optional<std::uint64_t> issuer_namespace = std::nullopt) const;
  static bool has_identity(const RetainedEvent &event);
  const Snapshot &visible() const noexcept;
  Result<IdentityBytes> reserve_identity();
  std::string journal_name_;
  std::unique_ptr<SavedState> saved_;
  std::shared_ptr<ArchiveCatalog> archive_;
  std::unique_ptr<JournalDirectory> directory_;
  std::vector<std::unique_ptr<FramedJournal>> historical_;
  std::unique_ptr<FramedJournal> journal_;
  JournalCapacity capacity_;
  std::optional<JournalCapacity> settlement_credit_;
  std::size_t max_history_entries_;
  Snapshot committed_;
  std::optional<Snapshot> prepared_;
  std::optional<PendingProposal> pending_proposal_;
  // Never restored: reopening burns all unused counters in the durable range.
  std::uint64_t allocation_namespace_ = 0;
  std::uint64_t allocation_cursor_ = 0;
  std::uint64_t allocation_limit_ = 0;
  std::uint64_t pending_counter_ = 0;
  std::uint64_t pending_namespace_ = 0;
  bool reconciled_ = true;
  bool in_transaction_ = false;
  bool recording_failed_ = false;
};
} // namespace blackbird
