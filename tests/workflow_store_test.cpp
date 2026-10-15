#include "blackbird/workflow_store.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

using namespace blackbird;
namespace {
std::atomic_bool reject_allocations{false};
const char *stage = "startup";
} // namespace
void *operator new(std::size_t size) {
  if (reject_allocations.load())
    throw std::bad_alloc{};
  if (void *memory = std::malloc(size == 0 ? 1 : size))
    return memory;
  throw std::bad_alloc{};
}
void operator delete(void *memory) noexcept { std::free(memory); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *memory) noexcept { ::operator delete(memory); }
void operator delete(void *memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete[](void *memory, std::size_t) noexcept {
  ::operator delete[](memory);
}

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class T> T identity(unsigned char byte) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{byte};
  return unwrap(T::from_bytes(bytes));
}
template <class Action> bool rejects(Action &&action, ErrorCode code) {
  try {
    action();
  } catch (const Error &error) {
    return error.code == code;
  }
  return false;
}
JournalHeader header() {
  return {identity<EnvironmentId>(1),
          identity<AuditStreamId>(2),
          3,
          {1024 * 1024, 4 * 1024 * 1024},
          std::nullopt};
}
constexpr JournalCapacity capacity{64 * 1024 * 1024, 20000};
const auto first = identity<WorkflowCellId>(11);
const auto second = identity<WorkflowCellId>(12);
const WorkflowCellSchema schema{"test.counter", 1};
WorkflowTransaction seed() {
  return {"seed",
          {{first, {0}}, {second, {0}}},
          {{first, schema, Value{Number{10}}}, {second, schema, Value{Number{20}}}},
          Value{"initial decision"}};
}
WorkflowTransaction advance(std::string submission = "advance") {
  return {std::move(submission),
          {{first, {1}}, {second, {1}}},
          {{first, schema, Value{Number{11}}}, {second, schema, Value{Number{21}}}},
          Value{"two-cell decision"}};
}
WorkflowCell cell(const WorkflowStore &store, WorkflowCellId id) {
  auto found = store.read(id);
  if (!found)
    throw std::runtime_error("expected cell is absent");
  return std::move(*found);
}
bool same_cursor(JournalCursor left, JournalCursor right) {
  return left.journal == right.journal && left.sequence == right.sequence &&
         left.end_offset == right.end_offset;
}
std::size_t deltas(const RetainedState &root) {
  std::size_t count = 0;
  for (std::size_t ordinal = 0; ordinal < root.fact_count(); ++ordinal) {
    const auto fact = unwrap(root.fact(ordinal));
    const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
    if (record && record->channel == ApplicationChannel::program) {
      const auto packet = unwrap(read_application_packet(*record));
      const auto *label = packet.find("label");
      if (label && *label == Value{"workflow-cells-delta-v1"})
        ++count;
    }
  }
  return count;
}
void native(const std::filesystem::path &path) {
  WorkflowCommit original{false, 0, {header().journal, 0}};
  Value snapshot;
  std::uint64_t boundary = 0;
  {
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                         NativeJournalDirectory::open(path.string()))),
                                     "audit", header(), capacity));
    AuditLog log{*root};
    WorkflowStore store{log};
    check(!store.read(first) && store.revision() == 0, "nonempty initial store");
    stage = "seed transaction";
    original = store.transact(seed());
    check(original.revision == 1 && !original.duplicate && deltas(*root) == 1,
          "seed is not one retained transition");
    const auto saved_cursor = root->cursor();
    auto duplicate = seed();
    std::reverse(duplicate.reads.begin(), duplicate.reads.end());
    std::reverse(duplicate.writes.begin(), duplicate.writes.end());
    stage = "duplicate transaction";
    const auto retry = store.transact(duplicate);
    check(retry.duplicate && retry.revision == 1 && retry.record == original.record &&
              same_cursor(root->cursor(), saved_cursor),
          "exact retry produced a new commit");
    duplicate.decision = Value{"different decision"};
    check(rejects([&] { (void)store.transact(duplicate); }, ErrorCode::conflict) &&
              same_cursor(root->cursor(), saved_cursor),
          "changed duplicate decision accepted");
    stage = "stale transaction";
    auto stale = advance("stale");
    stale.reads[1].revision = {0};
    check(rejects([&] { (void)store.transact(stale); }, ErrorCode::conflict) &&
              cell(store, first).value == Value{Number{10}} &&
              cell(store, second).value == Value{Number{20}} && deltas(*root) == 1 &&
              same_cursor(root->cursor(), saved_cursor),
          "stale multi-cell update partially published");
    const auto decision_id = unwrap(root->issue<DecisionId>());
    const RetainedEvent decision{{},
                                 DecisionEvent{decision_id,
                                               identity<ParticipantId>(21),
                                               identity<ConversationId>(22),
                                               identity<WorkflowId>(23),
                                               identity<DefinitionGenerationId>(24),
                                               identity<ContextRevisionId>(25),
                                               {},
                                               {}}};
    const std::array events{decision};
    stage = "native atomic transaction";
    const auto together = store.transact(advance(), events);
    check(together.revision == 2 && deltas(*root) == 2 &&
              unwrap(root->decision(decision_id)) ==
                  std::get<DecisionEvent>(decision.body),
          "native decision and cells did not commit together");
    const auto together_cursor = root->cursor();
    check(store.transact(advance(), events).duplicate &&
              same_cursor(root->cursor(), together_cursor),
          "native event retry repeated transition");
    check(rejects([&] { (void)store.transact(advance()); }, ErrorCode::conflict),
          "changed native event list accepted as duplicate");
    stage = "invalid native transaction";
    auto invalid = advance("bad-native");
    invalid.reads = {{first, {2}}, {second, {2}}};
    const std::array invalid_events{
        RetainedEvent{{}, AttemptOpenEvent{identity<OperationAttemptId>(99)}}};
    check(rejects([&] { (void)store.transact(invalid, invalid_events); },
                  ErrorCode::conflict) &&
              store.revision() == 2 && cell(store, first).revision.value == 2,
          "invalid native transition published cells");
    stage = "thread/process owner";
    bool thread_refused = false;
    std::thread other{[&] {
      thread_refused =
          rejects([&] { (void)store.read(first); }, ErrorCode::wrong_incarnation);
    }};
    other.join();
    check(thread_refused, "foreign thread accessed store");
    const auto child = ::fork();
    check(child >= 0, "fork failed");
    if (child == 0)
      ::_exit(rejects([&] { (void)store.snapshot(); }, ErrorCode::wrong_incarnation)
                  ? 0
                  : 7);
    int status = 0;
    check(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0,
          "forked process inherited owner authority");
    stage = "object canonical equality";
    auto payload = Value::object({{"z", Value{"last"}}, {"a", Value{"first"}}});
    WorkflowTransaction object{
        "object", {{first, {2}}}, {{first, schema, payload}}, std::nullopt};
    store.transact(object);
    payload.object()[0].second = Value{"caller mutation"};
    object.writes[0].value =
        Value::object({{"a", Value{"first"}}, {"z", Value{"last"}}});
    check(store.transact(object).duplicate &&
              field(cell(store, first).value, "z") == Value{"last"},
          "map order or caller mutation changed committed payload");
    stage = "snapshot and suffix";
    snapshot = store.snapshot();
    boundary = root->cursor().sequence;
    WorkflowTransaction suffix{
        "suffix", {{second, {2}}}, {{second, schema, Value{Number{30}}}}, std::nullopt};
    store.transact(suffix);
  }
  {
    auto root =
        unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                                       NativeJournalDirectory::open(path.string()))),
                                   "audit", header(), capacity));
    unwrap(root->confirm_recovery());
    AuditLog log{*root};
    stage = "reopened store construction";
    WorkflowStore reopened{log};
    check(reopened.revision() == 4 &&
              cell(reopened, second).value == Value{Number{30}} &&
              reopened.transact(seed()).record == original.record,
          "reopen lost cell state or exact submission receipt");
    stage = "snapshot reducer";
    WorkflowCellState reduced;
    reduced.restore(snapshot);
    for (std::size_t ordinal = 0; ordinal < root->fact_count(); ++ordinal) {
      const auto fact = unwrap(root->fact(ordinal));
      const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
      if (record && record->channel == ApplicationChannel::program &&
          fact.record.sequence > boundary) {
        const auto packet = unwrap(read_application_packet(*record));
        if (field(packet, "label") == Value{"workflow-cells-delta-v1"})
          reduced.replay(packet, fact.record, ordinal);
      }
    }
    check(reduced.snapshot() == reopened.snapshot(),
          "snapshot suffix differs from replay");
    auto malformed = snapshot;
    malformed.object().emplace_back("unexpected", Value{true});
    const auto before = reduced.snapshot();
    check(rejects([&] { reduced.restore(malformed); }, ErrorCode::corrupt) &&
              reduced.snapshot() == before,
          "malformed snapshot partially replaced reduction");
  }
}

struct Memory {
  std::vector<std::byte> bytes;
  bool exists = false, locked = false, fail_sync = false, reject_after_sync = false;
};
class MemoryFile final : public JournalFile {
public:
  explicit MemoryFile(std::shared_ptr<Memory> memory) : memory_(std::move(memory)) {}
  ~MemoryFile() override {
    if (owns_lock_)
      memory_->locked = false;
  }
  Result<void> lock_writer() override {
    if (memory_->locked)
      return Result<void>::failure({ErrorCode::busy});
    memory_->locked = true;
    owns_lock_ = true;
    return Result<void>::success();
  }
  Result<std::uint64_t> extent() override {
    return Result<std::uint64_t>::success(memory_->bytes.size());
  }
  Result<std::size_t> read_at(std::uint64_t offset, MutableByteView output) override {
    if (offset >= memory_->bytes.size())
      return Result<std::size_t>::success(0);
    const auto count = std::min(output.size(), memory_->bytes.size() -
                                                   static_cast<std::size_t>(offset));
    std::copy_n(memory_->bytes.begin() + static_cast<std::ptrdiff_t>(offset), count,
                output.begin());
    return Result<std::size_t>::success(count);
  }
  Result<std::size_t> write_at(std::uint64_t offset, ByteView input) override {
    memory_->bytes.resize(static_cast<std::size_t>(offset) + input.size());
    std::copy(input.begin(), input.end(),
              memory_->bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return Result<std::size_t>::success(input.size());
  }
  Result<void> synchronize(SyncStrength) override {
    if (memory_->fail_sync)
      return Result<void>::failure({ErrorCode::io, 5});
    if (memory_->reject_after_sync)
      reject_allocations.store(true);
    return Result<void>::success();
  }

private:
  std::shared_ptr<Memory> memory_;
  bool owns_lock_ = false;
};
class MemoryDirectory final : public JournalDirectory {
public:
  explicit MemoryDirectory(std::shared_ptr<Memory> memory)
      : memory_(std::move(memory)) {}
  Result<std::unique_ptr<JournalFile>> create_exclusive(std::string_view) override {
    if (memory_->exists)
      return Result<std::unique_ptr<JournalFile>>::failure({ErrorCode::conflict});
    memory_->exists = true;
    return file();
  }
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view,
                                                     FileAccess) override {
    return file();
  }
  Result<void> synchronize_directory(SyncStrength) override {
    return Result<void>::success();
  }

private:
  Result<std::unique_ptr<JournalFile>> file() {
    return Result<std::unique_ptr<JournalFile>>::success(
        std::make_unique<MemoryFile>(memory_));
  }
  std::shared_ptr<Memory> memory_;
};
void publication() {
  stage = "publication fixture";
  auto memory = std::make_shared<Memory>();
  {
    auto root = unwrap(RetainedState::create(std::make_unique<MemoryDirectory>(memory),
                                             "audit", header(), capacity));
    AuditLog log{*root};
    WorkflowStore store{log};
    store.transact(seed());
    stage = "post-sync no allocation";
    memory->reject_after_sync = true;
    bool no_allocation_failure = true;
    try {
      store.transact(advance());
    } catch (...) {
      no_allocation_failure = false;
    }
    reject_allocations.store(false);
    memory->reject_after_sync = false;
    check(no_allocation_failure && store.revision() == 2 &&
              cell(store, first).value == Value{Number{11}},
          "publication allocated after durable append");
    auto failed = advance("failed-sync");
    failed.reads = {{first, {2}}, {second, {2}}};
    stage = "failed synchronization";
    memory->fail_sync = true;
    check(rejects([&] { (void)store.transact(failed); }, ErrorCode::io) &&
              store.revision() == 2,
          "failed durability published cells");
    memory->fail_sync = false;
  }
  // The sync failure leaves complete, unacknowledged bytes. Reopen interprets
  // the recovered committed batch; caller ignorance does not authorize replanning.
  stage = "unacknowledged batch reopen";
  auto root = unwrap(RetainedState::open(std::make_unique<MemoryDirectory>(memory),
                                         "audit", header(), capacity));
  unwrap(root->confirm_recovery());
  AuditLog log{*root};
  stage = "reopened store construction";
  WorkflowStore reopened{log};
  check(reopened.revision() == 3, "unacknowledged committed batch was ignored");
}
} // namespace
int main() {
  char pattern[] = "/tmp/blackbird-workflow-store-XXXXXX";
  const auto *path = ::mkdtemp(pattern);
  if (!path)
    return 2;
  try {
    native(path);
    publication();
    std::filesystem::remove_all(path);
    std::cout << "workflow cells: atomic reads/writes/native decisions, exact retries, "
                 "reopen, owner fences and publication failures passed\n";
  } catch (const std::exception &error) {
    reject_allocations.store(false);
    std::cerr << stage << ": " << error.what() << '\n';
    std::filesystem::remove_all(path);
    return 1;
  } catch (const Error &error) {
    reject_allocations.store(false);
    std::cerr << stage << ": " << error_name(error.code) << ':' << error.detail << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
}
