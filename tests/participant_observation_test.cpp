#include "blackbird/participants.hpp"
#include "blackbird/raw_reader.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
using Clock = std::chrono::steady_clock;
thread_local bool deny_allocations = false, pause_allocation = false;
std::atomic_bool allocation_entered = false, release_allocation = false;
std::atomic_size_t denied = 0;
void *allocate(std::size_t size) {
  if (pause_allocation) {
    pause_allocation = false;
    allocation_entered = true;
    while (!release_allocation.load())
      std::this_thread::yield();
  }
  if (deny_allocations) {
    ++denied;
    throw std::bad_alloc{};
  }
  if (auto *result = std::malloc(size ? size : 1))
    return result;
  throw std::bad_alloc{};
}
void require(bool good, const char *message) {
  if (!good)
    throw std::runtime_error{message};
}
template <class F> void until(F predicate, const char *message) {
  const auto deadline = Clock::now() + std::chrono::seconds{5};
  while (!predicate()) {
    require(Clock::now() < deadline, message);
    std::this_thread::yield();
  }
}
ParticipantObservationId id(std::string_view text) {
  require(text.size() <= 128, "fixture ID fits actual participant bound");
  ParticipantObservationId result;
  result.size = text.size();
  std::copy(text.begin(), text.end(), result.bytes.begin());
  return result;
}
ParticipantObservation ready(const ParticipantObservationReader &reader,
                             ParticipantObservationQuery query = {}) {
  ParticipantObservation result;
  until(
      [&] {
        result = reader.try_observe(query);
        return result.status != ParticipantObservationStatus::busy;
      },
      "finite observation contention");
  require(result.status == ParticipantObservationStatus::ready,
          "actual participant owner publishes a ready native observation");
  return result;
}
ParticipantObservation selected(const ParticipantObservationReader &reader,
                                std::string_view key) {
  ParticipantObservationQuery query;
  query.selected = id(key);
  const auto result = ready(reader, query);
  require(result.selection == ParticipantObservationSelection::resident &&
              result.detail.id.view() == key,
          "selected detail names the exact actual resident participant");
  return result;
}
Value request(std::string_view delivery) {
  return Value::object(
      {{"request_id", Value{std::string{delivery}}},
       {"from", Value{"operator"}},
       {"to", Value{"expert"}},
       {"provider", Value{"openai"}},
       {"model", Value{"fixture-model"}},
       {"task", Value{"inspect the actual owner"}},
       {"context", Value{Value::Array{}}},
       {"profile", Value::object({{"name", Value{"context-only"}},
                                  {"provenance", Value{"private fixture"}},
                                  {"timeout_seconds", Value{Number{10}}},
                                  {"tools", Value{"none"}},
                                  {"requests", Value{Number{1}}}})}});
}
Value start_arguments(std::string_view delivery) {
  return Value::object(
      {{"request", request(delivery)}, {"task_id", Value{"task-real"}}});
}
Value run_arguments(std::string_view key) {
  return Value::object({{"run_id", Value{std::string{key}}}});
}
Value answer(std::string_view text) {
  auto content = Value::object(
      {{"type", Value{"output_text"}}, {"text", Value{std::string{text}}}});
  auto message = Value::object({{"type", Value{"message"}},
                                {"role", Value{"assistant"}},
                                {"content", Value{Value::Array{std::move(content)}}}});
  return Value::object({{"status", Value{"completed"}},
                        {"model", Value{"observed-model"}},
                        {"output", Value{Value::Array{std::move(message)}}}});
}
void join(Participants &pool, std::string_view key) {
  pool.join(Value::object({{"run_ids", Value{Value::Array{Value{std::string{key}}}}}}));
}
struct AnsweredRequest {
  std::string_view run_id, request_id;
};
void await_result(const ParticipantObservationReader &reader, AnsweredRequest target) {
  until(
      [&] {
        const auto result = selected(reader, target.run_id);
        return result.result.result_available &&
               result.result.last_answered_request.view() == target.request_id &&
               result.detail.phase == ParticipantObservationPhase::waiting;
      },
      "actual answer publication remains visible without drain");
}
std::string window(const ParticipantObservation &observation) {
  return {reinterpret_cast<const char *>(observation.window.bytes.data()),
          observation.window.size};
}
struct Gate {
  std::atomic_uint entered = 0, released = 0;
  ParticipantTransport transport() {
    return [&](const Value &prepared, const ColleagueCapture &,
               const std::function<bool()> &cancelled, const RawAdapterContext *) {
      const auto turn = ++entered;
      until([&] { return released.load() >= turn || cancelled(); },
            "private release gate");
      if (cancelled())
        throw Error{ErrorCode::interrupted};
      return answer("answer:" + string_field(field(prepared, "request"), "request_id"));
    };
  }
};
void semantic() {
  Gate gate;
  unsigned retains = 0, saves = 0;
  Participants pool{Value{},
                    [&](const OriginalCapture &) {
                      ++retains;
                      return std::optional<SourceReference>{};
                    },
                    [&](const Value &) {
                      ++saves;
                      return std::optional<ApplicationRecordId>{};
                    }};
  const auto reader = pool.observer();
  const auto empty = ready(reader);
  require(empty.row_count == 0 && !empty.more_rows && empty.incarnation != 0,
          "ready empty catalog has an actual live incarnation");
  pool.start("expert/nonhex", start_arguments("initial"), gate.transport());
  until([&] { return gate.entered.load() == 1; }, "real participant dispatch");
  auto value = selected(reader, "expert/nonhex");
  require(value.detail.worker_started && !value.detail.worker_ended &&
              value.detail.phase == ParticipantObservationPhase::running &&
              value.detail.task_id.view() == "task-real" &&
              value.result.initial_request.view() == "initial" &&
              value.result.current_request.view() == "initial" &&
              !value.result.result_available,
          "native roster reflects real ID, addressed request and actual lifecycle");
  const auto prior_retains = retains, prior_saves = saves;
  const auto prior_denied = denied.load();
  deny_allocations = true;
  const auto pure = reader.try_observe({});
  deny_allocations = false;
  require(
      pure.status == ParticipantObservationStatus::ready &&
          denied.load() == prior_denied && retains == prior_retains &&
          saves == prior_saves && gate.entered == 1,
      "native observation neither allocates nor drains, saves or invokes transport");
  gate.released = 1;
  await_result(reader, {.run_id = "expert/nonhex", .request_id = "initial"});
  value = selected(reader, "expert/nonhex");
  require(window(value) == "answer:initial" && value.detail.pending_captures > 0 &&
              retains == prior_retains && saves == prior_saves,
          "actual latest answer and pending originals observed without owner effects");
  const auto direction = Value::object({{"run_id", Value{"expert/nonhex"}},
                                        {"message_id", Value{"direction"}},
                                        {"from", Value{"operator"}},
                                        {"text", Value{"continue the inspection"}}});
  pool.send(direction);
  until([&] { return gate.entered.load() == 2; }, "next addressed request dispatched");
  value = selected(reader, "expert/nonhex");
  require(value.detail.accepted == 1 &&
              value.result.initial_request.view() == "initial" &&
              value.result.current_request.view() == "initial:direction" &&
              value.result.last_answered_request.view() == "initial" &&
              window(value) == "answer:initial",
          "initial/current/last-answer identities stay distinct during next request");
  gate.released = 2;
  await_result(reader, {.run_id = "expert/nonhex", .request_id = "initial:direction"});
  join(pool, "expert/nonhex");
  value = selected(reader, "expert/nonhex");
  require(value.detail.business_finished && value.detail.worker_ended &&
              value.detail.worker_joined && value.detail.pending_captures == 0 &&
              !value.result.initial_retained && !value.result.current_retained &&
              !value.result.result_retained,
          "explicit owner join publishes actual completion without conflating "
          "observation");
}
void ranges() {
  Participants pool{
      Value{}, [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
      [](const Value &) { return std::optional<ApplicationRecordId>{}; }};
  const auto reader = pool.observer();
  const std::string large(100000, 'Z');
  for (unsigned n = 0; n < 32; ++n) {
    const auto key = "run-" + std::to_string(100 + n);
    pool.start(key, start_arguments(key),
               [&](const Value &, const ColleagueCapture &,
                   const std::function<bool()> &,
                   const RawAdapterContext *) { return answer(large); });
    await_result(reader, {.run_id = key, .request_id = key});
    join(pool, key);
  }
  ParticipantObservationQuery query;
  query.row_limit = 3;
  query.selected = id("run-100");
  std::string reconstructed;
  std::size_t rows = 0;
  for (;;) {
    const auto value = ready(reader, query);
    require(
        value.row_count <= 3 && value.examined_rows <= 4 && value.seek_steps <= 16 &&
            value.examined_fields <= 32 * 32 && value.result.projection_known &&
            value.result.projection_truncated && value.window.total == 8192,
        "finite shallow work and explicit truncated projection over actual 32 runs");
    rows += value.row_count;
    if (!value.more_rows)
      break;
    require(value.next.has_value(), "actual catalog continuation");
    query.after = value.next;
  }
  require(rows == 32, "pages cover all actual resident runs exactly once");
  query.after.reset();
  query.row_limit = 0;
  query.byte_limit = 997;
  while (reconstructed.size() < 8192) {
    query.offset = reconstructed.size();
    const auto value = ready(reader, query);
    require(value.window.begin == query.offset && value.window.size <= 997 &&
                value.window.next == query.offset + value.window.size &&
                value.window.more == (value.window.next < 8192),
            "exact selected byte pagination without a whole-answer scan");
    reconstructed += window(value);
  }
  require(reconstructed == large.substr(0, 8192), "exact original projection prefix");
  query.row_limit = 9;
  require(reader.try_observe(query).status ==
              ParticipantObservationStatus::invalid_range,
          "oversized row budget explicitly refused");
  query.row_limit = 8;
  query.byte_limit = 4097;
  require(reader.try_observe(query).status ==
              ParticipantObservationStatus::invalid_range,
          "oversized detail budget explicitly refused");
  query.byte_limit = 0;
  query.selected = id("absent");
  require(ready(reader, query).selection ==
              ParticipantObservationSelection::not_resident,
          "missing selected ID differs from empty answer");
  auto invalid_id = id("invalid");
  invalid_id.size = 129;
  query.selected = invalid_id;
  require(reader.try_observe(query).status ==
              ParticipantObservationStatus::invalid_identity,
          "malformed ID length checked before reading its storage");
}
void busy() {
  Participants pool{
      Value{}, [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
      [](const Value &) {
        // The next allocation is configure's returned Value under its guard,
        // after save_snapshot has finished all its own preparatory allocation.
        pause_allocation = true;
        return std::optional<ApplicationRecordId>{};
      }};
  const auto reader = pool.observer();
  (void)ready(reader);
  const auto arguments = Value::object({{"concurrency", Value{Number{2}}}});
  std::exception_ptr failure;
  std::jthread holder{[&] {
    try {
      (void)pool.configure(arguments);
    } catch (...) {
      failure = std::current_exception();
    }
  }};
  const auto deadline = Clock::now() + std::chrono::seconds{5};
  while (!allocation_entered && Clock::now() < deadline)
    std::this_thread::yield();
  const bool entered = allocation_entered;
  const auto observed = reader.try_observe({});
  release_allocation = true;
  holder.join();
  if (failure)
    std::rethrow_exception(failure);
  require(entered && observed.status == ParticipantObservationStatus::busy,
          "real product guard contention returns busy without waiting");
  (void)ready(reader);
}
void lifecycle() {
  Value saved;
  auto owner = std::make_unique<Participants>(
      Value{}, [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
      [&](const Value &value) {
        saved = value;
        return std::optional<ApplicationRecordId>{};
      });
  const auto reader = owner->observer();
  (void)ready(reader);
  owner->start("stable/id", start_arguments("initial"),
               [](const Value &, const ColleagueCapture &,
                  const std::function<bool()> &,
                  const RawAdapterContext *) { return answer("exact answer"); });
  const auto running_saved = saved;
  await_result(reader, {.run_id = "stable/id", .request_id = "initial"});
  join(*owner, "stable/id");
  owner->archive(run_arguments("stable/id"));
  ParticipantObservationQuery query;
  query.selected = id("stable/id");
  require(ready(reader, query).selection ==
              ParticipantObservationSelection::not_resident,
          "archive retires exact selected identity without selecting a neighbor");
  std::atomic_bool observing = false;
  std::exception_ptr failure;
  std::jthread concurrent{[&](const std::stop_token &stop) {
    try {
      observing = true;
      while (!stop.stop_requested()) {
        const auto value = reader.try_observe({});
        require(value.status == ParticipantObservationStatus::ready ||
                    value.status == ParticipantObservationStatus::busy ||
                    value.status == ParticipantObservationStatus::retired,
                "concurrent retirement returns a finite old sample or retired");
      }
    } catch (...) {
      failure = std::current_exception();
    }
  }};
  until([&] { return observing.load(); },
        "reader entered actual owner retirement schedule");
  const auto old_scope = ready(reader).incarnation;
  owner.reset();
  require(reader.try_observe({}).status == ParticipantObservationStatus::retired,
          "retained reader cannot pin or dereference destroyed participant owner");
  concurrent.request_stop();
  concurrent.join();
  if (failure)
    std::rethrow_exception(failure);
  Participants restored{
      running_saved,
      [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
      [](const Value &) { return std::optional<ApplicationRecordId>{}; }};
  const auto next = restored.observer();
  const auto value = selected(next, "stable/id");
  require(value.incarnation != old_scope && value.detail.restored &&
              value.detail.reopened &&
              value.detail.phase == ParticipantObservationPhase::unknown &&
              !value.detail.worker_started && !value.result.current_request.present,
          "restoration is a distinct scope with unknown old execution, no adopted "
          "worker");
  query.incarnation = old_scope;
  require(next.try_observe(query).status ==
              ParticipantObservationStatus::scope_mismatch,
          "old scoped selection cannot attach to same ID in new owner");
  const auto &row = field(running_saved, "runs").array().front();
  const auto saved_with_result = [&](Value result) {
    Value::Object copied = row.object();
    for (auto &[name, item] : copied)
      if (name == "result") {
        item = std::move(result);
        break;
      }
    Value::Array rows{Value{std::move(copied)}};
    return Value::object(
        {{"concurrency", Value{Number{2}}}, {"runs", Value{std::move(rows)}}});
  };
  Value::Object too_many;
  for (unsigned n = 0; n < 40; ++n)
    too_many.emplace_back(std::to_string(n), Value{Number{n}});
  Participants invalid{
      saved_with_result(Value{std::move(too_many)}),
      [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
      [](const Value &) { return std::optional<ApplicationRecordId>{}; }};
  query.incarnation = 0;
  require(invalid.observer().try_observe(query).status ==
              ParticipantObservationStatus::unavailable,
          "oversized shallow result is refused before unbounded field scan");
  Participants nested{
      saved_with_result(Value::object(
          {{"status", Value{"completed"}},
           {"remote_disposition", Value{"completed"}},
           {"text", Value{"visible restored answer"}},
           {"usage", Value{std::string(1024 * 1024, 'N')}},
           {"reported_error", Value{Value::Array(10000, Value{Number{7}})}}})),
      [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
      [](const Value &) { return std::optional<ApplicationRecordId>{}; }};
  const auto nested_reader = nested.observer();
  const auto prior_denied = denied.load();
  deny_allocations = true;
  const auto shallow = nested_reader.try_observe(query);
  deny_allocations = false;
  require(
      shallow.status == ParticipantObservationStatus::ready &&
          shallow.examined_fields < 200 && denied.load() == prior_denied &&
          window(shallow) == "visible restored answer" &&
          !shallow.result.projection_known,
      "large nested metadata is ignored without allocation or invented completeness");
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char name[] = "/tmp/blackbird-participant-observation-XXXXXX";
    require(::mkdtemp(name) != nullptr, "private raw observation directory");
    path = name;
  }
  ~Directory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
template <class T> T take(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "actual native record field extent");
  T value;
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
void raw() {
  Directory directory;
  std::optional<ProviderOriginalReference> initial, result_source;
  std::string retained_result;
  IdentityBytes journal_bytes{};
  journal_bytes[0] = std::byte{31};
  const auto journal = unwrap(AuditStreamId::from_bytes(journal_bytes));
  std::optional<SourceReference> expected_retained;
  {
    IdentityBytes environment{};
    environment[0] = std::byte{79};
    auto runtime = unwrap(RawRuntime::open(
        directory.path.string(), unwrap(EnvironmentId::from_bytes(environment))));
    auto lane = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{lane};
    bool fail_result = true;
    std::uint64_t sequence = 0;
    Participants pool{
        Value{},
        [&](const OriginalCapture &capture) -> std::optional<SourceReference> {
          if (capture.label == "participant.result") {
            if (fail_result)
              throw Error{ErrorCode::io};
            retained_result = capture.bytes;
            expected_retained = SourceReference{journal, ++sequence};
            return expected_retained;
          }
          return SourceReference{journal, ++sequence};
        },
        [](const Value &) { return std::optional<ApplicationRecordId>{}; }};
    const auto reader = pool.observer();
    (void)ready(reader);
    pool.start("raw/actual", start_arguments("raw-request"),
               [](const Value &, const ColleagueCapture &,
                  const std::function<bool()> &,
                  const RawAdapterContext *) { return answer("actual raw answer"); });
    await_result(reader, {.run_id = "raw/actual", .request_id = "raw-request"});
    auto value = selected(reader, "raw/actual");
    require(value.result.initial_original.has_value() &&
                value.result.result_original.has_value() &&
                !value.result.result_retained && value.detail.pending_captures > 0,
            "actual raw original differs from not-yet-retained semantic result");
    initial = value.result.initial_original;
    result_source = value.result.result_original;
    bool failed = false;
    try {
      pool.drain();
    } catch (const Error &error) {
      failed = error.code == ErrorCode::io;
    }
    value = selected(reader, "raw/actual");
    require(failed && value.detail.pending_captures > 0 &&
                !value.result.result_retained && pool.debt(),
            "failed semantic retention leaves source/debt and read-only observation "
            "intact");
    fail_result = false;
    pool.drain();
    value = selected(reader, "raw/actual");
    require(
        value.result.result_retained == expected_retained &&
            expected_retained.has_value() && value.detail.pending_captures == 0 &&
            window(value) == "actual raw answer",
        "explicit owner retention publishes exactly its returned semantic reference");
    pool.shutdown();
  }
  if (!initial.has_value() || !result_source.has_value())
    throw std::runtime_error{"actual source refs survive copied observation"};
  const auto runs = unwrap(discover_raw_runs(directory.path.string()));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "one real closed recorder run");
  auto reader =
      unwrap(RawRunReader::open((directory.path / "raw" / runs.front().name).string()));
  std::vector<std::byte> storage(1024 * 1024);
  bool found_initial = false, found_result = false;
  while (const auto record = unwrap(reader->read(storage))) {
    auto bytes = record->raw;
    require(take<std::uint32_t>(bytes) == record->raw.size(),
            "raw envelope exact length");
    const auto site = take<std::uint32_t>(bytes);
    const auto ordinal = take<std::uint64_t>(bytes);
    const auto check = [&](const ProviderOriginalReference &source,
                           std::string_view expected, ProviderOriginalRole role) {
      if (source.event.lane != record->origin.lane || source.event.ordinal != ordinal)
        return false;
      require(site == 1322 && source.partition == RawPartition::data &&
                  source.record_offset <= record->raw.size() &&
                  source.bytes == record->raw.size() - source.record_offset,
              "observation reference resolves actual codec original extent");
      auto body = bytes;
      (void)take<IdentityBytes>(body);
      (void)take<std::uint64_t>(body);
      const auto actual_role = take<std::uint8_t>(body);
      const auto actual_size = take<std::size_t>(body);
      require(
          actual_role == static_cast<std::uint8_t>(role) &&
              actual_size == body.size() && source.bytes == actual_size &&
              source.record_offset == record->raw.size() - body.size(),
          "independently decoded codec schema proves actual role and original extent");
      const auto payload = body;
      const std::string_view text{reinterpret_cast<const char *>(payload.data()),
                                  payload.size()};
      require(text.find(expected) != std::string_view::npos,
              "actual original contains independently known addressed request/result "
              "bytes");
      if (role == ProviderOriginalRole::wrapper_packet)
        require(text == retained_result,
                "semantic retained result equals exact raw original");
      return true;
    };
    found_initial =
        check(*initial, "raw-request", ProviderOriginalRole::prepared_admission) ||
        found_initial;
    found_result =
        check(*result_source, retained_result, ProviderOriginalRole::wrapper_packet) ||
        found_result;
  }
  require(found_initial && found_result,
          "all observation original refs resolve actual recording");
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
    require(argc == 2, "one participant observation case");
    const std::string_view mode{argv[1]};
    if (mode == "semantic")
      semantic();
    else if (mode == "ranges")
      ranges();
    else if (mode == "busy")
      busy();
    else if (mode == "lifecycle")
      lifecycle();
    else if (mode == "raw")
      raw();
    else
      throw std::runtime_error{"unknown participant observation case"};
    std::cout << "PASS " << mode << '\n';
    return 0;
  } catch (const std::exception &error) {
    deny_allocations = false;
    release_allocation = true;
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  } catch (...) {
    deny_allocations = false;
    release_allocation = true;
    std::cerr << "FAIL native owner exception\n";
    return 1;
  }
}
