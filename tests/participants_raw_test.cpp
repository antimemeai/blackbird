#include "blackbird/context.hpp"
#include "blackbird/journal_storage.hpp"
#include "blackbird/participants.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/tools.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <poll.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
thread_local RetainedState *allocation_root = nullptr;
thread_local std::uint64_t allocation_sequence = 0;
thread_local bool reject_after_ack = false;
thread_local unsigned rejected = 0;
thread_local dev_t failed_device = 0;
thread_local ino_t failed_inode = 0;
thread_local bool fail_write = false;
std::atomic<int> selected_wait_fd{-1};
std::atomic<bool> native_wait_entered{false};
std::mutex native_wait_mutex;
std::condition_variable native_wait_changed;
struct NativeShutdown {
  int fd, how, result, error;
};
std::array<NativeShutdown, 64> observed_shutdowns{};
std::atomic_size_t observed_shutdown_count{0};
std::atomic<bool> fail_shutdown{false};
struct FailedWakeCase {
  pid_t recorder = -1, supervisor = -1, alias = -1;
  NativeShutdown shutdown{};
  std::atomic<unsigned> phase{0};
};
FailedWakeCase *failed_wake_case = nullptr;
} // namespace
void *operator new(std::size_t bytes) {
  if (reject_after_ack && allocation_root &&
      allocation_root->cursor().sequence > allocation_sequence) {
    ++rejected;
    throw std::bad_alloc{};
  }
  if (void *pointer = std::malloc(bytes == 0 ? 1 : bytes))
    return pointer;
  throw std::bad_alloc{};
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
void *operator new(std::size_t bytes, std::align_val_t alignment) {
  if (reject_after_ack && allocation_root &&
      allocation_root->cursor().sequence > allocation_sequence) {
    ++rejected;
    throw std::bad_alloc{};
  }
  void *pointer = nullptr;
  if (::posix_memalign(&pointer, static_cast<std::size_t>(alignment),
                       bytes == 0 ? 1 : bytes) == 0)
    return pointer;
  throw std::bad_alloc{};
}
void operator delete(void *pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t, std::align_val_t) noexcept {
  std::free(pointer);
}
void *operator new[](std::size_t bytes, std::align_val_t alignment) {
  return ::operator new(bytes, alignment);
}
void operator delete[](void *pointer, std::align_val_t alignment) noexcept {
  ::operator delete(pointer, alignment);
}
void operator delete[](void *pointer, std::size_t,
                       std::align_val_t alignment) noexcept {
  ::operator delete(pointer, alignment);
}
extern "C" ssize_t pwrite(int fd, const void *bytes, std::size_t size, off_t offset) {
  using Native = ssize_t (*)(int, const void *, std::size_t, off_t);
  static const auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "pwrite"));
  if (!native)
    std::abort();
  if (fail_write) {
    struct stat state{};
    if (::fstat(fd, &state) == 0 && state.st_dev == failed_device &&
        state.st_ino == failed_inode) {
      fail_write = false;
      errno = EIO;
      return -1;
    }
  }
  return native(fd, bytes, size, offset);
}
extern "C" int poll(pollfd *fds, nfds_t count, int timeout) {
  using Native = int (*)(pollfd *, nfds_t, int);
  static const auto forward = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "poll"));
  if (!forward)
    std::abort();
  const int saved = errno;
  const int selected = selected_wait_fd.load();
  if (selected >= 0 && timeout != 0 &&
      raw_wait_control().cancellation_fds[0] == selected) {
    bool matching = false;
    for (nfds_t i = 0; i < count; ++i)
      matching |= fds[i].fd == selected;
    if (matching) {
      {
        std::lock_guard lock{native_wait_mutex};
        native_wait_entered.store(true);
      }
      native_wait_changed.notify_one();
    }
  }
  errno = saved;
  return forward(fds, count, timeout);
}
extern "C" int shutdown(int fd, int how) {
  using Native = int (*)(int, int);
  static const auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "shutdown"));
  if (!native)
    std::abort();
  const bool selected_failure = fail_shutdown.exchange(false);
  if (selected_failure)
    (void)::close(fd); // The actual forwarded shutdown must return native EBADF.
  const int result = native(fd, how), error = result < 0 ? errno : 0, saved = errno;
  const auto index = observed_shutdown_count.fetch_add(1);
  if (index < observed_shutdowns.size())
    observed_shutdowns[index] = {fd, how, result, error};
  if (selected_failure && failed_wake_case) {
    failed_wake_case->shutdown = {fd, how, result, error};
    failed_wake_case->phase.store(2, std::memory_order_release);
  }
  errno = saved;
  return result;
}
namespace {
void check(bool good, const char *message) {
  if (!good)
    throw std::runtime_error{message};
}
template <class T> T id(unsigned char n) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{n};
  return unwrap(T::from_bytes(bytes));
}
struct Directory {
  std::array<char, 64> path{};
  bool preserve = false;
  Directory() {
    const std::string pattern = "/tmp/blackbird-participants-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), path.begin());
    check(::mkdtemp(path.data()) != nullptr, "mkdtemp");
  }
  ~Directory() {
    if (!preserve)
      std::filesystem::remove_all(path.data());
  }
};
struct Child {
  pid_t pid = -1;
  ~Child() {
    if (pid > 0) {
      (void)::kill(pid, SIGKILL);
      int status = 0;
      while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }
};
struct Resume {
  std::array<pid_t, 2> pids{-1, -1};
  ~Resume() {
    for (const auto pid : pids)
      if (pid > 0)
        (void)::kill(pid, SIGCONT);
  }
};
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
void confirm_stop(pid_t pid) {
  for (unsigned attempt = 0; attempt < 100000; ++attempt) {
    if (stopped(pid))
      return;
    std::this_thread::yield();
  }
  throw std::runtime_error{"owned holder did not actually stop"};
}
template <class T> void append(std::vector<std::byte> &bytes, const T &value) {
  const auto raw = std::as_bytes(std::span{&value, std::size_t{1}});
  bytes.insert(bytes.end(), raw.begin(), raw.end());
}
template <class T> T native(ByteView bytes, std::size_t offset = 0) {
  check(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset,
        "short native field");
  T result{};
  std::memcpy(&result, bytes.data() + offset, sizeof(T));
  return result;
}
struct Record {
  RawOrigin origin;
  std::uint32_t site;
  std::uint64_t ordinal;
  std::vector<std::byte> body;
};
std::vector<Record> records(const char *directory) {
  std::vector<Record> result;
  const auto discovered = unwrap(discover_raw_runs(directory));
  check(discovered.size() == 1 && !discovered[0].error, "raw discovery");
  auto reader = unwrap(RawRunReader::open(
      (std::filesystem::path{directory} / "raw" / discovered[0].name).string()));
  std::vector<std::byte> buffer(17 * 1024 * 1024);
  while (const auto row = unwrap(reader->read(buffer))) {
    result.push_back({row->origin,
                      native<std::uint32_t>(row->raw, 4),
                      native<std::uint64_t>(row->raw, 8),
                      {row->raw.begin() + 16, row->raw.end()}});
  }
  return result;
}
RawEventRef reference(const Record &record) {
  return {record.origin.lane, record.ordinal};
}
std::vector<std::byte> refbytes(RawEventRef ref) {
  std::vector<std::byte> bytes{ref.lane.begin(), ref.lane.end()};
  append(bytes, ref.ordinal);
  return bytes;
}
const Record &linked(const std::vector<Record> &rows, std::uint32_t site,
                     RawEventRef ref) {
  const auto prefix = refbytes(ref);
  const Record *found = nullptr;
  for (const auto &row : rows)
    if (row.site == site && row.body.size() >= prefix.size() &&
        std::equal(prefix.begin(), prefix.end(), row.body.begin())) {
      check(!found, "duplicate linked outcome");
      found = &row;
    }
  check(found != nullptr, "missing linked outcome");
  return *found;
}
Value request() {
  return Value::object(
      {{"request_id", Value{"initial"}},
       {"from", Value{"main"}},
       {"to", Value{"alpha"}},
       {"provider", Value{"openai"}},
       {"model", Value{"fake"}},
       {"task", Value{"source review"}},
       {"context", Value{Value::Array{Value::object(
                       {{"id", Value{"s1"}}, {"text", Value{"selected"}}})}}},
       {"profile", Value::object({{"name", Value{"context-only"}},
                                  {"provenance", Value{"test"}},
                                  {"timeout_seconds", Value{Number{3}}},
                                  {"tools", Value{"none"}},
                                  {"requests", Value{Number{1}}}})}});
}
Value args() { return Value::object({{"run_id", Value{"run"}}}); }
Value direction(unsigned n) {
  return Value::object({{"run_id", Value{"run"}},
                        {"message_id", Value{"d" + std::to_string(n)}},
                        {"from", Value{"operator"}},
                        {"text", Value{"direction"}}});
}
Value upstream() {
  return Value::object(
      {{"status", Value{"completed"}},
       {"model", Value{"actual"}},
       {"output", Value{Value::Array{Value::object(
                      {{"type", Value{"message"}},
                       {"role", Value{"assistant"}},
                       {"content", Value{Value::Array{Value::object(
                                       {{"type", Value{"output_text"}},
                                        {"text", Value{"answer"}}})}}}})}}}});
}
std::unique_ptr<RetainedState> root_at(const char *path) {
  return unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                          unwrap(NativeJournalDirectory::open(path))),
                                      "audit",
                                      {id<EnvironmentId>(1),
                                       id<AuditStreamId>(2),
                                       3,
                                       {16 * 1024 * 1024, 32 * 1024 * 1024},
                                       {}},
                                      {128 * 1024 * 1024, 4096}));
}
void native_original() {
  Directory directory;
  auto root = root_at(directory.path.data());
  AuditLog log{*root};
  const auto identity = log.issue();
  const auto warm = log.issue();
  (void)warm;
  const Value metadata = Value::object({{"fixture", Value{true}}});
  const std::string body{"\0\xff\x81\x47", 4};
  const auto before = root->cursor();
  allocation_root = root.get();
  allocation_sequence = before.sequence;
  rejected = 0;
  reject_after_ack = true;
  std::optional<SourceReference> retained;
  try {
    retained = log.original(identity, {"participant.raw", body, metadata});
  } catch (...) {
    reject_after_ack = false;
    throw;
  }
  reject_after_ack = false;
  check(rejected == 0 && retained && retained->journal == before.journal &&
            retained->sequence == before.sequence + 1,
        "native original late allocation/reference");
  check(root->cursor().sequence == before.sequence + 3,
        "native original one source/one record");
  if (!retained)
    throw std::runtime_error{"native original returned no source"};
  const auto source = unwrap(root->source(*retained));
  check(source.size() == body.size() &&
            std::memcmp(source.data(), body.data(), body.size()) == 0,
        "native original exact bytes");
  const auto facts = unwrap(root->fact_page(0, root->fact_count()));
  const auto &packet = std::get<ApplicationRecordEvent>(facts.back().event.body);
  check(packet.identity == identity &&
            string_field(unwrap(read_application_packet(packet)), "label") ==
                "participant.raw",
        "actual native original application identity/body");
  allocation_sequence = root->cursor().sequence;
  reject_after_ack = true;
  std::string formatted;
  try {
    formatted = log.original({"participant.old", body, metadata});
  } catch (...) {
    reject_after_ack = false;
    throw;
  }
  reject_after_ack = false;
  check(rejected == 0 && formatted.size() == 32,
        "old API formats before acknowledged append");
  const auto diagnostic =
      log.original(log.issue(), {"provider.stream", body, metadata});
  check(!diagnostic, "diagnostic has no physical SourceReference");
  const auto diagnostic_fact = unwrap(root->fact(root->fact_count() - 1));
  const auto decoded = unwrap(read_application_packet(
      std::get<ApplicationRecordEvent>(diagnostic_fact.event.body)));
  check(field(field(decoded, "metadata"), "available") == Value{true} &&
            field(field(decoded, "metadata"), "bytes") == Value{Number{4}},
        "actual diagnostic file outcome");
  const auto file = std::filesystem::path{directory.path.data()} / "audit";
  struct stat stat{};
  check(::stat(file.c_str(), &stat) == 0, "actual journal inode");
  failed_device = stat.st_dev;
  failed_inode = stat.st_ino;
  const auto failed_identity = log.issue();
  const auto failed_cursor = root->cursor();
  fail_write = true;
  bool refused = false;
  try {
    (void)log.original(failed_identity, {"participant.failed", body, metadata});
  } catch (const Error &error) {
    refused = error.code == ErrorCode::io && error.detail == EIO;
  }
  fail_write = false;
  allocation_root = nullptr;
  check(refused && root->cursor().sequence == failed_cursor.sequence &&
            root->cursor().end_offset == failed_cursor.end_offset,
        "actual EIO no acknowledged source/reference/retry");
}
void source_extent(bool unknown, bool retain_failure) {
  Directory directory, auth_directory;
  std::size_t callback_used = 0, retained_large = 0, retention_calls = 0;
  unsigned attempts = 0;
  bool retention_failed = false;
  std::mutex acquired_mutex;
  std::condition_variable acquired_changed;
  bool acquired = false;
  constexpr std::size_t size = 9 * 1024 * 1024;
  constexpr std::array pattern{'\0', static_cast<char>(0xff), static_cast<char>(0x81),
                               'G'};
  {
    auto runtime =
        unwrap(RawRuntime::open(directory.path.data(), id<EnvironmentId>(7)));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    ProviderAuthConfig config;
    config.directory = auth_directory.path.data();
    config.http = [&](const ProviderHttpRequest &, ProviderHttpResponseLease &lease) {
      ++attempts;
      const auto extent = lease.prepare(size);
      for (std::size_t i = 0; i < extent.size(); ++i)
        extent[i] = pattern[i % pattern.size()];
      callback_used = size;
      if (!unknown)
        lease.commit(size);
      if (unknown)
        throw Error{ErrorCode::io, EIO};
      return 200;
    };
    ProviderAuth auth{config};
    auth.set_key("openai", "fixture", "private-fixture");
    auth.use("openai", "fixture");
    Participants pool{
        Value{},
        [&](const OriginalCapture &capture) -> std::optional<SourceReference> {
          const auto label = capture.label, bytes = capture.bytes;
          if (label != "participant.admission") {
            ++retention_calls;
            if (bytes.size() == size) {
              for (std::size_t i = 0; i < bytes.size(); ++i)
                check(bytes[i] == pattern[i % pattern.size()],
                      "semantic original exact native bytes");
              if (retain_failure && !retention_failed) {
                retention_failed = true;
                throw Error{ErrorCode::io, EIO};
              }
              retained_large += bytes.size();
            }
          }
          return {};
        },
        [](const Value &) { return std::optional<ApplicationRecordId>{}; },
        [&](const Value &activity) {
          const auto &phase = string_field(activity, "phase");
          if (phase != "running" && phase != "waiting") {
            {
              std::lock_guard lock{acquired_mutex};
              acquired = true;
            }
            acquired_changed.notify_one();
          }
        }};
    pool.start("run", Value::object({{"request", request()}}),
               [&](const Value &, const ColleagueCapture &,
                   const std::function<bool()> &,
                   const RawAdapterContext *context) -> Value {
                 (void)auth.request("openai", "responses", Value::object({}),
                                    ProviderTimeouts{.request_seconds = 3}, {}, nullptr,
                                    context->owner, context);
                 throw Error{ErrorCode::corrupt};
               });
    {
      std::unique_lock lock{acquired_mutex};
      acquired_changed.wait(lock, [&] { return acquired; });
    }
    pool.shutdown(); // joins before source counter/native buffer inspection
    check(callback_used == size && attempts == 1,
          "actual bounded HTTP acquisition ran exactly once");
    if (unknown) {
      bool refused = false;
      try {
        pool.drain();
      } catch (const Error &error) {
        refused = error.code == ErrorCode::incomplete;
      }
      check(refused && pool.debt() && retained_large == 0,
            "unknown-used unresolved debt never Retain/parse");
    } else {
      if (retain_failure) {
        bool refused = false;
        try {
          pool.drain();
        } catch (const Error &error) {
          refused = error.code == ErrorCode::io && error.detail == EIO;
        }
        check(refused && pool.debt() && retained_large == 0,
              "failed Retain leaves exact acquired owner node");
      }
      pool.drain();
      check(retained_large == size && !pool.debt(),
            "crossing 8MiB extent retained once without redispatch");
    }
  }
  const auto rows = records(directory.path.data());
  const Record *source = nullptr;
  for (const auto &row : rows)
    if (row.site == 1312) {
      check(!source, "duplicate acquired HTTP source");
      source = &row;
    }
  check(source != nullptr, "native callback source absent");
  const auto bytes = ByteView{source->body};
  check(native<std::uint8_t>(bytes, 24) == (unknown ? 2 : 1) &&
            native<std::size_t>(bytes, 25) == size &&
            native<bool>(bytes, 33) == !unknown,
        "exact callback extent native qualification");
  const std::size_t offset = unknown ? 34 : 42;
  check(bytes.size() == offset + size, "callback preserves one exact native range");
  if (!unknown)
    check(native<std::size_t>(bytes, 34) == size, "actual committed used native width");
  for (std::size_t i = 0; i < size; ++i)
    check(std::to_integer<unsigned char>(bytes[offset + i]) ==
              static_cast<unsigned char>(pattern[i % pattern.size()]),
          "disk-preserved native callback bytes");
  const Record *handoff = nullptr;
  for (const auto &row : rows)
    if (row.site == 2108 && row.body.size() >= 25 &&
        native<std::uint8_t>(row.body, 24) == 1 && row.body.size() >= 71 &&
        std::equal(source->origin.lane.begin(), source->origin.lane.end(),
                   row.body.begin() + 25) &&
        native<std::uint64_t>(row.body, 41) == source->ordinal) {
      check(!handoff, "duplicate physical source handoff");
      handoff = &row;
    }
  check(handoff &&
            native<RawPartition>(handoff->body, 49) == source->origin.partition &&
            native<std::size_t>(handoff->body, 53) == offset + 16 &&
            native<std::size_t>(handoff->body, 61) == size,
        "handoff links actual physical partition/range");
  check(native<ProviderOriginalExtentKind>(handoff->body, 69) ==
            (unknown ? ProviderOriginalExtentKind::unknown_used
                     : ProviderOriginalExtentKind::exact_used),
        "handoff exact/unknown extent distinction");
  const auto &outcome = linked(rows, 2109, reference(*handoff));
  check(native<std::uint8_t>(outcome.body, 40) == 2,
        "actual crossing extent enters emergency before refusal");
  if (unknown) {
    unsigned debt = 0;
    for (const auto &row : rows)
      if (row.site == 2135 && row.body.size() >= 48 &&
          std::equal(handoff->origin.lane.begin(), handoff->origin.lane.end(),
                     row.body.begin() + 24) &&
          native<std::uint64_t>(row.body, 40) == handoff->ordinal)
        ++debt;
    check(debt == 1, "actual unresolved debt surface");
  } else {
    unsigned retained = 0;
    for (const auto &row : rows)
      if (row.site == 2111 && row.body.size() >= 24 &&
          std::equal(handoff->origin.lane.begin(), handoff->origin.lane.end(),
                     row.body.begin()) &&
          native<std::uint64_t>(row.body, 16) == handoff->ordinal) {
        const auto &out = linked(rows, 2112, reference(row));
        if (native<std::uint8_t>(out.body, 24) == 1)
          ++retained;
      }
    check(retained == 1, "one actual successful Retain after zero/one failure");
  }
}
void directions_and_cleanup() {
  Directory directory, journal_directory;
  const auto owner_thread = std::this_thread::get_id();
  std::vector<std::string> dispatched;
  std::optional<Value> initial_admission;
  std::mutex gate;
  std::condition_variable changed;
  bool waiting = false, release = false;
  observed_shutdown_count.store(0);
  {
    auto root = root_at(journal_directory.path.data());
    AuditLog log{*root};
    auto runtime =
        unwrap(RawRuntime::open(directory.path.data(), id<EnvironmentId>(8)));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    const auto owner_lane = raw_lane_epoch();
    Participants pool{Value{},
                      [&](const OriginalCapture &capture) {
                        const auto label = capture.label, bytes = capture.bytes;
                        const auto &metadata = capture.metadata;
                        if (label == "participant.admission" && !initial_admission)
                          initial_admission.emplace(unwrap(decode_packet(
                              std::as_bytes(std::span{bytes.data(), bytes.size()}))));
                        return log.original(log.issue(), {label, bytes, metadata});
                      },
                      [&](const Value &snapshot) {
                        const auto identity = log.issue();
                        log.record(identity, ApplicationChannel::program, snapshot);
                        return std::optional{identity};
                      },
                      [&](const Value &activity) {
                        if (std::this_thread::get_id() != owner_thread &&
                            string_field(activity, "phase") == "waiting") {
                          std::unique_lock lock{gate};
                          waiting = true;
                          changed.notify_one();
                          changed.wait(lock, [&] { return release; });
                        }
                      }};
    RawParticipantIdentities originals{};
    originals.attempt[0] = std::byte{21};
    originals.invocation[0] = std::byte{22};
    originals.generation[0] = std::byte{23};
    pool.start("run", Value::object({{"request", request()}}),
               [&](const Value &prepared, const ColleagueCapture &,
                   const std::function<bool()> &, const RawAdapterContext *context) {
                 check(context && context->owner &&
                           context->owner->attempt == originals.attempt &&
                           context->owner->invocation == originals.invocation &&
                           context->owner->generation == originals.generation,
                       "actual immutable dispatch owner identities");
                 check(initial_admission && prepared == *initial_admission,
                       "worker uses exact retained prepared request");
                 dispatched.push_back(
                     string_field(field(prepared, "request"), "request_id"));
                 return upstream();
               },
               {{}, originals});
    {
      std::unique_lock lock{gate};
      changed.wait(lock, [&] { return waiting; });
    }
    struct Release {
      std::mutex &gate;
      std::condition_variable &changed;
      bool &release;
      ~Release() {
        {
          std::lock_guard lock{gate};
          release = true;
        }
        changed.notify_all();
      }
    } release_on_failure{gate, changed, release};
    pool.drain();
    allocation_root = root.get();
    allocation_sequence = root->cursor().sequence;
    rejected = 0;
    reject_after_ack = true;
    Value accepted;
    try {
      accepted = pool.send(direction(0), {{}, originals});
    } catch (...) {
      reject_after_ack = false;
      throw;
    }
    reject_after_ack = false;
    check(rejected == 0 && field(accepted, "accepted") == Value{true},
          "accepted direction has zero postack allocations");
    check(field(pool.send(direction(0), {{}, originals}), "duplicate") == Value{true},
          "same actual receipt duplicates without new dispatch");
    for (unsigned i = 1; i < 16; ++i)
      (void)pool.send(direction(i), {{}, originals});
    (void)pool.cancel(args());
    {
      std::lock_guard lock{gate};
      release = true;
    }
    changed.notify_all();
    pool.shutdown();
    pool.drain();
    const auto row = pool.read(args());
    check(field(row, "queued_directions") == Value{Number{0}} &&
              field(row, "dropped_directions") == Value{Number{16}},
          "stop after waiting withdraws all sixteen directions");
    check(dispatched == std::vector<std::string>{"initial"},
          "withdrawn queued directions never dispatch");
    check(raw_lane_epoch() == owner_lane, "cleanup restores owner binding");
    allocation_root = nullptr;
  }
  const auto rows = records(directory.path.data());
  std::vector<const Record *> offers;
  const Record *run = nullptr, *entry = nullptr, *worker_end = nullptr,
               *owner_end = nullptr;
  for (const auto &row : rows) {
    if (row.site == 2102) {
      check(!run, "duplicate live Run");
      run = &row;
    }
    if (row.site == 2103) {
      check(!entry, "duplicate worker entry");
      entry = &row;
    }
    if (row.site == 2126) {
      check(!worker_end, "duplicate worker END");
      worker_end = &row;
    }
    if (row.site == 2136) {
      check(!owner_end, "duplicate owner END");
      owner_end = &row;
    }
    if (row.site == 2115)
      offers.push_back(&row);
  }
  check(run && entry && worker_end && owner_end && offers.size() == 17,
        "actual Run/worker/cleanup owners and every offer");
  const auto run_ref = refbytes(reference(*run));
  check(std::equal(run_ref.begin(), run_ref.end(), entry->body.begin()) &&
            std::equal(run_ref.begin(), run_ref.end(), worker_end->body.begin()),
        "actual worker owner linkage");
  check(owner_end->origin.lane == run->origin.lane &&
            native<bool>(owner_end->body, 48) &&
            std::equal(worker_end->origin.lane.begin(), worker_end->origin.lane.end(),
                       owner_end->body.begin() + 49) &&
            native<std::uint64_t>(owner_end->body, 65) == worker_end->ordinal,
        "owner cleanup exact actual worker END");
  check(native<bool>(owner_end->body, 73) &&
            native<std::uint8_t>(owner_end->body, 74) == 1,
        "joined owner END preserves actual pending semantic debt");
  check(native<std::size_t>(worker_end->body, 27) == 0,
        "worker END preserves actual empty inbox");
  unsigned accepted = 0, duplicates = 0, withdrawn = 0;
  for (const auto *offer : offers) {
    const auto &admission = linked(rows, 2118, reference(*offer));
    const auto branch = native<std::uint8_t>(admission.body, 24);
    if (branch == 2) {
      ++duplicates;
      continue;
    }
    check(branch == 1, "only actual accepted queue offer");
    ++accepted;
    const auto &transfer = linked(rows, 2116, reference(*offer));
    check(transfer.origin.lane == worker_end->origin.lane &&
              transfer.ordinal < worker_end->ordinal &&
              native<std::uint8_t>(transfer.body, 48) == 2,
          "each actual accepted direction withdrawn once before worker END");
    ++withdrawn;
  }
  check(accepted == 16 && duplicates == 1 && withdrawn == 16,
        "queue receipt/transfer conservation");
  check(observed_shutdown_count.load() == 1 && observed_shutdowns[0].how == SHUT_WR &&
            observed_shutdowns[0].result == 0,
        "one actual native persistent shutdown");
  const Record *shutdown_request = nullptr;
  for (const auto &row : rows)
    if (row.site == 2122) {
      check(!shutdown_request, "duplicate native shutdown request");
      shutdown_request = &row;
    }
  check(shutdown_request &&
            native<int>(shutdown_request->body, 24) == observed_shutdowns[0].fd &&
            native<int>(shutdown_request->body, 28) == observed_shutdowns[0].how,
        "exact actual native shutdown inputs");
  const auto &shutdown_return = linked(rows, 2123, reference(*shutdown_request));
  check(native<int>(shutdown_return.body, 24) == observed_shutdowns[0].result &&
            native<int>(shutdown_return.body, 28) == observed_shutdowns[0].error,
        "exact actual native shutdown return/errno");
}
void inherited_alias_and_full_lane() {
  Directory directory;
  std::mutex gate;
  std::condition_variable changed;
  bool entered = false, released = false;
  std::atomic<int> worker_observer{-1};
  std::atomic<bool> blocked{false}, interrupted{false}, persistent{false};
  {
    auto runtime =
        unwrap(RawRuntime::open(directory.path.data(), id<EnvironmentId>(9)));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    Participants pool{
        Value{},
        [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
        [](const Value &) { return std::optional<ApplicationRecordId>{}; }};
    pool.start("run", Value::object({{"request", request()}}),
               [&](const Value &, const ColleagueCapture &,
                   const std::function<bool()> &, const RawAdapterContext *) -> Value {
                 worker_observer.store(raw_wait_control().cancellation_fds[0]);
                 selected_wait_fd.store(worker_observer.load());
                 native_wait_entered.store(false);
                 {
                   std::unique_lock lock{gate};
                   entered = true;
                   changed.notify_one();
                   changed.wait(lock, [&] { return released; });
                 }
                 blocked.store(true);
                 for (;;) {
                   auto capacity = raw_try_claim_control(1024);
                   if (!capacity.has_value())
                     break;
                   auto held = std::move(capacity).value();
                   const std::array<std::byte, 1008> fill{};
                   const ByteView part{fill};
                   (void)unwrap(raw_emit(9293, std::span{&part, 1},
                                         RawPartition::control, &held));
                 }
                 for (;;) {
                   auto capacity = raw_try_claim_control(16);
                   if (!capacity.has_value())
                     break;
                   auto held = std::move(capacity).value();
                   (void)unwrap(raw_emit(9294, {}, RawPartition::control, &held));
                 }
                 const auto claim = raw_claim_control(16);
                 interrupted.store(!claim.has_value() &&
                                   claim.error().code == ErrorCode::interrupted);
                 pollfd ready{worker_observer.load(), POLLIN, 0};
                 const bool first =
                     ::poll(&ready, 1, 0) == 1 && (ready.revents & POLLIN) != 0;
                 char byte = 0;
                 const auto eof = ::read(ready.fd, &byte, 1);
                 ready.revents = 0;
                 persistent.store(first && eof == 0 && ::poll(&ready, 1, 0) == 1 &&
                                  (ready.revents & POLLIN) != 0);
                 throw Error{ErrorCode::interrupted};
               });
    {
      std::unique_lock lock{gate};
      changed.wait(lock, [&] { return entered; });
    }
    struct Release {
      std::mutex &gate;
      std::condition_variable &changed;
      bool &released;
      ~Release() {
        {
          std::lock_guard lock{gate};
          released = true;
        }
        changed.notify_all();
      }
    } release_on_failure{gate, changed, released};
    Child alias;
    alias.pid = ::fork();
    check(alias.pid >= 0, "actual fork alias");
    if (alias.pid == 0) {
      (void)::kill(::getpid(), SIGSTOP);
      _exit(0);
    }
    int child_status = 0;
    check(::waitpid(alias.pid, &child_status, WUNTRACED) == alias.pid &&
              WIFSTOPPED(child_status),
          "owned inherited child actually stopped before exec");
    const auto holders = unwrap(runtime->status());
    Resume resume{{holders.recorder, holders.supervisor}};
    check(::kill(holders.recorder, SIGSTOP) == 0 &&
              ::kill(holders.supervisor, SIGSTOP) == 0,
          "stop exact recorder/supervisor");
    confirm_stop(holders.recorder);
    confirm_stop(holders.supervisor);
    // Fill ordinary owner CONTROL physically; every held cleanup token remains
    // excluded by the producer's availability calculation.
    for (;;) {
      auto claim = raw_try_claim_control(1024);
      if (!claim.has_value())
        break;
      auto held = std::move(claim).value();
      const std::array<std::byte, 1008> fill{};
      const ByteView field{fill};
      (void)unwrap(raw_emit(9291, std::span{&field, 1}, RawPartition::control, &held));
    }
    for (;;) {
      auto claim = raw_try_claim_control(16);
      if (!claim.has_value())
        break;
      auto held = std::move(claim).value();
      (void)unwrap(raw_emit(9292, {}, RawPartition::control, &held));
    }
    check(!raw_try_claim_control(16).has_value(),
          "ordinary owner CONTROL physically full");
    std::array<int, 2> cancellation{-1, -1};
    check(::pipe(cancellation.data()) == 0, "owner cancellation endpoint");
    const char byte = 1;
    check(::write(cancellation[1], &byte, 1) == 1, "owner actual sticky cancel");
    {
      RawWaitScope cancelled{{{cancellation[0], -1}, {}}};
      {
        std::lock_guard lock{gate};
        released = true;
      }
      changed.notify_all();
      {
        std::unique_lock lock{native_wait_mutex};
        native_wait_changed.wait(lock, [] { return native_wait_entered.load(); });
      }
      pool.shutdown(); // MUST NOT claim fresh owner capacity or retire mapping.
      check(blocked.load() && interrupted.load() && persistent.load(),
            "held cleanup cancels actual wait with fork alias still stopped");
      check(pool.debt(), "mandatory cleanup reports pending semantic snapshot debt");
    }
    selected_wait_fd.store(-1);
    (void)::close(cancellation[0]);
    (void)::close(cancellation[1]);
    check(::kill(alias.pid, 0) == 0,
          "inherited child remains alive through worker join");
    (void)::kill(alias.pid, SIGKILL);
    check(::waitpid(alias.pid, &child_status, 0) == alias.pid,
          "exact inherited child cleanup");
    alias.pid = -1;
    // Resume before any optional ordinary drain/mapping retirement.
    (void)::kill(holders.recorder, SIGCONT);
    (void)::kill(holders.supervisor, SIGCONT);
    resume.pids = {-1, -1};
    pool.drain();
  }
  const auto rows = records(directory.path.data());
  unsigned end = 0, worker_end = 0;
  for (const auto &row : rows) {
    if (row.site == 2136)
      ++end;
    if (row.site == 2126)
      ++worker_end;
  }
  check(end == 1 && worker_end == 1, "actual stopped-holder held END preserved");
}
void drain_reentrancy_and_retry() {
  Directory directory;
  std::mutex gate;
  std::condition_variable changed;
  bool finished = false;
  bool reentered = false, failed = false;
  unsigned upstream_retains = 0, calls = 0;
  {
    auto runtime =
        unwrap(RawRuntime::open(directory.path.data(), id<EnvironmentId>(10)));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    Participants *current = nullptr;
    Participants pool{
        Value{},
        [&](const OriginalCapture &capture) -> std::optional<SourceReference> {
          const auto label = capture.label;
          if (label != "participant.admission") {
            ++upstream_retains;
            try {
              current->drain();
            } catch (const Error &error) {
              reentered = error.code == ErrorCode::busy;
            }
            if (!failed) {
              failed = true;
              throw Error{ErrorCode::io, EIO};
            }
          }
          return {};
        },
        [](const Value &) { return std::optional<ApplicationRecordId>{}; },
        [&](const Value &row) {
          if (string_field(row, "phase") == "waiting") {
            std::lock_guard lock{gate};
            finished = true;
            changed.notify_one();
          }
        }};
    current = &pool;
    pool.start("run", Value::object({{"request", request()}}),
               [&](const Value &, const ColleagueCapture &,
                   const std::function<bool()> &, const RawAdapterContext *) {
                 ++calls;
                 return upstream();
               });
    {
      std::unique_lock lock{gate};
      changed.wait(lock, [&] { return finished; });
    }
    bool actual_error = false;
    try {
      pool.drain();
    } catch (const Error &error) {
      actual_error = error.code == ErrorCode::io && error.detail == EIO;
    }
    check(actual_error && reentered && pool.debt(),
          "reentrant Retain rejects and keeps actual failed front");
    pool.shutdown();
    pool.drain();
    check(calls == 1 && upstream_retains >= 2 && !pool.debt(),
          "semantic-only retry never replays transport or loses later nodes");
  }
}

void overlapping_source_tickets(bool unknown) {
  Directory directory;
  constexpr std::size_t size = 9 * 1024 * 1024;
  std::mutex gate;
  std::condition_variable changed;
  bool finished = false, second_refused = false, second_acquired = false;
  bool unknown_debt = false;
  std::size_t retained_first = 0, retained_second = 0;
  {
    auto runtime =
        unwrap(RawRuntime::open(directory.path.data(), id<EnvironmentId>(11)));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    Participants pool{
        Value{},
        [&](const OriginalCapture &capture) -> std::optional<SourceReference> {
          if (capture.bytes.size() == size) {
            const char expected = capture.bytes.front();
            for (const char byte : capture.bytes)
              check(byte == expected, "retained ticket original exact at every byte");
            if (expected == 'A')
              retained_first += size;
            else if (expected == 'B')
              retained_second += size;
            else
              throw std::runtime_error{"unexpected retained ticket original"};
          }
          return {};
        },
        [](const Value &) { return std::optional<ApplicationRecordId>{}; },
        [&](const Value &activity) {
          const auto &phase = string_field(activity, "phase");
          if (phase != "running" && phase != "waiting") {
            std::lock_guard lock{gate};
            finished = true;
            changed.notify_one();
          }
        }};
    pool.start(
        "run", Value::object({{"request", request()}}),
        [&](const Value &, const ColleagueCapture &, const std::function<bool()> &,
            const RawAdapterContext *context) -> Value {
          check(context && context->originals, "actual participant receiver");
          const auto parent = context->parent.value_or(RawEventRef{});
          {
            ProviderOriginalLease abandoned{{ProviderOriginalRole::callback_body,
                                             ProviderOriginalMode::callback_extent, 128,
                                             "tickets.abandoned"},
                                            parent,
                                            context->originals};
          } // No prepare/fill/acquisition: pending ticket must be released.
          auto first = std::make_unique<ProviderOriginalLease>(
              ProviderOriginalRequest{ProviderOriginalRole::callback_body,
                                      ProviderOriginalMode::callback_extent, size,
                                      "tickets.first"},
              parent, context->originals);
          first->prepare(size);
          std::fill(first->writable().begin(), first->writable().end(), 'A');
          std::unique_ptr<ProviderOriginalLease> second;
          try {
            second = std::make_unique<ProviderOriginalLease>(
                ProviderOriginalRequest{ProviderOriginalRole::callback_body,
                                        ProviderOriginalMode::callback_extent, size,
                                        "tickets.second"},
                parent, context->originals);
            second->prepare(size);
            std::fill(second->writable().begin(), second->writable().end(), 'B');
            second_acquired = true;
          } catch (const Error &error) {
            second_refused = error == Error{ErrorCode::busy};
          }
          if (unknown)
            first.reset(); // Initialized first snapshot remains unresolved.
          else {
            first->commit(size);
            (void)first->complete();
          }
          if (second) {
            second->commit(size);
            (void)second->complete();
          }
          throw Error{ErrorCode::capacity};
        });
    {
      std::unique_lock lock{gate};
      changed.wait(lock, [&] { return finished; });
    }
    pool.shutdown();
    try {
      pool.drain();
    } catch (const Error &error) {
      unknown_debt = error == Error{ErrorCode::incomplete};
    }
    check(pool.debt() == unknown_debt, "actual ticket semantic debt remains honest");
  }
  const auto rows = records(directory.path.data());
  unsigned first_sources = 0, second_sources = 0;
  const Record *abandoned = nullptr, *second_prepare = nullptr;
  for (const auto &row : rows) {
    if (row.site == 1312) {
      const auto kind = native<std::uint8_t>(row.body, 24);
      const auto offset = kind == 2 ? std::size_t{34} : std::size_t{42};
      check(row.body.size() == offset + size, "actual ticket source native extent");
      const auto expected = row.body[offset];
      for (const auto byte : ByteView{row.body}.subspan(offset))
        check(byte == expected, "every physical ticket byte preserved");
      if (expected == std::byte{'A'}) {
        ++first_sources;
        check(kind == (unknown ? 2 : 1), "first exact/unknown native qualification");
      } else if (expected == std::byte{'B'})
        ++second_sources;
      else
        throw std::runtime_error{"unexpected physical ticket source"};
    }
    if (row.site == 2107) {
      const auto length = native<std::size_t>(row.body, 25);
      check(row.body.size() >= 33 + length, "actual receiver preparation label");
      const std::string_view label{reinterpret_cast<const char *>(row.body.data() + 33),
                                   length};
      if (label == "tickets.abandoned")
        abandoned = &row;
      if (label == "tickets.second")
        second_prepare = &row;
    }
  }
  std::fprintf(stderr,
               "ticket-cut unknown%d refused%d second_acquired%d nativeA%u nativeB%u "
               "retainedA%zu retainedB%zu debt%d\n",
               unknown, second_refused, second_acquired, first_sources, second_sources,
               retained_first, retained_second, unknown_debt);
  check(second_refused && !second_acquired && first_sources == 1 &&
            second_sources == 0 && retained_second == 0 &&
            retained_first == (unknown ? 0 : size) && unknown_debt == unknown,
        "second preparation refuses before acquisition; first original stays owned");
  check(abandoned && second_prepare, "actual abandoned and refused preparations");
  const auto &abandon = linked(rows, 2108, reference(*abandoned));
  check(
      abandon.body ==
          [&] {
            auto bytes = refbytes(reference(*abandoned));
            append(bytes, std::uint8_t{4});
            return bytes;
          }(),
      "abandoned pre-acquisition ticket publishes no invented original");
  const auto &refusal = linked(rows, 2108, reference(*second_prepare));
  auto expected = refbytes(reference(*second_prepare));
  append(expected, std::uint8_t{3});
  append(expected, ErrorCode::busy);
  append(expected, std::int64_t{0});
  check(refusal.body == expected, "exact held native pending-ticket refusal");
}
struct FailedWakeMemory {
  FailedWakeCase *state;
  FailedWakeMemory() {
    void *memory = ::mmap(nullptr, sizeof(FailedWakeCase), PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANON, -1, 0);
    check(memory != MAP_FAILED, "native shared failed-wake observation");
    state = new (memory) FailedWakeCase{};
  }
  ~FailedWakeMemory() {
    state->~FailedWakeCase();
    (void)::munmap(state, sizeof(FailedWakeCase));
  }
};
struct FailedWakeOwner {
  pid_t owner = -1;
  FailedWakeCase &state;
  std::mutex mutex;
  std::condition_variable changed;
  bool done = false, observed_exit = false;
  int status = 0;
  std::thread waiter;
  explicit FailedWakeOwner(FailedWakeCase &s) : state(s) {}
  void watch() {
    waiter = std::thread{[this] {
      siginfo_t information{};
      int result;
      do {
        result =
            ::waitid(P_PID, static_cast<id_t>(owner), &information, WEXITED | WNOWAIT);
      } while (result < 0 && errno == EINTR);
      {
        std::lock_guard lock{mutex};
        observed_exit = result == 0 && information.si_pid == owner;
        done = true;
      }
      changed.notify_one();
    }};
  }
  bool exited() {
    std::unique_lock lock{mutex};
    return changed.wait_for(lock, std::chrono::seconds{2}, [&] { return done; });
  }
  void cleanup() {
    bool exited;
    {
      std::lock_guard lock{mutex};
      exited = observed_exit;
    }
    // waitid WNOWAIT leaves our child unreaped, so its PID cannot be reused.
    if (owner > 0 && !exited)
      (void)::kill(owner, SIGKILL);
    if (waiter.joinable())
      waiter.join();
    if (owner > 0) {
      pid_t result;
      do {
        result = ::waitpid(owner, &status, 0);
      } while (result < 0 && errno == EINTR);
      if (result != owner)
        status = -1;
    }
    owner = -1;
    for (const auto pid : {state.alias, state.recorder, state.supervisor})
      if (pid > 0)
        (void)::kill(pid, SIGKILL);
    state.alias = state.recorder = state.supervisor = -1;
  }
  ~FailedWakeOwner() { cleanup(); }
};
void owner_callback_reentry() {
  Directory directory;
  unsigned admissions = 0, dispatches = 0, start_busy = 0, send_busy = 0,
           archive_busy = 0, save_busy = 0, publish_busy = 0;
  {
    auto runtime =
        unwrap(RawRuntime::open(directory.path.data(), id<EnvironmentId>(13)));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    const auto owner_thread = std::this_thread::get_id();
    std::mutex gate;
    std::condition_variable changed;
    bool entered = false, released = false, sending = false;
    Participants *owner = nullptr;
    ParticipantTransport transport = [&](const Value &, const ColleagueCapture &,
                                         const std::function<bool()> &,
                                         const RawAdapterContext *) -> Value {
      std::unique_lock lock{gate};
      ++dispatches;
      entered = true;
      changed.notify_one();
      changed.wait(lock, [&] { return released; });
      throw Error{ErrorCode::interrupted};
    };
    const auto start_args = Value::object({{"request", request()}});
    const auto message = direction(0);
    const auto refused = [](const auto &call) {
      try {
        call();
      } catch (const Error &error) {
        return error.code == ErrorCode::busy && error.detail == 0;
      }
      return false;
    };
    Participants pool{
        Value{},
        [&](const OriginalCapture &capture) -> std::optional<SourceReference> {
          if (capture.label != "participant.admission")
            return {};
          ++admissions;
          if (!sending) {
            start_busy +=
                refused([&] { (void)owner->start("run", start_args, transport); });
          } else {
            send_busy += refused([&] { (void)owner->send(message); });
            archive_busy += refused([&] { (void)owner->archive(args()); });
          }
          return {};
        },
        [&](const Value &) -> std::optional<ApplicationRecordId> {
          if (save_busy == 0)
            save_busy += refused([&] { (void)owner->read(args()); });
          return {};
        },
        [&](const Value &) {
          if (std::this_thread::get_id() == owner_thread && publish_busy == 0)
            publish_busy += refused([&] {
              (void)owner->configure(
                  Value::object({{"concurrency", Value{Number{2}}}}));
            });
        }};
    owner = &pool;
    pool.start("run", start_args, transport);
    {
      std::unique_lock lock{gate};
      changed.wait(lock, [&] { return entered; });
    }
    struct Release {
      std::mutex &gate;
      std::condition_variable &changed;
      bool &released;
      ~Release() {
        {
          std::lock_guard lock{gate};
          released = true;
        }
        changed.notify_all();
      }
    } release_on_failure{gate, changed, released};
    sending = true;
    check(field(pool.send(message), "accepted") == Value{true},
          "outer actual same-message admission accepted");
    check(field(pool.send(message), "duplicate") == Value{true},
          "later repeated actual receipt is duplicate");
    const auto row = pool.read(args());
    check(field(row, "queued_directions") == Value{Number{1}} &&
              field(row, "accepted_directions") == Value{Number{1}},
          "reentrant callback cannot admit a second delivery/receipt");
    check(start_busy == 1 && send_busy == 1 && archive_busy == 1 && save_busy == 1 &&
              publish_busy == 1 && admissions == 2,
          "actual Retain/Save/owner Publish callbacks refuse ordinary reentry");
    (void)pool.cancel(args());
    {
      std::lock_guard lock{gate};
      released = true;
    }
    changed.notify_one();
    pool.shutdown();
    pool.drain();
    check(dispatches == 1 && !pool.debt(),
          "one worker dispatch; accepted direction withdrawal and no lost debt");
  }
  const auto rows = records(directory.path.data());
  unsigned starts = 0, offers = 0, withdrawn = 0, pairs = 0, closes = 0, busy = 0;
  std::array<unsigned, 13> busy_actions{};
  for (const auto &row : rows) {
    starts += row.site == 2102;
    offers += row.site == 2115;
    pairs += row.site == 2120 && native<std::uint8_t>(row.body, 24) == 1;
    closes += row.site == 2125;
    if (row.site == 2125)
      check(row.body.size() == 32 && native<int>(row.body, 24) == 0 &&
                native<int>(row.body, 28) == 0,
            "every actual endpoint close succeeds");
    if (row.site == 2116) {
      ++withdrawn;
      check(native<std::uint8_t>(row.body, 48) == 2,
            "actual queued direction withdrawn without dispatch");
    }
    if (row.site != 2100)
      continue;
    const auto &outcome = linked(rows, 2101, reference(row));
    if (native<std::uint8_t>(outcome.body, 24) != 2 ||
        native<std::uint8_t>(outcome.body, 25) != 0)
      continue;
    check(outcome.body.size() == 35 &&
              native<ErrorCode>(outcome.body, 26) == ErrorCode::busy &&
              native<std::int64_t>(outcome.body, 27) == 0 && row.body.size() == 3 &&
              !native<bool>(row.body, 1) && !native<bool>(row.body, 2),
          "pre-argument callback refusal preserves exact native busy outcome");
    const auto kind = native<std::uint8_t>(row.body);
    check(kind < busy_actions.size(), "actual callback action kind");
    ++busy_actions[kind];
    ++busy;
  }
  check(starts == 1 && offers == 2 && withdrawn == 1 && pairs == 1 && closes == 4 &&
            busy == 5,
        "one Run/endpoint pair; no second acquisition or endpoint leak");
  for (const auto kind : {RawParticipantAction::start, RawParticipantAction::send,
                          RawParticipantAction::archive, RawParticipantAction::read,
                          RawParticipantAction::configure})
    check(busy_actions[static_cast<std::uint8_t>(kind)] == 1,
          "each actual refused callback kind has one linked native outcome");
}
void callback_cleanup_before_launch() {
  Directory directory;
  unsigned admissions = 0, dispatches = 0;
  bool cleaned = false, refused = false;
  {
    auto runtime =
        unwrap(RawRuntime::open(directory.path.data(), id<EnvironmentId>(14)));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    Participants *owner = nullptr;
    Participants pool{Value{},
                      [&](const OriginalCapture &) {
                        ++admissions;
                        return std::optional<SourceReference>{};
                      },
                      [&](const Value &) {
                        if (!cleaned) {
                          cleaned = true;
                          fail_shutdown.store(true);
                          owner->shutdown();
                        }
                        return std::optional<ApplicationRecordId>{};
                      }};
    owner = &pool;
    try {
      (void)pool.start("run", Value::object({{"request", request()}}),
                       [&](const Value &, const ColleagueCapture &,
                           const std::function<bool()> &, const RawAdapterContext *) {
                         ++dispatches;
                         return upstream();
                       });
    } catch (const Error &error) {
      refused = error.code == ErrorCode::busy && error.detail == 0;
    }
    fail_shutdown.store(false);
    pool.drain();
    const auto row = pool.read(args());
    check(refused && cleaned && admissions == 1 && dispatches == 0 &&
              string_field(row, "state") == "unknown" && !pool.debt(),
          "mandatory callback cleanup remains available before dispatch");
  }
  const auto rows = records(directory.path.data());
  unsigned failures = 0, joins = 0, ends = 0;
  for (const auto &row : rows) {
    if (row.site == 2123) {
      ++failures;
      auto expected = std::vector<std::byte>{row.body.begin(), row.body.begin() + 24};
      append(expected, int{-1});
      append(expected, int{EBADF});
      check(row.body == expected,
            "prelaunch actual failed shutdown does not invent native success");
    }
    if (row.site == 2127) {
      ++joins;
      check(!native<bool>(row.body, 48), "prelaunch cleanup has no borrowed worker");
    }
    if (row.site == 2136) {
      ++ends;
      check(!native<bool>(row.body, 48) && native<bool>(row.body, 49) &&
                native<std::uint8_t>(row.body, 50) == 2,
            "prelaunch owner END has no worker END and actual pending debt");
    }
    check(row.site != 2103 && row.site != 2126,
          "callback cleanup cannot launch a closed Run");
  }
  check(failures == 1 && joins == 1 && ends == 1,
        "one held prelaunch cleanup; no fatal exit or invented worker END");
}
void quiescent_channel_reclaim() {
  Directory directory;
  const auto basename =
      std::filesystem::path{directory.path.data()}.filename().string();
  const auto name = "/bbq" + basename.substr(basename.size() - 6);
  auto channel =
      unwrap(RawChannel::create(name, id<AuditStreamId>(15).bytes(), {64, 64, 64}));
  struct Unlink {
    std::string_view name;
    ~Unlink() { (void)RawChannel::unlink(name); }
  } unlink{name};
  {
    RawProducer producer{channel, -1};
    unwrap(producer.reserve(RawPartition::control, 32));
    (void)unwrap(producer.emit(9296, {}, RawPartition::control));
    (void)unwrap(producer.emit(9297, {}, RawPartition::control));
    const auto actual = unwrap(channel.read(RawPartition::control, 0, 64));
    std::ofstream saved{std::filesystem::path{directory.path.data()} / "raw",
                        std::ios::binary};
    saved.write(reinterpret_cast<const char *>(actual.bytes.data()),
                static_cast<std::streamsize>(actual.bytes.size()));
    saved.close();
    check(saved.good() && actual.end == 32, "actual complete native raw prefix stored");
    unwrap(channel.arm_space());
    const auto failed = channel.acknowledge(RawPartition::control, 16, -1);
    check(!failed.has_value() && failed.error().code == ErrorCode::invalid_range &&
              unwrap(channel.cursors(RawPartition::control)).acknowledged == 16,
          "ordinary armed absent endpoint still reports native notice failure");
    unwrap(channel.arm_space());
  } // The only producer is destroyed before explicit quiescent reclaim.
  for (const auto end : {std::uint64_t{17}, std::uint64_t{33}}) {
    const auto rejected_boundary =
        channel.acknowledge_quiescent(RawPartition::control, end);
    check(!rejected_boundary.has_value() &&
              rejected_boundary.error().code == ErrorCode::invalid_range &&
              unwrap(channel.cursors(RawPartition::control)).acknowledged == 16,
          "quiescent partial/past-publication boundary leaves cursor intact");
  }
  unwrap(channel.acknowledge_quiescent(RawPartition::control, 32));
  const auto cursor = unwrap(channel.cursors(RawPartition::control));
  check(cursor.published == 32 && cursor.acknowledged == 32,
        "fenced acknowledgement advances only actual preserved native prefix");
  unwrap(channel.acknowledge(RawPartition::control, 32, -1));
}
void await_dead_catalogue(const char *directory) {
  const auto base = std::filesystem::path{directory} / "raw";
  const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{2};
  for (const auto &entry : std::filesystem::directory_iterator{base}) {
    const auto file = entry.path() / "lanes";
    const int descriptor = ::open(file.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    check(descriptor >= 0, "actual orphan catalogue descriptor");
    struct Close {
      int fd;
      ~Close() { (void)::close(fd); }
    } close{descriptor};
    for (;;) {
      if (::flock(descriptor, LOCK_SH | LOCK_NB) == 0) {
        check(::flock(descriptor, LOCK_UN) == 0,
              "release fixture catalogue observation before recovery");
        break;
      }
      check(errno == EWOULDBLOCK || errno == EAGAIN,
            "actual held catalogue lease refusal");
      check(std::chrono::steady_clock::now() < limit,
            "killed holders actually release inherited catalogue descriptors");
      // Controller-only native wait for killed processes to finish FD teardown.
      // No source cadence or runtime retry is changed.
      (void)::poll(nullptr, 0, 1);
    }
  }
}
void failed_borrowed_shutdown() {
  Directory directory;
  directory.preserve = true;
  FailedWakeMemory memory;
  FailedWakeOwner child{*memory.state};
  std::array<int, 2> ready{-1, -1};
  check(::pipe(ready.data()) == 0, "native failed-wake setup pipe");
  struct Close {
    std::array<int, 2> &fds;
    ~Close() {
      for (const auto fd : fds)
        if (fd >= 0)
          (void)::close(fd);
    }
  } close{ready};
  failed_wake_case = memory.state;
  child.owner = ::fork();
  check(child.owner >= 0, "native failed-wake owner fork");
  if (child.owner == 0) {
    (void)::close(ready[0]);
    try {
      std::mutex gate;
      std::condition_variable changed;
      bool entered = false, released = false;
      auto runtime =
          unwrap(RawRuntime::open(directory.path.data(), id<EnvironmentId>(12)));
      RawThreadBinding binding{unwrap(runtime->prepare_thread())};
      Participants pool{
          Value{},
          [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
          [](const Value &) { return std::optional<ApplicationRecordId>{}; }};
      pool.start(
          "run", Value::object({{"request", request()}}),
          [&](const Value &, const ColleagueCapture &, const std::function<bool()> &,
              const RawAdapterContext *) -> Value {
            selected_wait_fd.store(raw_wait_control().cancellation_fds[0]);
            native_wait_entered.store(false);
            {
              std::unique_lock lock{gate};
              entered = true;
              changed.notify_one();
              changed.wait(lock, [&] { return released; });
            }
            for (const auto size : {std::uint64_t{1024}, std::uint64_t{16}})
              for (;;) {
                auto claim = raw_try_claim_control(size);
                if (!claim.has_value()) {
                  check(claim.error().code == ErrorCode::capacity,
                        "actual worker capacity refusal");
                  break;
                }
                auto credit = std::move(claim).value();
                const std::array<std::byte, 1008> padding{};
                const ByteView bytes{padding};
                const auto fields = size == 16 ? std::span<const ByteView>{}
                                               : std::span{&bytes, std::size_t{1}};
                (void)unwrap(raw_emit(9295, fields, RawPartition::control, &credit));
              }
            const auto claim = raw_claim_control(16);
            if (!claim.has_value())
              throw claim.error();
            throw Error{ErrorCode::corrupt};
          });
      {
        std::unique_lock lock{gate};
        changed.wait(lock, [&] { return entered; });
      }
      const auto holders = unwrap(runtime->status());
      failed_wake_case->recorder = holders.recorder;
      failed_wake_case->supervisor = holders.supervisor;
      const auto alias = ::fork();
      check(alias >= 0, "native failed-wake alias fork");
      if (alias == 0) {
        (void)::kill(::getpid(), SIGSTOP);
        ::_exit(0);
      }
      failed_wake_case->alias = alias;
      int alias_status = 0;
      check(::waitpid(failed_wake_case->alias, &alias_status, WUNTRACED) ==
                    failed_wake_case->alias &&
                WIFSTOPPED(alias_status),
            "actual preexec alias stopped");
      check(::kill(holders.recorder, SIGSTOP) == 0 &&
                ::kill(holders.supervisor, SIGSTOP) == 0,
            "stop actual failed-wake holders");
      confirm_stop(holders.recorder);
      confirm_stop(holders.supervisor);
      {
        std::lock_guard lock{gate};
        released = true;
      }
      changed.notify_one();
      {
        std::unique_lock lock{native_wait_mutex};
        native_wait_changed.wait(lock, [] { return native_wait_entered.load(); });
      }
      failed_wake_case->phase.store(1, std::memory_order_release);
      const char byte = 1;
      check(::write(ready[1], &byte, 1) == 1, "actual parked failure-cut setup");
      fail_shutdown.store(true);
      pool.shutdown(); // Baseline blocks; corrected owner publishes failure and
                       // exits70.
      ::_exit(0);
    } catch (...) {
      ::_exit(69);
    }
  }
  failed_wake_case = nullptr;
  (void)::close(std::exchange(ready[1], -1));
  child.watch();
  pollfd setup{ready[0], POLLIN, 0};
  check(::poll(&setup, 1, 5000) == 1 && (setup.revents & POLLIN),
        "actual parked child setup arrives");
  char byte = 0;
  check(::read(ready[0], &byte, 1) == 1 && byte == 1, "actual parked child setup");
  const bool exited = child.exited();
  const auto phase = memory.state->phase.load(std::memory_order_acquire);
  const auto actual = memory.state->shutdown;
  child.cleanup(); // Exact owned cleanup before any assertion/recovery, also for
                   // baseline red.
  const int actual_status = child.status;
  await_dead_catalogue(directory.path.data());
  std::fprintf(stderr,
               "failed-wake exited%d status%d phase%u native_return%d native_errno%d\n",
               exited, actual_status, phase, actual.result, actual.error);
  {
    auto reopened = RawRuntime::open(directory.path.data(), id<EnvironmentId>(12));
    if (!reopened.has_value()) {
      directory.preserve = true;
      std::fprintf(stderr, "failed-wake preserved raw directory %s\n",
                   directory.path.data());
      throw reopened.error();
    }
    auto recovered = std::move(reopened).value();
  }
  check(exited && WIFEXITED(actual_status) && WEXITSTATUS(actual_status) == 70 &&
            phase == 2 && actual.how == SHUT_WR && actual.result == -1 &&
            actual.error == EBADF,
        "failed borrowed native shutdown exits70 before blocked join");
  const auto runs = unwrap(discover_raw_runs(directory.path.data()));
  unsigned failures = 0, joins = 0, worker_ends = 0, owner_ends = 0;
  std::vector<Record> preserved;
  for (const auto &run : runs) {
    check(run.snapshot && !run.error && run.snapshot->closed,
          "ordinary orphan preservation closes uncertain raw run");
    auto reader = unwrap(RawRunReader::open(
        (std::filesystem::path{directory.path.data()} / "raw" / run.name).string()));
    std::array<std::byte, 131072> output{};
    while (const auto row = unwrap(reader->read(output))) {
      const auto site = native<std::uint32_t>(row->raw, 4);
      preserved.push_back({row->origin,
                           site,
                           native<std::uint64_t>(row->raw, 8),
                           {row->raw.begin() + 16, row->raw.end()}});
      if (site == 2123) {
        ++failures;
        check(native<int>(row->raw, 40) == -1 && native<int>(row->raw, 44) == EBADF,
              "preserved actual failed shutdown native fields");
      }
      joins += site == 2127;
      worker_ends += site == 2126;
      owner_ends += site == 2136;
    }
  }
  check(failures == 1 && joins == 0 && worker_ends == 0 && owner_ends == 0,
        "one preserved failed outcome; no join or invented successful END");
  unsigned requests = 0;
  for (const auto &request_row : preserved) {
    if (request_row.site != 2122)
      continue;
    ++requests;
    auto expected =
        std::vector<std::byte>{request_row.body.begin(), request_row.body.begin() + 24};
    append(expected, actual.fd);
    append(expected, actual.how);
    check(request_row.body == expected,
          "shutdown request preserves exact forwarded fd/method");
    const auto &returned = linked(preserved, 2123, reference(request_row));
    expected = refbytes(reference(request_row));
    append(expected, actual.result);
    append(expected, actual.error);
    check(returned.body == expected && returned.ordinal > request_row.ordinal &&
              returned.origin.lane == request_row.origin.lane,
          "preserved failed return links exact actual native shutdown request");
  }
  check(requests == 1, "one actual borrowed shutdown request");
  directory.preserve = false;
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view{argv[1]} == "ticket-cut") {
      overlapping_source_tickets(false);
      overlapping_source_tickets(true);
      return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "shutdown-cut") {
      quiescent_channel_reclaim();
      failed_borrowed_shutdown();
      return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "reentry-cut") {
      owner_callback_reentry();
      callback_cleanup_before_launch();
      return 0;
    }
    native_original();
    source_extent(false, false);
    source_extent(false, true);
    source_extent(true, false);
    directions_and_cleanup();
    inherited_alias_and_full_lane();
    drain_reentrancy_and_retry();
    owner_callback_reentry();
    callback_cleanup_before_launch();
    overlapping_source_tickets(false);
    overlapping_source_tickets(true);
    quiescent_channel_reclaim();
    failed_borrowed_shutdown();
  } catch (const Error &error) {
    std::fprintf(stderr, "participants_raw %s detail%lld\n", error_name(error.code),
                 static_cast<long long>(error.detail));
    return 1;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "participants_raw %s\n", error.what());
    return 1;
  }
}
