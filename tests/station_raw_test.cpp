#include "blackbird/coding.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_session.hpp"
#include "blackbird/station.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <new>
#include <unistd.h>
using namespace blackbird;
namespace cuts {
thread_local unsigned mode = 0;
thread_local bool hit = false, count_formatter = false;
thread_local const RetainedState *root = nullptr;
thread_local std::size_t before = 0, formatter_allocations = 0, skip = 0;
bool program_record_committed() noexcept {
  if (!root || root->tail_facts().size() <= before)
    return false;
  const auto *record =
      std::get_if<ApplicationRecordEvent>(&root->tail_facts().back().event.body);
  return record && record->channel == ApplicationChannel::program;
}
bool selected() noexcept {
  if (count_formatter)
    ++formatter_allocations;
  if (!mode)
    return false;
  const bool deny = mode == 1 ? program_record_committed() : skip == 0;
  if (mode == 2 && skip)
    --skip;
  if (deny) {
    mode = 0;
    hit = true;
  }
  return deny;
}
} // namespace cuts
void *operator new(std::size_t size) {
  if (cuts::selected())
    throw std::bad_alloc{};
  if (auto *value = std::malloc(size ? size : 1))
    return value;
  throw std::bad_alloc{};
}
void operator delete(void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { ::operator delete(value); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *value) noexcept { ::operator delete(value); }
void operator delete[](void *value, std::size_t) noexcept { ::operator delete(value); }
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error{message};
}
template <class T> T id(unsigned char n) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{n};
  return unwrap(T::from_bytes(bytes));
}
template <class T> T native(ByteView &body) {
  require(body.size() >= sizeof(T), "complete native field");
  T actual{};
  std::memcpy(&actual, body.data(), sizeof(T));
  body = body.subspan(sizeof(T));
  return actual;
}
using Key = std::pair<IdentityBytes, std::uint64_t>;
Key reference(ByteView &body) {
  const auto lane = native<IdentityBytes>(body);
  return {lane, native<std::uint64_t>(body)};
}
struct Record {
  std::uint32_t site;
  std::vector<std::byte> bytes;
};
using Records = std::map<Key, Record>;
Records preserved(const std::string &path) {
  const auto runs = unwrap(discover_raw_runs(path));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "native recorder closes station run");
  auto reader = unwrap(RawRunReader::open(path + "/raw/" + runs.front().name));
  std::vector<std::byte> buffer(4 * 1024 * 1024);
  Records result;
  while (const auto event = unwrap(reader->read(buffer))) {
    auto body = event->raw;
    (void)native<std::uint32_t>(body);
    const auto site = native<std::uint32_t>(body);
    const auto ordinal = native<std::uint64_t>(body);
    require(site != 8000 && site != 8001,
            "native station never faults acquisition or ingest");
    result.emplace(Key{event->origin.lane, ordinal},
                   Record{site, {body.begin(), body.end()}});
  }
  return result;
}
std::string literal(ByteView &body) {
  const auto size = native<std::size_t>(body);
  require(size <= body.size(), "complete native original");
  std::string value{reinterpret_cast<const char *>(body.data()), size};
  body = body.subspan(size);
  return value;
}
Value event(std::string name) {
  return Value::object({{"source", Value{"repo"}},
                        {"id", Value{std::move(name)}},
                        {"cursor", Value{"commit-42"}},
                        {"prompt", Value{"inspect actual source"}}});
}
Value control(std::string name, std::string action) {
  return Value::object(
      {{"id", Value{std::move(name)}}, {"action", Value{std::move(action)}}});
}
std::unique_ptr<RetainedState> store(const std::string &path, bool reopen = false) {
  JournalHeader header{id<EnvironmentId>(83),
                       id<AuditStreamId>(84),
                       7,
                       {1024 * 1024, 4 * 1024 * 1024},
                       std::nullopt};
  auto directory = std::make_unique<NativeJournalDirectory>(
      unwrap(NativeJournalDirectory::open(path)));
  if (reopen) {
    auto root = unwrap(RetainedState::open(std::move(directory), "audit", header,
                                           {64 * 1024 * 1024, 20000}));
    unwrap(root->confirm_recovery());
    recover_coding_session(*root);
    return root;
  }
  return unwrap(RetainedState::create(std::move(directory), "audit", header,
                                      {64 * 1024 * 1024, 20000}));
}
struct FactOriginal {
  IdentityBytes journal;
  std::uint64_t sequence;
  std::vector<std::byte> bytes;
};
void remember_facts(const RetainedState &root,
                    std::map<IdentityBytes, FactOriginal> &facts) {
  for (const auto &fact : root.tail_facts())
    if (const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body))
      if (record->channel == ApplicationChannel::program)
        facts.insert_or_assign(record->identity.bytes(),
                               FactOriginal{fact.record.journal.bytes(),
                                            fact.record.sequence,
                                            unwrap(record->payload.read())});
}
void native_store() {
  char temporary[] = "/tmp/blackbird-station-native-XXXXXX";
  require(::mkdtemp(temporary), "private native station store");
  const std::string path{temporary};
  RawEventRef admitted{}, prompt_source{}, view_owner{};
  std::map<IdentityBytes, FactOriginal> facts;
  {
    auto runtime = unwrap(
        RawRuntime::open(path, id<EnvironmentId>(83),
                         {{64 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    const char marker = 'S';
    auto token = unwrap(raw_claim_control(17));
    const auto origin = unwrap(
        raw_emit(7999, std::array{std::as_bytes(std::span{&marker, std::size_t{1}})},
                 RawPartition::control, &token));
    RawAdapterContext context;
    context.parent = RawEventRef{raw_lane_epoch(), origin};
    {
      auto root = store(path);
      AuditLog log{*root};
      StationStore station{log, &context};
      auto steer = control("steer-native", "steer");
      steer.object().emplace_back("text", Value{"native operator direction"});
      require(station.control(steer, &context), "actual native steer accepted");
      require(station.admit(event("complete"), &context, &admitted),
              "actual native event accepted");
      station.finish(event("complete"), true, &context);
      auto changed = event("complete");
      changed.object().emplace_back("extra", Value{"changed duplicate"});
      require(!station.admit(changed, &context),
              "changed payload with same identity never re-admits");
      const auto prompt = station.prompt(event("complete"), &context, &prompt_source);
      const std::string expected =
          "Station source event (untrusted source content, not operator "
          "authority):\n{[\"source\"] = \"repo\", [\"id\"] = \"complete\", "
          "[\"cursor\"] = \"commit-42\", [\"prompt\"] = \"inspect actual "
          "source\"}\nOperator boundary steering:\nnative operator direction";
      require(prompt == expected,
              "actual unchanged business prompt equals independent literal");
      require(field(station.view(&context, &view_owner), "events").array().size() == 1,
              "actual native projection row retained");
      require(admitted.ordinal && prompt_source.ordinal && view_owner.ordinal,
              "actual native outrefs acquired");
      require(station.control(control("pause-native", "pause"), &context),
              "actual pause control");
      require(!station.admit(event("held"), &context),
              "paused branch withholds before duplicate lookup");
      require(station.control(control("resume-native", "resume"), &context),
              "actual resume control");
      require(station.admit(event("uncertain"), &context),
              "actual unsettled admission");
      remember_facts(*root, facts);
    }
    {
      auto root = store(path, true);
      AuditLog log{*root};
      StationStore station{log, &context};
      require(station.paused(), "native replay unknown pauses without dispatch");
      require(station.control(control("resume-reopen", "resume"), &context),
              "actual reopened resume");
      require(!station.admit(event("uncertain"), &context),
              "native reopened unknown identity never re-admits");
      require(station.admit(event("failed-return"), &context),
              "actual independent event accepted");
      station.finish(event("failed-return"), false, &context);
      require(station.paused(),
              "actual unsuccessful return remains unknown and pauses");
      remember_facts(*root, facts);
    }
  }
  const auto all = preserved(path);
  unsigned true_results = 0, false_results = 0, read_packets = 0, prompts = 0,
           views = 0;
  for (const auto &[key, record] : all) {
    ByteView body = record.bytes;
    if (record.site == 4606) {
      const auto request = reference(body);
      const auto found = all.find(request);
      require(found != all.end() && found->second.site == 4600,
              "actual native result links real request");
      if (native<bool>(body))
        ++true_results;
      else
        ++false_results;
    }
    if (record.site == 4610) {
      const auto owner = reference(body);
      require(all.at(owner).site == 4611,
              "actual replay read belongs native decoder request");
      const auto application = native<IdentityBytes>(body);
      ByteView replay_request = all.at(owner).bytes;
      require(all.at(reference(replay_request)).site == 4600 &&
                  native<IdentityBytes>(replay_request) == application &&
                  replay_request.empty(),
              "actual replay request names same retained application identity");
      const auto journal = native<IdentityBytes>(body);
      const auto sequence = native<std::uint64_t>(body);
      const auto packet = literal(body);
      const auto &expected = facts.at(application);
      require(
          journal == expected.journal && sequence == expected.sequence &&
              packet.size() == expected.bytes.size() &&
              std::memcmp(packet.data(), expected.bytes.data(), packet.size()) == 0 &&
              body.empty(),
          "native replay original/physical identity equals independent retained fact");
      ++read_packets;
    }
    if (record.site == 4607 && key == Key{prompt_source.lane, prompt_source.ordinal}) {
      (void)reference(body);
      require(literal(body).ends_with("native operator direction") && body.empty(),
              "actual prompt source outref names whole preserved original");
      ++prompts;
    }
    if (record.site == 4613 &&
        reference(body) == Key{view_owner.lane, view_owner.ordinal}) {
      require(native<std::size_t>(body) == 1 && body.empty(),
              "moved native view retains truthful final row count");
      ++views;
    }
    if (record.site == 4615) {
      const auto owner = reference(body);
      const auto source = reference(body), event_id = reference(body);
      require(all.at(owner).site == 4600 && all.at(source).site == 4602 &&
                  all.at(event_id).site == 4602,
              "unknown comparisons identify real native row key sources");
    }
  }
  struct Expected {
    std::uint8_t method, stage;
    bool paused;
    std::size_t controls, events;
  };
  constexpr std::array expected{
      Expected{1, 6, false, 0, 0}, Expected{2, 7, false, 1, 0},
      Expected{3, 6, false, 1, 1}, Expected{4, 5, false, 1, 1},
      Expected{3, 3, false, 1, 1}, Expected{6, 4, false, 1, 1},
      Expected{7, 3, false, 1, 1}, Expected{2, 7, true, 2, 1},
      Expected{3, 2, true, 2, 1},  Expected{2, 7, false, 3, 1},
      Expected{3, 6, false, 3, 2}, Expected{1, 6, true, 3, 2},
      Expected{2, 7, false, 4, 2}, Expected{3, 3, false, 4, 2},
      Expected{3, 6, false, 4, 3}, Expected{4, 6, true, 4, 3}};
  std::vector<Key> requests;
  for (const auto &[key, row] : all)
    if (row.site == 4600)
      requests.push_back(key);
  require(requests.size() == expected.size(), "literal whole native method count");
  for (std::size_t index = 0; index < requests.size(); ++index) {
    const auto owner = requests[index];
    ByteView request = all.at(owner).bytes;
    require(all.at(reference(request)).site == 7999,
            "actual whole native method names real supplied owner source");
    require(native<std::uint8_t>(request) == expected[index].method,
            "literal native request method order");
    require(native<bool>(request) == (expected[index].method == 1
                                          ? false
                                          : expected[index ? index - 1 : 0].paused),
            "literal native entry paused flag");
    require(!native<bool>(request), "no supplied source asks station to stop");
    if (expected[index].method == 4)
      require(native<bool>(request) == (index == 3), "actual supplied workflow return");
    require(request.empty(), "exact native request fields");
    unsigned terminals = 0, states = 0;
    std::vector<std::pair<std::uint8_t, bool>> decisions;
    for (const auto &[key, row] : all) {
      (void)key;
      if (row.site != 4601 && row.site != 4603 && row.site != 4605)
        continue;
      ByteView body = row.bytes;
      if (reference(body) != owner)
        continue;
      if (row.site == 4601) {
        require(native<std::uint8_t>(body) ==
                        static_cast<std::uint8_t>(RawOperationBranch::returned) &&
                    native<std::uint8_t>(body) == expected[index].stage &&
                    !native<bool>(body) && body.empty(),
                "exact single returned method terminal/stage/no Error");
        ++terminals;
      } else if (row.site == 4605) {
        const auto mutation = native<std::uint8_t>(body);
        const auto expected_mutation = expected[index].method == 1 ? 9
                                       : expected[index].method == 2
                                           ? 1
                                           : expected[index].method;
        require(mutation == expected_mutation &&
                    native<bool>(body) == expected[index].paused &&
                    !native<bool>(body) &&
                    native<std::size_t>(body) == expected[index].controls &&
                    native<std::size_t>(body) == expected[index].events && body.empty(),
                "actual final mutation flags/counts equal literal operation sequence");
        ++states;
      } else {
        const auto decision = native<std::uint8_t>(body);
        const auto actual = native<bool>(body);
        const auto count = native<std::size_t>(body);
        const auto &before = expected[index ? index - 1 : 0];
        require(
            count == (expected[index].method == 2 ? before.controls : before.events) &&
                body.empty(),
            "literal native predicate count and extent");
        decisions.emplace_back(decision, actual);
      }
    }
    require(
        terminals == 1 &&
            states ==
                (expected[index].method == 6 || expected[index].method == 7 ? 0U : 1U),
        "one actual end/state, no double snapshot");
    std::vector<std::pair<std::uint8_t, bool>> predicates;
    if (expected[index].method == 2)
      predicates = {{1, false}, {2, false}};
    else if (expected[index].method == 3) {
      predicates.emplace_back(3, index == 8);
      if (index != 8) {
        const bool duplicate = index == 4 || index == 13;
        predicates.emplace_back(1, duplicate);
        if (!duplicate)
          predicates.emplace_back(2, false);
      }
    } else if (expected[index].method == 4)
      predicates = {{4, false}};
    require(decisions == predicates,
            "literal native predicate results and short circuits");
  }
  require(true_results == 7 && false_results == 3 && read_packets >= 4 &&
              prompts == 1 && views == 1,
          "native station/reopen success and refusal surfaces preserved");
  std::filesystem::remove_all(path);
}
void native_failure_cuts() {
  char temporary[] = "/tmp/blackbird-station-cuts-XXXXXX";
  require(::mkdtemp(temporary), "private actual station cuts");
  const std::string path{temporary};
  {
    auto runtime = unwrap(
        RawRuntime::open(path, id<EnvironmentId>(83),
                         {{64 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    const char marker = 'F';
    auto bank = unwrap(raw_claim_control(17));
    const auto ordinal = unwrap(
        raw_emit(7999, std::array{std::as_bytes(std::span{&marker, std::size_t{1}})},
                 RawPartition::control, &bank));
    RawAdapterContext context;
    context.parent = RawEventRef{raw_lane_epoch(), ordinal};
    auto root = store(path);
    AuditLog log{*root};
    StationStore station{log, &context};
    auto command = control("actual-allocation-control", "pause");
    cuts::root = root.get();
    cuts::before = root->tail_facts().size();
    cuts::mode = 1;
    cuts::hit = false;
    bool failed = false;
    RawEventRef absent{};
    try {
      (void)station.control(command, &context, &absent);
    } catch (const std::bad_alloc &) {
      failed = true;
    }
    cuts::mode = 0;
    cuts::root = nullptr;
    require(failed && cuts::hit && !absent.ordinal && !station.paused(),
            "actual set allocation fails after record without accepted return or "
            "applied pause");
    bool recorded = false;
    for (const auto &fact : root->tail_facts())
      if (const auto *actual = std::get_if<ApplicationRecordEvent>(&fact.event.body))
        if (actual->channel == ApplicationChannel::program) {
          const auto value = unwrap(read_packet(actual->payload));
          if (string_field(value, "label") == "station.control" &&
              string_field(field(value, "command"), "id") ==
                  "actual-allocation-control")
            recorded = true;
        }
    require(recorded,
            "actual successful semantic record survives native set allocation failure");
    const auto input = event("partial-prompt");
    cuts::formatter_allocations = 0;
    cuts::count_formatter = true;
    const auto formatted = format_value(input);
    cuts::count_formatter = false;
    require(formatted.has_value(), "native formatter allocation count completes");
    cuts::skip = cuts::formatter_allocations;
    cuts::mode = 2;
    cuts::hit = false;
    failed = false;
    try {
      (void)station.prompt(input, &context, &absent);
    } catch (const std::bad_alloc &) {
      failed = true;
    }
    cuts::mode = 0;
    require(failed && cuts::hit && !absent.ordinal,
            "actual prompt concatenate allocation fails after formatter with no "
            "whole-prompt ref");
    auto malformed = Value::object({{"id", Value{"missing-action"}}});
    failed = false;
    try {
      (void)station.control(malformed, &context);
    } catch (const Error &error) {
      failed = error.code == ErrorCode::corrupt;
    }
    require(failed, "actual absent action remains corrupt");
    auto oversized = control("oversized-steer", "steer");
    oversized.object().emplace_back("text", Value{std::string(32769, 's')});
    failed = false;
    try {
      (void)station.control(oversized, &context);
    } catch (const Error &error) {
      failed = error.code == ErrorCode::invalid_range;
    }
    require(failed, "actual oversized steer remains invalid without clipping");
  }
  const auto all = preserved(path);
  Key failed_control{}, failed_prompt{};
  bool oversized = false, absent = false, recorded = false, partial = false,
       allocation = false, formatted = false, whole = false;
  const std::string expected =
      "{[\"source\"] = \"repo\", [\"id\"] = \"partial-prompt\", [\"cursor\"] = "
      "\"commit-42\", [\"prompt\"] = \"inspect actual source\"}";
  for (const auto &[key, record] : all) {
    (void)key;
    ByteView body = record.bytes;
    if (record.site == 4602) {
      const auto owner = reference(body);
      const auto field = native<std::uint8_t>(body);
      const auto text = literal(body);
      if (field == 1 && text == "actual-allocation-control")
        failed_control = owner;
      if (field == 5 && text == "partial-prompt")
        failed_prompt = owner;
      if (field == 3 && text == std::string(32769, 's'))
        oversized = true;
    }
    if (record.site == 4609) {
      (void)reference(body);
      const auto field = native<std::uint8_t>(body);
      const auto present = native<bool>(body);
      if (field == 2 && !present) {
        require(body.empty(), "absent action has no fabricated variant index");
        absent = true;
      }
    }
  }
  require(failed_control.second && failed_prompt.second,
          "failed native input owners remain acquired");
  for (const auto &[key, record] : all) {
    (void)key;
    ByteView body = record.bytes;
    if (record.site == 4604 && reference(body) == failed_control) {
      require(literal(body).size() == 32,
              "actual acknowledged record return preserved");
      recorded = true;
    }
    if (record.site == 4605 && reference(body) == failed_control) {
      require(native<std::uint8_t>(body) == 1 && !native<bool>(body) &&
                  !native<bool>(body) && native<std::size_t>(body) == 0 &&
                  native<std::size_t>(body) == 0 && body.empty(),
              "partial set failure reports actual native flags and both counts");
      partial = true;
    }
    if (record.site == 4601) {
      const auto owner = reference(body);
      if (owner == failed_control || owner == failed_prompt) {
        require(native<std::uint8_t>(body) ==
                    static_cast<std::uint8_t>(RawOperationBranch::allocation),
                "native allocation branch is actual class");
        require(native<std::uint8_t>(body) == (owner == failed_control ? 6 : 3) &&
                    !native<bool>(body) && body.empty(),
                "literal allocation failure stage/no Error/trailing fields");
        allocation = true;
      }
    }
    if (record.site == 4614 && reference(body) == failed_prompt) {
      require(literal(body) == expected && body.empty(),
              "real formatter original survives later concatenation failure");
      formatted = true;
    }
    if (record.site == 4607 && reference(body) == failed_prompt)
      whole = true;
    if (record.site == 4606)
      require(reference(body) != failed_control,
              "failed control emits no accepted native bool return");
  }
  require(
      recorded && partial && allocation && formatted && !whole && oversized && absent,
      "actual allocator/invalid-field originals and partial outcomes are preserved");
  std::filesystem::remove_all(path);
}
void native_bound_refusal() {
  char temporary[] = "/tmp/blackbird-station-bound-XXXXXX";
  require(::mkdtemp(temporary), "private native prompt bound refusal");
  const std::string path{temporary};
  {
    auto runtime =
        unwrap(RawRuntime::open(path, id<EnvironmentId>(83),
                                {{1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    const char marker = 'B';
    auto bank = unwrap(raw_claim_control(17));
    const auto ordinal = unwrap(
        raw_emit(7999, std::array{std::as_bytes(std::span{&marker, std::size_t{1}})},
                 RawPartition::control, &bank));
    RawAdapterContext context;
    context.parent = RawEventRef{raw_lane_epoch(), ordinal};
    auto root = store(path);
    AuditLog log{*root};
    StationStore station{log, &context};
    RawCaptureFailureScope failure{true};
    bool refused = false;
    try {
      (void)station.prompt(event("bound-refusal"), &context);
    } catch (const Error &error) {
      refused = error.code == ErrorCode::capacity;
    }
    require(refused && failure.error() && failure.error()->code == ErrorCode::capacity,
            "actual native DATA source bound refuses before formatter and transfers "
            "failure origin");
  }
  bool ended = false;
  for (const auto &[key, record] : preserved(path)) {
    (void)key;
    ByteView body = record.bytes;
    require(record.site != 4614 && record.site != 4607,
            "native bound refusal creates no formatter/prompt return");
    if (record.site == 4601) {
      (void)reference(body);
      const auto branch = native<std::uint8_t>(body);
      (void)native<std::uint8_t>(body);
      const auto error = native<bool>(body);
      if (branch == static_cast<std::uint8_t>(RawOperationBranch::error) && error &&
          native<ErrorCode>(body) == ErrorCode::capacity)
        ended = true;
    }
  }
  require(ended, "native prompt source refusal has actual paid terminal");
  std::filesystem::remove_all(path);
}

void native_post_request_refusal() {
  for (unsigned method = 0; method < 3; ++method) {
    char temporary[] = "/tmp/blackbird-station-post-grant-XXXXXX";
    require(::mkdtemp(temporary), "private actual post-request method refusal");
    const std::string path{temporary};
    auto root = store(path);
    AuditLog log{*root};
    StationStore station{log};
    IdentityBytes lane{};
    lane[0] = std::byte{static_cast<unsigned char>(91 + method)};
    const auto name =
        "/bb-station-post-" + std::to_string(::getpid()) + "-" + std::to_string(method);
    auto channel = unwrap(RawChannel::create(name, lane, {1024 * 1024, 95, 4096}));
    RawProducer producer{channel};
    RawEventRef actual{lane, 99};
    {
      RawThreadBinding binding{producer, lane};
      RawAdapterContext context;
      const char marker = 'G';
      const auto parent = unwrap(
          raw_emit(7999, std::array{std::as_bytes(std::span{&marker, std::size_t{1}})},
                   RawPartition::control));
      context.parent = RawEventRef{lane, parent};
      unwrap(channel.acknowledge_quiescent(
          RawPartition::control,
          unwrap(channel.cursors(RawPartition::control)).published));
      RawCaptureFailureScope failure{true};
      bool refused = false;
      const auto before = root->tail_facts().size();
      try {
        if (method == 0) {
          StationStore reopened{log, &context};
        } else if (method == 1)
          (void)station.control(control("never-acquired", "pause"), &context, &actual);
        else
          (void)station.view(&context, &actual);
      } catch (const Error &error) {
        refused = error == Error{ErrorCode::capacity};
      }
      require(refused && failure.error() == Error{ErrorCode::capacity} &&
                  root->tail_facts().size() == before && !station.paused() &&
                  (method == 0 || !actual.ordinal),
              "actual post-request grant refuses before mutation/record/return");
    }
    auto bytes = unwrap(channel.read(RawPartition::control, 17, 95)).bytes;
    ByteView body = bytes;
    require(native<std::uint32_t>(body) == 43 && native<std::uint32_t>(body) == 4600,
            "actual base95 bank preserved native request");
    const auto ordinal = native<std::uint64_t>(body);
    (void)reference(body);
    require(native<std::uint8_t>(body) == (method == 0   ? 1
                                           : method == 1 ? 2
                                                         : 7) &&
                !native<bool>(body) && !native<bool>(body),
            "actual supplied method flags");
    require(native<std::uint32_t>(body) == 52 && native<std::uint32_t>(body) == 4601,
            "only one paid actual terminal, no state/effect");
    (void)native<std::uint64_t>(body);
    require(
        reference(body) == Key{lane, ordinal} &&
            native<std::uint8_t>(body) ==
                static_cast<std::uint8_t>(RawOperationBranch::error) &&
            native<std::uint8_t>(body) == 0 && native<bool>(body) &&
            native<ErrorCode>(body) == ErrorCode::capacity &&
            native<std::int64_t>(body) == 0 && body.empty(),
        "post-request Error is actual capacity/stage0, never destructor unfinished");
    unwrap(RawChannel::unlink(name));
    std::filesystem::remove_all(path);
  }
}

} // namespace
int main() {
  try {
    native_store();
    native_failure_cuts();
    native_bound_refusal();
    native_post_request_refusal();
    std::cout << "native StationStore source-to-disk checks passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << " " << error.detail << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
