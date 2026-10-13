#pragma once

#include "blackbird/journal.hpp"
#include "blackbird/raw_storage.hpp"

#include <type_traits>

namespace blackbird {
class RetainedState;
class RawRetainedTransfer;
class RawRetainedSettlementLoan;
class RawRetainedScope;
class RawRetainedExec;
class RawRetainedJobLoan;
struct PhysicalJournalRecord;
struct RawRetainedStopBundle;
struct JournalCursor;
enum class JournalWriterState : std::uint8_t;
class RawRetainedOwnerEnd {
public:
  static Result<std::unique_ptr<RawRetainedOwnerEnd>>
  prepare(const RawProductStorageOwner &owner, RawEventRef factory);
  ~RawRetainedOwnerEnd();
  void finish(const JournalCursor &cursor, JournalWriterState state, bool pending,
              bool prepared) noexcept;

private:
  RawRetainedOwnerEnd(RawStorageCleanupBank bank, RawEventRef factory);
  RawStorageCleanupBank bank_;
  RawEventRef factory_;
  bool finished_ = false;
};

class RawRetainedSettlementEnd {
public:
  static Result<std::unique_ptr<RawRetainedSettlementEnd>>
  prepare(const RawProductStorageOwner &owner, RawEventRef protection);
  ~RawRetainedSettlementEnd();
  void finish() noexcept;

private:
  RawRetainedSettlementEnd(RawStorageCleanupBank bank, RawEventRef protection);
  RawStorageCleanupBank bank_;
  RawEventRef protection_;
  bool finished_ = false;
};

struct RawRetainedOrigin {
  RawEventRef parent{};
  bool selected = false;
  RawProductStorageOwner owner;
  const void *root = nullptr;
  RawRetainedSettlementLoan *loan = nullptr;
  enum class Input : std::uint8_t { ordinary, no_new };
  Input input = Input::ordinary;
};

// One finite bank belongs to its actual issuing producer and selected root.
// Joining changes lifecycle eligibility; it does not move the claim or refill it.
class RawRetainedSettlementLoan {
public:
  static Result<std::unique_ptr<RawRetainedSettlementLoan>>
  prepare(const void *root, std::uint64_t control, RawProductStorageOwner owner);
  ~RawRetainedSettlementLoan();
  RawRetainedSettlementLoan(const RawRetainedSettlementLoan &) = delete;
  RawRetainedSettlementLoan &operator=(const RawRetainedSettlementLoan &) = delete;
  void started() noexcept { joined_ = false; }
  void joined() noexcept { joined_ = true; }
  void never_started() noexcept { joined_ = true; }
  const void *root() const noexcept { return root_; }
  const RawThreadHandle &issuer() const noexcept { return issuer_; }
  bool permits_current(const void *root) const noexcept;
  bool permits_release() const noexcept;
  Result<RawControlToken> split(const void *root, std::uint64_t amount);

private:
  friend class RetainedState;
  friend class RawRetainedExec;
  friend class RawRetainedJobLoan;
  static Result<std::unique_ptr<RawRetainedSettlementLoan>>
  prepare_authorized(const void *root, std::uint64_t control,
                     RawProductStorageOwner owner);
  RawRetainedSettlementLoan(const void *root, RawThreadHandle issuer,
                            RawControlToken credit, RawProductStorageOwner owner);
  const void *root_;
  RawThreadHandle issuer_;
  RawProductStorageOwner owner_;
  std::optional<RawControlToken> credit_;
  bool joined_ = false;
  bool original_issuer_ = false;
  const void *issuing_thread_ = nullptr;
  pid_t issuing_process_ = 0;
};

// One cell belongs to the physical journal, shared by its surviving cold readers.
// Immutable authority is checked before reading the lexical borrowed loan.
class RawRetainedReadOrigin {
public:
  explicit RawRetainedReadOrigin(RawRetainedOrigin initial)
      : owner_(initial.owner), origin(std::move(initial)) {}
  RawRetainedOrigin snapshot() const;

private:
  const RawProductStorageOwner owner_;

public:
  RawRetainedOrigin origin;
};

struct RawRetainedStopBundle {
  RawThreadHandle issuer;
  RawProductStorageOwner owner;
  std::unique_ptr<RawRetainedSettlementLoan> semantic;
  std::unique_ptr<RawStorageSettlementLoan> physical;
  ~RawRetainedStopBundle();
  void started() noexcept;
  void joined() noexcept;
  void never_started() noexcept;
  void verify_retirement() const noexcept;

private:
  bool borrower_joined_ = false;
};

struct RawRetainedPairSites {
  std::uint32_t request, returned;
};
struct RawRetainedEventSite {
  std::uint32_t site;
  std::uint64_t credit;
};
// Views borrow reached native locals through the synchronous publication. They
// never own, encode, normalize or walk a product representation.
class RawRetainedFields {
public:
  void add(ByteView value) noexcept { fields_[count_++] = value; }
  template <class T> void native(const T &value) noexcept {
    add(detail::raw_native_field(value));
  }
  template <class T> void reference(const T &value) noexcept {
    add(value.journal.bytes());
    native(value.sequence);
  }
  template <class T> void cursor(const T &value) noexcept {
    reference(value);
    native(value.end_offset);
  }
  template <class T> void physical(const T &value) noexcept {
    add(value.environment.bytes());
    add(value.journal.bytes());
    native(value.kind);
    native(value.sequence);
    native(value.batch_first);
    native(value.payload_offset);
    native(value.payload_size);
    native(value.frame_checksum);
  }
  template <class T> void page(const T &value) noexcept {
    native(value.offset);
    native(value.generation);
    native(value.checksum);
  }
  template <class T> void header(const T &value) noexcept {
    add(value.environment.bytes());
    add(value.journal.bytes());
    native(value.issuer_namespace);
    native(value.limits.max_payload);
    native(value.limits.max_batch_bytes);
    predecessor_present_ = value.predecessor.has_value();
    native(predecessor_present_);
    if (value.predecessor) {
      add(value.predecessor->journal.bytes());
      native(value.predecessor->validated_sequence);
      native(value.predecessor->validated_end_offset);
    }
  }
  template <class T> void selector(const T &value) noexcept {
    add(value.environment.bytes());
    add(value.root.bytes());
    add(value.active.bytes());
    native(value.generation);
  }
  std::span<const ByteView> view() const noexcept {
    return std::span{fields_}.first(count_);
  }

private:
  std::array<ByteView, 36> fields_{};
  std::size_t count_ = 0;
  bool predecessor_present_ = false;
};
class RawRetainedPair {
public:
  RawRetainedPair(const RawRetainedOrigin &origin, RawRetainedPairSites sites,
                  std::uint64_t credit, std::span<const ByteView> arguments = {});
  ~RawRetainedPair();
  RawRetainedPair(const RawRetainedPair &) = delete;
  RawRetainedPair &operator=(const RawRetainedPair &) = delete;
  RawEventRef reference() const noexcept { return request_; }
  RawRetainedOrigin child() const noexcept;
  void finish(std::uint8_t branch, std::span<const ByteView> fields = {},
              std::optional<Error> error = {}) noexcept;
  void failure(Error error) noexcept { finish(1, {}, error); }
  template <class T>
  void result(const Result<T> &value, std::span<const ByteView> fields = {}) noexcept {
    if (value.has_value())
      finish(0, fields);
    else
      finish(1, fields, value.error());
  }
  template <class F> decltype(auto) run(F &&body) {
    try {
      return std::forward<F>(body)();
    } catch (const Error &error) {
      failure(error);
      throw;
    } catch (const std::bad_alloc &) {
      finish(2);
      throw;
    } catch (...) {
      finish(3);
      throw;
    }
  }

private:
  RawRetainedOrigin origin_;
  RawRetainedPairSites sites_;
  std::optional<RawControlToken> credit_;
  RawEventRef request_{};
  bool finished_ = false;
};
RawEventRef raw_retained_event(const RawRetainedOrigin &origin,
                               RawRetainedEventSite site,
                               std::span<const ByteView> fields);
class RawRetainedEvent {
public:
  RawRetainedEvent(RawRetainedOrigin origin, RawRetainedEventSite site);
  void publish(std::span<const ByteView> fields) noexcept;

private:
  RawRetainedOrigin origin_;
  std::uint32_t site_;
  std::optional<RawControlToken> credit_;
};
template <class F> class RawRetainedFinally {
public:
  explicit RawRetainedFinally(F body) : body_(std::move(body)) {}
  ~RawRetainedFinally() { body_(); }

private:
  F body_;
};

// Stack selection changes native owner origins only. No callback, allocation,
// capacity wait or ambient authorization is performed by the lexical binding.
class RawRetainedOriginScope {
public:
  RawRetainedOriginScope(RawRetainedOrigin &owner, RawRetainedOrigin selected) noexcept;
  ~RawRetainedOriginScope();
  RawRetainedOriginScope(const RawRetainedOriginScope &) = delete;
  RawRetainedOriginScope &operator=(const RawRetainedOriginScope &) = delete;

private:
  RawRetainedOrigin &owner_;
  RawRetainedOrigin previous_;
};

class RawRetainedScope {
public:
  RawRetainedScope(RetainedState &root, RawRetainedSettlementLoan &semantic,
                   RawStorageSettlementLoan *physical, RawEventRef parent,
                   RawRetainedOrigin::Input input = RawRetainedOrigin::Input::no_new);
  ~RawRetainedScope();
  RawRetainedScope(const RawRetainedScope &) = delete;
  RawRetainedScope &operator=(const RawRetainedScope &) = delete;
  void error(Error value) noexcept;
  void suspend();
  Result<void> resume(RawStorageSettlementLoan *physical);

private:
  RetainedState &selected_root_;
  RawRetainedSettlementLoan &semantic_;
  std::shared_ptr<class ArchiveCatalog> archive_;
  std::optional<RawRetainedOriginScope> root_, journal_, catalog_;
  std::optional<RawStorageLoanScope> storage_;
  std::optional<RawRetainedOrigin> suspended_root_, suspended_journal_,
      suspended_catalog_;
  std::shared_ptr<class ArchiveCatalog> suspended_archive_;
};

// One caller keeps the exact acknowledged Decision/Invocation/Admission records.
// Its grants are prepared from the existing codec buffers before publication.
class RawRetainedExec {
public:
  enum class Kind { selected_exec, adapter_receipt };
  RawRetainedExec(RetainedState &root, RawEventRef parent,
                  Kind kind = Kind::selected_exec);
  bool full_exec() const noexcept { return kind_ == Kind::selected_exec; }
  ~RawRetainedExec();
  RawRetainedExec(const RawRetainedExec &) = delete;
  RawRetainedExec &operator=(const RawRetainedExec &) = delete;
  void enter(RawEventRef parent, RawRetainedOrigin::Input input);
  void leave() noexcept;
  void enter_terminal(RawEventRef parent);
  void suspend();
  void resume();
  Result<void> receipt_permission() const;
  Result<std::unique_ptr<RawRetainedJobLoan>> prepare_job(const RawThreadHandle &child);

private:
  friend class RetainedState;
  Result<void> prepare(std::span<const std::vector<std::byte>> encoded);
  RetainedState &root_;
  RawEventRef parent_;
  Kind kind_;
  std::unique_ptr<RawRetainedStopBundle> bundle_;
  std::optional<RawRetainedScope> scope_, terminal_scope_;
  std::unique_ptr<RawRetainedStopBundle> terminal_;
  std::optional<Error> resume_error_;
  std::array<PhysicalJournalRecord, 3> acknowledged_;
  std::optional<OperationAttemptId> attempt_;
  bool accepted_ = false;
};

class RawRetainedJobLoan {
public:
  ~RawRetainedJobLoan();
  void started() noexcept;
  void joined() noexcept;
  void never_started() noexcept;
  void enter(RawEventRef parent);
  void leave() noexcept;
  Result<std::unique_ptr<RawRetainedJobLoan>> renew();
  std::span<PhysicalJournalRecord> acknowledgment() noexcept { return {&terminal_, 1}; }
  bool *acknowledged_flag() noexcept { return &accepted_; }
  bool accepted() const noexcept { return accepted_; }

private:
  friend class RawRetainedExec;
  static Result<std::unique_ptr<RawRetainedJobLoan>>
  prepare(RetainedState &root, const RawThreadHandle &child, std::uint32_t invocation,
          RawEventRef parent);
  RawRetainedJobLoan(RetainedState &root, std::unique_ptr<RawRetainedStopBundle> bundle,
                     std::uint32_t invocation, RawEventRef parent);
  RetainedState &root_;
  std::weak_ptr<void> lifetime_;
  std::unique_ptr<RawRetainedStopBundle> bundle_;
  std::optional<RawRetainedScope> scope_;
  std::uint32_t invocation_;
  RawEventRef parent_;
  PhysicalJournalRecord terminal_;
  bool accepted_ = false;
};

// Terminal owns this guard through the actual join. The worker borrows it.
class RawRetainedTransfer {
public:
  ~RawRetainedTransfer();
  RawRetainedTransfer(const RawRetainedTransfer &) = delete;
  RawRetainedTransfer &operator=(const RawRetainedTransfer &) = delete;
  void stage_before_launch() noexcept;
  void commit_launch() noexcept;
  void rollback_launch() noexcept;
  Result<void> joined();
  RawStorageHandoff *handoff() noexcept { return storage_.get(); }

private:
  friend class RetainedState;
  RawRetainedTransfer(RetainedState &root, RawThreadHandle child,
                      std::unique_ptr<RawStorageHandoff> storage,
                      std::unique_ptr<RawRetainedStopBundle> candidate);
  RetainedState &root_;
  RawThreadHandle parent_, child_;
  std::unique_ptr<RawStorageHandoff> storage_;
  std::unique_ptr<RawRetainedStopBundle> candidate_, previous_;
  RawRetainedOrigin previous_origin_;
  bool staged_ = false, launched_ = false, joined_ = false;
  const void *parent_thread_ = nullptr;
  pid_t parent_process_ = 0;
};
std::string_view raw_retained_description() noexcept;
} // namespace blackbird
