#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_sampler.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <future>
#include <poll.h>
#include <string>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#if defined(__APPLE__)
#include <libproc.h>
#include <mach/mach_time.h>
#include <sys/proc.h>
#include <sys/resource.h>
#else
#include <cstdarg>
#include <sys/vfs.h>
#endif

namespace {
using namespace blackbird;
struct OwnedProcess {
  pid_t owner = 0, pid = -1;
  bool kill_on_failure = false;
};
std::array<OwnedProcess, 16> owned_processes{};
void track_process(pid_t pid, bool kill_on_failure) {
  for (auto &process : owned_processes)
    if (process.pid < 0) {
      process = {::getpid(), pid, kill_on_failure};
      return;
    }
  std::abort();
}
void release_process(pid_t pid) {
  for (auto &process : owned_processes)
    if (process.owner == ::getpid() && process.pid == pid)
      process.pid = -1;
}
void check(bool value, const char *text, int line) {
  if (!value) {
    std::fprintf(stderr, "sampler check line%d: %s\n", line, text);
    for (const auto &process : owned_processes)
      if (process.owner == ::getpid() && process.pid > 0) {
        (void)::kill(process.pid, SIGCONT);
        if (process.kill_on_failure)
          (void)::kill(process.pid, SIGKILL);
      }
    std::abort();
  }
}
#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __LINE__)
template <class T> T need(Result<T> result) {
  if (!result.has_value())
    std::fprintf(stderr, "%s (%lld)\n", error_name(result.error().code),
                 static_cast<long long>(result.error().detail));
  CHECK(result.has_value());
  return std::move(result).value();
}
void need(Result<void> result) { CHECK(result.has_value()); }
template <class T> T field(ByteView bytes, std::size_t offset) {
  CHECK(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset);
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}
void write_all(int fd, ByteView bytes) {
  while (!bytes.empty()) {
    const auto size = ::write(fd, bytes.data(), bytes.size());
    if (size < 0 && errno == EINTR)
      continue;
    CHECK(size > 0);
    bytes = bytes.subspan(static_cast<std::size_t>(size));
  }
}
std::array<int, 2> pipe_pair() {
  std::array<int, 2> descriptors{};
  CHECK(::pipe(descriptors.data()) == 0);
  return descriptors;
}
struct NativeObservation {
  int result = 0, error = 0;
  ssize_t read_result = 0;
  std::size_t size = 0;
  std::array<std::byte, 4096> bytes{};
};
struct ClockObservation {
  clockid_t method{};
  int result = 0, error = 0;
  timespec value{};
};
struct Capture {
  bool enabled = false;
  pid_t target = -1, owner = -1;
  int stat_fd = -1;
  std::size_t count = 0;
  std::array<NativeObservation, 64> observations{};
  std::size_t clock_count = 0;
  std::array<ClockObservation, 128> clocks{};
  NativeObservation setup;
};
Capture *capture = nullptr;
std::atomic<int> watched_stop{-1}, poll_notice{-1};
std::atomic<int> watched_control{-1}, control_notice{-1};
template <class T> T next_function(const char *name) {
  void *symbol = ::dlsym(RTLD_NEXT, name);
  CHECK(symbol != nullptr);
  static_assert(sizeof(T) == sizeof(symbol));
  T function{};
  std::memcpy(&function, &symbol, sizeof(function));
  return function;
}
struct CapturedCalls {
  CapturedCalls() {
    void *mapping = ::mmap(nullptr, sizeof(Capture), PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANON, -1, 0);
    CHECK(mapping != MAP_FAILED);
    capture = ::new (mapping) Capture{};
    capture->owner = ::getpid();
  }
  ~CapturedCalls() {
    capture->~Capture();
    CHECK(::munmap(capture, sizeof(Capture)) == 0);
    capture = nullptr;
  }
};
struct Record {
  RawOrigin origin;
  std::uint32_t site;
  std::uint64_t ordinal;
  std::vector<std::byte> body;
};
struct Fixture {
  std::array<char, 64> directory{};
  EnvironmentId environment =
      need(EnvironmentId::from_bytes(IdentityBytes{std::byte{1}}));
  Fixture() {
    constexpr std::string_view pattern = "/tmp/blackbird-raw-sampler-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), directory.begin());
    CHECK(::mkdtemp(directory.data()) != nullptr);
  }
  ~Fixture() { std::filesystem::remove_all(directory.data()); }
  std::unique_ptr<RawRuntime> runtime(RawRuntimeOptions options = {}) {
    return need(RawRuntime::open(directory.data(), environment, options));
  }
  std::vector<Record> records() {
    const auto entries = need(discover_raw_runs(directory.data()));
    CHECK(entries.size() == 1 && entries[0].snapshot.has_value());
    const auto &snapshot = entries.front().snapshot;
    if (!snapshot.has_value())
      std::abort();
    CHECK(snapshot->closed);
    const auto path = std::filesystem::path{directory.data()} / "raw" / entries[0].name;
    auto reader = need(RawRunReader::open(path.string()));
    const auto &description = reader->snapshot().descriptions;
    constexpr std::string_view mark = "sampler7000";
    CHECK(std::search(description.begin(), description.end(),
                      reinterpret_cast<const std::byte *>(mark.data()),
                      reinterpret_cast<const std::byte *>(mark.data() + mark.size())) !=
          description.end());
    std::array<std::byte, 8192> output{};
    std::vector<Record> result;
    for (;;) {
      const auto next = need(reader->read(output));
      if (!next.has_value())
        break;
      CHECK(next->raw.size() >= 16);
      const auto site = field<std::uint32_t>(next->raw, 4);
      if (site >= 7000 && site <= 7031)
        result.push_back({next->origin,
                          site,
                          field<std::uint64_t>(next->raw, 8),
                          {next->raw.begin() + 16, next->raw.end()}});
    }
    return result;
  }
};
struct Child {
  std::array<int, 2> pipe = pipe_pair();
  pid_t pid = ::fork();
  Child() {
    CHECK(pid >= 0);
    if (pid == 0) {
      (void)::close(pipe[1]);
      char byte{};
      while (::read(pipe[0], &byte, 1) < 0 && errno == EINTR) {
      }
      ::_exit(0);
    }
    track_process(pid, true);
    CHECK(::close(pipe[0]) == 0);
    pipe[0] = -1;
  }
  ~Child() { stop(); }
  void stop() {
    if (pid < 0)
      return;
    CHECK(::close(pipe[1]) == 0);
    pipe[1] = -1;
    int status{};
    CHECK(::waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    release_process(pid);
    pid = -1;
  }
};
constexpr std::uint32_t method =
#if defined(__APPLE__)
    static_cast<std::uint32_t>(RawSamplerMethod::mac_rusage);
#else
    static_cast<std::uint32_t>(RawSamplerMethod::linux_stat);
#endif
std::vector<Record> sites(const std::vector<Record> &records, std::uint32_t site) {
  std::vector<Record> result;
  for (const auto &record : records)
    if (record.site == site)
      result.push_back(record);
  // Recorder partitions preserve independently. This single-lane fixture joins
  // actual native chronology using the producer's original ordinal off path.
  std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
    return left.ordinal < right.ordinal;
  });
  return result;
}
void linked(const std::vector<Record> &records, const Record &returned,
            std::uint32_t requested_site) {
  CHECK(returned.body.size() >= 24);
  const auto ordinal = field<std::uint64_t>(returned.body, 16);
  const auto request =
      std::find_if(records.begin(), records.end(), [&](const Record &entry) {
        return entry.ordinal == ordinal && entry.site == requested_site &&
               std::equal(returned.body.begin(), returned.body.begin() + 16,
                          entry.origin.lane.begin());
      });
  CHECK(request != records.end());
}
void wire_oracle() {
  const RawSamplerRequest request{0x8877665544332211ULL, 1234, method};
  const auto encoded = encode_raw_sampler_request(request);
  std::array<std::byte, 32> expected{};
  std::memcpy(expected.data(), "BBRSSCTL", 8);
  const std::uint32_t version = 1, native_method = static_cast<std::uint32_t>(method);
  std::memcpy(expected.data() + 8, &version, 4);
  std::memcpy(expected.data() + 12, &native_method, 4);
  std::memcpy(expected.data() + 16, &request.controller, 8);
  std::memcpy(expected.data() + 24, &request.pid, 4);
  CHECK(encoded == expected);
}
void exact_native_calls() {
  Fixture fixture;
  Child child;
  CapturedCalls calls;
  auto runtime = fixture.runtime();
  {
    RawThreadBinding binding{need(runtime->prepare_thread())};
    capture->enabled = true;
    capture->target = child.pid;
    auto sampler = need(RawResourceSampler::open());
    need(sampler->acquire({1, child.pid, method}));
    need(sampler->acquire({2, child.pid, method}));
    capture->enabled = false;
    need(sampler->finish());
  }
  runtime.reset();
  const auto records = fixture.records();
  CHECK(sites(records, 7004).size() == 2 && sites(records, 7005).size() == 2);
#if defined(__APPLE__)
  const auto observed = sites(records, 7022);
  CHECK(observed.size() == 2 && capture->count == 2);
  for (std::size_t i = 0; i < observed.size(); ++i) {
    linked(records, observed[i], 7021);
    CHECK(capture->observations[i].result == 0);
    CHECK(observed[i].body.size() == 24 + 4 + 96);
    CHECK(field<int>(observed[i].body, 24) == 0);
    CHECK(std::equal(observed[i].body.begin() + 28, observed[i].body.end(),
                     capture->observations[i].bytes.begin()));
  }
#else
  const auto observed = sites(records, 7020);
  CHECK(observed.size() == capture->count && capture->count >= 4);
  for (std::size_t i = 0; i < observed.size(); ++i) {
    linked(records, observed[i], 7019);
    CHECK(field<ssize_t>(observed[i].body, 24) == capture->observations[i].read_result);
    if (capture->observations[i].read_result > 0)
      CHECK(std::equal(observed[i].body.begin() + 32, observed[i].body.end(),
                       capture->observations[i].bytes.begin(),
                       capture->observations[i].bytes.begin() +
                           static_cast<std::ptrdiff_t>(capture->observations[i].size)));
    else
      CHECK(observed[i].body.size() == 32);
  }
#endif
  const auto clocks = sites(records, 7010);
  CHECK(clocks.size() == capture->clock_count && !clocks.empty());
  for (std::size_t i = 0; i < clocks.size(); ++i) {
    const auto &record = clocks[i];
    linked(records, record, 7009);
    CHECK(record.body.size() == 24 + 4 + sizeof(time_t) + sizeof(long));
    CHECK(field<int>(record.body, 24) == capture->clocks[i].result);
    CHECK(field<time_t>(record.body, 28) == capture->clocks[i].value.tv_sec);
    CHECK(field<long>(record.body, 36) == capture->clocks[i].value.tv_nsec);
  }
#if defined(__APPLE__)
  const auto setup = sites(records, 7026);
  CHECK(setup.size() == 1 && capture->setup.size == 8);
  CHECK(field<kern_return_t>(setup[0].body, 24) == capture->setup.result);
  CHECK(std::equal(setup[0].body.begin() + 28, setup[0].body.end(),
                   capture->setup.bytes.begin()));
#else
  const auto setup = sites(records, 7014);
  CHECK(setup.size() == 1 && capture->setup.size == 120);
  CHECK(field<int>(setup[0].body, 24) == capture->setup.result);
  CHECK(std::equal(setup[0].body.begin() + 28, setup[0].body.end(),
                   capture->setup.bytes.begin()));
#endif
  CHECK(sites(records, 7001).size() == 1);
}
void failures_and_cancel() {
  Fixture fixture;
  auto runtime = fixture.runtime();
  const auto stop = pipe_pair();
  {
    RawThreadBinding binding{need(runtime->prepare_thread())};
    RawSamplerOptions options;
#if defined(__APPLE__)
    // Raw native selector7 is unimplemented by the OS. Preserve its ABI bits
    // rather than manufacturing a named SDK enumerator for this failure case.
    const int clock_selector = 7;
    static_assert(sizeof(clock_selector) == sizeof(options.clock));
    std::memcpy(&options.clock, &clock_selector, sizeof(options.clock));
#else
    options.clock = -1;
#endif
    options.control.cancellation_fds[0] = stop[0];
    auto sampler = need(RawResourceSampler::open(options));
    need(sampler->acquire({1, ::getpid(), method}));
    const auto absent = static_cast<pid_t>(std::numeric_limits<pid_t>::max());
    const auto failed = sampler->acquire({2, absent, method});
    CHECK(!failed.has_value() && failed.error().code == ErrorCode::io);
    const std::uint32_t unsupported = 99;
    CHECK(!sampler->acquire({3, ::getpid(), unsupported}).has_value());
    const std::array byte{std::byte{1}};
    write_all(stop[1], byte);
    const auto canceled = sampler->acquire({4, ::getpid(), method});
    CHECK(!canceled.has_value() && canceled.error().code == ErrorCode::interrupted);
    need(sampler->finish());
  }
  CHECK(::close(stop[0]) == 0 && ::close(stop[1]) == 0);
  runtime.reset();
  const auto records = fixture.records();
  CHECK(sites(records, 7004).size() == 2);
  CHECK(sites(records, 7005).size() == 2);
  const auto direct = sites(records, 7027);
  CHECK(direct.size() == 3);
  CHECK(field<std::uint64_t>(direct[2].body, 24) == 3);
  CHECK(field<pid_t>(direct[2].body, 32) == ::getpid());
  CHECK(field<std::uint32_t>(direct[2].body, 36) == 99);
  const auto rejected = sites(records, 7006);
  const auto linked_rejection =
      std::find_if(rejected.begin(), rejected.end(), [&](const Record &entry) {
        return field<std::uint64_t>(entry.body, 16) == direct[2].ordinal;
      });
  CHECK(linked_rejection != rejected.end());
  CHECK(field<ErrorCode>(linked_rejection->body, 24) == ErrorCode::unsupported);
  const auto clocks = sites(records, 7010);
  CHECK(!clocks.empty());
  for (const auto &record : clocks) {
    CHECK(record.body.size() == 32);
    CHECK(field<int>(record.body, 24) == -1 && field<int>(record.body, 28) == EINVAL);
  }
  CHECK(sites(records, 7006).size() >= 3);
  const auto ends = sites(records, 7001);
  CHECK(ends.size() == 1 && field<std::uint32_t>(ends[0].body, 24) != 0);
}
void control_bytes() {
  Fixture fixture;
  auto runtime = fixture.runtime();
  const auto pipe = pipe_pair();
  const auto command = encode_raw_sampler_request({7, ::getpid(), method});
  write_all(pipe[1], command);
  const std::array trailing{std::byte{'x'}, std::byte{0}, std::byte{255}};
  write_all(pipe[1], trailing);
  CHECK(::close(pipe[1]) == 0);
  {
    RawThreadBinding binding{need(runtime->prepare_thread())};
    auto sampler = need(RawResourceSampler::open());
    const auto result = sampler->run(pipe[0]);
    CHECK(!result.has_value() && result.error().code == ErrorCode::incomplete);
  }
  CHECK(::close(pipe[0]) == 0);
  runtime.reset();
  const auto records = fixture.records();
  CHECK(sites(records, 7004).size() == 1);
  std::vector<std::byte> preserved;
  bool eof = false;
  for (const auto &record : sites(records, 7003)) {
    linked(records, record, 7002);
    const auto count = field<ssize_t>(record.body, 24);
    if (count > 0)
      preserved.insert(preserved.end(), record.body.begin() + 32, record.body.end());
    else if (count == 0)
      eof = true;
  }
  std::vector<std::byte> expected(command.begin(), command.end());
  expected.insert(expected.end(), trailing.begin(), trailing.end());
  CHECK(preserved == expected && eof);
}
void capacity_preflight() {
#if defined(__APPLE__)
  constexpr std::uint64_t required_control = 670, required_data = 140;
#else
  constexpr std::uint64_t required_control = 865, required_data = 4144;
#endif
  for (const bool short_control : {true, false}) {
    Fixture fixture;
    auto runtime = fixture.runtime({{short_control ? 8192 : required_data - 1,
                                     short_control ? required_control - 1 : 4096, 4096},
                                    4096});
    {
      RawThreadBinding binding{need(runtime->prepare_thread())};
      const auto opened = RawResourceSampler::open();
      CHECK(!opened.has_value() && opened.error().code == ErrorCode::capacity);
    }
    runtime.reset();
    CHECK(fixture.records().empty());
  }
  Fixture fixture;
  auto runtime = fixture.runtime({{required_data, required_control, 4096}, 4096});
  const auto control = pipe_pair();
  const auto command = encode_raw_sampler_request({11, ::getpid(), method});
  write_all(control[1], command);
  CHECK(::close(control[1]) == 0);
  {
    RawThreadBinding binding{need(runtime->prepare_thread())};
    auto sampler = need(RawResourceSampler::open());
    need(sampler->run(control[0]));
  }
  CHECK(::close(control[0]) == 0);
  runtime.reset();
  const auto records = fixture.records();
  CHECK(sites(records, 7004).size() == 1 && sites(records, 7005).size() == 1);
}
void fragmented_command() {
  Fixture fixture;
  auto runtime = fixture.runtime();
  auto context = need(runtime->prepare_thread());
  const auto control = pipe_pair(), notice = pipe_pair();
  const auto command = encode_raw_sampler_request({19, ::getpid(), method});
  watched_control.store(control[0]);
  control_notice.store(notice[1]);
  write_all(control[1], ByteView{command}.first(7));
  std::optional<Error> failed;
  std::thread worker{[&] {
    RawThreadBinding binding{context};
    auto sampler = need(RawResourceSampler::open());
    const auto result = sampler->run(control[0]);
    if (!result.has_value())
      failed = result.error();
  }};
  char arrived{};
  CHECK(::read(notice[0], &arrived, 1) == 1 && arrived == 'r');
  write_all(control[1], ByteView{command}.subspan(7));
  CHECK(::close(control[1]) == 0);
  worker.join();
  CHECK(!failed.has_value());
  watched_control.store(-1);
  for (const int fd : {control[0], notice[0], notice[1]})
    CHECK(::close(fd) == 0);
  context.reset();
  runtime.reset();
  const auto records = fixture.records();
  const auto reads = sites(records, 7003);
  CHECK(reads.size() == 3);
  CHECK(field<ssize_t>(reads[0].body, 24) == 7);
  CHECK(field<ssize_t>(reads[1].body, 24) == 25);
  CHECK(field<ssize_t>(reads[2].body, 24) == 0);
  std::vector<std::byte> bytes(reads[0].body.begin() + 32, reads[0].body.end());
  bytes.insert(bytes.end(), reads[1].body.begin() + 32, reads[1].body.end());
  CHECK(std::equal(bytes.begin(), bytes.end(), command.begin(), command.end()));
  const auto acquired = sites(records, 7004);
  CHECK(acquired.size() == 1 &&
        field<std::uint64_t>(acquired[0].body, 16) == reads[1].ordinal);
  CHECK(sites(records, 7027).empty());
}
void invalid_control() {
  Fixture fixture;
  auto runtime = fixture.runtime();
  const auto pipe = pipe_pair();
  auto command = encode_raw_sampler_request({7, ::getpid(), method});
  command[0] = std::byte{0};
  write_all(pipe[1], command);
  CHECK(::close(pipe[1]) == 0);
  {
    RawThreadBinding binding{need(runtime->prepare_thread())};
    auto sampler = need(RawResourceSampler::open());
    CHECK(!sampler->run(pipe[0]).has_value());
  }
  CHECK(::close(pipe[0]) == 0);
  runtime.reset();
  const auto records = fixture.records();
  CHECK(sites(records, 7004).empty());
  const auto reads = sites(records, 7003);
  CHECK(reads.size() == 1 && reads[0].body.size() == 64);
  CHECK(std::equal(reads[0].body.begin() + 32, reads[0].body.end(), command.begin()));
  CHECK(sites(records, 7006).size() == 1);
}
void observe_stopped(pid_t pid) {
  for (;;) {
#if defined(__APPLE__)
    proc_bsdinfo observed{};
    CHECK(::proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &observed, sizeof(observed)) ==
          static_cast<int>(sizeof(observed)));
    if (observed.pbi_status == SSTOP)
      return;
#else
    const auto path = "/proc/" + std::to_string(pid) + "/stat";
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0);
    std::array<char, 1024> bytes{};
    const auto count = ::read(fd, bytes.data(), bytes.size());
    CHECK(::close(fd) == 0 && count > 0);
    const std::string_view raw{bytes.data(), static_cast<std::size_t>(count)};
    const auto end = raw.rfind(')');
    CHECK(end != std::string_view::npos && end + 2 < raw.size());
    if (raw[end + 2] == 'T' || raw[end + 2] == 't')
      return;
#endif
    std::this_thread::yield();
  }
}
void stopped_recorder() {
  Fixture fixture;
  Child target;
  const auto stop = pipe_pair(), notice = pipe_pair();
  constexpr RawCapacities capacities{8192, 4096, 4096};
  auto runtime = fixture.runtime({capacities, 4096});
  auto context = need(runtime->prepare_thread());
  const auto status = need(runtime->status());
  track_process(status.recorder, false);
  CHECK(::kill(status.recorder, SIGSTOP) == 0);
  observe_stopped(status.recorder);
  watched_stop.store(stop[0]);
  poll_notice.store(notice[1]);
  std::promise<IdentityBytes> identity;
  auto observed_identity = identity.get_future();
  Error canceled{ErrorCode::corrupt};
  std::thread worker{[&] {
    RawThreadBinding binding{context};
    identity.set_value(raw_lane_epoch());
    RawSamplerOptions options;
    options.control.cancellation_fds[0] = stop[0];
    auto sampler = need(RawResourceSampler::open(options));
    std::array<std::byte, 8192 - 16> filler{};
    need(raw_wait_data(8192));
    const std::array<ByteView, 1> parts{filler};
    (void)need(raw_emit(9400, parts));
    const auto acquired = sampler->acquire({31, target.pid, method});
    CHECK(!acquired.has_value());
    canceled = acquired.error();
    need(sampler->finish());
  }};
  const auto epoch = observed_identity.get();
  char arrived{};
  CHECK(::read(notice[0], &arrived, 1) == 1 && arrived == 'p');
  // The forwarding shim observed the actual native slow-path poll. The target
  // completes its own pipe transaction while sampler output remains full.
  const std::array byte{std::byte{1}};
  write_all(target.pipe[1], byte);
  target.stop();
  write_all(stop[1], byte);
  worker.join();
  watched_stop.store(-1);
  CHECK(canceled.code == ErrorCode::interrupted);
  std::string channel_name = "/bb";
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 12; ++i) {
    const auto value = std::to_integer<unsigned>(epoch[i]);
    channel_name += hex[value >> 4];
    channel_name += hex[value & 15U];
  }
  auto channel = need(RawChannel::open(channel_name, epoch, capacities));
  const auto data = need(channel.cursors(RawPartition::data));
  CHECK(data.published == 8192 && data.acknowledged == 0);
  CHECK(need(channel.cursors(RawPartition::failure)).published == 0);
  CHECK(::kill(status.recorder, SIGCONT) == 0);
  context.reset();
  runtime.reset();
  release_process(status.recorder);
  for (const int fd : {stop[0], stop[1], notice[0], notice[1]})
    CHECK(::close(fd) == 0);
  const auto records = fixture.records();
  CHECK(sites(records, 7022).empty() && sites(records, 7020).empty());
  const auto ends = sites(records, 7001);
  CHECK(ends.size() == 1 && field<std::uint32_t>(ends[0].body, 24) == 1);
  CHECK(field<ErrorCode>(ends[0].body, 28) == ErrorCode::interrupted);
  CHECK(sites(records, 7006).size() >= 1);
}
#if defined(__linux__)
void anchored_proc_acquisition() {
  Fixture fixture;
  const auto root = std::filesystem::path{fixture.directory.data()};
  const auto target = root / "target", replacement = root / "replacement",
             moved = root / "moved";
  std::filesystem::create_directory(target);
  std::filesystem::create_directory(replacement);
  const std::array original{std::byte{'a'}, std::byte{'('}, std::byte{')'},
                            std::byte{0}, std::byte{255}};
  const auto file =
      ::open((target / "stat").c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  CHECK(file >= 0);
  write_all(file, original);
  CHECK(::close(file) == 0);
  const auto fd =
      ::open(target.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  CHECK(fd >= 0);
  std::filesystem::rename(target, moved);
  std::filesystem::create_directory_symlink(replacement, target);
  auto runtime = fixture.runtime();
  {
    RawThreadBinding binding{need(runtime->prepare_thread())};
    auto sampler = need(RawResourceSampler::open());
    need(sampler->acquire_directory({71, ::getpid(), method}, fd));
    need(sampler->finish());
  }
  CHECK(::close(fd) == 0);
  runtime.reset();
  const auto records = fixture.records();
  const auto reads = sites(records, 7020);
  CHECK(reads.size() == 2 && reads[0].body.size() == 32 + original.size());
  CHECK(std::equal(reads[0].body.begin() + 32, reads[0].body.end(), original.begin()));
  CHECK(field<ssize_t>(reads[1].body, 24) == 0);
  CHECK(sites(records, 7015).empty());
}
void wrong_proc_mount() {
  Fixture fixture;
  auto runtime = fixture.runtime();
  {
    RawThreadBinding binding{need(runtime->prepare_thread())};
    RawSamplerOptions options;
    options.proc_root = fixture.directory.data();
    const auto sampler = RawResourceSampler::open(options);
    CHECK(!sampler.has_value() && sampler.error().code == ErrorCode::wrong_environment);
  }
  runtime.reset();
  const auto records = fixture.records();
  CHECK(sites(records, 7014).size() == 1 && sites(records, 7015).empty());
  CHECK(sites(records, 7004).empty() && sites(records, 7006).size() == 1);
  CHECK(sites(records, 7023).size() == 1 && sites(records, 7024).size() == 1);
  CHECK(sites(records, 7001).size() == 1);
}
#endif
void observe_dead(pid_t pid) {
  for (;;) {
#if defined(__APPLE__)
    proc_bsdinfo observed{};
    const auto count =
        ::proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &observed, sizeof(observed));
    if (count == 0 || observed.pbi_status == SZOMB)
      return;
    CHECK(count == static_cast<int>(sizeof(observed)));
#else
    const auto path = "/proc/" + std::to_string(pid) + "/stat";
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      CHECK(errno == ENOENT);
      return;
    }
    std::array<char, 1024> bytes{};
    const auto count = ::read(fd, bytes.data(), bytes.size());
    CHECK(::close(fd) == 0);
    if (count < 0) {
      CHECK(errno == ESRCH);
      return;
    }
    CHECK(count > 0);
    const std::string_view raw{bytes.data(), static_cast<std::size_t>(count)};
    const auto end = raw.rfind(')');
    CHECK(end != std::string_view::npos && end + 2 < raw.size());
    if (raw[end + 2] == 'Z')
      return;
#endif
    std::this_thread::yield();
  }
}
void killed_sampler() {
  Fixture fixture;
  Child target;
  CapturedCalls calls;
  const auto ready = pipe_pair(), gate = pipe_pair(), pause = pipe_pair();
  constexpr RawCapacities capacities{65536, 8192, 4096};
  struct Report {
    std::array<pid_t, 2> holders;
    IdentityBytes lane;
  };
  const auto child = ::fork();
  CHECK(child >= 0);
  if (child == 0) {
    (void)::close(ready[0]);
    (void)::close(gate[1]);
    (void)::close(pause[1]);
    auto runtime = fixture.runtime({capacities, 4096});
    const auto context = need(runtime->prepare_thread());
    const auto status = need(runtime->status());
    RawThreadBinding binding{context};
    const Report report{{status.supervisor, status.recorder}, raw_lane_epoch()};
    write_all(ready[1], std::as_bytes(std::span{&report, std::size_t{1}}));
    char byte{};
    CHECK(::read(gate[0], &byte, 1) == 1);
    capture->owner = ::getpid();
    capture->target = target.pid;
    capture->enabled = true;
    auto sampler = need(RawResourceSampler::open());
    need(sampler->acquire({91, target.pid, method}));
    capture->enabled = false;
    const std::array acquired{std::byte{1}};
    write_all(ready[1], acquired);
    (void)::read(pause[0], &byte, 1);
    ::_exit(71);
  }
  track_process(child, true);
  CHECK(::close(ready[1]) == 0 && ::close(gate[0]) == 0 && ::close(pause[0]) == 0);
  Report report{};
  std::size_t received = 0;
  auto raw = std::as_writable_bytes(std::span{&report, std::size_t{1}});
  while (received < raw.size()) {
    const auto count = ::read(ready[0], raw.data() + received, raw.size() - received);
    CHECK(count > 0);
    received += static_cast<std::size_t>(count);
  }
  for (const auto pid : report.holders) {
    track_process(pid, true);
    CHECK(::kill(pid, SIGSTOP) == 0);
    observe_stopped(pid);
  }
  const std::array proceed{std::byte{1}};
  write_all(gate[1], proceed);
  char acquired{};
  CHECK(::read(ready[0], &acquired, 1) == 1 && acquired == 1);
  std::string channel_name = "/bb";
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 12; ++i) {
    const auto value = std::to_integer<unsigned>(report.lane[i]);
    channel_name += hex[value >> 4];
    channel_name += hex[value & 15U];
  }
  auto channel = need(RawChannel::open(channel_name, report.lane, capacities));
  const auto pending = need(channel.cursors(RawPartition::data));
  CHECK(pending.published > 0 && pending.acknowledged == 0 && capture->count > 0);
  std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> pending_records;
  for (const auto partition : {RawPartition::data, RawPartition::control}) {
    const auto cursors = need(channel.cursors(partition));
    CHECK(cursors.acknowledged == 0);
    const auto copy = need(channel.read(partition, 0, 65536));
    CHECK(copy.end == cursors.published);
    ByteView remaining{copy.bytes};
    while (!remaining.empty()) {
      const auto size = field<std::uint32_t>(remaining, 0);
      CHECK(size >= 16 && size <= remaining.size());
      const auto site = field<std::uint32_t>(remaining, 4);
#if defined(__APPLE__)
      const bool selected = site == 7022;
#else
      const bool selected = site == 7020;
#endif
      if (selected)
        pending_records.emplace_back(
            field<std::uint64_t>(remaining, 8),
            std::vector<std::byte>{remaining.begin() + 16, remaining.begin() + size});
      remaining = remaining.subspan(size);
    }
  }
  std::sort(
      pending_records.begin(), pending_records.end(),
      [](const auto &left, const auto &right) { return left.first < right.first; });
  std::vector<std::vector<std::byte>> originals;
  for (auto &record : pending_records)
    originals.push_back(std::move(record.second));
  CHECK(originals.size() == capture->count);
#if defined(__APPLE__)
  CHECK(originals.size() == 1 && capture->observations[0].size == 96);
  CHECK(std::equal(originals[0].begin() + 28, originals[0].end(),
                   capture->observations[0].bytes.begin()));
#else
  CHECK(originals.size() >= 2);
  for (std::size_t i = 0; i < originals.size(); ++i) {
    CHECK(field<ssize_t>(originals[i], 24) == capture->observations[i].read_result);
    if (capture->observations[i].read_result > 0)
      CHECK(std::equal(originals[i].begin() + 32, originals[i].end(),
                       capture->observations[i].bytes.begin(),
                       capture->observations[i].bytes.begin() +
                           static_cast<std::ptrdiff_t>(capture->observations[i].size)));
  }
#endif
  const auto before = need(discover_raw_runs(fixture.directory.data()));
  CHECK(before.size() == 1);
  const auto original =
      std::filesystem::path{fixture.directory.data()} / "raw" / before.front().name;
  CHECK(::kill(child, SIGKILL) == 0);
  int status{};
  CHECK(::waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
        WTERMSIG(status) == SIGKILL);
  release_process(child);
  for (const auto pid : report.holders) {
    CHECK(::kill(pid, SIGKILL) == 0);
    observe_dead(pid);
  }
  // A native process observation does not establish catalogue-lease release.
  // Observe that actual lease before asking another owner to recover the run.
  const int lease = ::open((original / "lanes").c_str(), O_RDWR | O_CLOEXEC);
  CHECK(lease >= 0);
  while (::flock(lease, LOCK_EX | LOCK_NB) != 0) {
    CHECK(errno == EWOULDBLOCK);
    std::this_thread::yield();
  }
  CHECK(::flock(lease, LOCK_UN) == 0 && ::close(lease) == 0);
  for (const auto pid : report.holders)
    release_process(pid);
  for (const auto fd : {ready[0], gate[1], pause[1]})
    CHECK(::close(fd) == 0);
  auto recovered = fixture.runtime({capacities, 4096});
  recovered.reset();
  auto reader = need(RawRunReader::open(original.string()));
  CHECK(reader->snapshot().closed);
  std::array<std::byte, 8192> output{};
  std::vector<std::pair<std::uint64_t, std::vector<std::byte>>> recovered_records;
  bool fake_end = false;
  for (;;) {
    const auto record = need(reader->read(output));
    if (!record.has_value())
      break;
    const auto site = field<std::uint32_t>(record->raw, 4);
#if defined(__APPLE__)
    if (site == 7022)
      recovered_records.emplace_back(
          field<std::uint64_t>(record->raw, 8),
          std::vector<std::byte>{record->raw.begin() + 16, record->raw.end()});
#else
    if (site == 7020)
      recovered_records.emplace_back(
          field<std::uint64_t>(record->raw, 8),
          std::vector<std::byte>{record->raw.begin() + 16, record->raw.end()});
#endif
    if (site == 7001)
      fake_end = true;
  }
  std::sort(
      recovered_records.begin(), recovered_records.end(),
      [](const auto &left, const auto &right) { return left.first < right.first; });
  std::vector<std::vector<std::byte>> observed;
  for (auto &record : recovered_records)
    observed.push_back(std::move(record.second));
  if (fake_end || observed.size() != capture->count)
    std::fprintf(stderr, "killed sampler: end=%d preserved=%zu native=%zu closed=%d\n",
                 fake_end, observed.size(), capture->count, reader->snapshot().closed);
  CHECK(!fake_end && observed == originals);
}
void cli() {
  Fixture fixture;
  Child target;
  const auto pipe = pipe_pair();
  const auto first = encode_raw_sampler_request({41, target.pid, method});
  const auto second = encode_raw_sampler_request({42, target.pid, method});
  write_all(pipe[1], first);
  write_all(pipe[1], second);
  CHECK(::close(pipe[1]) == 0);
  const auto fd = std::to_string(pipe[0]);
  const auto child = ::fork();
  CHECK(child >= 0);
  if (child == 0) {
    ::execl(BLACKBIRD_RAW_SAMPLER_PATH, BLACKBIRD_RAW_SAMPLER_PATH, "pipe",
            fixture.directory.data(), "01000000000000000000000000000000", fd.c_str(),
            static_cast<char *>(nullptr));
    ::_exit(127);
  }
  CHECK(::close(pipe[0]) == 0);
  int status{};
  CHECK(::waitpid(child, &status, 0) == child);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  const auto records = fixture.records();
  const auto requests = sites(records, 7004);
  CHECK(requests.size() == 2 && field<std::uint64_t>(requests[0].body, 24) == 41 &&
        field<std::uint64_t>(requests[1].body, 24) == 42);
  CHECK(sites(records, 7003).size() == 3 && sites(records, 7005).size() == 2);
}
} // namespace

extern "C" int poll(pollfd *watches, nfds_t count, int timeout) {
  using Function = int (*)(pollfd *, nfds_t, int);
  static const auto function = next_function<Function>("poll");
  const int stop = watched_stop.load();
  if (stop >= 0 && timeout != 0) {
    for (nfds_t i = 0; i < count; ++i) {
      if (watches[i].fd == stop) {
        const int notice = poll_notice.exchange(-1);
        if (notice >= 0) {
          const char byte = 'p';
          CHECK(::write(notice, &byte, 1) == 1);
        }
        break;
      }
    }
  }
  return function(watches, count, timeout);
}
extern "C" int clock_gettime(clockid_t clock_method, timespec *value)
#if defined(__linux__)
    noexcept
#endif
{
  using Function = int (*)(clockid_t, timespec *);
  static const auto function = next_function<Function>("clock_gettime");
  const int result = function(clock_method, value), error = errno;
  if (capture && capture->enabled && capture->owner == ::getpid()) {
    CHECK(capture->clock_count < capture->clocks.size());
    auto &observed = capture->clocks[capture->clock_count++];
    observed.method = clock_method;
    observed.result = result;
    observed.error = error;
    if (result == 0)
      observed.value = *value;
  }
  errno = error;
  return result;
}
#if defined(__APPLE__)
extern "C" kern_return_t mach_timebase_info(mach_timebase_info_t value) {
  using Function = kern_return_t (*)(mach_timebase_info_t);
  static const auto function = next_function<Function>("mach_timebase_info");
  const auto result = function(value);
  if (capture && capture->enabled && capture->owner == ::getpid()) {
    capture->setup.result = result;
    if (result == KERN_SUCCESS) {
      capture->setup.size = sizeof(*value);
      std::memcpy(capture->setup.bytes.data(), value, sizeof(*value));
    }
  }
  return result;
}
#else
extern "C" int fstatfs(int fd, struct statfs *value) noexcept {
  using Function = int (*)(int, struct statfs *);
  static const auto function = next_function<Function>("fstatfs");
  const int result = function(fd, value), error = errno;
  if (capture && capture->enabled && capture->owner == ::getpid()) {
    capture->setup.result = result;
    capture->setup.error = error;
    if (result == 0) {
      capture->setup.size = sizeof(*value);
      std::memcpy(capture->setup.bytes.data(), value, sizeof(*value));
    }
  }
  errno = error;
  return result;
}
#endif

#if defined(__APPLE__)
extern "C" int proc_pid_rusage(int pid, int flavor, rusage_info_t *buffer) {
  using Function = int (*)(int, int, rusage_info_t *);
  static const auto function = next_function<Function>("proc_pid_rusage");
  const int result = function(pid, flavor, buffer), error = errno;
  if (capture && capture->enabled && capture->owner == ::getpid() &&
      capture->target == pid) {
    CHECK(capture->count < capture->observations.size());
    auto &observation = capture->observations[capture->count++];
    observation.result = result;
    observation.error = error;
    if (result == 0 && flavor == RUSAGE_INFO_V0) {
      observation.size = sizeof(rusage_info_v0);
      std::memcpy(observation.bytes.data(), buffer, observation.size);
    }
  }
  errno = error;
  return result;
}
#else
extern "C" int openat(int fd, const char *name, int flags, ...) {
  using Function = int (*)(int, const char *, int, ...);
  static const auto function = next_function<Function>("openat");
  mode_t mode = 0;
  if ((flags & O_CREAT) != 0) {
    va_list values;
    va_start(values, flags);
    mode = static_cast<mode_t>(va_arg(values, int));
    va_end(values);
  }
  const int result = function(fd, name, flags, mode), error = errno;
  if (capture && capture->enabled && capture->owner == ::getpid() &&
      std::strcmp(name, "stat") == 0)
    capture->stat_fd = result;
  errno = error;
  return result;
}
#endif
extern "C" ssize_t read(int fd, void *bytes, std::size_t size) {
  using Function = ssize_t (*)(int, void *, std::size_t);
  static const auto function = next_function<Function>("read");
  const auto result = function(fd, bytes, size);
  const int error = errno;
  if (capture && capture->enabled && capture->owner == ::getpid() &&
      capture->stat_fd == fd) {
    CHECK(capture->count < capture->observations.size());
    auto &observation = capture->observations[capture->count++];
    observation.read_result = result;
    observation.error = error;
    if (result > 0) {
      observation.size = static_cast<std::size_t>(result);
      CHECK(observation.size <= observation.bytes.size());
      std::memcpy(observation.bytes.data(), bytes, observation.size);
    }
  }
  if (fd == watched_control.load() && result > 0) {
    const int notice = control_notice.exchange(-1);
    if (notice >= 0) {
      const char byte = 'r';
      CHECK(::write(notice, &byte, 1) == 1);
    }
  }
  errno = error;
  return result;
}

int main() {
  wire_oracle();
  exact_native_calls();
  failures_and_cancel();
  control_bytes();
  capacity_preflight();
  fragmented_command();
  invalid_control();
  stopped_recorder();
#if defined(__linux__)
  anchored_proc_acquisition();
  wrong_proc_mount();
#endif
  killed_sampler();
  cli();
}
