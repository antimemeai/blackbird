#include "blackbird/retained_events.hpp"
#include "blackbird/raw_retained.hpp"

#include <algorithm>
#include <type_traits>

namespace blackbird {
namespace {
class Encoder {
public:
  explicit Encoder(std::uint32_t limit) : limit_(limit) {}
  void raw(ByteView bytes) {
    if (failed_ || bytes.size() > limit_ - bytes_.size()) {
      failed_ = true;
      return;
    }
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }
  template <std::size_t Width> void number(std::uint64_t value) {
    static_assert(Width > 0 && Width <= 8);
    std::array<std::byte, 8> bytes{};
    for (std::size_t offset = 0; offset < Width; ++offset) {
      bytes[offset] = static_cast<std::byte>(value & 255);
      value >>= 8;
    }
    raw(ByteView{bytes}.first(Width));
  }
  void count(std::size_t value) {
    if (value > UINT32_MAX) {
      failed_ = true;
      return;
    }
    number<4>(value);
  }
  template <typename T> void identity(const T &value) { raw(value.bytes()); }
  void blob(ByteView bytes) {
    count(bytes.size());
    raw(bytes);
  }
  void blob(const ImmutableBytes &bytes) {
    auto owned = bytes.read();
    if (!owned.has_value())
      throw owned.error();
    blob(ByteView{owned.value()});
  }
  Result<std::vector<std::byte>> finish() && {
    return failed_ ? Result<std::vector<std::byte>>::failure({ErrorCode::capacity})
                   : Result<std::vector<std::byte>>::success(std::move(bytes_));
  }

private:
  std::size_t limit_;
  std::vector<std::byte> bytes_;
  bool failed_ = false;
};
class Decoder {
public:
  explicit Decoder(ByteView bytes) : bytes_(bytes) {}
  ByteView take(std::size_t count) noexcept {
    if (failed_ || count > remaining()) {
      failed_ = true;
      return {};
    }
    const auto view = bytes_.subspan(offset_, count);
    offset_ += count;
    return view;
  }
  std::uint64_t number(std::size_t width) noexcept {
    const auto bytes = take(width);
    std::uint64_t value = 0;
    for (std::size_t offset = 0; offset < bytes.size(); ++offset) {
      value |= static_cast<std::uint64_t>(std::to_integer<unsigned int>(bytes[offset]))
               << (offset * 8);
    }
    return value;
  }
  template <typename T> Result<T> identity() noexcept {
    const auto view = take(16);
    if (failed_) {
      return Result<T>::failure({ErrorCode::corrupt});
    }
    IdentityBytes bytes{};
    std::copy(view.begin(), view.end(), bytes.begin());
    auto result = T::from_bytes(bytes);
    if (!result.has_value()) {
      failed_ = true;
      return Result<T>::failure({ErrorCode::corrupt});
    }
    return result;
  }
  std::vector<std::byte> blob() {
    const auto count = number(4);
    const auto view = take(static_cast<std::size_t>(count));
    return {view.begin(), view.end()};
  }
  template <typename T> Result<std::vector<T>> identities() {
    const auto count = number(4);
    if (failed_ || count > remaining() / 16) {
      return Result<std::vector<T>>::failure({ErrorCode::corrupt});
    }
    std::vector<T> ids;
    ids.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
      auto result = identity<T>();
      if (!result.has_value()) {
        return Result<std::vector<T>>::failure(result.error());
      }
      ids.push_back(std::move(result).value());
    }
    return Result<std::vector<T>>::success(std::move(ids));
  }
  bool complete() const noexcept { return !failed_ && remaining() == 0; }
  bool failed() const noexcept { return failed_; }
  std::size_t remaining() const noexcept { return bytes_.size() - offset_; }

private:
  ByteView bytes_;
  std::size_t offset_ = 0;
  bool failed_ = false;
};
bool valid_observation(AttemptPhase phase, AttemptDisposition disposition) noexcept {
  switch (phase) {
  case AttemptPhase::running:
  case AttemptPhase::settling:
    return disposition == AttemptDisposition::none;
  case AttemptPhase::terminal:
    return disposition == AttemptDisposition::success ||
           disposition == AttemptDisposition::failure ||
           disposition == AttemptDisposition::cancellation ||
           disposition == AttemptDisposition::unknown;
  }
  return false;
}
bool valid_conflict(const IdentityConflictEvent &event) noexcept {
  const auto nonzero =
      std::any_of(event.disputed_identity.begin(), event.disputed_identity.end(),
                  [](std::byte value) { return value != std::byte{0}; });
  return nonzero && (event.disputed_kind == RetainedKind::decision ||
                     event.disputed_kind == RetainedKind::invocation ||
                     event.disputed_kind == RetainedKind::attempt_admitted ||
                     event.disputed_kind == RetainedKind::application);
}
bool valid_capture(const ProvisionalCaptureEvent &event) noexcept {
  return event.first_sequence != 0 && event.proposal_length >= 48 &&
         (event.purpose == CapturePurpose::ordinary_submission ||
          event.purpose == CapturePurpose::rejected_proposal);
}
bool capture_less(const ProvisionalCaptureEvent &left,
                  const ProvisionalCaptureEvent &right) noexcept {
  if (left.origin != right.origin) {
    return left.origin.bytes() < right.origin.bytes();
  }
  if (left.first_sequence != right.first_sequence) {
    return left.first_sequence < right.first_sequence;
  }
  return left.purpose < right.purpose;
}
bool valid_choice(const RecoveryChoiceEvent &event) noexcept {
  if (event.problem && static_cast<std::uint8_t>(event.problem->code) >
                           static_cast<std::uint8_t>(ErrorCode::provider_rate_limit)) {
    return false;
  }
  for (std::size_t index = 0; index < event.checked_attempts.size(); ++index) {
    const auto &checked = event.checked_attempts[index];
    if ((checked.observed_phase
             ? !valid_observation(*checked.observed_phase, checked.disposition)
             : checked.disposition != AttemptDisposition::none) ||
        static_cast<std::uint8_t>(checked.prior_evidence) >
            static_cast<std::uint8_t>(RetainedEvidence::uncertain) ||
        (index != 0 && event.checked_attempts[index - 1].attempt.bytes() >=
                           checked.attempt.bytes())) {
      return false;
    }
  }
  for (std::size_t index = 0; index < event.required_captures.size(); ++index) {
    const auto &capture = event.required_captures[index];
    if (!valid_capture(capture) ||
        (index != 0 && !capture_less(event.required_captures[index - 1], capture))) {
      return false;
    }
  }
  return true;
}
void encode_body(Encoder &encoder, const IssuerReservationEvent &body) {
  encoder.number<8>(body.counter);
}
void encode_body(Encoder &encoder, const DecisionEvent &body) {
  encoder.identity(body.decision);
  encoder.identity(body.actor);
  encoder.identity(body.conversation);
  encoder.identity(body.workflow);
  encoder.identity(body.definition);
  encoder.identity(body.context);
  encoder.count(body.planned_invocations.size());
  for (const auto &invocation : body.planned_invocations) {
    encoder.identity(invocation);
  }
  encoder.blob(body.continuation);
}
void encode_body(Encoder &encoder, const InvocationEvent &body) {
  encoder.identity(body.invocation);
  encoder.identity(body.decision);
  encoder.identity(body.definition);
  encoder.blob(body.input);
}
void encode_body(Encoder &encoder, const AttemptAdmissionEvent &body) {
  encoder.identity(body.attempt);
  encoder.identity(body.invocation);
  encoder.identity(body.decision);
  if (!body.input_from_invocation)
    encoder.blob(body.input);
}
void encode_body(Encoder &encoder, const AttemptOpenEvent &body) {
  encoder.identity(body.attempt);
}
void encode_body(Encoder &encoder, const AttemptObservationEvent &body) {
  encoder.identity(body.attempt);
  encoder.number<1>(static_cast<std::uint8_t>(body.phase));
  encoder.number<1>(static_cast<std::uint8_t>(body.disposition));
  encoder.number<2>(0);
  encoder.blob(body.observation);
}
void encode_body(Encoder &encoder, const RetryEvent &body) {
  encoder.identity(body.decision);
  encoder.identity(body.invocation);
  encoder.identity(body.attempt);
}
void encode_body(Encoder &encoder, const IdentityConflictEvent &body) {
  encoder.number<2>(static_cast<std::uint16_t>(body.disputed_kind));
  encoder.raw(body.disputed_identity);
  encoder.blob(body.proposal);
}
void encode_body(Encoder &encoder, const ComplaintEvent &body) {
  encoder.identity(body.complaint);
  encoder.identity(body.actor);
  encoder.blob(body.detail);
}
void encode_body(Encoder &encoder, const ApplicationRecordEvent &body) {
  encoder.identity(body.identity);
  encoder.number<2>(static_cast<std::uint16_t>(body.channel));
  encoder.blob(body.payload);
}
void encode_body(Encoder &encoder, const RejectedSubmissionEvent &body) {
  encoder.number<2>(static_cast<std::uint8_t>(body.reason.code));
  encoder.number<2>(0);
  encoder.number<8>(static_cast<std::uint64_t>(body.reason.detail));
}
void encode_body(Encoder &encoder, const AdapterReceiptEvent &body) {
  encoder.identity(body.attempt);
  encoder.number<2>(body.failure ? 0 : 1);
  encoder.number<2>(body.failure ? static_cast<std::uint8_t>(body.failure->code) : 0);
  encoder.number<8>(body.failure ? static_cast<std::uint64_t>(body.failure->detail)
                                 : 0);
}
void encode_body(Encoder &encoder, const ProvisionalCaptureEvent &body) {
  encoder.identity(body.origin);
  encoder.number<8>(body.first_sequence);
  encoder.number<8>(body.proposal_length);
  encoder.number<2>(static_cast<std::uint16_t>(body.purpose));
  encoder.number<2>(0);
}
void encode_body(Encoder &encoder, const RecoveryChoiceEvent &body) {
  encoder.identity(body.predecessor.journal);
  encoder.number<8>(body.predecessor.validated_sequence);
  encoder.number<8>(body.predecessor.validated_end_offset);
  encoder.number<8>(body.diagnostic_offset);
  encoder.number<8>(body.available_end);
  encoder.number<4>(body.old_limits.max_payload);
  encoder.number<4>(body.old_limits.max_batch_bytes);
  encoder.number<8>(body.old_capacity.max_file_bytes);
  encoder.number<8>(body.old_capacity.max_records);
  encoder.number<8>(body.new_capacity.max_file_bytes);
  encoder.number<8>(body.new_capacity.max_records);
  encoder.number<2>(body.problem ? 1 : 0);
  encoder.number<2>(body.problem ? static_cast<std::uint8_t>(body.problem->code) : 0);
  encoder.number<8>(body.problem ? static_cast<std::uint64_t>(body.problem->detail)
                                 : 0);
  encoder.count(body.checked_attempts.size());
  for (const auto &checked : body.checked_attempts) {
    encoder.identity(checked.attempt);
    encoder.number<1>(checked.opened ? 1 : 0);
    encoder.number<1>(checked.observed_phase
                          ? static_cast<std::uint8_t>(*checked.observed_phase)
                          : 0);
    encoder.number<1>(static_cast<std::uint8_t>(checked.disposition));
    encoder.number<1>(static_cast<std::uint8_t>(checked.prior_evidence));
  }
  encoder.count(body.required_captures.size());
  for (const auto &capture : body.required_captures) {
    encode_body(encoder, capture);
  }
}
Result<ProvisionalCaptureEvent> decode_capture(Decoder &decoder) {
  auto origin = decoder.identity<AuditStreamId>();
  const auto first_sequence = decoder.number(8);
  const auto length = decoder.number(8);
  const auto purpose = decoder.number(2);
  const auto reserved = decoder.number(2);
  if (!origin.has_value() || decoder.failed() || reserved != 0 || purpose < 1 ||
      purpose > 2) {
    return Result<ProvisionalCaptureEvent>::failure({ErrorCode::corrupt});
  }
  ProvisionalCaptureEvent capture{std::move(origin).value(), first_sequence, length,
                                  static_cast<CapturePurpose>(purpose)};
  if (!valid_capture(capture)) {
    return Result<ProvisionalCaptureEvent>::failure({ErrorCode::corrupt});
  }
  return Result<ProvisionalCaptureEvent>::success(std::move(capture));
}
bool valid_body(const RetainedBody &body) {
  return std::visit(
      [](const auto &event) {
        using T = std::decay_t<decltype(event)>;
        if constexpr (std::is_same_v<T, IssuerReservationEvent>) {
          return event.counter != 0;
        } else if constexpr (std::is_same_v<T, AttemptObservationEvent>) {
          return valid_observation(event.phase, event.disposition);
        } else if constexpr (std::is_same_v<T, IdentityConflictEvent>) {
          return valid_conflict(event);
        } else if constexpr (std::is_same_v<T, RejectedSubmissionEvent>) {
          return static_cast<std::uint8_t>(event.reason.code) <=
                 static_cast<std::uint8_t>(ErrorCode::provider_rate_limit);
        } else if constexpr (std::is_same_v<T, AdapterReceiptEvent>) {
          return !event.failure ||
                 static_cast<std::uint8_t>(event.failure->code) <=
                     static_cast<std::uint8_t>(ErrorCode::provider_rate_limit);
        } else if constexpr (std::is_same_v<T, RecoveryChoiceEvent>) {
          return valid_choice(event);
        } else if constexpr (std::is_same_v<T, ProvisionalCaptureEvent>) {
          return valid_capture(event);
        } else if constexpr (std::is_same_v<T, ApplicationRecordEvent>) {
          return static_cast<std::uint16_t>(event.channel) >= 1 &&
                 static_cast<std::uint16_t>(event.channel) <= 3;
        } else {
          return true;
        }
      },
      body);
}
} // namespace

RetainedKind retained_kind(const RetainedBody &body) noexcept {
  // This mapping is explicit: reordering/adding a variant alternative does not
  // silently change the persistent wire kind.
  return std::visit(
      [](const auto &event) {
        using T = std::decay_t<decltype(event)>;
        if constexpr (std::is_same_v<T, IssuerReservationEvent>) {
          return RetainedKind::reservation;
        } else if constexpr (std::is_same_v<T, DecisionEvent>) {
          return RetainedKind::decision;
        } else if constexpr (std::is_same_v<T, InvocationEvent>) {
          return RetainedKind::invocation;
        } else if constexpr (std::is_same_v<T, AttemptAdmissionEvent>) {
          return RetainedKind::attempt_admitted;
        } else if constexpr (std::is_same_v<T, AttemptOpenEvent>) {
          return RetainedKind::attempt_open;
        } else if constexpr (std::is_same_v<T, AttemptObservationEvent>) {
          return RetainedKind::observation;
        } else if constexpr (std::is_same_v<T, RetryEvent>) {
          return RetainedKind::retry;
        } else if constexpr (std::is_same_v<T, IdentityConflictEvent>) {
          return RetainedKind::identity_conflict;
        } else if constexpr (std::is_same_v<T, ComplaintEvent>) {
          return RetainedKind::complaint;
        } else if constexpr (std::is_same_v<T, RejectedSubmissionEvent>) {
          return RetainedKind::rejected_submission;
        } else if constexpr (std::is_same_v<T, AdapterReceiptEvent>) {
          return RetainedKind::adapter_receipt;
        } else if constexpr (std::is_same_v<T, RecoveryChoiceEvent>) {
          return RetainedKind::recovery_choice;
        } else if constexpr (std::is_same_v<T, ApplicationRecordEvent>) {
          return RetainedKind::application;
        } else {
          return RetainedKind::provisional_capture;
        }
      },
      body);
}
Result<std::vector<std::byte>>
encode_retained_event(const RetainedEvent &event, std::uint32_t max_payload,
                      const RawRetainedOrigin *raw_origin) {
  if (!valid_body(event.body) ||
      (std::holds_alternative<RecoveryChoiceEvent>(event.body) &&
       !event.dependencies.empty()) ||
      (std::holds_alternative<ProvisionalCaptureEvent>(event.body) &&
       event.dependencies.empty()) ||
      std::any_of(
          event.dependencies.begin(), event.dependencies.end(),
          [](const SourceReference &reference) { return reference.sequence == 0; })) {
    return Result<std::vector<std::byte>>::failure({ErrorCode::invalid_range});
  }
  try {
    Encoder encoder{max_payload};
    const auto *admission = std::get_if<AttemptAdmissionEvent>(&event.body);
    const std::uint64_t schema = admission && admission->input_from_invocation ? 2 : 1;
    const auto kind = retained_kind(event.body);
    const auto dependency_count = event.dependencies.size();
    if (raw_origin) {
      RawRetainedFields fields;
      fields.native(kind);
      fields.native(schema);
      fields.native(dependency_count);
      (void)raw_retained_event(*raw_origin, {3112, 58}, fields.view());
    }
    encoder.number<2>(schema);
    encoder.number<2>(static_cast<std::uint16_t>(kind));
    encoder.count(dependency_count);
    for (const auto &reference : event.dependencies) {
      encoder.identity(reference.journal);
      encoder.number<8>(reference.sequence);
    }
    std::visit([&](const auto &body) { encode_body(encoder, body); }, event.body);
    return std::move(encoder).finish();
  } catch (const Error &error) {
    return Result<std::vector<std::byte>>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<std::vector<std::byte>>::failure({ErrorCode::allocation});
  }
}

Result<RetainedEvent> decode_retained_event(ByteView bytes, std::uint32_t max_payload,
                                            const RawRetainedOrigin *raw_origin) {
  if (bytes.size() > max_payload) {
    return Result<RetainedEvent>::failure({ErrorCode::capacity});
  }
  try {
    Decoder decoder{bytes};
    const auto schema = decoder.number(2);
    const auto kind = decoder.number(2);
    const auto count = decoder.number(4);
    if (raw_origin) {
      RawRetainedFields fields;
      fields.native(schema);
      fields.native(kind);
      fields.native(count);
      (void)raw_retained_event(*raw_origin, {3113, 64}, fields.view());
    }
    if (decoder.failed() || count > decoder.remaining() / 24) {
      return Result<RetainedEvent>::failure({ErrorCode::corrupt});
    }
    if (schema != 1 && !(schema == 2 && kind == 4)) {
      return Result<RetainedEvent>::failure({ErrorCode::unsupported});
    }
    std::vector<SourceReference> dependencies;
    dependencies.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
      auto journal = decoder.identity<AuditStreamId>();
      const auto sequence = decoder.number(8);
      if (!journal.has_value() || sequence == 0 || decoder.failed()) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      dependencies.push_back({std::move(journal).value(), sequence});
    }
    auto completed = [&](RetainedBody body) {
      if (!decoder.complete() || !valid_body(body) ||
          (std::holds_alternative<RecoveryChoiceEvent>(body) &&
           !dependencies.empty()) ||
          (std::holds_alternative<ProvisionalCaptureEvent>(body) &&
           dependencies.empty())) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      return Result<RetainedEvent>::success({std::move(dependencies), std::move(body)});
    };
    switch (kind) {
    case 1:
      return completed(IssuerReservationEvent{decoder.number(8)});
    case 2: {
      auto decision = decoder.identity<DecisionId>();
      auto actor = decoder.identity<ParticipantId>();
      auto conversation = decoder.identity<ConversationId>();
      auto workflow = decoder.identity<WorkflowId>();
      auto definition = decoder.identity<DefinitionGenerationId>();
      auto context = decoder.identity<ContextRevisionId>();
      auto invocations = decoder.identities<InvocationId>();
      auto continuation = decoder.blob();
      if (!decision.has_value() || !actor.has_value() || !conversation.has_value() ||
          !workflow.has_value() || !definition.has_value() || !context.has_value() ||
          !invocations.has_value()) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      return completed(
          DecisionEvent{std::move(decision).value(), std::move(actor).value(),
                        std::move(conversation).value(), std::move(workflow).value(),
                        std::move(definition).value(), std::move(context).value(),
                        std::move(invocations).value(), std::move(continuation)});
    }
    case 3: {
      auto invocation = decoder.identity<InvocationId>();
      auto decision = decoder.identity<DecisionId>();
      auto definition = decoder.identity<DefinitionGenerationId>();
      auto input = decoder.blob();
      if (!invocation.has_value() || !decision.has_value() || !definition.has_value()) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      return completed(
          InvocationEvent{std::move(invocation).value(), std::move(decision).value(),
                          std::move(definition).value(), std::move(input)});
    }
    case 4: {
      auto attempt = decoder.identity<OperationAttemptId>();
      auto invocation = decoder.identity<InvocationId>();
      auto decision = decoder.identity<DecisionId>();
      auto input = schema == 2 ? std::vector<std::byte>{} : decoder.blob();
      if (!attempt.has_value() || !invocation.has_value() || !decision.has_value()) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      return completed(AttemptAdmissionEvent{
          std::move(attempt).value(), std::move(invocation).value(),
          std::move(decision).value(), std::move(input), schema == 2});
    }
    case 5: {
      auto attempt = decoder.identity<OperationAttemptId>();
      if (!attempt.has_value()) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      return completed(AttemptOpenEvent{std::move(attempt).value()});
    }
    case 6: {
      auto attempt = decoder.identity<OperationAttemptId>();
      const auto phase = decoder.number(1);
      const auto disposition = decoder.number(1);
      const auto reserved = decoder.number(2);
      auto observation = decoder.blob();
      if (!attempt.has_value() || phase < 1 || phase > 3 || disposition > 4 ||
          reserved != 0) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      return completed(AttemptObservationEvent{
          std::move(attempt).value(), static_cast<AttemptPhase>(phase),
          static_cast<AttemptDisposition>(disposition), std::move(observation)});
    }
    case 7: {
      auto decision = decoder.identity<DecisionId>();
      auto invocation = decoder.identity<InvocationId>();
      auto attempt = decoder.identity<OperationAttemptId>();
      if (!decision.has_value() || !invocation.has_value() || !attempt.has_value()) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      return completed(RetryEvent{std::move(decision).value(),
                                  std::move(invocation).value(),
                                  std::move(attempt).value()});
    }
    case 8: {
      const auto disputed = decoder.number(2);
      const auto identity_view = decoder.take(16);
      auto proposal = decoder.blob();
      if (disputed != 2 && disputed != 3 && disputed != 4 && disputed != 14) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      IdentityBytes identity{};
      std::copy(identity_view.begin(), identity_view.end(), identity.begin());
      return completed(IdentityConflictEvent{static_cast<RetainedKind>(disputed),
                                             identity, std::move(proposal)});
    }
    case 9: {
      auto complaint = decoder.identity<ComplaintId>();
      auto actor = decoder.identity<ParticipantId>();
      auto detail = decoder.blob();
      if (!complaint.has_value() || !actor.has_value()) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      return completed(ComplaintEvent{std::move(complaint).value(),
                                      std::move(actor).value(), std::move(detail)});
    }
    case 14: {
      auto identity = decoder.identity<ApplicationRecordId>();
      const auto channel = decoder.number(2);
      auto payload = decoder.blob();
      if (!identity.has_value())
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      return completed(ApplicationRecordEvent{std::move(identity).value(),
                                              static_cast<ApplicationChannel>(channel),
                                              std::move(payload)});
    }
    case 10: {
      const auto reason = decoder.number(2);
      const auto reserved = decoder.number(2);
      const auto detail = decoder.number(8);
      if (reason > static_cast<std::uint8_t>(ErrorCode::provider_rate_limit) ||
          reserved != 0) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      // C++20 integral conversion specifies the congruent signed value.
      return completed(RejectedSubmissionEvent{
          {static_cast<ErrorCode>(reason), static_cast<std::int64_t>(detail)}});
    }
    case 11: {
      auto attempt = decoder.identity<OperationAttemptId>();
      const auto success = decoder.number(2);
      const auto reason = decoder.number(2);
      const auto detail = decoder.number(8);
      if (!attempt.has_value() || success > 1 ||
          reason > static_cast<std::uint8_t>(ErrorCode::provider_rate_limit) ||
          (success == 1 && (reason != 0 || detail != 0))) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      return completed(AdapterReceiptEvent{
          std::move(attempt).value(),
          success == 1 ? std::nullopt
                       : std::optional<Error>{{static_cast<ErrorCode>(reason),
                                               static_cast<std::int64_t>(detail)}}});
    }
    case 12: {
      auto journal = decoder.identity<AuditStreamId>();
      const auto sequence = decoder.number(8);
      const auto end = decoder.number(8);
      const auto diagnostic = decoder.number(8);
      const auto available = decoder.number(8);
      const auto payload = decoder.number(4);
      const auto batch = decoder.number(4);
      const auto old_file = decoder.number(8);
      const auto old_records = decoder.number(8);
      const auto new_file = decoder.number(8);
      const auto new_records = decoder.number(8);
      const auto present = decoder.number(2);
      const auto reason = decoder.number(2);
      const auto detail = decoder.number(8);
      const auto checked_count = decoder.number(4);
      // Reserve only after both count framing and the remaining capture count
      // have been bounded against bytes actually present, not declared policy.
      if (!journal.has_value() || decoder.failed() || present > 1 ||
          reason > static_cast<std::uint8_t>(ErrorCode::provider_rate_limit) ||
          (present == 0 && (reason != 0 || detail != 0)) || decoder.remaining() < 4 ||
          checked_count > (decoder.remaining() - 4) / 20) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      std::vector<CheckedAttempt> checked;
      checked.reserve(static_cast<std::size_t>(checked_count));
      for (std::uint64_t index = 0; index < checked_count; ++index) {
        auto attempt = decoder.identity<OperationAttemptId>();
        const auto opened = decoder.number(1);
        const auto phase = decoder.number(1);
        const auto disposition = decoder.number(1);
        const auto evidence = decoder.number(1);
        if (!attempt.has_value() || decoder.failed() || opened > 1 || phase > 3 ||
            disposition > 4 || evidence > 3) {
          return Result<RetainedEvent>::failure({ErrorCode::corrupt});
        }
        checked.push_back(
            {std::move(attempt).value(), opened == 1,
             phase == 0 ? std::nullopt
                        : std::optional<AttemptPhase>{static_cast<AttemptPhase>(phase)},
             static_cast<AttemptDisposition>(disposition),
             static_cast<RetainedEvidence>(evidence)});
      }
      const auto capture_count = decoder.number(4);
      if (decoder.failed() || capture_count > decoder.remaining() / 36) {
        return Result<RetainedEvent>::failure({ErrorCode::corrupt});
      }
      std::vector<ProvisionalCaptureEvent> captures;
      captures.reserve(static_cast<std::size_t>(capture_count));
      for (std::uint64_t index = 0; index < capture_count; ++index) {
        auto capture = decode_capture(decoder);
        if (!capture.has_value()) {
          return Result<RetainedEvent>::failure(capture.error());
        }
        captures.push_back(std::move(capture).value());
      }
      return completed(RecoveryChoiceEvent{
          {std::move(journal).value(), sequence, end},
          diagnostic,
          available,
          {static_cast<std::uint32_t>(payload), static_cast<std::uint32_t>(batch)},
          {old_file, old_records},
          {new_file, new_records},
          present == 0 ? std::nullopt
                       : std::optional<Error>{{static_cast<ErrorCode>(reason),
                                               static_cast<std::int64_t>(detail)}},
          std::move(checked),
          std::move(captures)});
    }
    case 13: {
      auto capture = decode_capture(decoder);
      if (!capture.has_value()) {
        return Result<RetainedEvent>::failure(capture.error());
      }
      return completed(std::move(capture).value());
    }
    default:
      return Result<RetainedEvent>::failure({ErrorCode::unsupported});
    }
  } catch (const std::bad_alloc &) {
    return Result<RetainedEvent>::failure({ErrorCode::allocation});
  }
}
} // namespace blackbird
