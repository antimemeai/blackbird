#include "blackbird/command_jobs.hpp"
#include "blackbird/context.hpp"
#include "blackbird/raw_reader.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <new>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
using Clock = std::chrono::steady_clock;
thread_local bool deny_allocations = false, pause_allocation = false;
std::atomic_bool allocation_entered{false}, release_allocation{false};
std::atomic_size_t denied{0};
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
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char name[] = "/tmp/blackbird-observation-XXXXXX";
    require(::mkdtemp(name) != nullptr, "private observation directory");
    path = name;
  }
  ~Directory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
CommandObservationId identity(char digit) {
  CommandObservationId id;
  id.bytes.fill(digit);
  return id;
}
CommandObservation observe(const CommandJobs &jobs,
                           const CommandObservationQuery &query) {
  return jobs.try_observe(query);
}
CommandObservation ready(const CommandJobs &jobs,
                         const CommandObservationQuery &query) {
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  for (;;) {
    const auto result = observe(jobs, query);
    if (result.status != CommandObservationStatus::busy)
      return result;
    require(Clock::now() < deadline, "finite observation contention");
    std::this_thread::yield();
  }
}
struct ChildOutput {
  unsigned count;
  std::size_t width;
};
int child(const char *directory, ChildOutput output) {
  const auto path = std::filesystem::path{directory} / "gate";
  const int gate = ::open(path.c_str(), O_RDWR);
  if (gate < 0)
    return 91;
  for (unsigned n = 0; n < output.count; ++n) {
    const std::string bytes(output.width, static_cast<char>('a' + n % 26));
    if (::write(STDOUT_FILENO, bytes.data(), bytes.size()) !=
        static_cast<ssize_t>(bytes.size()))
      return 92;
    char ack = 0;
    ssize_t received;
    do {
      received = ::read(gate, &ack, 1);
    } while (received < 0 && errno == EINTR);
    if (received != 1 || ack != 'X')
      return 93;
  }
  (void)::close(gate);
  return 7;
}
struct Fixture {
  Directory directory;
  int gate = -1;
  CommandJobs jobs;
  CommandObservationId id = identity('4');
  std::string key{id.view()};
  std::size_t received = 0;
  bool terminal = false;
  Fixture(const std::filesystem::path &executable, unsigned count,
          std::size_t width = 1) {
    const auto path = directory.path / "gate";
    require(::mkfifo(path.c_str(), 0600) == 0, "private output gate");
    gate = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    require(gate >= 0, "owned output gate");
    jobs.start(key, arguments(executable, count, width));
  }
  Value arguments(const std::filesystem::path &executable, unsigned count,
                  std::size_t width = 1) const {
    return Value::object(
        {{"argv", Value{Value::Array{Value{executable.string()}, Value{"--child"},
                                     Value{directory.path.string()},
                                     Value{std::to_string(count)},
                                     Value{std::to_string(width)}}}},
         {"timeout_seconds", Value{Number{10}}}});
  }
  ~Fixture() {
    jobs.shutdown();
    if (gate >= 0)
      (void)::close(gate);
  }
  void pump(bool retain = true) {
    for (const auto &event : jobs.drain())
      if (event.bytes) {
        received = std::max(received, event.offset + event.bytes->size());
        if (retain)
          jobs.retained(event.job_id, event.offset + event.bytes->size());
      } else {
        jobs.settled(event.job_id, event.result);
        terminal = true;
      }
  }
  void await_bytes(std::size_t count, bool retain = true) {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (received < count) {
      pump(retain);
      require(Clock::now() < deadline, "real child output arrives");
      std::this_thread::yield();
    }
  }
  void ack() {
    require(::write(gate, "X", 1) == 1, "release exact private output step");
  }
  void finish() {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!terminal) {
      pump();
      require(Clock::now() < deadline, "real child settles");
      std::this_thread::yield();
    }
  }
};
void allocation() {
  CommandJobs jobs;
  CommandObservationQuery query;
  deny_allocations = true;
  const auto result = observe(jobs, query);
  deny_allocations = false;
  require(result.status == CommandObservationStatus::ready && result.row_count == 0 &&
              denied == 0,
          "bounded empty observation remains ready with all allocation refused");
  query.selected = identity('g');
  require(ready(jobs, query).status == CommandObservationStatus::invalid_identity,
          "malformed fixed selected identity is explicit");
  query.selected = identity('4');
  require(ready(jobs, query).selection == CommandObservationSelection::not_resident,
          "well-formed absent identity is not resident");
  query.after = identity('G');
  require(ready(jobs, query).status == CommandObservationStatus::invalid_identity,
          "malformed fixed continuation is explicit");
  query.after.reset();
  query.byte_limit = 4097;
  require(ready(jobs, query).status == CommandObservationStatus::invalid_range,
          "over-budget copy is rejected");
  query.byte_limit = 4096;
  query.chunk_limit = 17;
  require(ready(jobs, query).status == CommandObservationStatus::invalid_range,
          "over-budget chunk visit is rejected");
  query.chunk_limit = 16;
  query.row_limit = 9;
  require(ready(jobs, query).status == CommandObservationStatus::invalid_range,
          "over-budget catalog is rejected");
}
template <class T> T take(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "complete native raw field");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
void raw_sources(const std::filesystem::path &directory,
                 const CommandObservation &observation) {
  const auto runs = unwrap(discover_raw_runs(directory.string()));
  require(runs.size() == 1, "one private raw observation run");
  auto reader = unwrap(RawRunReader::open((directory / "raw" / runs[0].name).string()));
  std::vector<std::byte> storage(1024 * 1024);
  std::map<std::pair<IdentityBytes, std::uint64_t>, std::string> output;
  unsigned view_records = 0;
  while (const auto record = unwrap(reader->read(storage))) {
    auto bytes = record->raw;
    (void)take<std::uint32_t>(bytes);
    const auto site = take<std::uint32_t>(bytes);
    const auto ordinal = take<std::uint64_t>(bytes);
    if (site == 2037 || site == 2038)
      ++view_records;
    if (site != 2008)
      continue;
    (void)take<IdentityBytes>(bytes);
    (void)take<std::uint64_t>(bytes);
    const auto count = take<ssize_t>(bytes);
    (void)take<int>(bytes);
    if (count > 0) {
      require(bytes.size() == static_cast<std::size_t>(count),
              "actual read literal extent");
      output[{record->origin.lane, ordinal}] =
          std::string{reinterpret_cast<const char *>(bytes.data()), bytes.size()};
    }
  }
  require(view_records == 0,
          "pure observation emits no ordinary read/copy raw records");
  std::size_t copied = 0;
  for (std::size_t n = 0; n < observation.window.source_count; ++n) {
    const auto &source = observation.window.sources[n];
    const auto found = output.find({source.original.lane, source.original.ordinal});
    require(found != output.end() && source.offset <= found->second.size() &&
                source.length <= found->second.size() - source.offset,
            "every page source resolves actual native read bytes");
    require(std::memcmp(observation.window.bytes.data() + copied,
                        found->second.data() + source.offset, source.length) == 0 &&
                source.chunk_begin + source.offset == observation.window.begin + copied,
            "actual source byte ranges reconstruct exact copied page");
    copied += source.length;
  }
  require(copied == observation.window.size, "every copied byte has source coverage");
}
void ranges(const std::filesystem::path &executable, bool raw) {
  Directory recording;
  std::unique_ptr<RawRuntime> runtime;
  std::optional<RawThreadBinding> binding;
  if (raw) {
    IdentityBytes epoch{};
    epoch[0] = std::byte{61};
    runtime = unwrap(RawRuntime::open(recording.path.string(),
                                      unwrap(EnvironmentId::from_bytes(epoch))));
    binding.emplace(unwrap(runtime->prepare_thread()));
  }
  CommandObservation saved;
  {
    Fixture fixture{executable, 40};
    CommandObservationQuery query;
    query.selected = fixture.id;
    fixture.await_bytes(1);
    fixture.ack();
    fixture.await_bytes(2, false);
    auto result = ready(fixture.jobs, query);
    require(result.status == CommandObservationStatus::ready &&
                result.window.size == 1 && result.window.retained_end == 1 &&
                result.window.received_end == 2 &&
                result.detail.custody == CommandObservationCustody::owned &&
                !result.detail.has_exit,
            "received unretained bytes never enter inspector window");
    const auto early = result;
    fixture.jobs.retained(fixture.key, 2);
    fixture.ack();
    for (std::size_t n = 3; n <= 40; ++n) {
      fixture.await_bytes(n);
      fixture.ack();
    }
    fixture.finish();
    query.offset = 5;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    for (;;) {
      deny_allocations = true;
      saved = observe(fixture.jobs, query);
      deny_allocations = false;
      if (saved.status != CommandObservationStatus::busy)
        break;
      require(Clock::now() < deadline, "finite settled observation contention");
    }
    require(saved.status == CommandObservationStatus::ready && denied == 0 &&
                saved.window.begin == 5 && saved.window.next == 21 &&
                saved.window.size == 16 && saved.window.source_count == 16 &&
                saved.examined_chunks == 16 && saved.seek_steps <= 7 &&
                saved.window.end == CommandObservationPageEnd::chunk_limit,
            "fragmented actual retained output obeys chunk/work/copy bounds");
    require(saved.window.provenance_complete == raw,
            "missing native raw provenance remains explicitly unavailable");
    for (std::size_t n = 0; n < saved.window.size; ++n)
      require(saved.window.bytes[n] ==
                  std::byte{static_cast<unsigned char>('a' + (n + 5) % 26)},
              "byte page matches actual source offset");
    query.offset = 37;
    query.byte_limit = 2;
    result = ready(fixture.jobs, query);
    require(result.window.size == 2 && result.window.next == 39 &&
                result.window.end == CommandObservationPageEnd::byte_limit,
            "byte limit ends before real retained frontier");
    query.offset = 39;
    result = ready(fixture.jobs, query);
    require(result.window.size == 1 && result.window.next == 40 &&
                result.window.end == CommandObservationPageEnd::frontier,
            "last page reports actual retained frontier");
    query.offset = 41;
    require(ready(fixture.jobs, query).status ==
                CommandObservationStatus::invalid_range,
            "future retained offset is rejected without fabricated bytes");
    require(early.window.size == 1 && early.window.received_end == 2 &&
                early.window.bytes[0] == std::byte{'a'} && saved.window.begin == 5 &&
                saved.window.size == 16 && fixture.jobs.retained_end(fixture.key) == 40,
            "copied observation stays owned and never changes owner frontier");
  }
  {
    Fixture fixture{executable, 130, 8192};
    for (std::size_t n = 1; n <= 130; ++n) {
      fixture.await_bytes(n * 8192);
      fixture.ack();
    }
    fixture.finish();
    CommandObservationQuery query;
    query.selected = fixture.id;
    auto result = ready(fixture.jobs, query);
    require(result.window.omitted_prefix && result.window.cached_begin >= 16384 &&
                result.window.begin == result.window.cached_begin &&
                result.window.size == 4096 && result.examined_chunks <= 2 &&
                result.seek_steps <= 9 && result.window.retained_end == 130 * 8192,
            "real tail eviction clamps to actual resident retained source frontier");
    query.offset = result.window.cached_begin + 11;
    result = ready(fixture.jobs, query);
    require(!result.window.omitted_prefix && result.window.begin == query.offset &&
                result.window.sources[0].offset == 11 && result.window.size == 4096,
            "byte page records genuine partial-native-chunk source offset");
  }
  binding.reset();
  runtime.reset();
  if (raw)
    raw_sources(recording.path, saved);
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
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  while (!allocation_entered.load() && Clock::now() < deadline)
    std::this_thread::yield();
  const bool entered = allocation_entered.load();
  const auto result = observe(jobs, {});
  release_allocation = true;
  holder.join();
  if (failure)
    std::rethrow_exception(failure);
  require(entered && result.status == CommandObservationStatus::busy,
          "actual registry mutex contention returns busy without waiting");
  require(ready(jobs, {}).status == CommandObservationStatus::ready,
          "busy does not poison later observation");
}
void lifecycle(const std::filesystem::path &executable) {
  Fixture fixture{executable, 0};
  fixture.finish();
  for (char digit = '5'; digit <= '9'; ++digit)
    fixture.jobs.start(std::string{identity(digit).view()},
                       fixture.arguments(executable, 0));
  for (char digit = 'a'; digit <= 'c'; ++digit)
    fixture.jobs.start(std::string{identity(digit).view()},
                       fixture.arguments(executable, 0));
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  for (;;) {
    fixture.pump();
    auto result = ready(fixture.jobs, {});
    require(result.status == CommandObservationStatus::ready && result.row_count == 8 &&
                result.examined_rows == 8 && result.more_rows && result.next &&
                result.rows[0].id == fixture.id,
            "catalog keeps eight rows with fixed stable pagination cursor");
    CommandObservationQuery query;
    query.after = result.next;
    query.selected = fixture.id;
    result = ready(fixture.jobs, query);
    require(result.row_count == 1 && result.rows[0].id == identity('c') &&
                result.examined_rows == 2 && !result.more_rows &&
                result.selection == CommandObservationSelection::resident,
            "selected detail is separate from eight-row catalog budget");
    require(result.detail.phase == CommandObservationPhase::settled &&
                result.detail.custody == CommandObservationCustody::reaped &&
                result.detail.has_exit && result.detail.exit_code == 7 &&
                result.detail.sealed && result.detail.terminal_retained &&
                result.detail.capture_complete,
            "sealed nonzero exit remains actual failed command not invented success");
    if (!fixture.jobs.active())
      break;
    require(Clock::now() < deadline, "catalog children settle");
  }
  CommandObservationQuery query;
  query.selected = fixture.id;
  const auto result = ready(fixture.jobs, query);
  require(result.row_count == 8 && result.examined_rows == 9 && result.more_rows,
          "selected detail never shrinks the catalog page");
  (void)fixture.jobs.control(
      Value::object({{"op", Value{"archive"}}, {"job_id", Value{fixture.key}}}));
  require(ready(fixture.jobs, query).selection ==
              CommandObservationSelection::not_resident,
          "retired projection is explicitly not resident rather than never existed");
  Fixture incomplete{executable, 1};
  incomplete.await_bytes(1);
  incomplete.jobs.capture_failed(incomplete.key);
  incomplete.finish();
  query.selected = incomplete.id;
  const auto failed = ready(incomplete.jobs, query);
  require(failed.detail.terminal_retained && !failed.detail.capture_complete &&
              !failed.detail.sealed && failed.detail.stop_requested,
          "actual failed capture retains uncertainty independently of terminal state");
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
    if (argc == 5 && std::string_view{argv[1]} == "--child")
      return child(argv[2], {static_cast<unsigned>(std::stoul(argv[3])),
                             static_cast<std::size_t>(std::stoull(argv[4]))});
    require(argc == 2, "one observation case selector");
    const std::string_view mode{argv[1]};
    const auto executable = std::filesystem::absolute(argv[0]);
    if (mode == "allocation")
      allocation();
    else if (mode == "ranges")
      ranges(executable, false);
    else if (mode == "ranges-raw")
      ranges(executable, true);
    else if (mode == "busy")
      busy();
    else if (mode == "lifecycle")
      lifecycle(executable);
    else
      throw std::runtime_error("unknown observation case");
    std::cout << "PASS " << mode << '\n';
    return 0;
  } catch (const std::exception &error) {
    deny_allocations = false;
    release_allocation = true;
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
