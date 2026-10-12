#include "blackbird/coding.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_runtime.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <new>
#include <set>
#include <source_location>
#include <stdexcept>
#include <thread>
#include <unistd.h>

static std::size_t allocations;
void *operator new(std::size_t n) {
  ++allocations;
  if (void *p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc{};
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
using namespace blackbird;
namespace {
void check(bool condition, const char *message,
           std::source_location location = std::source_location::current()) {
  if (!condition)
    throw std::runtime_error(std::string{message} + " at " +
                             std::to_string(location.line()));
}
template <class T> T take(Result<T> result) {
  if (!result.has_value())
    throw std::runtime_error("raw test setup failed: " +
                             std::to_string(static_cast<int>(result.error().code)));
  return std::move(result).value();
}
void take(Result<void> result) { check(result.has_value(), "raw setup failed"); }
IdentityBytes identity(unsigned char seed) {
  IdentityBytes result{};
  result[0] = std::byte{seed};
  return result;
}
template <class T> T id(unsigned char seed) {
  return take(T::from_bytes(identity(seed)));
}
struct Lane {
  std::string name;
  IdentityBytes epoch;
  RawChannel channel;
  RawProducer producer;
  static std::string next_name() {
    static unsigned serial = 0;
    return "/bbregion" + std::to_string(::getpid()) + "x" + std::to_string(++serial);
  }
  explicit Lane(RawCapacities capacity = {16384, 16384, 4096}, unsigned char seed = 71)
      : name(next_name()), epoch(identity(seed)),
        channel(take(RawChannel::create(name, epoch, capacity))), producer(channel) {}
  ~Lane() { (void)RawChannel::unlink(name); }
  std::vector<std::byte> bytes(RawPartition partition) {
    return take(channel.read(
                    partition, 0,
                    static_cast<std::size_t>(
                        channel.capacities()[static_cast<std::size_t>(partition) - 1])))
        .bytes;
  }
};
using Bytes = std::vector<std::byte>;
// The oracle follows the documented native ABI field order independently. It
// never calls the production clock-field or raw-envelope assembly helpers.
template <class T> void append(Bytes &bytes, const T &field) {
  const auto *begin = reinterpret_cast<const std::byte *>(&field);
  bytes.insert(bytes.end(), begin, begin + sizeof(T));
}
void append_bytes(Bytes &bytes, ByteView field) {
  bytes.insert(bytes.end(), field.begin(), field.end());
}
ByteView text_bytes(std::string_view text) {
  return std::as_bytes(std::span{text.data(), text.size()});
}
struct EventIdentity {
  std::uint32_t site;
  std::uint64_t ordinal;
};
Bytes envelope(EventIdentity identity, ByteView body) {
  Bytes result;
  append(result, static_cast<std::uint32_t>(16 + body.size()));
  append(result, identity.site);
  append(result, identity.ordinal);
  append_bytes(result, body);
  return result;
}
void join(Bytes &bytes, const Bytes &record) { append_bytes(bytes, record); }
unsigned clock_calls;
bool last_cpu;
RawClockSample first_clock() {
  return {{CLOCK_MONOTONIC, 0, 49, {9, 900000001}},
          {CLOCK_THREAD_CPUTIME_ID, -1, 22, {-2, 3}},
          true};
}
RawClockSample second_clock() {
  // Deliberately regressed wall seconds and strange CPU values are preserved.
  return {{CLOCK_MONOTONIC, -1, 5, {8, 100000002}},
          {CLOCK_THREAD_CPUTIME_ID, 0, 73, {-4, 900000005}},
          true};
}
RawClockSample fixture_clock(bool cpu) noexcept {
  errno = ERANGE;
  last_cpu = cpu;
  auto sample = ++clock_calls == 1 ? first_clock() : second_clock();
  sample.cpu_read = cpu;
  if (!cpu)
    sample.thread_cpu = {};
  return sample;
}
void append_clock(Bytes &body, const RawClockSample &sample) {
  append(body, sample.monotonic.method);
  append(body, sample.monotonic.result);
  append(body, sample.monotonic.error);
  append(body, sample.monotonic.value.tv_sec);
  append(body, sample.monotonic.value.tv_nsec);
  append(body, sample.cpu_read);
  append(body, sample.thread_cpu.method);
  append(body, sample.thread_cpu.result);
  append(body, sample.thread_cpu.error);
  append(body, sample.thread_cpu.value.tv_sec);
  append(body, sample.thread_cpu.value.tv_nsec);
}
Bytes begin_body(RawClockSample sample, int exceptions = 0) {
  Bytes body;
  append_clock(body, sample);
  append(body, exceptions);
  return body;
}
Bytes end_body(std::uint64_t begin, RawClockSample sample, RawRegionEnd reason,
               int exceptions = 0) {
  Bytes body;
  append(body, begin);
  append_clock(body, sample);
  append(body, exceptions);
  append(body, reason);
  return body;
}
void disabled_has_no_clock_or_allocation() {
  clock_calls = 0;
  const auto before = allocations;
  {
    RawRegion region{raw_regions::provider_prepare};
    region.observe({RawObservationKind::request_bytes, 3, 7, 11});
    region.linkage({{}, {}});
    region.outcome(RawRegionOutcome::returned);
    region.error({ErrorCode::io, -81});
    check(!region.enabled() && region.begin_ordinal() == 0,
          "disabled region acquired admission");
  }
  check(clock_calls == 0 && allocations == before,
        "disabled region acquired clock/storage");
  check(raw_worker_dispatch().ordinal == 0, "disabled dispatch published");
  raw_worker_started({identity(72), 7});
}
void exact_region_records() {
  Lane lane;
  RawThreadBinding binding{lane.producer, lane.epoch};
  clock_calls = 0;
  const std::string revision(2049, 'R');
  const std::string attempt{"attempt\0verbatim", 16};
  const auto before = allocations;
  {
    RawRegion region{raw_regions::provider_prepare};
    region.observe({RawObservationKind::request_bytes, 7, 19, 3});
    region.linkage({text_bytes(revision), text_bytes(attempt)});
    region.outcome(RawRegionOutcome::returned);
    region.error({ErrorCode::provider_transport, -92});
    region.finish();
    region.finish();
  }
  check(allocations == before, "admitted region allocated");
  check(clock_calls == 2, "finish acquired multiple clock samples");
  Bytes expected_data = envelope({1070, 1}, begin_body(first_clock()));
  Bytes observation;
  append(observation, std::uint64_t{1});
  append(observation, RawObservationKind::request_bytes);
  append(observation, std::size_t{7});
  append(observation, std::size_t{19});
  append(observation, std::uint64_t{3});
  join(expected_data, envelope({1072, 2}, observation));
  Bytes link;
  append(link, std::uint64_t{1});
  append(link, revision.size());
  append_bytes(link, text_bytes(revision));
  append(link, attempt.size());
  append_bytes(link, text_bytes(attempt));
  join(expected_data, envelope({1073, 3}, link));
  check(lane.bytes(RawPartition::data) == expected_data,
        "begin/observation/linkage bytes changed or truncated");
  Bytes outcome;
  append(outcome, std::uint64_t{1});
  append(outcome, RawRegionOutcome::returned);
  Bytes expected_control = envelope({1074, 4}, outcome);
  Bytes error;
  append(error, std::uint64_t{1});
  append(error, ErrorCode::provider_transport);
  append(error, std::int64_t{-92});
  join(expected_control, envelope({1110, 5}, error));
  join(expected_control,
       envelope({1071, 6}, end_body(1, second_clock(), RawRegionEnd::explicit_finish)));
  check(lane.bytes(RawPartition::control) == expected_control,
        "terminal facts/clock returns were changed, aggregated, or duplicated");
  check(lane.bytes(RawPartition::failure).empty(), "successful region fenced");
}
void terminal_credit_survives_data_full() {
  Lane lane{{256, 160, 256}};
  RawThreadBinding binding{lane.producer, lane.epoch};
  clock_calls = 0;
  RawRegion region{raw_regions::command_perform};
  const auto available = take(lane.producer.available(RawPartition::data));
  Bytes filler(static_cast<std::size_t>(available) - 16, std::byte{0xCC});
  take(lane.producer.reserve(RawPartition::data, available));
  const std::array<ByteView, 1> parts{filler};
  check(take(lane.producer.emit(17, parts)) == 2, "fill ordinal wrong");
  check(take(lane.producer.available(RawPartition::data)) == 0,
        "ordinary data lane not full");
  check(!lane.producer.reserve(RawPartition::control, 32).has_value(),
        "ordinary control stole held terminal capacity");
  region.outcome(RawRegionOutcome::returned);
  region.finish();
  Bytes outcome;
  append(outcome, std::uint64_t{1});
  append(outcome, RawRegionOutcome::returned);
  Bytes expected = envelope({1034, 3}, outcome);
  join(expected,
       envelope({1031, 4}, end_body(1, second_clock(), RawRegionEnd::explicit_finish)));
  check(lane.bytes(RawPartition::control) == expected,
        "data fullness dropped admitted terminal records");
  check(lane.bytes(RawPartition::failure).empty(), "normal fullness fenced end");
}
void scope_exit_is_not_success() {
  Lane lane;
  RawThreadBinding binding{lane.producer, lane.epoch};
  clock_calls = 0;
  try {
    RawRegion region{raw_regions::command_perform, {.cpu = false}};
    throw Error{ErrorCode::interrupted, 29};
  } catch (const Error &error) {
    check(error == Error{ErrorCode::interrupted, 29}, "hook changed owner exception");
  }
  auto first = first_clock();
  auto second = second_clock();
  first.cpu_read = second.cpu_read = false;
  first.thread_cpu = second.thread_cpu = {};
  check(clock_calls == 2 && !last_cpu, "disabled CPU clock requested");
  check(lane.bytes(RawPartition::data) == envelope({1030, 1}, begin_body(first)),
        "CPU-absent begin changed");
  check(lane.bytes(RawPartition::control) ==
            envelope({1031, 2}, end_body(1, second, RawRegionEnd::scope_exit, 1)),
        "scope exit fabricated success or lost uncaught exception fact");
}
void claim_failure_precedes_begin_and_effect() {
  Lane lane{{512, 32, 512}};
  RawThreadBinding binding{lane.producer, lane.epoch};
  clock_calls = 0;
  bool effect = false;
  bool refused = false;
  try {
    RawRegion region{raw_regions::provider_transport};
    effect = true;
  } catch (const Error &error) {
    refused = error.code == ErrorCode::capacity;
  }
  check(refused && !effect && clock_calls == 0,
        "terminal credit failure followed clock acquisition/effect");
  check(lane.bytes(RawPartition::data).empty(), "claim failure published begin");
  check(take(lane.producer.available(RawPartition::control)) == 32,
        "failed constructor leaked control credit");
}
void data_admission_precedes_clock_and_effect() {
  Lane lane{{32, 256, 256}};
  RawThreadBinding binding{lane.producer, lane.epoch};
  clock_calls = 0;
  errno = EDOM;
  bool effect = false;
  bool refused = false;
  Error original{ErrorCode::corrupt};
  try {
    RawRegion region{raw_regions::provider_transport};
    effect = true;
  } catch (const Error &error) {
    original = error;
    refused = error.code == ErrorCode::capacity;
  }
  check(refused && !effect && clock_calls == 0,
        "data capacity failure followed clock acquisition/effect");
  check(errno == EDOM, "data admission failure altered product errno");
  check(lane.bytes(RawPartition::data).empty(), "data failure published begin");
  check(take(lane.producer.available(RawPartition::control)) == 256,
        "data failure leaked constructor terminal credit");
  Bytes failure;
  append(failure, original.code);
  append(failure, original.detail);
  check(lane.bytes(RawPartition::failure) == envelope({8001, 1}, failure),
        "pre-clock admission failure was not independently retained");
}
void closing_epoch_refuses_before_worker_clock() {
  Lane lane{{512, 512, 512}};
  constexpr auto first = UINT64_MAX - 96;
  // The lane fixture's default producer remains unused; this is the sole owner.
  RawProducer closing{lane.channel, RawProducer::Start{-1, first}};
  RawThreadBinding binding{closing, lane.epoch};
  take(closing.reserve(RawPartition::data, raw_envelope_size));
  check(take(closing.emit(501, {})) == first, "closing fixture admission failed");
  clock_calls = 0;
  bool refused = false;
  try {
    (void)raw_worker_dispatch();
  } catch (const Error &error) {
    refused = error.code == ErrorCode::overflow;
  }
  check(refused && clock_calls == 0, "epoch closure followed worker clock acquisition");
  check(lane.bytes(RawPartition::data) == envelope({501, first}, {}),
        "closing acquisition published another event");
  Bytes failure;
  append(failure, ErrorCode::overflow);
  append(failure, std::int64_t{0});
  check(lane.bytes(RawPartition::failure) == envelope({8001, first + 1}, failure),
        "epoch closure lost prepaid failure event");
}
void variable_linkage_admission_keeps_terminal_custody() {
  Lane lane{{256, 256, 256}};
  RawThreadBinding binding{lane.producer, lane.epoch};
  clock_calls = 0;
  errno = EDOM;
  RawRegion region{raw_regions::provider_prepare};
  check(errno == EDOM, "begin clock sampling altered product errno");
  const std::string revision(257, 'X');
  bool refused = false;
  try {
    region.linkage({text_bytes(revision), {}});
  } catch (const Error &error) {
    refused = error.code == ErrorCode::capacity;
    region.error(error);
    region.outcome(RawRegionOutcome::rejected);
  }
  region.finish();
  check(refused && clock_calls == 2 && errno == EDOM,
        "variable admission failure lost clock/errno/terminal behavior");
  check(lane.bytes(RawPartition::data) ==
            envelope({1070, 1}, begin_body(first_clock())),
        "oversized linkage published a partial data record");
  const auto expected_end =
      envelope({1071, 5}, end_body(1, second_clock(), RawRegionEnd::explicit_finish));
  const auto control = lane.bytes(RawPartition::control);
  check(
      control.size() >= expected_end.size() &&
          std::equal(expected_end.begin(), expected_end.end(),
                     control.end() - static_cast<std::ptrdiff_t>(expected_end.size())),
      "failed linkage admission abandoned prepaid end");
  check(!lane.bytes(RawPartition::failure).empty(),
        "failed variable data admission lacked failure custody");
}
void admitted_terminals_survive_ingest_fence() {
  Lane lane{{256, 256, 256}};
  RawThreadBinding binding{lane.producer, lane.epoch};
  clock_calls = 0;
  RawRegion region{raw_regions::command_perform};
  const auto available = take(lane.producer.available(RawPartition::data));
  Bytes filler(static_cast<std::size_t>(available) - 16, std::byte{0xAB});
  take(lane.producer.reserve(RawPartition::data, available));
  const std::array<ByteView, 1> parts{filler};
  (void)take(lane.producer.emit(17, parts));
  Error original{ErrorCode::corrupt};
  bool refused = false;
  try {
    region.observe({RawObservationKind::request_bytes, 0, 8192, 3});
  } catch (const Error &error) {
    original = error;
    refused = error.code == ErrorCode::capacity;
  }
  check(refused, "full ordinary observation was not refused");
  region.error(original);
  region.outcome(RawRegionOutcome::rejected);
  region.finish();
  Bytes failure;
  append(failure, original.code);
  append(failure, original.detail);
  check(lane.bytes(RawPartition::failure) == envelope({8001, 3}, failure),
        "ingest failure lost original error or independent partition");
  Bytes error;
  append(error, std::uint64_t{1});
  append(error, original.code);
  append(error, original.detail);
  Bytes expected = envelope({1110, 4}, error);
  Bytes outcome;
  append(outcome, std::uint64_t{1});
  append(outcome, RawRegionOutcome::rejected);
  join(expected, envelope({1034, 5}, outcome));
  join(expected,
       envelope({1031, 6}, end_body(1, second_clock(), RawRegionEnd::explicit_finish)));
  check(lane.bytes(RawPartition::control) == expected,
        "fence abandoned previously admitted terminal custody");
}
struct Record {
  std::uint32_t site;
  std::uint64_t ordinal;
  Bytes body;
};
template <class T> T read(ByteView bytes, std::size_t &offset) {
  check(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset,
        "record too short");
  T value;
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  offset += sizeof(T);
  return value;
}
std::vector<Record> records(Lane &lane) {
  std::vector<Record> result;
  for (const auto partition :
       {RawPartition::data, RawPartition::control, RawPartition::failure}) {
    const auto bytes = lane.bytes(partition);
    std::size_t offset = 0;
    while (offset != bytes.size()) {
      const auto start = offset;
      const auto size = read<std::uint32_t>(bytes, offset);
      const auto site = read<std::uint32_t>(bytes, offset);
      const auto ordinal = read<std::uint64_t>(bytes, offset);
      check(size >= 16 && size <= bytes.size() - start, "invalid envelope");
      const ByteView body{bytes.data() + offset, size - 16};
      result.push_back({site, ordinal, {body.begin(), body.end()}});
      offset = start + size;
    }
  }
  std::sort(result.begin(), result.end(),
            [](const Record &a, const Record &b) { return a.ordinal < b.ordinal; });
  return result;
}
void worker_link_has_independent_owner_lane() {
  Lane ui{{4096, 4096, 4096}, 72};
  Lane worker{{4096, 4096, 4096}, 73};
  RawWorkerDispatch dispatch{};
  {
    RawThreadBinding binding{ui.producer, ui.epoch};
    dispatch = raw_worker_dispatch();
  }
  std::thread thread{[&] {
    RawThreadBinding binding{worker.producer, worker.epoch};
    raw_worker_started(dispatch);
  }};
  thread.join();
  const auto source = records(ui);
  const auto destination = records(worker);
  check(source.size() == 1 && source[0].site == 1100 && source[0].ordinal == 1,
        "worker dispatch was not UI-owned");
  check(destination.size() == 1 && destination[0].site == 1101 &&
            destination[0].ordinal == 1,
        "worker start lacked independent ordinal");
  Bytes prefix;
  append_bytes(prefix, ui.epoch);
  append(prefix, source[0].ordinal);
  check(destination[0].body.size() > prefix.size() &&
            std::equal(prefix.begin(), prefix.end(), destination[0].body.begin()),
        "worker transfer lost original lane/ordinal reference");
  check(!raw_enabled(), "worker binding leaked into calling context");
}
class Provider final : public CodingProvider {
public:
  enum class Mode { success, failure, invalid_response, retry };
  Mode mode;
  unsigned calls = 0;
  Value observed;
  explicit Provider(Mode selected) : mode(selected) {}
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    observed = request;
    ++calls;
    if (mode == Mode::failure || (mode == Mode::retry && calls == 1)) {
      capture("data: {\"type\":\"response.output_text.delta\","
              "\"item_id\":\"partial\",\"delta\":\"unused\"}\n\n");
      throw Error{ErrorCode::provider_transport, 92};
    }
    if (mode == Mode::invalid_response)
      return Value::object({{"output", Value{Number{7}}}});
    return Value::object({{"output", Value{Value::Array{}}}});
  }
};
struct Directory {
  std::string path;
  Directory() {
    static unsigned serial = 0;
    path = "/tmp/bb-region-provider-" + std::to_string(::getpid()) + "-" +
           std::to_string(++serial);
    check(std::filesystem::create_directory(path), "fixture directory exists");
    std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  }
  ~Directory() { std::filesystem::remove_all(path); }
};
void provider_owner_records(Provider::Mode mode) {
  Directory directory;
  const JournalHeader header{id<EnvironmentId>(31),
                             id<AuditStreamId>(32),
                             3,
                             {4 * 1024 * 1024, 16 * 1024 * 1024},
                             std::nullopt};
  auto root =
      take(RetainedState::create(std::make_unique<NativeJournalDirectory>(take(
                                     NativeJournalDirectory::open(directory.path))),
                                 "audit", header, {64 * 1024 * 1024, 10000}));
  AuditLog log{*root};
  ContextStore context{log};
  Provider provider{mode};
  // Runtime retains the borrowed producer until its VM closes after turn return.
  // The provider's existing bounded codec lease prepays 16MiB before encoding.
  // This borrowed lane has no recorder to reclaim its earlier published prefix.
  Lane lane{{32 * 1024 * 1024, 1024 * 1024, 4096}};
  CodingEngine engine{log, context, provider, "test"};
  bool failed = false;
  {
    RawThreadBinding binding{lane.producer, lane.epoch};
    try {
      engine.turn({"request observation", "arco.request({retry_policy="
                                          "{max_attempts=2,base_ms=0,cap_ms=0}})"});
    } catch (...) {
      failed = true;
    }
  }
  check(failed == (mode == Provider::Mode::failure ||
                   mode == Provider::Mode::invalid_response),
        "hook changed provider/import failure behavior");
  const auto events = records(lane);
  struct Phase {
    const Record *record;
    std::uint8_t kind;
    std::optional<RawEventRef> parent;
    std::optional<std::array<IdentityBytes, 3>> owner;
  };
  const auto read_ref = [](ByteView body, std::size_t &offset) {
    const auto epoch = read<IdentityBytes>(body, offset);
    return RawEventRef{epoch, read<std::uint64_t>(body, offset)};
  };
  const auto same_ref = [](RawEventRef a, RawEventRef b) {
    return a.lane == b.lane && a.ordinal == b.ordinal;
  };
  std::vector<Phase> phases;
  std::vector<const Record *> prepared, dispatch, observed, begin, links, outcomes,
      decisions;
  for (const auto &event : events) {
    if (event.site == 1320) {
      std::size_t offset = 0;
      Phase phase{&event, read<std::uint8_t>(event.body, offset), {}, {}};
      if (read<bool>(event.body, offset))
        phase.parent = read_ref(event.body, offset);
      if (read<bool>(event.body, offset)) {
        std::array<IdentityBytes, 3> owner{};
        for (auto &value : owner)
          value = read<IdentityBytes>(event.body, offset);
        phase.owner = owner;
      }
      check(offset == event.body.size(), "phase native fields changed");
      if (phase.kind == 3)
        dispatch.push_back(&event);
      if (phase.kind == 9 && phase.parent)
        observed.push_back(&event);
      phases.push_back(phase);
    }
    if (event.site == 1322) {
      std::size_t offset = 24;
      if (read<std::uint8_t>(event.body, offset) == 1)
        prepared.push_back(&event);
    }
    if (event.site == 1326)
      decisions.push_back(&event);
    if (event.site == 1080)
      begin.push_back(&event);
    if (event.site == 1083)
      links.push_back(&event);
    if (event.site == 1084)
      outcomes.push_back(&event);
    check(event.site != 1, "owner hook ingestion failed");
  }
  const auto phase_for = [&](const Record *record) -> const Phase & {
    const auto found =
        std::find_if(phases.begin(), phases.end(),
                     [&](const Phase &phase) { return phase.record == record; });
    check(found != phases.end(), "actual phase missing");
    return *found;
  };
  const auto only_kind = [&](std::uint8_t kind) -> const Phase & {
    check(std::count_if(phases.begin(), phases.end(),
                        [&](const Phase &phase) { return phase.kind == kind; }) == 1,
          "assembly/retry group cardinality changed");
    return *std::find_if(phases.begin(), phases.end(),
                         [&](const Phase &phase) { return phase.kind == kind; });
  };
  const auto &assembly = only_kind(1), &group = only_kind(2);
  const RawEventRef assembly_ref{lane.epoch, assembly.record->ordinal};
  const RawEventRef group_ref{lane.epoch, group.record->ordinal};
  check(!assembly.parent && !assembly.owner && group.parent && !group.owner &&
            same_ref(*group.parent, assembly_ref),
        "request/retry group has fabricated owner or parent");
  const unsigned attempts =
      mode == Provider::Mode::retry || mode == Provider::Mode::failure ? 2 : 1;
  check(prepared.size() == 1 && dispatch.size() == attempts &&
            decisions.size() == attempts && begin.size() == attempts &&
            links.size() == attempts && outcomes.size() == attempts &&
            provider.calls == attempts,
        "request/response owner event cardinality");
  check(observed.size() == (mode == Provider::Mode::failure ? 0U : 1U),
        "response observation fabricated/lost");
  std::size_t offset = 0;
  check(same_ref(read_ref(prepared[0]->body, offset), assembly_ref),
        "prepared request lost actual assembly reference");
  check(read<std::uint8_t>(prepared[0]->body, offset) == 1,
        "prepared request role changed");
  const auto request_size = read<std::size_t>(prepared[0]->body, offset);
  const auto expected_request = take(encode_packet_string(provider.observed));
  check(request_size == expected_request.size() &&
            prepared[0]->body.size() == offset + request_size &&
            std::equal(text_bytes(expected_request).begin(),
                       text_bytes(expected_request).end(),
                       prepared[0]->body.begin() + static_cast<std::ptrdiff_t>(offset)),
        "request hook altered product-owned native packet");
  IdentityBytes generation{};
  std::string revision;
  unsigned generations = 0, revisions = 0;
  for (const auto &event : events) {
    if (event.site != 1323)
      continue;
    offset = 0;
    const auto source = read_ref(event.body, offset);
    const auto stage = read<std::uint8_t>(event.body, offset);
    const auto variant = read<std::uint8_t>(event.body, offset);
    if (!same_ref(source, assembly_ref) || stage != 15)
      continue;
    if (variant == 8) {
      generation = read<IdentityBytes>(event.body, offset);
      check(offset == event.body.size(), "generation native fields changed");
      ++generations;
    }
    if (variant == 7) {
      const auto size = read<std::size_t>(event.body, offset);
      check(size <= event.body.size() - offset && offset + size == event.body.size(),
            "revision overflow/native fields changed");
      revision.assign(reinterpret_cast<const char *>(event.body.data() + offset), size);
      ++revisions;
    }
  }
  check(generations == 1 && revisions == 1 && !revision.empty(),
        "request generation/context revision absent or duplicated");
  std::set<IdentityBytes> attempts_seen;
  for (unsigned index = 0; index < attempts; ++index) {
    const auto &phase = phase_for(dispatch[index]);
    check(phase.parent && same_ref(*phase.parent, group_ref) && phase.owner,
          "dispatch lost retry group or actual business owner");
    const auto &owner = *phase.owner;
    const auto &attempt = owner[0], &invocation = owner[1];
    check(owner[2] == generation, "dispatch generation linkage changed");
    check(attempts_seen.insert(attempt).second, "retry reused owner attempt identity");
    const auto admission =
        take(root->attempt(take(OperationAttemptId::from_bytes(attempt))));
    check(admission.admission.invocation.bytes() == invocation,
          "dispatch identity does not reference actual retained attempt");
    Bytes expected_link;
    append(expected_link, begin[index]->ordinal);
    append(expected_link, revision.size());
    append_bytes(expected_link, text_bytes(revision));
    append(expected_link, attempt.size());
    append_bytes(expected_link, attempt);
    check(links[index]->body == expected_link,
          "transport lost actual retained owner/context linkage");
    offset = 0;
    check(same_ref(read_ref(decisions[index]->body, offset), group_ref),
          "retry decision references wrong group");
    check(read<unsigned>(decisions[index]->body, offset) == index + 1,
          "dispatch retry ordinal changed");
    check(same_ref(read_ref(decisions[index]->body, offset),
                   {lane.epoch, dispatch[index]->ordinal}),
          "retry decision references wrong actual attempt phase");
    check(read<unsigned>(decisions[index]->body, offset) == 0,
          "fixture retry delay changed");
    check(read<bool>(decisions[index]->body, offset) == (index + 1 < attempts) &&
              offset == decisions[index]->body.size(),
          "retry eligibility/native fields changed");
    check(prepared[0]->ordinal < dispatch[index]->ordinal,
          "dispatch preceded prepared request");
    offset = 0;
    check(read<std::uint64_t>(outcomes[index]->body, offset) == begin[index]->ordinal,
          "transport outcome references wrong region");
    const auto outcome = read<RawRegionOutcome>(outcomes[index]->body, offset);
    const bool returned = mode != Provider::Mode::failure && index + 1 == attempts;
    check(outcome == (returned ? RawRegionOutcome::returned
                               : RawRegionOutcome::exception) &&
              offset == outcomes[index]->body.size(),
          "transport imported failure or exception as returned");
  }
  if (!observed.empty()) {
    const auto &receipt = phase_for(observed[0]);
    const auto &dispatch_phase = phase_for(dispatch.back());
    check(receipt.owner && receipt.owner == dispatch_phase.owner && receipt.parent &&
              same_ref(*receipt.parent, {lane.epoch, dispatch.back()->ordinal}),
          "response lost actual attempt/invocation/generation parent");
    const auto admission =
        take(root->attempt(take(OperationAttemptId::from_bytes((*receipt.owner)[0]))));
    check((*receipt.owner)[1] == admission.admission.invocation.bytes(),
          "response observation lost retained source reference");
    check(dispatch.back()->ordinal < begin.back()->ordinal &&
              begin.back()->ordinal < observed[0]->ordinal &&
              observed[0]->ordinal < outcomes.back()->ordinal,
          "response observation was deferred until after outcome/import");
  }
  check(lane.bytes(RawPartition::failure).empty(), "actual provider hook fenced");
}
} // namespace
// This test executable supplies a fixed acquisition fixture at link time.
// Production links raw_clock.cpp and exposes no clock callback.
RawClockSample blackbird::raw_local_clock(bool cpu) noexcept {
  return fixture_clock(cpu);
}

int main() {
  try {
    disabled_has_no_clock_or_allocation();
    exact_region_records();
    terminal_credit_survives_data_full();
    scope_exit_is_not_success();
    claim_failure_precedes_begin_and_effect();
    data_admission_precedes_clock_and_effect();
    closing_epoch_refuses_before_worker_clock();
    variable_linkage_admission_keeps_terminal_custody();
    admitted_terminals_survive_ingest_fence();
    worker_link_has_independent_owner_lane();
    for (const auto mode : {Provider::Mode::success, Provider::Mode::failure,
                            Provider::Mode::invalid_response, Provider::Mode::retry})
      provider_owner_records(mode);
    std::cout << "raw region exact/native/provider oracles passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
    return 1;
  }
}
