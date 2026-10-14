#include "blackbird/beads.hpp"
#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/raw_file.hpp"
#include "blackbird/raw_reader.hpp"
#include <array>
#include <csignal>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <iostream>
#include <limits.h>
#include <new>
#include <sys/socket.h>
#include <unistd.h>
using namespace blackbird;
namespace {
constexpr char stderr_source[] = "BEADS-RAW-STDERR\0\xffprefix";
constexpr char stdout_source[] = "BEADS-RAW-STDOUT\0\xfeprefix";
using NativeRead = ssize_t (*)(int, void *, std::size_t);
NativeRead native_read = nullptr;
using NativeRealpath = char *(*)(const char *, char *);
NativeRealpath native_realpath = nullptr;
void require(bool, const char *);
int join_cut = 0;
bool join_cut_hit = false, join_cut_ready = false, join_cut_busy = false;
std::string join_operand;
std::array<std::optional<RawCapacityClaim>, 192> join_holds;
void hold_remaining(RawPartition partition, std::uint64_t remaining) {
  std::size_t used = 0;
  for (const auto amount :
       {4ULL * 1024 * 1024, 65536ULL, 4096ULL, 256ULL, 16ULL, 1ULL}) {
    for (;;) {
      auto grant = partition == RawPartition::control ? raw_try_claim_control(amount)
                                                      : raw_claim_data(amount);
      if (!grant.has_value()) {
        require(grant.error() == Error{ErrorCode::capacity},
                "actual finite grant floor");
        break;
      }
      require(used < join_holds.size(), "fixed native held grant slots");
      join_holds[used++].emplace(std::move(grant).value());
    }
  }
  for (auto &bank : join_holds)
    if (bank && bank->remaining() >= remaining) {
      auto released = unwrap(bank->split(remaining));
      return;
    }
  require(false, "actual held source credit can release exact allowance");
}
int gate_write = -1;
bool observe = false;
std::size_t stderr_acquired = 0, stdout_acquired = 0;
thread_local bool allocation_armed = false, allocation_hit = false, stack_busy = false;
thread_local int selected_allocation = 0;
thread_local bool selected_allocation_hit = false;
thread_local std::size_t selected_allocation_size = 0;
void require(bool yes, const char *why) {
  if (!yes)
    throw std::runtime_error{why};
}
bool canonical_return_stack() noexcept {
  std::array<void *, 48> frames{};
  const auto count = ::backtrace(frames.data(), static_cast<int>(frames.size()));
  for (int index = 0; index < count; ++index) {
    Dl_info info{};
    if (::dladdr(frames[static_cast<std::size_t>(index)], &info) && info.dli_sname &&
        std::strstr(info.dli_sname, "raw_file_canonical"))
      return true;
  }
  return false;
}
void join_admission_cut() {
  join_cut_busy = true;
  constexpr char digits[] = "0123456789abcdef";
  const auto lane = raw_lane_epoch();
  std::array<char, 28> name{'/', 'b', 'b'};
  for (std::size_t index = 0; index < 12; ++index) {
    const auto value = std::to_integer<unsigned char>(lane[index]);
    name[3 + 2 * index] = digits[value >> 4U];
    name[4 + 2 * index] = digits[value & 15U];
  }
  auto channel = unwrap(RawChannel::open(name.data(), lane,
                                         {256 * 1024 * 1024, 1024 * 1024, 128 * 1024}));
  for (;;) {
    const auto data = unwrap(channel.cursors(RawPartition::data));
    const auto control = unwrap(channel.cursors(RawPartition::control));
    if (data.published == data.acknowledged &&
        control.published == control.acknowledged)
      break;
    std::this_thread::yield();
  }
  join_cut_hit = true;
  if (join_cut == 1)
    hold_remaining(RawPartition::control, 48);
  else
    hold_remaining(RawPartition::data,
                   49 + join_operand.size() + 1 + 6 - (join_cut == 3 ? 1 : 0));
  join_cut_busy = false;
}
bool actual_original_stack() noexcept {
  if (stack_busy)
    return false;
  stack_busy = true;
  std::array<void *, 48> frames{};
  const auto count = ::backtrace(frames.data(), static_cast<int>(frames.size()));
  bool found = false;
  for (int i = 0; i < count; ++i) {
    Dl_info info{};
    if (::dladdr(frames[static_cast<std::size_t>(i)], &info) && info.dli_sname &&
        std::strstr(info.dli_sname, "AuditLog") &&
        std::strstr(info.dli_sname, "original"))
      found = true;
  }
  stack_busy = false;
  return found;
}
bool selected_allocation_stack(std::size_t size) noexcept {
  if (!selected_allocation || stack_busy)
    return false;
  stack_busy = true;
  std::array<void *, 64> frames{};
  const auto count = ::backtrace(frames.data(), static_cast<int>(frames.size()));
  bool argv = false;
  for (int i = 0; i < count; ++i) {
    Dl_info info{};
    if (::dladdr(frames[static_cast<std::size_t>(i)], &info) && info.dli_sname)
      argv = argv || (std::strstr(info.dli_sname, "RawBeadsArgv") &&
                      std::strstr(info.dli_sname, "push"));
  }
  stack_busy = false;
  return argv && size >= 8192;
}
template <class T> T id(unsigned char byte) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{byte};
  return unwrap(T::from_bytes(bytes));
}
template <class T> T take(ByteView &body) {
  require(body.size() >= sizeof(T), "raw native field exists");
  T value{};
  std::memcpy(&value, body.data(), sizeof(value));
  body = body.subspan(sizeof(value));
  return value;
}
struct RunObservation {
  bool before_mutation = false, after_mutation = false;
  RawOperationBranch branch = RawOperationBranch::unfinished;
  std::optional<Error> error;
  std::optional<Value> result;
};
std::vector<RunObservation> run_observations;
Value run_adapter(BeadsAdapter &adapter, const Value &arguments,
                  const RawAdapterContext *context = nullptr) {
  if (!context || !context->parent)
    return adapter.run(arguments, context);
  RunObservation actual;
  actual.before_mutation = adapter.mutation_may_have_started();
  struct Complete {
    BeadsAdapter &adapter;
    RunObservation &actual;
    ~Complete() {
      actual.after_mutation = adapter.mutation_may_have_started();
      run_observations.push_back(std::move(actual));
    }
  } complete{adapter, actual};
  try {
    auto returned = adapter.run(arguments, context);
    actual.branch = RawOperationBranch::returned;
    actual.result = returned;
    return returned;
  } catch (const Error &error) {
    actual.branch = RawOperationBranch::error;
    actual.error = error;
    throw;
  } catch (const std::bad_alloc &) {
    actual.branch = RawOperationBranch::allocation;
    throw;
  } catch (const std::length_error &) {
    actual.branch = RawOperationBranch::length;
    throw;
  } catch (const std::exception &) {
    actual.branch = RawOperationBranch::standard;
    throw;
  } catch (...) {
    actual.branch = RawOperationBranch::nonstandard;
    throw;
  }
}
class NoProvider : public CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw std::runtime_error{"unexpected provider"};
  }
};
void write_all(int fd, std::string_view body) {
  while (!body.empty()) {
    const auto n = ::write(fd, body.data(), body.size());
    require(n > 0, "real fixture pipe write");
    body.remove_prefix(static_cast<std::size_t>(n));
  }
}
int child(int argc, char **argv) {
  std::string_view operation;
  for (int i = 1; i < argc; ++i)
    if (!std::string_view{argv[i]}.starts_with("-")) {
      operation = argv[i];
      break;
    }
  const auto *directory = std::getenv("BEADS_DIR");
  require(directory, "actual child BEADS_DIR");
  const auto *mode_env = std::getenv("BB_RAW_BEADS_MODE");
  const std::string_view mode = mode_env ? mode_env : "normal";
  const auto project = std::filesystem::path{directory}.parent_path();
  Value::Array arguments;
  for (int i = 1; i < argc; ++i)
    arguments.emplace_back(std::string{argv[i]});
  const auto call = unwrap(dump_json(Value{std::move(arguments)})) + "\n";
  const int calls =
      ::open((project / "calls").c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
  require(calls >= 0, "independent native argv log open");
  write_all(calls, call);
  require(::close(calls) == 0, "independent native argv log close");
  if (operation == "version") {
    write_all(STDOUT_FILENO, mode == "badversion" ? "{\"version\":\"0.59.0\"}"
                                                  : "{\"version\":\"0.58.0\"}");
    return 0;
  }
  if (operation == "where") {
    write_all(STDOUT_FILENO,
              "{\"path\":\"" +
                  std::string{mode == "wrongwhere" ? "/wrong/.beads" : directory} +
                  "\"}");
    return 0;
  }
  if (operation == "ready" && std::getenv("BB_RAW_BEADS_GATE")) {
    write_all(STDOUT_FILENO, "[]");
    return 0;
  }
  if (!std::getenv("BB_RAW_BEADS_GATE")) {
    const bool mutation =
        operation == "create" || operation == "update" || operation == "close";
    if (mutation) {
      const int effect =
          ::open((project / "effect").c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
      require(effect >= 0, "native independent append-only effect counter");
      write_all(effect, "once");
      require(::close(effect) == 0, "actual independent effect counter closes");
    }
    write_all(STDERR_FILENO, "native warning\n");
    if (mode == "overflow") {
      write_all(STDOUT_FILENO, std::string(65537, 'z'));
      return 0;
    }
    if (mode == "nonzero") {
      write_all(STDOUT_FILENO, "{\"error\":\"postwrite failure\"}");
      return 9;
    }
    if (mode == "binary") {
      write_all(STDOUT_FILENO, {stdout_source, sizeof(stdout_source) - 1});
      return 0;
    }
    if (operation == "ready" || operation == "list") {
      if (mode == "cache")
        write_all(STDOUT_FILENO, unwrap(dump_json(Value{Value::Array{Value::object(
                                     {{"id", Value{"cached"}},
                                      {"title", Value{std::string(2048, 'c')}},
                                      {"status", Value{"open"}}})}})));
      else
        write_all(STDOUT_FILENO, mode == "wrongshape" ? "{}" : "[]");
      return 0;
    }
    bool claim = false;
    for (int i = 1; i < argc; ++i)
      if (std::string_view{argv[i]} == "--claim")
        claim = true;
    const auto target =
        operation == "create" ? std::string{"created"} : std::string{argv[argc - 1]};
    auto item = Value::object(
        {{"id", Value{mode == "wrongid" && operation != "show" ? "wrong" : target}},
         {"title", Value{"native"}},
         {"status",
          Value{operation == "close" ? "closed" : (claim ? "in_progress" : "open")}}});
    if (claim)
      item.object().emplace_back(
          "assignee", Value{mode == "wrongassignee" ? "wrong" : "native actor;$()"});
    write_all(
        STDOUT_FILENO,
        unwrap(dump_json(operation == "create" ? item : Value{Value::Array{item}})));
    return 0;
  }
  require(operation == "create", "real selected mutation command");
  const auto *fd_text = std::getenv("BB_RAW_BEADS_GATE");
  require(fd_text, "native gate descriptor inherited");
  const int gate = std::atoi(fd_text);
  write_all(STDERR_FILENO, {stderr_source, sizeof(stderr_source) - 1});
  char grant = 0;
  require(native_read(gate, &grant, 1) == 1 && grant == 'g',
          "stdout waits for actual parent stderr acquisition");
  const int marker =
      ::open((project / "effect").c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
  require(marker >= 0, "actual mutation marker open");
  write_all(marker, "once");
  require(::close(marker) == 0, "actual mutation marker close");
  write_all(STDOUT_FILENO, {stdout_source, sizeof(stdout_source) - 1});
  return 0;
}
void baseline(const char *executable) {
  char name[] = "/tmp/bb-beads-raw-XXXXXX";
  const auto *created = ::mkdtemp(name);
  require(created, "temporary Beads owner fixture");
  const auto base = std::filesystem::canonical(created);
  std::filesystem::create_directory(base / ".beads");
  std::filesystem::create_directory(base / "audit");
  std::filesystem::permissions(base / "audit", std::filesystem::perms::owner_all);
  int gate[2]{};
  require(::pipe(gate) == 0, "actual stream acknowledgment gate");
  gate_write = gate[1];
  const auto descriptor = std::to_string(gate[0]);
  require(::setenv("BB_RAW_BEADS_GATE", descriptor.c_str(), 1) == 0,
          "fixture gate is an ordinary retained child environment entry");
  const auto config = Value::object(
      {{"op", Value{"configure"}},
       {"project", Value{base.string()}},
       {"executable", Value{std::filesystem::canonical(executable).string()}},
       {"actor", Value{"native actor;$()"}}});
  {
    auto runtime = unwrap(
        RawRuntime::open(base.string(), id<EnvironmentId>(61),
                         {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    auto issuer = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{issuer};
      JournalHeader header{id<EnvironmentId>(62),
                           id<AuditStreamId>(63),
                           3,
                           {32768, 131072},
                           std::nullopt};
      auto root = unwrap(RetainedState::create(
          std::make_unique<NativeJournalDirectory>(
              unwrap(NativeJournalDirectory::open((base / "audit").string()))),
          "audit", header, {64 * 1024 * 1024, 10000}));
      AuditLog log{*root};
      ContextStore context{log};
      NoProvider provider;
      CodingEngine engine{log, context, provider, "fixture"};
      require(string_field(engine.operator_call("beads", config), "status") ==
                  "configured",
              "actual public Coding owner configure");
      require(string_field(engine.operator_call(
                               "beads", Value::object({{"op", Value{"ready"}}})),
                           "status") == "ok",
              "actual lazy version/where binding");
      std::array<void *, 4> warm{};
      (void)::backtrace(warm.data(), static_cast<int>(warm.size()));
      observe = true;
      try {
        (void)engine.operator_call("beads",
                                   Value::object({{"op", Value{"create"}},
                                                  {"title", Value{"native effect"}}}));
      } catch (const std::bad_alloc &) {
      }
      observe = false;
      allocation_armed = false;
      require(stderr_acquired == sizeof(stderr_source) - 1 &&
                  stdout_acquired == sizeof(stdout_source) - 1,
              "forwarded real native reads acquired both exact binary prefixes");
      require(allocation_hit, "actual semantic AuditLog original allocation throws "
                              "after native stdout acquisition");
      require(read_file(base / "effect") == "once",
              "actual child effect survives observer failure without replay");
      bool unknown = false;
      for (auto fact = root->committed_facts().rbegin();
           fact != root->committed_facts().rend(); ++fact)
        if (const auto *terminal =
                std::get_if<AttemptObservationEvent>(&fact->event.body);
            terminal && terminal->phase == AttemptPhase::terminal) {
          unknown = terminal->disposition == AttemptDisposition::unknown;
          break;
        }
      require(unknown, "actual retained Coding write terminal remains unknown after "
                       "acquired observer failure");
    }
    issuer.reset();
  }
  require(::close(gate[0]) == 0 && ::close(gate[1]) == 0, "fixture gate cleanup");
  ::unsetenv("BB_RAW_BEADS_GATE");
  const auto runs = unwrap(discover_raw_runs(base.string()));
  require(runs.size() == 1, "independent recorder has one public owner run");
  const auto &snapshot = runs[0].snapshot;
  if (!snapshot)
    throw std::runtime_error{"public owner snapshot missing"};
  require(snapshot->closed, "independent recorder closes genuine public owner run");
  auto reader = unwrap(RawRunReader::open((base / "raw" / runs[0].name).string()));
  std::vector<std::byte> storage(32 * 1024 * 1024);
  std::size_t raw_stderr = 0, raw_stdout = 0, run_count = 0;
  while (const auto record = unwrap(reader->read(storage))) {
    auto body = record->raw;
    require(take<std::uint32_t>(body) == record->raw.size(),
            "actual envelope byte length");
    const auto site = take<std::uint32_t>(body);
    (void)take<std::uint64_t>(body);
    require(site != 8000 && site != 8001,
            "no native capture failure hidden in baseline");
    if (site == 3500)
      ++run_count;
    if (site != 1404)
      continue;
    (void)take<IdentityBytes>(body);
    (void)take<std::uint64_t>(body);
    const auto channel = take<std::uint8_t>(body);
    (void)take<std::size_t>(body);
    const auto count = take<ssize_t>(body);
    (void)take<int>(body);
    if (count > 0 && body.size() == sizeof(stdout_source) - 1 &&
        std::memcmp(body.data(), stdout_source, body.size()) == 0 && channel == 1)
      raw_stdout += body.size();
    if (count > 0 && body.size() == sizeof(stderr_source) - 1 &&
        std::memcmp(body.data(), stderr_source, body.size()) == 0 && channel == 2)
      raw_stderr += body.size();
  }
  std::cerr << "native stderr=" << stderr_acquired << " stdout=" << stdout_acquired
            << " raw stderr=" << raw_stderr << " stdout=" << raw_stdout
            << " Beads run=" << run_count << " fixture=" << base << '\n';
  require(raw_stderr == stderr_acquired && raw_stdout == stdout_acquired &&
              run_count == 3,
          "actual Beads native source bytes and owner graph survive semantic observer "
          "allocation failure");
}
struct Event {
  RawEventRef ref;
  std::uint32_t site;
  std::vector<std::byte> body;
};
RawEventRef reference(ByteView &body) {
  return {take<IdentityBytes>(body), take<std::uint64_t>(body)};
}
bool same(RawEventRef a, RawEventRef b) {
  return a.lane == b.lane && a.ordinal == b.ordinal;
}
std::vector<Event> read_events(const std::filesystem::path &base) {
  const auto runs = unwrap(discover_raw_runs(base.string()));
  require(runs.size() == 1, "real adapter has one native run");
  const auto &snapshot = runs[0].snapshot;
  if (!snapshot)
    throw std::runtime_error{"native snapshot missing"};
  require(snapshot->closed, "real adapter run closed after owner unwind");
  auto reader = unwrap(RawRunReader::open((base / "raw" / runs[0].name).string()));
  std::vector<std::byte> storage(32 * 1024 * 1024);
  std::vector<Event> result;
  while (const auto record = unwrap(reader->read(storage))) {
    auto body = record->raw;
    require(take<std::uint32_t>(body) == record->raw.size(),
            "literal raw envelope length");
    const auto site = take<std::uint32_t>(body);
    const auto ordinal = take<std::uint64_t>(body);
    require(site != 8000 && site != 8001, "native capture does not silently fail");
    result.push_back(
        {{record->origin.lane, ordinal}, site, {body.begin(), body.end()}});
  }
  return result;
}
struct Fixture {
  std::filesystem::path base;
  Value config;
  explicit Fixture(const char *executable) {
    char name[] = "/tmp/bb-beads-owner-XXXXXX";
    require(::mkdtemp(name), "Beads direct native temporary project");
    base = std::filesystem::canonical(name);
    std::filesystem::create_directory(base / ".beads");
    config = Value::object(
        {{"op", Value{"configure"}},
         {"project", Value{base.string()}},
         {"executable", Value{std::filesystem::canonical(executable).string()}},
         {"actor", Value{"native actor;$()"}}});
  }
  ~Fixture() {
    ::unsetenv("BB_RAW_BEADS_MODE");
    if (std::uncaught_exceptions())
      std::cerr << "preserved Beads fixture " << base << '\n';
    else
      std::filesystem::remove_all(base);
  }
};
template <class F> std::vector<Event> captured(Fixture &fixture, F &&body) {
  run_observations.clear();
  run_observations.reserve(512);
  {
    auto runtime = unwrap(
        RawRuntime::open(fixture.base.string(), id<EnvironmentId>(71),
                         {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    auto issuer = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{issuer};
      const auto tag = std::uint8_t{1};
      const ByteView parts[]{detail::raw_native_field(tag)};
      const RawEventRef parent{raw_lane_epoch(), unwrap(raw_emit(9200, parts))};
      const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
      body(context);
    }
    issuer.reset();
  }
  return read_events(fixture.base);
}
void mode(std::string_view selected_mode) {
  const std::string owned{selected_mode};
  require(::setenv("BB_RAW_BEADS_MODE", owned.c_str(), 1) == 0,
          "fixture mode selected before child");
}
Value action(std::string_view operation) {
  if (operation == "create")
    return Value::object({{"op", Value{"create"}}, {"title", Value{"native title"}}});
  if (operation == "ready" || operation == "list" || operation == "cached")
    return Value::object({{"op", Value{std::string{operation}}}});
  auto value = Value::object(
      {{"op", Value{std::string{operation}}}, {"id", Value{"-task;$()"}}});
  if (operation == "update")
    value.object().emplace_back("title", Value{"updated"});
  return value;
}
const Event &find_event(const std::vector<Event> &events, RawEventRef ref,
                        std::uint32_t site) {
  const auto found =
      std::find_if(events.begin(), events.end(), [&](const Event &event) {
        return event.site == site && same(event.ref, ref);
      });
  require(found != events.end(), "actual native referenced site exists");
  return *found;
}
std::optional<RawEventRef> optional_ref(ByteView &body) {
  if (take<bool>(body))
    return reference(body);
  return {};
}
std::optional<Error> owned_error(ByteView &body) {
  if (!take<bool>(body))
    return {};
  const auto code = take<ErrorCode>(body);
  const auto detail = take<std::int64_t>(body);
  return Error{code, detail};
}
void verify_pairs(const std::vector<Event> &events) {
  constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 8> pairs{
      {{3500, 3501},
       {3510, 3512},
       {3520, 3521},
       {3508, 3509},
       {3516, 3517},
       {3524, 3525},
       {3528, 3529},
       {3532, 3533}}};
  std::size_t run_index = 0;
  for (const auto &request : events) {
    const auto pair = std::find_if(pairs.begin(), pairs.end(), [&](const auto &value) {
      return request.site == value.first;
    });
    if (pair == pairs.end())
      continue;
    const Event *terminal = nullptr;
    for (const auto &candidate : events) {
      if (candidate.site != pair->second)
        continue;
      auto body = ByteView{candidate.body};
      if (same(reference(body), request.ref)) {
        require(!terminal && candidate.ref.lane == request.ref.lane &&
                    candidate.ref.ordinal > request.ref.ordinal,
                "one actual end on the same producer after request");
        terminal = &candidate;
      }
    }
    require(terminal, "every published Beads request has its actual terminal");
    auto arguments = ByteView{request.body};
    const auto parent = reference(arguments);
    auto body = ByteView{terminal->body};
    require(same(reference(body), request.ref), "native terminal parent");
    const auto branch = take<RawOperationBranch>(body);
    require(branch <= RawOperationBranch::unfinished, "literal native branch");
    if (request.site == 3500) {
      const auto bound_source = optional_ref(arguments),
                 cache_source = optional_ref(arguments);
      if (bound_source)
        (void)find_event(events, *bound_source, 3520);
      if (cache_source)
        (void)find_event(events, *cache_source, 3520);
      const bool old_bound = take<bool>(arguments);
      const bool old_mutation = take<bool>(arguments);
      require(run_index < run_observations.size(),
              "independent actual run observation");
      const auto &observed = run_observations[run_index++];
      require(old_mutation == observed.before_mutation && (!bound_source || old_bound),
              "run request actual prior mutation/native binding source");
      const auto stage = take<std::uint8_t>(body);
      const bool mutation = take<bool>(body), bound = take<bool>(body);
      require(
          mutation == observed.after_mutation && branch == observed.branch,
          "actual returned/thrown product mutation and exact native exception class");
      if (observed.result && string_field(*observed.result, "status") == "configured")
        require(!bound && stage == 3, "configure actual unbound state/stage");
      const auto status = optional_ref(body);
      if (status) {
        const auto &choice = find_event(events, *status, 3504);
        auto bytes = ByteView{choice.body};
        const auto source = reference(bytes);
        const auto role = take<std::uint8_t>(bytes);
        if (role == 1 || role == 3)
          require(same(source, request.ref), "run-owned status parent");
        else {
          require(role == 2, "actual invocation status role");
          auto invoked = ByteView{find_event(events, source, 3520).body};
          require(same(reference(invoked), request.ref),
                  "run status links actual child invocation");
        }
        require(take<std::size_t>(bytes) == bytes.size(), "run status exact extent");
        const std::string_view value{reinterpret_cast<const char *>(bytes.data()),
                                     bytes.size()};
        if (observed.result)
          require(value == string_field(*observed.result, "status"),
                  "required emitted status equals independent actual returned status");
        require(value == "configured" || value == "cached" || value == "ok" ||
                    value == "unknown" || value == "invalid" || value == "rejected" ||
                    value == "unavailable",
                "actual run status literal");
      }
      require(stage >= 1 && stage <= 10, "run reached business stage");
      const auto error = owned_error(body);
      require(error == observed.error &&
                  error.has_value() == (branch == RawOperationBranch::error),
              "run exact actual Error code/detail only on owned Error exception");
    } else if (request.site == 3510) {
      const auto kind = take<std::uint8_t>(arguments);
      const bool write = take<bool>(arguments);
      require(kind >= 1 && kind <= 4 && (!write || kind == 4),
              "argv native kind/write");
      const auto stage = take<std::uint8_t>(body);
      const auto error = owned_error(body);
      const auto argc = take<std::size_t>(body);
      std::size_t emitted = 0;
      for (const auto &element : events)
        if (element.site == 3511) {
          auto bytes = ByteView{element.body};
          if (same(reference(bytes), request.ref))
            ++emitted;
        }
      require(argc == emitted && stage >= 1 && stage <= 3 &&
                  error.has_value() == (branch == RawOperationBranch::error),
              "argv end exact completed prefix/stage/Error variant");
    } else if (request.site == 3508) {
      require(take<std::uint8_t>(arguments) >= 1, "actual canonical branch");
      const auto owner = optional_ref(body);
      const bool code = take<bool>(body);
      if (owner)
        (void)find_event(events, *owner, 3410);
      if (code) {
        const auto value = take<int>(body);
        const auto category = take<const std::error_category *>(body);
        require(owner && category == (value == 0 ? &std::system_category()
                                                 : &std::generic_category()),
                "actual selected canonical native error category/owner");
        require(value == 0 || value == ENOENT || value == ENOTDIR,
                "fixture canonical exact reached native error");
      }
      const auto error = owned_error(body);
      require(error.has_value() == (branch == RawOperationBranch::error),
              "canonical Error exception remains separate from error_code");
    } else if (request.site == 3516) {
      const auto kind = take<std::uint8_t>(arguments);
      const auto subject = optional_ref(arguments);
      require(kind >= 1 && kind <= 36, "pinned predicate kind");
      if (subject)
        (void)find_event(events, *subject, 3520);
      const bool present = take<bool>(body);
      if (present)
        (void)take<bool>(body);
      const auto error = owned_error(body);
      require(error.has_value() == (branch == RawOperationBranch::error) &&
                  (!present || branch == RawOperationBranch::returned),
              "predicate actual bool absent on failed acquisition");
    } else if (request.site == 3524) {
      require(take<std::size_t>(arguments) == 65536, "actual native collect limit");
      const auto stage = take<std::uint8_t>(body);
      const bool present = take<bool>(body);
      if (present)
        require(take<std::size_t>(body) <= 65536, "actual returned collect bound");
      (void)take<std::size_t>(body);
      const auto exit = take<int>(body);
      const auto error = owned_error(body);
      require(stage == (present ? 2 : 1) &&
                  present == (branch == RawOperationBranch::returned) &&
                  error.has_value() == (branch == RawOperationBranch::error) &&
                  (exit == -1 || exit == 0 || exit == 9),
              "collect actual returned count/stage/exit/Error distinction");
    } else if (request.site == 3528) {
      const auto child = optional_ref(arguments);
      const auto channel = take<std::uint8_t>(arguments);
      const auto length = take<std::size_t>(arguments);
      const bool present = take<bool>(arguments);
      require(child && (channel == 1 || channel == 2) && length > 0,
              "observer actual child/channel/acquired native bytes");
      (void)find_event(events, *child, 1400);
      const auto error = owned_error(body);
      require((present || branch == RawOperationBranch::absent) &&
                  (present || !error) &&
                  error.has_value() == (branch == RawOperationBranch::error),
              "observer absence and actual exception class distinct");
      if (error)
        require(*error == Error{ErrorCode::conflict, 17},
                "actual observer Error code/detail");
    } else if (request.site == 3532) {
      const auto count = take<std::size_t>(arguments);
      const bool present = take<bool>(body), success = take<bool>(body);
      const auto error = owned_error(body);
      std::string acquired;
      const Event *child = nullptr;
      for (const auto &event : events)
        if (event.site == 1400) {
          auto bytes = ByteView{event.body};
          if (same(reference(bytes), parent))
            child = &event;
        }
      require(child, "parser belongs to actual native child");
      for (const auto &event : events)
        if (event.site == 1404) {
          auto bytes = ByteView{event.body};
          const auto source = reference(bytes);
          const auto channel = take<std::uint8_t>(bytes);
          (void)take<std::size_t>(bytes);
          const auto returned = take<ssize_t>(bytes);
          (void)take<int>(bytes);
          if (same(source, child->ref) && channel == 1 && returned > 0)
            acquired.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        }
      const auto expected = parse_json(acquired);
      require(acquired.size() == count && branch == RawOperationBranch::returned &&
                  present && success == expected.has_value() &&
                  error.has_value() == !expected.has_value() &&
                  (!error || *error == expected.error()),
              "parser literal Result/Error matches independent unchanged native bytes");
    } else {
      // Invocation fields are checked against argv, Child and actual collect below.
      arguments = {};
      body = {};
    }
    require(arguments.empty() && body.empty(),
            "literal Beads request/end consumed exactly");
  }
}
void verify_graph(const std::vector<Event> &events, const Fixture &fixture) {
  verify_pairs(events);
  std::vector<std::vector<std::string>> actual_child_argv;
  if (std::filesystem::exists(fixture.base / "calls")) {
    const auto log = read_file(fixture.base / "calls");
    std::size_t start = 0;
    while (start < log.size()) {
      const auto end = log.find('\n', start);
      require(end != std::string::npos, "whole independent child argv observation");
      auto decoded =
          unwrap(parse_json(std::string_view{log}.substr(start, end - start)));
      std::vector<std::string> values;
      for (const auto &value : decoded.array())
        values.push_back(value.string());
      actual_child_argv.push_back(std::move(values));
      start = end + 1;
    }
  }
  std::vector<std::vector<std::string>> selected_child_argv;
  for (const auto &invoke : events) {
    if (invoke.site != 3520)
      continue;
    auto fields = ByteView{invoke.body};
    const auto run = reference(fields);
    const auto prepared = reference(fields);
    const auto kind = take<std::uint8_t>(fields);
    const bool write = take<bool>(fields);
    const auto count = take<std::size_t>(fields);
    require(fields.empty() && same(run, {invoke.ref.lane, run.ordinal}) &&
                run.ordinal < prepared.ordinal && prepared.ordinal < invoke.ref.ordinal,
            "invocation links actual earlier run/preparation on same producer");
    std::vector<std::string> values;
    for (const auto &element : events)
      if (element.site == 3511) {
        auto bytes = ByteView{element.body};
        const auto parent = reference(bytes);
        const auto index = take<std::size_t>(bytes), length = take<std::size_t>(bytes);
        if (!same(parent, prepared))
          continue;
        require(index == values.size() && length == bytes.size() &&
                    element.ref.ordinal < invoke.ref.ordinal,
                "completed argv captured unchanged before invocation and later "
                "acquisitions");
        values.emplace_back(reinterpret_cast<const char *>(bytes.data()), bytes.size());
      }
    require(values.size() == count && !values.empty(),
            "literal native argc agrees with completed elements");
    auto prep_fields = ByteView{find_event(events, prepared, 3510).body};
    require(same(reference(prep_fields), run) &&
                take<std::uint8_t>(prep_fields) == kind &&
                take<bool>(prep_fields) == write && prep_fields.empty(),
            "invocation literal kind/write matches actual argv preparation");
    const bool version =
        std::find(values.begin(), values.end(), "version") != values.end();
    const bool where = std::find(values.begin(), values.end(), "where") != values.end();
    const bool readonly =
        std::find(values.begin(), values.end(), "--readonly") != values.end();
    require(kind >= 1 && kind <= 4 && version == (kind == 1) && where == (kind == 2) &&
                (!write || kind == 4) && (kind != 4 || readonly != write),
            "native selected kind/write independently agrees with actual child argv");
    values.erase(values.begin());
    selected_child_argv.push_back(std::move(values));
    const Event *child_request = nullptr, *cleanup = nullptr, *terminal = nullptr;
    for (const auto &event : events) {
      auto body = ByteView{event.body};
      if (event.site == 1400 && same(reference(body), invoke.ref))
        child_request = &event;
      if (event.site == 3521 && same(reference(body), invoke.ref))
        terminal = &event;
    }
    require(child_request && terminal, "actual Child and invocation terminal exist");
    for (const auto &event : events)
      if (event.site == 1409) {
        auto body = ByteView{event.body};
        if (same(reference(body), child_request->ref))
          cleanup = &event;
      }
    require(cleanup && cleanup->ref.ordinal < terminal->ref.ordinal,
            "native Child cleanup precedes actual invocation terminal");
    auto ending = ByteView{terminal->body};
    require(same(reference(ending), invoke.ref), "invocation terminal request");
    const auto branch = take<RawOperationBranch>(ending);
    const auto stage = take<std::uint8_t>(ending);
    const bool returned_write = take<bool>(ending), mutation = take<bool>(ending),
               spawned = take<bool>(ending);
    const auto exit = take<int>(ending);
    const auto out = take<std::size_t>(ending), err = take<std::size_t>(ending);
    const auto status = optional_ref(ending);
    const auto error = owned_error(ending);
    bool native_spawned = false;
    std::size_t native_out = 0, native_err = 0;
    const Event *collected = nullptr, *caught = nullptr;
    for (const auto &event : events) {
      auto body = ByteView{event.body};
      if (event.site == 1401 && same(reference(body), child_request->ref)) {
        (void)take<std::uint8_t>(body);
        (void)take<int>(body);
        (void)take<int>(body);
        native_spawned = take<pid_t>(body) > 0;
        (void)take<pid_t>(body);
        require(body.empty(), "literal native spawn observation");
      }
      if (event.site == 1404 && same(reference(body), child_request->ref)) {
        const auto channel = take<std::uint8_t>(body);
        (void)take<std::size_t>(body);
        const auto amount = take<ssize_t>(body);
        (void)take<int>(body);
        if (amount > 0) {
          require(static_cast<std::size_t>(amount) == body.size(),
                  "actual native positive bytes");
          if (channel == 1)
            native_out += body.size();
          if (channel == 2)
            native_err += body.size();
        }
      }
      if (event.site == 3525) {
        const auto &collect_request = find_event(events, reference(body), 3524);
        auto request_body = ByteView{collect_request.body};
        if (same(reference(request_body), invoke.ref))
          collected = &event;
      }
      if (event.site == 3530 && same(reference(body), invoke.ref))
        caught = &event;
    }
    require(ending.empty() && returned_write == write && spawned == native_spawned &&
                (!write || mutation) && out <= native_out && err <= native_err &&
                error.has_value() == (branch == RawOperationBranch::error),
            "invocation exact native write/spawn/bytes/Error fields");
    if (branch == RawOperationBranch::returned)
      require(stage == 10 && status,
              "returned invocation completed actual result/status");
    if (status) {
      auto choice = ByteView{find_event(events, *status, 3504).body};
      require(same(reference(choice), invoke.ref) && take<std::uint8_t>(choice) == 2 &&
                  take<std::size_t>(choice) == choice.size(),
              "invocation status source/role/length");
      const std::string_view text{reinterpret_cast<const char *>(choice.data()),
                                  choice.size()};
      require(text == "ok" || text == "unknown" || text == "rejected" ||
                  text == "unavailable",
              "required invocation status literal");
    }
    if (collected) {
      auto result = ByteView{collected->body};
      (void)reference(result);
      const auto outcome = take<RawOperationBranch>(result);
      (void)take<std::uint8_t>(result);
      const bool count_present = take<bool>(result);
      if (count_present)
        require(take<std::size_t>(result) == out,
                "actual collected stdout equals invoke result");
      require(take<std::size_t>(result) == err && take<int>(result) == exit,
              "actual acquired collector stderr/exit reaches invocation unchanged");
      if (outcome == RawOperationBranch::returned)
        require(out == native_out && err == native_err,
                "successful collect preserves exact native byte counts");
    }
    if (caught) {
      auto failed = ByteView{caught->body};
      (void)reference(failed);
      const auto failed_stage = take<std::uint8_t>(failed);
      const auto code = take<ErrorCode>(failed);
      const auto detail = take<std::int64_t>(failed);
      require(take<bool>(failed) == spawned && take<bool>(failed) == write &&
                  failed.empty() && (failed_stage == 4 || failed_stage == 6),
              "caught actual stage/spawn/write");
      if (code == ErrorCode::conflict)
        require(detail == 17 && failed_stage == 6 && spawned && write &&
                    branch == RawOperationBranch::returned && stage == 10,
                "actual observer Error converted only after spawned effect and native "
                "cleanup");
      else
        require(code == ErrorCode::io || code == ErrorCode::interrupted ||
                    code == ErrorCode::capacity,
                "fixture actual caught native failure class");
    }
  }
  require(selected_child_argv == actual_child_argv,
          "independent actual child argv equals every native raw invocation element");
  for (const auto &event : events)
    if (event.site == 3533) {
      auto fields = ByteView{event.body};
      (void)reference(fields);
      const auto branch = take<RawOperationBranch>(fields);
      const bool present = take<bool>(fields), value = take<bool>(fields),
                 error = take<bool>(fields);
      if (error) {
        (void)take<ErrorCode>(fields);
        (void)take<std::int64_t>(fields);
      }
      require(fields.empty() && branch == RawOperationBranch::returned && present &&
                  (value != error),
              "parser actual Result success/failure distinct from thrown exception");
    }
}
void ordinary_matrix(const char *executable) {
  Fixture fixture{executable};
  const auto events = captured(fixture, [&](const RawAdapterContext &context) {
    BeadsAdapter adapter;
    auto run = [&](const Value &arguments) {
      auto result = run_adapter(adapter, arguments, &context);
      require(!result.find("elapsed_us"), "Beads writer does not calculate duration");
      return result;
    };
    require(string_field(run(fixture.config), "status") == "configured",
            "selected configure");
    require(!std::filesystem::exists(fixture.base / "calls"),
            "configure has no probe child");
    for (const auto operation :
         {"ready", "list", "show", "create", "update", "close", "claim"}) {
      require(string_field(run(action(operation)), "status") == "ok",
              "actual supported native command result");
    }
    const auto prior = field(run(action("cached")), "data");
    mode("wrongshape");
    require(string_field(run(action("ready")), "status") == "rejected",
            "native read result shape rejected");
    require(field(run(action("cached")), "data") == prior,
            "failed refresh preserves actual cache");
    mode("wrongid");
    require(string_field(run(action("update")), "status") == "unknown",
            "postwrite wrong target remains unknown");
    mode("wrongassignee");
    require(string_field(run(action("claim")), "status") == "unknown",
            "claim assignment validation remains unknown");
    mode("nonzero");
    require(string_field(run(action("create")), "status") == "unknown",
            "actual nonzero write outcome is unknown");
    mode("binary");
    require(string_field(run(action("create")), "status") == "unknown",
            "actual malformed binary response is unknown");
    mode("normal");
    auto special = action("update");
    special.object().emplace_back("body", Value{"native '\"\n;$()\xC3\xA9"});
    require(string_field(run(special), "status") == "ok",
            "quoted newline UTF8 bytes remain argv data");
    auto maximum = action("create");
    maximum.object().emplace_back("body", Value{std::string(8192, 'm')});
    require(string_field(run(maximum), "status") == "ok",
            "actual8192 argv input accepted");
    const auto before = read_file(fixture.base / "calls");
    auto too_large = action("create");
    too_large.object().emplace_back("body", Value{std::string(8193, 'm')});
    bool invalid = false;
    try {
      (void)run(too_large);
    } catch (const Error &error) {
      invalid = error.code == ErrorCode::invalid_range;
    }
    require(invalid, "actual8193 acquired option rejected before child");
    auto sentinel = action("update");
    sentinel.object().emplace_back("body", Value{"-"});
    require(string_field(run(sentinel), "status") == "invalid",
            "stdin sentinel rejected without child");
    require(read_file(fixture.base / "calls") == before,
            "invalid inputs never invoke later child");
    auto nul =
        Value::object({{"op", Value{"show"}}, {"id", Value{std::string{"a\0b", 3}}}});
    invalid = false;
    try {
      (void)run(nul);
    } catch (const Error &error) {
      invalid = error.code == ErrorCode::invalid_range;
    }
    require(invalid, "embedded NUL ID refused");
    mode("badversion");
    (void)run(fixture.config);
    require(string_field(run(action("ready")), "status") == "unavailable",
            "unsupported version halts lazy binding");
    mode("wrongwhere");
    (void)run(fixture.config);
    require(string_field(run(action("ready")), "status") == "unavailable",
            "actual where mismatch halts action");
  });
  verify_graph(events, fixture);
  bool binary = false, field_nul = false;
  for (const auto &event : events) {
    if (event.site == 1404) {
      auto bytes = ByteView{event.body};
      (void)reference(bytes);
      const auto channel = take<std::uint8_t>(bytes);
      (void)take<std::size_t>(bytes);
      const auto count = take<ssize_t>(bytes);
      (void)take<int>(bytes);
      if (channel == 1 && count == static_cast<ssize_t>(sizeof(stdout_source) - 1) &&
          bytes.size() == sizeof(stdout_source) - 1 &&
          std::memcmp(bytes.data(), stdout_source, bytes.size()) == 0)
        binary = true;
    }
    if (event.site == 3503) {
      auto bytes = ByteView{event.body};
      (void)reference(bytes);
      (void)take<std::uint8_t>(bytes);
      const auto length = take<std::size_t>(bytes);
      require(length == bytes.size(), "literal field extent");
      if (std::find(bytes.begin(), bytes.end(), std::byte{0}) != bytes.end())
        field_nul = true;
    }
  }
  require(binary && !field_nul, "native binary response retained but nonexistent "
                                "invalid acquired ID not fabricated");
}
void observer_errors(const char *executable) {
  for (const auto cut : {0, 1, 2, 3, 4}) {
    Fixture fixture{executable};
    RawOperationBranch expected = RawOperationBranch::error;
    const auto events = captured(fixture, [&](const RawAdapterContext &context) {
      BeadsAdapter adapter;
      (void)run_adapter(adapter, fixture.config, &context);
      (void)run_adapter(adapter, action("ready"), &context);
      mode("binary");
      adapter.observer = [&](std::string_view label, std::string_view) {
        if (label != "beads.stdout")
          return;
        if (cut == 0)
          throw Error{ErrorCode::conflict, 17};
        if (cut == 1)
          throw std::bad_alloc{};
        if (cut == 2)
          throw std::length_error{"actual observer"};
        if (cut == 3)
          throw std::runtime_error{"actual observer"};
        throw 19;
      };
      bool escaped = false;
      try {
        const auto result = run_adapter(adapter, action("create"), &context);
        require(cut == 0 && string_field(result, "status") == "unknown",
                "actual Error converted using spawned write state");
      } catch (const std::bad_alloc &) {
        escaped = cut == 1;
        expected = RawOperationBranch::allocation;
      } catch (const std::length_error &) {
        escaped = cut == 2;
        expected = RawOperationBranch::length;
      } catch (const std::exception &) {
        escaped = cut == 3;
        expected = RawOperationBranch::standard;
      } catch (int) {
        escaped = cut == 4;
        expected = RawOperationBranch::nonstandard;
      }
      require(cut == 0 || escaped, "actual observer exception class preserved");
      require(adapter.mutation_may_have_started() &&
                  read_file(fixture.base / "effect") == "once",
              "conservative mutation and actual effect preserved");
    });
    verify_graph(events, fixture);
    bool original = false, terminal = false;
    for (const auto &event : events) {
      if (event.site == 1404) {
        auto bytes = ByteView{event.body};
        (void)reference(bytes);
        const auto channel = take<std::uint8_t>(bytes);
        (void)take<std::size_t>(bytes);
        const auto count = take<ssize_t>(bytes);
        (void)take<int>(bytes);
        if (channel == 1 && count == static_cast<ssize_t>(sizeof(stdout_source) - 1) &&
            bytes.size() == sizeof(stdout_source) - 1 &&
            std::memcmp(bytes.data(), stdout_source, bytes.size()) == 0)
          original = true;
      }
      if (event.site == 3529) {
        auto fields = ByteView{event.body};
        (void)reference(fields);
        if (take<RawOperationBranch>(fields) == expected)
          terminal = true;
      }
    }
    require(original && terminal,
            "acquired native bytes precede actual failed observer terminal");
  }
}
void state_and_canonical(const char *executable) {
  Fixture fixture{executable};
  const auto events = captured(fixture, [&](const RawAdapterContext &context) {
    BeadsAdapter adapter;
    (void)run_adapter(adapter, fixture.config, &context);
    (void)run_adapter(adapter, action("ready"), &context);
    // Unselected state updates invalidate captured links, even under binding.
    (void)run_adapter(adapter, fixture.config);
    (void)run_adapter(adapter, action("ready"));
    (void)run_adapter(adapter, action("cached"), &context);
    std::error_code error;
    RawEventRef owner{{}, 999};
    const auto missing =
        raw_file_canonical(fixture.base / "missing", error, &context, &owner);
    require(missing.empty() && error && owner.ordinal,
            "actual failed canonical owner exists despite empty path");
    owner.ordinal = 999;
    error.clear();
    (void)raw_file_canonical(fixture.base, error, nullptr, &owner);
    require(!owner.ordinal, "default canonical clears absent native owner");
    auto held = unwrap(raw_claim_control(1024 * 1024 - 102));
    bool refused = false;
    owner.ordinal = 999;
    try {
      (void)raw_file_canonical(fixture.base, error, &context, &owner);
    } catch (const Error &failure) {
      refused = failure.code == ErrorCode::capacity;
    }
    require(refused && !owner.ordinal,
            "canonical request grant refusal leaves no invented owner");
  });
  std::size_t runs = 0;
  for (const auto &event : events)
    if (event.site == 3500) {
      ++runs;
      auto body = ByteView{event.body};
      (void)reference(body);
      const bool bound_source = take<bool>(body);
      if (bound_source)
        (void)reference(body);
      const bool cache_source = take<bool>(body);
      if (cache_source)
        (void)reference(body);
      if (runs == 3)
        require(!bound_source && !cache_source,
                "selected cached read cannot link unselected new state to old source");
    }
  require(runs == 3, "default direct calls emit no Beads graph");
}

void finite_failures(const char *executable) {
  for (const auto kind : {0, 1, 2, 3}) {
    Fixture fixture{executable};
    const auto events = captured(fixture, [&](const RawAdapterContext &context) {
      BeadsAdapter adapter;
      auto config = fixture.config;
      if (kind == 0)
        for (auto &[key, value] : config.object())
          if (key == "executable")
            value = Value{(fixture.base / "missing-bd").string()};
      (void)run_adapter(adapter, config, &context);
      if (kind == 0) {
        const auto result = run_adapter(adapter, action("ready"), &context);
        require(string_field(result, "status") == "unavailable" &&
                    !result.find("spawned"),
                "actual failed version validation returns unavailable without invented "
                "child fields");
        require(!std::filesystem::exists(fixture.base / "calls"),
                "failed spawn never ran external child");
      } else if (kind == 1) {
        adapter.cancelled = [] { return true; };
        const auto result = run_adapter(adapter, action("ready"), &context);
        require(std::get<bool>(field(result, "interrupted").value()) &&
                    !std::get<bool>(field(result, "spawned").value()),
                "actual cancellation before version preserves no-spawn state");
        require(!std::filesystem::exists(fixture.base / "calls"),
                "canceled version prevents where/action");
      } else if (kind == 2) {
        (void)run_adapter(adapter, action("ready"), &context);
        mode("overflow");
        const auto result = run_adapter(adapter, action("create"), &context);
        require(string_field(result, "status") == "unknown" &&
                    adapter.mutation_may_have_started(),
                "real write overflow retains uncertainty");
        require(field(result, "stdout_bytes").number().text() != "65537",
                "business limit does not claim complete overflow");
      } else {
        int sockets[2]{};
        require(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
                "real sticky cancellation endpoints");
        struct Close {
          int *fds;
          ~Close() {
            ::close(fds[0]);
            ::close(fds[1]);
          }
        } close{sockets};
        RawWaitScope wait{{{sockets[0], -1}, {}}};
        adapter.observer = [&](std::string_view label, std::string_view bytes) {
          if (label == "beads.stdout" && bytes.starts_with("{\"path\":"))
            require(::shutdown(sockets[1], SHUT_WR) == 0,
                    "genuine native EOF after acquired where prefix");
        };
        bool interrupted = false;
        try {
          (void)run_adapter(adapter, action("ready"), &context);
        } catch (const Error &error) {
          interrupted = error.code == ErrorCode::interrupted;
        }
        require(interrupted && !adapter.mutation_may_have_started(),
                "sticky capture admission stops before later action");
      }
    });
    if (kind == 2) {
      std::size_t acquired = 0;
      for (const auto &event : events)
        if (event.site == 1404) {
          auto bytes = ByteView{event.body};
          (void)reference(bytes);
          const auto channel = take<std::uint8_t>(bytes);
          (void)take<std::size_t>(bytes);
          const auto count = take<ssize_t>(bytes);
          (void)take<int>(bytes);
          if (channel == 1 && count > 0 && !bytes.empty() &&
              bytes.front() == std::byte{'z'}) {
            require(static_cast<std::size_t>(count) == bytes.size() &&
                        std::all_of(bytes.begin(), bytes.end(),
                                    [](auto byte) { return byte == std::byte{'z'}; }),
                    "literal overflow native prefix extent");
            acquired += bytes.size();
          }
        }
      require(acquired == 65537,
              "all genuinely acquired overflow bytes preserved before product limit");
    }
    if (kind == 0 || kind == 1) {
      std::size_t invokes = 0, caught = 0;
      for (const auto &event : events) {
        if (event.site == 3520)
          ++invokes;
        if (event.site == 3530)
          ++caught;
      }
      require(
          invokes == 1 && caught == 1,
          "unspawned failure emits one reached version invoke and actual caughtError");
    }
    if (kind == 3) {
      const auto calls = read_file(fixture.base / "calls");
      require(calls.find("version") != std::string::npos &&
                  calls.find("where") != std::string::npos &&
                  calls.find("ready") == std::string::npos,
              "actual EOF where failure prevents action invocation");
    }
  }
}
void allocation_boundaries(const char *executable) {
  for (const auto kind : {1}) {
    Fixture fixture{executable};
    const auto events = captured(fixture, [&](const RawAdapterContext &context) {
      BeadsAdapter adapter;
      (void)run_adapter(adapter, fixture.config, &context);
      (void)run_adapter(adapter, action("ready"), &context);
      auto input = action(kind == 1 ? "create" : "ready");
      if (kind == 1)
        input.object().emplace_back("body", Value{std::string(8192, 'b')});
      else
        mode("cache");
      std::array<void *, 4> warm{};
      (void)::backtrace(warm.data(), static_cast<int>(warm.size()));
      selected_allocation = kind;
      selected_allocation_hit = false;
      bool escaped = false;
      try {
        (void)run_adapter(adapter, input, &context);
      } catch (const std::bad_alloc &) {
        escaped = true;
      }
      selected_allocation = 0;
      std::cerr << "actual allocation cut kind=" << kind
                << " bytes=" << selected_allocation_size << '\n';
      require(escaped && selected_allocation_hit,
              "actual selected argv acquisition allocation cut reached");
      if (kind == 1)
        require(!std::filesystem::exists(fixture.base / "effect"),
                "unacquired argv element prevents mutation child");
    });
    if (kind == 1) {
      const Event *failed = nullptr;
      for (const auto &event : events)
        if (event.site == 3512) {
          auto fields = ByteView{event.body};
          (void)reference(fields);
          if (take<RawOperationBranch>(fields) == RawOperationBranch::allocation)
            failed = &event;
        }
      require(failed, "actual incomplete argv preparation reports allocation branch");
      auto body = ByteView{failed->body};
      const auto prep = reference(body);
      (void)take<RawOperationBranch>(body);
      require(take<std::uint8_t>(body) == 2 && !take<bool>(body) &&
                  take<std::size_t>(body) == 5 && body.empty(),
              "failed body acquisition leaves five completed actual elements and no "
              "invented Error");
      std::size_t earlier = 0;
      for (const auto &event : events)
        if (event.site == 3511) {
          auto fields = ByteView{event.body};
          if (same(reference(fields), prep)) {
            require(event.ref.ordinal < failed->ref.ordinal,
                    "prior argv bytes published before failed terminal");
            ++earlier;
          }
        }
      require(earlier == 5, "every completed prefix element preserved and absent "
                            "failed element not fabricated");
    }
  }
}

void cache_sharing(const char *executable) {
  Fixture fixture{executable};
  const auto events = captured(fixture, [&](const RawAdapterContext &context) {
    BeadsAdapter adapter;
    (void)run_adapter(adapter, fixture.config, &context);
    mode("cache");
    const auto returned = run_adapter(adapter, action("ready"), &context);
    const auto cached = run_adapter(adapter, action("cached"), &context);
    const auto &data = field(returned, "data"), &saved = field(cached, "data");
    require(data.array().size() == 1 && saved.array().size() == 1 &&
                &data.value() == &saved.value(),
            "cache assignment shares actual unexposed parsed Storage without a clone "
            "allocation");
  });
  bool assigned = false;
  for (const auto &event : events)
    if (event.site == 3505) {
      auto body = ByteView{event.body};
      (void)reference(body);
      if (take<std::uint8_t>(body) != 2)
        continue;
      require(take<bool>(body), "accepted cache links actual action invocation");
      const auto source = reference(body);
      require(take<bool>(body) && take<std::size_t>(body) == 1 && body.empty(),
              "actual native cache array count retained");
      assigned = std::any_of(events.begin(), events.end(), [&](const Event &candidate) {
        return candidate.site == 3520 && same(candidate.ref, source);
      });
    }
  require(assigned,
          "actual shared cache assignment event preserved after accepted action");
}
void joined_operand(const char *executable) {
  for (const auto cut : {1, 2, 3}) {
    Fixture fixture{executable};
    auto project = fixture.base;
    while (project.native().size() <= static_cast<std::size_t>(PATH_MAX) / 2 + 128) {
      project /= std::string(100, 'j');
      std::filesystem::create_directory(project);
    }
    std::filesystem::create_directory(project / ".beads");
    auto config = fixture.config;
    for (auto &[key, value] : config.object())
      if (key == "project")
        value = Value{project.native()};
    join_operand = project.native();
    join_cut = cut;
    join_cut_hit = false;
    join_cut_ready = false;
    bool refused = false;
    const auto events = captured(fixture, [&](const RawAdapterContext &context) {
      BeadsAdapter adapter;
      try {
        (void)run_adapter(adapter, config, &context);
      } catch (const Error &error) {
        refused = error == Error{ErrorCode::capacity};
      }
      join_cut = 0;
      for (auto &bank : join_holds)
        bank.reset();
      require(refused && join_cut_hit && !adapter.mutation_may_have_started(),
              "actual joined path cut refuses before config assignment/child");
    });
    std::size_t originals = 0, file_helpers = 0;
    bool failed_path = false;
    for (const auto &event : events) {
      if (event.site == 3503) {
        auto bytes = ByteView{event.body};
        (void)reference(bytes);
        if (take<std::uint8_t>(bytes) == 9) {
          require(take<std::size_t>(bytes) == bytes.size() &&
                      std::string_view{reinterpret_cast<const char *>(bytes.data()),
                                       bytes.size()} == (project / ".beads").native(),
                  "completed SAME joined operand original");
          ++originals;
        }
      }
      if (event.site == 3410)
        ++file_helpers;
      if (event.site == 3509) {
        auto bytes = ByteView{event.body};
        (void)reference(bytes);
        if (take<RawOperationBranch>(bytes) == RawOperationBranch::error) {
          const auto owner = optional_ref(bytes);
          require(!take<bool>(bytes),
                  "refused canonical has no acquired native error_code");
          const auto error = owned_error(bytes);
          require(error && *error == Error{ErrorCode::capacity} && bytes.empty(),
                  "actual failed path owner captures capacity");
          require(owner.has_value() == (cut == 2),
                  "actual file begin presence after joined source");
          failed_path = true;
        }
      }
      require(event.site != 3520 && event.site != 1400 && event.site != 3505,
              "join refusal has no assignment/probe/child");
    }
    require(originals == (cut == 3 ? 0U : 1U) && file_helpers == (cut == 2 ? 2U : 1U) &&
                failed_path == (cut != 3),
            "exact source admission/refusal and preserved joined original precede next "
            "owner");
  }
  join_operand.clear();
}
void grant_boundaries(const char *executable) {
  for (const auto allowance : {std::uint64_t{246}, std::uint64_t{245}}) {
    Fixture fixture{executable};
    bool refused = false;
    const auto events = captured(fixture, [&](const RawAdapterContext &context) {
      BeadsAdapter adapter;
      // Literal CTRL: run171 minus its actual44-byte empty-source request leaves
      // 127 held; each reached check pays119 =>246, independent of emitter code.
      auto held = unwrap(raw_claim_control(1024 * 1024 - allowance));
      try {
        require(string_field(run_adapter(adapter, action("ready"), &context),
                             "status") == "unavailable",
                "exact CTRL credit reaches actual unconfigured result");
      } catch (const Error &error) {
        refused = error == Error{ErrorCode::capacity};
      }
    });
    require(refused == (allowance == 245),
            "independent actual held run plus check CTRL exact/max-minus-one");
    bool field = false;
    for (const auto &event : events)
      if (event.site == 3503)
        field = true;
    require(field == (allowance == 246),
            "short CTRL refuses before selected field acquisition");
  }
  for (const auto allowance : {std::uint64_t{8301}, std::uint64_t{8300}}) {
    Fixture fixture{executable};
    bool refused = false;
    const auto events = captured(fixture, [&](const RawAdapterContext &context) {
      BeadsAdapter adapter;
      // Required run status60 plus op's literal49+8192 bounded acquisition.
      auto held = unwrap(raw_claim_data(256 * 1024 * 1024 - allowance));
      try {
        require(string_field(run_adapter(adapter, action("ready"), &context),
                             "status") == "unavailable",
                "exact DATA credit reaches actual unavailable result");
      } catch (const Error &error) {
        refused = error == Error{ErrorCode::capacity};
      }
    });
    require(refused == (allowance == 8300),
            "actual field DATA exact/max-minus-one preserves pre-acquisition refusal");
    bool field = false;
    for (const auto &event : events)
      if (event.site == 3503)
        field = true;
    require(field == (allowance == 8301),
            "no nonexistent field original after DATA refusal");
  }
}

} // namespace
extern "C" char *realpath(const char *input, char *output) {
  if (!native_realpath)
    native_realpath = reinterpret_cast<NativeRealpath>(::dlsym(RTLD_NEXT, "realpath"));
  const auto result = native_realpath(input, output);
  const int saved = errno;
  if (join_cut && result && std::string_view{input} == join_operand)
    join_cut_ready = true;
  errno = saved;
  return result;
}
extern "C" ssize_t read(int fd, void *bytes, std::size_t capacity) {
  if (!native_read)
    native_read = reinterpret_cast<NativeRead>(::dlsym(RTLD_NEXT, "read"));
  const auto count = native_read(fd, bytes, capacity);
  const int saved = errno;
  if (observe && count == static_cast<ssize_t>(sizeof(stderr_source) - 1) &&
      std::memcmp(bytes, stderr_source, sizeof(stderr_source) - 1) == 0) {
    stderr_acquired += static_cast<std::size_t>(count);
    const char grant = 'g';
    if (::write(gate_write, &grant, 1) != 1)
      std::abort();
  }
  if (observe && count == static_cast<ssize_t>(sizeof(stdout_source) - 1) &&
      std::memcmp(bytes, stdout_source, sizeof(stdout_source) - 1) == 0) {
    stdout_acquired += static_cast<std::size_t>(count);
    allocation_armed = true;
  }
  errno = saved;
  return count;
}
void *operator new(std::size_t size) {
  if (selected_allocation_stack(size)) {
    selected_allocation = 0;
    selected_allocation_hit = true;
    selected_allocation_size = size;
    throw std::bad_alloc{};
  }
  if (allocation_armed && actual_original_stack()) {
    allocation_armed = false;
    allocation_hit = true;
    throw std::bad_alloc{};
  }
  if (void *value = std::malloc(size ? size : 1)) {
    if (join_cut && join_cut_ready && !join_cut_hit && !join_cut_busy &&
        canonical_return_stack())
      join_admission_cut();
    return value;
  }
  throw std::bad_alloc{};
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *value) noexcept { std::free(value); }
void operator delete[](void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }
void operator delete[](void *value, std::size_t) noexcept { std::free(value); }
int main(int argc, char **argv) {
  native_read = reinterpret_cast<NativeRead>(::dlsym(RTLD_NEXT, "read"));
  try {
    if (argc > 1 && std::string_view{argv[1]} == "--json")
      return child(argc, argv);
    if (argc > 1 && std::string_view{argv[1]} == "join") {
      joined_operand(argv[0]);
      return 0;
    }
    baseline(argv[0]);
    ordinary_matrix(argv[0]);
    observer_errors(argv[0]);
    state_and_canonical(argv[0]);
    joined_operand(argv[0]);
    finite_failures(argv[0]);
    allocation_boundaries(argv[0]);
    cache_sharing(argv[0]);
    grant_boundaries(argv[0]);
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << " " << error.detail << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
