#include "blackbird/coding.hpp"
#include "blackbird/command_jobs.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/tools.hpp"
#include <atomic>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <poll.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
const char *stage = "start";
thread_local std::size_t allocation_cut = 0;
thread_local bool allocation_hit = false;
std::atomic<pid_t> watched_pid{-1};
std::atomic<int> reap_failures{0};
std::atomic<bool> steal_reap{false};
std::atomic<bool> after_stolen{false};
std::atomic<unsigned> late_signals{0};
std::atomic<bool> watch_input_write{false}, watch_control_read{false};
std::atomic<int> input_descriptor{-1}, output_descriptor{-1};
std::atomic<std::size_t> input_progress{0};
std::atomic<bool> input_refusal{false};
std::atomic<bool> track_space_wait{false}, space_wait_entered{false};
struct RegistrationProbe {
  std::atomic<pid_t> supervisor{-1};
  std::atomic<bool> enabled{false}, entered{false};
};
RegistrationProbe *registration_probe = nullptr;
} // namespace
void *operator new(std::size_t size) {
  if (allocation_cut && allocation_cut == size) {
    allocation_cut = 0;
    allocation_hit = true;
    throw std::bad_alloc{};
  }
  if (void *pointer = std::malloc(size == 0 ? 1 : size))
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
extern "C" pid_t waitpid(pid_t pid, int *status, int options) {
  using Native = pid_t (*)(pid_t, int *, int);
  static auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "waitpid"));
  if (!native)
    std::abort();
  if (pid == watched_pid.load() && options == 0 && reap_failures.fetch_sub(1) > 0) {
    errno = EIO;
    return -1;
  }
  const auto result = native(pid, status, options);
  if (pid == watched_pid.load() && options == 0 && steal_reap.exchange(false) &&
      result == pid) {
    after_stolen.store(true);
    errno = ECHILD;
    return -1;
  }
  return result;
}
extern "C" int poll(pollfd *fds, nfds_t count, int timeout) {
  using Native = int (*)(pollfd *, nfds_t, int);
  static auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "poll"));
  if (!native)
    std::abort();
  if (track_space_wait.load() && count >= 3 && timeout != 0)
    space_wait_entered.store(true);
  if (registration_probe && registration_probe->enabled.load() &&
      ::getpid() == registration_probe->supervisor.load() && count == 2 &&
      timeout == -1)
    registration_probe->entered.store(true);
  return native(fds, count, timeout);
}
extern "C" ssize_t read(int fd, void *bytes, std::size_t count) {
  using Native = ssize_t (*)(int, void *, std::size_t);
  static auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "read"));
  if (!native)
    std::abort();
  const auto result = native(fd, bytes, count);
  const int error = errno;
  if (watch_control_read.load() && count == 8192 && ::isatty(fd))
    output_descriptor.store(fd);
  errno = error;
  return result;
}
extern "C" ssize_t write(int fd, const void *bytes, std::size_t count) {
  using Native = ssize_t (*)(int, const void *, std::size_t);
  static auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "write"));
  if (!native)
    std::abort();
  const auto result = native(fd, bytes, count);
  const int error = errno;
  if (watch_input_write.load() && count && static_cast<const char *>(bytes)[0] == 'V' &&
      ::isatty(fd)) {
    input_descriptor.store(fd);
    if (result > 0)
      input_progress.fetch_add(static_cast<std::size_t>(result));
    else if (result < 0 && error == EAGAIN)
      input_refusal.store(true);
  }
  errno = error;
  return result;
}
extern "C" int kill(pid_t pid, int number) {
  using Native = int (*)(pid_t, int);
  static auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "kill"));
  if (!native)
    std::abort();
  if (after_stolen.load() && (pid == watched_pid.load() || pid == -watched_pid.load()))
    ++late_signals;
  return native(pid, number);
}
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
struct Temporary {
  std::filesystem::path root;
  Temporary() {
    auto path =
        (std::filesystem::temp_directory_path() / "bb-command-raw-XXXXXX").string();
    require(::mkdtemp(path.data()) != nullptr, "temporary directory");
    root = path;
  }
  ~Temporary() {
    if (std::uncaught_exceptions())
      std::cerr << "failed fixture " << root << '\n';
    else
      std::filesystem::remove_all(root);
  }
};
} // namespace
template <class T> T native(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "native record field available");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
using RecordKey = std::pair<IdentityBytes, std::uint64_t>;
struct OwnedRecord {
  std::uint32_t site;
  RawPartition partition;
  std::vector<std::byte> body;
};
using RecordMap = std::map<RecordKey, OwnedRecord>;
RecordKey reference(ByteView &body) {
  const auto lane = native<IdentityBytes>(body);
  return {lane, native<std::uint64_t>(body)};
}
std::string text(ByteView &body) {
  const auto length = native<std::size_t>(body);
  require(length <= body.size(), "literal text length bounded by record");
  std::string result{reinterpret_cast<const char *>(body.data()), length};
  body = body.subspan(length);
  return result;
}
RecordMap records(const std::filesystem::path &session) {
  const auto runs = unwrap(discover_raw_runs(session.string()));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "native oracle run published and closed");
  auto reader =
      unwrap(RawRunReader::open((session / "raw" / runs.front().name).string()));
  std::array<std::byte, 131072> buffer{};
  RecordMap result;
  while (const auto record = unwrap(reader->read(buffer))) {
    auto body = record->raw;
    (void)native<std::uint32_t>(body);
    const auto site = native<std::uint32_t>(body);
    const auto ordinal = native<std::uint64_t>(body);
    require(site != 8001, "selected native operations never fence");
    require(
        result
            .emplace(
                RecordKey{record->origin.lane, ordinal},
                OwnedRecord{site, record->origin.partition, {body.begin(), body.end()}})
            .second,
        "producer ordinal unique across DATA and CONTROL");
  }
  return result;
}
const OwnedRecord &linked(const RecordMap &all, const RecordKey &response,
                          ByteView &body, std::uint32_t site) {
  const auto request = reference(body);
  const auto found = all.find(request);
  require(found != all.end() && found->second.site == site &&
              request.first == response.first && request.second < response.second,
          "response links its actual earlier same-producer native request");
  return found->second;
}
void canceled_exec_receipt(std::string_view fixture, const std::filesystem::path &root,
                           bool after_admission = false) {
  stage = after_admission ? "actual postadmission canceled exec"
                          : "actual canceled exec receipt";
  const auto session =
      root / (after_admission ? "admitted-exec-session" : "canceled-exec-session");
  const auto child =
      root / (after_admission ? "admitted-exec-child" : "canceled-exec-child");
  std::filesystem::create_directory(session);
  std::filesystem::create_directory(child);
  require(::chmod(session.c_str(), 0700) == 0, "private canceled exec session");
  IdentityBytes identity{};
  identity[0] = std::byte{97};
  const auto environment = unwrap(EnvironmentId::from_bytes(identity));
  identity[0] = std::byte{98};
  const auto journal = unwrap(AuditStreamId::from_bytes(identity));
  IdentityBytes caller_lane{};
  {
    // Reach the native cancellation cut after operation17's original/region
    // admission; the former2MiB lane refused before any selected exec effect.
    auto runtime = unwrap(RawRuntime::open(
        session.string(), environment, {{64 * 1024 * 1024, 256 * 1024, 4096}, 4096}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    caller_lane = raw_lane_epoch();
    auto retained = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(session.string()))),
        "audit", {environment, journal, 99, {1024 * 1024, 4 * 1024 * 1024}, {}},
        {32 * 1024 * 1024, 10000}));
    AuditLog log{*retained};
    ContextStore context{log};
    struct UnusedProvider final : CodingProvider {
      Value respond(const Value &,
                    const std::function<void(std::string_view)> &) override {
        throw std::runtime_error("canceled exec must not call provider");
      }
    } provider;
    CodingEngine engine{log, context, provider, "fixture"};
    RawCommandWake wake;
    bool canceled = false;
    if (after_admission) {
      (void)engine.tasks().edit(Value::object(
          {{"op_id", Value{"cancel-admitted"}},
           {"base", Value{Number{0}}},
           {"ops", Value{Value::Array{Value::object(
                       {{"op", Value{"add"}}, {"title", Value{"Native cancel"}}})}}}}));
      engine.task_activity = [&](const Value &activity) {
        if (string_field(activity, "phase") == "running") {
          canceled = true;
          wake.notify();
        }
      };
    }
    engine.cancelled = [&] {
      if (!after_admission && !canceled && std::filesystem::exists(child / "ready")) {
        canceled = true;
        wake.notify();
      }
      return canceled;
    };
    const std::string program = "arco.call('exec', {" +
                                std::string{after_admission ? "task_id='t1'," : ""} +
                                "argv={'" + std::string{fixture} + "','gated-flood','" +
                                child.string() + "'},timeout_seconds=10})";
    bool interrupted = false;
    {
      RawWaitScope wait{{{wake.read_fd(), -1}, {}}};
      try {
        engine.turn({"", program});
      } catch (const Error &error) {
        std::cerr << "canceled exec phase=" << after_admission
                  << " actual Error=" << error_name(error.code) << ':' << error.detail
                  << " canceled=" << canceled << '\n';
        interrupted = error.code == ErrorCode::interrupted;
      }
    }
    require(canceled && interrupted,
            "actual native canceled exec preserves interrupted disposition");
    require(retained->state() == JournalWriterState::live,
            "canceled exec receipt does not block semantic audit");
    unsigned receipts = 0, terminals = 0;
    std::optional<OperationAttemptId> attempt, terminal_attempt;
    for (std::size_t n = 0; n < retained->fact_count(); ++n) {
      const auto fact = unwrap(retained->fact(n));
      if (const auto *receipt = std::get_if<AdapterReceiptEvent>(&fact.event.body)) {
        ++receipts;
        attempt = receipt->attempt;
        require(receipt->failure && receipt->failure->code == ErrorCode::interrupted,
                "actual caller receipt retains interrupted adapter return");
      }
      if (const auto *terminal = std::get_if<AttemptObservationEvent>(&fact.event.body);
          terminal && terminal->phase == AttemptPhase::terminal) {
        ++terminals;
        require(terminal->disposition == AttemptDisposition::unknown,
                "command stop remains actual unknown effect");
        terminal_attempt = terminal->attempt;
      }
    }
    require(receipts == 1 && terminals == 1 && attempt == terminal_attempt,
            "one actual receipt and command terminal for the same native attempt");
    require(unwrap(retained->unresolved_attempts()).empty(),
            "actual canceled admission leaves no unfinished attempt");
    if (after_admission) {
      if (!attempt.has_value())
        throw std::runtime_error("actual receipt attempt available for open check");
      require(unwrap(retained->attempt(*attempt)).opened,
              "postadmission cut reaches actual adapter open before body refusal");
      require(!std::filesystem::exists(child / "ready") &&
                  engine.command_jobs()->debt() == 0,
              "postadmission cancellation acquires no native child");
    }
    require(detail::owned_children.load() == 0, "canceled native command reaped");
    canceled = false;
    unwrap(wake.clear());
    engine.cancelled = [] { return false; };
    engine.turn({"", "return 73"});
    require(engine.workflow_result() == Value{Number{73}},
            "next actual Lua turn succeeds after canceled exec");
    engine.shutdown_commands();
  }
  const auto all = records(session);
  unsigned caller_receipt_regions = 0, successful_settlements = 0;
  for (const auto &[key, record] : all) {
    if (record.site == raw_regions::retained_append.begin &&
        record.partition == RawPartition::control && key.first == caller_lane)
      ++caller_receipt_regions;
    if (record.site == 2036) {
      ByteView body{record.body};
      (void)linked(all, key, body, 2035);
      require(native<std::uint8_t>(body) == 1 && key.first != caller_lane,
              "joined worker owns actual command terminal settlement");
      ++successful_settlements;
    }
  }
  std::cerr << "phase=" << after_admission
            << " caller prepaid regions=" << caller_receipt_regions
            << " worker settlements=" << successful_settlements << '\n';
  // The current operation pays its ordered admission/open as well as the
  // actual later caller appends. Both cuts emit four caller CONTROL regions;
  // only the launched command has a distinct joined-worker terminal settlement.
  require(caller_receipt_regions == 4 &&
              successful_settlements == (after_admission ? 0u : 1u),
          "admission/open/receipt use caller prepaid regions; command terminal uses "
          "worker lane");
  if (after_admission) {
    for (const auto &[key, record] : all) {
      (void)key;
      require(record.site != 2000 && record.site != 2002,
              "actual canceled admission emits no launch request");
    }
    auto reopened = unwrap(RetainedState::open(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(session.string()))),
        "audit", {environment, journal, 99, {1024 * 1024, 4 * 1024 * 1024}, {}},
        {32 * 1024 * 1024, 10000}));
    unwrap(reopened->confirm_recovery());
    recover_coding_session(*reopened);
    require(reopened->state() == JournalWriterState::live &&
                unwrap(reopened->unresolved_attempts()).empty(),
            "ordinary native reopen grants no retry and needs no acknowledgment");
  }
}
void live_recorder(const std::filesystem::path &root) {
  const auto session = root / "live-recorder";
  std::filesystem::create_directory(session);
  require(::chmod(session.c_str(), 0700) == 0, "private lifecycle session");
  IdentityBytes identity{};
  identity[0] = std::byte{92};
  std::array<IdentityBytes, 2> epochs{};
  const std::array<std::string_view, 2> originals{"first original", "second original"};
  {
    stage = "live recorder lifecycle";
    auto runtime = unwrap(RawRuntime::open(session.string(),
                                           unwrap(EnvironmentId::from_bytes(identity)),
                                           {{16384, 16384, 4096}, 4096}));
    const auto recorder = unwrap(runtime->status()).recorder;
    for (std::size_t index = 0; index < originals.size(); ++index) {
      auto context = unwrap(runtime->prepare_thread());
      require(unwrap(runtime->status()).recorder == recorder,
              "addition keeps the actual recorder alive");
      {
        RawThreadBinding binding{context};
        epochs[index] = raw_lane_epoch();
        const std::array parts{
            std::as_bytes(std::span{originals[index].data(), originals[index].size()})};
        unwrap(raw_emit(60000, parts));
      }
      context.reset();
      require(unwrap(runtime->status()).recorder == recorder,
              "retirement keeps the actual recorder alive");
    }
  }
  const auto runs = unwrap(discover_raw_runs(session.string()));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "lifecycle run closes with published originals");
  auto reader =
      unwrap(RawRunReader::open((session / "raw" / runs.front().name).string()));
  std::array<bool, 2> found{};
  std::array<std::byte, 128> output{};
  while (auto record = unwrap(reader->read(output))) {
    auto body = record->raw;
    (void)native<std::uint32_t>(body);
    const auto site = native<std::uint32_t>(body);
    (void)native<std::uint64_t>(body);
    if (site != 60000)
      continue;
    for (std::size_t index = 0; index < originals.size(); ++index)
      if (record->origin.lane == epochs[index]) {
        require(!found[index] && body.size() == originals[index].size() &&
                    std::memcmp(body.data(), originals[index].data(), body.size()) == 0,
                "successor preserves the exact original from each retired epoch");
        found[index] = true;
      }
  }
  require(found[0] && found[1], "both retired producer originals remain readable");
}
void registration_recorder_death(const std::filesystem::path &root) {
  auto *shared = ::mmap(nullptr, sizeof(RegistrationProbe), PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANON, -1, 0);
  require(shared != MAP_FAILED, "native shared registration observation");
  registration_probe = new (shared) RegistrationProbe;
  struct Release {
    ~Release() {
      registration_probe->~RegistrationProbe();
      ::munmap(registration_probe, sizeof(RegistrationProbe));
      registration_probe = nullptr;
    }
  } release;
  const auto session = root / "registration-death";
  std::filesystem::create_directory(session);
  require(::chmod(session.c_str(), 0700) == 0, "private registration death session");
  IdentityBytes identity{};
  identity[0] = std::byte{93};
  const std::string_view original{"original acquired while recorder stopped"};
  IdentityBytes epoch{};
  {
    stage = "recorder death during registration";
    auto runtime = unwrap(RawRuntime::open(session.string(),
                                           unwrap(EnvironmentId::from_bytes(identity)),
                                           {{16384, 16384, 4096}, 4096}));
    auto context = unwrap(runtime->prepare_thread());
    const auto before = unwrap(runtime->status());
    const auto recorder = before.recorder;
    registration_probe->supervisor.store(before.supervisor);
    require(::kill(recorder, SIGSTOP) == 0, "stop actual owned recorder");
    struct Resume {
      pid_t pid;
      ~Resume() {
        if (pid > 0)
          ::kill(pid, SIGCONT);
      }
    } resume{recorder};
    {
      RawThreadBinding binding{context};
      epoch = raw_lane_epoch();
      const std::array parts{
          std::as_bytes(std::span{original.data(), original.size()})};
      unwrap(raw_emit(60001, parts));
    }
    registration_probe->enabled.store(true);
    std::optional<Result<RawThreadHandle>> result;
    std::jthread registration{[&] { result.emplace(runtime->prepare_thread()); }};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!registration_probe->entered.load() &&
           std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    const bool entered = registration_probe->entered.load();
    // Always release the stopped child before any assertion or thread join.
    const auto killed = ::kill(recorder, SIGKILL);
    if (killed == 0 || errno == ESRCH)
      resume.pid = -1;
    registration_probe->enabled.store(false);
    registration.join();
    require(entered && killed == 0, "kill actual recorder after queued native RPC");
    if (result && !result->has_value())
      std::cerr << "registration death return "
                << static_cast<unsigned>(result->error().code) << ':'
                << result->error().detail << '\n';
    else
      std::cerr << "registration death returned a context\n";
    require(result && !result->has_value() &&
                result->error().code == ErrorCode::audit_unavailable,
            "native recorder death refuses an unacknowledged registration");
    unwrap(runtime->recover());
    const auto recovered = unwrap(runtime->status());
    require(!recovered.fenced && recovered.recorder != recorder,
            "explicit recovery owns a new actual recorder after death");
    auto subsequent = unwrap(runtime->prepare_thread());
    require(unwrap(runtime->status()).recorder == recovered.recorder,
            "addition after full death recovery uses its live recorder");
    subsequent.reset();
    context.reset();
  }
  const auto runs = unwrap(discover_raw_runs(session.string()));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "death recovery preserves and closes all registered originals");
  auto reader =
      unwrap(RawRunReader::open((session / "raw" / runs.front().name).string()));
  std::array<std::byte, 256> output{};
  bool found = false;
  while (const auto record = unwrap(reader->read(output))) {
    auto body = record->raw;
    (void)native<std::uint32_t>(body);
    const auto site = native<std::uint32_t>(body);
    (void)native<std::uint64_t>(body);
    if (site == 60001) {
      require(!found && record->origin.lane == epoch &&
                  body.size() == original.size() &&
                  std::memcmp(body.data(), original.data(), body.size()) == 0,
              "full death recovery preserves the exact stopped-recorder original");
      found = true;
    }
  }
  require(found, "pending original retained through native recorder death");
}
void raw_oracles(std::string_view fixture, const std::filesystem::path &root) {
  const auto session = root / "raw-session";
  std::filesystem::create_directory(session);
  require(::chmod(session.c_str(), 0700) == 0, "private raw session");
  IdentityBytes identity{};
  identity[0] = std::byte{91};
  const auto environment = unwrap(EnvironmentId::from_bytes(identity));
  const std::string id(32, '2'), custody_id(32, '3'), limit_id(32, '4');
  std::string acquired;
  {
    stage = "runtime open";
    auto runtime = unwrap(RawRuntime::open(session.string(), environment,
                                           {{2 * 1024 * 1024, 64 * 1024, 4096}, 4096}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    CommandJobs jobs;
    bool malformed_refused = false;
    try {
      jobs.start(std::string(32, 'a'),
                 Value::object({{"timeout_seconds", Value{Number{0}}}}));
    } catch (const Error &error) {
      malformed_refused = error.code == ErrorCode::invalid_range;
    }
    require(malformed_refused, "malformed launch refused before native effect");
    auto launch = [&](const std::string &job, const char *mode,
                      const std::filesystem::path &directory, bool pty = false) {
      std::filesystem::create_directory(directory);
      jobs.start(job, Value::object(
                          {{"argv",
                            Value{Value::Array{Value{std::string{fixture}}, Value{mode},
                                               Value{directory.string()}}}},
                           {"pty", Value{pty}},
                           {"timeout_seconds", Value{Number{10}}}}));
    };
    stage = "binary launch";
    launch(id, "binary", root / "binary");
    JobEvent terminal;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (terminal.job_id.empty()) {
      require(std::chrono::steady_clock::now() < deadline,
              "raw worker completion bounded");
      for (auto &event : jobs.drain()) {
        if (event.bytes) {
          acquired += *event.bytes;
          jobs.retained(id, event.offset + event.bytes->size());
        } else
          terminal = event;
      }
      std::this_thread::yield();
    }
    require(acquired == std::string("A\0B\xff\xe2\x82\xac\n", 8),
            "raw-selected binary bytes unchanged");
    RawCommandWake canceled;
    canceled.notify();
    {
      RawWaitScope wait{{{canceled.read_fd(), -1}, {}}};
      stage = "canceled semantic settlement";
      jobs.settlement(terminal, [] {
        for (unsigned n = 0; n < 4; ++n) {
          // One fixed observation plus these three declared event observations
          // keeps each existing held burst exactly432 bytes on supportedABIs.
          RawRegion region{raw_regions::retained_append, {.retained_events = 3}};
          for (unsigned observation = 0; observation < 4; ++observation)
            region.observe({RawObservationKind::journal_state, n, observation, n});
          region.error({ErrorCode::io, EIO});
          region.outcome(RawRegionOutcome::success);
        }
      });
    }
    require(jobs.debt() == 0, "four prepaid regions settle after sticky cancellation");
    const auto base_children = detail::owned_children.load();
    stage = "custody launch";
    launch(custody_id, "gate", root / "custody");
    while (!std::filesystem::exists(root / "custody" / "ready")) {
      require(std::chrono::steady_clock::now() < deadline, "native child gate ready");
      std::this_thread::yield();
    }
    const auto snapshot = jobs.read(
        Value::object({{"job_id", Value{custody_id}}, {"count", Value{Number{0}}}}));
    watched_pid.store(
        static_cast<pid_t>(std::stol(field(snapshot, "pid").number().text())));
    reap_failures.store(1);
    (void)jobs.control(
        Value::object({{"op", Value{"stop"}}, {"job_id", Value{custody_id}}}));
    bool offered = false;
    while (!offered) {
      require(std::chrono::steady_clock::now() < deadline,
              "failed final reap terminal offered");
      for (auto &event : jobs.drain()) {
        if (event.bytes)
          jobs.retained(event.job_id, event.offset + event.bytes->size());
        else if (event.job_id == custody_id) {
          jobs.settled(event.job_id, event.result);
          offered = true;
        }
      }
      std::this_thread::yield();
    }
    require(jobs.debt() == 1 && detail::owned_children.load() == base_children + 1,
            "failed native reap keeps child custody and refit debt after semantic "
            "terminal");
    bool refused_archive = false;
    try {
      (void)jobs.control(
          Value::object({{"op", Value{"archive"}}, {"job_id", Value{custody_id}}}));
    } catch (const Error &error) {
      refused_archive = error.code == ErrorCode::busy;
    }
    require(refused_archive, "pending native custody cannot archive");
    stage = "custody retry";
    jobs.shutdown();
    require(jobs.debt() == 0 && detail::owned_children.load() == base_children,
            "joined owner retry obtains actual reap and releases custody once");
    watched_pid.store(-1);
    stage = "lost child custody";
    const std::string lost_id(32, '5');
    launch(lost_id, "gate", root / "lost");
    while (!std::filesystem::exists(root / "lost" / "ready")) {
      require(std::chrono::steady_clock::now() < deadline, "lost child gate ready");
      std::this_thread::yield();
    }
    const auto lost_snapshot = jobs.read(
        Value::object({{"job_id", Value{lost_id}}, {"count", Value{Number{0}}}}));
    watched_pid.store(
        static_cast<pid_t>(std::stol(field(lost_snapshot, "pid").number().text())));
    steal_reap.store(true);
    (void)jobs.control(
        Value::object({{"op", Value{"stop"}}, {"job_id", Value{lost_id}}}));
    bool lost_offered = false;
    while (!lost_offered) {
      require(std::chrono::steady_clock::now() < deadline,
              "lost child terminal bounded");
      for (auto &event : jobs.drain()) {
        if (event.bytes)
          jobs.retained(event.job_id, event.offset + event.bytes->size());
        else if (event.job_id == lost_id) {
          jobs.settled(event.job_id, event.result);
          lost_offered = true;
        }
      }
      std::this_thread::yield();
    }
    jobs.shutdown();
    require(after_stolen.load() && late_signals.load() == 0 &&
                detail::owned_children.load() == base_children && jobs.debt() == 0,
            "ECHILD forbids subsequent PID signals and clears actual parent custody "
            "exactly once");
    after_stolen.store(false);
    watched_pid.store(-1);
    stage = "limit crossing";
    launch(limit_id, "limit-cross", root / "limit");
    std::string crossing;
    bool completed = false;
    while (!completed) {
      require(std::chrono::steady_clock::now() < deadline,
              "crossing-limit bytes bounded");
      for (auto &event : jobs.drain()) {
        if (event.bytes) {
          crossing += *event.bytes;
          jobs.retained(event.job_id, event.offset + event.bytes->size());
        } else if (event.job_id == limit_id) {
          require(field(event.result, "capture_complete") == Value{false},
                  "crossing chunk marks incomplete capture");
          jobs.settled(event.job_id, event.result);
          completed = true;
        }
      }
      std::this_thread::yield();
    }
    require(crossing.size() == 16 * 1024 * 1024 + 4 &&
                crossing.substr(crossing.size() - 4) == std::string("X\0Y\xff", 4),
            "actual crossing-limit acquisition remains semantically owned");
    jobs.shutdown();
  }
  stage = "disk reader";
  const auto runs = unwrap(discover_raw_runs(session.string()));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "raw run preserved and closed");
  auto reader =
      unwrap(RawRunReader::open((session / "raw" / runs.front().name).string()));
  std::vector<std::byte> buffer(65536);
  std::string original;
  unsigned prepaid_begins = 0, failed_reaps = 0, successful_reaps = 0,
           malformed_returns = 0;
  RecordMap all;
  while (const auto record = unwrap(reader->read(buffer))) {
    auto body = record->raw;
    (void)native<std::uint32_t>(body);
    const auto site = native<std::uint32_t>(body);
    const auto ordinal = native<std::uint64_t>(body);
    require(all.emplace(RecordKey{record->origin.lane, ordinal},
                        OwnedRecord{
                            site, record->origin.partition, {body.begin(), body.end()}})
                .second,
            "raw binary producer ordinal unique across partitions");
    require(site != 8001, "bounded command hooks did not fence emitter");
    if (site == 2008) {
      body = body.subspan(24);
      const auto result = native<ssize_t>(body);
      (void)native<int>(body);
      if (result > 0)
        original.append(reinterpret_cast<const char *>(body.data()),
                        static_cast<std::size_t>(result));
    } else if (site == 2001 && body.size() == 50) {
      body = body.subspan(24);
      const auto branch = native<std::uint8_t>(body);
      const auto code = native<ErrorCode>(body);
      if (branch == 2 && code == ErrorCode::invalid_range)
        ++malformed_returns;
    } else if (site == 1090) {
      require(record->origin.partition == RawPartition::control,
              "prepaid region begin emits CONTROL");
      ++prepaid_begins;
    } else if (site == 2033) {
      body = body.subspan(24);
      const auto result = native<pid_t>(body);
      const auto error = native<int>(body);
      const auto valid = native<bool>(body);
      if (result < 0 && error == EIO && !valid)
        ++failed_reaps;
      if (result > 0 && valid)
        ++successful_reaps;
    }
  }
  std::map<IdentityBytes, std::size_t> offsets;
  std::map<std::string, std::string> exact;
  for (const auto &[key, record] : all) {
    if (record.site != 2008)
      continue;
    ByteView body{record.body};
    const auto &request = linked(all, key, body, 2007);
    require(request.partition == RawPartition::control,
            "read request uses prepaid CONTROL");
    ByteView fields{request.body};
    const auto launch = reference(fields);
    require(launch.first == key.first && launch.second < key.second &&
                all.at(launch).site == 2002,
            "native read retains actual worker launch identity and order");
    ByteView launched{all.at(launch).body};
    (void)reference(launched);
    const auto job = text(launched);
    require(native<int>(fields) >= 0,
            "binary acquisition captures actual native descriptor");
    const auto offset = native<std::size_t>(fields),
               requested = native<std::size_t>(fields);
    const auto count = native<ssize_t>(body);
    (void)native<int>(body);
    require(requested == 8192 && fields.empty() && offset == offsets[key.first],
            "binary read request records actual native extent and byte offset");
    if (count > 0) {
      require(body.size() == static_cast<std::size_t>(count) &&
                  body.size() <= requested && record.partition == RawPartition::data,
              "binary DATA original is exactly native returned bytes");
      offsets[key.first] += body.size();
      exact[job].append(reinterpret_cast<const char *>(body.data()), body.size());
    } else
      require(body.empty() && record.partition == RawPartition::control,
              "nonpositive native read has an exact empty CONTROL result");
  }
  require(exact[id] == acquired && exact[limit_id].size() == 16 * 1024 * 1024 + 4 &&
              exact[limit_id].ends_with(std::string("X\0Y\xff", 4)),
          "linked read requests preserve literal selected binary and crossing-limit "
          "originals");
  require(original.starts_with(acquired) &&
              original.ends_with(std::string("X\0Y\xff", 4)),
          "disk originals include literal binary and crossing-limit bytes");
  require(malformed_returns == 1 && prepaid_begins == 4 && failed_reaps == 1 &&
              successful_reaps == 3,
          "prepaid region and failure/actual reap records are exact");
}
void api_projection_cuts(std::string_view fixture, const std::filesystem::path &root) {
  stage = "actual API projection allocation";
  const auto session = root / "api-session", child = root / "api-child";
  std::filesystem::create_directory(session);
  std::filesystem::create_directory(child);
  require(::chmod(session.c_str(), 0700) == 0, "private API session");
  IdentityBytes identity{};
  identity[0] = std::byte{94};
  {
    auto runtime = unwrap(RawRuntime::open(session.string(),
                                           unwrap(EnvironmentId::from_bytes(identity)),
                                           {{65536, 65536, 4096}, 4096}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    CommandJobs jobs;
    auto configure = Value::object({{"op", Value{"configure"}},
                                    {"max_live", Value{Number{1}}},
                                    {"max_records", Value{Number{4}}}});
    allocation_hit = false;
    allocation_cut = sizeof(Value::Object::value_type);
    bool failed = false;
    try {
      (void)jobs.control(configure);
    } catch (const std::bad_alloc &) {
      failed = true;
    }
    allocation_cut = 0;
    require(failed && allocation_hit, "actual configure return allocation fails");
    require(raw_try_claim_control(16).has_value(),
            "post-effect projection failure does not fence producer");
    const std::string id(32, '7');
    const auto launch = Value::object(
        {{"argv", Value{Value::Array{Value{std::string{fixture}}, Value{"gate"},
                                     Value{child.string()}}}},
         {"timeout_seconds", Value{Number{10}}}});
    jobs.start(id, launch);
    bool capacity = false;
    try {
      jobs.start(std::string(32, '8'), launch);
    } catch (const Error &error) {
      capacity = error.code == ErrorCode::capacity;
    }
    require(capacity, "accepted configure effect survives failed return projection");
    allocation_hit = false;
    allocation_cut = 11 * sizeof(Value::Object::value_type);
    failed = false;
    try {
      (void)jobs.wait(id, {0}, [] {}, [] { return false; });
    } catch (const std::bad_alloc &) {
      failed = true;
    }
    allocation_cut = 0;
    require(failed && allocation_hit, "actual wait snapshot allocation fails");
    for (bool in_tick : {true, false}) {
      bool thrown = false;
      try {
        (void)jobs.wait(
            id, {0},
            [=] {
              if (in_tick)
                throw std::runtime_error("actual tick failure");
            },
            [=] {
              if (!in_tick)
                throw std::runtime_error("actual cancellation callback failure");
              return false;
            });
      } catch (const std::runtime_error &) {
        thrown = true;
      }
      require(thrown, "actual owner callback exception propagates");
    }
    jobs.shutdown();
    for (auto &event : jobs.drain()) {
      if (event.bytes)
        jobs.retained(id, event.offset + event.bytes->size());
      else
        jobs.settled(id, event.result);
    }
    for (auto &event : jobs.drain())
      if (!event.bytes)
        jobs.settled(id, event.result);
  }
  const auto all = records(session);
  unsigned control_failures = 0, effects = 0, wait_failures = 0, wait_requests = 0;
  std::map<RecordKey, unsigned> terminals;
  for (const auto &[key, record] : all) {
    ByteView body{record.body};
    if (record.site == 2024)
      ++wait_requests;
    if (record.site == 2014) {
      const auto request_key = reference(body);
      require(++terminals[request_key] == 1, "configure API terminal single-use");
      body = record.body;
      const auto &request = linked(all, key, body, 2013);
      ByteView fields{request.body};
      require(text(fields).empty() && text(fields) == "configure",
              "exception links actual configure request");
      require(native<std::uint8_t>(body) == 4, "configure API reports exception once");
      (void)native<std::size_t>(body);
      require(!native<bool>(body) && native<bool>(body), "failed API projection flags");
      require(native<ErrorCode>(body) == ErrorCode::allocation,
              "native allocation error");
      (void)native<std::int64_t>(body);
      require(body.empty(), "configure exception body exact");
      ++control_failures;
    } else if (record.site == 2045) {
      const auto request_key = reference(body);
      const auto length = native<std::size_t>(body);
      body = body.subspan(length);
      if (native<std::uint8_t>(body) == 1) {
        require(
            all.at(request_key).site == 2013 && request_key.first == key.first &&
                request_key.second < key.second && native<std::uint8_t>(body) == 1,
            "accepted configure effect links actual request and reports success once");
        ++effects;
      }
    } else if (record.site == 2026) {
      const auto request_key = reference(body);
      require(++terminals[request_key] == 1, "wait exception terminal single-use");
      body = record.body;
      const auto &request = linked(all, key, body, 2024);
      ByteView fields{request.body};
      require(text(fields) == std::string(32, '7') && native<bool>(fields) &&
                  native<std::uint64_t>(fields) == 0,
              "exception links exact actual wait request");
      (void)native<std::uint64_t>(body);
      require(native<std::uint8_t>(body) == 6,
              "wait exception reports failure terminal");
      const auto error = native<ErrorCode>(body);
      require(error == (wait_failures == 0 ? ErrorCode::allocation
                                           : ErrorCode::external_unknown),
              "wait terminal preserves actual exception class");
      (void)native<std::int64_t>(body);
      (void)native<bool>(body);
      (void)native<bool>(body);
      (void)native<bool>(body);
      require(body.empty(), "wait exception terminal body exact");
      ++wait_failures;
    }
  }
  require(
      control_failures == 1 && effects == 1 && wait_failures == 3 && wait_requests == 3,
      "one linked API terminal per actual exception; accepted effect remains distinct");
}

void native_controls(std::string_view fixture, const std::filesystem::path &root) {
  stage = "native PTY control fields";
  const auto session = root / "controls-session", child = root / "controls-child";
  std::filesystem::create_directory(session);
  std::filesystem::create_directory(child);
  require(::chmod(session.c_str(), 0700) == 0, "private controls session");
  IdentityBytes identity{};
  identity[0] = std::byte{95};
  const std::string id(32, '9'), payload(65536, 'V');
  pid_t child_pid = -1;
  std::uint64_t foreground_epoch = 0;
  {
    auto runtime = unwrap(RawRuntime::open(session.string(),
                                           unwrap(EnvironmentId::from_bytes(identity)),
                                           {{2 * 1024 * 1024, 65536, 4096}, 4096}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    CommandJobs jobs;
    input_descriptor.store(-1);
    output_descriptor.store(-1);
    watch_control_read.store(true);
    struct EndObservation {
      ~EndObservation() {
        watch_control_read.store(false);
        watch_input_write.store(false);
      }
    } end_observation;
    jobs.start(id,
               Value::object({{"argv", Value{Value::Array{Value{std::string{fixture}},
                                                          Value{"gated-raw-input"},
                                                          Value{child.string()}}}},
                              {"pty", Value{true}},
                              {"timeout_seconds", Value{Number{10}}}}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{7};
    auto bounded = [&] {
      require(std::chrono::steady_clock::now() < deadline,
              "native control cut bounded");
      std::this_thread::yield();
    };
    while (!std::filesystem::exists(child / "ready"))
      bounded();
    child_pid = static_cast<pid_t>(
        std::stol(field(jobs.read(Value::object(
                            {{"job_id", Value{id}}, {"count", Value{Number{0}}}})),
                        "pid")
                      .number()
                      .text()));
    auto delivery = [&](const char *op, Value::Object extra, const char *request_id) {
      extra.emplace_back("op", Value{op});
      extra.emplace_back("job_id", Value{id});
      extra.emplace_back("request_id", Value{request_id});
      return Value::object(std::move(extra));
    };
    auto resize = delivery(
        "resize", {{"rows", Value{Number{37}}}, {"columns", Value{Number{113}}}},
        "actual-resize");
    (void)jobs.control(resize);
    while (field(jobs.control(resize), "status") != Value{"delivered"})
      bounded();
    for (const char *name : {"stop", "continue"}) {
      auto signal = delivery("signal", {{"signal", Value{name}}}, name);
      (void)jobs.control(signal);
      while (field(jobs.control(signal), "status") != Value{"delivered"})
        bounded();
      if (std::string_view{name} == "stop")
        while (field(jobs.read(Value::object(
                         {{"job_id", Value{id}}, {"count", Value{Number{0}}}})),
                     "state") != Value{"stopped"})
          bounded();
    }
    auto context = unwrap(runtime->prepare_thread());
    std::atomic<bool> entered{false};
    std::exception_ptr wait_error;
    Value wait_result;
    std::thread waiter{[&, context = std::move(context)] {
      try {
        RawThreadBinding wait_binding{context};
        wait_result =
            jobs.wait(id, {5000}, [&] { entered.store(true); }, [] { return false; });
      } catch (...) {
        wait_error = std::current_exception();
      }
    }};
    // On any failing assertion, preserve actual child/thread cleanup before unwinding.
    struct Join {
      std::thread &thread;
      CommandJobs &jobs;
      ~Join() {
        if (thread.joinable()) {
          jobs.stop_all();
          thread.join();
        }
      }
    } join{waiter, jobs};
    while (!entered.load())
      bounded();
    const auto foreground = jobs.foreground();
    require(field(foreground, "job_id") == Value{id} &&
                field(foreground, "pty") == Value{true},
            "actual foreground lease belongs to live PTY");
    foreground_epoch = std::stoull(field(foreground, "wait_epoch").number().text());
    auto background = Value::object({{"op", Value{"background"}},
                                     {"job_id", Value{id}},
                                     {"wait_epoch", Value{Number{foreground_epoch}}}});
    require(field(jobs.control(background), "released") == Value{true},
            "background releases actual foreground lease");
    waiter.join();
    if (wait_error)
      std::rethrow_exception(wait_error);
    require(field(wait_result, "leader_exited") == Value{false} &&
                field(wait_result, "output_closed") == Value{false} &&
                ::kill(child_pid, 0) == 0,
            "foreground release leaves actual child running and output open");
    require(field(jobs.control(background), "released") == Value{false},
            "stale released lease does not release a new owner");
    auto input = delivery("input", {{"bytes", Value{payload}}}, "actual-input");
    input_progress.store(0);
    input_refusal.store(false);
    watch_input_write.store(true);
    (void)jobs.control(input);
    while (input_progress.load() == 0)
      bounded();
    const auto progress = jobs.control(input);
    const auto written = std::stoull(field(progress, "written_bytes").number().text());
    require(written < payload.size() && field(progress, "status") == Value{"queued"},
            "actual kernel partial input remains queued with its acquired prefix");
    bool conflict = false;
    try {
      (void)jobs.control(
          delivery("input", {{"bytes", Value{"changed"}}}, "actual-input"));
    } catch (const Error &error) {
      conflict = error.code == ErrorCode::conflict;
    }
    require(conflict, "changed duplicate refused without another native write");
    while (!input_refusal.load())
      bounded();
    {
      std::ofstream release{child / "read-input"};
      release << "read";
    }
    bool complete = false;
    while (!complete) {
      for (auto &event : jobs.drain()) {
        if (event.bytes)
          jobs.retained(id, event.offset + event.bytes->size());
        else {
          jobs.settled(id, event.result);
          complete = true;
        }
      }
      bounded();
    }
    watch_input_write.store(false);
    require(field(jobs.control(input), "status") == Value{"delivered"},
            "duplicate reports actual completed delivery");
    std::ifstream received{child / "received", std::ios::binary};
    const std::string original{std::istreambuf_iterator<char>{received}, {}};
    require(original == payload,
            "real PTY child receives exact complete original once");
    std::ifstream dimensions{child / "resized"};
    const std::string size{std::istreambuf_iterator<char>{dimensions}, {}};
    require(size == "rows=37\ncols=113\n",
            "real private PTY receives selected native dimensions");
    jobs.shutdown();
  }
  const auto all = records(session);
  unsigned positive_reads = 0, writes = 0, resizes = 0, signals = 0, leases = 0,
           released = 0, unreleased = 0, waits = 0, conflicts = 0;
  std::size_t delivered = 0;
  unsigned native_refusals = 0;
  RecordKey lease_key{}, wait_key{};
  bool partial = false;
  std::string output;
  for (const auto &[key, record] : all) {
    ByteView body{record.body};
    if (record.site == 2008) {
      const auto &request = linked(all, key, body, 2007);
      ByteView fields{request.body};
      const auto launch = reference(fields);
      require(all.at(launch).site == 2002, "read request links actual launch");
      require(native<int>(fields) == output_descriptor.load(),
              "read captures actual observed descriptor");
      const auto offset = native<std::size_t>(fields);
      const auto requested = native<std::size_t>(fields);
      const auto count = native<ssize_t>(body);
      (void)native<int>(body);
      require(requested == 8192 && fields.empty(),
              "read records native buffer extent exactly");
      if (count > 0) {
        require(record.partition == RawPartition::data && offset == output.size() &&
                    static_cast<std::size_t>(count) == body.size() &&
                    body.size() <= requested,
                "read original length/offset follows actual native result");
        output.append(reinterpret_cast<const char *>(body.data()), body.size());
        ++positive_reads;
      } else
        require(record.partition == RawPartition::control && body.empty(),
                "nonpositive native read has no invented original");
    } else if (record.site == 2017) {
      const auto &request = linked(all, key, body, 2016);
      ByteView fields{request.body};
      require(all.at(reference(fields)).site == 2002, "input links selected launch");
      require(text(fields) == "actual-input", "native input preserves delivery name");
      require(native<int>(fields) == input_descriptor.load(),
              "input records actual observed descriptor");
      const auto offset = native<std::size_t>(fields);
      const auto requested = native<std::size_t>(fields);
      const auto count = native<ssize_t>(body);
      const auto error = native<int>(body);
      require(offset == delivered && requested == payload.size() - delivered &&
                  fields.empty() && body.empty(),
              "native input offset and remainder exactly follow custody");
      if (count > 0) {
        require(static_cast<std::size_t>(count) <= requested,
                "actual partial write bounded");
        partial |= static_cast<std::size_t>(count) < requested;
        delivered += static_cast<std::size_t>(count);
      } else {
        require(count < 0 && (error == EAGAIN || error == EINTR),
                "native retry error preserved");
        if (error == EAGAIN)
          ++native_refusals;
      }
      ++writes;
    } else if (record.site == 2019) {
      const auto &request = linked(all, key, body, 2018);
      ByteView fields{request.body};
      require(all.at(reference(fields)).site == 2002, "resize links selected launch");
      const auto name = text(fields);
      require(native<int>(fields) == output_descriptor.load(),
              "resize captures actual observed PTY descriptor");
      const auto rows = native<unsigned short>(fields),
                 columns = native<unsigned short>(fields);
      require(native<int>(body) == 0, "actual resize native return success");
      (void)native<int>(body);
      if (name == "actual-resize") {
        require(rows == 37 && columns == 113 && fields.empty() && body.empty(),
                "native resize request and result exact");
        ++resizes;
      }
    } else if (record.site == 2021) {
      const auto &request = linked(all, key, body, 2020);
      ByteView fields{request.body};
      require(all.at(reference(fields)).site == 2002,
              "signal links actual child launch");
      const auto name = text(fields);
      const auto target = native<pid_t>(fields);
      const auto signal = native<int>(fields);
      const auto method = native<std::uint8_t>(fields);
      const auto result = native<int>(body);
      (void)native<int>(body);
      if ((name == "stop" || name == "continue") && method == 1) {
        require(target == -child_pid &&
                    signal == (name == "stop" ? SIGSTOP : SIGCONT) && result == 0 &&
                    fields.empty() && body.empty(),
                "actual group signal request/return exact");
        ++signals;
      }
    } else if (record.site == 2014) {
      const auto &request = linked(all, key, body, 2013);
      ByteView fields{request.body};
      require(text(fields) == id, "API request preserves original job name");
      const auto operation = text(fields), delivery = text(fields),
                 bytes = text(fields);
      const auto signal = text(fields);
      require(text(fields).empty() && text(fields).empty() && text(fields).empty() &&
                  fields.empty(),
              "API requests do not format native numeric fields");
      const auto branch = native<std::uint8_t>(body);
      const auto written = native<std::size_t>(body);
      const auto done = native<bool>(body), failed = native<bool>(body);
      const auto error = native<ErrorCode>(body);
      (void)native<std::int64_t>(body);
      require(body.empty(), "control terminal native body exact");
      if (branch == 3) {
        require(operation == "input" && delivery == "actual-input" &&
                    bytes == "changed" && error == ErrorCode::conflict && !done &&
                    failed && written == 0,
                "actual changed-input refusal links literal original request");
        ++conflicts;
      } else {
        require((branch == 1 || branch == 2) && !failed,
                "control terminal captures actual success or duplicate");
        if (operation == "input")
          require(bytes == payload && delivery == "actual-input",
                  "input raw request retains exact payload");
        if (operation == "signal")
          require(signal == delivery, "signal preserves original name");
      }
    } else if (record.site == 2025 || record.site == 2026) {
      const auto &request = linked(all, key, body, 2024);
      ByteView fields{request.body};
      require(text(fields) == id && native<bool>(fields) &&
                  native<std::uint64_t>(fields) == 5000,
              "foreground wait links actual requested lease and duration");
      require(native<std::uint64_t>(body) == foreground_epoch,
              "wait uses actual foreground epoch");
      if (record.site == 2025) {
        require(native<bool>(body), "lease is for actual PTY");
        lease_key = key;
        ++leases;
      } else {
        require(native<std::uint8_t>(body) == 2,
                "wait reports release distinctly from terminal");
        (void)native<ErrorCode>(body);
        (void)native<std::int64_t>(body);
        require(!native<bool>(body) && !native<bool>(body) && !native<bool>(body),
                "released lease does not claim semantic/native completion");
        wait_key = key;
        ++waits;
      }
    } else if (record.site == 2028) {
      const auto &request = linked(all, key, body, 2027);
      ByteView fields{request.body};
      require(text(fields) == id && native<std::uint64_t>(fields) == foreground_epoch &&
                  fields.empty(),
              "background request records actual selected lease");
      require(native<std::uint64_t>(body) == foreground_epoch,
              "background response retains current epoch");
      if (native<bool>(body))
        ++released;
      else
        ++unreleased;
    }
  }
  require(positive_reads > 0 && output == "READY\nDONE\n" && writes > 1 && partial &&
              delivered == payload.size() && native_refusals > 0 &&
              lease_key.first == wait_key.first && lease_key.second < wait_key.second &&
              resizes == 1 && signals == 2 && leases == 1 && waits == 1 &&
              released == 1 && unreleased == 1 && conflicts == 1,
          "real child request/ref/native fields, partial transfer and foreground "
          "release verified");
}
void blocked_worker(std::string_view fixture, const std::filesystem::path &root) {
  stage = "full worker data lane";
  const auto session = root / "blocked-session", child = root / "blocked-child";
  std::filesystem::create_directory(session);
  std::filesystem::create_directory(child);
  require(::chmod(session.c_str(), 0700) == 0, "private blocked session");
  IdentityBytes identity{};
  identity[0] = std::byte{92};
  {
    auto runtime = unwrap(RawRuntime::open(session.string(),
                                           unwrap(EnvironmentId::from_bytes(identity)),
                                           {{16384, 16384, 4096}, 4096}));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    CommandJobs jobs;
    auto status = unwrap(runtime->status());
    struct Resume {
      RawRuntimeStatus status;
      ~Resume() {
        (void)::kill(status.supervisor, SIGCONT);
        (void)::kill(status.recorder, SIGCONT);
      }
    } resume{status};
    const std::string id(32, '6');
    track_space_wait.store(true);
    space_wait_entered.store(false);
    jobs.start(id,
               Value::object({{"argv", Value{Value::Array{Value{std::string{fixture}},
                                                          Value{"gated-flood"},
                                                          Value{child.string()}}}},
                              {"timeout_seconds", Value{Number{10}}}}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!std::filesystem::exists(child / "ready")) {
      require(std::chrono::steady_clock::now() < deadline,
              "registered worker launched");
      std::this_thread::yield();
    }
    status = unwrap(runtime->status());
    resume.status = status;
    require(::kill(status.supervisor, SIGSTOP) == 0 &&
                ::kill(status.recorder, SIGSTOP) == 0,
            "pause both recorder owners after registration");
    {
      std::ofstream release{child / "start"};
      release << "start";
    }
    while (!space_wait_entered.load()) {
      require(std::chrono::steady_clock::now() < deadline,
              "one original acquired before full lane");
      std::this_thread::yield();
    }
    const auto observed =
        jobs.read(Value::object({{"job_id", Value{id}}, {"count", Value{Number{0}}}}));
    const auto acquired_size = static_cast<std::size_t>(
        std::stoull(field(observed, "received_bytes").number().text()));
    require(acquired_size > 0 && acquired_size < 16384,
            "worker retained bounded prefix before blocked acquisition");
    track_space_wait.store(false);
    {
      RawCommandWake canceled;
      canceled.notify();
      RawWaitScope wait{{{canceled.read_fd(), -1}, {}}};
      jobs.stop_all();
      jobs.shutdown();
    }
    require(detail::owned_children.load() == 0,
            "prepaid cleanup reaps child with both recorder owners stopped");
    (void)::kill(status.supervisor, SIGCONT);
    (void)::kill(status.recorder, SIGCONT);
    std::string acquired;
    for (auto &event : jobs.drain()) {
      if (event.bytes) {
        acquired += *event.bytes;
        jobs.retained(id, event.offset + event.bytes->size());
      }
    }
    for (auto &event : jobs.drain())
      if (!event.bytes)
        jobs.settled(id, event.result);
    require(acquired == std::string(acquired_size, 'F') && jobs.debt() == 0,
            "full worker cancellation retains exact acquired prefix and terminal");
  }
  const auto runs = unwrap(discover_raw_runs(session.string()));
  auto reader =
      unwrap(RawRunReader::open((session / "raw" / runs.front().name).string()));
  std::vector<std::byte> buffer(16384);
  unsigned parent_stops = 0, shutdown_outcomes = 0;
  IdentityBytes parent_lane{}, worker_lane{};
  while (auto record = unwrap(reader->read(buffer))) {
    auto body = record->raw;
    (void)native<std::uint32_t>(body);
    const auto site = native<std::uint32_t>(body);
    (void)native<std::uint64_t>(body);
    require(site != 8001, "canceled owner cleanup does not fence");
    if (site == 2029) {
      const auto length = native<std::size_t>(body);
      body = body.subspan(length);
      if (native<std::uint8_t>(body) == 7) {
        ++parent_stops;
        parent_lane = record->origin.lane;
        require(record->origin.partition == RawPartition::control,
                "canceled caller stop uses its held control");
      }
    } else if (site == 2034)
      worker_lane = record->origin.lane;
    else if (site == 2045) {
      body = body.subspan(24);
      const auto length = native<std::size_t>(body);
      body = body.subspan(length);
      if (native<std::uint8_t>(body) == 3)
        ++shutdown_outcomes;
    }
  }
  require(parent_stops == 1 && shutdown_outcomes == 1 && parent_lane != worker_lane,
          "one caller stop and initial shutdown on a distinct sequential producer");
}
int main(int argc, char **argv) {
  try {
    require(argc == 2, "fixture executable argument");
    Temporary temporary;
    live_recorder(temporary.root);
    registration_recorder_death(temporary.root);
    CommandJobs jobs;
    const std::string id(32, '1');
    jobs.start(id,
               Value::object(
                   {{"argv", Value{Value::Array{Value{argv[1]}, Value{"closed-stdio"},
                                                Value{temporary.root.string()}}}}}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    std::vector<JobEvent> offered;
    while (offered.empty()) {
      require(std::chrono::steady_clock::now() < deadline,
              "worker offers original output");
      offered = jobs.drain();
      std::this_thread::yield();
    }
    require(offered.front().bytes && *offered.front().bytes == "STDIO\n",
            "literal worker bytes");
    const auto repeated = jobs.drain();
    require(!repeated.empty() && repeated.front().bytes == offered.front().bytes &&
                repeated.front().offset == 0,
            "unacknowledged drain retains the same shared original");
    jobs.retained(id, offered.front().bytes->size());
    std::vector<JobEvent> terminal;
    while (terminal.empty()) {
      require(std::chrono::steady_clock::now() < deadline, "worker terminal offered");
      terminal = jobs.drain();
      std::this_thread::yield();
    }
    require(!terminal.front().bytes, "terminal follows acknowledged bytes");
    const auto again = jobs.drain();
    require(again.size() == 1 && again.front().result == terminal.front().result,
            "failed semantic settlement keeps the actual terminal available");
    jobs.settled(id, terminal.front().result);
    require(jobs.drain().empty() && jobs.debt() == 0,
            "acknowledgment settles exactly once");
    jobs.shutdown();
    canceled_exec_receipt(argv[1], temporary.root);
    canceled_exec_receipt(argv[1], temporary.root, true);
    raw_oracles(argv[1], temporary.root);
    api_projection_cuts(argv[1], temporary.root);
    native_controls(argv[1], temporary.root);
    blocked_worker(argv[1], temporary.root);
    std::cout << "command raw ownership oracles pass\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const Error &error) {
    std::cerr << stage << " native error " << static_cast<unsigned>(error.code) << ':'
              << error.detail << '\n';
    return 1;
  }
}
