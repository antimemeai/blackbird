#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <new>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
enum class Cut : unsigned { none, partial, sync_error, synced, published };
struct NativeCut {
  std::atomic<Cut> mode{Cut::none};
  std::atomic<unsigned> writes{0}, syncs{0}, reached{0};
  std::atomic<pid_t> stop_recorder{0};
  std::atomic<unsigned> stop_reached{0}, stop_wake{0};
  std::atomic<unsigned> space_notices{0}, stop_arms{0};
  dev_t device{};
  ino_t inode{};
  std::array<char, 512> published{};
  int park = -1;
  int park_write = -1;
};
NativeCut *cut = nullptr;
using Sync = int (*)(int);
using Rename = int (*)(const char *, const char *);
using Poll = int (*)(pollfd *, nfds_t, int);
using Fork = pid_t (*)();
using Send = ssize_t (*)(int, const void *, std::size_t, int);
Sync native_sync = nullptr;
Rename native_rename = nullptr;
Poll native_poll = nullptr;
Fork native_fork = nullptr;
Send native_send = nullptr;
thread_local bool stop_poll = false;
pid_t fixture_owner = 0;
std::atomic<pid_t> cleanup_group{0};
std::array<char, 64> cleanup_directory{};
void cleanup_failed_fixture() noexcept;
extern "C" void abort_cleanup(int) {
  if (::getpid() != fixture_owner)
    return;
  const auto group = cleanup_group.load();
  if (group > 0)
    (void)::kill(-group, SIGKILL);
}
thread_local int space_notice = -1;
bool selected(int descriptor) {
  struct stat info{};
  return cut && cut->mode.load() != Cut::none && ::fstat(descriptor, &info) == 0 &&
         info.st_dev == cut->device && info.st_ino == cut->inode;
}
void park() {
  cut->reached.store(1);
  char value = 0;
  (void)::read(cut->park, &value, 1);
  // A teardown signal or parent EOF ends the fixture child. Never return to
  // RawAudit from this cut, and never swallow teardown EINTR indefinitely.
  ::_exit(96);
}
} // namespace

// Each supervisor starts a fixture-owned group; all replacement recorders stay
// in it. The parent retains this direct child until RawRuntime has joined it.
extern "C" pid_t fork() {
  const bool supervisor = ::getpid() == fixture_owner;
  const auto result = native_fork();
  if (supervisor && result == 0) {
    if (::setpgid(0, 0) != 0)
      ::_exit(97);
    if (cut && cut->park_write >= 0)
      (void)::close(cut->park_write);
  } else if (supervisor && result > 0) {
    cleanup_group.store(result);
    if (::setpgid(result, result) != 0) {
      cleanup_failed_fixture();
      std::abort();
    }
  }
  return result;
}

extern "C" ssize_t pwrite(int descriptor, const void *body, std::size_t byte_count,
                          off_t file_offset) {
  const auto arguments = std::pair{byte_count, file_offset};
  if (selected(descriptor) && arguments.second >= 112) {
    const auto call = cut->writes.fetch_add(1);
    if (cut->mode.load() == Cut::partial) {
      if (call == 0 && arguments.second == 112 && arguments.first >= 19) {
        iovec part{const_cast<void *>(body), 19};
        const auto result = ::pwritev(descriptor, &part, 1, arguments.second);
        if (result != 19)
          std::abort();
        return result;
      }
      cut->reached.store(1);
      errno = EIO;
      return -1;
    }
  }
  iovec part{const_cast<void *>(body), arguments.first};
  return ::pwritev(descriptor, &part, 1, arguments.second);
}
extern "C" int fsync(int descriptor) {
  if (selected(descriptor) && cut->writes.load() != 0) {
    ++cut->syncs;
    if (cut->mode.load() == Cut::sync_error) {
      cut->reached.store(1);
      errno = EIO;
      return -1;
    }
    const auto result = native_sync(descriptor);
    if (result == 0 && cut->mode.load() == Cut::synced)
      park();
    return result;
  }
  return native_sync(descriptor);
}
extern "C" int rename(const char *source, const char *destination) {
  const auto result = native_rename(source, destination);
  if (result == 0 && cut && cut->mode.load() == Cut::published &&
      cut->writes.load() != 0 && std::strcmp(destination, cut->published.data()) == 0)
    park();
  return result;
}
extern "C" ssize_t send(int descriptor, const void *body, std::size_t size, int flags) {
  // Classify before forwarding: the parent can receive a lifecycle reply and
  // arm the later cut before this send returns in the recorder. Producer space
  // notices carry the same byte, but use the distinct STREAM transport.
  const bool selected_reply = cut && cut->stop_recorder.load() == ::getpid() &&
                              size == 1 &&
                              *static_cast<const std::byte *>(body) == std::byte{1};
  int type = 0;
  if (selected_reply) {
    socklen_t length = sizeof(type);
    const int saved = errno;
    if (::getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &length) != 0 ||
        length != sizeof(type))
      std::abort();
    errno = saved;
  }
  const auto result = native_send(descriptor, body, size, flags);
  if (selected_reply && result == 1) {
    if (type == SOCK_DGRAM) {
      stop_poll = true;
      ++cut->stop_arms;
    }
    if (type == SOCK_STREAM)
      ++cut->space_notices;
  }
  return result;
}
extern "C" int poll(pollfd *descriptors, nfds_t descriptor_count, int wait_ms) {
  const auto arguments = std::pair{descriptor_count, wait_ms};
  if (stop_poll && arguments.second == -1) {
    stop_poll = false;
    sigset_t stops, previous, waiting;
    if (sigemptyset(&stops) != 0 || sigaddset(&stops, SIGTERM) != 0 ||
        sigaddset(&stops, SIGUSR1) != 0 ||
        ::sigprocmask(SIG_BLOCK, &stops, &previous) != 0 || sigfillset(&waiting) != 0 ||
        sigdelset(&waiting, SIGTERM) != 0 || sigdelset(&waiting, SIGUSR1) != 0)
      std::abort();
    cut->stop_reached.store(1);
    // The real supervisor signal runs the real handler after the recorder's
    // flag check and before the real poll. No scheduler-dependent signal race.
    if (::sigsuspend(&waiting) != -1 || errno != EINTR ||
        ::sigprocmask(SIG_SETMASK, &previous, nullptr) != 0)
      std::abort();
    const auto result = native_poll(descriptors, arguments.first, 1000);
    cut->stop_wake.store(result > 0 ? 1U : 2U);
    return result;
  }
  if (space_notice >= 0 && arguments.second != 0) {
    for (nfds_t index = 0; index < arguments.first; ++index) {
      int type = 0;
      socklen_t size = sizeof(type);
      if ((descriptors[index].events & POLLIN) != 0 &&
          ::getsockopt(descriptors[index].fd, SOL_SOCKET, SO_TYPE, &type, &size) == 0 &&
          type == SOCK_STREAM) {
        const char notice = 1;
        if (::write(space_notice, &notice, 1) != 1)
          std::abort();
        space_notice = -1;
        break;
      }
    }
  }
  return native_poll(descriptors, arguments.first, arguments.second);
}

namespace {
using namespace blackbird;
void cleanup_failed_fixture() noexcept {
  if (::getpid() != fixture_owner)
    return;
  if (cut)
    cut->mode.store(Cut::none);
  const auto group = cleanup_group.exchange(0);
  if (group > 0) {
    (void)::kill(-group, SIGKILL);
    int status = 0;
    while (::waitpid(group, &status, 0) < 0 && errno == EINTR) {
    }
  }
  if (cleanup_directory[0] == 0)
    return;
  try {
    const auto raw = std::filesystem::path{cleanup_directory.data()} / "raw";
    if (std::filesystem::exists(raw))
      for (const auto &run : std::filesystem::directory_iterator{raw})
        for (const auto &entry : std::filesystem::directory_iterator{run.path()}) {
          const auto name = entry.path().filename().string();
          if (!name.starts_with("lane.") && !name.starts_with("registered."))
            continue;
          const int descriptor = ::open(entry.path().c_str(), O_RDONLY | O_CLOEXEC);
          if (descriptor < 0)
            continue;
          std::array<std::byte, 80> bytes{};
          const auto count = ::read(descriptor, bytes.data(), bytes.size());
          (void)::close(descriptor);
          if (count != 80)
            continue;
          std::array<char, 32> lane_name{};
          std::memcpy(lane_name.data(), bytes.data() + 40, lane_name.size());
          if (lane_name[0] == '/' && lane_name.back() == 0)
            (void)RawChannel::unlink(lane_name.data());
        }
    std::filesystem::remove_all(cleanup_directory.data());
  } catch (...) {
    // Cleanup is bounded to names this fixture owns, even on a native FS error.
  }
}
void check(bool valid, int line) {
  if (!valid) {
    std::fprintf(stderr, "raw_recorder_cuts:%d\n", line);
    cleanup_failed_fixture();
    std::abort();
  }
}
#define CHECK(...) check((__VA_ARGS__), __LINE__)
template <class T> T take(Result<T> result) {
  if (!result.has_value())
    std::fprintf(stderr, "raw_recorder_cuts error %s:%lld\n",
                 error_name(result.error().code),
                 static_cast<long long>(result.error().detail));
  CHECK(result.has_value());
  return std::move(result).value();
}
void take(Result<void> result) { CHECK(result.has_value()); }
template <class Predicate> void until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
  while (!predicate()) {
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  }
}
struct Pipe {
  std::array<int, 2> descriptors{};
  Pipe() { CHECK(::pipe(descriptors.data()) == 0); }
  ~Pipe() {
    CHECK(::close(descriptors[0]) == 0);
    CHECK(::close(descriptors[1]) == 0);
  }
  void consume() const {
    pollfd watch{descriptors[0], POLLIN, 0};
    CHECK(::poll(&watch, 1, 8000) == 1 && (watch.revents & POLLIN) != 0);
    char value = 0;
    CHECK(::read(descriptors[0], &value, 1) == 1 && value == 1);
  }
};
struct Directory {
  std::array<char, 64> name{};
  Directory() {
    const std::string pattern = "/tmp/blackbird-raw-cuts-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), name.begin());
    CHECK(::mkdtemp(name.data()) != nullptr);
    cleanup_directory = name;
  }
  ~Directory() {
    std::filesystem::remove_all(name.data());
    cleanup_directory.fill(0);
  }
};
struct ProcessGuard {
  ~ProcessGuard() {
    // Declared before RawRuntime: its joined supervisor is no longer signalable.
    cleanup_group.store(0);
    if (cut) {
      cut->park = -1;
      cut->park_write = -1;
    }
  }
};
EnvironmentId environment() {
  IdentityBytes bytes{};
  bytes[0] = std::byte{31};
  return take(EnvironmentId::from_bytes(bytes));
}
constexpr std::array<std::byte, 7> literal{std::byte{0},  std::byte{255}, std::byte{23},
                                           std::byte{0},  std::byte{64},  std::byte{2},
                                           std::byte{191}};
std::vector<std::byte> file_bytes(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  CHECK(input.good());
  const auto size = input.tellg();
  CHECK(size >= 0);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  CHECK(input.good());
  return bytes;
}
std::filesystem::path run_path(const Directory &directory) {
  auto entry = std::filesystem::directory_iterator{
      std::filesystem::path{directory.name.data()} / "raw"};
  CHECK(entry != std::filesystem::directory_iterator{});
  const auto result = entry->path();
  CHECK(++entry == std::filesystem::directory_iterator{});
  return result;
}
RawChannel lane(const std::filesystem::path &path, IdentityBytes epoch,
                RawCapacities capacities) {
  for (const auto &entry : std::filesystem::directory_iterator{path}) {
    if (!entry.path().filename().string().starts_with("lane."))
      continue;
    const auto registration = file_bytes(entry.path());
    CHECK(registration.size() == 80);
    if (!std::equal(epoch.begin(), epoch.end(), registration.begin()))
      continue;
    std::array<char, 32> name{};
    std::memcpy(name.data(), registration.data() + 40, name.size());
    return take(RawChannel::open(name.data(), epoch, capacities));
  }
  CHECK(false);
  std::abort();
}
RawChannel failure_lane(const std::filesystem::path &path) {
  for (const auto &entry : std::filesystem::directory_iterator{path}) {
    if (!entry.path().filename().string().starts_with("lane."))
      continue;
    const auto registration = file_bytes(entry.path());
    CHECK(registration.size() == 80);
    RawCapacities capacities{};
    std::memcpy(capacities.data(), registration.data() + 16, sizeof(capacities));
    if (capacities[0] != 16 || capacities[1] != 16)
      continue;
    IdentityBytes epoch{};
    std::memcpy(epoch.data(), registration.data(), epoch.size());
    return lane(path, epoch, capacities);
  }
  CHECK(false);
  std::abort();
}
template <unsigned Width> void add(std::vector<std::byte> &bytes, std::uint64_t value) {
  for (unsigned index = 0; index < Width; ++index) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 255U)});
    value >>= 8;
  }
}
template <std::uint32_t Site>
std::vector<std::byte> envelope(std::uint64_t ordinal, ByteView body) {
  std::vector<std::byte> bytes;
  add<4>(bytes, 16 + body.size());
  add<4>(bytes, Site);
  add<8>(bytes, ordinal);
  bytes.insert(bytes.end(), body.begin(), body.end());
  return bytes;
}
template <class T> void append(std::vector<std::byte> &bytes, const T &value) {
  const auto part = std::as_bytes(std::span{&value, std::size_t{1}});
  bytes.insert(bytes.end(), part.begin(), part.end());
}
struct Record {
  RawOrigin origin;
  std::vector<std::byte> raw;
};
std::vector<Record> records(const std::filesystem::path &path) {
  auto reader = take(RawRunReader::open(path.string()));
  std::array<std::byte, 4096> buffer{};
  std::vector<Record> output;
  while (const auto record = take(reader->read(buffer)))
    output.push_back({record->origin, {record->raw.begin(), record->raw.end()}});
  return output;
}
void pending_original(RawChannel &channel) {
  const auto cursor = take(channel.cursors(RawPartition::data));
  CHECK(cursor.published == 23 && cursor.acknowledged == 0);
  const auto raw = take(channel.read(RawPartition::data, 0, 23));
  CHECK(raw.start == 0 && raw.end == 23 && raw.bytes == envelope<9200>(1, literal));
}
void native_cut(Cut mode) {
  Directory directory;
  Pipe parking;
  cut->~NativeCut();
  cut = new (cut) NativeCut;
  cut->park = parking.descriptors[0];
  cut->park_write = parking.descriptors[1];
  ProcessGuard process_guard;
  const RawRuntimeOptions options{{4096, 4096, 4096}, 111};
  auto runtime = take(RawRuntime::open(directory.name.data(), environment(), options));
  RawThreadBinding binding{take(runtime->prepare_thread())};
  const auto path = run_path(directory);
  until([&] { return std::filesystem::exists(path / "published"); });
  CHECK(records(path).empty());
  struct stat info{};
  CHECK(::stat((path / "events.0").c_str(), &info) == 0 && info.st_size == 112);
  cut->device = info.st_dev;
  cut->inode = info.st_ino;
  const auto publication = (path / "published").string();
  CHECK(publication.size() < cut->published.size());
  std::copy(publication.begin(), publication.end(), cut->published.begin());
  auto source = lane(path, raw_lane_epoch(), options.producer_capacities);
  auto failure = failure_lane(path);
  const auto header = take(decode_journal_header(file_bytes(path / "events.0")));
  const auto status = take(runtime->status());
  cut->mode.store(mode);
  const std::array<ByteView, 1> parts{literal};
  CHECK(take(raw_emit(9200, parts)) == 1);
  until([&] { return cut->reached.load() != 0; });
  const bool io_error = mode == Cut::partial || mode == Cut::sync_error;
  if (io_error) {
    until([&] { return take(runtime->status()).fenced; });
    CHECK(take(runtime->status()).recorder == 0);
    pending_original(source);
    const auto failures = take(failure.cursors(RawPartition::failure));
    CHECK(failures.published == 103 && failures.acknowledged == 0);
    std::vector<std::byte> expected;
    append(expected, ErrorCode::io);
    append(expected, std::int64_t{EIO});
    append(expected, std::uint64_t{1});
    append(expected, std::uint64_t{112});
    append(expected, std::uint64_t{mode == Cut::partial ? 19U : 143U});
    append(expected, mode == Cut::sync_error);
    expected.insert(expected.end(), header.journal.bytes().begin(),
                    header.journal.bytes().end());
    expected.insert(expected.end(), source.epoch().begin(), source.epoch().end());
    append(expected, RawPartition::data);
    append(expected, std::uint64_t{0});
    append(expected, std::uint64_t{23});
    append(expected, true);
    const auto expected_failure = envelope<8000>(1, expected);
    const auto retained = take(failure.read(RawPartition::failure, 0, 111));
    CHECK(retained.bytes == expected_failure);
    CHECK(cut->writes.load() == (mode == Cut::partial ? 2U : 1U));
    CHECK(cut->syncs.load() == (mode == Cut::partial ? 0U : 1U));
    CHECK(records(path).empty());
    const auto physical = file_bytes(path / "events.0");
    CHECK(physical.size() == (mode == Cut::partial ? 131U : 255U));
    if (mode == Cut::partial) {
      // Header bytes before CRC/payload: native journal magic, version, RAW kind,
      // sequence1 and the first three bytes of batch_first1.
      std::vector<std::byte> expected_tail{std::byte{'A'}, std::byte{'R'},
                                           std::byte{'J'}, std::byte{'1'}};
      add<2>(expected_tail, 1);
      add<2>(expected_tail, static_cast<std::uint16_t>(FrameKind::raw));
      add<8>(expected_tail, 1);
      add<3>(expected_tail, 1);
      CHECK(std::equal(expected_tail.begin(), expected_tail.end(),
                       physical.begin() + 112));
    }
    cut->mode.store(Cut::none);
    take(runtime->recover());
    until([&] {
      return take(source.cursors(RawPartition::data)).acknowledged == 23 &&
             take(failure.cursors(RawPartition::failure)).acknowledged == 103;
    });
    until([&] { return records(path).size() == 2; });
    const auto output = records(path);
    CHECK(output.size() == 2);
    unsigned originals = 0, errors = 0;
    for (const auto &record : output) {
      if (record.origin.lane == source.epoch()) {
        CHECK(record.origin.partition == RawPartition::data &&
              record.origin.source_start == 0);
        CHECK(record.raw == envelope<9200>(1, literal));
        ++originals;
      } else {
        CHECK(record.origin.lane == failure.epoch() &&
              record.origin.partition == RawPartition::failure &&
              record.origin.source_start == 0);
        CHECK(record.raw == expected_failure);
        ++errors;
      }
    }
    CHECK(originals == 1 && errors == 1);
    if (mode == Cut::partial)
      CHECK(file_bytes(path / "events.0") == physical);
  } else {
    CHECK(::kill(status.supervisor, SIGSTOP) == 0);
    pending_original(source);
    const auto at_cut = records(path);
    CHECK(at_cut.size() == (mode == Cut::published ? 1U : 0U));
    if (!at_cut.empty())
      CHECK(at_cut[0].raw == envelope<9200>(1, literal) &&
            at_cut[0].origin.lane == source.epoch() &&
            at_cut[0].origin.partition == RawPartition::data &&
            at_cut[0].origin.source_start == 0);
    CHECK(cut->writes.load() == 1 && cut->syncs.load() == 1);
    cut->mode.store(Cut::none);
    CHECK(::kill(status.recorder, SIGKILL) == 0);
    CHECK(::kill(status.supervisor, SIGCONT) == 0);
    until([&] { return take(source.cursors(RawPartition::data)).acknowledged == 23; });
    until([&] { return records(path).size() == 1; });
    const auto output = records(path);
    CHECK(output.size() == 1 && output[0].raw == envelope<9200>(1, literal));
    CHECK(output[0].origin.lane == source.epoch() &&
          output[0].origin.partition == RawPartition::data &&
          output[0].origin.source_start == 0);
    CHECK(take(failure.cursors(RawPartition::failure)).published == 0);
  }
}
void two_waiting_owners() {
  Directory directory;
  ProcessGuard process_guard;
  const RawRuntimeOptions options{{64, 4096, 4096}, 4096};
  auto runtime = take(RawRuntime::open(directory.name.data(), environment(), options));
  std::array<RawThreadHandle, 2> handles{take(runtime->prepare_thread()),
                                         take(runtime->prepare_thread())};
  const auto status = take(runtime->status());
  CHECK(::kill(status.recorder, SIGSTOP) == 0);
  Pipe first, second;
  std::array<IdentityBytes, 2> epochs{};
  std::array<std::atomic<bool>, 2> ready{};
  std::array<std::thread, 2> owners;
  for (std::size_t index = 0; index < owners.size(); ++index)
    owners[index] = std::thread{[&, index] {
      RawThreadBinding binding{handles[index]};
      epochs[index] = raw_lane_epoch();
      ready[index].store(true);
      std::array<std::byte, 48> full{};
      full[0] = static_cast<std::byte>(index + 1);
      const std::array<ByteView, 1> parts{full};
      CHECK(take(raw_emit(9201, parts)) == 1);
      space_notice = index == 0 ? first.descriptors[1] : second.descriptors[1];
      take(raw_wait_data(23));
      const std::array<ByteView, 1> next{literal};
      CHECK(take(raw_emit(9202, next)) == 2);
    }};
  first.consume();
  second.consume();
  CHECK(ready[0].load() && ready[1].load());
  const auto path = run_path(directory);
  auto first_lane = lane(path, epochs[0], options.producer_capacities);
  auto second_lane = lane(path, epochs[1], options.producer_capacities);
  for (auto *channel : {&first_lane, &second_lane}) {
    const auto cursor = take(channel->cursors(RawPartition::data));
    CHECK(cursor.published == 64 && cursor.acknowledged == 0);
  }
  CHECK(::kill(status.recorder, SIGCONT) == 0);
  for (auto &owner : owners)
    owner.join();
  until([&] {
    return take(first_lane.cursors(RawPartition::data)).acknowledged == 87 &&
           take(second_lane.cursors(RawPartition::data)).acknowledged == 87;
  });
  const auto output = records(path);
  CHECK(output.size() == 4);
  std::array<unsigned, 2> counts{};
  for (const auto &record : output) {
    const auto index = record.origin.lane == epochs[0] ? 0U : 1U;
    CHECK(record.origin.lane == epochs[index] &&
          record.origin.partition == RawPartition::data);
    if (counts[index]++ == 0) {
      std::array<std::byte, 48> full{};
      full[0] = static_cast<std::byte>(index + 1);
      CHECK(record.origin.source_start == 0 && record.raw == envelope<9201>(1, full));
    } else {
      CHECK(record.origin.source_start == 64 &&
            record.raw == envelope<9202>(2, literal));
    }
  }
  CHECK(counts[0] == 2 && counts[1] == 2);
}
void competing_lane_fairness() {
  Directory directory;
  ProcessGuard process_guard;
  constexpr std::uint64_t count = 6144;
  const RawRuntimeOptions options{{count * 23, 4096, 4096}, 4096};
  auto runtime = take(RawRuntime::open(directory.name.data(), environment(), options));
  const auto hot = take(runtime->prepare_thread()),
             cold = take(runtime->prepare_thread());
  const auto status = take(runtime->status());
  CHECK(::kill(status.recorder, SIGSTOP) == 0);
  IdentityBytes hot_epoch{}, cold_epoch{};
  const std::array<ByteView, 1> parts{literal};
  {
    RawThreadBinding binding{hot};
    hot_epoch = raw_lane_epoch();
    for (std::uint64_t ordinal = 1; ordinal <= count; ++ordinal)
      CHECK(take(raw_emit(9203, parts)) == ordinal);
  }
  {
    RawThreadBinding binding{cold};
    cold_epoch = raw_lane_epoch();
    CHECK(take(raw_emit(9204, parts)) == 1);
  }
  const auto path = run_path(directory);
  auto hot_lane = lane(path, hot_epoch, options.producer_capacities);
  auto cold_lane = lane(path, cold_epoch, options.producer_capacities);
  const auto hot_pending = take(hot_lane.cursors(RawPartition::data));
  CHECK(hot_pending.published == count * 23 && hot_pending.acknowledged == 0);
  const auto cold_pending = take(cold_lane.cursors(RawPartition::data));
  CHECK(cold_pending.published == 23 && cold_pending.acknowledged == 0);
  CHECK(::kill(status.recorder, SIGCONT) == 0);
  until([&] {
    return take(hot_lane.cursors(RawPartition::data)).acknowledged == count * 23 &&
           take(cold_lane.cursors(RawPartition::data)).acknowledged == 23;
  });
  const auto output = records(path);
  CHECK(output.size() == count + 1);
  std::uint64_t seen_hot = 0;
  bool seen_cold = false;
  for (const auto &record : output) {
    CHECK(record.origin.partition == RawPartition::data);
    if (record.origin.lane == hot_epoch) {
      CHECK(record.origin.source_start == seen_hot * 23);
      CHECK(record.raw == envelope<9203>(++seen_hot, literal));
    } else {
      CHECK(record.origin.lane == cold_epoch && !seen_cold && seen_hot <= 4096);
      CHECK(record.origin.source_start == 0 &&
            record.raw == envelope<9204>(1, literal));
      seen_cold = true;
    }
  }
  CHECK(seen_hot == count && seen_cold);
}
void stop_before_poll(bool recover) {
  Directory directory;
  ProcessGuard process_guard;
  const RawRuntimeOptions options{};
  auto runtime = take(RawRuntime::open(directory.name.data(), environment(), options));
  auto handle = take(runtime->prepare_thread());
  IdentityBytes first_epoch{};
  const std::array<ByteView, 1> parts{literal};
  {
    RawThreadBinding binding{handle};
    first_epoch = raw_lane_epoch();
  }
  const auto status = take(runtime->status());
  const auto path = run_path(directory);
  cut->stop_reached.store(0);
  cut->stop_wake.store(0);
  cut->space_notices.store(0);
  cut->stop_arms.store(0);
  cut->stop_recorder.store(status.recorder);
  auto first_lane = lane(path, first_epoch, options.producer_capacities);
  take(first_lane.arm_space());
  {
    RawThreadBinding binding{handle};
    CHECK(take(raw_emit(9205, parts)) == 1);
  }
  until([&] { return cut->space_notices.load() == 1; });
  // A real producer space notification cannot arm the retirement-only stop cut.
  CHECK(cut->stop_arms.load() == 0);
  handle.reset();
  until([&] { return cut->stop_reached.load() == 1; });
  CHECK(cut->stop_arms.load() == 1);
  IdentityBytes second_epoch{};
  if (recover) {
    take(runtime->recover());
    CHECK(take(runtime->status()).recorder != status.recorder);
    RawThreadBinding binding{take(runtime->prepare_thread())};
    second_epoch = raw_lane_epoch();
    CHECK(take(raw_emit(9206, parts)) == 1);
  }
  runtime.reset();
  CHECK(cut->stop_wake.load() == 1);
  cut->stop_recorder.store(0);
  CHECK(std::filesystem::exists(path / "closed"));
  const auto output = records(path);
  CHECK(output.size() == (recover ? 2U : 1U));
  CHECK(output[0].origin.lane == first_epoch &&
        output[0].origin.partition == RawPartition::data &&
        output[0].origin.source_start == 0 &&
        output[0].raw == envelope<9205>(1, literal));
  if (recover)
    CHECK(second_epoch != first_epoch && output[1].origin.lane == second_epoch &&
          output[1].origin.partition == RawPartition::data &&
          output[1].origin.source_start == 0 &&
          output[1].raw == envelope<9206>(1, literal));
}
} // namespace

int main(int argc, char **argv) {
  native_sync = reinterpret_cast<Sync>(::dlsym(RTLD_NEXT, "fsync"));
  native_rename = reinterpret_cast<Rename>(::dlsym(RTLD_NEXT, "rename"));
  native_poll = reinterpret_cast<Poll>(::dlsym(RTLD_NEXT, "poll"));
  native_fork = reinterpret_cast<Fork>(::dlsym(RTLD_NEXT, "fork"));
  native_send = reinterpret_cast<Send>(::dlsym(RTLD_NEXT, "send"));
  CHECK(native_sync && native_rename && native_poll && native_fork && native_send);
  fixture_owner = ::getpid();
  struct sigaction abort_action{};
  abort_action.sa_handler = abort_cleanup;
  abort_action.sa_flags = static_cast<int>(SA_RESETHAND);
  CHECK(sigemptyset(&abort_action.sa_mask) == 0);
  CHECK(::sigaction(SIGABRT, &abort_action, nullptr) == 0);
  static_assert(std::atomic<pid_t>::is_always_lock_free);
  void *mapping = ::mmap(nullptr, sizeof(NativeCut), PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANON, -1, 0);
  CHECK(mapping != MAP_FAILED);
  cut = new (mapping) NativeCut;
  static_assert(std::atomic<unsigned>::is_always_lock_free);
  static_assert(std::atomic<Cut>::is_always_lock_free);
  CHECK(argc == 1 || (argc == 2 && std::string_view{argv[1]} == "stop"));
  if (argc == 1) {
    for (const auto mode :
         {Cut::partial, Cut::sync_error, Cut::synced, Cut::published}) {
      std::fprintf(stderr, "raw_recorder_cuts: native cut %u\n",
                   static_cast<unsigned>(mode));
      native_cut(mode);
    }
    std::fputs("raw_recorder_cuts: two full owners\n", stderr);
    two_waiting_owners();
    std::fputs("raw_recorder_cuts: hot/cold source-count fairness\n", stderr);
    competing_lane_fairness();
  }
  std::fputs("raw_recorder_cuts: stop before poll, graceful drain\n", stderr);
  stop_before_poll(false);
  std::fputs("raw_recorder_cuts: stop before poll, explicit recovery\n", stderr);
  stop_before_poll(true);
  cut->~NativeCut();
  cut = nullptr;
  CHECK(::munmap(mapping, sizeof(NativeCut)) == 0);
}
