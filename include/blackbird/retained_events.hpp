#pragma once

#include "blackbird/journal.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

namespace blackbird {
struct RawRetainedOrigin;

struct DecisionTag;
struct ComplaintTag;
using DecisionId = Id<DecisionTag>;
using ComplaintId = Id<ComplaintTag>;
struct ApplicationRecordTag;
using ApplicationRecordId = Id<ApplicationRecordTag>;
enum class ApplicationChannel : std::uint16_t { log = 1, context = 2, program = 3 };
// Immutable application bytes are either owned working bytes or a checked disk
// reader with shared storage lifetime. Reading returns an owned buffer and never
// installs a historical cache. Eager accessors deliberately reject cold bytes.
class ImmutableBytes {
public:
  ImmutableBytes() = default;
  ImmutableBytes(std::vector<std::byte> bytes)
      : bytes_(std::make_shared<const std::vector<std::byte>>(std::move(bytes))) {}
  ImmutableBytes(std::initializer_list<std::byte> bytes)
      : ImmutableBytes(std::vector<std::byte>{bytes}) {}
  template <typename Iterator>
  ImmutableBytes(Iterator first, Iterator last)
      : ImmutableBytes(std::vector<std::byte>{first, last}) {}
  using Reader = std::function<Result<std::vector<std::byte>>()>;
  static ImmutableBytes cold(std::size_t size, Reader reader) {
    ImmutableBytes result;
    result.size_ = size;
    result.reader_ = std::make_shared<const Reader>(std::move(reader));
    return result;
  }
  bool is_cold() const noexcept { return bool(reader_); }
  std::size_t resident_bytes() const noexcept { return bytes_ ? bytes_->size() : 0; }
  Result<std::vector<std::byte>> read() const {
    try {
      if (!reader_)
        return Result<std::vector<std::byte>>::success(value());
      auto result = (*reader_)();
      if (result.has_value() && result.value().size() != size_)
        return Result<std::vector<std::byte>>::failure({ErrorCode::corrupt});
      return result;
    } catch (const std::bad_alloc &) {
      return Result<std::vector<std::byte>>::failure({ErrorCode::allocation});
    }
  }
  const std::byte *data() const { return value().data(); }
  std::size_t size() const noexcept {
    return reader_ ? size_ : (bytes_ ? bytes_->size() : 0);
  }
  bool empty() const noexcept { return size() == 0; }
  const std::byte &operator[](std::size_t i) const { return value()[i]; }
  auto begin() const { return value().begin(); }
  auto end() const { return value().end(); }
  friend bool operator==(const ImmutableBytes &a, const ImmutableBytes &b) {
    if (a.size() != b.size())
      return false;
    if (!a.reader_ && !b.reader_)
      return a.bytes_ == b.bytes_ || a.value() == b.value();
    auto left = a.read();
    if (!left.has_value())
      throw left.error();
    auto right = b.read();
    if (!right.has_value())
      throw right.error();
    return left.value() == right.value();
  }

private:
  const std::vector<std::byte> &value() const {
    // An eager accessor must never turn a historical visit into a pinned cache.
    if (reader_)
      throw Error{ErrorCode::stale_handle};
    static const std::vector<std::byte> empty;
    return bytes_ ? *bytes_ : empty;
  }
  std::shared_ptr<const std::vector<std::byte>> bytes_;
  std::shared_ptr<const Reader> reader_;
  std::size_t size_ = 0;
};
struct ApplicationRecordEvent {
  ApplicationRecordId identity;
  ApplicationChannel channel;
  ImmutableBytes payload;
  bool operator==(const ApplicationRecordEvent &) const = default;
};
struct SourceReference {
  AuditStreamId journal;
  std::uint64_t sequence;
  bool operator==(const SourceReference &) const = default;
};
enum class RetainedKind : std::uint16_t {
  reservation = 1,
  decision = 2,
  invocation = 3,
  attempt_admitted = 4,
  attempt_open = 5,
  observation = 6,
  retry = 7,
  identity_conflict = 8,
  complaint = 9,
  rejected_submission = 10,
  adapter_receipt = 11,
  recovery_choice = 12,
  provisional_capture = 13,
  application = 14
};
struct IssuerReservationEvent {
  std::uint64_t counter;
  bool operator==(const IssuerReservationEvent &) const = default;
};
struct DecisionEvent {
  DecisionId decision;
  ParticipantId actor;
  ConversationId conversation;
  WorkflowId workflow;
  DefinitionGenerationId definition;
  ContextRevisionId context;
  std::vector<InvocationId> planned_invocations;
  ImmutableBytes continuation;
  bool operator==(const DecisionEvent &) const = default;
};
struct InvocationEvent {
  InvocationId invocation;
  DecisionId decision;
  DefinitionGenerationId definition;
  ImmutableBytes input;
  bool operator==(const InvocationEvent &) const = default;
};
struct AttemptAdmissionEvent {
  OperationAttemptId attempt;
  InvocationId invocation;
  DecisionId decision;
  ImmutableBytes input;
  // Schema 2 omits input bytes; RetainedState resolves this invocation reference.
  bool input_from_invocation = false;
  bool operator==(const AttemptAdmissionEvent &) const = default;
};
struct AttemptOpenEvent {
  OperationAttemptId attempt;
  bool operator==(const AttemptOpenEvent &) const = default;
};
enum class AttemptPhase : std::uint8_t { running = 1, settling = 2, terminal = 3 };
enum class AttemptDisposition : std::uint8_t {
  none = 0,
  success = 1,
  failure = 2,
  cancellation = 3,
  unknown = 4
};
struct AttemptObservationEvent {
  OperationAttemptId attempt;
  AttemptPhase phase;
  AttemptDisposition disposition;
  ImmutableBytes observation;
  bool operator==(const AttemptObservationEvent &) const = default;
};
struct RetryEvent {
  DecisionId decision;
  InvocationId invocation;
  OperationAttemptId attempt;
  bool operator==(const RetryEvent &) const = default;
};
struct IdentityConflictEvent {
  RetainedKind disputed_kind;
  IdentityBytes disputed_identity;
  ImmutableBytes proposal;
  bool operator==(const IdentityConflictEvent &) const = default;
};
struct ComplaintEvent {
  ComplaintId complaint;
  ParticipantId actor;
  ImmutableBytes detail;
  bool operator==(const ComplaintEvent &) const = default;
};
struct RejectedSubmissionEvent {
  Error reason;
  bool operator==(const RejectedSubmissionEvent &) const = default;
};
struct AdapterReceiptEvent {
  OperationAttemptId attempt;
  std::optional<Error> failure;
  bool operator==(const AdapterReceiptEvent &) const = default;
};
enum class RetainedEvidence : std::uint8_t {
  live,
  recovered_pending,
  recovered,
  uncertain
};
enum class CapturePurpose : std::uint16_t {
  ordinary_submission = 1,
  rejected_proposal = 2
};
struct ProvisionalCaptureEvent {
  AuditStreamId origin;
  std::uint64_t first_sequence;
  std::uint64_t proposal_length;
  CapturePurpose purpose;
  bool operator==(const ProvisionalCaptureEvent &) const = default;
};
struct CheckedAttempt {
  OperationAttemptId attempt;
  bool opened;
  std::optional<AttemptPhase> observed_phase;
  AttemptDisposition disposition;
  RetainedEvidence prior_evidence;
  bool operator==(const CheckedAttempt &) const = default;
};
// Wire declarations describe policy; replay never uses untrusted capacities to
// choose an allocation bound. The selected-chain owner validates actual limits.
struct RetainedCapacityDeclaration {
  std::uint64_t max_file_bytes;
  std::uint64_t max_records;
  bool operator==(const RetainedCapacityDeclaration &) const = default;
};
struct RecoveryChoiceEvent {
  JournalPredecessor predecessor;
  std::uint64_t diagnostic_offset;
  std::uint64_t available_end;
  JournalLimits old_limits;
  RetainedCapacityDeclaration old_capacity;
  RetainedCapacityDeclaration new_capacity;
  std::optional<Error> problem;
  std::vector<CheckedAttempt> checked_attempts;
  std::vector<ProvisionalCaptureEvent> required_captures;
  bool operator==(const RecoveryChoiceEvent &) const = default;
};
using RetainedBody =
    std::variant<IssuerReservationEvent, DecisionEvent, InvocationEvent,
                 AttemptAdmissionEvent, AttemptOpenEvent, AttemptObservationEvent,
                 RetryEvent, IdentityConflictEvent, ComplaintEvent,
                 RejectedSubmissionEvent, AdapterReceiptEvent, RecoveryChoiceEvent,
                 ProvisionalCaptureEvent, ApplicationRecordEvent>;
struct RetainedEvent {
  std::vector<SourceReference> dependencies;
  RetainedBody body;
  bool operator==(const RetainedEvent &) const = default;
};

RetainedKind retained_kind(const RetainedBody &body) noexcept;
// Owned, exact wire records. Successful decoding does not validate a ledger
// transition or authorize a source dependency. RetainedState owns root checks;
// the selected-chain owner alone validates maintenance placement and capture sets.
Result<std::vector<std::byte>>
encode_retained_event(const RetainedEvent &event, std::uint32_t max_payload,
                      const RawRetainedOrigin *raw_origin = nullptr);
Result<RetainedEvent>
decode_retained_event(ByteView bytes, std::uint32_t max_payload,
                      const RawRetainedOrigin *raw_origin = nullptr);

} // namespace blackbird
