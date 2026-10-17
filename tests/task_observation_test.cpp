#include "blackbird/raw_ui.hpp"
#include "blackbird/task_observation.hpp"
#include "blackbird/tasks.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <unistd.h>

using namespace blackbird;
namespace {
thread_local bool refuse_allocations = false;
std::atomic_size_t attempted{0};
void *allocate(std::size_t size) {
  if (refuse_allocations) {
    ++attempted;
    throw std::bad_alloc{};
  }
  if (auto *pointer = std::malloc(size ? size : 1))
    return pointer;
  throw std::bad_alloc{};
}
void require(bool value, const char *claim) {
  if (!value)
    throw std::runtime_error(claim);
}
TaskObservation observe(const TaskState &state,
                        const TaskObservationQuery &query = {}) noexcept {
  return state.observe(query);
}
TaskObservation without_allocation(const TaskState &state,
                                   const TaskObservationQuery &query = {}) {
  refuse_allocations = true;
  const auto observation = observe(state, query);
  refuse_allocations = false;
  require(attempted == 0, "pure task observation attempts no allocation");
  return observation;
}
void put(Value &object, std::string_view key, Value value) {
  for (auto &[name, field_value] : object.object())
    if (name == key) {
      field_value = std::move(value);
      return;
    }
  object.object().emplace_back(key, std::move(value));
}
Value &member(Value &object, std::string_view key) {
  for (auto &[name, value] : object.object())
    if (name == key)
      return value;
  throw std::runtime_error("missing mutable fixture field");
}
Value add(std::string title) {
  return Value::object({{"op", Value{"add"}}, {"title", Value{std::move(title)}}});
}
Value apply(TaskState &state, Value::Array operations) {
  auto prepared = state.prepare(
      Value::object({{"op_id", Value{"edit-" + std::to_string(state.revision())}},
                     {"base", Value{Number{state.revision()}}},
                     {"ops", Value{std::move(operations)}}}));
  const auto publication = prepared.packet;
  state.commit(std::move(prepared));
  return publication;
}
void allocation() {
  TaskState state;
  const auto empty = without_allocation(state);
  require(empty.status == TaskObservationStatus::ready && empty.row_count == 0 &&
              empty.total == 0 && empty.revision == 0 && empty.title.view() == "Work",
          "bounded empty task observation is ready with allocation refused");
  Value::Array operations;
  operations.push_back(Value::object({{"op", Value{"list"}},
                                      {"title", Value{std::string(512, 'L')}},
                                      {"bead", Value{std::string(128, 'B')}}}));
  for (unsigned n = 0; n < 8; ++n) {
    auto row = add(std::string(512, static_cast<char>('a' + n)));
    put(row, "owner", Value{std::string(128, 'O')});
    put(row, "note", Value{std::string(2048, 'N')});
    put(row, "blocker", Value{std::string(512, 'X')});
    operations.push_back(std::move(row));
  }
  (void)apply(state, std::move(operations));
  const auto before = state.snapshot();
  TaskObservationQuery query;
  query.selected = TaskObservationId{1};
  const auto result = without_allocation(state, query);
  require(result.status == TaskObservationStatus::ready && result.row_count == 8 &&
              result.selection == TaskObservationSelection::resident &&
              result.examined_rows == 9 && result.copied_text_bytes == 8960 &&
              result.detail.row.title.view() ==
                  std::string_view{std::string(512, 'a')} &&
              result.detail.note.size == 2048 && result.detail.blocker.size == 512,
          "full admitted text stays owned within exact nine-projection byte budget");
  require(state.snapshot() == before && state.revision() == 1,
          "observation neither edits nor changes current task owner state");
}
void ranges() {
  TaskState state;
  Value::Array operations;
  for (unsigned n = 1; n <= 10; ++n)
    operations.push_back(add("task " + std::to_string(n)));
  (void)apply(state, std::move(operations));
  TaskObservationQuery query;
  query.selected = TaskObservationId{9};
  auto result = without_allocation(state, query);
  require(result.status == TaskObservationStatus::ready && result.row_count == 8 &&
              result.rows.front().id.ordinal == 1 && result.rows[7].id.ordinal == 8 &&
              result.examined_rows == 9 && result.more_rows && result.next &&
              *result.next == TaskObservationCursor{1, 8} &&
              result.detail.row.id.ordinal == 9,
          "source-order catalog and independent selected detail keep full page");
  query.cursor = result.next;
  query.selected = TaskObservationId{1};
  result = without_allocation(state, query);
  require(result.row_count == 2 && result.rows[0].id.ordinal == 9 &&
              result.rows[1].id.ordinal == 10 && !result.more_rows && !result.next &&
              result.examined_rows == 3 && result.detail.row.id.ordinal == 1,
          "revision-bound continuation and selected item outside page are exact");
  query.cursor.reset();
  query.row_limit = 0;
  result = without_allocation(state, query);
  require(result.row_count == 0 && result.examined_rows == 1 && result.more_rows &&
              !result.next && result.selection == TaskObservationSelection::resident,
          "detail-only query copies no catalog page or nonprogress cursor");
  query.row_limit = 9;
  require(observe(state, query).status == TaskObservationStatus::invalid_range,
          "over-budget task row request is explicit");
  query.row_limit = 8;
  query.selected = TaskObservationId{0};
  require(observe(state, query).status == TaskObservationStatus::invalid_identity,
          "zero native task identity is invalid");
  query.selected.reset();
  query.cursor = TaskObservationCursor{1, 11};
  require(observe(state, query).status == TaskObservationStatus::invalid_range,
          "cursor beyond actual order is invalid");
  query.cursor = TaskObservationCursor{0, 0};
  result = observe(state, query);
  require(result.status == TaskObservationStatus::revision_conflict &&
              result.revision == 1 && result.row_count == 0,
          "stale page cannot mix source revisions");
  query.cursor = TaskObservationCursor{1, 10};
  result = observe(state, query);
  require(result.status == TaskObservationStatus::ready && result.row_count == 0 &&
              !result.more_rows && !result.next,
          "exact order frontier is a valid empty page");
  query.cursor.reset();
  query.selected = TaskObservationId{UINT64_MAX};
  require(without_allocation(state, query).selection ==
              TaskObservationSelection::not_resident,
          "well-formed unissued uint64 identity is not resident");
}
void semantic() {
  TaskState state;
  auto parent = add("investigation");
  put(parent, "status", Value{"blocked"});
  put(parent, "owner", Value{"free-text owner"});
  put(parent, "note", Value{"read exact original"});
  put(parent, "blocker", Value{"awaiting evidence"});
  auto child = add("finished child");
  put(child, "parent", Value{"t1"});
  put(child, "status", Value{"done"});
  auto other = add("other work");
  put(other, "status", Value{"active"});
  (void)apply(state, {parent, child, other});
  TaskObservationQuery query;
  query.selected = TaskObservationId{1};
  const auto original = without_allocation(state, query);
  require(original.total == 3 &&
              original.counts == std::array<std::size_t, 5>{0, 1, 1, 0, 0} &&
              original.detail.row.declared_status == TaskDeclaredStatus::blocked &&
              original.detail.row.subtasks == 1 &&
              original.detail.row.subtasks_done == 1 &&
              original.rows[1].parent == std::optional{TaskObservationId{1}} &&
              original.rows[1].declared_status == TaskDeclaredStatus::done &&
              original.detail.row.owner.view() == "free-text owner" &&
              original.detail.note.view() == "read exact original" &&
              original.detail.blocker.view() == "awaiting evidence",
          "declared parent state, child rollup, labels and top-level populations stay "
          "distinct");
  TaskState replica;
  replica.restore(state.snapshot());
  const auto delta =
      apply(state, {Value::object({{"op", Value{"set"}},
                                   {"id", Value{"t1"}},
                                   {"version", Value{Number{1}}},
                                   {"title", Value{"revised investigation"}},
                                   {"status", Value{"active"}}})});
  replica.replay(delta);
  const auto changed = without_allocation(replica, query);
  require(changed.revision == 2 && changed.detail.row.version == 2 &&
              changed.detail.row.title.view() == "revised investigation" &&
              changed.detail.row.declared_status == TaskDeclaredStatus::active &&
              changed.counts == std::array<std::size_t, 5>{0, 2, 0, 0, 0} &&
              original.detail.row.title.view() == "investigation",
          "real snapshot/delta replay updates actual fields without altering copied "
          "observation");
  query.cursor = TaskObservationCursor{1, 0};
  require(observe(state, query).status == TaskObservationStatus::revision_conflict,
          "ordinary item edit invalidates old catalog revision");
  query.cursor.reset();
  (void)apply(state, {Value::object({{"op", Value{"archive"}},
                                     {"id", Value{"t1"}},
                                     {"version", Value{Number{2}}}})});
  query.selected = TaskObservationId{2};
  const auto archived = without_allocation(state, query);
  require(archived.selection == TaskObservationSelection::not_resident &&
              archived.total == 1 && archived.rows[0].id.ordinal == 3 &&
              archived.counts == std::array<std::size_t, 5>{0, 1, 0, 0, 0},
          "archiving actual parent retires child projection without inventing history "
          "loss");
}
void numeric() {
  TaskState seed;
  (void)apply(seed, {add("boundary")});
  const auto original = seed.snapshot();
  struct NumericCase {
    Number value;
    std::uint64_t expected;
  };
  const std::array cases{
      NumericCase{Number{UINT64_MAX}, UINT64_MAX},
      NumericCase{Number{INT64_MAX}, static_cast<std::uint64_t>(INT64_MAX)},
      NumericCase{Number{"18446744073709551615.0"}, UINT64_MAX},
      NumericCase{Number{42.0}, 42}, NumericCase{Number{"1e16"}, 10000000000000000ULL}};
  for (const auto &test : cases) {
    auto snapshot = original;
    put(snapshot, "next", Value{Number{UINT64_MAX}});
    auto &row = member(snapshot, "items").array().front();
    put(row, "id", Value{"t18446744073709551614"});
    put(row, "version", Value{test.value});
    put(row, "updated", Value{test.value});
    put(snapshot, "order", Value{Value::Array{Value{"t18446744073709551614"}}});
    TaskState state;
    state.restore(snapshot);
    TaskObservationQuery query;
    query.selected = TaskObservationId{UINT64_MAX - 1};
    const auto result = without_allocation(state, query);
    require(result.status == TaskObservationStatus::ready &&
                result.detail.row.id.ordinal == UINT64_MAX - 1 &&
                result.detail.row.version == test.expected &&
                result.detail.row.updated == test.expected,
            "accepted exact numeric representations and maximum issued task identity "
            "survive observation");
  }
  auto extended = original;
  auto &row = member(extended, "items").array().front();
  for (unsigned n = 0; n < 4096; ++n)
    row.object().emplace_back("extra-" + std::to_string(n), Value{"not a UI field"});
  TaskState state;
  state.restore(
      extended); // Existing restore accepts extra keys; preserve its contract.
  require(
      without_allocation(state).status == TaskObservationStatus::unavailable,
      "noncanonical admitted row is explicitly unavailable within finite field budget");
}
void bounded() {
  TaskState state;
  for (std::size_t begin = 0; begin < TaskState::max_items;
       begin += TaskState::max_batch) {
    Value::Array operations;
    for (std::size_t n = begin; n < begin + TaskState::max_batch; ++n)
      operations.push_back(add("task " + std::to_string(n + 1)));
    (void)apply(state, std::move(operations));
  }
  const auto revision = state.revision();
  TaskObservationQuery query;
  query.cursor = TaskObservationCursor{revision, 2040};
  query.selected = TaskObservationId{1};
  const auto result = without_allocation(state, query);
  require(
      result.status == TaskObservationStatus::ready && result.total == 2048 &&
          result.row_count == 8 && result.examined_rows == 9 &&
          result.rows[0].id.ordinal == 2041 && result.rows[7].id.ordinal == 2048 &&
          result.detail.row.id.ordinal == 1 && !result.more_rows && !result.next &&
          result.copied_text_bytes <= 8960 && state.revision() == revision,
      "2048-item last page and early selected lookup obey real projection/copy bounds");
  (void)apply(state, {Value::object({{"op", Value{"move"}},
                                     {"id", Value{"t2048"}},
                                     {"version", Value{Number{1}}},
                                     {"after", Value{"t1"}}})});
  require(observe(state, query).status == TaskObservationStatus::revision_conflict,
          "structural reorder rejects old ordered cursor");
  query.cursor.reset();
  query.selected = TaskObservationId{2048};
  const auto moved = without_allocation(state, query);
  require(moved.rows[0].id.ordinal == 1 && moved.rows[1].id.ordinal == 2048 &&
              moved.detail.row.id.ordinal == 2048 && moved.detail.row.version == 2,
          "identity survives actual reorder without relabeling a positional row");
}
void raw() {
  IdentityBytes epoch{};
  epoch[0] = std::byte{78};
  const auto name = "/bb-task-observation-" + std::to_string(::getpid());
  auto channel =
      unwrap(RawChannel::create(name, epoch, {32 * 1024 * 1024, 65536, 4096}));
  {
    RawProducer producer{channel};
    RawThreadBinding binding{producer, epoch};
    RawCaptureFailureScope capture{true};
    TaskState state;
    (void)apply(state, {add("actual raw task")});
    const auto before = unwrap(raw_emit(9270, {}));
    std::vector<RawCapacityClaim> holds;
    holds.reserve(128);
    for (const auto partition : {RawPartition::data, RawPartition::control}) {
      for (std::uint64_t bytes = 1ULL << 26; bytes; bytes >>= 1) {
        auto grant = partition == RawPartition::data ? raw_try_claim_data(bytes)
                                                     : raw_try_claim_control(bytes);
        if (grant.has_value())
          holds.push_back(std::move(grant).value());
      }
      const auto refused = partition == RawPartition::data ? raw_try_claim_data(1)
                                                           : raw_try_claim_control(1);
      require(!refused.has_value() && refused.error().code == ErrorCode::capacity,
              "actual current producer capture partition is exhausted");
    }
    TaskObservationQuery query;
    query.selected = TaskObservationId{1};
    const auto result = without_allocation(state, query);
    require(result.status == TaskObservationStatus::ready &&
                result.detail.row.title.view() == "actual raw task" && !capture.error(),
            "pure task observation stays ready under real capture pressure without "
            "fencing");
    holds.clear();
    const auto after = unwrap(raw_emit(9271, {}));
    require(
        after == before + 1,
        "actual same-producer raw ordinals prove pure observation emitted no event");
  }
  unwrap(RawChannel::unlink(name));
}
} // namespace
void *operator new(std::size_t size) { return allocate(size); }
void *operator new[](std::size_t size) { return allocate(size); }
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept {
  ::operator delete[](pointer);
}
int main(int argc, char **argv) {
  try {
    require(argc == 2, "one task observation case selector");
    const std::string_view mode{argv[1]};
    if (mode == "allocation")
      allocation();
    else if (mode == "semantic")
      semantic();
    else if (mode == "ranges")
      ranges();
    else if (mode == "numeric")
      numeric();
    else if (mode == "bounded")
      bounded();
    else if (mode == "raw")
      raw();
    else
      require(false, "unknown task observation case");
    std::cout << "PASS " << mode << '\n';
  } catch (const Error &error) {
    refuse_allocations = false;
    std::cerr << "FAIL " << error_name(error.code) << ':' << error.detail << '\n';
    return 1;
  } catch (const std::exception &error) {
    refuse_allocations = false;
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
