#include "blackbird/retained_proposal.hpp"
#include "blackbird/retained_state.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

using namespace blackbird;
template <class T> T present(std::optional<T> value) {
  if (!value)
    throw std::runtime_error("expected present optional in fixture");
  return std::move(*value);
}

namespace {
std::atomic<std::ptrdiff_t> allocation_cut{-1};
std::atomic_bool measure_allocation{false};
std::atomic_size_t allocated_bytes{0};
} // namespace
void *operator new(std::size_t size) {
  if (measure_allocation.load())
    allocated_bytes.fetch_add(size);
  if (allocation_cut.load() >= 0 && allocation_cut.fetch_sub(1) == 0) {
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
void require(Result<void> result) { CHECK(result.has_value()); }
const PendingProposal &require_pending(const RetainedState &state) {
  const auto &packet = state.pending_proposal();
  if (!packet) {
    throw std::runtime_error("expected owned pending proposal");
  }
  return *packet;
}
template <typename T> void error_is(const Result<T> &result, ErrorCode code) {
  CHECK(!result.has_value());
  if (result.error().code != code) {
    std::fprintf(stderr, "expected error %u, received %u\n",
                 static_cast<unsigned int>(code),
                 static_cast<unsigned int>(result.error().code));
  }
  CHECK(result.error().code == code);
}
template <typename T> T id(unsigned char value) {
  IdentityBytes bytes{};
  bytes[15] = std::byte{value};
  return require(T::from_bytes(bytes));
}
struct StorageState {
  std::vector<std::byte> bytes;
  std::vector<std::byte> synchronized;
  std::size_t writes = 0;
  bool exists = false;
  bool locked = false;
  bool fail_sync = false;
  bool fail_directory = false;
  std::size_t sync_calls = 0;
  std::function<void()> on_sync;
  std::size_t fail_sync_call = 0;
  std::size_t allocate_after_sync_call = 0;
  std::size_t fail_after = SIZE_MAX;
  bool fail_extent = false;
  std::size_t fail_extent_after_sync_call = 0;
};
class MemoryFile final : public JournalFile {
public:
  explicit MemoryFile(std::shared_ptr<StorageState> state) : state_(std::move(state)) {}
  ~MemoryFile() override {
    if (locked_) {
      state_->locked = false;
    }
  }
  Result<void> lock_writer() override {
    if (state_->locked) {
      return Result<void>::failure({ErrorCode::busy});
    }
    state_->locked = true;
    locked_ = true;
    return Result<void>::success();
  }
  Result<std::uint64_t> extent() override {
    if (state_->fail_extent) {
      return Result<std::uint64_t>::failure({ErrorCode::io, 5});
    }
    return Result<std::uint64_t>::success(state_->bytes.size());
  }
  Result<std::size_t> read_at(std::uint64_t offset, MutableByteView bytes) override {
    if (offset >= state_->bytes.size()) {
      return Result<std::size_t>::success(0);
    }
    const auto count =
        std::min(bytes.size(), state_->bytes.size() - static_cast<std::size_t>(offset));
    std::copy_n(state_->bytes.begin() + static_cast<std::ptrdiff_t>(offset), count,
                bytes.begin());
    return Result<std::size_t>::success(count);
  }
  Result<std::size_t> write_at(std::uint64_t offset, ByteView bytes) override {
    ++state_->writes;
    if (offset >= state_->fail_after) {
      return Result<std::size_t>::failure({ErrorCode::io, 28});
    }
    const auto count =
        std::min({std::size_t{7}, bytes.size(),
                  state_->fail_after - static_cast<std::size_t>(offset)});
    state_->bytes.resize(static_cast<std::size_t>(offset) + count);
    std::copy_n(bytes.begin(), count,
                state_->bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return Result<std::size_t>::success(count);
  }
  Result<void> synchronize(SyncStrength strength) override {
    CHECK(strength == SyncStrength::full);
    ++state_->sync_calls;
    if (state_->on_sync)
      state_->on_sync();
    if (state_->fail_sync || state_->sync_calls == state_->fail_sync_call) {
      return Result<void>::failure({ErrorCode::io, 5});
    }
    auto copy = state_->bytes;
    state_->synchronized.swap(copy);
    if (state_->sync_calls == state_->fail_extent_after_sync_call) {
      state_->fail_extent = true;
    }
    if (state_->sync_calls == state_->allocate_after_sync_call) {
      allocation_cut.store(0);
    }
    return Result<void>::success();
  }

private:
  std::shared_ptr<StorageState> state_;
  bool locked_ = false;
};
class MemoryDirectory final : public JournalDirectory {
public:
  explicit MemoryDirectory(std::shared_ptr<StorageState> state)
      : state_(std::move(state)) {}
  Result<std::unique_ptr<JournalFile>> create_exclusive(std::string_view) override {
    if (state_->exists) {
      return Result<std::unique_ptr<JournalFile>>::failure({ErrorCode::conflict});
    }
    state_->exists = true;
    return make_file();
  }
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view,
                                                     FileAccess) override {
    if (!state_->exists) {
      return Result<std::unique_ptr<JournalFile>>::failure({ErrorCode::io, 2});
    }
    return make_file();
  }
  Result<void> synchronize_directory(SyncStrength) override {
    CHECK(state_->bytes == state_->synchronized);
    return state_->fail_directory ? Result<void>::failure({ErrorCode::io, 5})
                                  : Result<void>::success();
  }

private:
  Result<std::unique_ptr<JournalFile>> make_file() {
    try {
      return Result<std::unique_ptr<JournalFile>>::success(
          std::make_unique<MemoryFile>(state_));
    } catch (const std::bad_alloc &) {
      return Result<std::unique_ptr<JournalFile>>::failure({ErrorCode::allocation});
    }
  }
  std::shared_ptr<StorageState> state_;
};
JournalHeader header() {
  return {id<EnvironmentId>(1),
          id<AuditStreamId>(2),
          0x0102030405060708,
          {1024, 4096},
          std::nullopt};
}
constexpr JournalCapacity capacity{1 << 20, 1024};
const std::vector<std::byte> original{std::byte{0}, std::byte{255}, std::byte{10}};
DecisionEvent decision(unsigned char value = 3) {
  return {id<DecisionId>(value),         id<ParticipantId>(4),
          id<ConversationId>(5),         id<WorkflowId>(6),
          id<DefinitionGenerationId>(7), id<ContextRevisionId>(8),
          {id<InvocationId>(9)},         original};
}
InvocationEvent invocation() {
  return {id<InvocationId>(9), id<DecisionId>(3), id<DefinitionGenerationId>(10),
          original};
}
AttemptAdmissionEvent admission(unsigned char attempt = 11,
                                unsigned char checkpoint = 3) {
  return {id<OperationAttemptId>(attempt), id<InvocationId>(9),
          id<DecisionId>(checkpoint), original};
}
void indexed_errors_close_queries() {
  auto storage = std::make_shared<StorageState>();
  auto state = require(RetainedState::create(std::make_unique<MemoryDirectory>(storage),
                                             "journal", header(), capacity));
  auto index_storage = std::make_shared<StorageState>();
  require(state->enable_indexed_queries(std::make_unique<MemoryFile>(index_storage),
                                        4 * 1024 * 1024));
  const RetainedEvent event{{}, decision()};
  require(state->submit(event));
  const auto before = state->cursor();
  const auto bytes = storage->bytes;
  // Selected-page corruption is a query error, never absence or a new submission.
  index_storage->bytes.back() ^= std::byte{1};
  error_is(state->submit(event), ErrorCode::corrupt);
  error_is(state->fact(0), ErrorCode::corrupt);
  CHECK(state->cursor().sequence == before.sequence && storage->bytes == bytes);
  index_storage->bytes.back() ^= std::byte{1};
  CHECK(require(state->submit(event)).existing);
  index_storage->fail_after = index_storage->writes;
  error_is(state->submit({{}, invocation()}), ErrorCode::io);
  CHECK(state->cursor().sequence == before.sequence && storage->bytes == bytes);
  CHECK(require(state->submit(event)).existing); // old root survives failed derivation
}
bool indexed_mode = false;
std::unique_ptr<RetainedState> attach(std::unique_ptr<RetainedState> state) {
  if (indexed_mode)
    require(state->enable_indexed_queries(
        std::make_unique<MemoryFile>(std::make_shared<StorageState>()),
        256 * 1024 * 1024));
  return state;
}
std::unique_ptr<RetainedState> create(const std::shared_ptr<StorageState> &storage) {
  return attach(require(RetainedState::create(
      std::make_unique<MemoryDirectory>(storage), "journal", header(), capacity)));
}
std::unique_ptr<RetainedState> open(const std::shared_ptr<StorageState> &storage) {
  return attach(require(RetainedState::open(std::make_unique<MemoryDirectory>(storage),
                                            "journal", header(), capacity)));
}
void prepare(RetainedState &state) {
  require(state.submit({{}, decision()}));
  require(state.submit({{}, invocation()}));
  require(state.submit({{}, admission()}));
}
class Counter final : public EffectBoundary {
public:
  std::size_t calls = 0;
  bool fail = false;
  bool settle = false;
  bool fail_recording_allocation = false;
  StorageState *fail_recording_sync = nullptr;
  StorageState *fail_recording_write = nullptr;
  RetainedState *reenter = nullptr;
  Result<void> dispatch(const EffectIntent &intent) override {
    ++calls;
    CHECK(intent.environment == header().environment &&
          intent.actor == id<ParticipantId>(4));
    CHECK(intent.invocation == id<InvocationId>(9));
    CHECK(std::equal(original.begin(), original.end(), intent.input.begin(),
                     intent.input.end()));
    if (reenter) {
      const auto duplicate = require(reenter->dispatch(intent.attempt, *this));
      CHECK(!duplicate.dispatched);
      require(reenter->submit(
          {{}, ComplaintEvent{id<ComplaintId>(22), id<ParticipantId>(4), original}}));
      CHECK(std::equal(original.begin(), original.end(), intent.input.begin(),
                       intent.input.end()));
      if (settle) {
        require(reenter->submit(
            {{},
             AttemptObservationEvent{intent.attempt, AttemptPhase::terminal,
                                     AttemptDisposition::success, original}}));
      }
    }
    if (fail_recording_write)
      fail_recording_write->fail_after = fail_recording_write->bytes.size() + 7;
    if (fail_recording_sync) {
      fail_recording_sync->fail_sync = true;
    }
    if (fail_recording_allocation) {
      allocation_cut.store(0);
    }
    return fail ? Result<void>::failure({ErrorCode::external_unknown, 17})
                : Result<void>::success();
  }
};
class Custody final : public CustodyVerifier {
public:
  bool fail = false;
  std::size_t seen = 0;
  Result<void> verify(std::span<const AttemptState> attempts) override {
    seen = attempts.size();
    for (const auto &attempt : attempts) {
      CHECK(attempt.reconciliation_required);
    }
    return fail ? Result<void>::failure({ErrorCode::busy}) : Result<void>::success();
  }
};
void admission_references() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  require(state->submit({{}, decision()}));
  require(state->submit({{}, invocation()}));
  auto referenced = admission();
  referenced.input_from_invocation = true;
  require(state->submit({{}, referenced}));
  auto only_reference = referenced;
  only_reference.input = {};
  const auto before_duplicate = storage->writes;
  CHECK(require(state->submit({{}, only_reference})).existing);
  CHECK(storage->writes == before_duplicate);
  const auto stored = require(state->attempt(referenced.attempt)).admission;
  CHECK(stored.input == original);
  CHECK(stored.input.data() ==
        require(state->invocation(referenced.invocation)).input.data());
  auto mismatch = admission(12);
  mismatch.input_from_invocation = true;
  mismatch.input = {std::byte{77}};
  error_is(state->submit({{}, mismatch}), ErrorCode::conflict);
  Counter effect;
  CHECK(require(state->dispatch(referenced.attempt, effect)).dispatched);
  CHECK(effect.calls == 1);
  state.reset();
  state = open(storage);
  require(state->confirm_recovery());
  const auto recovered = require(state->attempt(referenced.attempt));
  CHECK(recovered.admission.input_from_invocation &&
        recovered.admission.input == original);
  CHECK(recovered.reconciliation_required);
  Custody custody;
  require(state->reconcile(custody));
  CHECK(!require(state->dispatch(referenced.attempt, effect)).dispatched &&
        effect.calls == 1);
}
void duplicates_and_retries() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  prepare(*state);
  const auto writes = storage->writes;
  const auto existing = require(state->submit({{}, admission()}));
  CHECK(existing.existing && existing.evidence == RetainedEvidence::live);
  CHECK(storage->writes == writes);
  auto changed = admission();
  changed.input = {std::byte{77}};
  error_is(state->submit({{}, changed}), ErrorCode::conflict);
  CHECK(state->committed_facts().size() == 4);
  CHECK(std::holds_alternative<IdentityConflictEvent>(
      state->committed_facts().back().event.body));
  const auto &rejected = state->committed_facts().back().event;
  CHECK(rejected.dependencies.size() == 1);
  const auto proposal = require(state->source(rejected.dependencies[0]));
  CHECK(proposal.size() == 113);
  CHECK(std::equal(proposal.begin(), proposal.begin() + 8,
                   std::as_bytes(std::span{"ARPROP01", 8}).begin()));
  CHECK(proposal[24] == std::byte{6});
  CHECK(proposal[32] == std::byte{121} && proposal[33] == std::byte{2});
  CHECK(proposal[40] == std::byte{0} && proposal[44] == std::byte{1});
  CHECK(proposal[48] == std::byte{61} && proposal.back() == std::byte{77});
  CHECK(require(state->attempt(id<OperationAttemptId>(11))).admission == admission());
  Counter effect;
  effect.reenter = state.get();
  const auto dispatched = require(state->dispatch(id<OperationAttemptId>(11), effect));
  CHECK(dispatched.dispatched && dispatched.effect.has_value() &&
        dispatched.recording.has_value());
  CHECK(effect.calls == 1);
  CHECK(!require(state->dispatch(id<OperationAttemptId>(11), effect)).dispatched);
  CHECK(effect.calls == 1);
  error_is(state->submit({{}, admission(12)}), ErrorCode::conflict);
  require(state->submit({{}, decision(13)}));
  require(state->submit({{},
                         RetryEvent{id<DecisionId>(13), id<InvocationId>(9),
                                    id<OperationAttemptId>(12)}}));
  require(state->submit({{}, admission(12, 13)}));
  effect.reenter = nullptr;
  effect.fail = true;
  const auto retried = require(state->dispatch(id<OperationAttemptId>(12), effect));
  CHECK(retried.dispatched && !retried.effect.has_value() &&
        retried.recording.has_value());
  CHECK(effect.calls == 2);
  const auto observed = require(state->attempt(id<OperationAttemptId>(12)));
  CHECK(observed.opened && !observed.observation);
  CHECK(observed.receipt ==
        std::optional<AdapterReceiptEvent>{
            {id<OperationAttemptId>(12), Error{ErrorCode::external_unknown, 17}}});
}
void recovery_and_failed_admission() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  require(state->submit({{}, decision()}));
  require(state->submit({{}, invocation()}));
  storage->fail_sync = true;
  error_is(state->submit({{}, admission()}), ErrorCode::io);
  CHECK(state->committed_facts().size() == 2);
  CHECK(require(state->submit({{}, admission()})).evidence ==
        RetainedEvidence::uncertain);
  Counter effect;
  error_is(state->dispatch(id<OperationAttemptId>(11), effect),
           ErrorCode::audit_unavailable);
  CHECK(effect.calls == 0);
  state.reset();
  storage->fail_sync = false;
  state = open(storage);
  CHECK(state->committed_facts().empty());
  CHECK(require(state->submit({{}, admission()})).evidence ==
        RetainedEvidence::recovered_pending);
  error_is(state->dispatch(id<OperationAttemptId>(11), effect),
           ErrorCode::audit_unavailable);
  require(state->confirm_recovery());
  CHECK(state->committed_facts().size() == 3);
  CHECK(require(state->submit({{}, admission()})).evidence ==
        RetainedEvidence::recovered);
  Custody custody;
  custody.fail = true;
  error_is(state->reconcile(custody), ErrorCode::busy);
  CHECK(state->state() == JournalWriterState::recovered);
  custody.fail = false;
  require(state->reconcile(custody));
  CHECK(custody.seen == 1 && state->state() == JournalWriterState::live);
  CHECK(!require(state->dispatch(id<OperationAttemptId>(11), effect)).dispatched);
  CHECK(effect.calls == 0);
}
void uncertain_open() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  prepare(*state);
  storage->fail_sync = true;
  Counter effect;
  error_is(state->dispatch(id<OperationAttemptId>(11), effect), ErrorCode::io);
  CHECK(effect.calls == 0 && state->committed_facts().size() == 3);
  const auto inspected = require(state->attempt(id<OperationAttemptId>(11)));
  CHECK(inspected.opened && !inspected.receipt);
  CHECK(inspected.evidence == RetainedEvidence::uncertain);
  CHECK(inspected.reconciliation_required);
}
void replay_avoids_prefix_payload_copies() {
  auto storage = std::make_shared<StorageState>();
  {
    MemoryDirectory directory{storage};
    auto journal =
        require(FramedJournal::create(directory, "journal", header(), capacity));
    for (unsigned char i = 1; i <= 64; ++i) {
      const auto payload = require(encode_retained_event(
          {{},
           ApplicationRecordEvent{id<ApplicationRecordId>(i), ApplicationChannel::log,
                                  std::vector<std::byte>(768, std::byte{i})}},
          1024));
      const std::array batch{JournalDraft{FrameKind::semantic, payload}};
      require(journal->append(batch));
    }
  }
  allocated_bytes.store(0);
  measure_allocation.store(true);
  auto reopened = open(storage);
  measure_allocation.store(false);
  const auto bytes = allocated_bytes.load();
  std::fprintf(stderr, "replay allocations %zu for %zu journal bytes\n", bytes,
               storage->bytes.size());
  CHECK(bytes < storage->bytes.size() * 24 + 262144);
  require(reopened->confirm_recovery());
  CHECK(reopened->committed_facts().size() == 64);
  Custody custody;
  require(reopened->reconcile(custody));
  const auto prefix =
      std::get<ApplicationRecordEvent>(reopened->committed_facts()[0].event.body)
          .payload;
  CHECK(prefix.is_cold() && prefix.resident_bytes() == 0);
  allocated_bytes.store(0);
  measure_allocation.store(true);
  for (int i = 0; i < 32; ++i)
    require(reopened->issue<ParticipantId>());
  measure_allocation.store(false);
  const auto copy =
      std::get<ApplicationRecordEvent>(reopened->committed_facts()[0].event.body);
  CHECK(copy.payload.is_cold() && copy.payload.resident_bytes() == 0);
  CHECK(copy.payload == prefix);
  for (std::size_t i = 0; i < 64; ++i) {
    const auto &fact =
        std::get<ApplicationRecordEvent>(reopened->committed_facts()[i].event.body);
    CHECK(fact.identity == id<ApplicationRecordId>(static_cast<unsigned char>(i + 1)));
    CHECK(fact.payload ==
          std::vector<std::byte>(768, std::byte{static_cast<unsigned char>(i + 1)}));
  }
}
void sources_and_invalid_replay() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  const auto expected = state->cursor();
  const SourceReference source{header().journal, 1};
  const std::array<ByteView, 1> sources{original};
  const std::array events{RetainedEvent{{source}, decision()}};
  require(state->append(expected, sources, events));
  CHECK(require(state->source(source)) == original);
  const auto committed = storage->bytes;
  error_is(state->append(expected, sources, events), ErrorCode::conflict);
  CHECK(require(state->source(source)) == original);
  auto wrong = invocation();
  wrong.decision = id<DecisionId>(77);
  error_is(state->submit({{}, wrong}), ErrorCode::conflict);
  CHECK(std::holds_alternative<RejectedSubmissionEvent>(
      state->committed_facts().back().event.body));
  state.reset();
  // Construct a structurally committed but semantically invalid second batch.
  storage->bytes = committed;
  storage->synchronized = committed;
  {
    MemoryDirectory directory{storage};
    auto raw = require(FramedJournal::open(directory, "journal", header(), capacity));
    require(raw->confirm_recovery());
    require(raw->resume_after_reconciliation());
    const auto payload = require(encode_retained_event({{}, admission()}, 1024));
    const std::array draft{JournalDraft{FrameKind::semantic, payload}};
    require(raw->append(draft));
  }
  const auto invalid = storage->bytes;
  state = open(storage);
  CHECK(state->recovery_report().problem ==
        std::optional<Error>{{ErrorCode::conflict}});
  require(state->confirm_recovery());
  CHECK(state->committed_facts().size() == 1 &&
        state->state() == JournalWriterState::blocked);
  CHECK(require(state->source(source)) == original && storage->bytes == invalid);
  Counter effect;
  error_is(state->dispatch(id<OperationAttemptId>(11), effect),
           ErrorCode::audit_unavailable);
  CHECK(effect.calls == 0);
}
void pending_source_inspection() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  storage->fail_sync = true;
  const std::array<ByteView, 1> sources{original};
  const SourceReference source{header().journal, 1};
  const std::array events{RetainedEvent{{source}, decision()}};
  error_is(state->append(state->cursor(), sources, events), ErrorCode::io);
  error_is(state->source(source), ErrorCode::audit_unavailable);
  const auto observed = require(state->read_original_range(144, original.size()));
  CHECK(observed.bytes == original && state->state() == JournalWriterState::poisoned);
  CHECK(state->committed_facts().empty());
  error_is(state->source(source), ErrorCode::audit_unavailable);
  state.reset();
  storage->fail_sync = false;
  state = open(storage);
  CHECK(require(state->read_original_range(144, original.size())).bytes == original);
  CHECK(state->committed_facts().empty());
  error_is(state->source(source), ErrorCode::audit_unavailable);
}
void pending_proposal_owns_originals() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  CHECK(!state->pending_proposal());
  const auto expected_cursor = state->cursor();
  const auto supplied_event = RetainedEvent{{{header().journal, 1}}, decision()};
  const auto encoded_event = require(encode_retained_event(supplied_event, 1024));
  {
    auto caller_bytes = original;
    const std::array<ByteView, 1> sources{caller_bytes};
    const std::array events{supplied_event};
    storage->fail_sync = true;
    error_is(state->append(expected_cursor, sources, events), ErrorCode::io);
    std::fill(caller_bytes.begin(), caller_bytes.end(), std::byte{44});
  }
  CHECK(state->pending_proposal().has_value());
  const auto &pending = require_pending(*state);
  CHECK(pending.capture ==
        ProvisionalCaptureEvent{header().journal, 1,
                                48 + 4 + original.size() + 4 + encoded_event.size(),
                                CapturePurpose::ordinary_submission});
  const auto proposal = require(decode_retained_proposal(pending.bytes, 1 << 20));
  CHECK(proposal.expected.journal == expected_cursor.journal &&
        proposal.expected.sequence == 0 && proposal.expected.end_offset == 112);
  CHECK(proposal.sources == std::vector<std::vector<std::byte>>{original});
  CHECK(proposal.events == std::vector<std::vector<std::byte>>{encoded_event});
  CHECK(state->cursor().sequence == 0 && state->committed_facts().empty());
  error_is(state->source({header().journal, 1}), ErrorCode::audit_unavailable);
  CHECK(require(state->submit(supplied_event)).evidence == RetainedEvidence::uncertain);
  Counter effect;
  error_is(state->dispatch(id<OperationAttemptId>(11), effect),
           ErrorCode::audit_unavailable);
  CHECK(effect.calls == 0);
  state.reset();
  storage->fail_sync = false;
  state = open(storage);
  CHECK(!state->pending_proposal()); // A dead process's RAM is not reconstructed.
}
void first_submission_every_write_cut() {
  const RetainedEvent event{{{header().journal, 1}}, decision()};
  const auto encoded = require(encode_retained_event(event, 1024));
  const std::vector<std::vector<std::byte>> events{encoded};
  const auto end = 112 + 32 + original.size() + 32 + encoded.size() + 56;
  for (std::size_t cut = 112; cut < end; ++cut) {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    const auto cursor = state->cursor();
    auto caller_bytes = original;
    const std::array<ByteView, 1> sources{caller_bytes};
    const auto expected_packet =
        require(encode_retained_proposal(cursor, sources, events, 1 << 20));
    storage->fail_after = cut;
    const std::array submitted{event};
    error_is(state->append(cursor, sources, submitted), ErrorCode::io);
    std::fill(caller_bytes.begin(), caller_bytes.end(), std::byte{33});
    CHECK(storage->bytes.size() == cut && storage->synchronized.size() == 112);
    CHECK(state->state() == JournalWriterState::poisoned);
    CHECK(state->committed_facts().empty());
    CHECK(state->cursor().sequence == 0 && state->cursor().end_offset == 112);
    CHECK(state->pending_proposal().has_value());
    const auto &pending = require_pending(*state);
    CHECK(pending.bytes == expected_packet);
    CHECK(pending.capture.first_sequence == 1 &&
          pending.capture.purpose == CapturePurpose::ordinary_submission);
    error_is(state->source({header().journal, 1}), ErrorCode::audit_unavailable);
    Counter effect;
    error_is(state->dispatch(id<OperationAttemptId>(11), effect),
             ErrorCode::audit_unavailable);
    CHECK(effect.calls == 0);
  }
}
void pending_proposal_clean_outcomes() {
  const std::array<ByteView, 1> sources{original};
  const std::array events{RetainedEvent{{{header().journal, 1}}, decision()}};
  {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    require(state->append(state->cursor(), sources, events));
    CHECK(!state->pending_proposal());
    CHECK(state->committed_facts().size() == 1);
    CHECK(require(state->source({header().journal, 1})) == original);
  }
  {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    const auto writes = storage->writes;
    storage->fail_extent = true;
    error_is(state->append(state->cursor(), sources, events), ErrorCode::io);
    CHECK(storage->writes == writes && storage->bytes.size() == 112);
    CHECK(state->state() == JournalWriterState::blocked);
    CHECK(!state->pending_proposal());
  }
  {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    const auto writes = storage->writes;
    auto stale = state->cursor();
    stale.sequence = 77;
    storage->fail_extent = true;
    error_is(state->append(stale, sources, events), ErrorCode::io);
    CHECK(storage->writes == writes && storage->bytes.size() == 112);
    CHECK(state->state() == JournalWriterState::blocked);
    CHECK(!state->pending_proposal()); // No diagnostic byte was attempted.
  }
  {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    require(state->submit({{}, decision()}));
    auto changed = decision();
    auto continuation = std::vector<std::byte>(changed.continuation.begin(),
                                               changed.continuation.end());
    continuation.push_back(std::byte{33});
    changed.continuation = std::move(continuation);
    error_is(state->submit({{}, changed}), ErrorCode::conflict);
    CHECK(!state->pending_proposal());
    CHECK(state->state() == JournalWriterState::live);
    CHECK(std::holds_alternative<IdentityConflictEvent>(
        state->committed_facts().back().event.body));
  }
}
void rejected_wrong_cursor_owns_caller_packet() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  const JournalCursor wrong_cursor{id<AuditStreamId>(77), UINT64_MAX, UINT64_MAX};
  const auto request = RetainedEvent{{}, decision()};
  const std::array submitted{request};
  const std::vector<std::vector<std::byte>> encoded{
      require(encode_retained_event(request, 1024))};
  const auto expected =
      require(encode_retained_proposal(wrong_cursor, {}, encoded, 1024));
  storage->fail_sync = true;
  error_is(state->append(wrong_cursor, {}, submitted), ErrorCode::io);
  CHECK(state->pending_proposal().has_value());
  const auto &pending = require_pending(*state);
  CHECK(pending.capture == ProvisionalCaptureEvent{header().journal, 1, expected.size(),
                                                   CapturePurpose::rejected_proposal});
  CHECK(pending.bytes == expected);
  CHECK(state->committed_facts().empty());
  const auto caller_packet = require(decode_retained_proposal(expected, 1024));
  CHECK(caller_packet.expected.journal == id<AuditStreamId>(77) &&
        caller_packet.expected.sequence == UINT64_MAX &&
        caller_packet.expected.end_offset == UINT64_MAX &&
        caller_packet.events == encoded);
  // Refused caller input did not enter the ordinary prepared semantic snapshot.
  error_is(state->submit(request), ErrorCode::audit_unavailable);
  Counter effect;
  error_is(state->dispatch(id<OperationAttemptId>(11), effect),
           ErrorCode::audit_unavailable);
  CHECK(effect.calls == 0);
}
void root_refuses_continuation_records() {
  const std::array events{
      RetainedEvent{{},
                    RecoveryChoiceEvent{{header().journal, 0, 112},
                                        112,
                                        112,
                                        header().limits,
                                        {capacity.max_file_bytes, capacity.max_records},
                                        {capacity.max_file_bytes, capacity.max_records},
                                        std::nullopt,
                                        {},
                                        {}}},
      RetainedEvent{{{header().journal, 1}},
                    ProvisionalCaptureEvent{header().journal, 1, 48,
                                            CapturePurpose::ordinary_submission}}};
  for (const auto &event : events) {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    const std::array<ByteView, 1> sources{original};
    const std::array submitted{event};
    error_is(state->append(state->cursor(), sources, submitted),
             ErrorCode::unsupported);
    CHECK(state->committed_facts().size() == 1);
    CHECK(std::holds_alternative<RejectedSubmissionEvent>(
        state->committed_facts()[0].event.body));
    CHECK(state->state() == JournalWriterState::live);
    state.reset();
    // Complete physical commit cannot legalize maintenance in a root segment.
    storage = std::make_shared<StorageState>();
    {
      MemoryDirectory directory{storage};
      auto raw =
          require(FramedJournal::create(directory, "journal", header(), capacity));
      const auto encoded = require(encode_retained_event(event, 1024));
      const std::array drafts{JournalDraft{FrameKind::source, original},
                              JournalDraft{FrameKind::semantic, encoded}};
      require(raw->append(drafts));
    }
    const auto unchanged = storage->bytes;
    state = open(storage);
    CHECK(state->recovery_report().problem ==
          std::optional<Error>{{ErrorCode::unsupported}});
    require(state->confirm_recovery());
    CHECK(state->committed_facts().empty());
    CHECK(state->cursor().sequence == 0 && state->cursor().end_offset == 112);
    CHECK(state->state() == JournalWriterState::blocked);
    CHECK(require(state->read_original_range(0, unchanged.size())).bytes == unchanged);
    CHECK(storage->bytes == unchanged);
    Counter effect;
    error_is(state->dispatch(id<OperationAttemptId>(11), effect),
             ErrorCode::audit_unavailable);
    CHECK(effect.calls == 0);
  }
}
std::uint64_t identity_counter(const IdentityBytes &bytes) {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value |= std::uint64_t(std::to_integer<unsigned char>(bytes[i + 8])) << (8 * i);
  return value;
}
void issuer_count_and_credit() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  const auto syncs = storage->sync_calls;
  for (std::uint64_t i = 1; i <= 14610; ++i)
    CHECK(identity_counter(require(state->issue<ParticipantId>()).bytes()) == i);
  CHECK(state->issuer_counter() == 15360);
  CHECK(state->committed_facts().size() == 15);
  CHECK(storage->sync_calls == syncs + 15);
  for (std::size_t i = 0; i < 15; ++i)
    CHECK(std::get<IssuerReservationEvent>(state->committed_facts()[i].event.body)
              .counter == (i + 1) * 1024);
  const auto usage = state->journal_usage();
  auto scope = require(state->protect_settlement(
      {present(usage.remaining_bytes()), usage.remaining_records()}));
  const auto before = storage->bytes;
  const auto credit = present(state->protected_settlement());
  // Already durable IDs consume no audit capacity or protected credit.
  for (std::uint64_t i = 14611; i <= 15360; ++i)
    CHECK(identity_counter(require(state->issue<DecisionId>()).bytes()) == i);
  CHECK(storage->bytes == before && storage->sync_calls == syncs + 15);
  CHECK(present(state->protected_settlement()).max_file_bytes == credit.max_file_bytes);
  CHECK(present(state->protected_settlement()).max_records == credit.max_records);
  error_is(state->issue<DecisionId>(), ErrorCode::capacity);
  CHECK(storage->bytes == before);
  scope.reset();
  CHECK(identity_counter(require(state->issue<DecisionId>()).bytes()) == 15361);
  std::puts("issuer fake: issued=14610 reservations=15 sync_delta=15 highwater=15360");
}
void issuer() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  const auto syncs = storage->sync_calls;
  for (std::uint64_t i = 1; i <= 1024; ++i) {
    const auto identity = require(state->issue<ParticipantId>());
    CHECK(identity_counter(identity.bytes()) == i);
    CHECK(identity.bytes()[0] == std::byte{8} && identity.bytes()[7] == std::byte{1});
  }
  CHECK(storage->sync_calls == syncs + 1);
  CHECK(state->issuer_counter() == 1024 && state->committed_facts().size() == 1);
  storage->fail_sync = true;
  error_is(state->issue<InvocationId>(), ErrorCode::io);
  error_is(state->issue<InvocationId>(), ErrorCode::audit_unavailable);
  state.reset();
  storage->fail_sync = false;
  state = open(storage);
  CHECK(state->issuer_counter() == 2048);
  error_is(state->issue<InvocationId>(), ErrorCode::audit_unavailable);
  require(state->confirm_recovery());
  Custody custody;
  require(state->reconcile(custody));
  CHECK(identity_counter(require(state->issue<InvocationId>()).bytes()) == 2049);
  state.reset();
  state = open(storage);
  require(state->confirm_recovery());
  require(state->reconcile(custody));
  CHECK(identity_counter(require(state->issue<WorkflowId>()).bytes()) == 3073);
  // External highwater burns the cached remainder; final range is shortened.
  require(state->submit({{}, IssuerReservationEvent{UINT64_MAX - 2}}));
  CHECK(identity_counter(require(state->issue<ParticipantId>()).bytes()) ==
        UINT64_MAX - 1);
  CHECK(identity_counter(require(state->issue<ParticipantId>()).bytes()) == UINT64_MAX);
  error_is(state->issue<ParticipantId>(), ErrorCode::overflow);
  // Reentrant issue during submission cannot dispense cached durable space.
  storage = std::make_shared<StorageState>();
  state = create(storage);
  require(state->issue<ParticipantId>());
  bool checked = false;
  storage->on_sync = [&] {
    error_is(state->issue<ParticipantId>(), ErrorCode::busy);
    checked = true;
  };
  require(state->submit({{}, IssuerReservationEvent{2048}}));
  CHECK(checked);
  storage->on_sync = {};
  CHECK(identity_counter(require(state->issue<ParticipantId>()).bytes()) == 2049);
  // Successful refill synchronization can itself close admission: no ID escapes.
  storage = std::make_shared<StorageState>();
  state = create(storage);
  storage->on_sync = [&] { state->block_admission(); };
  error_is(state->issue<ParticipantId>(), ErrorCode::audit_unavailable);
  CHECK(state->issuer_counter() == 1024 && state->committed_facts().size() == 1);
  storage->on_sync = {};
  error_is(state->issue<ParticipantId>(), ErrorCode::audit_unavailable);
  state.reset();
  state = open(storage);
  require(state->confirm_recovery());
  require(state->reconcile(custody));
  CHECK(identity_counter(require(state->issue<ParticipantId>()).bytes()) == 1025);
  // A cached range cannot bypass a host admission block.
  storage = std::make_shared<StorageState>();
  state = create(storage);
  require(state->issue<ParticipantId>());
  state->block_admission();
  error_is(state->issue<ParticipantId>(), ErrorCode::audit_unavailable);
}
void rejected_batch_and_suffix() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  const SourceReference first_source{header().journal, 1};
  const std::array<ByteView, 1> source{original};
  const std::array events{RetainedEvent{{first_source}, decision()}};
  require(state->append(state->cursor(), source, events));
  CHECK(state->cursor().sequence == 3 && state->cursor().end_offset == 390);
  state.reset();
  {
    MemoryDirectory directory{storage};
    auto raw = require(FramedJournal::open(directory, "journal", header(), capacity));
    require(raw->confirm_recovery());
    require(raw->resume_after_reconciliation());
    // A valid invocation precedes an invalid admission in the same physical
    // batch. Sequence 5 names the invocation, not a source dependency.
    const auto valid =
        require(encode_retained_event({{first_source}, invocation()}, 1024));
    const auto invalid =
        require(encode_retained_event({{{header().journal, 5}}, admission()}, 1024));
    const auto reservation =
        require(encode_retained_event({{}, IssuerReservationEvent{100}}, 1024));
    const std::array bad{JournalDraft{FrameKind::source, original},
                         JournalDraft{FrameKind::semantic, valid},
                         JournalDraft{FrameKind::semantic, invalid},
                         JournalDraft{FrameKind::semantic, reservation}};
    require(raw->append(bad));
    const auto complaint = require(encode_retained_event(
        {{}, ComplaintEvent{id<ComplaintId>(22), id<ParticipantId>(4), original}},
        1024));
    const auto later_reservation =
        require(encode_retained_event({{}, IssuerReservationEvent{200}}, 1024));
    const std::array later{JournalDraft{FrameKind::semantic, complaint},
                           JournalDraft{FrameKind::semantic, later_reservation}};
    require(raw->append(later));
    CHECK(raw->cursor().sequence == 11);
  }
  const auto originals = storage->bytes;
  state = open(storage);
  CHECK(state->cursor().sequence == 3 && state->cursor().end_offset == 390);
  CHECK(state->issuer_counter() == 200);
  CHECK(state->committed_facts().empty());
  require(state->confirm_recovery());
  CHECK(state->state() == JournalWriterState::blocked);
  CHECK(state->committed_facts().size() == 1);
  CHECK(std::holds_alternative<DecisionEvent>(state->committed_facts()[0].event.body));
  CHECK(storage->bytes == originals &&
        require(state->source(first_source)) == original);
  error_is(state->attempt(id<OperationAttemptId>(11)), ErrorCode::stale_handle);
  error_is(state->submit({{}, invocation()}), ErrorCode::audit_unavailable);
}
void receipt_settlement_and_failures() {
  {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    prepare(*state);
    Counter effect;
    effect.reenter = state.get();
    effect.settle = true;
    const auto reported = require(state->dispatch(id<OperationAttemptId>(11), effect));
    CHECK(reported.dispatched && reported.recording.has_value());
    const auto settled = require(state->attempt(id<OperationAttemptId>(11)));
    CHECK(settled.observation && settled.observation->phase == AttemptPhase::terminal);
    CHECK(settled.receipt && !settled.receipt->failure && effect.calls == 1);
    state.reset();
    state = open(storage);
    require(state->confirm_recovery());
    Custody custody;
    require(state->reconcile(custody));
    CHECK(custody.seen == 0);
    effect.reenter = state.get();
    CHECK(!require(state->dispatch(id<OperationAttemptId>(11), effect)).dispatched);
    CHECK(effect.calls == 1);
  }
  for (const bool allocate : {false, true}) {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    prepare(*state);
    Counter effect;
    effect.fail_recording_allocation = allocate;
    effect.fail_recording_sync = allocate ? nullptr : storage.get();
    const auto reported = require(state->dispatch(id<OperationAttemptId>(11), effect));
    allocation_cut.store(-1);
    CHECK(reported.dispatched && reported.effect.has_value() &&
          !reported.recording.has_value());
    CHECK(reported.recording.error().code ==
          (allocate ? ErrorCode::allocation : ErrorCode::io));
    CHECK(effect.calls == 1 && state->state() != JournalWriterState::live);
    const auto writer_state = state->state();
    const auto extent = storage->bytes.size();
    const auto observed = require(state->read_original_range(0, extent));
    CHECK(observed.bytes == storage->bytes && observed.observed_extent == extent &&
          observed.offset == 0 && observed.journal == header().journal);
    CHECK(state->state() == writer_state &&
          state->state() ==
              (allocate ? JournalWriterState::blocked : JournalWriterState::poisoned));
    CHECK(require(state->attempt(id<OperationAttemptId>(11))).reconciliation_required);
    error_is(state->dispatch(id<OperationAttemptId>(11), effect),
             ErrorCode::audit_unavailable);
    CHECK(effect.calls == 1);
  }
}
void allocation_before_publication() {
  bool before_io = false;
  bool during_io = false;
  bool completed = false;
  for (std::ptrdiff_t cut = 0; cut != 128; ++cut) {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    require(state->submit({{}, decision()}));
    require(state->submit({{}, invocation()}));
    const RetainedEvent request{{}, admission()};
    const auto acknowledged = storage->synchronized;
    const auto writes = storage->writes;
    allocation_cut.store(cut);
    const auto submitted = state->submit(request);
    allocation_cut.store(-1);
    if (submitted.has_value()) {
      CHECK(state->committed_facts().size() == 3);
      CHECK(storage->synchronized == storage->bytes);
      CHECK(!state->pending_proposal());
      completed = true;
      break;
    }
    error_is(submitted, ErrorCode::allocation);
    CHECK(state->committed_facts().size() == 2);
    CHECK(storage->synchronized == acknowledged);
    if (writes == storage->writes) {
      before_io = true;
      CHECK(state->state() == JournalWriterState::live);
      CHECK(!state->pending_proposal());
      CHECK(storage->bytes == acknowledged);
      const auto retried = require(state->submit(request));
      CHECK(!retried.existing && retried.evidence == RetainedEvidence::live);
      CHECK(state->committed_facts().size() == 3);
      CHECK(storage->bytes == storage->synchronized);
      CHECK(storage->bytes.size() == acknowledged.size() + 151);
    } else {
      during_io = true;
      CHECK(state->state() == JournalWriterState::poisoned);
      CHECK(state->pending_proposal().has_value());
      CHECK(require(state->submit(request)).evidence == RetainedEvidence::uncertain);
      Counter effect;
      error_is(state->dispatch(id<OperationAttemptId>(11), effect),
               ErrorCode::audit_unavailable);
      CHECK(effect.calls == 0);
    }
  }
  CHECK(completed && before_io && during_io);
}
void transition_matrix() {
  for (const auto disposition :
       {AttemptDisposition::success, AttemptDisposition::failure,
        AttemptDisposition::cancellation, AttemptDisposition::unknown}) {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    prepare(*state);
    const RetainedEvent terminal{{},
                                 AttemptObservationEvent{id<OperationAttemptId>(11),
                                                         AttemptPhase::terminal,
                                                         disposition, original}};
    if (disposition == AttemptDisposition::success ||
        disposition == AttemptDisposition::failure) {
      error_is(state->submit(terminal), ErrorCode::conflict);
      CHECK(!require(state->attempt(id<OperationAttemptId>(11))).observation);
    } else {
      require(state->submit(terminal));
      Counter effect;
      CHECK(!require(state->dispatch(id<OperationAttemptId>(11), effect)).dispatched);
      CHECK(effect.calls == 0);
    }
  }
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  prepare(*state);
  error_is(state->submit(
               {{}, AdapterReceiptEvent{id<OperationAttemptId>(11), std::nullopt}}),
           ErrorCode::conflict);
  require(state->submit({{}, AttemptOpenEvent{id<OperationAttemptId>(11)}}));
  const RetainedEvent running{
      {},
      AttemptObservationEvent{id<OperationAttemptId>(11), AttemptPhase::running,
                              AttemptDisposition::none, original}};
  CHECK(!require(state->submit(running)).existing);
  const auto written = storage->writes;
  CHECK(!require(state->submit(running)).existing && storage->writes > written);
  require(state->submit(
      {{},
       AttemptObservationEvent{id<OperationAttemptId>(11), AttemptPhase::settling,
                               AttemptDisposition::none, original}}));
  error_is(state->submit(running), ErrorCode::conflict);
  const auto settling = require(state->attempt(id<OperationAttemptId>(11)));
  CHECK(settling.observation && settling.observation->phase == AttemptPhase::settling);
  const RetainedEvent terminal{
      {},
      AttemptObservationEvent{id<OperationAttemptId>(11), AttemptPhase::terminal,
                              AttemptDisposition::success, original}};
  require(state->submit(terminal));
  const auto terminal_writes = storage->writes;
  CHECK(require(state->submit(terminal)).existing &&
        storage->writes == terminal_writes);
  error_is(state->submit(running), ErrorCode::conflict);
  auto changed = terminal;
  std::get<AttemptObservationEvent>(changed.body).disposition =
      AttemptDisposition::failure;
  error_is(state->submit(changed), ErrorCode::conflict);
  require(state->submit(
      {{}, AdapterReceiptEvent{id<OperationAttemptId>(11), Error{ErrorCode::io, 5}}}));
  const auto inspected = require(state->attempt(id<OperationAttemptId>(11)));
  CHECK(inspected.observation &&
        inspected.observation->disposition == AttemptDisposition::success);
  CHECK(inspected.receipt &&
        inspected.receipt->failure == std::optional<Error>{{ErrorCode::io, 5}});
}
void large_rejection() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  require(state->submit({{},
                         ComplaintEvent{id<ComplaintId>(31), id<ParticipantId>(4),
                                        std::vector<std::byte>(934, std::byte{33})}}));
  const auto complaint = [](unsigned char identity) {
    return RetainedEvent{{},
                         ComplaintEvent{id<ComplaintId>(identity), id<ParticipantId>(4),
                                        std::vector<std::byte>(934, std::byte{44})}};
  };
  const std::array requests{complaint(31), complaint(32), complaint(33), complaint(34)};
  error_is(state->append(state->cursor(), {}, requests), ErrorCode::conflict);
  CHECK(state->committed_facts().size() == 2);
  const auto &rejected = state->committed_facts().back().event;
  CHECK(std::holds_alternative<RejectedSubmissionEvent>(rejected.body));
  CHECK(rejected.dependencies.size() == 4);
  CHECK(rejected.dependencies[0].sequence == 3 &&
        rejected.dependencies[1].sequence == 4 &&
        rejected.dependencies[2].sequence == 5 &&
        rejected.dependencies[3].sequence == 7);
  CHECK(state->cursor().sequence == 10 && state->cursor().end_offset == 5598);
  std::vector<std::byte> proposal;
  for (const auto &reference : rejected.dependencies) {
    const auto bytes = require(state->source(reference));
    proposal.insert(proposal.end(), bytes.begin(), bytes.end());
  }
  CHECK(proposal.size() == 3976 && proposal[44] == std::byte{4});
  CHECK(proposal[48] == std::byte{210} && proposal[49] == std::byte{3});
  CHECK(proposal.back() == std::byte{44});
  // Construct the full proposal directly from its specified layout.
  std::vector<std::byte> expected(3976);
  const auto magic = std::as_bytes(std::span{"ARPROP01", 8});
  std::copy(magic.begin(), magic.end(), expected.begin());
  expected[23] = std::byte{2};
  expected[24] = std::byte{2};
  expected[32] = std::byte{154};
  expected[33] = std::byte{4};
  expected[44] = std::byte{4};
  for (std::size_t index = 0; index < 4; ++index) {
    const auto start = 48 + index * 982;
    expected[start] = std::byte{210};
    expected[start + 1] = std::byte{3};
    expected[start + 4] = std::byte{1};
    expected[start + 6] = std::byte{9};
    expected[start + 27] = static_cast<std::byte>(31 + index);
    expected[start + 43] = std::byte{4};
    expected[start + 44] = std::byte{166};
    expected[start + 45] = std::byte{3};
    std::fill(expected.begin() + static_cast<std::ptrdiff_t>(start + 48),
              expected.begin() + static_cast<std::ptrdiff_t>(start + 982),
              std::byte{44});
  }
  CHECK(proposal == expected);
  CHECK(std::get<ComplaintEvent>(state->committed_facts()[0].event.body).detail ==
        std::vector<std::byte>(934, std::byte{33}));
  CHECK(state->state() == JournalWriterState::live);
  state.reset();
  state = open(storage);
  require(state->confirm_recovery());
  CHECK(state->committed_facts().size() == 2);
  const auto recovered_facts = state->committed_facts();
  for (const auto &reference : recovered_facts.back().event.dependencies) {
    CHECK(!require(state->source(reference)).empty());
  }
}
void rejection_partial_failures() {
  for (const auto fail : std::array<std::size_t, 5>{3, 4, 5, 6, 7}) {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    require(
        state->submit({{},
                       ComplaintEvent{id<ComplaintId>(31), id<ParticipantId>(4),
                                      std::vector<std::byte>(934, std::byte{33})}}));
    const auto request = [](unsigned char identity) {
      return RetainedEvent{{},
                           ComplaintEvent{id<ComplaintId>(identity),
                                          id<ParticipantId>(4),
                                          std::vector<std::byte>(934, std::byte{44})}};
    };
    const std::array requests{request(31), request(32), request(33), request(34)};
    const auto expected_cursor = state->cursor();
    std::vector<std::vector<std::byte>> expected_events;
    for (const auto &event : requests) {
      expected_events.push_back(require(encode_retained_event(event, 1024)));
    }
    if (fail == 6) {
      storage->allocate_after_sync_call = 3;
    } else if (fail == 7) {
      storage->fail_extent_after_sync_call = 3;
    } else {
      storage->fail_sync_call = fail;
    }
    const auto result = state->append(state->cursor(), {}, requests);
    allocation_cut.store(-1);
    error_is(result, fail == 6 ? ErrorCode::allocation : ErrorCode::io);
    CHECK(state->state() != JournalWriterState::live);
    CHECK(state->state() ==
          (fail >= 6 ? JournalWriterState::blocked : JournalWriterState::poisoned));
    CHECK(state->committed_facts().size() == 1);
    CHECK(state->pending_proposal().has_value());
    const auto &pending = require_pending(*state);
    CHECK(pending.capture ==
          ProvisionalCaptureEvent{header().journal, 3, 3976,
                                  CapturePurpose::rejected_proposal});
    const auto packet = require(decode_retained_proposal(pending.bytes, 1 << 20));
    CHECK(packet.expected.journal == expected_cursor.journal &&
          packet.expected.sequence == expected_cursor.sequence &&
          packet.expected.end_offset == expected_cursor.end_offset);
    CHECK(packet.sources.empty() && packet.events == expected_events);
    const auto pending_before = pending.bytes;
    const auto writes = storage->writes;
    const std::array ordinary{RetainedEvent{{}, decision()}};
    error_is(state->append(state->cursor(), {}, ordinary),
             ErrorCode::audit_unavailable);
    CHECK(storage->writes == writes && require_pending(*state).bytes == pending_before);
    error_is(state->submit({{}, decision()}), ErrorCode::audit_unavailable);
    if (fail >= 4) {
      CHECK(require(state->source({header().journal, 3})).size() == 1024);
    }
  }
}
void rejection_exact_noncommit_capacity() {
  for (const std::size_t max_records : {std::size_t{5}, std::size_t{6}}) {
    auto storage = std::make_shared<StorageState>();
    auto state =
        require(RetainedState::create(std::make_unique<MemoryDirectory>(storage),
                                      "journal", header(), {1 << 20, max_records}));
    const auto request = [](unsigned char identity, unsigned char byte) {
      return RetainedEvent{
          {},
          ComplaintEvent{id<ComplaintId>(identity), id<ParticipantId>(4),
                         std::vector<std::byte>(934, std::byte{byte})}};
    };
    require(state->submit(request(31, 33))); // One semantic record, plus commit.
    const std::array requests{request(31, 44), request(32, 44), request(33, 44),
                              request(34, 44)};
    const auto writes = storage->writes;
    const auto originals = storage->bytes;
    const auto result = state->append(state->cursor(), {}, requests);
    CHECK(!state->pending_proposal());
    CHECK(state->state() == JournalWriterState::live);
    if (max_records == 5) {
      error_is(result, ErrorCode::capacity);
      CHECK(storage->writes == writes && storage->bytes == originals);
      CHECK(state->committed_facts().size() == 1 && state->cursor().sequence == 2);
    } else {
      error_is(result, ErrorCode::conflict);
      // Four chunk sources and one rejection marker exactly exhaust the five
      // remaining NONCOMMIT records. Two group commits and a marker commit also
      // consume sequence numbers, not record-index capacity.
      CHECK(state->committed_facts().size() == 2);
      CHECK(state->committed_facts().back().event.dependencies.size() == 4);
      CHECK(state->cursor().sequence == 10 && state->cursor().end_offset == 5598);
      CHECK(storage->bytes == storage->synchronized);
    }
  }
}
void no_allocation_after_acknowledged_sync() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  const std::array<ByteView, 1> sources{original};
  const std::array events{RetainedEvent{{{header().journal, 1}}, decision()}};
  storage->allocate_after_sync_call = 2; // Header sync was call1.
  auto written = state->append(state->cursor(), sources, events);
  allocation_cut.store(-1);
  require(std::move(written));
  CHECK(state->state() == JournalWriterState::live && state->cursor().sequence == 3);
  CHECK(!state->pending_proposal());
  CHECK(state->committed_facts().size() == 1);
  CHECK(require(state->source({header().journal, 1})) == original);
  CHECK(storage->bytes == storage->synchronized);
}
void torn_reservation_burn() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  require(state->issue<ParticipantId>());
  state.reset();
  const auto payload =
      require(encode_retained_event({{}, IssuerReservationEvent{1077}}, 1024));
  const auto frame = require(
      encode_journal_frame(FrameKind::semantic, 3, 3, payload, header().limits));
  storage->bytes.insert(storage->bytes.end(), frame.begin(), frame.end());
  state = open(storage);
  CHECK(state->issuer_counter() == 1077);
  const auto bytes = storage->bytes;
  const auto inspected = require(state->read_original_range(0, bytes.size()));
  CHECK(inspected.bytes == bytes && inspected.journal == header().journal);
  const auto report = state->recovery_report();
  CHECK(report.problem && report.problem->code == ErrorCode::incomplete);
  require(state->confirm_recovery());
  CHECK(state->state() == JournalWriterState::blocked &&
        state->issuer_counter() == 1077);
  error_is(state->issue<InvocationId>(), ErrorCode::audit_unavailable);
}
void rejection_preflight_limits() {
  auto storage = std::make_shared<StorageState>();
  auto limited_header = header();
  limited_header.limits = {64, 512};
  auto state = require(RetainedState::create(std::make_unique<MemoryDirectory>(storage),
                                             "journal", limited_header, capacity));
  require(state->submit({{},
                         ComplaintEvent{id<ComplaintId>(31), id<ParticipantId>(4),
                                        std::vector<std::byte>(20, std::byte{33})}}));
  const auto complaint = [](unsigned char identity) {
    return RetainedEvent{{},
                         ComplaintEvent{id<ComplaintId>(identity), id<ParticipantId>(4),
                                        std::vector<std::byte>(20, std::byte{44})}};
  };
  const std::array requests{complaint(31), complaint(32), complaint(33), complaint(34)};
  const auto originals = storage->bytes;
  const auto writes = storage->writes;
  error_is(state->append(state->cursor(), {}, requests), ErrorCode::capacity);
  CHECK(storage->writes == writes && storage->bytes == originals);
  CHECK(state->state() == JournalWriterState::live &&
        state->committed_facts().size() == 1);
}
void rejection_exceeds_single_batch() {
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  require(state->submit({{},
                         ComplaintEvent{id<ComplaintId>(31), id<ParticipantId>(4),
                                        std::vector<std::byte>(934, std::byte{33})}}));
  std::vector<RetainedEvent> requests;
  for (unsigned char identity = 31; identity != 36; ++identity) {
    requests.push_back({{},
                        ComplaintEvent{id<ComplaintId>(identity), id<ParticipantId>(4),
                                       std::vector<std::byte>(identity == 35 ? 73 : 934,
                                                              std::byte{44})}});
  }
  error_is(state->append(state->cursor(), {}, requests), ErrorCode::conflict);
  const auto facts = state->committed_facts();
  CHECK(facts.size() == 2 && facts.back().event.dependencies.size() == 5);
  std::vector<std::byte> proposal;
  for (const auto reference : facts.back().event.dependencies) {
    const auto bytes = require(state->source(reference));
    proposal.insert(proposal.end(), bytes.begin(), bytes.end());
  }
  CHECK(proposal.size() == 4097 && proposal[44] == std::byte{5});
  CHECK(state->cursor().sequence == 11 && state->cursor().end_offset == 5775);
}
void rejection_exact_marker_bound() {
  for (const bool above : {false, true}) {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    require(
        state->submit({{},
                       ComplaintEvent{id<ComplaintId>(31), id<ParticipantId>(4),
                                      std::vector<std::byte>(934, std::byte{33})}}));
    std::vector<RetainedEvent> requests;
    for (unsigned char identity = 31; identity != 74; ++identity) {
      const std::size_t size = identity == 73 ? (above ? 645 : 644) : 934;
      requests.push_back(
          {{},
           ComplaintEvent{id<ComplaintId>(identity), id<ParticipantId>(4),
                          std::vector<std::byte>(size, std::byte{44})}});
    }
    const auto writes = storage->writes;
    const auto originals = storage->bytes;
    const auto result = state->append(state->cursor(), {}, requests);
    if (above) {
      error_is(result, ErrorCode::capacity);
      CHECK(storage->writes == writes && storage->bytes == originals);
      CHECK(state->committed_facts().size() == 1);
    } else {
      // 48 + 43*4 + 42*978 + 688 = 41984 = 41*1024 exactly.
      error_is(result, ErrorCode::conflict);
      const auto facts = state->committed_facts();
      CHECK(facts.size() == 2 && facts.back().event.dependencies.size() == 41);
      CHECK(state->cursor().sequence == 59 && state->cursor().end_offset == 46350);
      for (const auto reference : facts.back().event.dependencies) {
        CHECK(require(state->source(reference)).size() == 1024);
      }
    }
    CHECK(state->state() == JournalWriterState::live);
  }
}
void retry_and_in_batch_conflict() {
  {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    auto changed = decision();
    changed.actor = id<ParticipantId>(44);
    const std::array requests{RetainedEvent{{}, decision()},
                              RetainedEvent{{}, changed}};
    error_is(state->append(state->cursor(), {}, requests), ErrorCode::conflict);
    CHECK(state->committed_facts().size() == 1);
    CHECK(std::holds_alternative<IdentityConflictEvent>(
        state->committed_facts()[0].event.body));
    CHECK(std::get<IdentityConflictEvent>(state->committed_facts()[0].event.body)
              .disputed_kind == RetainedKind::decision);
  }
  auto storage = std::make_shared<StorageState>();
  auto state = create(storage);
  prepare(*state);
  require(state->submit({{}, decision(13)}));
  require(state->submit({{},
                         RetryEvent{id<DecisionId>(13), id<InvocationId>(9),
                                    id<OperationAttemptId>(12)}}));
  require(state->submit({{}, admission(12, 13)}));
  error_is(state->submit({{},
                          RetryEvent{id<DecisionId>(13), id<InvocationId>(9),
                                     id<OperationAttemptId>(14)}}),
           ErrorCode::conflict);
  require(state->submit({{}, decision(14)}));
  require(state->submit({{},
                         RetryEvent{id<DecisionId>(14), id<InvocationId>(9),
                                    id<OperationAttemptId>(14)}}));
  require(state->submit({{}, admission(14, 14)}));
  auto other = decision(30);
  other.planned_invocations = {id<InvocationId>(19)};
  require(state->submit({{}, other}));
  require(state->submit({{},
                         InvocationEvent{id<InvocationId>(19), id<DecisionId>(30),
                                         id<DefinitionGenerationId>(10), original}}));
  require(state->submit(
      {{},
       AttemptAdmissionEvent{id<OperationAttemptId>(21), id<InvocationId>(19),
                             id<DecisionId>(30), original}}));
  auto shared = decision(40);
  shared.planned_invocations.push_back(id<InvocationId>(19));
  require(state->submit({{}, shared}));
  const RetainedEvent first_retry{
      {},
      RetryEvent{id<DecisionId>(40), id<InvocationId>(9), id<OperationAttemptId>(50)}};
  require(state->submit(first_retry));
  require(state->submit({{},
                         RetryEvent{id<DecisionId>(40), id<InvocationId>(19),
                                    id<OperationAttemptId>(51)}}));
  const auto writes = storage->writes;
  CHECK(require(state->submit(first_retry)).existing && storage->writes == writes);
}

std::uint64_t transaction_cost(const RetainedEvent &event) {
  return 56 + journal_frame_header_size +
         require(encode_retained_event(event, header().limits.max_payload)).size();
}
void atomic_program_settlement() {
  auto storage = std::make_shared<StorageState>();
  auto state = require(RetainedState::create(std::make_unique<MemoryDirectory>(storage),
                                             "journal", header(), {8192, 128}));
  prepare(*state);
  Counter counter;
  require(require(state->dispatch(id<OperationAttemptId>(11), counter)).recording);
  const RetainedEvent program{{},
                              ApplicationRecordEvent{id<ApplicationRecordId>(70),
                                                     ApplicationChannel::program,
                                                     original}};
  const RetainedEvent terminal{
      {},
      AttemptObservationEvent{id<OperationAttemptId>(11), AttemptPhase::terminal,
                              AttemptDisposition::success, original}};
  const std::array batch{program, terminal};
  const auto bytes = transaction_cost(program) + transaction_cost(terminal) - 56;
  auto scope = require(state->protect_settlement({bytes, 2}));
  const auto before_invalid = state->cursor();
  const std::array invalid{program, RetainedEvent{{}, admission(12)}};
  error_is(state->append_settlement(before_invalid, invalid), ErrorCode::invalid_range);
  CHECK(state->cursor().end_offset == before_invalid.end_offset);
  const std::vector<std::byte> filler(128, std::byte{0x5a});
  const std::array<ByteView, 1> sources{filler};
  for (;;) {
    const auto added = state->append(state->cursor(), sources, {});
    if (!added.has_value()) {
      error_is(added, ErrorCode::capacity);
      break;
    }
  }
  const auto before = state->cursor();
  error_is(state->append(before, {}, batch), ErrorCode::capacity);
  require(state->append_settlement(before, batch));
  CHECK(state->cursor().sequence == before.sequence + 3);
  CHECK(present(state->protected_settlement()).max_records == 0);
  CHECK(present(state->protected_settlement()).max_file_bytes == 0);
  const auto attempt = require(state->attempt(id<OperationAttemptId>(11)));
  CHECK(attempt.observation &&
        attempt.observation->disposition == AttemptDisposition::success);
  scope.reset();
}
void protected_settlement_faults() {
  const RetainedEvent terminal{
      {},
      AttemptObservationEvent{id<OperationAttemptId>(11), AttemptPhase::terminal,
                              AttemptDisposition::unknown, original}};
  const RetainedEvent receipt{
      {}, AdapterReceiptEvent{id<OperationAttemptId>(11), Error{ErrorCode::capacity}}};
  const JournalCapacity credit{transaction_cost(receipt) + transaction_cost(terminal),
                               2};
  // Independent byte and record limits, through real dispatch/callback/receipt.
  for (bool records : {false, true}) {
    auto storage = std::make_shared<StorageState>();
    auto state = require(RetainedState::create(
        std::make_unique<MemoryDirectory>(storage), "journal", header(),
        {records ? 65536U : 4096U, records ? 7U : 128U}));
    prepare(*state);
    auto scope = require(state->protect_settlement(credit));
    error_is(state->protect_settlement(credit), ErrorCode::busy);
    error_is(state->submit_settlement({{}, admission(12)}), ErrorCode::invalid_range);
    error_is(
        state->submit_settlement(
            {{},
             AttemptObservationEvent{id<OperationAttemptId>(11), AttemptPhase::running,
                                     AttemptDisposition::unknown, original}}),
        ErrorCode::invalid_range);
    struct Capture final : EffectBoundary {
      RetainedState &state;
      StorageState &storage;
      std::vector<SourceReference> retained;
      unsigned calls = 0;
      Capture(RetainedState &s, StorageState &f) : state(s), storage(f) {}
      Result<void> dispatch(const EffectIntent &) override {
        ++calls;
        error_is(state.protect_settlement({1, 1}), ErrorCode::busy);
        for (;;) {
          const auto usage = state.journal_usage();
          const auto floor = present(state.protected_settlement());
          const auto normal = present(usage.remaining_bytes()) - floor.max_file_bytes;
          const auto overhead = 56 + journal_frame_header_size;
          // Exactly fill available bytes, unless record room runs out first.
          const auto length =
              normal > overhead ? std::min<std::uint64_t>(1024, normal - overhead) : 1;
          const std::vector<std::byte> chunk(length, std::byte{0x5a});
          const std::array<ByteView, 1> sources{chunk};
          const auto before = state.cursor();
          const auto extent = storage.bytes.size();
          const auto result = state.append(before, sources, {});
          if (!result.has_value()) {
            error_is(result, ErrorCode::capacity);
            CHECK(state.cursor().end_offset == before.end_offset);
            CHECK(storage.bytes.size() == extent);
            CHECK(!state.pending_proposal());
            // Earlier originals remain addressable; rejected chunk isn't claimed
            // retained.
            for (auto reference : retained) {
              auto bytes = require(state.source(reference));
              CHECK(!bytes.empty() && bytes.front() == std::byte{0x5a});
            }
            return Result<void>::failure({ErrorCode::capacity});
          }
          retained.push_back({before.journal, before.sequence + 1});
        }
      }
    } capture{*state, *storage};
    const auto report = require(state->dispatch(id<OperationAttemptId>(11), capture));
    CHECK(report.dispatched && capture.calls == 1 && !capture.retained.empty());
    error_is(report.effect, ErrorCode::capacity);
    require(report.recording);
    CHECK(present(state->protected_settlement()).max_file_bytes ==
          transaction_cost(terminal));
    CHECK(present(state->protected_settlement()).max_records == 1);
    require(state->submit_settlement(terminal));
    CHECK(present(state->protected_settlement()).max_file_bytes == 0);
    CHECK(present(state->protected_settlement()).max_records == 0);
    const auto before = state->cursor().end_offset;
    CHECK(require(state->submit_settlement(terminal)).existing);
    CHECK(state->cursor().end_offset == before);
    const auto attempt = require(state->attempt(id<OperationAttemptId>(11)));
    CHECK(attempt.receipt && attempt.observation &&
          attempt.observation->disposition == AttemptDisposition::unknown);
    CHECK(!require(state->dispatch(id<OperationAttemptId>(11), capture)).dispatched);
    CHECK(capture.calls == 1);
    scope.reset();
    CHECK(!state->protected_settlement());
  }
  // Holding all remaining room refuses open BEFORE effect; releasing only drops RAM
  // floor.
  {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    prepare(*state);
    const auto usage = state->journal_usage();
    auto scope = require(state->protect_settlement(
        {present(usage.remaining_bytes()), usage.remaining_records()}));
    Counter counter;
    const auto before = storage->bytes;
    error_is(state->dispatch(id<OperationAttemptId>(11), counter), ErrorCode::capacity);
    CHECK(counter.calls == 0 && storage->bytes == before);
    CHECK(require(state->submit({{}, decision()})).existing);
    const std::array duplicate{RetainedEvent{{}, decision()}};
    error_is(state->append(state->cursor(), {}, duplicate), ErrorCode::capacity);
    // Issuer allocation and rejected-proposal diagnostics cannot steal credits.
    error_is(state->issue<DecisionId>(), ErrorCode::capacity);
    auto stale = state->cursor();
    --stale.sequence;
    const std::array events{RetainedEvent{{}, decision(24)}};
    error_is(state->append(stale, {}, events), ErrorCode::capacity);
    CHECK(storage->bytes == before);
    scope.reset();
    CHECK(storage->bytes == before);
    require(require(state->dispatch(id<OperationAttemptId>(11), counter)).recording);
    CHECK(counter.calls == 1);
  }
  // Grant failure is pre-mutation, including extent errors and allocation failure.
  {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    storage->fail_extent = true;
    CHECK(!state->protect_settlement(credit).has_value());
    CHECK(!state->protected_settlement());
    storage->fail_extent = false;
    allocation_cut.store(0);
    const auto failed = state->protect_settlement(credit);
    allocation_cut.store(-1);
    error_is(failed, ErrorCode::allocation);
    CHECK(!state->protected_settlement());
    prepare(*state);
    Counter counter;
    require(require(state->dispatch(id<OperationAttemptId>(11), counter)).recording);
    auto scope = require(state->protect_settlement({1, 1}));
    const auto before = storage->bytes;
    error_is(state->submit_settlement(terminal), ErrorCode::capacity);
    CHECK(storage->bytes == before && state->state() == JournalWriterState::live);
    CHECK(present(state->protected_settlement()).max_file_bytes == 1);
    scope.reset();
    require(state->submit_settlement(terminal));
  }
  // Allowance itself and oversized terminal are rejected prewrite, no credit consumed.
  {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    prepare(*state);
    error_is(state->protect_settlement({UINT64_MAX, SIZE_MAX}), ErrorCode::capacity);
    error_is(state->protect_settlement({0, 1}), ErrorCode::invalid_range);
    auto scope = require(state->protect_settlement({1, 1}));
    Counter counter;
    const auto report = require(state->dispatch(id<OperationAttemptId>(11), counter));
    CHECK(report.dispatched && counter.calls == 1);
    error_is(report.recording, ErrorCode::capacity);
    CHECK(state->state() == JournalWriterState::blocked);
    CHECK(present(state->protected_settlement()).max_file_bytes == 1);
    error_is(state->submit_settlement(terminal), ErrorCode::audit_unavailable);
  }
  // Write and sync failures in reserved receipt remain unknown/unpublished/poisoned.
  for (bool sync : {false, true}) {
    auto storage = std::make_shared<StorageState>();
    auto state = create(storage);
    prepare(*state);
    auto scope = require(state->protect_settlement(credit));
    Counter counter;
    if (sync)
      counter.fail_recording_sync = storage.get();
    else
      counter.fail_recording_write = storage.get();
    const auto report = require(state->dispatch(id<OperationAttemptId>(11), counter));
    CHECK(report.dispatched && counter.calls == 1 && report.effect.has_value());
    CHECK(!report.recording.has_value());
    CHECK(state->state() == JournalWriterState::poisoned);
    CHECK(present(state->protected_settlement()).max_file_bytes ==
          credit.max_file_bytes);
    CHECK(state->pending_proposal().has_value());
    CHECK(require(state->attempt(id<OperationAttemptId>(11))).evidence ==
          RetainedEvidence::uncertain);
    for (const auto &fact : state->committed_facts())
      CHECK(!std::holds_alternative<AdapterReceiptEvent>(fact.event.body));
    error_is(state->protect_settlement(credit), ErrorCode::busy);
    scope.reset();
    error_is(state->protect_settlement(credit), ErrorCode::audit_unavailable);
  }
}
} // namespace
int main(int argc, char **) {
  indexed_mode = argc > 1;
  try {
    indexed_errors_close_queries();
    atomic_program_settlement();
    protected_settlement_faults();
    admission_references();
    duplicates_and_retries();
    recovery_and_failed_admission();
    uncertain_open();
    if (!indexed_mode)
      replay_avoids_prefix_payload_copies();
    sources_and_invalid_replay();
    pending_source_inspection();
    pending_proposal_owns_originals();
    first_submission_every_write_cut();
    pending_proposal_clean_outcomes();
    rejected_wrong_cursor_owns_caller_packet();
    root_refuses_continuation_records();
    issuer_count_and_credit();
    issuer();
    rejected_batch_and_suffix();
    receipt_settlement_and_failures();
    allocation_before_publication();
    transition_matrix();
    retry_and_in_batch_conflict();
    large_rejection();
    rejection_partial_failures();
    rejection_exact_noncommit_capacity();
    no_allocation_after_acknowledged_sync();
    torn_reservation_burn();
    rejection_preflight_limits();
    rejection_exceeds_single_batch();
    rejection_exact_marker_bound();
    std::puts(
        "PASS retained state, exact dedup, fenced recovery and once-only effects");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "FAIL retained state: %s\n", error.what());
    return 1;
  }
}
