#include "blackbird/coding.hpp"
#include "blackbird/context.hpp"
#include "blackbird/raw_command.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <poll.h>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
thread_local bool observe_allocations = false;
thread_local std::size_t observed_allocations = 0;
using NativeWrite = ssize_t (*)(int, const void *, std::size_t);
using NativePoll = int (*)(pollfd *, nfds_t, int);
NativeWrite native_write = nullptr;
NativePoll native_poll = nullptr;
using NativeShutdown = int (*)(int, int);
NativeShutdown native_shutdown = nullptr;
std::atomic<int> gated_shutdown_fd{-1};
std::atomic<bool> shutdown_entered{false}, shutdown_release{false};
std::atomic<unsigned> shutdown_calls{0};
using NativeFcntl = int (*)(int, int, ...);
using NativeDup2 = int (*)(int, int);
NativeFcntl native_fcntl = nullptr;
NativeDup2 native_dup2 = nullptr;
std::atomic_bool break_getfl{false}, break_dup2{false};
std::atomic<int> gated_dup2_target{-1};
std::atomic_bool dup2_entered{false}, dup2_release{false};
std::atomic<int> observed_cancel_fd{-1};
std::atomic<bool> parked{false};
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char name[] = "/tmp/blackbird-command-wake-XXXXXX";
    auto *created = ::mkdtemp(name);
    require(created != nullptr, "private native wake fixture directory");
    path = created;
  }
  ~Directory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};
struct Pipe {
  int fds[2]{-1, -1};
  Pipe() { require(::pipe(fds) == 0, "native fixture pipe"); }
  ~Pipe() {
    if (fds[0] >= 0)
      (void)::close(fds[0]);
    if (fds[1] >= 0)
      (void)::close(fds[1]);
  }
};
void until(const std::atomic<bool> &condition) {
  while (!condition.load(std::memory_order_acquire))
    std::this_thread::yield();
}
struct Alias {
  pid_t pid = -1;
  Alias() {
    pid = ::fork();
    require(pid >= 0, "fork native stopped pre-exec alias");
    if (pid == 0) {
      (void)::raise(SIGSTOP);
      ::_exit(0);
    }
    int status = 0;
    require(::waitpid(pid, &status, WUNTRACED) == pid && WIFSTOPPED(status),
            "real inherited endpoint alias remains stopped");
  }
  ~Alias() {
    if (pid > 0) {
      (void)::kill(pid, SIGKILL);
      int status = 0;
      while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
    }
  }
};
EnvironmentId environment() {
  IdentityBytes value{};
  value[0] = std::byte{107};
  return unwrap(EnvironmentId::from_bytes(value));
}
using Key = std::pair<IdentityBytes, std::uint64_t>;
struct Record {
  std::uint32_t site;
  RawPartition partition;
  std::vector<std::byte> body;
};
using Records = std::map<Key, Record>;
template <class T> T take(ByteView &body) {
  require(body.size() >= sizeof(T), "literal native field exists");
  T result{};
  ::memcpy(&result, body.data(), sizeof(result));
  body = body.subspan(sizeof(result));
  return result;
}
Key ref(ByteView &body) {
  const auto lane = take<IdentityBytes>(body);
  return {lane, take<std::uint64_t>(body)};
}
Records records(const std::filesystem::path &session, bool closed = true) {
  const auto runs = unwrap(discover_raw_runs(session.string()));
  require(runs.size() == 1 && runs.front().snapshot &&
              (!closed || runs.front().snapshot->closed),
          "native run available");
  auto reader =
      unwrap(RawRunReader::open((session / "raw" / runs.front().name).string()));
  std::vector<std::byte> output(2 * 1024 * 1024 + 4096);
  Records all;
  while (const auto event = unwrap(reader->read(output))) {
    auto body = event->raw;
    const auto length = take<std::uint32_t>(body);
    const auto site = take<std::uint32_t>(body);
    const auto ordinal = take<std::uint64_t>(body);
    require(length == event->raw.size(), "native record exact framing");
    require(
        all.emplace(Key{event->origin.lane, ordinal},
                    Record{site, event->origin.partition, {body.begin(), body.end()}})
            .second,
        "actual producer ordinal remains unique");
  }
  return all;
}
void verify(const Records &all, unsigned expected_shutdowns, bool failed = false) {
  unsigned shutdowns = 0;
  for (const auto &[key, event] : all) {
    require(event.site != 8001, "valid wake does not fence its producer");
    if (event.site < 2050 || event.site > 2065 || event.site % 2 == 0)
      continue;
    ByteView body{event.body};
    const auto request = ref(body);
    const auto found = all.find(request);
    require(found != all.end() && found->second.site + 1 == event.site &&
                request.first == key.first && request.second < key.second &&
                event.partition == RawPartition::control,
            "actual native return references earlier same-producer request");
    if (event.site == 2057) {
      ++shutdowns;
      require(body.size() == 2 * sizeof(int),
              "shutdown return contains literal native fields");
      const auto result = take<int>(body), error = take<int>(body);
      require(failed ? result == -1 && error == EBADF : result == 0,
              "actual shutdown result and errno preserved");
      ByteView request_body{found->second.body};
      const auto incarnation = ref(request_body);
      require(all.contains(incarnation) && all.at(incarnation).site == 2050,
              "shutdown names real socket incarnation");
      (void)take<int>(request_body);
      require(take<int>(request_body) == SHUT_WR && request_body.empty(),
              "native shutdown operation captured exactly");
    }
  }
  require(shutdowns == expected_shutdowns,
          "one native shutdown per acquired incarnation");
}
#if defined(__linux__)
constexpr std::size_t native_mask_bytes = 8;
#else
constexpr std::size_t native_mask_bytes = sizeof(sigset_t);
#endif
void mask_equal(ByteView &body, const sigset_t &expected) {
  require(body.size() >= native_mask_bytes, "actual native kernel signal mask field");
  require(::memcmp(body.data(), &expected, native_mask_bytes) == 0,
          "actual native signal mask bytes match independently observed mask");
  body = body.subspan(native_mask_bytes);
}
void action_equal(ByteView &body, const struct sigaction &expected) {
  require(take<decltype(expected.sa_handler)>(body) == expected.sa_handler &&
              take<decltype(expected.sa_flags)>(body) == expected.sa_flags,
          "actual action handler and flags match independently observed action");
  mask_equal(body, expected.sa_mask);
#if defined(__linux__)
  require(take<decltype(expected.sa_restorer)>(body) == expected.sa_restorer,
          "actual Linux native restorer field is retained");
#endif
  require(body.empty(), "native action member fields omit padding and unused words");
}
void signal_fields(const Records &all, const struct sigaction &before,
                   const struct sigaction &installed, const sigset_t &owner_mask) {
  struct sigaction requested{};
  requested.sa_handler = installed.sa_handler;
  sigemptyset(&requested.sa_mask);
  sigset_t blocked{};
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGINT);
  sigset_t after_block = owner_mask;
  sigaddset(&after_block, SIGINT);
  unsigned actions = 0, masks = 0;
  for (const auto &[key, event] : all) {
    if (event.site != 2062 && event.site != 2064)
      continue;
    auto body = ByteView{event.body};
    (void)ref(body);
    const auto returned = all.find({key.first, key.second + 1});
    require(returned != all.end() && returned->second.site == event.site + 1,
            "actual native signal call has its immediate protected return");
    auto result = ByteView{returned->second.body};
    require(ref(result) == key && take<int>(result) == 0,
            "native signal return matches actual successful caller request");
    (void)take<int>(result); // Native errno copied even when not authoritative.
    require(take<bool>(result), "successful native call has acquired previous fields");
    if (event.site == 2062) {
      require(take<int>(body) == SIGINT && actions < 2, "two real SIGINT actions");
      action_equal(body, actions == 0 ? requested : before);
      action_equal(result, actions == 0 ? before : installed);
      ++actions;
    } else {
      require(take<int>(body) == (masks == 0 ? SIG_BLOCK : SIG_SETMASK) && masks < 2,
              "actual protected block and restore methods");
      mask_equal(body, masks == 0 ? blocked : owner_mask);
      mask_equal(result, masks == 0 ? owner_mask : after_block);
      require(body.empty() && result.empty(), "native masks contain no unused words");
      ++masks;
    }
  }
  require(actions == 2 && masks == 2, "actual install/restore and teardown masks");
}
void require_flags(const RawCommandWake &wake) {
  for (const int descriptor : {wake.read_fd(), wake.write_fd()}) {
    require(descriptor >= 4 && (::fcntl(descriptor, F_GETFD) & FD_CLOEXEC) != 0 &&
                (::fcntl(descriptor, F_GETFL) & O_NONBLOCK) != 0,
            "both actual endpoints are normalized, nonblocking and CLOEXEC");
  }
}
void parked_alias_socket() {
  Directory directory;
  auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(),
                                         {{64, 8192, 4096}, 4096}));
  auto caller = unwrap(runtime->prepare_thread());
  auto producer = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{caller};
    RawCommandWake wake{RawCommandWake::Mode::signal};
    require_flags(wake);
    Alias alias;
    wake.publish();
    const auto status = unwrap(runtime->status());
    require(::kill(status.recorder, SIGSTOP) == 0,
            "stop recorder before actual pressure");
    observed_cancel_fd.store(wake.read_fd());
    parked.store(false);
    Result<void> admission = Result<void>::failure({ErrorCode::incomplete});
    std::thread waiter{[&] {
      RawThreadBinding owner{producer};
      const std::byte original[48]{};
      const ByteView parts[]{ByteView{original}};
      (void)unwrap(raw_emit(9200, parts));
      admission = raw_wait_data(16, {{wake.read_fd(), -1}, {}});
    }};
    until(parked);
    std::thread notifier{[&] { wake.notify(); }};
    notifier.join();
    waiter.join();
    require(!admission.has_value() && admission.error().code == ErrorCode::interrupted,
            "native socket shutdown wakes actual parked admission before alias resume");
    pollfd readable{wake.read_fd(), POLLIN, 0};
    require(native_poll(&readable, 1, 0) == 1 && (readable.revents & POLLIN),
            "kernel observer remains readable with stopped inherited alias");
    wake.retire();
    observed_cancel_fd.store(-1);
    require(::kill(status.recorder, SIGCONT) == 0,
            "resume recorder after actual assertion");
  }
  caller.reset();
  producer.reset();
  runtime.reset();
  verify(records(directory.path), 1);
}
void reset_and_signal() {
  Directory directory;
  auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(),
                                         {{64, 8192, 4096}, 4096}));
  auto caller = unwrap(runtime->prepare_thread());
  struct sigaction before{}, installed{}, after{};
  sigset_t owner_mask{}, restored_mask{};
  require(::sigaction(SIGINT, nullptr, &before) == 0 &&
              ::pthread_sigmask(SIG_SETMASK, nullptr, &owner_mask) == 0,
          "observe actual prior handler and owner-thread signal mask");
  {
    RawThreadBinding binding{caller};
    std::atomic_bool interrupted{false};
    RawCommandInterrupt interrupt{interrupted};
    require(::sigaction(SIGINT, nullptr, &installed) == 0,
            "independently observe actual installed native action");
    const int observer = interrupt.wake().read_fd(),
              activation = interrupt.wake().write_fd();
    require_flags(interrupt.wake());
    Alias alias;
    gated_shutdown_fd.store(activation);
    shutdown_entered.store(false);
    shutdown_release.store(false);
    std::atomic_bool started{false}, finish{false};
    std::size_t handler_allocations = 0;
    std::thread worker{[&] {
      observed_allocations = 0;
      observe_allocations = true;
      started.store(true, std::memory_order_release);
      until(finish);
      observe_allocations = false;
      handler_allocations = observed_allocations;
    }};
    until(started);
    require(::pthread_kill(worker.native_handle(), SIGINT) == 0,
            "real SIGINT delivered on actual non-owner thread");
    until(shutdown_entered);
    const auto busy = interrupt.reset();
    require(
        !busy.has_value() && busy.error().code == ErrorCode::busy && interrupted.load(),
        "rearm cannot bind dedicated producer while native handler owns publication");
    shutdown_release.store(true, std::memory_order_release);
    while (shutdown_calls.load() == 0)
      std::this_thread::yield();
    finish.store(true, std::memory_order_release);
    worker.join();
    require(handler_allocations == 0,
            "real cross-thread signal publication performs no allocation");
    gated_shutdown_fd.store(-1);
    unwrap(interrupt.reset());
    require(!interrupted.load() && interrupt.wake().read_fd() == observer &&
                interrupt.wake().write_fd() == activation,
            "joined rearm replaces incarnation at stable numbers");
    require_flags(interrupt.wake());
    pollfd fresh{observer, POLLIN, 0};
    require(native_poll(&fresh, 1, 0) == 0,
            "stopped old inherited alias cannot cancel new socket incarnation");
    // First handler consumed its96; this uncanceled reset reuses the replacement
    // token and must not leave its publisher pointing at a destroyed claim.
    unwrap(interrupt.reset());
    observed_allocations = 0;
    observe_allocations = true;
    const int raised = ::raise(SIGINT);
    observe_allocations = false;
    require(raised == 0 && interrupted.load() && observed_allocations == 0,
            "fresh handler owns second incarnation");
    require(native_poll(&fresh, 1, 0) == 1,
            "second incarnation has genuine native EOF");
  }
  int restored_flags = before.sa_flags;
#if defined(__linux__)
  // The qualified glibc x86_64 ABI adds its native restoration trampoline.
  restored_flags |= 0x04000000;
#endif
  require(::sigaction(SIGINT, nullptr, &after) == 0 &&
              ::pthread_sigmask(SIG_SETMASK, nullptr, &restored_mask) == 0 &&
              before.sa_handler == after.sa_handler && after.sa_flags == restored_flags,
          "actual prior handler and application flags restored through native libc");
  for (int signal = 1; signal < NSIG; ++signal)
    require(sigismember(&before.sa_mask, signal) ==
                    sigismember(&after.sa_mask, signal) &&
                sigismember(&owner_mask, signal) == sigismember(&restored_mask, signal),
            "actual handler mask and owner-thread mask membership restored");
  caller.reset();
  runtime.reset();
  const auto all = records(directory.path);
  verify(all, 2);
  signal_fields(all, before, installed, owner_mask);
}
void signal_during_rearm() {
  Directory directory;
  auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(),
                                         {{64, 8192, 4096}, 4096}));
  auto caller = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{caller};
    std::atomic_bool interrupted{false};
    RawCommandInterrupt interrupt{interrupted};
    const int observer = interrupt.wake().read_fd(),
              activation = interrupt.wake().write_fd();
    Alias alias;
    require(::raise(SIGINT) == 0 && interrupted.load(),
            "old inherited incarnation receives actual cancellation");
    pollfd old{observer, POLLIN, 0};
    require(native_poll(&old, 1, 0) == 1,
            "stopped alias retains readable old incarnation");
    const auto before = shutdown_calls.load();
    gated_dup2_target.store(observer);
    dup2_entered.store(false);
    dup2_release.store(false);
    bool returned_before_release = false, no_shutdown = false, fresh_unready = false,
         sticky = false;
    int raised = -1;
    std::size_t allocations = 0;
    std::thread worker{[&] {
      until(dup2_entered);
      observed_allocations = 0;
      observe_allocations = true;
      raised = ::raise(SIGINT);
      observe_allocations = false;
      allocations = observed_allocations;
      returned_before_release = !dup2_release.load();
      no_shutdown = shutdown_calls.load() == before;
      sticky = interrupted.load();
      pollfd fresh{observer, POLLIN, 0};
      fresh_unready = native_poll(&fresh, 1, 0) == 0;
      dup2_release.store(true, std::memory_order_release);
    }};
    const auto reset = interrupt.reset();
    worker.join();
    gated_dup2_target.store(-1);
    unwrap(reset);
    require(
        raised == 0 && returned_before_release && no_shutdown && sticky &&
            fresh_unready && allocations == 0,
        "real handler returns allocation-free during native rearm without shutdown");
    require(interrupted.load() && shutdown_calls.load() == before + 1 &&
                interrupt.wake().read_fd() == observer &&
                interrupt.wake().write_fd() == activation,
            "returning rearm resynchronizes sticky cancellation on stable descriptors");
    pollfd fresh{observer, POLLIN, 0};
    char byte{};
    require(native_poll(&fresh, 1, 0) == 1 &&
                ::recv(observer, &byte, 1, MSG_DONTWAIT) == 0,
            "new incarnation receives exactly one shutdown and native EOF");
    require_flags(interrupt.wake());
  }
  caller.reset();
  runtime.reset();
  const auto all = records(directory.path);
  verify(all, 2);
  Key initial{}, replacement{};
  unsigned incarnations = 0, new_shutdowns = 0;
  for (const auto &[key, event] : all)
    if (event.site == 2050) {
      ByteView body{event.body};
      const auto parent = ref(body);
      if (parent.second == 0)
        initial = key;
      else {
        replacement = key;
        require(parent == initial, "actual rearm names original socket incarnation");
      }
      ++incarnations;
    }
  require(incarnations == 2 && replacement.second != 0,
          "actual rearm publishes one distinct socket incarnation");
  for (const auto &[key, event] : all)
    if (event.site == 2056) {
      ByteView body{event.body};
      if (ref(body) == replacement)
        ++new_shutdowns;
    }
  require(new_shutdowns == 1,
          "resynchronized shutdown names the actual new incarnation exactly once");
}
void refused_reset_and_predispatch() {
  Directory directory;
  auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(),
                                         {{64, 4096, 4096}, 4096}));
  auto caller = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{caller};
    // Destroy before any worker dispatch: both native endpoints need final credit.
    {
      RawCommandWake unlaunched{RawCommandWake::Mode::one_shot};
      require_flags(unlaunched);
    }
    RawCommandWake wake{RawCommandWake::Mode::signal};
    wake.publish();
    const auto status = unwrap(runtime->status());
    require(::kill(status.recorder, SIGSTOP) == 0,
            "stop recorder for native nonblocking credit refusal");
    const auto descriptor = wake.read_fd();
    unsigned attempts = 0;
    while (true) {
      const auto reset = wake.clear();
      ++attempts;
      if (!reset.has_value()) {
        require(reset.error().code == ErrorCode::capacity,
                "actual refusal is physical capacity");
        break;
      }
      require(attempts < 8, "finite actual ring fills with captured reset operations");
    }
    require(descriptor == wake.read_fd(),
            "refused admission did not replace observer number");
    wake.notify();
    pollfd ready{wake.read_fd(), POLLIN, 0};
    require(native_poll(&ready, 1, 0) == 1,
            "refused reset preserved original unused held notification token");
    wake.retire();
    require(::kill(status.recorder, SIGCONT) == 0,
            "resume recorder after refusal assertion");
  }
  caller.reset();
  runtime.reset();
  verify(records(directory.path), 1);
}
void native_lifecycle_faults() {
  Directory directory;
  auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(),
                                         {{64, 8192, 4096}, 4096}));
  auto caller = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{caller};
    break_getfl.store(true);
    bool setup_failed = false;
    try {
      RawCommandWake wake{RawCommandWake::Mode::one_shot};
    } catch (const Error &error) {
      setup_failed = error == Error{ErrorCode::io, EBADF};
    }
    require(setup_failed && !break_getfl.load(),
            "genuine closed setup descriptor yields ordinary recorded EBADF");
    RawCommandWake wake{RawCommandWake::Mode::signal};
    const int observer = wake.read_fd();
    break_dup2.store(true);
    const auto rearm = wake.clear();
    require(!rearm.has_value() && rearm.error() == Error{ErrorCode::io, EBADF} &&
                !break_dup2.load() && wake.read_fd() == observer,
            "genuine invalid staged descriptor refuses rearm before new waiter "
            "publication");
    require(!wake.clear().has_value(), "native failed incarnation remains refused");
  }
  caller.reset();
  runtime.reset();
  const auto all = records(directory.path);
  verify(all, 0);
  unsigned failed_fcntl = 0, failed_dup2 = 0, failed_close = 0;
  for (const auto &[key, event] : all) {
    (void)key;
    if (event.site != 2053 && event.site != 2059 && event.site != 2055)
      continue;
    ByteView body{event.body};
    (void)ref(body);
    const int result = take<int>(body), error = take<int>(body);
    if (result == -1 && error == EBADF) {
      if (event.site == 2053)
        ++failed_fcntl;
      if (event.site == 2059)
        ++failed_dup2;
      if (event.site == 2055)
        ++failed_close;
    }
  }
  require(failed_fcntl == 1 && failed_dup2 == 1 && failed_close == 2,
          "every actual invalid acquisition/cleanup return survives independently");
}

void native_notice_failure() {
  IdentityBytes identity{};
  identity[0] = std::byte{108};
  const auto name = "/bb-wake-notice-" + std::to_string(::getpid());
  auto channel = unwrap(RawChannel::create(name, identity, {64, 256, 64}));
  struct Unlink {
    std::string name;
    ~Unlink() { (void)RawChannel::unlink(name); }
  } unlink{name};
  auto endpoints = unwrap(raw_socket_pair());
  RawProducer producer{channel, endpoints[0]};
  {
    RawThreadBinding owner{producer, identity};
    auto claim = unwrap(raw_claim_control(96));
    const auto publisher =
        unwrap(RawSignalPublisher::prepare(raw_current_thread(), claim));
    auto native = publisher;
    unwrap(channel.arm_data());
    require(::close(endpoints[0]) == 0, "destroy actual native notice descriptor");
    endpoints[0] = -1;
    const unsigned char body[32]{};
    const RawNativePart parts[]{{body, sizeof(body)}};
    const auto request = native.emit(2056, parts, 1);
    require(request.published && request.notice_failed &&
                request.notice_error == Error{ErrorCode::io, EBADF},
            "actual notice failure preserves already published request");
    const auto returned = native.emit(2057, parts, 1);
    require(returned.published && returned.ordinal == request.ordinal + 2,
            "prepaid native return remains possible after ingest failure event");
    const auto next = raw_try_claim_control(1);
    require(!next.has_value() && next.error().code == ErrorCode::audit_unavailable,
            "native failure fences new ordinary admission");
    const auto failure = unwrap(channel.read(RawPartition::failure, 0, 64));
    ByteView bytes{failure.bytes};
    require(
        take<std::uint32_t>(bytes) == 25 && take<std::uint32_t>(bytes) == 8001 &&
            take<std::uint64_t>(bytes) == request.ordinal + 1 &&
            take<ErrorCode>(bytes) == ErrorCode::io &&
            take<std::int64_t>(bytes) == EBADF && bytes.empty(),
        "existing FAILURE bank records literal ingest failure without signal wrappers");
  }
  (void)::close(endpoints[1]);
}

AuditStreamId journal_identity() {
  IdentityBytes value{};
  value[0] = std::byte{109};
  return unwrap(AuditStreamId::from_bytes(value));
}
JournalHeader semantic_header() {
  return {environment(), journal_identity(), 109, {1024 * 1024, 4 * 1024 * 1024}, {}};
}
void fatal_native_failure(std::string_view executable) {
  Directory directory;
  Pipe custody;
  const auto owner = ::fork();
  require(owner >= 0, "fork isolated real production owner");
  if (owner == 0) {
    custody.fds[0] = -1;
    constexpr std::size_t pressure_bytes = 2 * 1024 * 1024;
    auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(),
                                           {{pressure_bytes, 64 * 1024, 4096}, 4096}));
    auto context = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{context};
    auto retained = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string()))),
        "audit", semantic_header(), {32 * 1024 * 1024, 10000}));
    AuditLog log{*retained};
    ContextStore store{log};
    struct UnusedProvider final : CodingProvider {
      Value respond(const Value &,
                    const std::function<void(std::string_view)> &) override {
        throw std::runtime_error("native exec must not use provider");
      }
    } provider;
    CodingEngine engine{log, store, provider, "fixture"};
    const auto child = directory.path / "actual-child";
    std::filesystem::create_directory(child);
    (void)engine.operator_call(
        "exec",
        Value::object(
            {{"argv", Value{Value::Array{Value{std::string{executable}}, Value{"gate"},
                                         Value{child.string()}}}},
             {"yield_ms", Value{Number{0}}},
             {"timeout_seconds", Value{Number{10}}}}));
    while (!std::filesystem::exists(child / "ready"))
      std::this_thread::yield();
    std::ifstream identity{child / "identity"};
    std::string pid_line;
    std::getline(identity, pid_line);
    require(pid_line.starts_with("pid="),
            "actual native child PID independently available");
    const auto actual_child = static_cast<pid_t>(std::stol(pid_line.substr(4)));
    require(unwrap(retained->unresolved_attempts()).size() == 1,
            "real selected exec has retained opened unresolved custody before fault");
    std::atomic_bool interrupted{false};
    RawCommandInterrupt interrupt{interrupted};
    Alias alias;
    const auto producer = unwrap(runtime->prepare_thread());
    std::vector<std::byte> original(pressure_bytes - raw_envelope_size);
    std::optional<RawCapacityClaim> pressure;
    {
      RawThreadBinding producer_owner{producer};
      pressure.emplace(unwrap(raw_claim_data(pressure_bytes)));
    }
    const auto runtime_status = unwrap(runtime->status());
    require(::kill(runtime_status.recorder, SIGSTOP) == 0,
            "park production recorder for a real waiter");
    observed_cancel_fd.store(interrupt.wake().read_fd());
    parked.store(false);
    std::jthread waiter{[&] {
      RawThreadBinding worker{producer};
      const ByteView parts[]{ByteView{original}};
      (void)unwrap(raw_emit(9200, parts, RawPartition::data, &*pressure));
      (void)raw_wait_data(16, {{interrupt.wake().read_fd(), -1}, {}});
    }};
    until(parked);
    const pid_t custody_ids[]{alias.pid, runtime_status.recorder, actual_child};
    require(native_write(custody.fds[1], custody_ids, sizeof(custody_ids)) ==
                sizeof(custody_ids),
            "transfer actual stopped alias custody to fixture parent");
    // Literal ownership fault: original shared socket remains open in alias,
    // but the production activation descriptor itself is no longer valid.
    require(::close(interrupt.wake().write_fd()) == 0,
            "actual activation descriptor destroyed");
    interrupt.wake().notify();
    ::_exit(99);
  }
  pid_t custody_ids[3]{-1, -1, -1};
  require(::read(custody.fds[0], custody_ids, sizeof(custody_ids)) ==
                  sizeof(custody_ids) &&
              custody_ids[0] > 0,
          "receive actual unresolved alias custody");
  int status = 0;
  require(::waitpid(owner, &status, 0) == owner && WIFEXITED(status) &&
              WEXITSTATUS(status) == 70,
          "production native failure exits70 before unwinding into any join");
  require(::kill(custody_ids[0], 0) == 0,
          "actual inherited alias still exists at fatal owner return");
  require(::kill(custody_ids[1], SIGCONT) == 0,
          "fixture resumes recorder only after production fatal disposition");
  require(::kill(custody_ids[0], SIGKILL) == 0,
          "fixture releases actual externally retained alias afterward");
  // This stopped pre-exec alias also inherited the real business writer lock.
  // SIGKILL acceptance alone is not release. Wait on that exact native lock,
  // without a timer or repeated nonblocking reopen, before business recovery.
  const int business =
      ::open((directory.path / "audit").c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  require(business >= 0 && ::flock(business, LOCK_EX) == 0,
          "actual inherited writer lock released after alias termination");
  require(::close(business) == 0, "release fixture's native writer lock before reopen");
  require(::kill(-custody_ids[2], SIGKILL) == 0,
          "fixture releases actual external command process group after observing "
          "fatal owner");
  auto reopened = unwrap(
      RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                              NativeJournalDirectory::open(directory.path.string()))),
                          "audit", semantic_header(), {32 * 1024 * 1024, 10000}));
  unwrap(reopened->confirm_recovery());
  bool unknown = false;
  try {
    recover_coding_session(*reopened);
  } catch (const Error &error) {
    unknown = error.code == ErrorCode::external_unknown;
  }
  require(unknown && unwrap(reopened->unresolved_attempts()).size() == 1 &&
              reopened->state() != JournalWriterState::live,
          "business reopen preserves actual opened exec and refuses uncertain custody "
          "without replay");
  // The independent recorder preserves originals after owner death. No fixture
  // recorder release substitutes for production wake success or orderly custody.
  while (true) {
    try {
      const auto all = records(directory.path, false);
      bool found = false;
      for (const auto &[key, event] : all) {
        (void)key;
        found |= event.site == 2057;
      }
      if (found) {
        verify(all, 1, true);
        break;
      }
    } catch (const Error &) {
    }
    std::this_thread::yield();
  }
}

} // namespace
void *operator new(std::size_t size) {
  if (observe_allocations)
    ++observed_allocations;
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
extern "C" int poll(pollfd *fds, nfds_t count, int timeout) {
  if (timeout == -1)
    for (nfds_t index = 0; index < count; ++index)
      if (fds[index].fd == observed_cancel_fd.load())
        parked.store(true, std::memory_order_release);
  return native_poll(fds, count, timeout);
}
extern "C" int shutdown(int fd, int how) {
  if (fd == gated_shutdown_fd.load(std::memory_order_acquire)) {
    shutdown_entered.store(true, std::memory_order_release);
    until(shutdown_release);
  }
  const int result = native_shutdown(fd, how);
  const int error = errno;
  shutdown_calls.fetch_add(1, std::memory_order_release);
  errno = error;
  return result;
}
extern "C" int fcntl(int fd, int command, ...) {
  int argument = 0;
  const bool has_argument = command != F_GETFL && command != F_GETFD &&
                            command != F_GETOWN
#if defined(__APPLE__)
                            && command != F_FULLFSYNC
#endif
      ;
  if (command == F_GETLK || command == F_SETLK || command == F_SETLKW
#if defined(__APPLE__)
      || command == F_GETPATH
#endif
  ) {
    va_list arguments;
    va_start(arguments, command);
    auto *pointer = va_arg(arguments, void *);
    va_end(arguments);
    return native_fcntl(fd, command, pointer);
  }
  if (has_argument) {
    va_list arguments;
    va_start(arguments, command);
    argument = va_arg(arguments, int);
    va_end(arguments);
  }
  if (command == F_GETFL && break_getfl.exchange(false))
    (void)::close(fd);
  return has_argument ? native_fcntl(fd, command, argument) : native_fcntl(fd, command);
}
extern "C" int dup2(int source, int target) {
  if (break_dup2.exchange(false))
    (void)::close(source);
  const int result = native_dup2(source, target);
  const int error = errno;
  if (result >= 0 && target == gated_dup2_target.load(std::memory_order_acquire)) {
    dup2_entered.store(true, std::memory_order_release);
    until(dup2_release);
  }
  errno = error;
  return result;
}
int main(int argc, char **argv) {
  native_write = reinterpret_cast<NativeWrite>(::dlsym(RTLD_NEXT, "write"));
  native_poll = reinterpret_cast<NativePoll>(::dlsym(RTLD_NEXT, "poll"));
  native_shutdown = reinterpret_cast<NativeShutdown>(::dlsym(RTLD_NEXT, "shutdown"));
  native_fcntl = reinterpret_cast<NativeFcntl>(::dlsym(RTLD_NEXT, "fcntl"));
  native_dup2 = reinterpret_cast<NativeDup2>(::dlsym(RTLD_NEXT, "dup2"));
  if (!native_write || !native_poll || !native_shutdown || !native_fcntl ||
      !native_dup2)
    return 2;
  try {
    std::cerr << "native wake ABI sigaction=" << sizeof(struct sigaction)
              << " sigset=" << sizeof(sigset_t)
              << " owner action bank=" << RawCommandInterrupt::action_credit
              << " mask bank=" << RawCommandInterrupt::mask_credit << '\n';
    {
      parked_alias_socket();
      shutdown_calls.store(0);
      reset_and_signal();
      signal_during_rearm();
      refused_reset_and_predispatch();
      native_lifecycle_faults();
      native_notice_failure();
      require(argc == 2, "actual command fixture path provided");
      fatal_native_failure(argv[1]);
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
    return 1;
  }
}
