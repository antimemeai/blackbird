#include "blackbird/raw_reader.hpp"
#include "blackbird/terminal.hpp"
#include "blackbird/terminal_custody.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <new>
#include <pthread.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error{message};
}
thread_local unsigned short_writes = 0;
thread_local bool short_output = false, fail_next = false, command_cut = false,
                  append_cut = false;
thread_local bool allocator_hit = false;
thread_local int draft_fd = -1;
thread_local std::string created_path;
struct Observation {
  std::size_t requested;
  ssize_t result;
  int error;
  std::string bytes;
};
thread_local std::vector<Observation> output_observations;
thread_local bool child_cut = false, child_cut_hit = false;
thread_local int cancel_write = -1;
thread_local pid_t actual_child = 0;
thread_local pid_t observed_child = 0;
struct WaitObservation {
  pid_t requested, returned;
  int options, error, status;
};
thread_local std::vector<WaitObservation> wait_observations;
thread_local std::array<std::optional<RawCapacityClaim>, 76> child_holds;
std::atomic_bool wait_entered{false}, wait_finished{false};
void child_grant_cut();
void empty_signal(int) {}
volatile sig_atomic_t restored_resize_calls = 0;
void previous_resize(int) { restored_resize_calls = 1; }
} // namespace
void *operator new(std::size_t size) {
  if (fail_next ||
      (command_cut && size >= 600 && draft_fd < 0 && !created_path.empty())) {
    fail_next = command_cut = false;
    allocator_hit = true;
    throw std::bad_alloc{};
  }
  if (void *memory = std::malloc(size ? size : 1))
    return memory;
  throw std::bad_alloc{};
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete[](void *memory) noexcept { ::operator delete(memory); }
void operator delete[](void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}
extern "C" ssize_t write(int fd, const void *bytes, std::size_t size) {
  using Native = ssize_t (*)(int, const void *, std::size_t);
  static auto function = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "write"));
  if (!function)
    std::abort();
  const auto result = function(
      fd, bytes,
      short_output && fd == STDOUT_FILENO ? std::min<std::size_t>(2, size) : size);
  const int error = errno;
  if (short_output && fd == STDOUT_FILENO) {
    ++short_writes;
    output_observations.push_back(
        {size, result, error, std::string{static_cast<const char *>(bytes), size}});
  }
  errno = error;
  return result;
}
namespace {
thread_local bool rollback_cut = false;
thread_local unsigned rollback_sets = 0;
} // namespace
extern "C" int tcsetattr(int fd, int action, const termios *mode) {
  using Native = int (*)(int, int, const termios *);
  static auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "tcsetattr"));
  if (!native)
    std::abort();
  const int result = native(fd, action, mode), error = errno;
  if (rollback_cut) {
    ++rollback_sets;
    if (result == 0 && !(mode->c_lflag & ICANON))
      ::close(STDOUT_FILENO);
  }
  errno = error;
  return result;
}
extern "C" int mkstemp(char *path) {
  using Native = int (*)(char *);
  static auto function = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "mkstemp"));
  if (!function)
    std::abort();
  const int result = function(path), error = errno;
  if (raw_enabled()) {
    draft_fd = result;
    created_path = path;
  }
  errno = error;
  return result;
}
extern "C" int close(int fd) {
  using Native = int (*)(int);
  static auto function = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "close"));
  if (!function)
    std::abort();
  const int result = function(fd), error = errno;
  if (fd == draft_fd)
    draft_fd = -1;
  errno = error;
  return result;
}
extern "C" ssize_t read(int fd, void *bytes, std::size_t size) {
  using Native = ssize_t (*)(int, void *, std::size_t);
  static auto function = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "read"));
  if (!function)
    std::abort();
  const auto result = function(fd, bytes, size);
  if (append_cut && raw_enabled() && size == 4096 && result > 0) {
    append_cut = false;
    fail_next = true;
  }
  return result;
}
extern "C" int posix_spawn(pid_t *pid, const char *path,
                           const posix_spawn_file_actions_t *actions,
                           const posix_spawnattr_t *attributes, char *const arguments[],
                           char *const environment[]) {
  using Native = int (*)(pid_t *, const char *, const posix_spawn_file_actions_t *,
                         const posix_spawnattr_t *, char *const[], char *const[]);
  static auto function = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "posix_spawn"));
  if (!function)
    std::abort();
  const int result = function(pid, path, actions, attributes, arguments, environment);
  if (raw_enabled() && result == 0)
    observed_child = *pid;
  if (child_cut && result == 0)
    actual_child = *pid;
  return result;
}
extern "C" pid_t waitpid(pid_t pid, int *status, int options) {
  using Native = pid_t (*)(pid_t, int *, int);
  static auto function = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "waitpid"));
  if (!function)
    std::abort();
  const bool selected = child_cut && pid == actual_child;
  if (selected && !child_cut_hit)
    wait_entered.store(true, std::memory_order_release);
  const auto result = function(pid, status, options);
  const int error = errno;
  if (raw_enabled() && pid == observed_child)
    wait_observations.push_back(
        {pid, result, options, error, result > 0 ? *status : 0});
  if (selected && !child_cut_hit) {
    wait_finished.store(true, std::memory_order_release);
    if (result < 0 && error == EINTR)
      child_grant_cut();
  } else if (selected && child_cut_hit && result > 0)
    for (auto &loan : child_holds)
      loan.reset();
  errno = error;
  return result;
}
namespace {
template <class T> T take(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "complete literal H native field");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
using Key = std::pair<IdentityBytes, std::uint64_t>;
Key ref(ByteView &bytes) {
  return {take<IdentityBytes>(bytes), take<std::uint64_t>(bytes)};
}
struct Row {
  std::uint32_t site;
  std::vector<std::byte> body;
};
using Rows = std::map<Key, Row>;
Rows preserved(const std::string &directory, bool allow_fence = false) {
  const auto runs = unwrap(discover_raw_runs(directory));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "actual H recorder run closes");
  auto reader = unwrap(RawRunReader::open(directory + "/raw/" + runs.front().name));
  std::vector<std::byte> buffer(4 * 1024 * 1024);
  Rows rows;
  while (auto acquired = unwrap(reader->read(buffer))) {
    auto bytes = acquired->raw;
    (void)take<std::uint32_t>(bytes);
    const auto site = take<std::uint32_t>(bytes);
    const auto ordinal = take<std::uint64_t>(bytes);
    require(site != 8000 && (site != 8001 || allow_fence),
            "finite H owners never corrupt native transport");
    require(rows.emplace(Key{acquired->origin.lane, ordinal},
                         Row{site, {bytes.begin(), bytes.end()}})
                .second,
            "actual native H owner origin unique");
  }
  return rows;
}
std::string original(ByteView &body) {
  const auto size = take<std::size_t>(body);
  require(size <= body.size(), "complete literal native H original");
  std::string bytes{reinterpret_cast<const char *>(body.data()), size};
  body = body.subspan(size);
  return bytes;
}
std::string directory() {
  char temporary[] = "/tmp/blackbird-H-native-XXXXXX";
  require(::mkdtemp(temporary), "actual private H directory");
  return temporary;
}
auto runtime(const std::string &path) {
  IdentityBytes identity{};
  identity[0] = std::byte{101};
  return unwrap(
      RawRuntime::open(path, unwrap(EnvironmentId::from_bytes(identity)),
                       {{64 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
}
RawAdapterContext parent() {
  const char marker = 'H';
  auto credit = unwrap(raw_claim_control(17));
  const auto request =
      unwrap(raw_publish(7999, std::array{detail::raw_native_field(marker)},
                         RawPartition::control, credit));
  return {nullptr, request, nullptr, nullptr};
}
bool modes_equal(const termios &a, const termios &b) {
  return a.c_iflag == b.c_iflag && a.c_oflag == b.c_oflag && a.c_cflag == b.c_cflag &&
         (a.c_lflag & static_cast<tcflag_t>(~PENDIN)) ==
             (b.c_lflag & static_cast<tcflag_t>(~PENDIN)) &&
         std::equal(std::begin(a.c_cc), std::end(a.c_cc), std::begin(b.c_cc)) &&
         a.c_ispeed == b.c_ispeed && a.c_ospeed == b.c_ospeed;
}
termios decode_mode(ByteView &body) {
  termios mode{};
  mode.c_iflag = take<tcflag_t>(body);
  mode.c_oflag = take<tcflag_t>(body);
  mode.c_cflag = take<tcflag_t>(body);
  mode.c_lflag = take<tcflag_t>(body);
  const bool line = take<bool>(body);
#if defined(__linux__)
  require(line, "Linux actual native line discipline");
  mode.c_line = take<cc_t>(body);
#else
  require(!line, "Darwin native line discipline absent");
#endif
  for (auto &byte : mode.c_cc)
    byte = take<cc_t>(body);
  mode.c_ispeed = take<speed_t>(body);
  mode.c_ospeed = take<speed_t>(body);
  return mode;
}
RawChannel channel() {
  constexpr char digits[] = "0123456789abcdef";
  const auto lane = raw_lane_epoch();
  std::string name = "/bb";
  for (std::size_t index = 0; index < 12; ++index) {
    const auto byte = std::to_integer<unsigned char>(lane[index]);
    name += digits[byte >> 4U];
    name += digits[byte & 15U];
  }
  return unwrap(
      RawChannel::open(name, lane, {64 * 1024 * 1024, 1024 * 1024, 128 * 1024}));
}
void acknowledged(RawChannel &actual, RawPartition partition) {
  while (true) {
    const auto cursor = unwrap(actual.cursors(partition));
    if (cursor.published == cursor.acknowledged)
      return;
    std::this_thread::yield();
  }
}
template <std::size_t N>
void fill(RawPartition partition,
          std::array<std::optional<RawCapacityClaim>, N> &holds) {
  std::size_t used = 0;
  for (const std::uint64_t amount :
       {4ULL * 1024 * 1024, 65536ULL, 4096ULL, 256ULL, 16ULL, 1ULL})
    while (true) {
      auto grant = partition == RawPartition::data ? raw_try_claim_data(amount)
                                                   : raw_try_claim_control(amount);
      if (!grant.has_value()) {
        require(grant.error() == Error{ErrorCode::capacity},
                "actual native H quota floor");
        break;
      }
      require(used < holds.size(), "source-derived finite H loan slots");
      holds[used++].emplace(std::move(grant).value());
    }
}
void child_grant_cut() {
  auto actual = channel();
  acknowledged(actual, RawPartition::control);
  fill(RawPartition::control, child_holds);
  const char byte = 'C';
  require(::write(cancel_write, &byte, 1) == 1,
          "actual cancellation input wakes blocked admission");
  child_cut_hit = true;
}
void native_mode_cleanup() {
  const auto path = directory();
  const int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  require(master >= 0 && ::grantpt(master) == 0 && ::unlockpt(master) == 0,
          "real native H PTY");
  const int slave = ::open(::ptsname(master), O_RDWR | O_NOCTTY | O_CLOEXEC);
  require(slave >= 0, "real native H slave");
  int output[2];
  require(::pipe(output) == 0, "real native mode output pipe");
  const int saved_in = ::dup(STDIN_FILENO), saved_out = ::dup(STDOUT_FILENO);
  require(saved_in >= 0 && saved_out >= 0 && ::dup2(slave, STDIN_FILENO) >= 0 &&
              ::dup2(output[1], STDOUT_FILENO) >= 0,
          "actual terminal descriptor handoff");
  termios before{}, active{}, after{};
  require(::tcgetattr(STDIN_FILENO, &before) == 0, "actual native initial termios");
  const int flags = ::fcntl(STDOUT_FILENO, F_GETFL);
  IdentityBytes original_lane{}, previous_lane{};
  {
    auto owner = runtime(path);
    const auto creator = unwrap(owner->prepare_thread()),
               other = unwrap(owner->prepare_thread());
    std::unique_ptr<TerminalMode> mode;
    std::array<std::optional<RawCapacityClaim>, 139> data;
    std::array<std::optional<RawCapacityClaim>, 76> control;
    {
      RawThreadBinding binding{creator};
      original_lane = raw_lane_epoch();
      const auto context = parent();
      mode = std::make_unique<TerminalMode>(&context);
      require(::tcgetattr(STDIN_FILENO, &active) == 0 &&
                  !(active.c_lflag & (ICANON | ECHO | ISIG)),
              "actual native raw flags active");
      bool refused = false;
      std::thread foreign{[&] {
        try {
          mode->resume();
        } catch (const Error &) {
          refused = true;
        }
      }};
      foreign.join();
      require(refused, "cross-thread native custody refused before effects");
      auto actual = channel();
      acknowledged(actual, RawPartition::data);
      acknowledged(actual, RawPartition::control);
      fill(RawPartition::data, data);
      fill(RawPartition::control, control);
    }
    {
      RawThreadBinding binding{other};
      previous_lane = raw_lane_epoch();
      output_observations.reserve(8);
      short_output = true;
      short_writes = 0;
      mode.reset();
      short_output = false;
      require(raw_lane_epoch() == previous_lane,
              "cold native cleanup restores previously bound issuer");
      require(short_writes == 8, "exact one bounded cleanup batch, no ninth write");
    }
    for (auto &loan : data)
      loan.reset();
    for (auto &loan : control)
      loan.reset();
    require(::tcgetattr(STDIN_FILENO, &after) == 0 && modes_equal(before, after),
            "full quota and partial control output still restores actual termios");
    const int restored_flags = ::fcntl(STDOUT_FILENO, F_GETFL);
#if defined(__APPLE__)
    // XNU's F_GETFL exposes FWASWRITTEN (0x10000), which F_SETFL cannot clear.
    require((restored_flags & ~0x10000) == (flags & ~0x10000),
            "actual configurable output flags restored after cleanup");
#else
    require(restored_flags == flags, "actual output flags restored after cleanup");
#endif
  }
  require(::dup2(saved_in, STDIN_FILENO) >= 0 && ::dup2(saved_out, STDOUT_FILENO) >= 0,
          "fixture restores actual descriptors");
  ::close(saved_in);
  ::close(saved_out);
  ::close(slave);
  ::close(master);
  ::close(output[1]);
  std::array<char, 256> physical{};
  const auto count = ::read(output[0], physical.data(), physical.size());
  require(count > 16, "native entry bytes plus actual partial exit prefix");
  ::close(output[0]);
  const auto rows = preserved(path);
  Key cleanup{}, exit_frame{};
  unsigned set_count = 0, returned_count = 0, writes = 0;
  bool retired = false;
  for (const auto &[key, row] : rows) {
    ByteView body = row.body;
    if (row.site == 4100) {
      (void)ref(body);
      if (take<std::uint8_t>(body) == 4) {
        require(key.first == original_lane,
                "cold Mode cleanup uses actual creating issuer");
        cleanup = key;
      }
    }
    if (row.site == 4104) {
      const auto request = ref(body);
      require(take<int>(body) == STDIN_FILENO && take<int>(body) == TCSANOW,
              "actual tcsetattr descriptor/action");
      const auto mode = decode_mode(body);
      require(body.empty(), "termios fields exclude native padding");
      require(modes_equal(mode, request == cleanup ? before : active),
              "actual recorded termios matches independent native values");
      ++set_count;
    }
    if (row.site == 4105) {
      (void)ref(body);
      require(take<int>(body) == 0 && !take<bool>(body) && body.empty(),
              "native termios actual return success");
      ++returned_count;
    }
    if (row.site == 4041) {
      const auto frame = ref(body);
      const auto text = original(body);
      if (text.find("2004l") != std::string::npos) {
        require(body.empty(), "native exit packet exact original");
        exit_frame = frame;
      }
    }
  }
  require(cleanup.second && exit_frame.second && set_count == 2 && returned_count == 2,
          "native acquisition and cleanup termios captured separately");
  std::size_t actual_offset = 0;
  for (const auto &[key, row] : rows) {
    (void)key;
    ByteView body = row.body;
    if (row.site == 4044 && ref(body) == exit_frame) {
      require(take<int>(body) == STDOUT_FILENO &&
                  take<std::size_t>(body) == actual_offset,
              "actual partial writer offset");
      require(take<std::size_t>(body) == output_observations[writes].requested &&
                  body.empty(),
              "actual partial write proposed extent");
      actual_offset += static_cast<std::size_t>(output_observations[writes++].result);
    }
    if (row.site == 4042 && ref(body) == exit_frame) {
      const auto branch = take<std::uint8_t>(body);
      const auto offset = take<std::size_t>(body);
      if (branch == 4) {
        require(offset == 16 && body.empty(),
                "actual remaining packet retired at native accepted extent");
        retired = true;
      }
    }
  }
  require(writes == 8 && retired && actual_offset == 16,
          "native eight-write remainder oracle");
  std::string accepted;
  for (const auto &item : output_observations)
    accepted += item.bytes.substr(0, static_cast<std::size_t>(item.result));
  require(std::memcmp(physical.data() + count - 16, accepted.data(), 16) == 0,
          "actual native pipe contains exactly accepted exit prefix");
  output_observations.clear();
  std::filesystem::remove_all(path);
}
void actual_child_refusal() {
  const auto path = directory(), fifo = path + "/child-block";
  require(::mkfifo(fifo.c_str(), 0600) == 0, "actual editor blocking input FIFO");
  const auto editor = "sh -c 'read native < " + fifo + "' --";
  ::setenv("TMPDIR", path.c_str(), 1);
  ::setenv("VISUAL", editor.c_str(), 1);
  int cancel[2];
  require(::pipe(cancel) == 0, "actual admission cancellation pipe");
  struct sigaction action{}, previous{};
  action.sa_handler = empty_signal;
  sigemptyset(&action.sa_mask);
  require(::sigaction(SIGUSR1, &action, &previous) == 0,
          "real non-restarting wait interruption handler");
  Error actual_error{};
  bool failed = false;
  {
    auto owner = runtime(path);
    RawThreadBinding binding{unwrap(owner->prepare_thread())};
    const auto context = parent();
    const auto main_thread = ::pthread_self();
    wait_entered.store(false);
    wait_finished.store(false);
    child_cut = true;
    child_cut_hit = false;
    cancel_write = cancel[1];
    actual_child = 0;
    wait_observations.clear();
    std::thread sender{[&] {
      while (!wait_entered.load(std::memory_order_acquire) &&
             !wait_finished.load(std::memory_order_acquire))
        std::this_thread::yield();
      while (!wait_finished.load(std::memory_order_acquire)) {
        (void)::pthread_kill(main_thread, SIGUSR1);
        std::this_thread::yield();
      }
    }};
    {
      RawWaitScope wait{{{cancel[0], -1}, {}}};
      try {
        (void)edit_terminal_draft("acquired child draft", &context);
      } catch (const Error &error) {
        actual_error = error;
        failed = true;
      }
    }
    wait_finished.store(true, std::memory_order_release);
    sender.join();
    child_cut = false;
    for (auto &loan : child_holds)
      loan.reset();
    require(failed && child_cut_hit && actual_child > 0,
            "actual ordinary wait EINTR followed by reached raw-admission failure");
    int status{};
    require(::waitpid(actual_child, &status, WNOHANG) == -1 && errno == ECHILD,
            "actual positive editor PID killed and reaped by paid cleanup");
    require(!std::filesystem::exists(created_path),
            "actual acquired child failure unlinks real draft");
  }
  require(::sigaction(SIGUSR1, &previous, nullptr) == 0,
          "restore native fixture handler");
  ::close(cancel[0]);
  ::close(cancel[1]);
  const auto rows = preserved(path);
  Key request{};
  unsigned ends = 0, kills = 0, waits = 0;
  bool reaped = false, masked = false, restored = false;
  for (const auto &[key, row] : rows) {
    ByteView body = row.body;
    if (row.site == 4120)
      request = key;
    if (row.site == 4121) {
      require(ref(body) == request && take<std::uint8_t>(body) == 1 &&
                  take<std::uint8_t>(body) == 0 &&
                  take<ErrorCode>(body) == actual_error.code &&
                  take<std::int64_t>(body) == actual_error.detail && body.empty(),
              "actual admission failure preserved as paid owned Error");
      ++ends;
    }
    if (row.site == 4123) {
      (void)ref(body);
      const auto method = take<std::uint8_t>(body);
      if (method == 12) {
        require(take<pid_t>(body) == actual_child && take<int>(body) == SIGKILL &&
                    body.empty(),
                "only actual acquired positive editor PID receives paid SIGKILL");
        ++kills;
      }
      waits += method == 6;
    }
    if (row.site == 4128) {
      (void)ref(body);
      require(take<pid_t>(body) == actual_child, "native child cleanup exact PID");
      const bool owned = take<bool>(body), actual_reaped = take<bool>(body);
      require(body.empty(), "native child state fields exact");
      reaped |= !owned && actual_reaped;
    }
    if (row.site == 2064 && ref(body) == request) {
      const auto how = take<int>(body);
      masked |= how == SIG_BLOCK;
      restored |= how == SIG_SETMASK;
      require(body.size() == RawCommandInterrupt::signal_mask_bytes,
              "qualified paid child cleanup native mask");
    }
  }
  require(ends == 1 && kills == 1 && waits == 2 && reaped && masked && restored,
          "actual post-spawn failure owns one interrupted wait, one paid kill/reap and "
          "restored native mask");
  std::filesystem::remove_all(path);
}
void actual_resize_custody() {
  const auto path = directory();
  struct sigaction prior{}, original_action{}, after{};
  prior.sa_handler = previous_resize;
  sigemptyset(&prior.sa_mask);
  prior.sa_flags = SA_RESTART;
  require(::sigaction(SIGWINCH, &prior, &original_action) == 0,
          "real previously owned SIGWINCH handler");
  sigset_t before_mask{}, after_mask{};
  require(::pthread_sigmask(SIG_SETMASK, nullptr, &before_mask) == 0,
          "actual native original signal mask");
  {
    auto owner = runtime(path);
    const auto creator = unwrap(owner->prepare_thread()),
               other = unwrap(owner->prepare_thread());
    std::unique_ptr<TerminalResize> resize;
    {
      RawThreadBinding binding{creator};
      const auto context = parent();
      resize = std::make_unique<TerminalResize>(&context);
      resize->install();
      restored_resize_calls = 0;
      require(::raise(SIGWINCH) == 0, "actual owned SIGWINCH delivered");
      require(restored_resize_calls == 0,
              "actual selected resize handler replaces previous owner");
      pollfd fd{resize->read_fd(), POLLIN, 0};
      require(::poll(&fd, 1, 0) == 1,
              "actual signal shutdown wake makes existing endpoint ready");
      {
        RawThreadBinding foreign{other};
        std::array<std::optional<RawCapacityClaim>, 76> holds;
        auto actual = channel();
        acknowledged(actual, RawPartition::control);
        fill(RawPartition::control, holds);
        const auto previous_lane = raw_lane_epoch();
        resize->observed();
        require(raw_lane_epoch() == previous_lane,
                "observed rebinds creator and restores saturated foreign issuer");
      }
      fd.revents = 0;
      require(::poll(&fd, 1, 0) == 0, "actual resize endpoint rearmed without timer");
      resize->uninstall();
      require(::sigaction(SIGWINCH, nullptr, &after) == 0 &&
                  after.sa_handler == previous_resize && (after.sa_flags & SA_RESTART),
              "actual acquired prior resize action restored");
      require(::raise(SIGWINCH) == 0 && restored_resize_calls == 1,
              "actual restored prior native handler receives next signal");
    }
    {
      RawThreadBinding binding{other};
      const auto previous_lane = raw_lane_epoch();
      resize.reset();
      require(raw_lane_epoch() == previous_lane,
              "retained resize cleanup restores other issuer binding");
    }
  }
  require(::pthread_sigmask(SIG_SETMASK, nullptr, &after_mask) == 0,
          "actual native restored mask acquired");
  for (int signal = 1; signal < NSIG; ++signal)
    require(sigismember(&before_mask, signal) == sigismember(&after_mask, signal),
            "actual resize paid destruction restores original mask members");
  require(::sigaction(SIGWINCH, &original_action, nullptr) == 0,
          "fixture restores original native resize action");
  const auto rows = preserved(path);
  unsigned actions = 0, notifies = 0, masks = 0;
  for (const auto &[key, row] : rows) {
    (void)key;
    ByteView body = row.body;
    if (row.site == 2062) {
      (void)ref(body);
      if (take<int>(body) == SIGWINCH)
        ++actions;
    }
    if (row.site == 2056) {
      (void)ref(body);
      (void)take<int>(body);
      require(take<int>(body) == SHUT_WR && body.empty(),
              "actual signal-safe native shutdown wake arguments");
      ++notifies;
    }
    if (row.site == 2064) {
      (void)ref(body);
      const auto how = take<int>(body);
      require((how == SIG_BLOCK || how == SIG_SETMASK) &&
                  body.size() == RawCommandInterrupt::signal_mask_bytes,
              "actual resize cleanup qualified native mask fields");
      ++masks;
    }
  }
  require(actions == 2 && notifies == 1 && masks == 2,
          "one actual signal notification, acquired install/restore and paid final "
          "native mask pair");
  std::filesystem::remove_all(path);
}
void editor_case(unsigned kind) {
  const auto path = directory();
  const auto temp = path + "/apostrophe'actual";
  std::filesystem::create_directory(temp);
  ::setenv("TMPDIR", temp.c_str(), 1);
  const std::string literal = "native-editor-" + std::to_string(kind);
  std::string editor = "sh -c 'printf " + literal + " > \"$1\"' --";
  if (kind == 1)
    editor = "sh -c 'exit 7' --";
  if (kind == 2)
    editor = "sh -c 'kill -TERM $$' --";
  if (kind == 3)
    editor = "sh -c 'dd if=/dev/zero of=\"$1\" bs=1048577 count=1 2>/dev/null' --";
  if (kind == 4)
    editor += std::string(600, ' ');
  if (kind == 5)
    editor = "sh -c 'printf append-cut-native-bytes > \"$1\"' --";
  if (kind == 6)
    ::setenv("TMPDIR", "", 1);
  if (kind == 9)
    editor = "sh -c 'rm \"$1\"; mkfifo \"$1\"' --";
  if (kind == 11)
    editor = "no-such-blackbird-native-editor";
  ::setenv("VISUAL", editor.c_str(), 1);
  ::setenv("EDITOR", "unreached-editor-fallback", 1);
  const char *previous_path = std::getenv("PATH");
  const std::string previous_path_value = previous_path ? previous_path : "";
  if (kind == 7) {
    ::setenv("VISUAL", "", 1);
    ::setenv("EDITOR", editor.c_str(), 1);
  }
  if (kind == 8) {
    const auto bin = path + "/bin";
    std::filesystem::create_directory(bin);
    const auto vi = bin + "/vi";
    const int executable = ::open(vi.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0700);
    require(executable >= 0, "actual private default editor");
    const std::string script = "#!/bin/sh\nprintf " + literal + " > \"$1\"\n";
    require(::write(executable, script.data(), script.size()) ==
                    static_cast<ssize_t>(script.size()) &&
                ::close(executable) == 0,
            "actual private default editor bytes");
    ::unsetenv("VISUAL");
    ::unsetenv("EDITOR");
    ::setenv("PATH", bin.c_str(), 1);
    editor = "vi";
  }
  if (kind == 10)
    for (const char *name : {"TMPDIR", "TMP", "TEMP", "TEMPDIR"})
      ::unsetenv(name);
  const std::string draft = kind == 12 ? std::string(1024 * 1024 + 1, 'D')
                                       : std::string{"native\0draft\n", 13};
  EditorResult returned_value;
  {
    auto owner = runtime(path);
    RawThreadBinding binding{unwrap(owner->prepare_thread())};
    const auto context = parent();
    created_path.clear();
    allocator_hit = false;
    wait_observations.clear();
    observed_child = 0;
    command_cut = kind == 4;
    append_cut = kind == 5;
    returned_value = edit_terminal_draft(draft, &context);
    command_cut = append_cut = fail_next = false;
    require(kind == 0 || kind == 7 || kind == 8 || kind == 10
                ? returned_value.accepted && returned_value.text == literal
                : !returned_value.accepted,
            "actual native editor success/error behavior");
    if (kind == 4 || kind == 5)
      require(allocator_hit, "actual forwarding allocation cut reached real owner");
    require(created_path.empty() || !std::filesystem::exists(created_path),
            "actual editor cleanup unlinks owned draft");
    require(draft_fd == -1, "actual editor cleanup closes writer descriptor");
  }
  if (previous_path)
    ::setenv("PATH", previous_path_value.c_str(), 1);
  else
    ::unsetenv("PATH");
  const auto rows = preserved(path);
  Key request{}, command{}, returned_source{};
  unsigned ends = 0, launches = 0, waits = 0, closes = 0, unlinks = 0;
  std::string chunks;
  bool editor_original = false;
  std::map<Key, std::uint8_t> native_methods;
  std::size_t wait_outcomes = 0;
  for (const auto &[key, row] : rows) {
    ByteView body = row.body;
    if (row.site == 4120) {
      (void)ref(body);
      require(original(body) == draft && body.empty(),
              "actual binary editor input original");
      request = key;
    }
    if (row.site == 4122) {
      const auto owner = ref(body);
      const auto role = take<std::uint8_t>(body);
      const bool present = take<bool>(body);
      const auto text = present ? original(body) : std::string{};
      require(body.empty(), "actual editor source optional fields");
      if (role == 4) {
        require(text == editor + std::string(1, '\0'),
                "actual VISUAL native source has precedence");
        editor_original = true;
      }
      if (present && (role <= 5 || role == 8 || role == 9))
        require(!text.empty() && text.back() == '\0',
                "actual native CString includes stored NUL");
      if (role == 5) {
        require(text.find("exec " + editor + " ") == 0 &&
                    (kind == 10 || text.find("'\\''") != std::string::npos),
                "actual business shell command and apostrophe path original");
        command = key;
      }
      if (role == 6) {
        require(text == returned_value.text,
                "actual whole returned native editor source");
        returned_source = key;
      }
      if (role == 7) {
        require(rows.at(owner).site == 4123,
                "actual acquired chunk links native read request");
        chunks += text;
      }
    }
    if (row.site == 4123) {
      (void)ref(body);
      const auto method = take<std::uint8_t>(body);
      native_methods.emplace(key, method);
      if (method == 5) {
        require(ref(body) == command,
                "actual spawn references acquired business shell command");
        ++launches;
      }
      if (method == 6) {
        require(waits < wait_observations.size(),
                "native wait request observation exists");
        require(take<pid_t>(body) == wait_observations[waits].requested &&
                    take<int>(body) == wait_observations[waits].options && body.empty(),
                "actual native wait request PID/options independent forwarding oracle");
      }
      waits += method == 6;
      closes += method == 4;
      unlinks += method == 10;
    }
    if (row.site == 4124) {
      const auto native_request = ref(body);
      const auto method = native_methods.at(native_request);
      if (method == 5) {
        require(take<int>(body) == 0 && !take<bool>(body) &&
                    take<pid_t>(body) == observed_child && body.empty(),
                "actual native launch return and independent child PID");
      }
      if (method == 6) {
        require(wait_outcomes < wait_observations.size(),
                "native wait return observation exists");
        const auto &actual = wait_observations[wait_outcomes++];
        require(
            take<pid_t>(body) == actual.returned &&
                take<bool>(body) == (actual.returned < 0),
            "actual native wait result/error predicate independent forwarding oracle");
        if (actual.returned < 0)
          require(take<int>(body) == actual.error, "actual native failed wait errno");
        else if (actual.returned > 0)
          require(take<int>(body) == actual.status,
                  "actual native child raw wait status");
        require(body.empty(), "native wait actual optional fields exact");
      }
    }
    if (row.site == 4121) {
      require(ref(body) == request, "actual whole editor outcome owner");
      const auto branch = take<std::uint8_t>(body);
      (void)take<std::uint8_t>(body);
      require(branch == (kind == 4 || kind == 5 ? 2
                         : kind == 6            ? 7
                                                : 0),
              "actual editor returned/allocation/system branch");
      if (branch == 7)
        (void)take<int>(body);
      require(body.empty(), "editor outcome exact native fields");
      ++ends;
    }
  }
  require(ends == 1, "one actual whole editor outcome after native lexical cleanup");
  require(waits == wait_observations.size() &&
              wait_outcomes == wait_observations.size(),
          "every actual acquired editor wait request and return is preserved");
  if (kind == 1)
    require(wait_observations.size() == 1 && WIFEXITED(wait_observations[0].status) &&
                WEXITSTATUS(wait_observations[0].status) == 7,
            "real editor nonzero exit observed natively");
  if (kind == 2)
    require(wait_observations.size() == 1 && WIFSIGNALED(wait_observations[0].status) &&
                WTERMSIG(wait_observations[0].status) == SIGTERM,
            "real editor signal termination observed natively");
  if (kind == 0 || kind == 7 || kind == 8 || kind == 10)
    require(editor_original && launches == 1 && waits == 1 && closes == 2 &&
                unlinks == 1 && chunks == literal && returned_source.second,
            "actual successful child/read/source/cleanup graph");
  if (kind == 3)
    require(chunks.size() == 1024 * 1024 + 1 && !returned_source.second,
            "oversized acquired editor originals preserved before business refusal");
  if (kind == 4)
    require(editor_original && !command.second && launches == 0 && closes == 1 &&
                unlinks == 1,
            "source before command allocation and actual paid cleanup");
  if (kind == 5)
    require(chunks == "append-cut-native-bytes" && !returned_source.second &&
                closes == 2 && unlinks == 1,
            "actual acquired read bytes survive append allocation failure and lexical "
            "cleanup");
  if (kind == 6 || kind == 12)
    require(launches == 0 && closes == 0 && unlinks == 0,
            "empty present temp environment fails before file/child ownership");
  std::filesystem::remove_all(path);
}
} // namespace
namespace {
void constructor_rollback() {
  const auto path = directory();
  const int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
  require(master >= 0 && ::grantpt(master) == 0 && ::unlockpt(master) == 0,
          "rollback real PTY");
  const int slave = ::open(::ptsname(master), O_RDWR | O_NOCTTY | O_CLOEXEC);
  int output[2];
  require(slave >= 0 && ::pipe(output) == 0, "rollback native descriptors");
  const int saved_in = ::dup(STDIN_FILENO), saved_out = ::dup(STDOUT_FILENO);
  require(::dup2(slave, STDIN_FILENO) >= 0 && ::dup2(output[1], STDOUT_FILENO) >= 0,
          "rollback descriptor handoff");
  termios before{}, after{};
  require(::tcgetattr(STDIN_FILENO, &before) == 0, "rollback actual before mode");
  {
    auto owner = runtime(path);
    RawThreadBinding binding{unwrap(owner->prepare_thread())};
    const auto context = parent();
    rollback_sets = 0;
    rollback_cut = true;
    bool failed = false;
    try {
      TerminalMode mode{&context};
    } catch (const std::exception &) {
      failed = true;
    } catch (const Error &) {
      failed = true;
    }
    rollback_cut = false;
    require(failed && rollback_sets == 2,
            "actual closed-output constructor failure has one rollback set");
    require(::tcgetattr(STDIN_FILENO, &after) == 0 && modes_equal(before, after),
            "failed constructor restores native mode");
  }
  require(::dup2(saved_in, STDIN_FILENO) >= 0 && ::dup2(saved_out, STDOUT_FILENO) >= 0,
          "restore rollback fixture descriptors");
  ::close(saved_in);
  ::close(saved_out);
  ::close(slave);
  ::close(master);
  ::close(output[0]);
  ::close(output[1]);
  unsigned rolled_back = 0;
  for (const auto &[key, row] : preserved(path)) {
    (void)key;
    if (row.site == 4106) {
      ByteView body = row.body;
      (void)ref(body);
      const auto method = take<std::uint8_t>(body);
      const bool active = take<bool>(body);
      if (method == 1) {
        require(!active && body.empty(), "rollback actual state is inactive");
        ++rolled_back;
      }
    }
  }
  require(rolled_back == 1, "one actual constructor rollback state recorded");
  std::filesystem::remove_all(path);
}
void absent_resize_custody() {
  const auto path = directory();
  {
    auto owner = runtime(path);
    RawThreadBinding binding{unwrap(owner->prepare_thread())};
    (void)parent();
    const auto lane = raw_lane_epoch();
    {
      TerminalResize resize{nullptr};
      resize.install();
      require(::raise(SIGWINCH) == 0, "actual opt-out signal delivered");
      pollfd fd{resize.read_fd(), POLLIN, 0};
      require(::poll(&fd, 1, 0) == 1, "actual opt-out signal wake readable");
      resize.observed();
      require(::poll(&fd, 1, 0) == 0, "actual opt-out native rearm");
      resize.uninstall();
    }
    require(raw_lane_epoch() == lane, "opt-out restores ambient issuer");
  }
  for (const auto &[key, row] : preserved(path)) {
    (void)key;
    require(row.site == 7999 || row.site < 1000,
            "absent H context emits no wake, action, mask or owner events");
  }
  std::filesystem::remove_all(path);
}
void malformed_context() {
  for (unsigned kind = 0; kind < 3; ++kind) {
    const auto path = directory();
    {
      auto owner = runtime(path);
      RawThreadBinding binding{unwrap(owner->prepare_thread())};
      auto context = parent();
      if (!context.parent)
        throw std::runtime_error("fixture parent absent");
      if (kind == 0)
        context.parent->ordinal = 0;
      else
        context.parent->lane = {};
      bool refused = false;
      errno = EDOM;
      try {
        if (kind == 0) {
          TerminalMode mode{&context};
        } else if (kind == 1)
          (void)edit_terminal_draft("untouched", &context);
        else
          (void)terminal_native_poll(&context, {}, 0);
      } catch (const Error &error) {
        refused = error.code == ErrorCode::invalid_identity;
      }
      require(refused && errno == EDOM,
              "explicit malformed H context refuses before effect and preserves errno");
    }
    unsigned fences = 0;
    for (const auto &[key, row] : preserved(path, true)) {
      (void)key;
      require(row.site < 4100 || row.site > 4179,
              "malformed context has no H request or effect");
      if (row.site == 8001)
        ++fences;
    }
    require(fences == 1, "malformed supplied parent fences actual producer once");
    std::filesystem::remove_all(path);
  }
}
} // namespace
int main() {
  try {
    native_mode_cleanup();
    for (unsigned kind = 0; kind < 13; ++kind)
      editor_case(kind);
    actual_child_refusal();
    actual_resize_custody();
    constructor_rollback();
    absent_resize_custody();
    malformed_context();
    std::cout << "actual H mode/editor/native cleanup source-to-disk oracles passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << '(' << error.detail << ")\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
