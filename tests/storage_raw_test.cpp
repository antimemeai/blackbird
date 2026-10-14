// Direct whole physical-storage owner fixture. Genuine prior baseline is preserved
// separately.
#include "blackbird/archive_catalog.hpp"
#include "blackbird/context.hpp"
#include "blackbird/journal_storage.hpp"
#include "blackbird/journal_writer.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/recovery_index.hpp"
#include "blackbird/saved_state.hpp"
#include "blackbird/terminal.hpp"
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc.h>
#endif
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace blackbird;
namespace {
thread_local bool armed = false;
thread_local bool reject_allocations = false;
thread_local std::size_t refused_allocations = 0;
thread_local dev_t selected_device{};
thread_local ino_t selected_inode{};
thread_local unsigned calls = 0;
thread_local bool refuse_worker_launch = false;
thread_local unsigned refused_worker_launches = 0;
thread_local ssize_t original_return = -1;
thread_local std::array<std::byte, 4096> original_bytes{};
enum class DiagnosticFault { kernel_error, zero, unlink_error };
struct DiagnosticFailure {
  DiagnosticFault fault;
  const char *path = nullptr;
  int cancellation_writer = -1, descriptor = -1;
  dev_t device{};
  ino_t inode{};
  IdentityBytes issuer{};
  RawChannel *channel = nullptr;
  std::uint64_t cleanup_data = 0;
  std::array<pid_t, 2> holders{-1, -1};
  std::vector<RawControlToken> pressure;
  std::optional<RawCapacityClaim> data_pressure;
  ssize_t returned = -2;
  int native_errno = 0, unlink_errno = 0;
  unsigned writes = 0, closes = 0, unlinks = 0;
  bool armed = true, stopped = false;
  explicit DiagnosticFailure(DiagnosticFault selected) : fault(selected) {
    pressure.reserve(1056);
  }
  void stop_and_fill();
  ~DiagnosticFailure() {
    if (stopped)
      for (auto process : holders)
        (void)::kill(process, SIGCONT);
  }
};
thread_local DiagnosticFailure *diagnostic_failure = nullptr;
enum class DirectoryFault { none, missing_nul, inconsistent_length, long_name };
thread_local DirectoryFault directory_fault = DirectoryFault::none;
thread_local DIR *selected_stream = nullptr;
thread_local std::vector<std::byte> supplied_directory_entry;
thread_local unsigned stream_closes = 0;
thread_local bool fail_directory_close = false, fail_directory_conversion = false;
struct AcquiredPrefix {
  ssize_t returned = -1;
  std::array<std::byte, 4096> bytes{};
};
AcquiredPrefix *shared_prefix = nullptr;
thread_local int cut_ready = -1, cut_pause = -1;
struct ColdCloseProbe {
  dev_t device{};
  ino_t inode{};
  std::atomic_uint calls{0};
  std::atomic_bool closed{false};
};
std::array<ColdCloseProbe, 4> cold_close_probes;
std::atomic_bool watch_cold_closes{false};
struct ColdCloseGate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false;
};
ColdCloseGate *cold_close_gate = nullptr;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error{message};
}
template <class T> T identity(unsigned n) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(n);
  return unwrap(T::from_bytes(bytes));
}
template <class T> T field(ByteView bytes, std::size_t offset) {
  require(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset,
          "short literal native field");
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}
} // namespace
extern "C" int
pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
               void *(*start)(void *),
               void *argument) noexcept(noexcept(::pthread_create(thread, attributes,
                                                                  start, argument))) {
  using Native =
      int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
  static const auto forward = [] {
#if defined(__linux__)
    // The Linux ASan runtime is linked into this executable. RTLD_NEXT alone
    // skips its interceptor, leaving actual threads absent from ASan's registry.
    if (auto *interceptor = ::dlsym(RTLD_DEFAULT, "__interceptor_pthread_create"))
      return reinterpret_cast<Native>(interceptor);
#endif
    return reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "pthread_create"));
  }();
  if (!forward)
    std::abort();
  if (refuse_worker_launch) {
    refuse_worker_launch = false;
    ++refused_worker_launches;
    return EAGAIN;
  }
  return forward(thread, attributes, start, argument);
}
#if defined(__linux__)
extern "C" int __real_close(int);
int native_close(int descriptor) { return __real_close(descriptor); }
extern "C" int __wrap_close(int descriptor) {
#else
int native_close(int descriptor) {
  using Native = int (*)(int);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "close"));
  if (!forward)
    std::abort();
  return forward(descriptor);
}
extern "C" int close(int descriptor) {
#endif
  if (diagnostic_failure && descriptor == diagnostic_failure->descriptor) {
    const auto saved_errno = errno;
    struct stat incarnation{};
    const auto found = ::fstat(descriptor, &incarnation);
    const bool owned = found == 0 && incarnation.st_dev == diagnostic_failure->device &&
                       incarnation.st_ino == diagnostic_failure->inode;
    const bool lost = found < 0 && errno == EBADF &&
                      diagnostic_failure->fault == DiagnosticFault::kernel_error &&
                      raw_current_thread() &&
                      raw_lane_epoch() != diagnostic_failure->issuer;
    if (owned || lost)
      ++diagnostic_failure->closes;
    errno = saved_errno;
  }
  ColdCloseProbe *selected = nullptr;
  if (watch_cold_closes.load(std::memory_order_acquire)) {
    const auto before = errno;
    struct stat incarnation{};
    if (::fstat(descriptor, &incarnation) == 0)
      for (auto &probe : cold_close_probes)
        if (!probe.closed.load(std::memory_order_acquire) &&
            probe.device == incarnation.st_dev && probe.inode == incarnation.st_ino) {
          selected = &probe;
          if (&probe == &cold_close_probes.front() && cold_close_gate) {
            std::unique_lock lock{cold_close_gate->mutex};
            cold_close_gate->entered = true;
            cold_close_gate->changed.notify_one();
            cold_close_gate->changed.wait(lock,
                                          [] { return cold_close_gate->released; });
          }
          break;
        }
    errno = before;
  }
  const auto returned = native_close(descriptor);
  const auto native_errno = errno;
  if (selected) {
    ++selected->calls;
    selected->closed.store(returned == 0 && ::fcntl(descriptor, F_GETFD) == -1 &&
                           errno == EBADF);
  }
  errno = native_errno;
  return returned;
}
#if defined(__linux__)
extern "C" ssize_t __real_write(int, const void *, std::size_t);
extern "C" ssize_t __wrap_write(int descriptor, const void *input, std::size_t count) {
  const auto forward = __real_write;
#else
extern "C" ssize_t write(int descriptor, const void *input, std::size_t count) {
  using Native = ssize_t (*)(int, const void *, std::size_t);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "write"));
  if (!forward)
    std::abort();
#endif
  auto *cut = diagnostic_failure;
  struct stat requested{}, selected{};
  const int original_errno = errno;
  const bool applies = cut && cut->armed && ::fstat(descriptor, &requested) == 0 &&
                       ::stat(cut->path, &selected) == 0 &&
                       requested.st_dev == selected.st_dev &&
                       requested.st_ino == selected.st_ino;
  errno = original_errno;
  if (!applies)
    return forward(descriptor, input, count);
  cut->armed = false;
  cut->descriptor = descriptor;
  cut->device = requested.st_dev;
  cut->inode = requested.st_ino;
  ++cut->writes;
  if (cut->fault == DiagnosticFault::kernel_error)
    require(native_close(descriptor) == 0, "actual external selected descriptor loss");
  const auto returned = forward(
      descriptor, input, cut->fault == DiagnosticFault::kernel_error ? count : 0);
  cut->returned = returned;
  cut->native_errno = errno;
  cut->stop_and_fill();
  const char stop = 'x';
  require(forward(cut->cancellation_writer, &stop, 1) == 1,
          "actual sticky diagnostic cleanup cancellation");
  errno = cut->native_errno;
  return returned;
}
#if defined(__linux__)
extern "C" int __real_unlinkat(int, const char *, int);
extern "C" int __wrap_unlinkat(int directory, const char *name, int flags) {
  const auto forward = __real_unlinkat;
#else
extern "C" int unlinkat(int directory, const char *name, int flags) {
  using Native = int (*)(int, const char *, int);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "unlinkat"));
  if (!forward)
    std::abort();
#endif
  auto *cut = diagnostic_failure;
  if (cut && !cut->armed) {
    ++cut->unlinks;
    if (cut->fault == DiagnosticFault::unlink_error)
      require(native_close(directory) == 0, "actual diagnostic directory fd loss");
  }
  const auto result = forward(directory, name, flags);
  const int original_errno = errno;
  if (cut && !cut->armed)
    cut->unlink_errno = original_errno;
  errno = original_errno;
  return result;
}
#if defined(__linux__)
extern "C" dirent *__real_readdir(DIR *);
extern "C" dirent *__wrap_readdir(DIR *stream) {
  const auto forward = __real_readdir;
#else
extern "C" dirent *readdir(DIR *stream) {
  using Native = dirent *(*)(DIR *);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "readdir"));
  if (!forward)
    std::abort();
#endif
  auto *result = forward(stream);
  if (directory_fault == DirectoryFault::none || stream != selected_stream)
    return result;
  require(result != nullptr, "forwarded native entry for record framing cut");
  auto *supplied = reinterpret_cast<dirent *>(supplied_directory_entry.data());
  // Native framing fault/extended-record outputs are prepared before acquisition.
  // Ordinary calls continue to forward the actual libc/kernel result.
  supplied->d_ino = result->d_ino;
#if defined(__APPLE__)
  supplied->d_seekoff = result->d_seekoff;
#else
  supplied->d_off = result->d_off;
#endif
  directory_fault = DirectoryFault::none;
  return supplied;
}
#if defined(__linux__)
extern "C" int __real_closedir(DIR *);
extern "C" int __wrap_closedir(DIR *stream) {
  const auto forward = __real_closedir;
#else
extern "C" int closedir(DIR *stream) {
  using Native = int (*)(DIR *);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "closedir"));
  if (!forward)
    std::abort();
#endif
  const bool selected = stream == selected_stream;
  if (selected) {
    ++stream_closes;
    if (fail_directory_close)
      require(native_close(::dirfd(stream)) == 0, "actual external directory fd loss");
  }
  return forward(stream);
}
#if defined(__linux__)
extern "C" DIR *__real_fdopendir(int);
extern "C" DIR *__wrap_fdopendir(int descriptor) {
  const auto forward = __real_fdopendir;
#else
extern "C" DIR *fdopendir(int descriptor) {
  using Native = DIR *(*)(int);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "fdopendir"));
  if (!forward)
    std::abort();
#endif
  if (fail_directory_conversion) {
    fail_directory_conversion = false;
    require(native_close(descriptor) == 0, "actual external fdopendir fd loss");
  }
  auto *result = forward(descriptor);
  if (directory_fault != DirectoryFault::none && result) {
    selected_stream = result;
    stream_closes = 0;
  }
  return result;
}
void *operator new(std::size_t count) {
  if (reject_allocations) {
    ++refused_allocations;
    throw std::bad_alloc{};
  }
  if (auto *memory = std::malloc(count ? count : 1))
    return memory;
  throw std::bad_alloc{};
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { ::operator delete(memory); }
void *operator new[](std::size_t count) { return ::operator new(count); }
void operator delete[](void *memory) noexcept { ::operator delete(memory); }
void operator delete[](void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}
void *operator new(std::size_t count, std::align_val_t alignment) {
  if (reject_allocations) {
    ++refused_allocations;
    throw std::bad_alloc{};
  }
  void *memory = nullptr;
  if (::posix_memalign(&memory, static_cast<std::size_t>(alignment),
                       count ? count : 1) == 0)
    return memory;
  throw std::bad_alloc{};
}
void operator delete(void *memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t, std::align_val_t alignment) noexcept {
  ::operator delete(memory, alignment);
}
void *operator new[](std::size_t count, std::align_val_t alignment) {
  return ::operator new(count, alignment);
}
void operator delete[](void *memory, std::align_val_t alignment) noexcept {
  ::operator delete(memory, alignment);
}
void operator delete[](void *memory, std::size_t, std::align_val_t alignment) noexcept {
  ::operator delete(memory, alignment);
}
#if defined(__linux__)
extern "C" ssize_t __real_read(int, void *, std::size_t);
extern "C" ssize_t __wrap_read(int fd, void *output, std::size_t count) {
  const auto forward = __real_read;
#else
extern "C" ssize_t read(int fd, void *output, std::size_t count) {
  using Native = ssize_t (*)(int, void *, std::size_t);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "read"));
  if (!forward)
    std::abort();
#endif
  const int saved = errno;
  struct stat status{};
  const bool selected = armed && ::fstat(fd, &status) == 0 &&
                        status.st_dev == selected_device &&
                        status.st_ino == selected_inode;
  errno = saved;
  if (!selected)
    return forward(fd, output, count);
  ++calls;
  if (calls == 1) {
    const auto result = forward(fd, output, std::min(count, original_bytes.size()));
    const int after = errno;
    original_return = result;
    if (result > 0)
      std::memcpy(original_bytes.data(), output, static_cast<std::size_t>(result));
    errno = after;
    if (shared_prefix) {
      shared_prefix->returned = result;
      if (result > 0)
        std::memcpy(shared_prefix->bytes.data(), output,
                    static_cast<std::size_t>(result));
    }
    reject_allocations = result > 0;
    return result;
  }
  reject_allocations = false;
  if (cut_ready >= 0) {
    const char ready = 1;
    if (::write(cut_ready, &ready, 1) != 1)
      ::_exit(68);
    char resume{};
    if (::read(cut_pause, &resume, 1) != 1)
      ::_exit(68);
  }
  errno = EIO;
  return -1;
}

namespace {
thread_local bool short_write = false;
thread_local bool capture_write = false;
thread_local bool interrupt_write = false;
thread_local bool interrupt_unlock = false;
thread_local bool interrupt_reacquire = false;
thread_local unsigned observed_writes = 0, observed_unlocks = 0;
thread_local std::size_t native_requested = 0;
thread_local off_t native_offset = 0;
thread_local ssize_t native_written = -1;
bool selected_descriptor(int descriptor) {
  const int saved = errno;
  struct stat s{};
  const bool found = ::fstat(descriptor, &s) == 0 && s.st_dev == selected_device &&
                     s.st_ino == selected_inode;
  errno = saved;
  return found;
}
} // namespace
#if defined(__linux__)
extern "C" ssize_t __real_pwrite(int, const void *, std::size_t, off_t);
extern "C" ssize_t __wrap_pwrite(int descriptor, const void *input, std::size_t count,
                                 off_t offset) {
  const auto forward = __real_pwrite;
#else
extern "C" ssize_t pwrite(int descriptor, const void *input, std::size_t count,
                          off_t offset) {
  using Native = ssize_t (*)(int, const void *, std::size_t, off_t);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "pwrite"));
  if (!forward)
    std::abort();
#endif
  if ((short_write || interrupt_write || capture_write) &&
      selected_descriptor(descriptor)) {
    ++observed_writes;
    native_requested = count;
    native_offset = offset;
    if (interrupt_write) {
      errno = EINTR;
      native_written = -1;
      return -1;
    }
    const auto actual =
        forward(descriptor, input,
                short_write ? std::min(count, std::size_t{4}) : count, offset);
    native_written = actual;
    return actual;
  }
  return forward(descriptor, input, count, offset);
}
#if defined(__linux__)
extern "C" int __real_flock(int, int);
extern "C" int __wrap_flock(int descriptor, int operation) {
  const auto forward = __real_flock;
#else
extern "C" int flock(int descriptor, int operation) {
  using Native = int (*)(int, int);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "flock"));
  if (!forward)
    std::abort();
#endif
  if (selected_descriptor(descriptor)) {
    if (operation == LOCK_UN) {
      ++observed_unlocks;
      if (interrupt_unlock) {
        errno = EINTR;
        return -1;
      }
    }
    if ((operation & LOCK_EX) && interrupt_reacquire) {
      errno = EINTR;
      return -1;
    }
  }
  return forward(descriptor, operation);
}
namespace {
thread_local bool capture_stat = false, fail_stat = false;
thread_local struct stat observed_stat{};
thread_local unsigned observed_stats = 0;
} // namespace
#if defined(__linux__)
extern "C" int __real_fstat(int, struct stat *);
extern "C" int __wrap_fstat(int descriptor, struct stat *output) {
  const auto forward = __real_fstat;
#else
extern "C" int fstat(int descriptor, struct stat *output) {
  using Native = int (*)(int, struct stat *);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "fstat"));
  if (!forward)
    std::abort();
#endif
  const int result = forward(descriptor, output);
  const int original_errno = errno;
  if (capture_stat && result == 0 && output->st_dev == selected_device &&
      output->st_ino == selected_inode) {
    observed_stat = *output;
    ++observed_stats;
    if (fail_stat) {
      errno = EBADF;
      return -1;
    }
  }
  errno = original_errno;
  return result;
}
void diagnostic_prefix() {
  char pattern[] = "/tmp/blackbird-storage-prefix-red-XXXXXX";
  const auto *path = ::mkdtemp(pattern);
  if (!path)
    throw std::runtime_error{"mkdtemp failed"};
  std::fprintf(stderr, "preserved fixture: %s\n", path);
  try {
    auto runtime = unwrap(RawRuntime::open(path, identity<EnvironmentId>(119)));
    auto lane = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{lane};
      auto owner = unwrap(RawProductStorageOwner::prepare());
      auto directory = unwrap(NativeJournalDirectory::open(path, owner));
      constexpr std::uint64_t now = 100000;
      const std::string name =
          std::to_string(now + 30ULL * 24 * 60 * 60) + "." + std::string(32, 'd');
      std::array<std::byte, 65536> input{};
      for (std::size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<std::byte>((i * 73 + i / 251) & 255);
      unwrap(directory.store_diagnostic(name, input, now));
      struct stat status{};
      const auto diagnostic = std::filesystem::path{path} / "diagnostics" / name;
      require(::stat(diagnostic.c_str(), &status) == 0 && status.st_size == 65536,
              "real private65536 diagnostic file");
      selected_device = status.st_dev;
      selected_inode = status.st_ino;
      refused_allocations = 0;
      calls = 0;
      armed = true;
      const auto result = directory.read_diagnostic(name, now);
      armed = false;
      reject_allocations = false;
      require(refused_allocations == 0,
              "no allocation between actual native acquire and immediate "
              "publication/next native request");
      require(!result.has_value() && result.error() == Error{ErrorCode::io, EIO},
              "actual second-read EIO propagates");
      require(
          calls == 2 && original_return == 4096 &&
              std::equal(original_bytes.begin(), original_bytes.end(), input.begin()),
          "actual forwarded first read acquired exact prefix before EIO");
    }
    lane.reset();
    runtime.reset();
    const auto runs = unwrap(discover_raw_runs(path));
    require(runs.size() == 1 && !runs.front().error && runs.front().snapshot &&
                runs.front().snapshot->closed,
            "actual preserved closed raw run");
    auto reader = unwrap(RawRunReader::open(
        (std::filesystem::path{path} / "raw" / runs.front().name).string()));
    std::array<std::byte, 65536 + 512> output{};
    unsigned matches = 0;
    while (const auto row = unwrap(reader->read(output))) {
      const auto bytes = row->raw;
      if (field<std::uint32_t>(bytes, 4) != 3008 || bytes.size() != 49 + 4096)
        continue;
      require(field<ssize_t>(bytes, 40) == original_return && !field<bool>(bytes, 48),
              "native positive read body has actual result and absent errno");
      if (std::equal(original_bytes.begin(), original_bytes.end(), bytes.begin() + 49))
        ++matches;
    }
    require(matches == 1,
            "missing raw storage prefix: native bytes acquired then destroyed by EIO");
    std::filesystem::remove_all(path);
    return;
  } catch (const Error &error) {
    std::fprintf(stderr, "%s:%lld\n", error_name(error.code),
                 static_cast<long long>(error.detail));
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
  }
  throw std::runtime_error{"diagnostic prefix case failed"};
}

namespace {
struct Row {
  RawOrigin origin;
  std::vector<std::byte> raw;
};
std::vector<Row> read_rows(std::string_view path) {
  auto runs = unwrap(discover_raw_runs(path));
  require(runs.size() == 1, "one preserved raw run");
  const auto &run = runs.front();
  if (run.error || !run.snapshot.has_value())
    throw std::runtime_error{"preserved raw run lacks intact snapshot"};
  require(run.snapshot->closed, "preserved closed raw run");
  auto reader = unwrap(RawRunReader::open(
      (std::filesystem::path{path} / "raw" / runs.front().name).string()));
  std::vector<std::byte> buffer(16 * 1024 * 1024 + 1024);
  std::vector<Row> rows;
  while (auto row = unwrap(reader->read(buffer)))
    rows.push_back({row->origin, {row->raw.begin(), row->raw.end()}});
  return rows;
}
std::uint32_t site(const Row &row) { return field<std::uint32_t>(row.raw, 4); }
RawEventRef reference(const Row &row) {
  return {row.origin.lane, field<std::uint64_t>(row.raw, 8)};
}
RawEventRef linked(const Row &row, std::size_t offset = 16) {
  RawEventRef ref;
  require(row.raw.size() >= offset + 24, "literalR bytes");
  std::copy_n(row.raw.begin() + static_cast<std::ptrdiff_t>(offset), 16,
              ref.lane.begin());
  ref.ordinal = field<std::uint64_t>(row.raw, offset + 16);
  return ref;
}
bool same(RawEventRef a, RawEventRef b) {
  return a.lane == b.lane && a.ordinal == b.ordinal;
}
std::string channel_name(IdentityBytes lane);
void observe_process(pid_t process, char state);
void DiagnosticFailure::stop_and_fill() {
  stopped = true;
  for (auto process : holders) {
    require(::kill(process, SIGSTOP) == 0, "actual stopped diagnostic recorder holder");
    observe_process(process, 'T');
  }
  const auto before = unwrap(channel->cursors(RawPartition::data));
  const auto used = before.published - before.acknowledged;
  require(used + cleanup_data < channel->capacities()[0],
          "actual input and prepaid failure-unlink fit before pressure");
  data_pressure.emplace(
      unwrap(raw_claim_data(channel->capacities()[0] - used - cleanup_data)));
  for (const auto count : {256U, 16U, 1U})
    while (true) {
      auto held = raw_try_claim_control(count);
      if (!held.has_value())
        break;
      pressure.push_back(std::move(held).value());
    }
}
struct Fixture {
  char path[64] = "/tmp/blackbird-storage-native-XXXXXX";
  std::unique_ptr<RawRuntime> runtime;
  RawThreadHandle primary;
  explicit Fixture(RawRuntimeOptions options = {}) {
    require(::mkdtemp(path) != nullptr, "native fixture directory");
    std::fprintf(stderr, "physical fixture:%s\n", path);
    runtime = unwrap(RawRuntime::open(path, identity<EnvironmentId>(120), options));
    primary = unwrap(runtime->prepare_thread());
  }
  std::vector<Row> finish() {
    primary.reset();
    runtime.reset();
    return read_rows(path);
  }
  ~Fixture() {
    if (!std::uncaught_exceptions())
      std::filesystem::remove_all(path);
    else
      std::fprintf(stderr, "preserved physical fixture:%s\n", path);
  }
};
class NoUnresolvedAttempts final : public CustodyVerifier {
public:
  Result<void> verify(std::span<const AttemptState> attempts) override {
    require(attempts.empty(), "fixture has no unresolved external attempts");
    return Result<void>::success();
  }
};
void diagnostic_failure_cleanup(DiagnosticFault fault) {
  Fixture fixture;
  std::array<std::byte, 65536> original{};
  for (std::size_t i = 0; i < original.size(); ++i)
    original[i] = static_cast<std::byte>((i * 13 + 7) & 255);
  unsigned observed_closes = 0;
  int observed_unlink_errno = 0;
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    auto channel =
        unwrap(RawChannel::open(channel_name(raw_lane_epoch()), raw_lane_epoch(),
                                {64 * 1024 * 1024, 256 * 1024, 128 * 1024}));
    const auto status = unwrap(fixture.runtime->status());
    constexpr std::uint64_t now = 100000;
    const auto name =
        std::to_string(now + 30ULL * 24 * 60 * 60) + "." + std::string(32, 'e');
    const auto path =
        (std::filesystem::path{fixture.path} / "diagnostics" / name).string();
    int stop[2];
    require(::pipe(stop) == 0, "actual diagnostic cancellation pipe");
    struct ClosePipe {
      int *descriptors;
      ~ClosePipe() {
        ::close(descriptors[0]);
        ::close(descriptors[1]);
      }
    } close_pipe{stop};
    DiagnosticFailure cut{fault};
    cut.path = path.c_str();
    cut.issuer = raw_lane_epoch();
    cut.channel = &channel;
    cut.holders = {status.supervisor, status.recorder};
    cut.cancellation_writer = stop[1];
    cut.cleanup_data = 81 + name.size() + 1;
    diagnostic_failure = &cut;
    struct Clear {
      ~Clear() { diagnostic_failure = nullptr; }
    } clear;
    {
      RawWaitScope cancelled{{{stop[0], -1}, {}}};
      const auto result = directory.store_diagnostic(name, original, now);
      const Error expected{ErrorCode::io,
                           fault == DiagnosticFault::kernel_error ? EBADF : 0};
      require(!result.has_value() && result.error() == expected,
              "primary actual kernel write error/io0 survives full pressure and "
              "cancellation");
    }
    std::fprintf(stderr,
                 "diagnostic native counts: fault%u writes%u closes%u unlinks%u\n",
                 static_cast<unsigned>(fault), cut.writes, cut.closes, cut.unlinks);
    require(cut.writes == 1 && cut.closes == 1 && cut.unlinks == 1,
            "one actual write, held close and prepaid failure unlink");
    require(cut.returned == (fault == DiagnosticFault::kernel_error ? -1 : 0),
            "native forwarding retains actual write return");
    require(std::filesystem::exists(path) == (fault == DiagnosticFault::unlink_error),
            "real namespace deletion matches actual unlink outcome");
    observed_closes = cut.closes;
    observed_unlink_errno = cut.unlink_errno;
  }
  const auto rows = fixture.finish();
  unsigned writes = 0, duplicates = 0, unlinks = 0, terminal = 0;
  std::optional<RawEventRef> action, unlink;
  for (const auto &row : rows)
    if (site(row) == 3002 &&
        field<std::uint8_t>(row.raw, 40) ==
            static_cast<std::uint8_t>(RawStorageActionKind::store_diagnostic))
      action = reference(row);
  for (const auto &row : rows)
    if (site(row) == 3021 && action && same(linked(row), *action) &&
        field<std::uint8_t>(row.raw, 64) == 1)
      unlink = reference(row);
  for (const auto &row : rows) {
    if (site(row) == 3002 &&
        field<std::uint8_t>(row.raw, 40) ==
            static_cast<std::uint8_t>(RawStorageActionKind::store_diagnostic))
      action = reference(row);
    if (site(row) == 3004 && field<std::uint8_t>(row.raw, 40) == 2)
      ++duplicates;
    if (site(row) == 3009 && action && same(linked(row), *action)) {
      require(row.raw.size() == 78 + original.size() &&
                  std::equal(original.begin(), original.end(), row.raw.begin() + 78),
              "one exact original diagnostic native input before effect");
      ++writes;
    }
    if (site(row) == 3021 && action && same(linked(row), *action) &&
        field<std::uint8_t>(row.raw, 64) == 1) {
      require(field<int>(row.raw, 69) == 0,
              "paid actual unlink flags remain original zero");
      unlink = reference(row);
    }
    if (site(row) == 3022 && unlink && same(linked(row), *unlink)) {
      const bool failed = fault == DiagnosticFault::unlink_error;
      require(row.raw.size() == (failed ? 49U : 45U) &&
                  field<int>(row.raw, 40) == (failed ? -1 : 0) &&
                  field<bool>(row.raw, 44) == failed,
              "exact native unlink result separate from caller error");
      if (failed)
        require(field<int>(row.raw, 45) == observed_unlink_errno &&
                    observed_unlink_errno == EBADF,
                "actual failed unlink errno independently retained");
      ++unlinks;
    }
    if (site(row) == 3003 && action && same(linked(row), *action)) {
      require(
          row.raw.size() == 50 && field<std::uint8_t>(row.raw, 40) == 1 &&
              field<std::uint8_t>(row.raw, 41) ==
                  static_cast<std::uint8_t>(ErrorCode::io) &&
              field<std::int64_t>(row.raw, 42) ==
                  (fault == DiagnosticFault::kernel_error ? EBADF : 0),
          "actual API terminal retains first write error, not admission/unlink error");
      ++terminal;
    }
  }
  require(writes == 1 && duplicates == 0 && unlinks == 1 && terminal == 1 &&
              observed_closes == 1,
          "single representation and exact paid diagnostic failure lifecycle");
}
void directory_native_framing(DirectoryFault fault, bool failed_close = false) {
  Fixture fixture;
  const std::size_t name_bytes = fault == DirectoryFault::long_name ? 301 : 5;
  supplied_directory_entry.assign(
      std::max(sizeof(dirent), offsetof(dirent, d_name) + name_bytes), std::byte{});
  auto *entry = reinterpret_cast<dirent *>(supplied_directory_entry.data());
  entry->d_reclen =
      static_cast<decltype(entry->d_reclen)>(offsetof(dirent, d_name) + name_bytes);
  entry->d_type = DT_REG;
  auto *name = reinterpret_cast<char *>(entry) + offsetof(dirent, d_name);
  std::fill_n(name, name_bytes, 'x');
  if (fault == DirectoryFault::long_name ||
      fault == DirectoryFault::inconsistent_length)
    name[name_bytes - 1] = '\0';
#if defined(__APPLE__)
  entry->d_namlen = static_cast<decltype(entry->d_namlen)>(
      fault == DirectoryFault::inconsistent_length ? name_bytes + 3 : name_bytes - 1);
#endif
  const auto expected_name =
      std::vector<std::byte>{reinterpret_cast<std::byte *>(name),
                             reinterpret_cast<std::byte *>(name) + name_bytes};
  unsigned actual_closes = 0;
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    directory_fault = fault;
    fail_directory_close = failed_close;
    const auto result = directory.expire_diagnostics(100000);
    require(
        result.has_value() == (fault == DirectoryFault::long_name),
        "native long name is legal; malformed record refuses before classification");
    if (!result.has_value())
      require(result.error().code == ErrorCode::corrupt,
              "bounded malformed original maps to actual product corrupt");
  }
  actual_closes = stream_closes;
  fail_directory_close = false;
  selected_stream = nullptr;
  directory_fault = DirectoryFault::none;
  const auto rows = fixture.finish();
  unsigned captured = 0, close_returns = 0;
  std::optional<RawEventRef> stream_request, close_request;
  for (const auto &row : rows) {
    if (site(row) == 3023)
      stream_request = reference(row);
    if (site(row) == 3017 && field<std::uint8_t>(row.raw, 65) == 2)
      close_request = reference(row);
  }
  require(stream_request.has_value() && close_request.has_value(),
          "actual stream creation and one selected native close request");
  for (const auto &row : rows) {
    if (site(row) == 3026 && field<bool>(row.raw, 40)) {
#if defined(__APPLE__)
      constexpr std::size_t name_length_offset = 63, payload_offset = 71;
#else
      constexpr std::size_t name_length_offset = 61, payload_offset = 69;
#endif
      if (row.raw.size() == payload_offset + name_bytes &&
          std::equal(expected_name.begin(), expected_name.end(),
                     row.raw.begin() + payload_offset)) {
        require(field<std::size_t>(row.raw, name_length_offset) == name_bytes &&
                    field<decltype(entry->d_reclen)>(row.raw, 58) == entry->d_reclen,
                "original native record and exact bounded name through NUL or "
                "malformed extent");
#if defined(__APPLE__)
        require(field<decltype(entry->d_namlen)>(row.raw, 60) == entry->d_namlen,
                "actual inconsistent native namlen retained unchanged");
#endif
        ++captured;
      }
    }
    if (site(row) == 3018 && same(linked(row), *close_request)) {
      require(field<int>(row.raw, 40) == (failed_close ? -1 : 0) &&
                  field<bool>(row.raw, 44) == failed_close &&
                  row.raw.size() == (failed_close ? 49U : 45U),
              "actual closedir success/failure distinct from malformed product result");
      if (failed_close)
        require(field<int>(row.raw, 45) == EBADF,
                "forwarded actual closedir EBADF retained");
      ++close_returns;
    }
  }
  require(captured == 1 && close_returns == 1 && actual_closes == 1,
          "one exact native extended/malformed acquisition and final stream close");
}
void directory_conversion_failure() {
  Fixture fixture;
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    fail_directory_conversion = true;
    const auto result = directory.expire_diagnostics(100000);
    require(!result.has_value() && result.error() == Error{ErrorCode::io, EBADF},
            "actual fdopendir failure returns original native error");
  }
  const auto rows = fixture.finish();
  unsigned failures = 0, closes = 0;
  for (const auto &row : rows) {
    if (site(row) == 3024 && row.raw.size() == 53 &&
        field<std::uintptr_t>(row.raw, 40) == 0 && field<bool>(row.raw, 48) &&
        field<int>(row.raw, 49) == EBADF)
      ++failures;
    if (site(row) == 3018 && row.raw.size() == 49 && field<int>(row.raw, 40) == -1 &&
        field<bool>(row.raw, 44) && field<int>(row.raw, 45) == EBADF)
      ++closes;
  }
  require(failures == 1 && closes == 1,
          "one actual failed conversion and actual paid descriptor close failure");
}
void opt_out_and_native_originals() {
  Fixture fixture;
  std::array<std::byte, 512> original{};
  for (std::size_t i = 0; i < original.size(); ++i)
    original[i] = static_cast<std::byte>((i * 47) & 255);
  RawEventRef actual_resource;
  IdentityBytes primary_lane{};
  {
    RawThreadBinding binding{fixture.primary};
    primary_lane = raw_lane_epoch();
    // The recorder/reader/default native adapter remains explicitly unobserved.
    auto plain = unwrap(NativeJournalDirectory::open(fixture.path));
    auto unobserved = unwrap(plain.create_exclusive("plain"));
    unwrap(unobserved->write_at(0, original));
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    auto file = unwrap(directory.create_exclusive("observed"));
    actual_resource = file->raw_storage_resource()->reference();
    unwrap(file->lock_writer());
    require(unwrap(file->write_at(17, original)) == original.size(),
            "actual pwrite full input");
    std::array<std::byte, 512> output{};
    require(unwrap(file->read_at(17, output)) == output.size() && output == original,
            "actual preserved native read bytes");
    require(unwrap(file->extent()) == 529, "actual fstat extent");
    unwrap(file->synchronize(SyncStrength::data));
    file->release_writer();
    file->release_writer();
    unwrap(directory.replace_file("observed", "renamed"));
    unwrap(directory.synchronize_directory(SyncStrength::data));
    require(!file->read_at(UINT64_MAX, output).has_value(),
            "invalid original API count admitted, no invalid syscall");
  }
  const auto rows = fixture.finish();
  unsigned writes = 0, reads = 0, unlocks = 0, closes = 0;
  std::optional<IdentityBytes> cleanup;
  for (const auto &row : rows) {
    if (site(row) == 3009) {
      require(row.raw.size() == 86 + original.size(),
              "exact positioned original native input record");
      require(field<off_t>(row.raw, 78) == 17, "actual native offset");
      require(std::equal(original.begin(), original.end(), row.raw.begin() + 86),
              "exact input copied before native write");
      require(row.origin.lane == primary_lane, "ordinary actual issuer");
      ++writes;
    }
    if (site(row) == 3008 && row.raw.size() > 53) {
      require(field<ssize_t>(row.raw, 40) == 512 && !field<bool>(row.raw, 48),
              "native full read return");
      require(std::equal(original.begin(), original.end(), row.raw.begin() + 49),
              "exact acquired output bytes");
      ++reads;
    }
    if (site(row) == 3015 && same(linked(row, 40), actual_resource) &&
        field<int>(row.raw, 69) == LOCK_UN) {
      ++unlocks;
      cleanup = row.origin.lane;
    }
    if (site(row) == 3017 && same(linked(row, 40), actual_resource)) {
      ++closes;
      require(cleanup && row.origin.lane == *cleanup && row.origin.lane != primary_lane,
              "actual close uses dedicated cleanup issuer");
    }
  }
  require(writes == 1 && reads == 1 && unlocks == 1 && closes == 1,
          "one observed original/read/native unlock/close; default adapter is absent");
}
void authority_and_cold_cleanup() {
  Fixture fixture;
  IdentityBytes child_lane{}, primary_lane{};
  RawEventRef first, second;
  {
    RawThreadBinding parent{fixture.primary};
    primary_lane = raw_lane_epoch();
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    std::shared_ptr<JournalFile> main_file =
        unwrap(directory.create_exclusive("main-native"));
    first = main_file->raw_storage_resource()->reference();
    const auto inherited = ::fork();
    require(inherited >= 0, "actual fork for inherited process proof");
    if (inherited == 0) {
      const auto result = main_file->extent();
      ::_exit(!owner.permits_current() && !owner.permits_thread() &&
                      !result.has_value() &&
                      result.error().code == ErrorCode::wrong_incarnation
                  ? 0
                  : 68);
    }
    int inherited_status = 0;
    require(::waitpid(inherited, &inherited_status, 0) == inherited &&
                WIFEXITED(inherited_status) && WEXITSTATUS(inherited_status) == 0,
            "fork inherited TLS/lane cannot authorize actual product acquisition");
    auto child = unwrap(raw_prepare_thread());
    auto dispatch = raw_worker_dispatch();
    auto transfer = unwrap(owner.prepare_handoff(child, dispatch));
    auto other = unwrap(raw_prepare_thread());
    {
      RawThreadBinding unrelated{other};
      require(!main_file->extent().has_value(),
              "unrelated producer refuses before native fstat");
    }
    std::shared_ptr<JournalFile> child_file;
    std::exception_ptr worker_error;
    std::thread worker{[&] {
      try {
        RawThreadBinding binding{child};
        child_lane = raw_lane_epoch();
        raw_worker_started(dispatch);
        RawRegion command{raw_regions::command_perform};
        RawStorageAuthorityScope scope{transfer.get(),
                                       {command.lane_epoch(), command.begin_ordinal()}};
        child_file = unwrap(directory.create_exclusive("child-native"));
        second = child_file->raw_storage_resource()->reference();
        // Reverse case: main-created resource reaches last drop on the child.
        main_file.reset();
      } catch (...) {
        worker_error = std::current_exception();
      }
    }};
    worker.join();
    if (worker_error)
      std::rethrow_exception(worker_error);
    transfer.reset();
    child.reset();
    other.reset();
    require(unwrap(child_file->extent()) == 0,
            "main ordinary access after real child join");
    // Cold lifetime may end on arbitrary test threads; cleanup is serialized.
    std::thread cold{[owned = std::move(child_file)]() mutable { owned.reset(); }};
    cold.join();
  }
  const auto rows = fixture.finish();
  unsigned first_close = 0, second_close = 0;
  std::optional<IdentityBytes> cleanup;
  for (const auto &row : rows)
    if (site(row) == 3017 &&
        (same(linked(row, 40), first) || same(linked(row, 40), second))) {
      if (!cleanup)
        cleanup = row.origin.lane;
      require(row.origin.lane == *cleanup && row.origin.lane != primary_lane &&
                  row.origin.lane != child_lane,
              "shared cleanup sole producer distinct from both creator lanes");
      if (same(linked(row, 40), first))
        ++first_close;
      else
        ++second_close;
    }
  require(first_close == 1 && second_close == 1,
          "actual converse last owner drops close once each");
}
void retained_cold_concurrent_cleanup() {
  Fixture fixture;
  std::array<RawEventRef, 4> resources{};
  IdentityBytes primary_lane{}, child_lane{};
  const std::string literal = "cold-generation";
  std::vector<std::byte> expected{
      std::byte{'B'}, std::byte{'B'}, std::byte{'M'}, std::byte{2}, std::byte{10},
      std::byte{1},   std::byte{8},   std::byte{1},   std::byte{6}, std::byte{15}};
  for (const auto character : literal)
    expected.push_back(static_cast<std::byte>(character));
  {
    RawThreadBinding binding{fixture.primary};
    primary_lane = raw_lane_epoch();
    auto owner = unwrap(RawProductStorageOwner::prepare());
    const JournalHeader header{identity<EnvironmentId>(120),
                               identity<AuditStreamId>(20),
                               7,
                               {32 * 1024 * 1024, 34 * 1024 * 1024},
                               {}};
    const auto directory = [&] {
      return std::make_unique<NativeJournalDirectory>(
          unwrap(NativeJournalDirectory::open(fixture.path, owner)));
    };
    auto root = unwrap(RetainedState::create(directory(), "journal", header,
                                             {64 * 1024 * 1024, 1024}, owner));
    ApplicationRecordId application = identity<ApplicationRecordId>(1);
    {
      AuditLog log{*root};
      application = log.issue();
      (void)log.record(application, ApplicationChannel::program,
                       Value::object({{"label", Value{literal}}}));
    }
    root.reset();
    root = unwrap(RetainedState::open(directory(), "journal", header,
                                      {64 * 1024 * 1024, 1024}, true, true, UINT64_MAX,
                                      owner));
    unwrap(root->confirm_recovery());
    NoUnresolvedAttempts verifier;
    unwrap(root->reconcile(verifier));
    ImmutableBytes payload;
    for (const auto &fact : root->tail_facts())
      if (const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
          record && record->identity == application)
        payload = record->payload;
    require(payload.is_cold() && payload.resident_bytes() == 0 &&
                unwrap(payload.read()) == expected,
            "actual full replay retains a cold immutable original with literal bytes");
    RetainedState::FactHistory worker_history, main_history;
    const auto capture = [&](std::size_t index, std::string_view name,
                             const std::shared_ptr<JournalFile> &file) {
      resources[index] = file->raw_storage_resource()->reference();
      struct stat incarnation{};
      require(::stat((std::filesystem::path{fixture.path} / name).c_str(),
                     &incarnation) == 0,
              "actual retained native file incarnation");
      cold_close_probes[index].device = incarnation.st_dev;
      cold_close_probes[index].inode = incarnation.st_ino;
      cold_close_probes[index].calls.store(0);
      cold_close_probes[index].closed.store(false);
    };
    capture(3, "journal", root->raw_storage_selection().journal);
    ColdCloseGate gate;
    {
      AuditLog log{*root};
      auto context = std::make_unique<ContextStore>(log);
      unwrap(context->checkpoint());
      auto old_history = root->history_snapshot();
      capture(0, "journal.archive." + std::to_string(root->saved_state()->slot),
              root->raw_storage_selection().archive);
      const auto snapshot = root->saved_state()->context;
      context.reset();
      auto child = unwrap(raw_prepare_thread());
      const auto dispatch = raw_worker_dispatch();
      auto transfer = unwrap(root->prepare_raw_transfer(child, dispatch));
      transfer->stage_before_launch();
      cold_close_gate = &gate;
      watch_cold_closes.store(true, std::memory_order_release);
      std::mutex launch_mutex;
      std::condition_variable launch_changed;
      bool launched = false;
      bool worker_done = false;
      std::exception_ptr failure;
      std::thread worker{[&, history = std::move(old_history)]() mutable {
        try {
          {
            std::unique_lock lock{launch_mutex};
            launch_changed.wait(lock, [&] { return launched; });
          }
          RawThreadBinding active{child};
          child_lane = raw_lane_epoch();
          raw_worker_started(dispatch);
          RawRegion command{raw_regions::command_perform};
          RawStorageAuthorityScope authority{
              transfer->handoff(), {command.lane_epoch(), command.begin_ordinal()}};
          AuditLog worker_log{*root};
          worker_log.record(ApplicationChannel::program,
                            Value::object({{"label", Value{"worker-generation"}}}));
          unwrap(root->save_current_state(snapshot));
          worker_history = root->history_snapshot();
          capture(1, "journal.archive." + std::to_string(root->saved_state()->slot),
                  root->raw_storage_selection().archive);
          history = {};
          command.outcome(RawRegionOutcome::returned);
        } catch (...) {
          const std::lock_guard lock{gate.mutex};
          failure = std::current_exception();
          gate.entered = true;
          gate.changed.notify_one();
        }
        {
          const std::lock_guard lock{gate.mutex};
          worker_done = true;
        }
        gate.changed.notify_one();
      }};
      transfer->commit_launch();
      {
        const std::lock_guard lock{launch_mutex};
        launched = true;
      }
      launch_changed.notify_one();
      {
        std::unique_lock lock{gate.mutex};
        gate.changed.wait(lock, [&] { return gate.entered || worker_done; });
        // The real old archive close is waiting inside cleanup, while MAIN emits
        // on its own producer. No Root/Context method runs on MAIN during handoff.
        RawRegion render{raw_regions::command_render};
        render.observe({RawObservationKind::source_bytes, 0, expected.size(), 0});
        render.outcome(RawRegionOutcome::flushed);
        gate.released = true;
      }
      gate.changed.notify_one();
      worker.join();
      cold_close_gate = nullptr;
      if (failure)
        std::rethrow_exception(failure);
      unwrap(transfer->joined());
      transfer.reset();
      child.reset();
      require(cold_close_probes[0].calls.load() == 1 &&
                  cold_close_probes[0].closed.load(),
              "actual MAIN-opened old catalog closes once on worker final drop");
      log.record(ApplicationChannel::program,
                 Value::object({{"label", Value{"main-generation"}}}));
      context = std::make_unique<ContextStore>(log);
      unwrap(context->checkpoint());
      main_history = root->history_snapshot();
      capture(2, "journal.archive." + std::to_string(root->saved_state()->slot),
              root->raw_storage_selection().archive);
    }
    auto unused = RawStorageResource::prepare(owner);
    {
      RawStorageAction action{owner, RawStorageActionKind::open, unused.get()};
      const auto missing =
          (std::filesystem::path{fixture.path} / "missing-native").string();
      require(RawStorageNative::open(action, unused.get(), {}, missing, O_RDONLY) ==
                      -1 &&
                  errno == ENOENT,
              "actual failed native open retains unused cleanup bank");
      action.finish(Error{ErrorCode::io, ENOENT});
    }
    auto plain = unwrap(NativeJournalDirectory::open(fixture.path));
    auto contender = unwrap(plain.open_existing("journal", FileAccess::read_write));
    require(!contender->lock_writer().has_value(),
            "actual live Root owns native writer lease");
    root.reset();
    unwrap(contender->lock_writer());
    contender->release_writer();
    require(cold_close_probes[1].calls.load() == 0 &&
                cold_close_probes[2].calls.load() == 0 &&
                cold_close_probes[3].calls.load() == 0,
            "surviving real histories and cold payload pin native files beyond "
            "Root/Context");
    require(
        !unwrap(worker_history.page(0, 16)).empty() &&
            !unwrap(main_history.page(0, 16)).empty() &&
            unwrap(payload.read()) == expected && payload.resident_bytes() == 0,
        "lawful primary cold reads survive Root destruction without caching originals");
    ColdCloseGate drops;
    unsigned ready = 0;
    const auto holders = unwrap(fixture.runtime->status());
    struct ResumeHolders {
      std::array<pid_t, 2> processes;
      bool stopped = false;
      ~ResumeHolders() {
        if (stopped)
          for (const auto process : processes)
            (void)::kill(process, SIGCONT);
      }
    } resume{{holders.recorder, holders.supervisor}};
    std::vector<RawControlToken> pressure;
    pressure.reserve(1056);
    std::array<int, 2> cancellation{};
    require(::pipe(cancellation.data()) == 0, "actual cold cleanup cancellation pipe");
    require(::close(cancellation[1]) == 0, "actual sticky cancellation EOF");
    const auto await_drop = [&] {
      std::unique_lock lock{drops.mutex};
      ++ready;
      drops.changed.notify_all();
      drops.changed.wait(lock, [&] { return drops.released; });
    };
    std::thread first{
        [&, history = std::move(worker_history), bytes = std::move(payload)]() mutable {
          await_drop();
          history = {};
          bytes = {};
        }};
    std::thread second{
        [&, history = std::move(main_history), bank = std::move(unused)]() mutable {
          await_drop();
          bank.reset();
          history = {};
        }};
    {
      std::unique_lock lock{drops.mutex};
      drops.changed.wait(lock, [&] { return ready == 2; });
      RawRegion render{raw_regions::command_render};
      render.observe({RawObservationKind::source_bytes, 0, expected.size(), 0});
      resume.stopped = true;
      for (const auto process : {holders.recorder, holders.supervisor}) {
        require(::kill(process, SIGSTOP) == 0,
                "actual stopped cold cleanup recorder holder");
        observe_process(process, 'T');
      }
      for (const auto count : {256U, 16U, 1U})
        while (true) {
          auto held = raw_try_claim_control(count);
          if (!held.has_value())
            break;
          pressure.push_back(std::move(held).value());
        }
      RawWaitScope stopped{{{cancellation[0], -1}, {}}};
      const auto refused = raw_try_claim_control(1);
      require(!refused.has_value(),
              "actual MAIN control pressure prevents fresh claims");
      drops.released = true;
      drops.changed.notify_all();
      render.outcome(RawRegionOutcome::flushed);
    }
    first.join();
    second.join();
    for (const auto process : {holders.recorder, holders.supervisor})
      require(::kill(process, SIGCONT) == 0,
              "resume actual cold cleanup recorder holder");
    resume.stopped = false;
    pressure.clear();
    require(::close(cancellation[0]) == 0,
            "actual cold cleanup cancellation pipe close");
    watch_cold_closes.store(false, std::memory_order_release);
    for (std::size_t index = 0; index < cold_close_probes.size(); ++index) {
      const auto &probe = cold_close_probes[index];
      std::fprintf(stderr, "cold native close: index%zu calls%u closed%d\n", index,
                   probe.calls.load(), static_cast<int>(probe.closed.load()));
      require(probe.calls.load() == 1 && probe.closed.load(),
              "each pinned native incarnation reaches one actual close and EBADF");
    }
  }
  const auto rows = fixture.finish();
  std::optional<IdentityBytes> cleanup;
  std::uint64_t last_end = 0, owner_end = 0;
  std::array<unsigned, 4> requests{}, returned{}, endings{};
  std::array<RawEventRef, 4> close_requests{};
  for (const auto &row : rows) {
    if (site(row) == 3017 || site(row) == 3029)
      for (std::size_t index = 0; index < resources.size(); ++index)
        if (same(linked(row, site(row) == 3017 ? 40 : 16), resources[index])) {
          if (!cleanup)
            cleanup = row.origin.lane;
          require(row.origin.lane == *cleanup && row.origin.lane != primary_lane &&
                      row.origin.lane != child_lane,
                  "all final native cleanup uses one dedicated producer");
          if (site(row) == 3017) {
            ++requests[index];
            close_requests[index] = reference(row);
          } else {
            ++endings[index];
            last_end = std::max(last_end, reference(row).ordinal);
          }
        }
    if (site(row) == 3018)
      for (std::size_t index = 0; index < resources.size(); ++index)
        if (same(linked(row), close_requests[index])) {
          require(field<int>(row.raw, 40) == 0 && !field<bool>(row.raw, 44),
                  "actual successful native close return");
          ++returned[index];
        }
    if (site(row) == 3047 && cleanup && row.origin.lane == *cleanup)
      owner_end = reference(row).ordinal;
  }
  for (std::size_t index = 0; index < resources.size(); ++index)
    require(requests[index] == 1 && returned[index] == 1 && endings[index] == 1,
            "one literal native close pair and resource END per retained incarnation");
  require(owner_end > last_end, "cleanup owner END follows every actual resource END");
}
void held_physical_stop() {
  Fixture fixture;
  RawEventRef resource, scope;
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    std::optional<NativeJournalDirectory> directory{
        unwrap(NativeJournalDirectory::open(fixture.path, owner))};
    std::shared_ptr<JournalFile> file =
        unwrap(directory->create_exclusive("settlement"));
    resource = file->raw_storage_resource()->reference();
    int root = 0;
    const RawStorageLoanBudget budget{RawStorageLoanKind::stop, 256, 1, 1, 0, 0, 0, 0};
    auto loan = unwrap(RawStorageSettlementLoan::prepare(&root, owner, budget));
    struct stat target{};
    require(::stat((std::filesystem::path{fixture.path} / "settlement").c_str(),
                   &target) == 0,
            "actual selected native settlement incarnation");
    selected_device = target.st_dev;
    selected_inode = target.st_ino;
    auto channel =
        unwrap(RawChannel::open(channel_name(raw_lane_epoch()), raw_lane_epoch(),
                                {64 * 1024 * 1024, 256 * 1024, 128 * 1024}));
    const auto status = unwrap(fixture.runtime->status());
    struct Resume {
      std::array<pid_t, 2> holders;
      ~Resume() {
        for (auto process : holders)
          (void)::kill(process, SIGCONT);
      }
    } resume{{status.supervisor, status.recorder}};
    for (auto process : resume.holders) {
      require(::kill(process, SIGSTOP) == 0, "actual stopped physical loan recorder");
      observe_process(process, 'T');
    }
    const auto before = unwrap(channel.cursors(RawPartition::data));
    const auto reservation = unwrap(raw_storage_loan_reservation(budget));
    const auto available = channel.capacities()[0] -
                           (before.published - before.acknowledged) - reservation.data;
    std::optional<RawCapacityClaim> data_pressure{unwrap(raw_claim_data(available))};
    std::vector<RawControlToken> pressure;
    pressure.reserve(1056);
    for (const auto count : {256U, 16U, 1U})
      while (true) {
        auto held = raw_try_claim_control(count);
        if (!held.has_value())
          break;
        pressure.push_back(std::move(held).value());
      }
    int stop[2];
    require(::pipe(stop) == 0, "actual cancellation pipe");
    require(::write(stop[1], "x", 1) == 1, "persistent cancellation readiness");
    capture_stat = true;
    observed_stats = 0;
    capture_write = true;
    observed_writes = 0;
    {
      RawWaitScope cancel{{{stop[0], -1}, {}}};
      require(!file->extent().has_value(),
              "ordinary native acquisition refuses sticky cancellation");
      const std::array refused_original{std::byte{0x13}};
      const auto refused_write = file->write_at(0, refused_original);
      require(!refused_write.has_value() &&
                  refused_write.error().code == ErrorCode::interrupted,
              "ordinary write refuses before native input under full DATA/CONTROL");
      RawStorageLoanScope selected{*loan, {&root, owner, file, {}}};
      require(unwrap(file->extent()) == 0,
              "prepaid real fstat succeeds under full caller CONTROL+sticky stop");
      require(
          observed_stats == 1 && observed_writes == 0,
          "ordinary pressure/cancel admits no native stat/write; paid stat is exact");
      capture_stat = false;
      std::array<std::byte, 256> original{};
      original[0] = std::byte{37};
      require(unwrap(file->write_at(0, original)) == 256, "prepaid real full pwrite");
      unwrap(file->synchronize(SyncStrength::data));
    }
    capture_write = false;
    require(observed_writes == 1 && native_requested == 256 && native_written == 256,
            "exact one paid native write, no unpaid acquisition under pressure");
    loan->finish();
    // Last native resource drops use already held cleanup pairs while both holders
    // remain stopped and the ordinary caller's two partitions remain fully reserved.
    file.reset();
    directory.reset();
    const auto after = unwrap(channel.cursors(RawPartition::data));
    require(after.acknowledged == before.acknowledged &&
                after.published > before.published,
            "actual held acquisition published under stopped recorder without reclaim");
    std::array<std::byte, 256> output{};
    {
      std::ifstream input{std::filesystem::path{fixture.path} / "settlement",
                          std::ios::binary};
      input.read(reinterpret_cast<char *>(output.data()),
                 static_cast<std::streamsize>(output.size()));
      require(bool(input) && output[0] == std::byte{37},
              "independent kernel bytes survive full caller pressure and native close");
    }
    ::close(stop[0]);
    ::close(stop[1]);
    pressure.clear();
    data_pressure.reset();
  }
  const auto rows = fixture.finish();
  unsigned writes = 0, ends = 0;
  for (const auto &row : rows) {
    if (site(row) == 3009 && same(linked(row, 40), resource))
      ++writes;
    if (site(row) == 3037) {
      ++ends;
      scope = linked(row);
      require(field<std::uint8_t>(row.raw, 40) == 0,
              "actual prepaid physical scope completed");
    }
  }
  require(writes == 1 && ends == 1 && scope.ordinal,
          "finite physical scope emitted actual writes and held END");
}
void finite_native_failures() {
  for (const bool interrupted : {false, true}) {
    Fixture fixture;
    RawEventRef resource;
    std::array<std::byte, 256> original{};
    for (std::size_t i = 0; i < original.size(); ++i)
      original[i] = static_cast<std::byte>((i * 51) & 255);
    {
      RawThreadBinding binding{fixture.primary};
      auto owner = unwrap(RawProductStorageOwner::prepare());
      auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
      std::shared_ptr<JournalFile> file = unwrap(directory.create_exclusive("finite"));
      resource = file->raw_storage_resource()->reference();
      struct stat s{};
      require(::stat((std::filesystem::path{fixture.path} / "finite").c_str(), &s) == 0,
              "actual selected file identity");
      selected_device = s.st_dev;
      selected_inode = s.st_ino;
      int root = 0;
      auto loan = unwrap(RawStorageSettlementLoan::prepare(
          &root, owner, {RawStorageLoanKind::stop, 256, 1, 1, 0, 0, 0, 0}));
      {
        RawStorageLoanScope scope{*loan, {&root, owner, file, {}}};
        RawStorageWriteContinuation continuation;
        unwrap(file->begin_write_batch(original.size(), continuation));
        observed_writes = 0;
        short_write = !interrupted;
        interrupt_write = interrupted;
        const auto first = file->write_at_continuing(0, original, continuation);
        const auto next = file->write_at_continuing(
            interrupted ? 0 : 4,
            interrupted ? ByteView{original} : ByteView{original}.subspan(4),
            continuation);
        short_write = false;
        interrupt_write = false;
        require(native_requested == 256 && native_offset == 0 && observed_writes == 1,
                "one actual admitted native attempt; no unpaid retry");
        if (interrupted)
          require(!first.has_value() &&
                      first.error() == Error{ErrorCode::interrupted, EINTR} &&
                      native_written == -1,
                  "exact actual EINTR survives");
        else
          require(first.has_value() && first.value() == 4 && native_written == 4,
                  "real forwarded positive-short pwrite");
        require(!next.has_value() && next.error().code == ErrorCode::capacity,
                "finite budget refuses BEFORE next native syscall");
        scope.error(next.error());
      }
      loan->finish();
      require(unwrap(file->extent()) == (interrupted ? 0 : 4),
              "actual kernel prefix is honest after exhausted budget");
    }
    const auto rows = fixture.finish();
    unsigned input = 0, returned = 0;
    for (const auto &row : rows) {
      if (site(row) == 3009 && same(linked(row, 40), resource)) {
        require(row.raw.size() == 342 &&
                    std::equal(original.begin(), original.end(), row.raw.begin() + 86),
                "original full native input survives native failure");
        ++input;
      }
      if (site(row) == 3010) {
        require(field<ssize_t>(row.raw, 40) == (interrupted ? -1 : 4) &&
                    field<bool>(row.raw, 48) == interrupted,
                "native return/failure presence exact");
        if (interrupted)
          require(field<int>(row.raw, 49) == EINTR, "original native errno");
        ++returned;
      }
    }
    require(input == 1 && returned == 1,
            "single acquired input/return, no invented unpaid attempt");
  }
}
void single_unlock_failure() {
  Fixture fixture;
  RawEventRef resource;
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    auto file = unwrap(directory.create_exclusive("unlock"));
    resource = file->raw_storage_resource()->reference();
    unwrap(file->lock_writer());
    struct stat s{};
    require(::stat((std::filesystem::path{fixture.path} / "unlock").c_str(), &s) == 0,
            "actual lease inode");
    selected_device = s.st_dev;
    selected_inode = s.st_ino;
    observed_unlocks = 0;
    interrupt_unlock = true;
    file->release_writer();
    file->release_writer();
    interrupt_unlock = false;
    require(observed_unlocks == 1, "single actual failed unlock, no arbitrary retry");
    auto other = unwrap(directory.open_existing("unlock", FileAccess::read_only));
    require(!other->lock_writer().has_value(),
            "actual failed unlock leaves writer lease held");
    interrupt_reacquire = true;
    const auto reacquired = file->lock_writer();
    interrupt_reacquire = false;
    require(
        !reacquired.has_value() &&
            reacquired.error() == Error{ErrorCode::interrupted, EINTR},
        "freshly admitted failed reacquisition preserves unresolved original lease");
    file->release_writer();
    file->release_writer();
    require(
        observed_unlocks == 2,
        "newly funded unresolved lease releases once, spent repeated release does not");
    unwrap(other->lock_writer());
    file.reset();
    other->release_writer();
  }
  const auto rows = fixture.finish();
  unsigned failed = 0, original_unlocks = 0;
  for (const auto &row : rows)
    if (site(row) == 3015 && same(linked(row, 40), resource) &&
        field<int>(row.raw, 69) == LOCK_UN)
      ++original_unlocks;
    else if (site(row) == 3016 && field<int>(row.raw, 40) == -1 &&
             field<int>(row.raw, 45) == EINTR)
      ++failed;
  require(failed == 2 && original_unlocks == 2,
          "failed unlock/reacquisition preserved and two separately paid original "
          "releases");
}
void write_reference_and_scope_quota() {
  Fixture fixture;
  RawEventRef resource;
  std::vector<std::byte> original(16 * 1024 * 1024 + 256);
  for (std::size_t i = 0; i < original.size(); ++i)
    original[i] = static_cast<std::byte>((i * 29) & 255);
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    std::shared_ptr<JournalFile> file = unwrap(directory.create_exclusive("reference"));
    resource = file->raw_storage_resource()->reference();
    struct stat s{};
    require(::stat((std::filesystem::path{fixture.path} / "reference").c_str(), &s) ==
                0,
            "actual reference input file");
    selected_device = s.st_dev;
    selected_inode = s.st_ino;
    int root = 0;
    auto loan = unwrap(RawStorageSettlementLoan::prepare(
        &root, owner,
        {RawStorageLoanKind::selected_exec, original.size(), 1, 1, 0, 0, 0, 0}));
    {
      RawStorageLoanScope scope{*loan, {&root, owner, file, {}}};
      RawStorageWriteContinuation continuation;
      unwrap(file->begin_write_batch(original.size(), continuation));
      observed_writes = 0;
      short_write = true;
      require(unwrap(file->write_at_continuing(0, original, continuation)) == 4,
              "actual first positive short");
      short_write = false;
      require(unwrap(file->write_at_continuing(4, ByteView{original}.subspan(4),
                                               continuation)) == 16 * 1024 * 1024 - 4,
              "second native call references exact remaining preserved slice");
      const auto exhausted = file->write_at_continuing(
          16 * 1024 * 1024, ByteView{original}.subspan(16 * 1024 * 1024), continuation);
      require(!exhausted.has_value() && exhausted.error().code == ErrorCode::capacity,
              "actual third native input remains unacquired after two paid calls");
      scope.error(exhausted.error());
    }
    {
      RawStorageLoanScope scope{*loan, {&root, owner, file, {}}};
    }
    {
      RawStorageLoanScope scope{*loan, {&root, owner, file, {}}};
    }
    bool refused = false;
    try {
      RawStorageLoanScope scope{*loan, {&root, owner, file, {}}};
    } catch (const Error &error) {
      refused = error.code == ErrorCode::wrong_incarnation ||
                error.code == ErrorCode::capacity;
    }
    require(refused, "kind-selected finite exec3 scopes forbid fourth");
    loan->finish();
    require(unwrap(file->extent()) == 16 * 1024 * 1024,
            "real kernel prefix matches finite calls, no invented remaining write");
  }
  const auto rows = fixture.finish();
  unsigned copied = 0, references = 0, scopes = 0;
  RawEventRef first;
  for (const auto &row : rows) {
    if (site(row) == 3009 && same(linked(row, 40), resource)) {
      require(row.raw.size() == 86 + 16 * 1024 * 1024,
              "one original native slice copied");
      require(std::equal(original.begin(), original.begin() + 16 * 1024 * 1024,
                         row.raw.begin() + 86),
              "exact original preserved native bytes");
      first = reference(row);
      ++copied;
    }
  }
  for (const auto &row : rows) {
    if (site(row) == 3032) {
      require(same(linked(row, 86), first) && field<std::size_t>(row.raw, 110) == 90 &&
                  field<std::size_t>(row.raw, 69) == 16 * 1024 * 1024 - 4 &&
                  field<off_t>(row.raw, 78) == 4,
              "reference names original raw envelope slice and actual new native "
              "count/offset");
      ++references;
    }
    if (site(row) == 3035)
      ++scopes;
  }
  require(copied == 1 && references == 1 && scopes == 3,
          "no second input copy, no fabricated fourth activation");
}
class ForwardFile final : public JournalFile {
public:
  explicit ForwardFile(std::shared_ptr<JournalFile> file) : file_(std::move(file)) {}
  Result<std::size_t> read_at(std::uint64_t offset, MutableByteView output) override {
    return file_->read_at(offset, output);
  }
  Result<std::size_t> write_at(std::uint64_t offset, ByteView input) override {
    return file_->write_at(offset, input);
  }
  Result<std::uint64_t> extent() override { return file_->extent(); }
  Result<void> synchronize(SyncStrength strength) override {
    return file_->synchronize(strength);
  }
  Result<void> lock_writer() override { return file_->lock_writer(); }
  void release_writer() noexcept override { file_->release_writer(); }
  RawStorageResource *raw_storage_resource() noexcept override {
    return file_->raw_storage_resource();
  }
  Result<void> begin_write_batch(std::size_t count,
                                 RawStorageWriteContinuation &continuation) override {
    return file_->begin_write_batch(count, continuation);
  }
  Result<std::size_t>
  write_at_continuing(std::uint64_t offset, ByteView input,
                      RawStorageWriteContinuation &continuation) override {
    return file_->write_at_continuing(offset, input, continuation);
  }

private:
  std::shared_ptr<JournalFile> file_;
};
class ForwardDirectory final : public JournalDirectory {
public:
  explicit ForwardDirectory(NativeJournalDirectory &directory)
      : directory_(directory) {}
  std::shared_ptr<JournalFile> actual;
  Result<std::unique_ptr<JournalFile>>
  create_exclusive(std::string_view name) override {
    auto result = directory_.create_exclusive(name);
    if (!result.has_value())
      return result;
    actual = std::move(result).value();
    return Result<std::unique_ptr<JournalFile>>::success(
        std::make_unique<ForwardFile>(actual));
  }
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view name,
                                                     FileAccess access) override {
    return directory_.open_existing(name, access);
  }
  Result<void> synchronize_directory(SyncStrength strength) override {
    return directory_.synchronize_directory(strength);
  }

private:
  NativeJournalDirectory &directory_;
};
void actual_journal_consumer(bool cut) {
  Fixture fixture;
  const std::size_t size = cut ? 256 : 32 * 1024 * 1024;
  std::vector<std::byte> original(size);
  for (std::size_t i = 0; i < size; ++i)
    original[i] = static_cast<std::byte>((i * 31 + i / 61) & 255);
  RawEventRef resource;
  JournalCursor accepted{identity<AuditStreamId>(12), 0, 112};
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    ForwardDirectory forwarding{directory};
    const JournalHeader header{identity<EnvironmentId>(120),
                               identity<AuditStreamId>(12),
                               7,
                               {32 * 1024 * 1024, 34 * 1024 * 1024},
                               {}};
    auto journal = unwrap(FramedJournal::create(
        forwarding, "journal", header, {64 * 1024 * 1024, 8}, SyncStrength::data));
    resource = forwarding.actual->raw_storage_resource()->reference();
    accepted = journal->cursor();
    const std::uint64_t prepared = size + 88;
    auto loan = unwrap(RawStorageSettlementLoan::prepare(
        journal.get(), owner, {RawStorageLoanKind::stop, prepared, 1, 1, 0, 0, 0, 0}));
    struct stat status{};
    require(
        ::stat((std::filesystem::path{fixture.path} / "journal").c_str(), &status) == 0,
        "actual journal native inode");
    selected_device = status.st_dev;
    selected_inode = status.st_ino;
    {
      RawStorageLoanScope scope{*loan, {journal.get(), owner, forwarding.actual, {}}};
      short_write = cut;
      observed_writes = 0;
      const std::array drafts{JournalDraft{FrameKind::semantic, original}};
      const auto appended = journal->append(drafts);
      short_write = false;
      if (cut) {
        const auto extent = forwarding.actual->extent();
        std::fprintf(stderr,
                     "journal cut: state%u sequence%llu end%llu pending%zu physical%zu "
                     "extent%llu append_error%s native_calls%u\n",
                     static_cast<unsigned>(journal->state()),
                     static_cast<unsigned long long>(journal->cursor().sequence),
                     static_cast<unsigned long long>(journal->cursor().end_offset),
                     journal->pending_records().size(),
                     journal->physical_records().size(),
                     static_cast<unsigned long long>(extent.has_value() ? extent.value()
                                                                        : UINT64_MAX),
                     appended.has_value() ? "none" : error_name(appended.error().code),
                     observed_writes);
        require(!appended.has_value() && appended.error().code == ErrorCode::capacity,
                "actual writer native quota refuses before unpaid retry");
        require(journal->state() == JournalWriterState::poisoned &&
                    journal->cursor().sequence == accepted.sequence &&
                    journal->cursor().end_offset == accepted.end_offset &&
                    journal->physical_records().empty() && extent.has_value() &&
                    extent.value() == accepted.end_offset + 4,
                "actual FramedJournal poisoned state, unchanged accepted cursor and "
                "native four-byte partial tail");
        scope.error(appended.error());
      } else {
        require(
            appended.has_value() && appended.value().sequence == 2,
            "actual32MiB journal payload accepted through bounded native transfers");
        accepted = appended.value();
      }
    }
    loan->finish();
    if (!cut) {
      require(journal->physical_records().size() == 1,
              "actual physical record metadata");
      auto cold = unwrap(journal->payload_reader(journal->physical_records().front()));
      journal.reset();
      require(unwrap(cold()) == original, "real cold payload reader after writer lease "
                                          "release preserves exact original32MiB");
      cold = {};
    }
    journal.reset();
    forwarding.actual.reset();
  }
  const auto rows = fixture.finish();
  unsigned inputs = 0;
  std::vector<std::byte> copied;
  for (const auto &row : rows)
    if (site(row) == 3009 && same(linked(row, 40), resource) &&
        field<off_t>(row.raw, 78) >= 112) {
      require(field<std::size_t>(row.raw, 69) <= 16 * 1024 * 1024,
              "actual native acquisition bound");
      copied.insert(copied.end(), row.raw.begin() + 86, row.raw.end());
      ++inputs;
    }
  if (cut)
    require(
        inputs == 1 && copied.size() == size + 88,
        "actual single original prepared batch preserved before short-write failure");
  else {
    require(
        inputs == 3 && copied.size() == size + 88,
        "actual larger encoded batch split into exactly three original native inputs");
    require(std::equal(original.begin(), original.end(), copied.begin() + 32),
            "single business encoding retained unchanged inside actual native inputs");
  }
}

char native_process_state(pid_t process) {
#if defined(__APPLE__)
  proc_bsdinfo observed{};
  const auto count =
      ::proc_pidinfo(process, PROC_PIDTBSDINFO, 0, &observed, sizeof(observed));
  if (count == 0)
    return 'Z';
  require(count == static_cast<int>(sizeof(observed)), "actual native process state");
  return observed.pbi_status == SSTOP ? 'T' : observed.pbi_status == SZOMB ? 'Z' : 'R';
#else
  const auto path = "/proc/" + std::to_string(process) + "/stat";
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    require(errno == ENOENT, "actual missing native process");
    return 'Z';
  }
  std::array<char, 1024> bytes{};
  const auto count = ::read(fd, bytes.data(), bytes.size());
  const int error = errno;
  require(::close(fd) == 0, "native state fd close");
  if (count < 0) {
    require(error == ESRCH, "actual dead native process read");
    return 'Z';
  }
  require(count > 0, "actual native process bytes");
  const std::string_view raw{bytes.data(), static_cast<std::size_t>(count)};
  const auto end = raw.rfind(')');
  require(end != std::string_view::npos && end + 2 < raw.size(),
          "actual native process state parse");
  return raw[end + 2];
#endif
}
void observe_process(pid_t process, char state) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (native_process_state(process) != state) {
    require(std::chrono::steady_clock::now() < deadline,
            "native process reaches actual requested cut state");
    std::this_thread::yield();
  }
}
struct CutProcesses {
  pid_t owner = -1;
  std::array<pid_t, 2> holders{-1, -1};
  ~CutProcesses() { cleanup(); }
  void cleanup() noexcept {
    for (auto &process : holders)
      if (process > 0) {
        (void)::kill(process, SIGKILL);
        process = -1;
      }
    if (owner > 0) {
      (void)::kill(owner, SIGKILL);
      int status{};
      (void)::waitpid(owner, &status, 0);
      owner = -1;
    }
  }
};
struct CutReport {
  std::array<pid_t, 2> holders{};
  IdentityBytes lane{};
};
void pipe_write(int descriptor, ByteView bytes) {
  while (!bytes.empty()) {
    const auto count = ::write(descriptor, bytes.data(), bytes.size());
    if (count < 0 && errno == EINTR)
      continue;
    require(count > 0, "native test pipe write");
    bytes = bytes.subspan(static_cast<std::size_t>(count));
  }
}
void pipe_read(int descriptor, MutableByteView bytes) {
  while (!bytes.empty()) {
    pollfd ready{descriptor, POLLIN, 0};
    require(::poll(&ready, 1, 5000) == 1, "native test cut ready");
    const auto count = ::read(descriptor, bytes.data(), bytes.size());
    if (count < 0 && errno == EINTR)
      continue;
    require(count > 0, "native test pipe read");
    bytes = bytes.subspan(static_cast<std::size_t>(count));
  }
}
std::string channel_name(IdentityBytes lane) {
  std::string name = "/bb";
  constexpr char digits[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 12; ++i) {
    const auto value = std::to_integer<unsigned>(lane[i]);
    name += digits[value >> 4];
    name += digits[value & 15U];
  }
  return name;
}
std::filesystem::path only_run(std::string_view path) {
  const auto runs = unwrap(discover_raw_runs(path));
  require(runs.size() == 1, "actual selected orphan run");
  return std::filesystem::path{path} / "raw" / runs.front().name;
}
void reclaim_after_death(std::string_view path, const std::filesystem::path &run,
                         CutProcesses &processes) {
  const auto holders = processes.holders;
  processes.cleanup();
  for (auto process : holders)
    observe_process(process, 'Z');
  const int fd = ::open((run / "lanes").c_str(), O_RDWR | O_CLOEXEC);
  require(fd >= 0, "actual preserved catalogue lease");
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    require(errno == EWOULDBLOCK && std::chrono::steady_clock::now() < deadline,
            "actual orphan exclusive catalogue fence");
    std::this_thread::yield();
  }
  require(::flock(fd, LOCK_UN) == 0 && ::close(fd) == 0,
          "actual catalogue fence released");
  auto runtime = unwrap(RawRuntime::open(path, identity<EnvironmentId>(120)));
  runtime.reset();
}
void acquired_prefix_death() {
  Fixture fixture;
  fixture.primary.reset();
  fixture.runtime.reset(); // Child owns this run, avoiding inherited runtime authority.
  // Remove the parent's empty closed run so the actual cut has exactly one run.
  std::filesystem::remove_all(std::filesystem::path{fixture.path} / "raw");
  std::array<int, 2> ready{}, go{}, pause{};
  require(::pipe(ready.data()) == 0 && ::pipe(go.data()) == 0 &&
              ::pipe(pause.data()) == 0,
          "native death cut pipes");
  const auto descriptors =
      std::array{ready[0], ready[1], go[0], go[1], pause[0], pause[1]};
  struct ClosePipes {
    const std::array<int, 6> &fds;
    ~ClosePipes() {
      for (auto fd : fds)
        (void)::close(fd);
    }
  } close{descriptors};
  auto *memory = ::mmap(nullptr, sizeof(AcquiredPrefix), PROT_READ | PROT_WRITE,
                        MAP_ANON | MAP_SHARED, -1, 0);
  require(memory != MAP_FAILED, "native shared acquisition snapshot");
  auto *prefix = new (memory) AcquiredPrefix{};
  struct Unmap {
    void *memory;
    ~Unmap() { (void)::munmap(memory, sizeof(AcquiredPrefix)); }
  } unmap{memory};
  CutProcesses processes;
  processes.owner = ::fork();
  require(processes.owner >= 0, "actual prefix owner fork");
  if (processes.owner == 0) {
    try {
      auto runtime =
          unwrap(RawRuntime::open(fixture.path, identity<EnvironmentId>(120)));
      auto primary = unwrap(runtime->prepare_thread());
      RawThreadBinding binding{primary};
      auto owner = unwrap(RawProductStorageOwner::prepare());
      auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
      const std::string name = "2692000." + std::string(32, 'e');
      std::array<std::byte, 65536> bytes{};
      for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::byte>((i * 73 + i / 251) & 255);
      unwrap(directory.store_diagnostic(name, bytes, 100000));
      struct stat selected{};
      require(
          ::stat((std::filesystem::path{fixture.path} / "diagnostics" / name).c_str(),
                 &selected) == 0,
          "actual cut diagnostic inode");
      selected_device = selected.st_dev;
      selected_inode = selected.st_ino;
      const auto status = unwrap(runtime->status());
      const CutReport report{{status.supervisor, status.recorder}, raw_lane_epoch()};
      pipe_write(ready[1], std::as_bytes(std::span{&report, 1}));
      char gate{};
      pipe_read(go[0], std::as_writable_bytes(std::span{&gate, 1}));
      calls = 0;
      armed = true;
      shared_prefix = prefix;
      cut_ready = ready[1];
      cut_pause = pause[0];
      (void)directory.read_diagnostic(name, 100000);
      ::_exit(68);
    } catch (...) {
      ::_exit(69);
    }
  }
  CutReport report;
  pipe_read(ready[0], std::as_writable_bytes(std::span{&report, 1}));
  processes.holders = report.holders;
  for (auto process : processes.holders) {
    require(::kill(process, SIGSTOP) == 0,
            "stop actual raw holder before native acquisition");
    observe_process(process, 'T');
  }
  auto channel =
      unwrap(RawChannel::open(channel_name(report.lane), report.lane,
                              RawCapacities{64 * 1024 * 1024, 256 * 1024, 128 * 1024}));
  const auto before = unwrap(channel.cursors(RawPartition::data));
  const char gate = 1;
  pipe_write(go[1], std::as_bytes(std::span{&gate, 1}));
  char acquired{};
  pipe_read(ready[0], std::as_writable_bytes(std::span{&acquired, 1}));
  require(acquired == 1 && prefix->returned == 4096,
          "actual native forwarded prefix at second read cut");
  const auto pending = unwrap(channel.cursors(RawPartition::data));
  require(pending.acknowledged == before.acknowledged &&
              pending.published > before.published &&
              pending.published > pending.acknowledged,
          "actual selected pending original P>A while owner alive");
  const auto copy = unwrap(channel.read(RawPartition::data, before.published, 65536));
  ByteView remaining = copy.bytes;
  std::vector<std::byte> original;
  unsigned matches = 0;
  while (!remaining.empty()) {
    const auto size = field<std::uint32_t>(remaining, 0);
    require(size >= 16 && size <= remaining.size(), "native pending raw envelope");
    const auto row = remaining.first(size);
    if (field<std::uint32_t>(row, 4) == 3008) {
      require(
          size == 49 + 4096 && field<ssize_t>(row, 40) == 4096 &&
              !field<bool>(row, 48) &&
              std::equal(prefix->bytes.begin(), prefix->bytes.end(), row.begin() + 49),
          "native pending exact acquired prefix copied before next read failure");
      original.assign(row.begin(), row.end());
      ++matches;
    }
    remaining = remaining.subspan(size);
  }
  require(matches == 1, "one native prefix actually pending before owner death");
  const auto run = only_run(fixture.path);
  reclaim_after_death(fixture.path, run, processes);
  auto reader = unwrap(RawRunReader::open(run.string()));
  require(reader->snapshot().closed, "actual recovered raw run closed");
  std::array<std::byte, 65536 + 1024> buffer{};
  unsigned recovered = 0;
  while (auto row = unwrap(reader->read(buffer)))
    if (row->origin.lane == report.lane && field<std::uint32_t>(row->raw, 4) == 3008) {
      require(std::equal(row->raw.begin(), row->raw.end(), original.begin(),
                         original.end()),
              "orphan recovery preserves exact pending original raw bytes");
      ++recovered;
    }
  require(recovered == 1, "exactly one acquired original survives owner/recorder death "
                          "without complete business result");
}
void wrong_thread_bank_death() {
  Fixture fixture;
  fixture.primary.reset();
  fixture.runtime.reset();
  std::filesystem::remove_all(std::filesystem::path{fixture.path} / "raw");
  std::array<int, 2> ready{};
  require(::pipe(ready.data()) == 0, "native custody fault pipe");
  struct Close {
    std::array<int, 2> fds;
    ~Close() {
      for (auto fd : fds)
        (void)::close(fd);
    }
  } close{ready};
  CutProcesses processes;
  processes.owner = ::fork();
  require(processes.owner >= 0, "actual custody fault fork");
  if (processes.owner == 0) {
    try {
      auto runtime =
          unwrap(RawRuntime::open(fixture.path, identity<EnvironmentId>(120)));
      auto primary = unwrap(runtime->prepare_thread());
      RawThreadBinding binding{primary};
      auto owner = unwrap(RawProductStorageOwner::prepare());
      auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
      auto file = unwrap(directory.create_exclusive("prior"));
      const std::array bytes{std::byte{0x52}, std::byte{0x00}, std::byte{0xa9}};
      unwrap(file->write_at(0, bytes));
      int root = 0;
      auto loan = unwrap(RawStorageSettlementLoan::prepare(
          &root, owner, {RawStorageLoanKind::stop, 256, 1, 1, 0, 0, 0, 0}));
      const auto status = unwrap(runtime->status());
      const CutReport report{{status.supervisor, status.recorder}, raw_lane_epoch()};
      pipe_write(ready[1], std::as_bytes(std::span{&report, 1}));
      std::thread illegal{[&] {
        RawThreadBinding wrong{primary};
        loan.reset();
      }};
      illegal.join();
      ::_exit(68);
    } catch (...) {
      ::_exit(69);
    }
  }
  CutReport report;
  pipe_read(ready[0], std::as_writable_bytes(std::span{&report, 1}));
  processes.holders = report.holders;
  int status{};
  require(::waitpid(processes.owner, &status, 0) == processes.owner,
          "actual custody fault child wait");
  processes.owner = -1;
  require(WIFEXITED(status) && WEXITSTATUS(status) == 74,
          "wrong OS thread cannot retire live bank, actual immediate exit74");
  const auto run = only_run(fixture.path);
  reclaim_after_death(fixture.path, run, processes);
  auto reader = unwrap(RawRunReader::open(run.string()));
  std::array<std::byte, 65536> buffer{};
  unsigned originals = 0, ends = 0;
  while (auto row = unwrap(reader->read(buffer))) {
    if (row->origin.lane != report.lane)
      continue;
    const auto site = field<std::uint32_t>(row->raw, 4);
    if (site == 3009) {
      const std::array bytes{std::byte{0x52}, std::byte{0x00}, std::byte{0xa9}};
      require(row->raw.size() == 89 &&
                  std::equal(bytes.begin(), bytes.end(), row->raw.begin() + 86),
              "prior acquired input preserved through internal custody death");
      ++originals;
    }
    if (site == 3038)
      ++ends;
  }
  require(originals == 1 && ends == 0,
          "custody violation preserves prior raw bytes without fabricated owner END");
}

void combined_transfer(bool cancelled_join) {
  Fixture fixture;
  RawEventRef resource;
  JournalCursor final{identity<AuditStreamId>(14), 0, 0};
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = std::make_unique<NativeJournalDirectory>(
        unwrap(NativeJournalDirectory::open(fixture.path, owner)));
    const JournalHeader header{identity<EnvironmentId>(120),
                               identity<AuditStreamId>(14),
                               7,
                               {32 * 1024 * 1024, 34 * 1024 * 1024},
                               {}};
    auto root = unwrap(RetainedState::create(std::move(directory), "journal", header,
                                             {64 * 1024 * 1024, 256}, owner));
    resource =
        root->raw_storage_selection().journal->raw_storage_resource()->reference();
    auto settlement = unwrap(root->protect_settlement({2048, 32}));
    // Actual candidate installation/rollback before any worker exists restores the
    // OLDmain bundle.
    {
      auto child = unwrap(raw_prepare_thread());
      auto dispatch = raw_worker_dispatch();
      auto failed = unwrap(root->prepare_raw_transfer(child, dispatch));
      failed->stage_before_launch();
      failed->rollback_launch();
      const auto retained = root->protected_settlement();
      require(!root->raw_owner_blocked() && owner.permits_current() &&
                  retained.has_value() && retained->max_file_bytes == 2048,
              "never-started actual child rollback preserves OLDmain owner/bundle");
    }
    auto child = unwrap(raw_prepare_thread());
    auto dispatch = raw_worker_dispatch();
    auto transfer = unwrap(root->prepare_raw_transfer(child, dispatch));
    transfer->stage_before_launch();
    std::mutex gate;
    std::condition_variable changed;
    bool entered = false, proceed = false;
    std::exception_ptr failure;
    std::jthread worker{[&] {
      try {
        RawThreadBinding active{child};
        raw_worker_started(dispatch);
        RawRegion command{raw_regions::command_perform};
        RawStorageAuthorityScope authority{transfer->handoff(),
                                           {raw_lane_epoch(), command.begin_ordinal()}};
        {
          const std::lock_guard lock{gate};
          entered = true;
        }
        changed.notify_one();
        {
          std::unique_lock lock{gate};
          changed.wait(lock, [&] { return proceed; });
        }
        AuditLog log{*root};
        log.record(ApplicationChannel::program,
                   Value::object({{"label", Value{"actual child root operation"}}}));
        command.outcome(RawRegionOutcome::returned);
        command.finish();
      } catch (...) {
        const std::lock_guard lock{gate};
        failure = std::current_exception();
        entered = true;
        changed.notify_one();
      }
    }};
    {
      std::unique_lock lock{gate};
      changed.wait(lock, [&] { return entered; });
    }
    if (failure)
      std::rethrow_exception(failure);
    require(!owner.permits_current() && owner.permits_thread() == false,
            "actual entered child authority excludes parent ordinary acquisition");
    transfer->commit_launch();
    {
      const std::lock_guard lock{gate};
      proceed = true;
    }
    changed.notify_one();
    worker.join();
    if (failure)
      std::rethrow_exception(failure);
    if (cancelled_join) {
      std::array<int, 2> cancel{};
      require(::pipe(cancel.data()) == 0, "actual joined cancellation pipe");
      const char byte = 1;
      require(::write(cancel[1], &byte, 1) == 1, "actual joined sticky cancellation");
      {
        RawWaitScope stop{{{cancel[0], -1}, {}}};
        const auto restored = transfer->joined();
        require(!restored.has_value() &&
                    restored.error().code == ErrorCode::interrupted &&
                    root->raw_owner_blocked(),
                "joined fresh owner refuses actual sticky cancellation, retains child "
                "custody");
      }
      require(::close(cancel[0]) == 0 && ::close(cancel[1]) == 0,
              "cancelled joined pipe close");
      transfer.reset();
      // An actual later uncancelled fresh admission replaces the preserved joined child
      // before a new effect.
      auto next_child = unwrap(raw_prepare_thread());
      auto next_dispatch = raw_worker_dispatch();
      auto resumed = unwrap(root->prepare_raw_transfer(next_child, next_dispatch));
      resumed->stage_before_launch();
      resumed->rollback_launch();
      resumed.reset();
      require(!root->raw_owner_blocked() && owner.permits_current(),
              "actual explicit fresh owner grant resumes after retained joined debt");
    } else {
      unwrap(transfer->joined());
      transfer.reset();
      require(!root->raw_owner_blocked() && owner.permits_current(),
              "actual successful join restores fresh MAIN whole bundle");
    }
    final = root->cursor();
    settlement.reset();
    root.reset();
  }
  const auto rows = fixture.finish();
  unsigned closures = 0;
  std::optional<std::uint64_t> end;
  std::optional<IdentityBytes> cleanup;
  for (const auto &row : rows) {
    if (site(row) == 3108) {
      require(row.raw.size() == 72 &&
                  field<std::uint64_t>(row.raw, 56) == final.sequence &&
                  field<std::uint64_t>(row.raw, 64) == final.end_offset,
              "actual native Root final cursor captured before member drops");
      require(std::equal(final.journal.bytes().begin(), final.journal.bytes().end(),
                         row.raw.begin() + 40),
              "actual native final journal identity");
      end = reference(row).ordinal;
      cleanup = row.origin.lane;
      ++closures;
    }
    if (site(row) == 3017 && same(linked(row, 40), resource)) {
      require(end && cleanup && *cleanup == row.origin.lane &&
                  *end < reference(row).ordinal,
              "Root final fields and resource close serialized on SAME cleanup owner "
              "in actual lifetime order");
    }
  }
  require(closures == 1,
          "actual unique Root emits exactly one completed owner teardown");
}

void exact_stat_fields() {
  Fixture fixture;
  RawEventRef resource;
  struct stat native{};
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    auto file = unwrap(directory.create_exclusive("stat"));
    resource = file->raw_storage_resource()->reference();
    const std::array bytes{std::byte{0x72}, std::byte{0x00}, std::byte{0x99}};
    unwrap(file->write_at(11, bytes));
    struct stat selected{};
    require(::stat((std::filesystem::path{fixture.path} / "stat").c_str(), &selected) ==
                0,
            "actual native stat selected inode");
    selected_device = selected.st_dev;
    selected_inode = selected.st_ino;
    capture_stat = true;
    observed_stats = 0;
    require(unwrap(file->extent()) == 14, "actual forwarded successful native stat");
    native = observed_stat;
    fail_stat = true;
    const auto failed = file->extent();
    fail_stat = false;
    capture_stat = false;
    require(observed_stats == 2 && !failed.has_value() &&
                failed.error() == Error{ErrorCode::io, EBADF},
            "selected real stat failure preserves actual native error");
  }
  const auto rows = fixture.finish();
  unsigned successes = 0, failures = 0;
  for (const auto &returned : rows)
    if (site(returned) == 3012) {
      const auto request_ref = linked(returned);
      auto request = std::find_if(rows.begin(), rows.end(), [&](const auto &row) {
        return same(reference(row), request_ref);
      });
      if (request == rows.end() || site(*request) != 3011 ||
          !same(linked(*request, 40), resource))
        continue;
      if (field<bool>(returned.raw, 44)) {
        require(returned.raw.size() == 49 && field<int>(returned.raw, 45) == EBADF,
                "failed native stat emits errno only, no unavailable fields");
        ++failures;
        continue;
      }
      // The open-validation stat predates our selected extent; only match the actual
      // reached size14.
      std::size_t offset = 45;
      const auto dev = field<dev_t>(returned.raw, offset);
      offset += sizeof(dev_t);
      const auto ino = field<ino_t>(returned.raw, offset);
      offset += sizeof(ino_t);
      const auto mode = field<mode_t>(returned.raw, offset);
      offset += sizeof(mode_t);
      const auto uid = field<uid_t>(returned.raw, offset);
      offset += sizeof(uid_t);
      const auto links = field<nlink_t>(returned.raw, offset);
      offset += sizeof(nlink_t);
      const auto size = field<off_t>(returned.raw, offset);
      offset += sizeof(off_t);
      if (size == 14) {
        require(returned.raw.size() == offset && dev == native.st_dev &&
                    ino == native.st_ino && mode == native.st_mode &&
                    uid == native.st_uid && links == native.st_nlink &&
                    size == native.st_size,
                "exact forwarded native stat fields individually, no struct padding");
        ++successes;
      }
    }
  require(successes == 1 && failures == 1,
          "one actual selected native stat success and original failure");
}

void saved_limit_and_index_page() {
  Fixture fixture;
  RawEventRef index_resource;
  std::array<std::byte, 16384> actual_page{};
  std::vector<std::byte> original(64 * 1024 * 1024);
  for (std::size_t i = 0; i < original.size(); ++i)
    original[i] = static_cast<std::byte>((i * 17 + i / 257) & 255);
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    const JournalHeader header{identity<EnvironmentId>(120),
                               identity<AuditStreamId>(13),
                               7,
                               {32 * 1024 * 1024, 34 * 1024 * 1024},
                               {}};
    auto journal = unwrap(FramedJournal::create(
        directory, "journal", header, {64 * 1024 * 1024, 8}, SyncStrength::data));
    // Actual saved-slot loader admits physical64MiB, then rejects these deliberately
    // invalid codec bytes.
    const auto path = std::filesystem::path{fixture.path} / "journal.state.0";
    {
      std::ofstream output{path, std::ios::binary};
      output.write(reinterpret_cast<const char *>(original.data()),
                   static_cast<std::streamsize>(original.size()));
      require(bool(output), "real64MiB saved slot file");
    }
    require(::chmod(path.c_str(), 0600) == 0, "private actual saved slot");
    const auto loaded =
        unwrap(load_saved_state(directory, "journal", *journal, {64 * 1024 * 1024, 8}));
    require(
        !loaded,
        "invalid64MiB codec remains invalid after actual complete native acquisition");
    auto index_file = unwrap(directory.create_exclusive("index"));
    index_resource = index_file->raw_storage_resource()->reference();
    auto index = unwrap(RecoveryIndex::open(std::move(index_file), 1024 * 1024));
    RecoveryKey key{};
    key[0] = std::byte{0x83};
    const auto root = unwrap(index->put({}, key, 313));
    require(unwrap(index->find(root, key)) == std::optional<std::uint64_t>{313},
            "actual16384-byte index page write/read unchanged");
    unwrap(index->synchronize());
    {
      std::ifstream input{std::filesystem::path{fixture.path} / "index",
                          std::ios::binary};
      input.read(reinterpret_cast<char *>(actual_page.data()),
                 static_cast<std::streamsize>(actual_page.size()));
      require(bool(input), "independent actual native index disk bytes");
    }
  }
  const auto rows = fixture.finish();
  unsigned saved_reads = 0, index_inputs = 0, index_reads = 0;
  std::size_t saved_offset = 0;
  for (const auto &row : rows) {
    if (site(row) == 3008 && field<ssize_t>(row.raw, 40) == 16 * 1024 * 1024) {
      require(saved_offset <= original.size() - 16 * 1024 * 1024 &&
                  std::equal(
                      original.begin() + static_cast<std::ptrdiff_t>(saved_offset),
                      original.begin() +
                          static_cast<std::ptrdiff_t>(saved_offset + 16 * 1024 * 1024),
                      row.raw.begin() + 49),
              "actual64MiB saved-loader prefix exact native bytes");
      saved_offset += 16 * 1024 * 1024;
      ++saved_reads;
    }
    if (site(row) == 3009 && same(linked(row, 40), index_resource)) {
      require(row.raw.size() == 86 + 16384,
              "actual index native input page exactly16384");
      require(std::equal(actual_page.begin(), actual_page.end(), row.raw.begin() + 86),
              "actual index native input preserved byte-for-byte against disk");
      ++index_inputs;
    }
    if (site(row) == 3008 && field<ssize_t>(row.raw, 40) == 16384) {
      require(row.raw.size() == 49 + 16384,
              "actual index native output page exactly16384");
      require(std::equal(actual_page.begin(), actual_page.end(), row.raw.begin() + 49),
              "actual index native read preserved byte-for-byte against disk");
      ++index_reads;
    }
  }
  require(saved_reads == 4 && saved_offset == original.size() && index_inputs == 1 &&
              index_reads == 1,
          "whole actual large saved loader and index page acquisitions");
}
void enumeration_and_retention() {
  Fixture fixture;
  std::string expired;
  const std::string scratch = "journal.state.0.tmp.1", live = "journal.state.0.tmp.2",
                    signature = "journal.state.0.tmp.3";
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    constexpr std::uint64_t now = 100000, ttl = 30ULL * 24 * 60 * 60;
    expired = std::to_string(now + ttl) + "." + std::string(32, 'a');
    const std::array original{std::byte{0x00}, std::byte{0xff}, std::byte{0x03}};
    unwrap(directory.store_diagnostic(expired, original, now));
    auto held = unwrap(directory.create_exclusive(live));
    unwrap(held->lock_writer());
    {
      auto file = unwrap(directory.create_exclusive(scratch));
      unwrap(file->write_at(0, original));
    }
    {
      auto file = unwrap(directory.create_exclusive(signature));
      const std::string bytes = "ARCOJ001";
      unwrap(file->write_at(0, std::as_bytes(std::span{bytes})));
    }
    const auto folder = std::filesystem::path{fixture.path};
    std::filesystem::create_symlink(scratch, folder / "journal.state.0.tmp.4");
    bool complete = false;
    unsigned removed = 0;
    for (unsigned i = 0; i < 8 && !complete; ++i) {
      auto result = unwrap(directory.reclaim_publication_scratch("journal", 128, 16));
      require(result.scanned <= 128 && result.removed <= 16,
              "actual bounded scratch enumeration");
      removed += static_cast<unsigned>(result.removed);
      complete = result.complete;
    }
    require(complete && removed == 1 && !std::filesystem::exists(folder / scratch) &&
                std::filesystem::exists(folder / live) &&
                std::filesystem::exists(folder / signature) &&
                std::filesystem::is_symlink(folder / "journal.state.0.tmp.4"),
            "actual scratch removes only unowned numeric non-journal");
    const auto result = unwrap(directory.expire_diagnostics(now + ttl));
    require(result.removed == 1 &&
                !std::filesystem::exists(folder / "diagnostics" / expired),
            "actual diagnostic retention deletes one expired original");
  }
  const auto rows = fixture.finish();
  bool scratch_name = false, expired_name = false;
  unsigned streams = 0, closed_streams = 0, unlinks = 0;
  for (const auto &row : rows) {
    if (site(row) == 3026 && row.raw.size() > 46) {
      const auto offset =
#if defined(__APPLE__)
          std::size_t{71};
#else
          std::size_t{69};
#endif
      const auto text =
          std::string_view{reinterpret_cast<const char *>(row.raw.data() + offset),
                           row.raw.size() - offset};
      if (text == std::string_view{scratch.c_str(), scratch.size() + 1})
        scratch_name = true;
      if (text == std::string_view{expired.c_str(), expired.size() + 1})
        expired_name = true;
    }
    if (site(row) == 3024 && field<void *>(row.raw, 40) != nullptr)
      ++streams;
    if (site(row) == 3017 && field<std::uint8_t>(row.raw, 65) == 2)
      ++closed_streams;
    if (site(row) == 3021 && field<std::uint8_t>(row.raw, 64) == 1)
      ++unlinks;
  }
  require(scratch_name && expired_name && streams == closed_streams && streams >= 2 &&
              unlinks == 2,
          "actual acquired names before classification, actual unlink and one closedir "
          "per successful stream");
}

void cleanup_grant_exhaustion() {
  RawRuntimeOptions options;
  options.producer_capacities[1] = 1024;
  Fixture fixture{options};
  RawEventRef rejected;
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = unwrap(NativeJournalDirectory::open(fixture.path, owner));
    std::vector<std::shared_ptr<JournalFile>> held;
    for (unsigned i = 0; i < 5; ++i)
      held.push_back(unwrap(directory.create_exclusive("held" + std::to_string(i))));
    // Cleanup lane:50 END +160 directory +5*160 file =1010 actual held bytes.
    // No native file open is permitted after its160-byte close grant refuses.
    const auto refused = directory.create_exclusive("unpaid");
    require(
        !refused.has_value() && refused.error().code == ErrorCode::capacity &&
            !std::filesystem::exists(std::filesystem::path{fixture.path} / "unpaid"),
        "actual exhausted cleanup bank refuses before native open");
    const std::array byte{std::byte{0x7a}};
    require(unwrap(held.front()->write_at(0, byte)) == 1,
            "existing prepaid resources remain usable after separate cleanup refusal");
    held.clear();
  }
  const auto rows = fixture.finish();
  unsigned opened = 0, closed = 0, refused = 0;
  for (const auto &row : rows) {
    if (site(row) == 3005)
      ++opened;
    if (site(row) == 3017)
      ++closed;
    if (site(row) == 3003 && row.raw.size() == 50 &&
        field<std::uint8_t>(row.raw, 41) ==
            static_cast<std::uint8_t>(ErrorCode::capacity)) {
      rejected = linked(row);
      ++refused;
    }
  }
  require(opened == 6 && closed == 6 && refused == 1 && rejected.ordinal != 0,
          "six actual native opens/paid closes, one actual pre-open refusal");
}

void maintenance_catalog_retarget() {
  Fixture fixture;
  RawEventRef replaced, current, refused;
  std::array<std::byte, 96> original{};
  std::uint64_t actual_entries = 0, refused_entries = 0;
  {
    RawThreadBinding binding{fixture.primary};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto directory = std::make_unique<NativeJournalDirectory>(
        unwrap(NativeJournalDirectory::open(fixture.path, owner)));
    const JournalHeader header{identity<EnvironmentId>(120),
                               identity<AuditStreamId>(19),
                               7,
                               {32 * 1024 * 1024, 34 * 1024 * 1024},
                               {}};
    auto root = unwrap(RetainedState::create(std::move(directory), "journal", header,
                                             {64 * 1024 * 1024, 1024}, owner));
    const auto context = Value::object({{"version", Value{Number{"1"}}},
                                        {"base", Value{std::string(32, '0')}},
                                        {"entries", Value{Value::Array{}}},
                                        {"live_originals", Value{Value::Array{}}}});
    auto marker = [&](const char *text) {
      AuditLog log{*root};
      log.record(ApplicationChannel::program, Value::object({{"label", Value{text}}}));
    };
    auto independent_count = [&] {
      auto plain = unwrap(NativeJournalDirectory::open(fixture.path));
      auto catalog = unwrap(ArchiveCatalog::open(
          plain, "journal.archive." + std::to_string(root->saved_state()->slot), header,
          root->cursor(), 256 * 6));
      return catalog->count();
    };
    marker("session.identity");
    marker("first");
    unwrap(root->save_current_state(context));
    marker("second");
    auto selected = root->raw_storage_selection();
    replaced = selected.archive->raw_storage_resource()->reference();
    auto loan = unwrap(RawStorageSettlementLoan::prepare(
        root.get(), owner,
        {RawStorageLoanKind::selected_exec, 1024, 1, 1, 1, 256 * 6, 0, 0}));
    {
      RawStorageLoanScope scope{*loan, selected};
      RawStorageLoanPause maintenance{selected};
      unwrap(root->save_current_state(context));
      auto replacement = root->raw_storage_selection();
      current = replacement.archive->raw_storage_resource()->reference();
      require(!same(replaced, current) && selected.archive != replacement.archive,
              "actual successful checkpoint selects new native catalog incarnation");
      actual_entries = independent_count();
      unwrap(maintenance.restore(replacement, actual_entries));
      require(unwrap(replacement.archive->read_at(160, original)) == original.size(),
              "restored native loan acquires real entry from current catalog");
      std::array<std::byte, 96> disk{};
      std::ifstream input{
          std::filesystem::path{fixture.path} /
              ("journal.archive." + std::to_string(root->saved_state()->slot)),
          std::ios::binary};
      input.seekg(160);
      input.read(reinterpret_cast<char *>(disk.data()),
                 static_cast<std::streamsize>(disk.size()));
      require(bool(input) && disk == original,
              "independent actual new catalog disk bytes match native acquisition");
      bool refused_callback = false;
      try {
        RawStorageLoanPause exhausted{replacement};
      } catch (const Error &error) {
        refused_callback = error.code == ErrorCode::capacity;
      }
      require(refused_callback,
              "actual one-outer-callback quota refuses before next effect");
    }
    loan->finish();
    selected = {};
    loan.reset();
    root.reset();
    directory = std::make_unique<NativeJournalDirectory>(
        unwrap(NativeJournalDirectory::open(fixture.path, owner)));
    root = unwrap(RetainedState::open(std::move(directory), "journal", header,
                                      {64 * 1024 * 1024, 1024}, false, true, UINT64_MAX,
                                      owner));
    NoUnresolvedAttempts verifier;
    unwrap(root->confirm_recovery());
    unwrap(root->reconcile(verifier));
    for (unsigned index = 0; index < 128; ++index)
      marker("actual-maintenance-prefix");
    const auto identity = unwrap(root->issue<ApplicationRecordId>());
    const RetainedEvent candidate{
        {},
        ApplicationRecordEvent{identity, ApplicationChannel::program,
                               unwrap(encode_packet(Value::object(
                                   {{"label", Value{"must-not-apply"}}})))}};
    unsigned callbacks = 0;
    root->set_maintenance_checkpoint([&](const RawEventRef &) {
      ++callbacks;
      return root->save_current_state(context);
    });
    auto narrow = unwrap(RawStorageSettlementLoan::prepare(
        root.get(), owner,
        {RawStorageLoanKind::selected_exec, 1024, 1, 1, 1, actual_entries, 0, 0}));
    {
      RawStorageLoanScope scope{*narrow, root->raw_storage_selection()};
      const auto before = root->cursor();
      const auto appended = root->append(before, {}, std::span{&candidate, 1});
      require(!appended.has_value() && appended.error().code == ErrorCode::capacity &&
                  callbacks == 1 && root->cursor().journal == before.journal &&
                  root->cursor().sequence == before.sequence &&
                  root->cursor().end_offset == before.end_offset,
              "actual append refuses after real maintenance retarget before candidate "
              "apply");
      refused_entries = independent_count();
      require(refused_entries > actual_entries,
              "actual callback grew catalog beyond the admitted native bound");
      refused =
          root->raw_storage_selection().archive->raw_storage_resource()->reference();
      scope.error(appended.error());
    }
    root->set_maintenance_checkpoint({});
    narrow->finish(Error{ErrorCode::capacity});
  }
  const auto rows = fixture.finish();
  unsigned success = 0, refusal = 0, acquired = 0, rejected_acquisition = 0;
  std::optional<RawEventRef> retarget, refused_retarget;
  for (const auto &row : rows) {
    if (site(row) == 3048) {
      require(row.raw.size() == 98 && field<bool>(row.raw, 40) &&
                  field<bool>(row.raw, 65),
              "literal present new native selection fields");
      if (same(linked(row, 66), current)) {
        require(field<std::uint64_t>(row.raw, 90) == actual_entries,
                "actual validated new catalog count copied into retarget request");
        retarget = reference(row);
      }
      if (same(linked(row, 66), refused)) {
        require(field<std::uint64_t>(row.raw, 90) == refused_entries,
                "actual oversized replacement count copied before refusal");
        refused_retarget = reference(row);
      }
    }
    if (site(row) == 3049 && retarget && same(linked(row), *retarget)) {
      require(row.raw.size() == 41 && field<std::uint8_t>(row.raw, 40) == 0,
              "actual selection restore returned success");
      ++success;
    }
    if (site(row) == 3049 && refused_retarget && same(linked(row), *refused_retarget)) {
      require(row.raw.size() == 50 && field<std::uint8_t>(row.raw, 40) == 1 &&
                  field<std::uint8_t>(row.raw, 41) ==
                      static_cast<std::uint8_t>(ErrorCode::capacity) &&
                  field<std::int64_t>(row.raw, 42) == 0,
              "exact native capacity refusal linked to oversized replacement");
      ++refusal;
    }
    if (site(row) == 3007 && same(linked(row, 40), refused) && refused_retarget &&
        refused_retarget->lane == row.origin.lane &&
        refused_retarget->ordinal < reference(row).ordinal)
      ++rejected_acquisition;
    if (site(row) == 3007 && same(linked(row, 40), current) &&
        field<off_t>(row.raw, 78) == 160) {
      const auto request = reference(row);
      for (const auto &returned : rows)
        if (site(returned) == 3008 && same(linked(returned), request)) {
          require(returned.raw.size() == 145 &&
                      std::equal(original.begin(), original.end(),
                                 returned.raw.begin() + 49),
                  "exact new resource original entry bytes after retarget publication");
          require(retarget && retarget->lane == row.origin.lane &&
                      retarget->ordinal < request.ordinal,
                  "new native access follows actual published selection retarget");
          ++acquired;
        }
    }
  }
  require(success == 1 && refusal == 1 && acquired == 1 && rejected_acquisition == 0,
          "one real changed catalog restore/acquisition and one real bound refusal");
}

enum class TerminalCut { normal, cancellation, launch_failure };
void terminal_product_owner(TerminalCut cut) {
  const int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  require(master >= 0 && ::grantpt(master) == 0 && ::unlockpt(master) == 0,
          "actual native test PTY");
  const char *name = ::ptsname(master);
  require(name != nullptr, "actual PTY slave pathname");
  const int slave = ::open(name, O_RDWR | O_NOCTTY | O_CLOEXEC);
  require(slave >= 0, "actual native PTY slave");
  winsize dimensions{};
  dimensions.ws_row = 24;
  dimensions.ws_col = 80;
  require(::ioctl(slave, TIOCSWINSZ, &dimensions) == 0, "actual test PTY dimensions");
  std::array<int, 2> ready{};
  require(::pipe(ready.data()) == 0, "actual Terminal owner signal pipe");
  struct Descriptors {
    std::array<int, 4> values;
    ~Descriptors() {
      for (auto fd : values)
        (void)::close(fd);
    }
  } descriptors{{master, slave, ready[0], ready[1]}};
  CutProcesses processes;
  processes.owner = ::fork();
  require(processes.owner >= 0, "actual Terminal fixture process");
  if (processes.owner == 0) {
    if (::dup2(slave, STDIN_FILENO) < 0 || ::dup2(slave, STDOUT_FILENO) < 0 ||
        ::dup2(slave, STDERR_FILENO) < 0)
      ::_exit(67);
    (void)::close(master);
    (void)::close(slave);
    (void)::close(ready[0]);
    int result = 0;
    try {
      Fixture fixture;
      std::uint64_t sequence = 0;
      IdentityBytes primary_lane{};
      {
        RawThreadBinding binding{fixture.primary};
        primary_lane = raw_lane_epoch();
        auto owner = unwrap(RawProductStorageOwner::prepare());
        auto directory = std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(fixture.path, owner)));
        const JournalHeader header{identity<EnvironmentId>(120),
                                   identity<AuditStreamId>(18),
                                   7,
                                   {32 * 1024 * 1024, 34 * 1024 * 1024},
                                   {}};
        auto root = unwrap(RetainedState::create(
            std::move(directory), "journal", header, {64 * 1024 * 1024, 256}, owner));
        auto settlement = unwrap(root->protect_settlement({2048, 32}));
        const auto status = unwrap(fixture.runtime->status());
        CutReport report{{status.supervisor, status.recorder}, primary_lane};
        pipe_write(ready[1], std::as_bytes(std::span{&report, 1}));
        TerminalUI terminal{"selected physical owner fixture"};
        std::atomic_bool cancelled{false}, performed{false};
        unsigned calls_to_perform = 0, idle_after_perform = 0;
        auto perform = [&](std::string_view prompt) {
          require(prompt == "original terminal prompt" && owner.permits_current(),
                  "whole actual Terminal perform runs under approved child authority");
          ++calls_to_perform;
          AuditLog log{*root};
          log.record(ApplicationChannel::program,
                     Value::object({{"original", Value{std::string{prompt}}}}));
          if (cut == TerminalCut::cancellation) {
            const char reached = 'R';
            pipe_write(ready[1], std::as_bytes(std::span{&reached, 1}));
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (!cancelled.load()) {
              require(std::chrono::steady_clock::now() < deadline,
                      "actual Terminal Ctrl-C reaches existing worker cancellation");
              std::this_thread::yield();
            }
          }
          performed.store(true);
        };
        auto idle = [&] {
          require(!root->raw_owner_blocked() && owner.permits_current(),
                  "MAIN idle executes only after actual joined owner restoration");
          if (performed.load()) {
            ++idle_after_perform;
            AuditLog log{*root};
            log.record(ApplicationChannel::program,
                       Value::object({{"original", Value{"MAIN idle after join"}}}));
          }
        };
        auto exit = [&] {
          if (cut == TerminalCut::launch_failure)
            return refused_worker_launches == 1;
          return idle_after_perform == 1;
        };
        refused_worker_launches = 0;
        refuse_worker_launch = cut == TerminalCut::launch_failure;
        terminal.run(perform, cancelled, exit, "original terminal prompt", {}, idle, {},
                     -1, {}, root.get());
        refuse_worker_launch = false;
        const auto retained = root->protected_settlement();
        if (cut == TerminalCut::launch_failure)
          require(
              calls_to_perform == 0 && refused_worker_launches == 1 &&
                  !root->raw_owner_blocked() && owner.permits_current() &&
                  retained.has_value() && retained->max_file_bytes == 2048,
              "actual failed jthread construction restores untouched OLDmain bundle");
        else if (cut == TerminalCut::cancellation)
          require(calls_to_perform == 1 && performed.load() && cancelled.load() &&
                      !root->raw_owner_blocked() && owner.permits_current() &&
                      idle_after_perform == 1,
                  "actual canceled Terminal join rearms before restored MAIN idle");
        else
          require(calls_to_perform == 1 && idle_after_perform == 1 &&
                      !root->raw_owner_blocked() && owner.permits_current(),
                  "actual Terminal normal join precedes MAIN idle native acquisition");
        sequence = root->cursor().sequence;
        settlement.reset();
        root.reset();
      }
      const auto rows = fixture.finish();
      unsigned entries = 0, released = 0, closures = 0;
      for (const auto &row : rows) {
        if (site(row) == 3042) {
          require(row.origin.lane != primary_lane, "actual worker owns handoff entry");
          const auto command = linked(row, 64);
          const auto found =
              std::find_if(rows.begin(), rows.end(), [&](const auto &other) {
                return same(reference(other), command) && site(other) == 1030;
              });
          require(found != rows.end(),
                  "actual handoff links native Terminal perform BEGIN");
          ++entries;
        }
        if (site(row) == 3128)
          ++released;
        if (site(row) == 3108) {
          require(field<std::uint64_t>(row.raw, 56) == sequence,
                  "actual Terminal Root END copies last native accepted sequence");
          ++closures;
        }
      }
      require(
          entries == (cut == TerminalCut::launch_failure ? 0U : 1U) && released == 1 &&
              closures == 1,
          "actual Terminal reached handoff and separately paid settlement/root close");
    } catch (const Error &error) {
      std::fprintf(stderr, "terminal storage:%s:%lld\n", error_name(error.code),
                   static_cast<long long>(error.detail));
      result = 1;
    } catch (const std::exception &error) {
      std::fprintf(stderr, "terminal storage:%s\n", error.what());
      result = 1;
    }
    std::fflush(stderr);
    ::_exit(result);
  }
  CutReport report{};
  pipe_read(ready[0], std::as_writable_bytes(std::span{&report, 1}));
  processes.holders = report.holders;
  bool signalled_cancel = false;
  std::string transcript;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  for (;;) {
    int status{};
    const auto finished = ::waitpid(processes.owner, &status, WNOHANG);
    require(finished >= 0, "actual Terminal child wait");
    if (finished == processes.owner) {
      processes.owner = -1;
      require(
          WIFEXITED(status) && WEXITSTATUS(status) == 0,
          ("actual selected Terminal native owner failure:\n" + transcript).c_str());
      processes.holders = {-1, -1};
      break;
    }
    require(std::chrono::steady_clock::now() < deadline,
            ("actual selected Terminal native owner deadline:\n" + transcript).c_str());
    std::array<pollfd, 2> wait{{{master, POLLIN, 0}, {ready[0], POLLIN, 0}}};
    require(::poll(wait.data(), wait.size(), 100) >= 0, "actual Terminal PTY drain");
    if (wait[0].revents & POLLIN) {
      std::array<char, 8192> output{};
      const auto count = ::read(master, output.data(), output.size());
      require(count >= 0 || errno == EIO, "actual Terminal PTY native output");
      if (count > 0)
        transcript.append(output.data(), static_cast<std::size_t>(count));
    }
    if ((wait[1].revents & POLLIN) && !signalled_cancel) {
      char reached{};
      require(::read(ready[0], &reached, 1) == 1 && reached == 'R' &&
                  cut == TerminalCut::cancellation,
              "actual Terminal worker reached cancellation cut");
      const char cancel = 3;
      require(::write(master, &cancel, 1) == 1, "actual Terminal Ctrl-C input");
      signalled_cancel = true;
    }
  }
  require(signalled_cancel == (cut == TerminalCut::cancellation),
          "actual requested Terminal cancellation cut reached");
}

} // namespace
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view{argv[1]} == "completion") {
      retained_cold_concurrent_cleanup();
      held_physical_stop();
      combined_transfer(false);
      combined_transfer(true);
      cleanup_grant_exhaustion();
      maintenance_catalog_retarget();
      terminal_product_owner(TerminalCut::normal);
      terminal_product_owner(TerminalCut::cancellation);
      terminal_product_owner(TerminalCut::launch_failure);
      return 0;
    }
    require(argc == 1, "unknown native fixture selection");
    diagnostic_prefix();
    diagnostic_failure_cleanup(DiagnosticFault::kernel_error);
    diagnostic_failure_cleanup(DiagnosticFault::zero);
    diagnostic_failure_cleanup(DiagnosticFault::unlink_error);
    directory_native_framing(DirectoryFault::missing_nul);
    directory_native_framing(DirectoryFault::missing_nul, true);
    directory_native_framing(DirectoryFault::long_name);
#if defined(__APPLE__)
    directory_native_framing(DirectoryFault::inconsistent_length);
#endif
    directory_conversion_failure();
    opt_out_and_native_originals();
    authority_and_cold_cleanup();
    retained_cold_concurrent_cleanup();
    held_physical_stop();
    finite_native_failures();
    single_unlock_failure();
    write_reference_and_scope_quota();
    actual_journal_consumer(true);
    actual_journal_consumer(false);
    exact_stat_fields();
    saved_limit_and_index_page();
    enumeration_and_retention();
    acquired_prefix_death();
    wrong_thread_bank_death();
    combined_transfer(false);
    combined_transfer(true);
    cleanup_grant_exhaustion();
    maintenance_catalog_retarget();
    terminal_product_owner(TerminalCut::normal);
    terminal_product_owner(TerminalCut::cancellation);
    terminal_product_owner(TerminalCut::launch_failure);
    return 0;
  } catch (const Error &error) {
    std::fprintf(stderr, "storage:%s:%lld\n", error_name(error.code),
                 static_cast<long long>(error.detail));
  } catch (const std::exception &error) {
    std::fprintf(stderr, "storage:%s\n", error.what());
  }
  return 1;
}
