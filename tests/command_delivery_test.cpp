#include "blackbird/command_jobs.hpp"
#include "blackbird/context.hpp"
#include "blackbird/raw_reader.hpp"
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <new>
#include <signal.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
using Path = std::filesystem::path;
using Clock = std::chrono::steady_clock;
using Kill = int (*)(pid_t, int);
using GetGroup = pid_t (*)(pid_t);
using Wait = int (*)(idtype_t, id_t, siginfo_t *, int);
Kill native_kill = nullptr;
GetGroup native_group = nullptr;
Wait native_wait = nullptr;
std::atomic_int fault_mode{0};
std::atomic_uint signal_calls{0};
std::atomic_bool pause_wait{false}, wait_entered{false}, release_wait{false};
thread_local pid_t verify_pid = 0;
thread_local bool deny_allocations = false, pause_allocation = false;
std::atomic_bool allocation_entered{false}, release_allocation{false};
std::atomic_size_t denied{0};
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error{message};
}
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
template <class F> void until(F predicate, const char *message) {
  const auto deadline = Clock::now() + std::chrono::seconds{5};
  while (!predicate()) {
    check(Clock::now() < deadline, message);
    std::this_thread::yield();
  }
}
struct Directory {
  Path path;
  Directory() {
    char name[] = "/tmp/blackbird-delivery-XXXXXX";
    check(::mkdtemp(name) != nullptr, "private delivery directory");
    path = name;
  }
  ~Directory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
CommandObservationId identity(char digit) {
  CommandObservationId result;
  result.bytes.fill(digit);
  return result;
}
void interrupted(int) {
  const int saved = errno;
  constexpr char byte = 'I';
  (void)::write(STDOUT_FILENO, &byte, 1);
  errno = saved;
}
int child(const Path &directory) {
  struct sigaction action{};
  action.sa_handler = interrupted;
  (void)sigemptyset(&action.sa_mask);
  if (::sigaction(SIGINT, &action, nullptr) != 0)
    return 91;
  const int gate = ::open((directory / "gate").c_str(), O_RDWR);
  if (gate < 0 || ::write(STDOUT_FILENO, "R", 1) != 1)
    return 92;
  char byte = 0;
  ssize_t result;
  do {
    result = ::read(gate, &byte, 1);
  } while (result < 0 && errno == EINTR);
  (void)::close(gate);
  return result == 1 && byte == 'X' ? 0 : 93;
}
CommandDeliveryObservation observe(const CommandJobs &jobs,
                                   const CommandDeliveryQuery &query) {
  deny_allocations = true;
  const auto result = jobs.try_observe_delivery(query);
  deny_allocations = false;
  return result;
}
CommandDeliveryObservation ready(const CommandJobs &jobs,
                                 const CommandDeliveryQuery &query) {
  CommandDeliveryObservation result;
  until(
      [&] {
        result = observe(jobs, query);
        return result.status != CommandDeliveryStatus::busy;
      },
      "finite delivery contention");
  return result;
}
bool same_ref(const RawEventRef &left, const RawEventRef &right) {
  return left.lane == right.lane && left.ordinal == right.ordinal;
}
enum class Startup { ready, defer };
struct Fixture {
  Directory directory;
  CommandJobs jobs;
  CommandObservationId id = identity('a');
  std::string key{id.view()}, output;
  int gate = -1;
  bool terminal = false;
  explicit Fixture(const Path &executable, Startup startup = Startup::ready) {
    const auto path = directory.path / "gate";
    check(::mkfifo(path.c_str(), 0600) == 0, "private child exit gate");
    gate = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    check(gate >= 0, "owned child exit gate");
    jobs.start(
        key,
        Value::object(
            {{"argv", Value{Value::Array{Value{executable.string()}, Value{"--child"},
                                         Value{directory.path.string()}}}},
             {"timeout_seconds", Value{Number{10}}}}));
    if (startup == Startup::ready)
      until(
          [&] {
            pump();
            return output == "R";
          },
          "real child ready with installed SIGINT handler");
  }
  ~Fixture() {
    fault_mode = 0;
    jobs.shutdown();
    if (gate >= 0)
      (void)::close(gate);
  }
  void pump() {
    for (const auto &event : jobs.drain())
      if (event.bytes) {
        output.append(*event.bytes);
        jobs.retained(event.job_id, event.offset + event.bytes->size());
      } else {
        jobs.settled(event.job_id, event.result);
        terminal = true;
      }
  }
  Value signal(std::string_view request, std::string_view name = "interrupt") const {
    return Value::object({{"op", Value{"signal"}},
                          {"job_id", Value{key}},
                          {"request_id", Value{std::string{request}}},
                          {"signal", Value{std::string{name}}}});
  }
  CommandDeliveryQuery query(std::string_view request) const {
    return {0, id, request};
  }
  CommandDeliveryObservation consumed(const CommandDeliveryQuery &query) {
    CommandDeliveryObservation result;
    until(
        [&] {
          result = ready(jobs, query);
          return result.stage == CommandDeliveryStage::consumed ||
                 result.stage == CommandDeliveryStage::failed;
        },
        "actual worker consumes accepted signal");
    return result;
  }
  void finish() {
    check(::write(gate, "X", 1) == 1, "release real child exit");
    until(
        [&] {
          pump();
          return terminal;
        },
        "real child terminal retained");
  }
};
void semantic(const Path &executable) {
  Fixture fixture{executable};
  const auto request = fixture.signal("first");
  const auto query = fixture.query("first");
  fixture.jobs.request(request);
  const auto debt = fixture.jobs.debt();
  const auto reserved = ready(fixture.jobs, query);
  check(reserved.status == CommandDeliveryStatus::ready &&
            reserved.stage == CommandDeliveryStage::reserved &&
            reserved.operation == CommandDeliveryOperation::signal &&
            reserved.signal == CommandDeliverySignal::interrupt &&
            reserved.request_size == 5 &&
            std::string_view{reserved.request.data(), reserved.request_size} ==
                "first" &&
            reserved.job == fixture.id && reserved.incarnation != 0 &&
            reserved.lookup_count <= 3 && !reserved.semantic_link_available &&
            !reserved.done && reserved.call_count == 0,
        "real accepted mailbox reservation is observable without allocation");
  for (unsigned n = 0; n < 100; ++n)
    check(ready(fixture.jobs, query).stage == CommandDeliveryStage::reserved,
          "pure observation must not dispatch the accepted control");
  check(fixture.jobs.debt() == debt && denied == 0 && signal_calls == 0,
        "query preserves owner debt and performs zero native signals");
  const auto selected = fixture.jobs.requests();
  check(selected.size() == 1 && selected.front() == request &&
            ready(fixture.jobs, query).stage == CommandDeliveryStage::selected,
        "selection remains an observable promise before domain dispatch");
  (void)fixture.jobs.control(selected.front());
  const auto result = fixture.consumed(query);
  check(result.stage == CommandDeliveryStage::consumed && result.done &&
            !result.failed &&
            result.consumption == CommandDeliveryConsumption::worker &&
            result.call_count == 2 && result.calls[0].method == 1 &&
            result.calls[1].method == 3 && result.calls[0].result == 0 &&
            result.calls[0].signal == SIGINT && result.calls[0].target < 0 &&
            result.calls[1].target == -result.calls[0].target &&
            result.calls[1].result == result.calls[1].target,
        "worker receipt records actual group kill and verification without claiming "
        "interruption");
  until(
      [&] {
        fixture.pump();
        return fixture.output == "RI";
      },
      "real handler independently acknowledges first SIGINT");
  check(result.admission.ordinal == 0 && result.enqueue.ordinal == 0 &&
            result.selection.ordinal == 0 && result.control.ordinal == 0 &&
            result.calls[0].request.ordinal == 0 &&
            result.calls[0].returned.ordinal == 0,
        "capture-disabled observation never fabricates references");
  // A closed/reloaded view keeps only this native key; querying does not replay it.
  const CommandDeliveryQuery after_reload{result.incarnation, fixture.id, "first"};
  const auto before = signal_calls.load();
  for (unsigned n = 0; n < 100; ++n)
    check(ready(fixture.jobs, after_reload).call_count == result.call_count,
          "same accepted key survives presentation recreation");
  fixture.jobs.request(request);
  const auto duplicate = fixture.jobs.requests();
  check(duplicate.size() == 1 &&
            field(fixture.jobs.control(duplicate.front()), "duplicate") == Value{true},
        "exact explicit redelivery reconciles existing domain delivery");
  check(signal_calls == before && ready(fixture.jobs, after_reload).call_count == 2,
        "reconciliation and exact duplicate never enqueue another worker signal");
  bool conflict = false;
  try {
    fixture.jobs.request(fixture.signal("first", "terminate"));
  } catch (const Error &error) {
    conflict = error.code == ErrorCode::conflict;
  }
  check(conflict, "same key with changed signal refuses before dispatch");
  (void)fixture.jobs.control(fixture.signal("second"));
  const auto second = fixture.consumed(fixture.query("second"));
  check(second.call_count == 2 && signal_calls == before + 1,
        "fresh intentional trigger has a distinct actual worker delivery");
  fixture.finish();

  Fixture abandoned{executable};
  struct ResumeWait {
    ~ResumeWait() { release_wait = true; }
  } resume_wait;
  const auto prior_calls = signal_calls.load();
  pause_wait = true;
  until([] { return wait_entered.load(); }, "real worker reaches native wait cut");
  (void)abandoned.jobs.control(abandoned.signal("abandoned-before-consumption"));
  const auto abandoned_query = abandoned.query("abandoned-before-consumption");
  const auto queued = ready(abandoned.jobs, abandoned_query);
  release_wait = true;
  check(queued.stage == CommandDeliveryStage::queued && !queued.done &&
            queued.consumption == CommandDeliveryConsumption::pending,
        "queued control remains distinct from worker consumption");
  const auto stopped = abandoned.consumed(abandoned_query);
  check(stopped.stage == CommandDeliveryStage::failed && stopped.done &&
            stopped.failed &&
            stopped.consumption == CommandDeliveryConsumption::abandoned &&
            stopped.call_count == 0 && signal_calls == prior_calls,
        "worker failure exposes abandoned control without inventing a syscall");
}
template <class T> T take(ByteView &bytes) {
  check(bytes.size() >= sizeof(T), "complete independent native raw field");
  T result{};
  std::memcpy(&result, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return result;
}
RawEventRef take_ref(ByteView &bytes) {
  return {take<IdentityBytes>(bytes), take<std::uint64_t>(bytes)};
}
std::string_view text(ByteView &bytes) {
  const auto size = take<std::size_t>(bytes);
  check(bytes.size() >= size, "complete independent native text");
  const std::string_view result{reinterpret_cast<const char *>(bytes.data()), size};
  bytes = bytes.subspan(size);
  return result;
}
struct Record {
  std::uint32_t site;
  std::vector<std::byte> body;
};
using RecordKey = std::pair<IdentityBytes, std::uint64_t>;
using Records = std::map<RecordKey, Record>;
ByteView body(const Records &records, const RawEventRef &reference,
              std::uint32_t expected) {
  const auto found = records.find({reference.lane, reference.ordinal});
  check(reference.ordinal != 0 && found != records.end() &&
            found->second.site == expected,
        "observed reference resolves the actual expected raw site");
  return found->second.body;
}
void raw(const Path &executable) {
  Directory recording;
  IdentityBytes epoch{};
  epoch[0] = std::byte{71};
  auto runtime = unwrap(RawRuntime::open(recording.path.string(),
                                         unwrap(EnvironmentId::from_bytes(epoch))));
  std::optional<RawThreadBinding> binding;
  binding.emplace(unwrap(runtime->prepare_thread()));
  CommandDeliveryObservation result;
  {
    Fixture fixture{executable};
    const auto request = fixture.signal("raw-first");
    const auto query = fixture.query("raw-first");
    fixture.jobs.request(request);
    const auto reserved = ready(fixture.jobs, query);
    check(reserved.status == CommandDeliveryStatus::ready &&
              reserved.stage == CommandDeliveryStage::reserved,
          "raw accepted reservation exposes actual enqueue before selection");
    const auto selected = fixture.jobs.requests();
    const auto chosen = ready(fixture.jobs, query);
    check(chosen.stage == CommandDeliveryStage::selected &&
              same_ref(chosen.admission, reserved.admission) &&
              same_ref(chosen.enqueue, reserved.enqueue),
          "selection preserves original accepted provenance");
    check(selected.size() == 1, "raw selects exact accepted signal");
    (void)fixture.jobs.control(selected.front());
    result = fixture.consumed(query);
    fixture.jobs.request(request);
    const auto duplicate = fixture.jobs.requests();
    check(duplicate.size() == 1, "raw exact duplicate selected");
    (void)fixture.jobs.control(duplicate.front());
    const auto after = ready(fixture.jobs, query);
    check(same_ref(after.admission, result.admission) &&
              same_ref(after.enqueue, result.enqueue) &&
              same_ref(after.selection, result.selection) &&
              same_ref(after.control, result.control) &&
              after.call_count == result.call_count,
          "raw duplicate retains original admission and worker provenance");
    fixture.finish();
  }
  binding.reset();
  runtime.reset();
  const auto runs = unwrap(discover_raw_runs(recording.path.string()));
  check(runs.size() == 1, "one private raw delivery run");
  auto reader =
      unwrap(RawRunReader::open((recording.path / "raw" / runs.front().name).string()));
  std::vector<std::byte> storage(1024 * 1024);
  Records records;
  while (const auto record = unwrap(reader->read(storage))) {
    auto bytes = record->raw;
    (void)take<std::uint32_t>(bytes);
    const auto site = take<std::uint32_t>(bytes);
    const auto ordinal = take<std::uint64_t>(bytes);
    check(site != 2037 && site != 2038,
          "bounded delivery queries emit no ordinary read/copy records");
    records.emplace(RecordKey{record->origin.lane, ordinal},
                    Record{site, {bytes.begin(), bytes.end()}});
  }
  for (const auto reference : {result.admission, result.control}) {
    auto bytes = body(records, reference, 2013);
    check(text(bytes) == std::string(32, 'a') && text(bytes) == "signal" &&
              text(bytes) == "raw-first" && text(bytes).empty() &&
              text(bytes) == "interrupt" && text(bytes).empty() &&
              text(bytes).empty() && text(bytes).empty() && bytes.empty(),
          "independent control original contains exact accepted typed arguments");
  }
  check(!same_ref(result.admission, result.control),
        "mailbox admission and actual control have distinct native originals");
  auto enqueue = body(records, result.enqueue, 2015);
  check(same_ref(take_ref(enqueue), result.admission) &&
            text(enqueue) == std::string(32, 'a') && text(enqueue) == "raw-first" &&
            take<std::uint8_t>(enqueue) == 1 && enqueue.empty(),
        "first enqueue resolves actual admission and delivery identity");
  auto selection = body(records, result.selection, 2015);
  check(same_ref(take_ref(selection), result.enqueue) &&
            text(selection) == std::string(32, 'a') && text(selection) == "raw-first" &&
            take<std::uint8_t>(selection) == 2 && selection.empty(),
        "first selection links to that exact enqueue");
  check(result.call_count == 2, "real raw signal uses group then verification");
  for (std::size_t n = 0; n < result.call_count; ++n) {
    const auto &call = result.calls[n];
    auto request = body(records, call.request, 2020);
    check(same_ref(take_ref(request), result.launch) && text(request) == "raw-first" &&
              take<pid_t>(request) == call.target && take<int>(request) == SIGINT &&
              take<std::uint8_t>(request) == call.method && request.empty(),
          "native syscall request links exact launch/key/target/signal/method");
    auto returned = body(records, call.returned, 2021);
    check(same_ref(take_ref(returned), call.request) &&
              take<int>(returned) == call.result && take<int>(returned) == call.error,
          "native syscall receipt contains exact actual return and errno");
    if (call.method == 3)
      check(take<pid_t>(returned) == call.result, "getpgid actual returned group");
    check(returned.empty(), "no invented native receipt fields");
  }
}
void errors(const Path &executable) {
  Fixture fixture{executable};
  fault_mode = 1;
  (void)fixture.jobs.control(fixture.signal("refused-kernel"));
  const auto refused = fixture.consumed(fixture.query("refused-kernel"));
  check(refused.stage == CommandDeliveryStage::consumed && refused.done &&
            refused.call_count == 2 && refused.calls[0].method == 1 &&
            refused.calls[0].result == -1 && refused.calls[0].error == ESRCH &&
            refused.calls[1].method == 2 && refused.calls[1].result == -1 &&
            refused.calls[1].error == EPERM,
        "worker consumption never hides actual kernel refusal behind delivered label");
  fault_mode = 2;
  (void)fixture.jobs.control(fixture.signal("verification-fallback"));
  const auto fallback = fixture.consumed(fixture.query("verification-fallback"));
  fault_mode = 0;
  check(fallback.call_count == 3 && fallback.calls[0].method == 1 &&
            fallback.calls[0].result == 0 && fallback.calls[1].method == 3 &&
            fallback.calls[1].result == fallback.calls[1].target + 1 &&
            fallback.calls[2].method == 2 && fallback.calls[2].result == 0 &&
            fallback.calls[2].target == fallback.calls[1].target,
        "bounded three-call receipt preserves unchanged group verification fallback");
  fixture.finish();
}
void busy() {
  CommandJobs jobs;
  const auto arguments = Value::object({});
  std::exception_ptr failure;
  std::jthread holder{[&] {
    try {
      pause_allocation = true;
      (void)jobs.read(arguments);
    } catch (...) {
      failure = std::current_exception();
    }
  }};
  const auto deadline = Clock::now() + std::chrono::seconds{5};
  while (!allocation_entered.load() && Clock::now() < deadline)
    std::this_thread::yield();
  const bool entered = allocation_entered.load();
  const auto result = observe(jobs, {0, identity('a'), "held"});
  release_allocation = true;
  holder.join();
  if (failure)
    std::rethrow_exception(failure);
  check(entered && result.status == CommandDeliveryStatus::busy,
        "actual registry contention returns busy before mutex owner released");
  check(ready(jobs, {0, identity('a'), "held"}).stage ==
            CommandDeliveryStage::job_not_resident,
        "busy does not poison later exact-key reads");
}
void lifecycle(const Path &executable) {
  {
    Fixture startup{executable, Startup::defer};
    const auto early_query = startup.query("never-submitted");
    until(
        [&] {
          const auto early = ready(startup.jobs, early_query);
          check(early.status == CommandDeliveryStatus::ready &&
                    early.stage == CommandDeliveryStage::key_not_resident,
                "exact-key observation is safe during native launch publication");
          startup.pump();
          return startup.output == "R";
        },
        "native startup stays observable while worker launches");
    startup.finish();
  }
  Fixture fixture{executable};
  const auto query = fixture.query("exit-before-dispatch");
  fixture.jobs.request(fixture.signal(query.request));
  const auto selected = fixture.jobs.requests();
  check(selected.size() == 1, "real exit fixture selected accepted promise");
  const auto before = ready(fixture.jobs, query);
  check(before.stage == CommandDeliveryStage::selected && before.incarnation != 0,
        "accepted promise observed before real target exit");
  fixture.finish();
  check(
      ready(fixture.jobs, query).stage == CommandDeliveryStage::selected &&
          fixture.jobs.debt() == 1,
      "selected promise remains observable after job terminal before explicit refusal");
  bool refused = false;
  try {
    (void)fixture.jobs.control(selected.front());
  } catch (const Error &error) {
    refused = error.code == ErrorCode::busy;
  }
  check(refused &&
            ready(fixture.jobs, query).stage ==
                CommandDeliveryStage::key_not_resident &&
            signal_calls == 0 && fixture.jobs.debt() == 0,
        "actual refusal releases promise; missing key remains unknown without replay");
  (void)fixture.jobs.control(
      Value::object({{"op", Value{"archive"}}, {"job_id", Value{fixture.key}}}));
  check(ready(fixture.jobs, query).stage == CommandDeliveryStage::job_not_resident,
        "retired job stays absent rather than targeting another process");
  CommandJobs other;
  auto stale = query;
  stale.incarnation = before.incarnation;
  check(ready(other, stale).status == CommandDeliveryStatus::scope_mismatch,
        "physical owner incarnation cannot be transplanted across runtime owners");
  auto malformed = query;
  malformed.job = identity('g');
  check(ready(other, malformed).status == CommandDeliveryStatus::invalid_identity,
        "noncanonical job rejects before registry lookup");
  malformed = query;
  malformed.request = {};
  check(ready(other, malformed).status == CommandDeliveryStatus::invalid_identity,
        "missing key never resolves an implicit request or foreground target");
  const std::string oversized(129, 'x');
  malformed.request = oversized;
  check(ready(other, malformed).status == CommandDeliveryStatus::invalid_identity,
        "overlength key rejects without truncation");
  const std::string boundary(128, 'x');
  malformed.request = boundary;
  check(ready(other, malformed).status == CommandDeliveryStatus::ready && denied == 0,
        "full128-byte key queries without allocation");
}
} // namespace
void *operator new(std::size_t size) { return allocate(size); }
void *operator new[](std::size_t size) { return allocate(size); }
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
extern "C" int kill(pid_t target, int signal) {
  if (signal == SIGINT) {
    ++signal_calls;
    if (fault_mode == 1) {
      errno = target < 0 ? ESRCH : EPERM;
      return -1;
    }
    if (fault_mode == 2 && target < 0)
      verify_pid = -target;
  }
  return native_kill(target, signal);
}
extern "C" pid_t getpgid(pid_t target) {
  if (fault_mode == 2 && target == verify_pid) {
    verify_pid = 0;
    return target + 1;
  }
  return native_group(target);
}
extern "C" int waitid(idtype_t type, id_t child_id, siginfo_t *observed, int options) {
  if (pause_wait.exchange(false)) {
    wait_entered = true;
    while (!release_wait.load())
      std::this_thread::yield();
    *observed = {};
    errno = EIO;
    return -1;
  }
  return native_wait(type, child_id, observed, options);
}
int main(int argc, char **argv) {
  try {
    native_kill = reinterpret_cast<Kill>(::dlsym(RTLD_NEXT, "kill"));
    native_group = reinterpret_cast<GetGroup>(::dlsym(RTLD_NEXT, "getpgid"));
    native_wait = reinterpret_cast<Wait>(::dlsym(RTLD_NEXT, "waitid"));
    check(native_kill && native_group && native_wait, "resolve actual native calls");
    if (argc == 3 && std::string_view{argv[1]} == "--child")
      return child(argv[2]);
    check(argc == 2, "one delivery case selector");
    const std::string_view mode{argv[1]};
    const auto executable = std::filesystem::absolute(argv[0]);
    if (mode == "semantic")
      semantic(executable);
    else if (mode == "raw")
      raw(executable);
    else if (mode == "errors")
      errors(executable);
    else if (mode == "busy")
      busy();
    else if (mode == "lifecycle")
      lifecycle(executable);
    else
      throw std::runtime_error{"unknown delivery case"};
    std::cout << "PASS " << mode << '\n';
    return 0;
  } catch (const std::exception &error) {
    deny_allocations = false;
    release_allocation = true;
    release_wait = true;
    fault_mode = 0;
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
