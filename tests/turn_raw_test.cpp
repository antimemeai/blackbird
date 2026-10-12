#include "blackbird/coding.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <new>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc.h>
#else
#include <fstream>
#endif
using namespace blackbird;
namespace {
struct ClockFact {
  clockid_t method;
  int result, error;
  timespec value;
};
thread_local std::array<ClockFact, 8192> clocks{};
thread_local std::size_t clock_count = 0;
thread_local bool observe_clocks = false, refuse_next = false;
thread_local RetainedState *pin_root = nullptr;
thread_local std::uint64_t pin_sequence = 0;
thread_local bool pin_refused = false;
thread_local RawChannel *child_probe = nullptr;
thread_local std::uint64_t child_probe_start = 0;
thread_local int child_cancel = -1;
thread_local bool watch_child = false, child_cut = false;
thread_local std::size_t child_allocation = 0;
void observe_child_boundary(std::size_t allocation) {
  if (!watch_child)
    return;
  watch_child = false;
  if (!child_probe)
    std::abort();
  auto snapshot =
      child_probe->read(RawPartition::control, child_probe_start, 1024 * 1024);
  if (!snapshot.has_value())
    std::abort();
  ByteView bytes{snapshot.value().bytes};
  while (!bytes.empty()) {
    if (bytes.size() < raw_envelope_size)
      std::abort();
    std::uint32_t length = 0, site = 0;
    std::memcpy(&length, bytes.data(), sizeof(length));
    std::memcpy(&site, bytes.data() + sizeof(length), sizeof(site));
    if (length < raw_envelope_size || length > bytes.size())
      std::abort();
    if (site == 1908) {
      // Select the real business revision allocation, after identity issuance.
      // Allocator capacity rounding differs between the supported profiles.
      std::array<void *, 32> stack{};
      const int count = ::backtrace(stack.data(), static_cast<int>(stack.size()));
      bool revision = false;
      for (int index = 0; index < count; ++index) {
        Dl_info frame{};
        if (::dladdr(stack[static_cast<std::size_t>(index)], &frame) &&
            frame.dli_sname && std::strstr(frame.dli_sname, "hex_identity"))
          revision = true;
      }
      if (!revision) {
        watch_child = true;
        return;
      }
      child_cut = true;
      child_allocation = allocation;
      const char byte = 'C';
      if (::write(child_cancel, &byte, 1) != 1)
        std::abort();
      return;
    }
    bytes = bytes.subspan(length);
  }
  watch_child = true;
}
void require(bool condition, const char *claim) {
  if (!condition)
    throw std::runtime_error{claim};
}
template <class T> T identity(unsigned char value) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(value);
  return unwrap(T::from_bytes(bytes));
}
template <class T> T read(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "short native field");
  T result{};
  std::memcpy(&result, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return result;
}
IdentityBytes id_bytes(ByteView &bytes) {
  require(bytes.size() >= 16, "short native identity");
  IdentityBytes result{};
  std::copy_n(bytes.begin(), 16, result.begin());
  bytes = bytes.subspan(16);
  return result;
}
RawEventRef ref(ByteView &bytes) {
  const auto lane = id_bytes(bytes);
  return {lane, read<std::uint64_t>(bytes)};
}
bool same(RawEventRef a, RawEventRef b) {
  return a.lane == b.lane && a.ordinal == b.ordinal;
}
std::string text(ByteView &bytes) {
  const auto length = read<std::size_t>(bytes);
  require(length <= bytes.size(), "short exact text");
  std::string result{reinterpret_cast<const char *>(bytes.data()), length};
  bytes = bytes.subspan(length);
  return result;
}
template <class T> void append(std::vector<std::byte> &bytes, const T &value) {
  const auto field = std::as_bytes(std::span{&value, std::size_t{1}});
  bytes.insert(bytes.end(), field.begin(), field.end());
}
void append(std::vector<std::byte> &bytes, RawEventRef value) {
  bytes.insert(bytes.end(), value.lane.begin(), value.lane.end());
  append(bytes, value.ordinal);
}
template <class... T> std::vector<std::byte> body(const T &...fields) {
  std::vector<std::byte> result;
  (append(result, fields), ...);
  return result;
}
struct Record {
  RawEventRef reference;
  std::uint32_t site;
  std::vector<std::byte> bytes;
};
const Record &only(const std::vector<Record> &all, std::uint32_t site,
                   std::optional<RawEventRef> parent = {}) {
  const Record *found = nullptr;
  for (const auto &record : all) {
    if (record.site != site)
      continue;
    if (parent) {
      ByteView bytes{record.bytes};
      if (!same(ref(bytes), *parent))
        continue;
    }
    require(!found, "duplicate actual owner event");
    found = &record;
  }
  require(found, "missing actual owner event");
  return *found;
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char name[] = "/tmp/blackbird-turn-native-XXXXXX";
    require(::mkdtemp(name), "private native fixture directory");
    path = name;
  }
  ~Directory() {
    if (std::uncaught_exceptions())
      std::cerr << "failed fixture: " << path << '\n';
    else {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  }
};
struct Unused final : CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw Error{ErrorCode::conflict};
  }
};
struct Fixture {
  Directory directory;
  JournalHeader header{identity<EnvironmentId>(91),
                       identity<AuditStreamId>(92),
                       3,
                       {65536, 4 * 1024 * 1024},
                       {}};
  std::unique_ptr<RetainedState> root = unwrap(
      RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(directory.path.string()))),
                            "audit", header, {64 * 1024 * 1024, 8192}));
  AuditLog log{*root};
  Unused provider;
  RawRuntimeOptions options{{4 * 1024 * 1024, 1024 * 1024, 4096}, 4096};
  RawRuntimeStatus holders{-1, -1, false};
  std::vector<ClockFact> observed;
  template <class F> std::vector<Record> capture(F &&action) {
    {
      auto runtime = unwrap(
          RawRuntime::open(directory.path.string(), header.environment, options));
      auto lane = unwrap(runtime->prepare_thread());
      holders = unwrap(runtime->status());
      {
        RawThreadBinding binding{lane};
        ContextStore context{log};
        CodingEngine engine{log, context, provider, "native.fixture"};
        clock_count = 0;
        observe_clocks = true;
        try {
          action(engine, context);
        } catch (...) {
          observe_clocks = false;
          pin_root = nullptr;
          throw;
        }
        observe_clocks = false;
        observed.assign(clocks.begin(),
                        clocks.begin() + static_cast<std::ptrdiff_t>(clock_count));
      }
      lane.reset();
    }
    const auto runs = unwrap(discover_raw_runs(directory.path.string()));
    require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
            "one closed actual native run");
    auto reader = unwrap(
        RawRunReader::open((directory.path / "raw" / runs.front().name).string()));
    std::vector<std::byte> storage(4 * 1024 * 1024);
    std::vector<Record> result;
    while (const auto record = unwrap(reader->read(storage))) {
      ByteView bytes{record->raw};
      const auto length = read<std::uint32_t>(bytes);
      const auto site = read<std::uint32_t>(bytes);
      const auto ordinal = read<std::uint64_t>(bytes);
      require(length == record->raw.size(), "literal native envelope length");
      require(site != 8000 && site != 8001, "ordinary emission unexpectedly fenced");
      if ((site >= 1800 && site <= 1920) || site == 1060)
        result.push_back(
            {{record->origin.lane, ordinal}, site, {bytes.begin(), bytes.end()}});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
      return a.reference.lane < b.reference.lane ||
             (a.reference.lane == b.reference.lane &&
              a.reference.ordinal < b.reference.ordinal);
    });
    return result;
  }
};
void clock_pair(ByteView &bytes, const std::vector<ClockFact> &native) {
  const auto one = [&] {
    ClockFact result;
    result.method = read<clockid_t>(bytes);
    result.result = read<int>(bytes);
    result.error = read<int>(bytes);
    result.value.tv_sec = read<time_t>(bytes);
    result.value.tv_nsec = read<long>(bytes);
    return result;
  };
  const auto monotonic = one();
  require(read<bool>(bytes), "actual selected thread CPU acquisition");
  const auto cpu = one();
  const auto equal = [](const ClockFact &a, const ClockFact &b) {
    return a.method == b.method && a.result == b.result && a.error == b.error &&
           a.value.tv_sec == b.value.tv_sec && a.value.tv_nsec == b.value.tv_nsec;
  };
  bool matched = false;
  for (std::size_t i = 1; i < native.size(); ++i)
    matched = matched || (equal(native[i - 1], monotonic) && equal(native[i], cpu));
  require(matched && monotonic.method == CLOCK_MONOTONIC &&
              cpu.method == CLOCK_THREAD_CPUTIME_ID,
          "copied clock fields equal an actual forwarded native pair");
}
void turn_case(std::string source, std::string prompt,
               std::optional<ErrorCode> failure) {
  Fixture fixture;
  std::string initial_head;
  const auto records =
      fixture.capture([&](CodingEngine &engine, ContextStore &context) {
        initial_head = context.head();
        try {
          engine.turn({prompt, source});
          require(!failure, "selected failure must reach its actual branch");
        } catch (const Error &error) {
          require(failure && error.code == *failure, "actual specified turn failure");
        }
      });
  const auto &request = only(records, 1900);
  ByteView input{request.bytes};
  require(!read<bool>(input) && !read<bool>(input),
          "actual fresh turn admission flags");
  const auto prior_generation = id_bytes(input);
  require(prior_generation != IdentityBytes{}, "actual prior generation exists");
  require(text(input) == initial_head, "exact admission context head");
  clock_pair(input, fixture.observed);
  require(input.empty(), "exact complete turn request body");
  const auto &pin = only(records, 1902, request.reference);
  ByteView pinned{pin.bytes};
  require(same(ref(pinned), request.reference), "source request actual turn parent");
  const auto generation = id_bytes(pinned);
  require(generation != prior_generation && text(pinned) == source && pinned.empty(),
          "one exact original source and actual newly issued generation");
  const auto &returned = only(records, 1903, pin.reference);
  ByteView source_result{returned.bytes};
  require(same(ref(source_result), pin.reference), "actual source response parent");
  const auto source_branch = read<std::uint8_t>(source_result);
  if (source.size() <= fixture.header.limits.max_payload) {
    require(source_branch == 0, "real source retention returned");
    const auto journal = unwrap(AuditStreamId::from_bytes(id_bytes(source_result)));
    const SourceReference reference{journal, read<std::uint64_t>(source_result)};
    const auto original = unwrap(fixture.root->source(reference));
    require(
        original.size() == source.size() &&
            std::equal(original.begin(), original.end(),
                       std::as_bytes(std::span{source.data(), source.size()}).begin()),
        "source result locates the exact actual retained bytes");
  } else {
    if (!failure)
      throw std::runtime_error{"oversized source must select a failure oracle"};
    require(source_branch == 1 && read<ErrorCode>(source_result) == *failure,
            "real oversized source retention refused before Lua execution");
    (void)read<std::int64_t>(source_result);
  }
  require(source_result.empty(), "exact complete source return body");
  const auto &terminal = only(records, 1906, request.reference);
  ByteView ended{terminal.bytes};
  require(same(ref(ended), request.reference), "terminal actual turn parent");
  require(read<std::uint8_t>(ended) == (failure ? 2 : 0), "actual terminal branch");
  require(read<std::uint8_t>(ended) == (failure ? 4 : 3),
          "actual terminal reached stage");
  if (failure) {
    require(read<ErrorCode>(ended) == *failure, "actual terminal native error");
    (void)read<std::int64_t>(ended);
  }
  clock_pair(ended, fixture.observed);
  require(ended.empty(), "exact complete turn terminal body");
  unsigned closed = 0;
  for (const auto &record : records) {
    if (record.site != 1904)
      continue;
    ByteView state{record.bytes};
    require(same(ref(state), request.reference), "actual state turn parent");
    const auto stage = read<std::uint8_t>(state);
    const auto running = read<bool>(state);
    if (stage == 5) {
      ++closed;
      require(!running && record.reference.ordinal < terminal.reference.ordinal,
              "actual Running cleanup precedes terminal publication");
    } else
      require(running, "reached native state belongs to actual running turn");
    require(state.size() ==
                6 * sizeof(bool) + 2 * sizeof(unsigned) + 2 * sizeof(std::size_t),
            "complete native state shape");
  }
  require(closed == 1, "exactly one actual closed state");
}
Value policy(bool enabled, std::size_t trigger, std::size_t target,
             std::string reason = "native.fixture") {
  return Value::object({{"enabled", Value{enabled}},
                        {"trigger_bytes", Value{Number{trigger}}},
                        {"target_bytes", Value{Number{target}}},
                        {"reason", Value{std::move(reason)}}});
}
enum class BoundaryCut {
  none,
  no_pending_pin,
  atomic_pin,
  rejection_pin,
  prepublication
};
void policy_case(BoundaryCut cut) {
  Fixture fixture;
  const std::string restored_revision(513, 'R');
  fixture.log.record(ApplicationChannel::program,
                     Value::object({{"label", Value{"context-budget-effective-v1"}},
                                    {"policy", policy(true, 100, 50)},
                                    {"revision", Value{restored_revision}}}));
  std::size_t encoded_input = 0;
  std::string staged_revision;
  const bool pin_failure = cut == BoundaryCut::no_pending_pin ||
                           cut == BoundaryCut::atomic_pin ||
                           cut == BoundaryCut::rejection_pin;
  const bool accepted_program = cut == BoundaryCut::none ||
                                cut == BoundaryCut::no_pending_pin ||
                                cut == BoundaryCut::atomic_pin;
  pin_refused = false;
  const auto records = fixture.capture([&](CodingEngine &engine,
                                           ContextStore &context) {
    context.append({Value::object({{"role", Value{"assistant"}},
                                   {"content", Value{std::string(300, 'B')}}})},
                   "native.fixture");
    encoded_input = unwrap(encode_packet_string(Value{context.items()})).size();
    if (cut == BoundaryCut::atomic_pin || cut == BoundaryCut::rejection_pin ||
        cut == BoundaryCut::prepublication) {
      const auto view = context.view();
      const auto original = string_field(field(view, "entries").array().front(), "id");
      const auto managed = Value::object({{"base", Value{context.head()}},
                                          {"mode", Value{"select"}},
                                          {"ids", Value{Value::Array{Value{original}}}},
                                          {"source", Value{"native.fixture"}},
                                          {"reason", Value{"native.fixture"}}});
      engine.effect_policy = [&, managed](std::string_view name, const Value &) {
        if (name == "context_budget") {
          auto proposal = managed;
          for (auto &[key, value] : proposal.object())
            if (key == "base")
              value = Value{context.head()};
          const auto result = context.manage(proposal);
          const auto *staged = result.find("staged");
          require(staged && *staged == Value{true},
                  "real Context proposal staged during actual running turn");
        }
      };
    }
    if (cut != BoundaryCut::none)
      engine.operation_completed = [&](std::string_view name) {
        if (name.starts_with("context_budget")) {
          if (cut == BoundaryCut::prepublication)
            context.protect = [](const Value::Array &, const Value &,
                                 const RawEventRef &) {
              throw Error{ErrorCode::io, EIO};
            };
          else {
            if (cut == BoundaryCut::rejection_pin) {
              const auto edited = context.edit(
                  Value::object({{"base", Value{context.head()}},
                                 {"entries", field(context.view(), "entries")}}));
              require(field(edited, "accepted") == Value{true},
                      "actual accepted edit changes staged settlement base");
            }
            pin_root = fixture.root.get();
            pin_sequence = fixture.root->cursor().sequence;
          }
        }
      };
    bool failed = false;
    try {
      const auto result = engine.operator_call(
          "context_budget", Value::object({{"proposal", policy(false, 900, 450)}}));
      staged_revision = string_field(result, "pending_revision");
    } catch (const std::bad_alloc &) {
      pin_root = nullptr;
      failed = true;
    } catch (const Error &error) {
      require(cut == BoundaryCut::prepublication && error == Error{ErrorCode::io, EIO},
              "actual prepublication protection refusal");
      failed = true;
    }
    pin_root = nullptr;
    require(failed == (cut != BoundaryCut::none) && (!pin_failure || pin_refused),
            "actual selected publication/pin failure cut");
    engine.operation_completed = {};
    const auto effective = field(engine.stats(), "context_budget");
    require(field(field(effective, "effective"), "enabled") ==
                    Value{!accepted_program} &&
                field(effective, "pending") == Value{},
            "policy activation follows actual program acceptance only");
  });
  const Record *proposal = nullptr, *child = nullptr;
  unsigned staged = 0, activated = 0;
  for (const auto &record : records) {
    if (record.site == 1907) {
      ByteView bytes{record.bytes};
      const auto method = read<std::uint8_t>(bytes);
      const bool parented = read<bool>(bytes);
      std::optional<RawEventRef> parent;
      if (parented)
        parent = ref(bytes);
      const auto revision = text(bytes);
      require(bytes.empty(), "exact native policy request body");
      if (method == 1) {
        require(!proposal && revision == restored_revision && parented,
                "proposal uses exact restored513 byte effective revision");
        proposal = &record;
      } else if (method == 2 && parent && proposal &&
                 same(*parent, proposal->reference)) {
        require(!child && revision == restored_revision,
                "proposal child observes the effective revision before activation");
        child = &record;
      }
    } else if (record.site == 1909) {
      ++staged;
      ByteView bytes{record.bytes};
      require(proposal && same(ref(bytes), proposal->reference),
              "native policy stage parent");
      const auto revision = text(bytes);
      require(revision.size() == 32 && revision != restored_revision && bytes.empty(),
              "stage contains only the actual new pending revision");
      if (cut != BoundaryCut::none)
        staged_revision = revision;
      else
        require(revision == staged_revision,
                "native stage equals actual returned revision");
    } else if (record.site == 1905) {
      ++activated;
      ByteView bytes{record.bytes};
      const auto parent = ref(bytes);
      require(same(parent, only(records, 1900).reference) &&
                  read<std::uint8_t>(bytes) == 2 && text(bytes) == staged_revision &&
                  bytes.empty(),
              "exact accepted native budget activation");
    }
  }
  require(proposal && child && staged == 1 && activated == unsigned(accepted_program),
          "one proposal/child/stage; activation only for accepted program boundary");
  unsigned boundaries = 0;
  for (std::size_t ordinal = 0; ordinal < fixture.root->fact_count(); ++ordinal) {
    const auto fact = unwrap(fixture.root->fact(ordinal));
    const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
    if (!record || record->channel != ApplicationChannel::program)
      continue;
    const auto packet = unwrap(read_packet(record->payload));
    const auto *label = packet.find("label");
    if (label && *label == Value{"workflow-config-effective-v1"})
      ++boundaries;
  }
  require(boundaries == unsigned(accepted_program),
          "exact retained program boundary count, no accepted append replay");
  require(only(records, 1908, proposal->reference).bytes ==
              body(proposal->reference, true, false, true, std::size_t{450}, true,
                   std::size_t{900}),
          "validation copies proposed native fields without fabricated input");
  require(
      only(records, 1908, child->reference).bytes ==
          body(child->reference, true, true, std::size_t{100}, encoded_input, true),
      "child observation uses actual effective trigger and existing encoder length");
}
void policy_refusal_case(bool stale) {
  Fixture fixture;
  const auto records = fixture.capture([&](CodingEngine &engine, ContextStore &) {
    auto proposed = policy(false, 900, 450, "");
    if (stale)
      proposed.object().emplace_back("base", Value{"different actual revision"});
    const auto result =
        engine.operator_call("context_budget", Value::object({{"proposal", proposed}}));
    require(stale ? field(result, "staged") == Value{false}
                  : field(result, "error") == Value{"invalid_range"},
            "actual stale or empty-reason proposal refusal");
    const auto state = field(engine.stats(), "context_budget");
    require(field(state, "pending") == Value{},
            "refused proposal never transfers pending policy");
  });
  const Record *proposal = nullptr;
  for (const auto &record : records) {
    if (record.site == 1907) {
      ByteView bytes{record.bytes};
      if (read<std::uint8_t>(bytes) == 1) {
        require(!proposal, "one actual refused proposal owner");
        proposal = &record;
      }
    }
    require(record.site != 1909 && record.site != 1905,
            "refusal has no staged or activated policy");
  }
  require(proposal != nullptr, "actual refused proposal request exists");
  if (stale)
    require(only(records, 1910, proposal->reference).bytes ==
                body(proposal->reference, std::uint8_t{1}),
            "exact stale policy return");
  else {
    require(only(records, 1908, proposal->reference).bytes ==
                body(proposal->reference, true, false, true, std::size_t{450}, true,
                     std::size_t{900}),
            "empty reason preserves actual earlier parsed native fields");
    require(only(records, 1910, proposal->reference).bytes ==
                body(proposal->reference, std::uint8_t{2}, ErrorCode::invalid_range,
                     std::int64_t{0}),
            "exact native validation refusal terminal");
  }
}
void disabled_policy_case() {
  Fixture fixture;
  std::size_t input_bytes = 0;
  const auto records =
      fixture.capture([&](CodingEngine &engine, ContextStore &context) {
        (void)engine.operator_call(
            "context_budget", Value::object({{"proposal", policy(false, 900, 450)}}));
        input_bytes = unwrap(encode_packet_string(Value{context.items()})).size();
        (void)engine.stats();
      });
  unsigned disabled = 0;
  for (const auto &record : records) {
    if (record.site != 1908)
      continue;
    ByteView bytes{record.bytes};
    const auto parent = ref(bytes);
    const auto found = std::find_if(records.begin(), records.end(), [&](const auto &r) {
      return r.site == 1907 && same(r.reference, parent);
    });
    require(found != records.end(), "policy field has its actual request");
    ByteView request{found->bytes};
    if (read<std::uint8_t>(request) == 2 && !read<bool>(request) &&
        !read<bool>(bytes)) {
      require(!read<bool>(bytes), "disabled view never parses a trigger");
      require(read<std::size_t>(bytes) == input_bytes && !read<bool>(bytes) &&
                  bytes.empty(),
              "disabled stats contains actual encoded input and false triggered only");
      ++disabled;
    }
  }
  require(disabled == 1, "one actual disabled postactivation stats observation");
}
void protocol_case() {
  Fixture fixture;
  const std::string call{"literal-call-id"};
  const auto records =
      fixture.capture([&](CodingEngine &engine, ContextStore &context) {
        context.append({Value::object({{"type", Value{"function_call_output"}},
                                       {"call_id", Value{call}},
                                       {"output", Value{"unmatched"}}})},
                       "native.fixture");
        bool rejected = false;
        try {
          engine.validate_restart();
        } catch (const Error &error) {
          rejected = error.code == ErrorCode::conflict;
        }
        require(rejected, "real restart validation rejects orphan output");
      });
  const auto &request = only(records, 1911);
  require(request.bytes == body(false, std::size_t{1}),
          "exact standalone validation request");
  const auto &conflict = only(records, 1913, request.reference);
  ByteView bytes{conflict.bytes};
  require(same(ref(bytes), request.reference) && text(bytes) == call && bytes.empty(),
          "actual conflict copies the exact original call identity");
  require(only(records, 1912, request.reference).bytes ==
              body(request.reference, std::uint8_t{1}, ErrorCode::conflict,
                   std::int64_t{0}),
          "exact actual failed validation return");
}
bool stopped(pid_t pid) {
#if defined(__APPLE__)
  proc_bsdinfo info{};
  return ::proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) ==
             sizeof(info) &&
         info.pbi_status == SSTOP;
#else
  std::ifstream input{"/proc/" + std::to_string(pid) + "/status"};
  std::string line;
  while (std::getline(input, line))
    if (line.starts_with("State:"))
      return line.find('T') != std::string::npos;
  return false;
#endif
}
void stopped_pair(RawRuntimeStatus holders) {
  require(::kill(holders.recorder, SIGSTOP) == 0 &&
              ::kill(holders.supervisor, SIGSTOP) == 0,
          "stop only actual test-owned Rec/Sup");
  for (unsigned attempt = 0; attempt < 100000; ++attempt) {
    if (stopped(holders.recorder) && stopped(holders.supervisor))
      return;
    std::this_thread::yield();
  }
  throw std::runtime_error{"actual holder stop not observed"};
}
void policy_child_refusal_case() {
  Fixture fixture;
  std::optional<Error> caller_error;
  const std::string restored_revision(513, 'R');
  fixture.log.record(ApplicationChannel::program,
                     Value::object({{"label", Value{"context-budget-effective-v1"}},
                                    {"policy", policy(true, 100, 50)},
                                    {"revision", Value{restored_revision}}}));
  child_cut = false;
  const auto records = fixture.capture([&](CodingEngine &engine, ContextStore &) {
    std::string name = "/bb";
    constexpr char digits[] = "0123456789abcdef";
    const auto epoch = raw_lane_epoch();
    for (std::size_t index = 0; index < 12; ++index) {
      const auto byte = std::to_integer<unsigned>(epoch[index]);
      name += digits[byte >> 4U];
      name += digits[byte & 15U];
    }
    auto probe = unwrap(
        RawChannel::open(name, raw_lane_epoch(), fixture.options.producer_capacities));
    std::array<int, 2> pipe{-1, -1};
    require(::pipe(pipe.data()) == 0, "actual child admission cancellation endpoint");
    struct Restore {
      RawRuntimeStatus holders;
      std::array<int, 2> pipe;
      ~Restore() {
        watch_child = false;
        child_probe = nullptr;
        child_cancel = -1;
        (void)::kill(holders.recorder, SIGCONT);
        (void)::kill(holders.supervisor, SIGCONT);
        (void)::close(pipe[0]);
        (void)::close(pipe[1]);
      }
    } restore{fixture.holders, pipe};
    std::optional<RawWaitScope> wait;
    wait.emplace(RawWaitControl{{pipe[0], -1}, {}});
    engine.effect_policy = [&](std::string_view operation, const Value &) {
      if (operation == "context_budget") {
        stopped_pair(fixture.holders);
        child_probe = &probe;
        child_probe_start = unwrap(probe.cursors(RawPartition::control)).published;
        child_cancel = pipe[1];
        watch_child = true;
      }
    };
    try {
      (void)engine.operator_call(
          "context_budget", Value::object({{"proposal", policy(false, 900, 450)}}));
    } catch (const Error &error) {
      caller_error = error;
    }
    watch_child = false;
    child_probe = nullptr;
    wait.reset();
    (void)::kill(fixture.holders.recorder, SIGCONT);
    (void)::kill(fixture.holders.supervisor, SIGCONT);
    require(caller_error.has_value() && child_cut && child_allocation != 0,
            "real postvalidation business revision allocation cancels child admission");
    const auto state = field(engine.stats(), "context_budget");
    require(field(state, "pending") == Value{} &&
                field(field(state, "effective"), "enabled") == Value{true},
            "child admission refusal precedes pending policy transfer and activation");
  });
  const Record *proposal = nullptr;
  for (const auto &record : records) {
    require(record.site != 1909 && record.site != 1905,
            "refused child has no staged or activated policy");
    if (record.site != 1907)
      continue;
    ByteView bytes{record.bytes};
    const auto method = read<std::uint8_t>(bytes);
    const bool parent = read<bool>(bytes);
    if (parent)
      (void)ref(bytes);
    if (method == 1) {
      require(!proposal && parent && text(bytes) == restored_revision && bytes.empty(),
              "exact real parent request before child refusal");
      proposal = &record;
    }
    require(method != 2 || !parent,
            "failed child acquisition never publishes a fictitious child request");
  }
  require(proposal != nullptr, "real child refusal has its parent request");
  require(only(records, 1908, proposal->reference).bytes ==
                  body(proposal->reference, true, false, true, std::size_t{450}, true,
                       std::size_t{900}) &&
              only(records, 1910, proposal->reference).bytes ==
                  body(proposal->reference, std::uint8_t{2}, ErrorCode::interrupted,
                       std::int64_t{ECANCELED}),
          "native parent validation and held interrupted terminal remain exact");
  // The existing operation receipt is unpaid on this path; its refusal closes
  // semantic admission. The child Error and the final caller Error are distinct.
  if (!caller_error || *caller_error != Error{ErrorCode::audit_unavailable})
    throw std::runtime_error{
        "actual unpaid operation receipt closes semantic admission"};
  const auto &turn = only(records, 1900);
  const auto &terminal = only(records, 1906, turn.reference);
  ByteView ended{terminal.bytes};
  require(same(ref(ended), turn.reference) && read<std::uint8_t>(ended) == 2 &&
              read<std::uint8_t>(ended) == 4 &&
              read<ErrorCode>(ended) == caller_error->code &&
              read<std::int64_t>(ended) == caller_error->detail,
          "held turn terminal records the actual final caller failure");
  clock_pair(ended, fixture.observed);
  require(ended.empty(), "exact child refusal turn terminal fields");
  unsigned closed = 0;
  for (const auto &record : records) {
    if (record.site != 1904)
      continue;
    ByteView state{record.bytes};
    require(same(ref(state), turn.reference), "actual refused turn state owner");
    if (read<std::uint8_t>(state) == 5) {
      ++closed;
      require(!read<bool>(state) &&
                  record.reference.ordinal < terminal.reference.ordinal,
              "held cleanup closes Running before final refused turn terminal");
    }
  }
  require(closed == 1, "one actual refused turn cleanup");
}
void cancellation_case() {
  Fixture fixture;
  bool full = false, cancelled = false;
  const auto records =
      fixture.capture([&](CodingEngine &engine, ContextStore &context) {
        context.append({Value::object({{"type", Value{"function_call"}},
                                       {"call_id", Value{"unanswered-original"}},
                                       {"name", Value{"write_file"}},
                                       {"arguments", Value{"{}"}}})},
                       "native.fixture");
        struct Resume {
          RawRuntimeStatus holders;
          ~Resume() {
            (void)::kill(holders.recorder, SIGCONT);
            (void)::kill(holders.supervisor, SIGCONT);
          }
        } resume{fixture.holders};
        std::array<int, 2> pipe{-1, -1};
        require(::pipe(pipe.data()) == 0, "actual cancellation endpoint");
        struct Close {
          std::array<int, 2> &pipe;
          ~Close() {
            (void)::close(pipe[0]);
            (void)::close(pipe[1]);
          }
        } close{pipe};
        std::optional<RawWaitScope> wait;
        wait.emplace(RawWaitControl{{pipe[0], -1}, {}});
        engine.cancelled = [&] {
          if (!cancelled) {
            stopped_pair(fixture.holders);
            for (const auto size : {std::uint64_t{1024}, std::uint64_t{16}})
              for (;;) {
                auto acquired = raw_try_claim_control(size);
                if (!acquired.has_value()) {
                  require(acquired.error().code == ErrorCode::capacity,
                          "physical ordinary CONTROL exhaustion");
                  break;
                }
                auto credit = std::move(acquired).value();
                const std::array<std::byte, 1008> padding{};
                const ByteView bytes{padding};
                const auto fields = size == 16 ? std::span<const ByteView>{}
                                               : std::span{&bytes, std::size_t{1}};
                (void)unwrap(raw_emit(9291, fields, RawPartition::control, &credit));
              }
            full = !raw_try_claim_control(16).has_value();
            const char byte = 'C';
            require(::write(pipe[1], &byte, 1) == 1, "real sticky cancellation wake");
            cancelled = true;
          }
          return true;
        };
        bool interrupted = false;
        try {
          engine.turn({"", "return 17"});
        } catch (const Error &error) {
          interrupted = error.code == ErrorCode::interrupted;
        }
        engine.cancelled = {};
        require(full && cancelled && interrupted && stopped(fixture.holders.recorder) &&
                    stopped(fixture.holders.supervisor),
                "held stop completes with actual full CONTROL, sticky cancellation, "
                "stopped holders");
        wait.reset();
        // Resume before making an ordinary presentation query.
        (void)::kill(fixture.holders.recorder, SIGCONT);
        (void)::kill(fixture.holders.supervisor, SIGCONT);
        const auto items = context.items();
        require(items.size() == 2 &&
                    string_field(items.back(), "type") == "function_call_output" &&
                    string_field(items.back(), "call_id") == "unanswered-original",
                "actual stop append preserves exact original unanswered call linkage");
      });
  const auto &request = only(records, 1900);
  ByteView ended{only(records, 1906, request.reference).bytes};
  require(same(ref(ended), request.reference) && read<std::uint8_t>(ended) == 2 &&
              read<std::uint8_t>(ended) == 4 &&
              read<ErrorCode>(ended) == ErrorCode::interrupted &&
              read<std::int64_t>(ended) == 0,
          "exact held cancellation terminal");
  unsigned placeholder_returns = 0;
  for (const auto &record : records) {
    if (record.site != 1918)
      continue;
    ByteView bytes{record.bytes};
    require(same(ref(bytes), request.reference), "placeholder actual turn parent");
    (void)read<bool>(bytes);
    require(text(bytes).size() == 32 && bytes.empty(), "exact placeholder input head");
    require(only(records, 1914, record.reference).bytes ==
                body(record.reference, std::uint8_t{0}, std::size_t{1}),
            "held placeholder return reports the actual one constructed row");
    ++placeholder_returns;
  }
  require(placeholder_returns == 2,
          "initial and canceled placeholder transformations distinct");
}
int child(const std::filesystem::path &directory) {
  const int fd = ::open((directory / "gate").c_str(), O_RDONLY);
  if (fd < 0)
    return 90;
  char byte = 0;
  const auto acquired = ::read(fd, &byte, 1);
  (void)::close(fd);
  return acquired == 1 && byte == 'G' ? 7 : 91;
}
void task_case(const std::string &executable) {
  Fixture fixture;
  fixture.options.producer_capacities[0] = 32 * 1024 * 1024;
  require(::mkfifo((fixture.directory.path / "gate").c_str(), 0600) == 0,
          "native owned child gate");
  unsigned notifications = 0;
  bool terminal_display = false, task_precedes_display = false;
  std::string task;
  const auto records = fixture.capture([&](CodingEngine &engine, ContextStore &) {
    const auto created = engine.tasks().operator_command("add literal task");
    task = string_field(field(created, "changed").array().front(), "id");
    engine.task_activity = [&](const Value &activity) {
      ++notifications;
      require(string_field(activity, "task_id") == task,
              "actual task callback association");
      if (notifications == 1)
        throw Error{ErrorCode::io, EIO};
      refuse_next = true;
      try {
        const std::string refused(129, 'X');
        require(refused.size() == 129 && refused.front() == 'X',
                "actual callback allocation returned its requested body");
      } catch (...) {
        refuse_next = false;
        throw;
      }
    };
    engine.process_output = [&](std::string_view output) {
      if (output.starts_with("\n[exit ")) {
        terminal_display = true;
        task_precedes_display = notifications == 2;
      }
    };
    const auto launched = engine.operator_call(
        "exec",
        Value::object(
            {{"argv", Value{Value::Array{Value{executable}, Value{"--child"},
                                         Value{fixture.directory.path.string()}}}},
             {"yield_ms", Value{Number{1}}},
             {"task_id", Value{task}}}));
    require(
        !launched.find("exit_code") && notifications == 1,
        "background return is still running, throwing presentation cannot settle it");
    const auto job = string_field(launched, "job_id");
    int gate = -1;
    for (unsigned attempt = 0; attempt < 100000; ++attempt) {
      gate = ::open((fixture.directory.path / "gate").c_str(), O_WRONLY | O_NONBLOCK);
      if (gate >= 0)
        break;
      require(errno == ENXIO, "actual child FIFO admission");
      std::this_thread::yield();
    }
    require(gate >= 0, "actual native child reached its gate");
    engine.cancelled = [&] {
      if (gate >= 0 &&
          string_field(engine.command_jobs()->foreground(), "job_id") == job) {
        const char byte = 'G';
        const auto written = ::write(gate, &byte, 1);
        (void)::close(gate);
        gate = -1;
        require(written == 1, "release owned child only during actual foreground wait");
      }
      return false;
    };
    const auto finished = engine.operator_call(
        "process", Value::object({{"op", Value{"foreground"}},
                                  {"job_id", Value{job}},
                                  {"yield_ms", Value{Number{5000}}}}));
    engine.cancelled = {};
    require(field(finished, "exit_code") == Value{Number{7}} && notifications == 2,
            "one actual accepted failed child settlement despite callback allocation "
            "refusal");
    require(terminal_display && task_precedes_display,
            "accepted task terminal is delivered before exit presentation");
  });
  const auto &binding = only(records, 1915);
  ByteView fields{binding.bytes};
  const auto operation = ref(fields);
  require(std::any_of(records.begin(), records.end(),
                      [&](const auto &record) {
                        return record.site == 1060 && same(record.reference, operation);
                      }),
          "actual command admission parent");
  const auto attempt = id_bytes(fields);
  require(attempt != IdentityBytes{} && text(fields) == task && fields.empty(),
          "exact native task binding with actual issued attempt");
  unsigned activities = 0;
  for (const auto &record : records) {
    if (record.site != 1916)
      continue;
    require(record.bytes == body(binding.reference,
                                 activities == 0 ? std::uint8_t{0} : std::uint8_t{2}),
            "actual running then failed activity, no background completion");
    const auto &notification = only(records, 1920, record.reference);
    require(notification.bytes == body(record.reference, true),
            "separate actual presentation request");
    require(only(records, 1917, notification.reference).bytes ==
                (activities == 0 ? body(notification.reference, std::uint8_t{2},
                                        ErrorCode::io, std::int64_t{EIO})
                                 : body(notification.reference, std::uint8_t{3})),
            "exact callback Error/allocation outcome after native task activity");
    ++activities;
  }
  require(activities == 2, "one real task running/terminal pair");
}
} // namespace
void *operator new(std::size_t size) {
  observe_child_boundary(size);
  if (refuse_next || (pin_root && pin_root->cursor().sequence > pin_sequence)) {
    pin_refused = pin_root != nullptr;
    pin_root = nullptr;
    refuse_next = false;
    throw std::bad_alloc{};
  }
  if (auto *pointer = std::malloc(size == 0 ? 1 : size))
    return pointer;
  throw std::bad_alloc{};
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
extern "C" int clock_gettime(clockid_t method, timespec *value)
#if defined(__linux__)
    noexcept
#endif
{
  using Native = int (*)(clockid_t, timespec *);
  static const auto forward =
      reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "clock_gettime"));
  if (!forward)
    std::abort();
  const int result = forward(method, value), saved = errno;
  if (observe_clocks) {
    if (clock_count == clocks.size())
      std::abort();
    clocks[clock_count++] = {method, result, saved, result == 0 ? *value : timespec{}};
  }
  errno = saved;
  return result;
}
int main(int argc, char **argv) {
  if (argc == 3 && std::string_view{argv[1]} == "--child")
    return child(argv[2]);
  try {
    turn_case("return 17", "literal operator prompt", {});
    turn_case("bad Lua !", "", ErrorCode::external_unknown);
    turn_case(std::string(131072, 'A'), "", ErrorCode::capacity);
    for (const auto cut :
         {BoundaryCut::none, BoundaryCut::no_pending_pin, BoundaryCut::atomic_pin,
          BoundaryCut::rejection_pin, BoundaryCut::prepublication}) {
      policy_case(cut);
    }
    policy_refusal_case(false);
    policy_refusal_case(true);
    disabled_policy_case();
    policy_child_refusal_case();
    protocol_case();
    cancellation_case();
    task_case(std::filesystem::absolute(argv[0]).string());
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
