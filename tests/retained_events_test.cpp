#include "blackbird/retained_events.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

using namespace blackbird;
namespace {
std::atomic<std::ptrdiff_t> fail_allocation{-1};
}
void *operator new(std::size_t size) {
  if (fail_allocation.load() >= 0 && fail_allocation.fetch_sub(1) == 0) {
    throw std::bad_alloc{};
  }
  if (void *pointer = std::malloc(size == 0 ? 1 : size)) {
    return pointer;
  }
  throw std::bad_alloc{};
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
#if defined(__linux__)
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { std::free(pointer); }
#endif

namespace {
void check(bool condition, const char *expression, int line) {
  if (!condition) {
    throw std::runtime_error(std::to_string(line) + ": " + expression);
  }
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
template <typename T> T require(Result<T> result) {
  CHECK(result.has_value());
  return std::move(result).value();
}
template <typename T> T id(unsigned char value) {
  IdentityBytes bytes{};
  bytes[15] = std::byte{value};
  return require(T::from_bytes(bytes));
}
const std::vector<std::byte> original{std::byte{0}, std::byte{255}, std::byte{10}};
DecisionEvent decision() {
  return {id<DecisionId>(1),
          id<ParticipantId>(2),
          id<ConversationId>(3),
          id<WorkflowId>(4),
          id<DefinitionGenerationId>(5),
          id<ContextRevisionId>(6),
          {id<InvocationId>(7)},
          original};
}
void reservation_bytes() {
  const RetainedEvent event{{}, IssuerReservationEvent{0x0102030405060708}};
  const auto bytes = require(encode_retained_event(event, 256));
  const std::array expected{std::byte{1}, std::byte{0}, std::byte{1}, std::byte{0},
                            std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                            std::byte{8}, std::byte{7}, std::byte{6}, std::byte{5},
                            std::byte{4}, std::byte{3}, std::byte{2}, std::byte{1}};
  CHECK(std::equal(expected.begin(), expected.end(), bytes.begin(), bytes.end()));
  CHECK(require(decode_retained_event(expected, 256)) == event);
  for (std::size_t end = 0; end < expected.size(); ++end) {
    CHECK(!decode_retained_event(ByteView{expected}.first(end), 256).has_value());
  }
  auto zero = bytes;
  std::fill(zero.begin() + 8, zero.end(), std::byte{0});
  CHECK(!decode_retained_event(zero, 256).has_value());
  CHECK(!encode_retained_event({{}, IssuerReservationEvent{0}}, 256).has_value());
  CHECK(!encode_retained_event(event, 15).has_value());
  CHECK(require(encode_retained_event(event, 16)) == bytes);
  const auto too_small = decode_retained_event(bytes, 15);
  CHECK(!too_small.has_value() && too_small.error().code == ErrorCode::capacity);
}
void admission_reference_bytes() {
  const RetainedEvent event{{},
                            AttemptAdmissionEvent{id<OperationAttemptId>(8),
                                                  id<InvocationId>(7),
                                                  id<DecisionId>(1), original, true}};
  const auto bytes = require(encode_retained_event(event, 56));
  CHECK(bytes.size() == 56 && bytes[0] == std::byte{2} && bytes[2] == std::byte{4});
  const auto decoded = require(decode_retained_event(bytes, 56));
  const auto &admission = std::get<AttemptAdmissionEvent>(decoded.body);
  CHECK(admission.input.empty() && admission.input_from_invocation);
  CHECK(admission.attempt == id<OperationAttemptId>(8));
  CHECK(admission.invocation == id<InvocationId>(7));
  CHECK(admission.decision == id<DecisionId>(1));
  CHECK(require(encode_retained_event(decoded, 56)) == bytes);
  for (std::size_t end = 0; end < bytes.size(); ++end)
    CHECK(!decode_retained_event(ByteView{bytes}.first(end), 56).has_value());
  auto extra = bytes;
  extra.push_back(std::byte{0});
  CHECK(!decode_retained_event(extra, 256).has_value());
  auto unknown = bytes;
  unknown[0] = std::byte{3};
  CHECK(decode_retained_event(unknown, 256).error().code == ErrorCode::unsupported);
  const RetainedEvent legacy{{},
                             AttemptAdmissionEvent{id<OperationAttemptId>(8),
                                                   id<InvocationId>(7),
                                                   id<DecisionId>(1), original}};
  CHECK(require(decode_retained_event(require(encode_retained_event(legacy, 256)),
                                      256)) == legacy);
}
void decision_bytes() {
  const RetainedEvent event{{}, decision()};
  const auto bytes = require(encode_retained_event(event, 256));
  CHECK(bytes.size() == 131);
  CHECK(bytes[0] == std::byte{1} && bytes[2] == std::byte{2});
  for (std::size_t field = 0; field < 6; ++field) {
    CHECK(bytes[8 + field * 16 + 15] == static_cast<std::byte>(field + 1));
    for (std::size_t leading = 0; leading < 15; ++leading) {
      CHECK(bytes[8 + field * 16 + leading] == std::byte{0});
    }
  }
  CHECK(bytes[104] == std::byte{1} && bytes[123] == std::byte{7});
  CHECK(bytes[124] == std::byte{3});
  CHECK(bytes[128] == std::byte{0} && bytes[129] == std::byte{255} &&
        bytes[130] == std::byte{10});
  CHECK(require(decode_retained_event(bytes, 256)) == event);
  for (std::size_t end = 0; end < bytes.size(); ++end) {
    CHECK(!decode_retained_event(ByteView{bytes}.first(end), 256).has_value());
  }
  auto bad = bytes;
  bad[104] = std::byte{255}; // Count exceeds remaining bytes before allocation.
  CHECK(!decode_retained_event(bad, 256).has_value());
  bad = bytes;
  bad[124] = std::byte{255};
  CHECK(!decode_retained_event(bad, 256).has_value());
  bad = bytes;
  bad[23] = std::byte{0}; // Zero decision identity.
  CHECK(!decode_retained_event(bad, 256).has_value());
  bad = bytes;
  bad.push_back(std::byte{0});
  CHECK(!decode_retained_event(bad, 256).has_value());
  bad = bytes;
  bad[0] = std::byte{2};
  CHECK(!decode_retained_event(bad, 256).has_value());
  bad = bytes;
  bad[2] = std::byte{255};
  CHECK(!decode_retained_event(bad, 256).has_value());
}
void dependencies_and_other_bodies() {
  const SourceReference reference{id<AuditStreamId>(12), 17};
  const RetainedEvent with_reference{{reference}, IssuerReservationEvent{19}};
  const auto bytes = require(encode_retained_event(with_reference, 256));
  CHECK(bytes.size() == 40 && bytes[4] == std::byte{1});
  CHECK(bytes[23] == std::byte{12} && bytes[24] == std::byte{17});
  CHECK(bytes[32] == std::byte{19});
  CHECK(require(decode_retained_event(bytes, 256)) == with_reference);
  auto zero_sequence = bytes;
  zero_sequence[24] = std::byte{0};
  CHECK(!decode_retained_event(zero_sequence, 256).has_value());
  auto excessive_dependencies = bytes;
  excessive_dependencies[4] = std::byte{255};
  CHECK(!decode_retained_event(excessive_dependencies, 256).has_value());
  const auto invocation = id<InvocationId>(7);
  const auto checkpoint = id<DecisionId>(1);
  const auto attempt = id<OperationAttemptId>(8);
  const std::array<RetainedEvent, 7> events{
      RetainedEvent{{reference},
                    InvocationEvent{invocation, checkpoint,
                                    id<DefinitionGenerationId>(5), original}},
      RetainedEvent{{},
                    AttemptAdmissionEvent{attempt, invocation, checkpoint, original}},
      RetainedEvent{{}, AttemptOpenEvent{attempt}},
      RetainedEvent{{},
                    AttemptObservationEvent{attempt, AttemptPhase::terminal,
                                            AttemptDisposition::unknown, original}},
      RetainedEvent{{}, RetryEvent{checkpoint, invocation, attempt}},
      RetainedEvent{{},
                    IdentityConflictEvent{RetainedKind::invocation, invocation.bytes(),
                                          original}},
      RetainedEvent{
          {}, ComplaintEvent{id<ComplaintId>(9), id<ParticipantId>(2), original}}};
  for (const auto &event : events) {
    const auto encoded = require(encode_retained_event(event, 256));
    CHECK(require(decode_retained_event(encoded, 256)) == event);
    for (std::size_t end = 0; end < encoded.size(); ++end) {
      CHECK(!decode_retained_event(ByteView{encoded}.first(end), 256).has_value());
    }
    auto trailing = encoded;
    trailing.push_back(std::byte{0});
    const auto trailing_result = decode_retained_event(trailing, 256);
    CHECK(!trailing_result.has_value() &&
          trailing_result.error().code == ErrorCode::corrupt);
  }
  const RetainedEvent invalid{{},
                              AttemptObservationEvent{attempt, AttemptPhase::running,
                                                      AttemptDisposition::success,
                                                      original}};
  CHECK(!encode_retained_event(invalid, 256).has_value());
  auto observation = require(encode_retained_event(events[3], 256));
  observation[26] = std::byte{1}; // Observation's reserved byte.
  CHECK(!decode_retained_event(observation, 256).has_value());
  const auto valid_observation = require(encode_retained_event(events[3], 256));
  for (unsigned char phase = 0; phase <= 4; ++phase) {
    for (unsigned char disposition = 0; disposition <= 5; ++disposition) {
      auto candidate = valid_observation;
      candidate[24] = static_cast<std::byte>(phase);
      candidate[25] = static_cast<std::byte>(disposition);
      const bool valid = ((phase == 1 || phase == 2) && disposition == 0) ||
                         (phase == 3 && disposition >= 1 && disposition <= 4);
      CHECK(decode_retained_event(candidate, 256).has_value() == valid);
    }
  }
  CHECK(!encode_retained_event(
             {{},
              AttemptObservationEvent{attempt, AttemptPhase::terminal,
                                      AttemptDisposition::none, original}},
             256)
             .has_value());
  CHECK(!encode_retained_event({{},
                                IdentityConflictEvent{RetainedKind::complaint,
                                                      invocation.bytes(), original}},
                               256)
             .has_value());
  CHECK(!encode_retained_event({{},
                                IdentityConflictEvent{RetainedKind::invocation,
                                                      IdentityBytes{}, original}},
                               256)
             .has_value());
  auto conflict = require(encode_retained_event(events[5], 256));
  std::fill(conflict.begin() + 10, conflict.begin() + 26, std::byte{0});
  CHECK(!decode_retained_event(conflict, 256).has_value());
  fail_allocation.store(0);
  const auto encode_failure = encode_retained_event(with_reference, 256);
  CHECK(!encode_failure.has_value() &&
        encode_failure.error().code == ErrorCode::allocation);
  fail_allocation.store(0);
  const auto decode_failure = decode_retained_event(bytes, 256);
  CHECK(!decode_failure.has_value() &&
        decode_failure.error().code == ErrorCode::allocation);
  const auto allocating =
      require(encode_retained_event({{reference}, decision()}, 256));
  fail_allocation.store(2); // Dependencies and planned IDs succeed; blob fails.
  const auto later_failure = decode_retained_event(allocating, 256);
  fail_allocation.store(-1);
  CHECK(!later_failure.has_value() &&
        later_failure.error().code == ErrorCode::allocation);
}
void provider_error_bytes() {
  for (const auto code : {ErrorCode::provider_auth, ErrorCode::provider_rate_limit}) {
    const auto reason = static_cast<std::byte>(code);
    const std::array rejected{
        std::byte{1}, std::byte{0}, std::byte{10},   std::byte{0}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0},    reason,       std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0xad}, std::byte{1}, std::byte{0},
        std::byte{0}, std::byte{0}, std::byte{0},    std::byte{0}, std::byte{0}};
    const RetainedEvent rejection{{}, RejectedSubmissionEvent{{code, 429}}};
    CHECK(require(decode_retained_event(rejected, 256)) == rejection);
    const auto encoded = require(encode_retained_event(rejection, 256));
    CHECK(std::equal(rejected.begin(), rejected.end(), encoded.begin(), encoded.end()));
    const RetainedEvent receipt{
        {}, AdapterReceiptEvent{id<OperationAttemptId>(8), Error{code, -429}}};
    const auto receipt_bytes = require(encode_retained_event(receipt, 256));
    CHECK(require(decode_retained_event(receipt_bytes, 256)) == receipt);
    CHECK(receipt_bytes[26] == reason);
    auto invalid = rejected;
    invalid[8] = std::byte{255};
    CHECK(!decode_retained_event(invalid, 256).has_value());
  }
}
void rejection_and_receipt_bytes() {
  const RetainedEvent rejected{{}, RejectedSubmissionEvent{{ErrorCode::conflict, -2}}};
  const auto bytes = require(encode_retained_event(rejected, 256));
  CHECK(bytes.size() == 20 && bytes[0] == std::byte{1} && bytes[2] == std::byte{10});
  CHECK(bytes[8] == std::byte{18} && bytes[10] == std::byte{0} &&
        bytes[11] == std::byte{0});
  CHECK(bytes[12] == std::byte{254});
  for (std::size_t i = 13; i < 20; ++i) {
    CHECK(bytes[i] == std::byte{255});
  }
  CHECK(require(decode_retained_event(bytes, 256)) == rejected);
  const RetainedEvent receipt{
      {}, AdapterReceiptEvent{id<OperationAttemptId>(11), std::nullopt}};
  const auto success = require(encode_retained_event(receipt, 256));
  std::vector<std::byte> expected(36, std::byte{0});
  expected[0] = std::byte{1};
  expected[2] = std::byte{11};
  expected[23] = std::byte{11};
  expected[24] = std::byte{1};
  CHECK(success == expected &&
        require(decode_retained_event(expected, 256)) == receipt);
  const RetainedEvent failed{
      {},
      AdapterReceiptEvent{id<OperationAttemptId>(11),
                          Error{ErrorCode::external_unknown, -3}}};
  const auto failure = require(encode_retained_event(failed, 256));
  expected[24] = std::byte{0};
  expected[26] = std::byte{15};
  expected[28] = std::byte{253};
  std::fill(expected.begin() + 29, expected.end(), std::byte{255});
  CHECK(failure == expected && require(decode_retained_event(expected, 256)) == failed);
  for (const auto &encoded : {bytes, success, failure}) {
    for (std::size_t end = 0; end < encoded.size(); ++end) {
      CHECK(!decode_retained_event(ByteView{encoded}.first(end), 256).has_value());
    }
    auto trailing = encoded;
    trailing.push_back(std::byte{0});
    CHECK(!decode_retained_event(trailing, 256).has_value());
  }
  auto invalid = bytes;
  invalid[10] = std::byte{1};
  CHECK(!decode_retained_event(invalid, 256).has_value());
  invalid = bytes;
  invalid[8] = std::byte{255};
  CHECK(!decode_retained_event(invalid, 256).has_value());
  invalid = success;
  invalid[26] = std::byte{1};
  CHECK(!decode_retained_event(invalid, 256).has_value());
  invalid = success;
  invalid[24] = std::byte{2};
  CHECK(!decode_retained_event(invalid, 256).has_value());
  invalid = failure;
  invalid[26] = std::byte{255};
  CHECK(!decode_retained_event(invalid, 256).has_value());
}
void continuation_wire() {
  // Independent empty-choice vector: 8-byte envelope + 108-byte fixed body.
  std::vector<std::byte> choice(116, std::byte{0});
  choice[0] = std::byte{1};
  choice[2] = std::byte{12};
  choice[23] = std::byte{12};  // Immediate predecessor journal.
  choice[32] = std::byte{112}; // Empty validated prefix.
  choice[40] = std::byte{112}; // Diagnostic offset.
  choice[48] = std::byte{112}; // Observed available end.
  choice[57] = std::byte{4};   // Old payload limit 1024.
  choice[61] = std::byte{8};   // Old batch limit 2048.
  choice[66] = std::byte{1};   // Old file capacity 65536.
  choice[72] = std::byte{64};  // Old record capacity.
  choice[82] = std::byte{2};   // New file capacity 131072.
  choice[88] = std::byte{128}; // New record capacity.
  const auto decoded = require(decode_retained_event(choice, 256));
  CHECK(static_cast<unsigned int>(retained_kind(decoded.body)) == 12);
  CHECK(require(encode_retained_event(decoded, 256)) == choice);
  // One chunk dependency + 36-byte capture body. Marker is only a wire record.
  std::vector<std::byte> capture(68, std::byte{0});
  capture[0] = std::byte{1};
  capture[2] = std::byte{13};
  capture[4] = std::byte{1};
  capture[23] = std::byte{13};
  capture[24] = std::byte{3};
  capture[47] = std::byte{12};
  capture[48] = std::byte{1};
  capture[56] = std::byte{48};
  capture[64] = std::byte{1};
  const auto marker = require(decode_retained_event(capture, 256));
  CHECK(static_cast<unsigned int>(retained_kind(marker.body)) == 13);
  CHECK(require(encode_retained_event(marker, 256)) == capture);

  auto full = choice;
  full[96] = std::byte{1}; // Explicit problem, conflict/-2.
  full[98] = std::byte{18};
  full[100] = std::byte{254};
  std::fill(full.begin() + 101, full.begin() + 108, std::byte{255});
  full[108] = std::byte{2};
  full.insert(full.begin() + 112, 40, std::byte{0});
  full[127] = std::byte{7};
  full[128] = std::byte{1};
  full[129] = std::byte{1}; // Running/none/uncertain.
  full[131] = std::byte{3};
  full[147] = std::byte{8};
  full[148] = std::byte{1};
  full[149] = std::byte{3}; // Terminal/unknown/recovered.
  full[150] = std::byte{4};
  full[151] = std::byte{2};
  full[152] = std::byte{2};
  full.resize(228, std::byte{0});
  full[171] = std::byte{12};
  full[172] = std::byte{1};
  full[180] = std::byte{48};
  full[188] = std::byte{1};
  full[207] = std::byte{12};
  full[208] = std::byte{8};
  full[216] = std::byte{255};
  full[217] = std::byte{1}; // 511 bytes.
  full[224] = std::byte{2};
  const auto populated = require(decode_retained_event(full, 256));
  const auto &body = std::get<RecoveryChoiceEvent>(populated.body);
  CHECK(body.predecessor == JournalPredecessor{id<AuditStreamId>(12), 0, 112});
  CHECK(body.old_limits == JournalLimits{1024, 2048});
  CHECK(body.old_capacity == RetainedCapacityDeclaration{65536, 64});
  CHECK(body.new_capacity == RetainedCapacityDeclaration{131072, 128});
  CHECK(body.problem == std::optional<Error>{{ErrorCode::conflict, -2}});
  CHECK(body.checked_attempts ==
        std::vector<CheckedAttempt>{
            {id<OperationAttemptId>(7), true, AttemptPhase::running,
             AttemptDisposition::none, RetainedEvidence::uncertain},
            {id<OperationAttemptId>(8), true, AttemptPhase::terminal,
             AttemptDisposition::unknown, RetainedEvidence::recovered}});
  CHECK(body.required_captures ==
        std::vector<ProvisionalCaptureEvent>{
            {id<AuditStreamId>(12), 1, 48, CapturePurpose::ordinary_submission},
            {id<AuditStreamId>(12), 8, 511, CapturePurpose::rejected_proposal}});
  CHECK(require(encode_retained_event(populated, 228)) == full);
  for (const auto code : {ErrorCode::provider_auth, ErrorCode::provider_rate_limit}) {
    auto provider_problem = full;
    provider_problem[98] = static_cast<std::byte>(code);
    const auto event = require(decode_retained_event(provider_problem, 256));
    CHECK(std::get<RecoveryChoiceEvent>(event.body).problem ==
          std::optional<Error>{{code, -2}});
    CHECK(require(encode_retained_event(event, 256)) == provider_problem);
  }
  CHECK(std::get<ProvisionalCaptureEvent>(marker.body) ==
        ProvisionalCaptureEvent{id<AuditStreamId>(12), 1, 48,
                                CapturePurpose::ordinary_submission});
  for (const auto &encoded : {choice, capture, full}) {
    for (std::size_t end = 0; end < encoded.size(); ++end) {
      const auto cut = decode_retained_event(ByteView{encoded}.first(end), 256);
      CHECK(!cut.has_value() && cut.error().code == ErrorCode::corrupt);
    }
    auto trailing = encoded;
    trailing.push_back(std::byte{0});
    const auto trailing_result = decode_retained_event(trailing, 256);
    CHECK(!trailing_result.has_value() &&
          trailing_result.error().code == ErrorCode::corrupt);
    const auto cap =
        decode_retained_event(encoded, static_cast<std::uint32_t>(encoded.size() - 1));
    CHECK(!cap.has_value() && cap.error().code == ErrorCode::capacity);
    const auto typed = require(decode_retained_event(encoded, 256));
    const auto encode_cap =
        encode_retained_event(typed, static_cast<std::uint32_t>(encoded.size() - 1));
    CHECK(!encode_cap.has_value() && encode_cap.error().code == ErrorCode::capacity);
  }
  const auto bad_choice_byte = [&](std::size_t offset, unsigned char value) {
    auto bad = full;
    bad[offset] = static_cast<std::byte>(value);
    const auto refused = decode_retained_event(bad, 256);
    CHECK(!refused.has_value() && refused.error().code == ErrorCode::corrupt);
  };
  bad_choice_byte(23, 0); // Zero predecessor.
  bad_choice_byte(96, 2);
  bad_choice_byte(98, 255);
  bad_choice_byte(96, 0);    // Absent problem must have zero code/detail.
  bad_choice_byte(108, 255); // Count cannot drive unbounded allocation.
  bad_choice_byte(127, 0);   // Zero attempt.
  bad_choice_byte(128, 2);   // Opened boolean.
  bad_choice_byte(129, 4);   // Phase.
  bad_choice_byte(130, 1);   // Nonterminal cannot have disposition.
  bad_choice_byte(131, 4);   // Evidence enum.
  bad_choice_byte(147, 7);   // Duplicate checked ID.
  bad_choice_byte(147, 6);   // Out of order.
  bad_choice_byte(152, 255); // Capture count bounded by actual remaining bytes.
  bad_choice_byte(171, 0);   // Zero origin.
  bad_choice_byte(172, 0);   // Zero first physical sequence.
  bad_choice_byte(180, 47);  // Below ARPROP01 fixed extent.
  bad_choice_byte(188, 0);
  bad_choice_byte(188, 3);
  bad_choice_byte(189, 1);  // Full u16 purpose validated.
  bad_choice_byte(190, 1);  // Reserved.
  bad_choice_byte(207, 11); // Descriptor ordering by origin bytes.
  auto duplicate_descriptor = full;
  std::copy_n(full.begin() + 156, 36, duplicate_descriptor.begin() + 192);
  CHECK(!decode_retained_event(duplicate_descriptor, 256).has_value());
  auto reversed = populated;
  auto &reversed_body = std::get<RecoveryChoiceEvent>(reversed.body);
  std::swap(reversed_body.checked_attempts[0], reversed_body.checked_attempts[1]);
  CHECK(!encode_retained_event(reversed, 256).has_value());
  reversed = populated;
  std::reverse(std::get<RecoveryChoiceEvent>(reversed.body).required_captures.begin(),
               std::get<RecoveryChoiceEvent>(reversed.body).required_captures.end());
  CHECK(!encode_retained_event(reversed, 256).has_value());
  auto dependency_choice = populated;
  dependency_choice.dependencies.push_back({id<AuditStreamId>(13), 3});
  CHECK(!encode_retained_event(dependency_choice, 256).has_value());
  auto dependency_wire = full;
  dependency_wire[4] = std::byte{1};
  dependency_wire.insert(dependency_wire.begin() + 8, capture.begin() + 8,
                         capture.begin() + 32);
  CHECK(!decode_retained_event(dependency_wire, 256).has_value());
  auto missing_chunk = marker;
  missing_chunk.dependencies.clear();
  CHECK(!encode_retained_event(missing_chunk, 256).has_value());
  auto missing_chunk_wire = capture;
  missing_chunk_wire[4] = std::byte{0};
  missing_chunk_wire.erase(missing_chunk_wire.begin() + 8,
                           missing_chunk_wire.begin() + 32);
  CHECK(!decode_retained_event(missing_chunk_wire, 256).has_value());
  auto rejected_capture = capture;
  rejected_capture[64] = std::byte{2};
  CHECK(require(encode_retained_event(
            require(decode_retained_event(rejected_capture, 256)), 256)) ==
        rejected_capture);
  auto unknown = choice;
  unknown[2] = std::byte{15};
  const auto unknown_kind = decode_retained_event(unknown, 256);
  CHECK(!unknown_kind.has_value() &&
        unknown_kind.error().code == ErrorCode::unsupported);
  auto malformed = capture;
  malformed[64] = std::byte{0};
  const auto malformed_capture = decode_retained_event(malformed, 256);
  CHECK(!malformed_capture.has_value() &&
        malformed_capture.error().code == ErrorCode::corrupt);
  for (unsigned char phase = 0; phase <= 4; ++phase) {
    for (unsigned char disposition = 0; disposition <= 5; ++disposition) {
      auto candidate = full;
      candidate[129] = static_cast<std::byte>(phase);
      candidate[130] = static_cast<std::byte>(disposition);
      const bool valid = (phase <= 2 && disposition == 0) ||
                         (phase == 3 && disposition >= 1 && disposition <= 4);
      CHECK(decode_retained_event(candidate, 256).has_value() == valid);
    }
  }
  // Sweep actual allocator failures; reset before checking results or copying.
  for (const bool encoding : {false, true}) {
    bool success = false;
    for (std::ptrdiff_t cut = 0; cut < 64; ++cut) {
      fail_allocation.store(cut);
      const auto result = encoding ? encode_retained_event(populated, 256)
                                   : [&]() -> Result<std::vector<std::byte>> {
        const auto parsed = decode_retained_event(full, 256);
        if (!parsed.has_value()) {
          return Result<std::vector<std::byte>>::failure(parsed.error());
        }
        fail_allocation.store(-1);
        CHECK(parsed.value() == populated);
        return Result<std::vector<std::byte>>::success({});
      }();
      fail_allocation.store(-1);
      if (result.has_value()) {
        success = true;
        if (encoding) {
          CHECK(result.value() == full);
        }
        break;
      }
      CHECK(result.error().code == ErrorCode::allocation);
    }
    CHECK(success);
  }
}
} // namespace
int main() {
  try {
    reservation_bytes();
    admission_reference_bytes();
    decision_bytes();
    dependencies_and_other_bodies();
    rejection_and_receipt_bytes();
    provider_error_bytes();
    continuation_wire();
    std::puts("PASS specified semantic bytes, identities, bounds and exact bodies");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL retained events: %s\n", error.what());
    return 1;
  }
}
