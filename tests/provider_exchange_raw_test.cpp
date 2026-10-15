#include "../src/native_process.hpp"
#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/provider_original.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/tools.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <new>
#include <stdexcept>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
#endif

using namespace blackbird;
namespace {
std::atomic<int> watched_cancel{-1};
std::atomic<bool> actual_space_wait{false};
struct ClockFact {
  clockid_t method;
  int result, error;
  timespec value;
};
thread_local bool track_clocks = false;
thread_local std::array<ClockFact, 4096> clock_facts{};
thread_local std::size_t clock_count = 0;
thread_local bool clock_overflow = false;
struct LiveAllocation {
  void *pointer;
  std::size_t size;
};
thread_local std::array<LiveAllocation, 4096> wire_allocations{};
thread_local std::string_view wire_literal;
thread_local const char *existing_wire = nullptr;
thread_local bool watch_wire = false, wire_cut = false, wire_live = false;
thread_local std::size_t refused_wire_allocation = 0;
bool has_wire(const void *pointer, std::size_t size) noexcept {
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
  if (pointer &&
      __asan_region_is_poisoned(const_cast<void *>(pointer), wire_literal.size()))
    return false;
#endif
#endif
  return pointer && size >= wire_literal.size() &&
         std::memcmp(pointer, wire_literal.data(), wire_literal.size()) == 0;
}
} // namespace
void *operator new(std::size_t size) {
  const int saved = errno;
  if (watch_wire) {
    bool acquired = existing_wire && has_wire(existing_wire, wire_literal.size());
    for (const auto &allocation : wire_allocations)
      acquired = acquired || has_wire(allocation.pointer, allocation.size);
    if (acquired) {
      watch_wire = false;
      wire_cut = wire_live = true;
      refused_wire_allocation = size;
      errno = saved;
      throw std::bad_alloc{};
    }
  }
  const auto extent = size == 0 ? 1 : size;
  void *pointer = std::malloc(extent);
  if (!pointer)
    throw std::bad_alloc{};
  if (watch_wire) {
    // This direct fault fixture initializes watched extents so inspecting live
    // encoder storage never reads uninitialized bytes. No production allocator.
    std::memset(pointer, 0, extent);
    const auto slot = std::find_if(wire_allocations.begin(), wire_allocations.end(),
                                   [](const auto &value) { return !value.pointer; });
    if (slot == wire_allocations.end())
      std::abort();
    *slot = {pointer, extent};
  }
  errno = saved;
  return pointer;
}
void operator delete(void *pointer) noexcept {
  if (watch_wire)
    for (auto &allocation : wire_allocations)
      if (allocation.pointer == pointer)
        allocation = {};
  std::free(pointer);
}
void operator delete(void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept {
  ::operator delete[](pointer);
}
extern "C" int clock_gettime(clockid_t method, timespec *value)
#if defined(__linux__)
    noexcept
#endif
{
  using Native = int (*)(clockid_t, timespec *);
  static const auto native =
      reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "clock_gettime"));
  if (!native)
    std::abort();
  const int result = native(method, value);
  const int error = errno;
  if (track_clocks) {
    if (clock_count == clock_facts.size())
      clock_overflow = true;
    else
      clock_facts[clock_count++] = {method, result, error,
                                    result == 0 ? *value : timespec{}};
  }
  errno = error;
  return result;
}
// Observe a real STREAM endpoint space wait, never replace its native return.
extern "C" int poll(pollfd *fds, nfds_t count, int timeout) {
  using Native = int (*)(pollfd *, nfds_t, int);
  static const auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "poll"));
  if (!native)
    std::abort();
  bool cancellation = false, stream = false;
  for (nfds_t i = 0; i < count; ++i) {
    cancellation = cancellation || fds[i].fd == watched_cancel.load();
    int type = 0;
    socklen_t length = sizeof(type);
    if (fds[i].events == POLLIN &&
        ::getsockopt(fds[i].fd, SOL_SOCKET, SO_TYPE, &type, &length) == 0 &&
        type == SOCK_STREAM)
      stream = true;
  }
  if (cancellation && stream && timeout != 0)
    actual_space_wait.store(true);
  return native(fds, count, timeout);
}
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error{message};
}
template <class T> T read_native(ByteView &bytes) {
  check(bytes.size() >= sizeof(T), "short native field");
  T result{};
  std::memcpy(&result, bytes.data(), sizeof(result));
  bytes = bytes.subspan(sizeof(result));
  return result;
}
RawEventRef read_ref(ByteView &bytes) {
  RawEventRef result;
  check(bytes.size() >= result.lane.size(), "short actual lane");
  std::copy_n(bytes.begin(), result.lane.size(), result.lane.begin());
  bytes = bytes.subspan(result.lane.size());
  result.ordinal = read_native<std::uint64_t>(bytes);
  return result;
}
bool same(RawEventRef a, RawEventRef b) {
  return a.lane == b.lane && a.ordinal == b.ordinal;
}
std::string_view take_text(ByteView &bytes, std::size_t length) {
  check(bytes.size() >= length, "short original body");
  const std::string_view value{reinterpret_cast<const char *>(bytes.data()), length};
  bytes = bytes.subspan(length);
  return value;
}
template <class T> T id(unsigned char first) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{first};
  return unwrap(T::from_bytes(bytes));
}
struct Directory {
  std::filesystem::path root;
  Directory() {
    std::array<char, 64> name{};
    constexpr std::string_view pattern = "/tmp/blackbird-exchange-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), name.begin());
    check(::mkdtemp(name.data()) != nullptr, "fixture directory create");
    root = std::filesystem::canonical(name.data());
  }
  ~Directory() { std::filesystem::remove_all(root); }
  Directory(const Directory &) = delete;
  Directory &operator=(const Directory &) = delete;
  std::filesystem::path child(std::string_view name) const {
    const auto result = root / name;
    std::filesystem::create_directory(result);
    check(::chmod(result.c_str(), 0700) == 0, "fixture directory mode");
    return result;
  }
};
struct Record {
  RawOrigin origin;
  std::uint32_t site;
  std::uint64_t ordinal;
  std::vector<std::byte> body;
  RawEventRef reference() const { return {origin.lane, ordinal}; }
};
std::vector<Record> records(const std::filesystem::path &session) {
  const auto runs = unwrap(discover_raw_runs(session.string()));
  if (runs.size() != 1)
    throw std::runtime_error{"real raw run count"};
  const auto snapshot = runs.front().snapshot;
  if (!snapshot || !snapshot->closed)
    throw std::runtime_error{"real raw run not closed"};
  auto reader = unwrap(RawRunReader::open((session / "raw" / runs[0].name).string()));
  std::vector<std::byte> storage(2 * 1024 * 1024);
  std::vector<Record> result;
  while (const auto item = unwrap(reader->read(storage))) {
    ByteView bytes{item->raw};
    const auto length = read_native<std::uint32_t>(bytes);
    const auto site = read_native<std::uint32_t>(bytes);
    const auto ordinal = read_native<std::uint64_t>(bytes);
    check(length == item->raw.size(), "original envelope length changed");
    result.push_back({item->origin, site, ordinal, {bytes.begin(), bytes.end()}});
  }
  std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
    return a.origin.lane < b.origin.lane ||
           (a.origin.lane == b.origin.lane && a.ordinal < b.ordinal);
  });
  for (const auto &record : result)
    check(record.site != 8000 && record.site != 8001,
          "original publication unexpectedly fenced");
  return result;
}
const Record &only(const std::vector<Record> &all, std::uint32_t site,
                   std::optional<RawEventRef> parent = std::nullopt) {
  const Record *found = nullptr;
  for (const auto &record : all) {
    if (record.site != site)
      continue;
    if (parent) {
      ByteView body{record.body};
      if (!same(read_ref(body), *parent))
        continue;
    }
    check(found == nullptr, "duplicate native source result");
    found = &record;
  }
  check(found != nullptr, "missing actual native source result");
  return *found;
}
struct Capture final : ProviderOriginalCapture {
  struct Node {
    std::optional<ProviderOwnedOriginal> original;
  };
  struct Ticket final : ProviderOriginalTicket {
    Capture &owner;
    std::unique_ptr<Node> node = std::make_unique<Node>();
    std::shared_ptr<ProviderOriginalStorage> storage =
        std::make_shared<ProviderOriginalStorage>();
    explicit Ticket(Capture &destination) : owner(destination) {}
    std::shared_ptr<ProviderOriginalStorage> take_storage() noexcept override {
      return std::move(storage);
    }
    ProviderCaptureDisposition
    complete(ProviderOwnedOriginal &&original) noexcept override {
      node->original.emplace(std::move(original));
      owner.node = std::move(node);
      ++owner.transfers;
      return owner.disposition;
    }
  };
  std::unique_ptr<Node> node;
  unsigned preparations = 0, transfers = 0;
  ProviderCaptureDisposition disposition{ProviderCaptureState::accepted, std::nullopt};
  std::optional<Error> refusal;
  Result<std::unique_ptr<ProviderOriginalTicket>>
  prepare(ProviderOriginalRequest request) override {
    ++preparations;
    check(request.label == "raw", "source invented a business label");
    check(request.maximum_bytes == 16 * 1024 * 1024 || request.maximum_bytes == 8192,
          "unbounded acquisition requested");
    if (refusal)
      return Result<std::unique_ptr<ProviderOriginalTicket>>::failure(*refusal);
    return Result<std::unique_ptr<ProviderOriginalTicket>>::success(
        std::make_unique<Ticket>(*this));
  }
};
const std::string original{"B\0Y\xff\n", 5};
ProviderHttpRequest request() {
  return {"https://example.com/v1/responses",
          "POST",
          "{\"prompt\":\"fixture\"}",
          {{"Authorization", "Bearer private-wire-sentinel"}},
          30};
}
enum class CallbackCase {
  exact,
  partial_exact,
  partial_error,
  partial_unknown,
  uncommitted,
  before_prepare,
  zero,
  double_commit,
  observer_error,
  observer_unknown,
  overflow,
  receiver_refusal
};
void callback_case(const Directory &directory, CallbackCase mode, unsigned sequence) {
  const auto session = directory.child("callback-" + std::to_string(sequence));
  Capture capture;
  if (mode == CallbackCase::overflow)
    capture.disposition.state = ProviderCaptureState::overflow;
  if (mode == CallbackCase::receiver_refusal)
    capture.refusal = Error{ErrorCode::capacity, 19};
  RawProviderReceipt receipt;
  const RawAdapterContext context{nullptr, std::nullopt, &capture, &receipt};
  ProviderAuthConfig config;
  unsigned callbacks = 0, observers = 0;
  config.http = [&](const ProviderHttpRequest &r,
                    ProviderHttpResponseLease &response) -> int {
    ++callbacks;
    check(r.method == request().method && r.url == request().url &&
              r.body == request().body,
          "real outgoing business request changed");
    if (mode == CallbackCase::before_prepare)
      throw Error{ErrorCode::io, ECONNRESET};
    const std::size_t extent =
        mode == CallbackCase::zero
            ? 0
            : (mode == CallbackCase::exact || mode == CallbackCase::observer_error ||
                       mode == CallbackCase::observer_unknown ||
                       mode == CallbackCase::overflow
                   ? 5
                   : 8);
    auto bytes = response.prepare(extent);
    if (extent)
      std::copy(original.begin(), original.begin() + (extent == 5 ? 5 : 3),
                bytes.begin());
    if (mode == CallbackCase::partial_error)
      throw Error{ErrorCode::io, EPIPE};
    if (mode == CallbackCase::partial_unknown)
      throw std::runtime_error{"partial callback failure"};
    if (mode == CallbackCase::uncommitted)
      return 307;
    response.commit(extent == 8 ? 3 : extent);
    if (mode == CallbackCase::double_commit)
      response.commit(1);
    return 200;
  };
  std::optional<Error> error;
  bool unknown = false;
  {
    auto runtime = unwrap(RawRuntime::open(session.string(), id<EnvironmentId>(33)));
    auto handle = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{handle};
      try {
        const auto result = provider_http(
            request(), config,
            [&](std::string_view body) {
              ++observers;
              check(body == (mode == CallbackCase::zero ? std::string{}
                             : mode == CallbackCase::partial_exact
                                 ? original.substr(0, 3)
                                 : original),
                    "observer did not see exact observed prefix");
              if (mode == CallbackCase::observer_error)
                throw Error{ErrorCode::corrupt, 93};
              if (mode == CallbackCase::observer_unknown)
                throw std::runtime_error{"observer failure"};
            },
            nullptr, true, nullptr, &context);
        check(result.status == 200 &&
                  result.body == (mode == CallbackCase::zero ? std::string{}
                                  : mode == CallbackCase::partial_exact
                                      ? original.substr(0, 3)
                                      : original),
              "callback native status/body changed");
      } catch (const Error &e) {
        error = e;
      } catch (const std::runtime_error &) {
        unknown = true;
      }
    }
    handle.reset();
  }
  const bool successful = mode == CallbackCase::exact ||
                          mode == CallbackCase::partial_exact ||
                          mode == CallbackCase::zero;
  check(successful == (!error && !unknown), "wrong callback owner outcome");
  if (mode == CallbackCase::partial_error)
    check(error == Error{ErrorCode::io, EPIPE}, "partial native Error changed");
  if (mode == CallbackCase::double_commit)
    check(error && error->code == ErrorCode::invalid_range, "second commit accepted");
  if (mode == CallbackCase::overflow)
    check(error && error->code == ErrorCode::capacity && capture.transfers == 1,
          "overflow preceded source transfer");
  if (mode == CallbackCase::receiver_refusal)
    check(callbacks == 0 && error == Error{ErrorCode::capacity, 19},
          "receiver refusal acquired source");
  const auto all = records(session);
  const auto &application = only(all, 1300);
  const auto &wire = only(all, 1301, application.reference());
  ByteView outgoing{wire.body};
  check(same(read_ref(outgoing), application.reference()),
        "wire request linkage changed");
  auto count = read_native<std::size_t>(outgoing);
  check(take_text(outgoing, count) == "POST", "wire method changed");
  count = read_native<std::size_t>(outgoing);
  check(take_text(outgoing, count) == request().url, "wire URL changed");
  count = read_native<std::size_t>(outgoing);
  check(take_text(outgoing, count) == request().body && outgoing.empty(),
        "wire original changed");
  for (const auto &record : all) {
    const std::string_view body{reinterpret_cast<const char *>(record.body.data()),
                                record.body.size()};
    check(body.find("private-wire-sentinel") == std::string_view::npos,
          "credential-bearing config entered raw custody");
  }
  if (mode == CallbackCase::receiver_refusal) {
    check(std::none_of(all.begin(), all.end(),
                       [](const auto &r) {
                         return r.site == 1310 || r.site == 1311 || r.site == 1312;
                       }),
          "refusal fabricated acquired callback");
    return;
  }
  const auto &invoke = only(all, 1310, application.reference());
  ByteView begin{invoke.body};
  check(same(read_ref(begin), application.reference()) &&
            read_native<std::size_t>(begin) == 16 * 1024 * 1024 && begin.empty(),
        "injected begin bound/ref changed");
  check(receipt.request && receipt.response &&
            same(*receipt.request, application.reference()) &&
            same(*receipt.response, invoke.reference()),
        "receipt invented request/response identity");
  const auto &returned = only(all, 1311, invoke.reference());
  ByteView end{returned.body};
  check(same(read_ref(end), invoke.reference()), "callback return paired incorrectly");
  const auto branch = read_native<std::uint8_t>(end);
  const bool status = read_native<bool>(end);
  const bool callback_threw =
      mode == CallbackCase::partial_error || mode == CallbackCase::partial_unknown ||
      mode == CallbackCase::before_prepare || mode == CallbackCase::double_commit;
  check(status == !callback_threw, "fabricated callback status");
  if (status)
    check(read_native<int>(end) == (mode == CallbackCase::uncommitted ? 307 : 200) &&
              branch == 1,
          "native callback status/return changed");
  else if (mode == CallbackCase::partial_unknown)
    check(branch == 5, "unknown exception unobserved");
  else {
    check(branch == 2 && error && read_native<ErrorCode>(end) == error->code &&
              read_native<std::int64_t>(end) == error->detail,
          "original callback Error changed");
  }
  check(end.empty(), "extra fabricated callback return fields");
  if (mode == CallbackCase::before_prepare) {
    check(capture.transfers == 0 && !capture.node &&
              std::none_of(all.begin(), all.end(),
                           [](const auto &r) { return r.site == 1312; }),
          "pre-prepare throw fabricated original");
    return;
  }
  const auto &body = only(all, 1312, application.reference());
  ByteView acquired{body.body};
  check(same(read_ref(acquired), application.reference()),
        "original source linkage changed");
  const bool unresolved = mode == CallbackCase::partial_error ||
                          mode == CallbackCase::partial_unknown ||
                          mode == CallbackCase::uncommitted;
  check(read_native<std::uint8_t>(acquired) == (unresolved ? 2 : 1),
        "unknown-used qualification changed");
  const auto extent = read_native<std::size_t>(acquired);
  check(read_native<bool>(acquired) == !unresolved, "invented observed-used length");
  const std::size_t expected_used = mode == CallbackCase::zero ? 0
                                    : extent == 8              ? 3
                                                               : 5;
  if (!unresolved)
    check(read_native<std::size_t>(acquired) == expected_used,
          "exact used count changed");
  std::string expected = original.substr(0, expected_used);
  if (unresolved)
    expected.resize(extent, '\0');
  check(take_text(acquired, expected.size()) == expected && acquired.empty(),
        "acquired original bytes lost/normalized");
  if (capture.transfers != 1 || !capture.node || !capture.node->original)
    throw std::runtime_error{"acquired ownership not transferred once"};
  const auto &owned = *capture.node->original;
  check(owned.bytes() == expected &&
            owned.observed_used() ==
                (unresolved ? std::optional<std::size_t>{} : expected_used) &&
            owned.reference() && same(owned.reference()->event, body.reference()) &&
            owned.reference()->partition == RawPartition::data &&
            owned.reference()->record_offset == (unresolved ? 50 : 58) &&
            owned.reference()->bytes == expected.size(),
        "shared source reference/range/bytes changed");
  check(observers == (successful || mode == CallbackCase::observer_error ||
                              mode == CallbackCase::observer_unknown
                          ? 1U
                          : 0U),
        "observer ran before original transfer/refusal");
}
void inactive_source() {
  Capture capture;
  ProviderAuthConfig config;
  config.http = [](const ProviderHttpRequest &, ProviderHttpResponseLease &response) {
    auto extent = response.prepare(original.size());
    std::copy(original.begin(), original.end(), extent.begin());
    response.commit(original.size());
    return 200;
  };
  const RawAdapterContext context{nullptr, std::nullopt, &capture, nullptr};
  check(provider_http(request(), config, {}, nullptr, true, nullptr, &context).body ==
                original &&
            capture.node && capture.node->original &&
            !capture.node->original->reference() &&
            capture.node->original->bytes() == original,
        "raw-inactive source fabricated a Ref/lost bytes");
}
struct Pipe {
  std::array<int, 2> fds{-1, -1};
  Pipe() { check(::pipe(fds.data()) == 0, "fixture pipe create"); }
  ~Pipe() {
    for (int fd : fds)
      (void)::close(fd);
  }
  Pipe(const Pipe &) = delete;
  Pipe &operator=(const Pipe &) = delete;
  void notify() const {
    const char byte = 1;
    check(::write(fds[1], &byte, 1) == 1, "fixture pipe write");
  }
};
std::string lane_name(IdentityBytes epoch) {
  std::string name = "/bb";
  constexpr std::string_view hex = "0123456789abcdef";
  for (std::size_t i = 0; i < 12; ++i) {
    const auto byte = std::to_integer<unsigned>(epoch[i]);
    name += hex[byte >> 4];
    name += hex[byte & 15U];
  }
  return name;
}
void bounded_admission(const Directory &directory, bool expire) {
  const auto session = directory.child(expire ? "source-expiry" : "source-cancel");
  constexpr RawCapacities capacities{16 * 1024 * 1024 + 4096, 128 * 1024, 4096};
  Pipe cancellation;
  Capture capture;
  ProviderAuthConfig config;
  unsigned calls = 0;
  config.http = [&](const ProviderHttpRequest &, ProviderHttpResponseLease &) {
    ++calls;
    return 200;
  };
  RawProviderReceipt receipt;
  const RawAdapterContext context{nullptr, std::nullopt, &capture, &receipt};
  {
    auto runtime = unwrap(
        RawRuntime::open(session.string(), id<EnvironmentId>(44), {capacities, 4096}));
    auto handle = unwrap(runtime->prepare_thread());
    const auto status = unwrap(runtime->status());
    check(::kill(status.recorder, SIGSTOP) == 0, "stop actual recorder");
    struct Resume {
      pid_t pid;
      ~Resume() { (void)::kill(pid, SIGCONT); }
    } resume{status.recorder};
    {
      RawThreadBinding binding{handle};
      std::string filler(
          static_cast<std::size_t>(capacities[0]) - 1024 - raw_envelope_size, 'F');
      const std::array<ByteView, 1> parts{detail::raw_string_field(filler)};
      unwrap(raw_emit(1313, parts));
      auto channel = unwrap(
          RawChannel::open(lane_name(raw_lane_epoch()), raw_lane_epoch(), capacities));
      const auto before = unwrap(channel.cursors(RawPartition::data));
      check(before.published > before.acknowledged, "full source has no pending bytes");
      auto control = raw_wait_control();
      control.cancellation_fds[0] = cancellation.fds[0];
      if (expire)
        control.deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds{30};
      watched_cancel.store(cancellation.fds[0]);
      actual_space_wait.store(false);
      std::thread interrupt;
      if (!expire)
        interrupt = std::thread{[&] {
          while (!actual_space_wait.load())
            std::this_thread::yield();
          cancellation.notify();
        }};
      bool refused = false;
      {
        RawWaitScope scope{control};
        try {
          (void)provider_http(request(), config, {}, nullptr, true, nullptr, &context);
        } catch (const Error &error) {
          refused = error == (expire ? Error{ErrorCode::provider_transport, 28}
                                     : Error{ErrorCode::interrupted, ECANCELED});
        }
      }
      if (interrupt.joinable())
        interrupt.join();
      watched_cancel.store(-1);
      check(refused && actual_space_wait.load() && calls == 0 &&
                capture.preparations == 1 && capture.transfers == 0,
            "source admission failed to cancel/expire before acquisition");
      const auto after = unwrap(channel.cursors(RawPartition::data));
      check(after.acknowledged == before.acknowledged &&
                after.published > before.published &&
                unwrap(channel.cursors(RawPartition::control)).published > 0 &&
                unwrap(channel.cursors(RawPartition::failure)).published == 0,
            "held source terminals or previous request original lost");
    }
    check(::kill(status.recorder, SIGCONT) == 0, "resume source recorder");
    handle.reset();
  }
  // Filler exceeds ordinary reader storage. Read the selected original directly
  // with its actual bounded extent; no fabricated response after refused ingest.
  const auto runs = unwrap(discover_raw_runs(session.string()));
  auto reader =
      unwrap(RawRunReader::open((session / "raw" / runs.at(0).name).string()));
  std::vector<std::byte> buffer(static_cast<std::size_t>(capacities[0]));
  unsigned begins = 0, terminals = 0, wire = 0;
  while (const auto item = unwrap(reader->read(buffer))) {
    ByteView bytes{item->raw};
    (void)read_native<std::uint32_t>(bytes);
    const auto site = read_native<std::uint32_t>(bytes);
    (void)read_native<std::uint64_t>(bytes);
    check(site != 1310 && site != 1311 && site != 1312 && site != 8001,
          "refused admission fabricated response or fenced held cleanup");
    begins += site == 1320 ? 1U : 0U;
    terminals += site == 1321 ? 1U : 0U;
    wire += site == 1301 ? 1U : 0U;
  }
  check(begins == 1 && terminals == 1 && wire == 1,
        "refused source failed actual owner closure/original");
}
template <class Predicate> void until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
  while (!predicate()) {
    check(std::chrono::steady_clock::now() < deadline, "native fixture cut missing");
    std::this_thread::yield();
  }
}
void published_producer_death(const Directory &directory) {
  const auto session = directory.child("source-death");
  constexpr RawRuntimeOptions options{{32 * 1024 * 1024, 128 * 1024, 4096}, 4096};
  Pipe reports, commands;
  const auto producer = ::fork();
  check(producer >= 0, "fork actual injected producer");
  if (producer == 0) {
    (void)::close(reports.fds[0]);
    (void)::close(commands.fds[1]);
    try {
      auto runtime =
          unwrap(RawRuntime::open(session.string(), id<EnvironmentId>(45), options));
      RawThreadBinding binding{unwrap(runtime->prepare_thread())};
      const auto status = unwrap(runtime->status());
      const auto epoch = raw_lane_epoch();
      check(::write(reports.fds[1], &status, sizeof(status)) == sizeof(status) &&
                ::write(reports.fds[1], epoch.data(), epoch.size()) == 16,
            "report actual source holders/epoch");
      char command = 0;
      check(::read(commands.fds[0], &command, 1) == 1, "wait native holders stop");
      ProviderAuthConfig config;
      config.http = [&](const ProviderHttpRequest &,
                        ProviderHttpResponseLease &response) {
        auto extent = response.prepare(original.size());
        std::copy(original.begin(), original.end(), extent.begin());
        response.commit(original.size());
        reports.notify(); // Actual raw publication precedes this native cut.
        check(::read(commands.fds[0], &command, 1) == 1, "hold callback before return");
        return 200;
      };
      (void)provider_http(request(), config, {}, nullptr, true);
    } catch (...) {
      ::_exit(98);
    }
    ::_exit(99);
  }
  (void)::close(reports.fds[1]);
  reports.fds[1] = -1;
  (void)::close(commands.fds[0]);
  commands.fds[0] = -1;
  RawRuntimeStatus holders{};
  IdentityBytes epoch{};
  check(::read(reports.fds[0], &holders, sizeof(holders)) == sizeof(holders) &&
            ::read(reports.fds[0], epoch.data(), epoch.size()) == 16,
        "read actual source holders/epoch");
  check(::kill(holders.supervisor, SIGSTOP) == 0 &&
            ::kill(holders.recorder, SIGSTOP) == 0,
        "stop actual source holders");
  commands.notify();
  pollfd notice{reports.fds[0], POLLIN, 0};
  check(::poll(&notice, 1, 8000) == 1, "source publication cut absent");
  char published = 0;
  check(::read(reports.fds[0], &published, 1) == 1, "read source publication cut");
  std::optional<RawEventRef> published_body, application;
  {
    auto channel =
        unwrap(RawChannel::open(lane_name(epoch), epoch, options.producer_capacities));
    const auto cut = unwrap(channel.cursors(RawPartition::data));
    check(cut.published > 0 && cut.acknowledged == 0,
          "source death cut is not actual unacknowledged publication");
    auto saved = unwrap(channel.read(RawPartition::data, 0, 65536));
    ByteView bytes{saved.bytes};
    while (!bytes.empty()) {
      const auto length = read_native<std::uint32_t>(bytes);
      const auto site = read_native<std::uint32_t>(bytes);
      const auto ordinal = read_native<std::uint64_t>(bytes);
      check(length >= 16 && length - 16 <= bytes.size(), "death cut native envelope");
      auto body = bytes.first(length - 16);
      bytes = bytes.subspan(length - 16);
      if (site == 1300)
        application = RawEventRef{epoch, ordinal};
      if (site != 1312)
        continue;
      check(!published_body && application && same(read_ref(body), *application) &&
                read_native<std::uint8_t>(body) == 1 &&
                read_native<std::size_t>(body) == original.size() &&
                read_native<bool>(body) &&
                read_native<std::size_t>(body) == original.size() &&
                take_text(body, original.size()) == original && body.empty(),
            "actual original absent/changed at unacknowledged cut");
      published_body = RawEventRef{epoch, ordinal};
    }
  }
  if (!published_body || !application)
    throw std::runtime_error{"did not publish source before killing holders"};
  check(::kill(producer, SIGKILL) == 0 && ::kill(holders.supervisor, SIGKILL) == 0 &&
            ::kill(holders.recorder, SIGKILL) == 0,
        "kill all actual source holders");
  int status = 0;
  check(::waitpid(producer, &status, 0) == producer && WIFSIGNALED(status) &&
            WTERMSIG(status) == SIGKILL,
        "producer survived selected native cut");
  std::filesystem::path old_run;
  for (const auto &entry : std::filesystem::directory_iterator{session / "raw"})
    if (entry.is_directory()) {
      check(old_run.empty(), "duplicate original source run");
      old_run = entry.path();
    }
  check(!old_run.empty(), "original source run missing");
  const int lease = ::open((old_run / "lanes").c_str(), O_RDWR | O_CLOEXEC);
  check(lease >= 0, "open native source catalogue lease");
  until([&] { return ::flock(lease, LOCK_EX | LOCK_NB) == 0; });
  check(::close(lease) == 0, "release source catalogue lease");
  {
    auto recovered =
        unwrap(RawRuntime::open(session.string(), id<EnvironmentId>(45), options));
  }
  auto reader = unwrap(RawRunReader::open(old_run.string()));
  check(reader->snapshot().closed, "dead source run not recovered/closed");
  std::vector<std::byte> buffer(65536);
  unsigned bodies = 0, invokes = 0, returns = 0, terminals = 0;
  while (const auto item = unwrap(reader->read(buffer))) {
    ByteView bytes{item->raw};
    (void)read_native<std::uint32_t>(bytes);
    const auto site = read_native<std::uint32_t>(bytes);
    const auto ordinal = read_native<std::uint64_t>(bytes);
    invokes += site == 1310 ? 1U : 0U;
    returns += site == 1311 ? 1U : 0U;
    terminals += site == 1321 || site == 1303 ? 1U : 0U;
    if (site != 1312)
      continue;
    ++bodies;
    check(same({item->origin.lane, ordinal}, *published_body) &&
              same(read_ref(bytes), *application) &&
              read_native<std::uint8_t>(bytes) == 1 &&
              read_native<std::size_t>(bytes) == 5 && read_native<bool>(bytes) &&
              read_native<std::size_t>(bytes) == 5 && take_text(bytes, 5) == original &&
              bytes.empty(),
          "recovery changed source Ref/native fields/original bytes");
  }
  check(bodies == 1 && invokes == 1 && returns == 0 && terminals == 0,
        "death recovery fabricated callback/owner completion");
}
void native_transfer(const Directory &directory, const std::string &executable) {
  const auto session = directory.child("native-source");
  Capture capture;
  std::optional<ProviderOriginalReference> source;
  {
    auto runtime = unwrap(RawRuntime::open(session.string(), id<EnvironmentId>(46)));
    auto handle = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{handle};
      detail::Child child;
      child.raw_capture({});
      child.original_capture = &capture;
      child.start({executable, "--native-original"});
      child.close_input();
      int status = -1;
      check(child.collect(detail::Clock::now() + std::chrono::seconds{5}, 64,
                          &status) == original &&
                status == 7,
            "native business read/status changed");
      if (capture.transfers != 1 || !capture.node || !capture.node->original)
        throw std::runtime_error{"native original not transferred"};
      check(capture.node->original->bytes() == original &&
                capture.node->original->observed_used() == original.size() &&
                capture.node->original->reference(),
            "native original bytes/count/reference changed");
      source = capture.node->original->reference();
    }
    handle.reset();
  }
  if (!source)
    throw std::runtime_error{"native source reference absent"};
  const auto all = records(session);
  check(std::none_of(all.begin(), all.end(),
                     [](const auto &r) { return r.site == 1312; }),
        "native source duplicated its original into callback publication");
  const auto found = std::find_if(all.begin(), all.end(), [&](const auto &record) {
    return same(record.reference(), source->event);
  });
  check(found != all.end() && found->site == 1404 &&
            source->partition == RawPartition::data && source->record_offset == 61 &&
            source->bytes == original.size(),
        "native source reference is not actual 1404 range");
  ByteView bytes{found->body};
  const auto child = read_ref(bytes);
  const auto &begin = only(all, 1400);
  check(same(child, begin.reference()) && read_native<std::uint8_t>(bytes) == 1 &&
            read_native<std::size_t>(bytes) == 8192 && read_native<ssize_t>(bytes) == 5,
        "native original source/channel/extent/result changed");
  (void)read_native<int>(bytes); // Meaningful on native failure; copied unchanged.
  check(take_text(bytes, 5) == original && bytes.empty(),
        "native original bytes changed");
}
void decoder_and_codec(const Directory &directory) {
  const auto session = directory.child("decoder-codec");
  std::optional<ProviderOriginalReference> codec;
  {
    auto runtime = unwrap(RawRuntime::open(session.string(), id<EnvironmentId>(47)));
    auto handle = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{handle};
      const auto value = Value{"literal"};
      auto encoded = provider_encode_original(
          value, {ProviderOriginalRole::prepared_admission,
                  ProviderOriginalMode::codec_adopt, ValueLimits{}.bytes, "admission"});
      // Independent literal BBM2 string packet: magic, text tag, varint length, bytes.
      const std::array<unsigned char, 13> packet{'B', 'B', 'M', 2,   6,   7,  'l',
                                                 'i', 't', 'e', 'r', 'a', 'l'};
      check(encoded.bytes().size() == packet.size() &&
                std::memcmp(encoded.bytes().data(), packet.data(), packet.size()) ==
                    0 &&
                encoded.reference(),
            "owner admission codec bytes/ref changed");
      codec = encoded.reference();
      ProviderAuthConfig config;
      std::string wire = "data: "
                         "{\"type\":\"response.output_text.delta\",\"item_id\":"
                         "\"m\",\"delta\":\"hi\"}\n\n"
                         "data: "
                         "{\"type\":\"response.completed\",\"response\":{"
                         "\"status\":\"completed\",\"output\":[]}}\n\n";
      config.http = [&](const ProviderHttpRequest &,
                        ProviderHttpResponseLease &response) {
        auto extent = response.prepare(wire.size());
        std::copy(wire.begin(), wire.end(), extent.begin());
        response.commit(wire.size());
        return 200;
      };
      RawProviderReceipt receipt;
      const RawAdapterContext context{nullptr, std::nullopt, nullptr, &receipt};
      auto body =
          provider_http(request(), config, {}, nullptr, true, nullptr, &context).body;
      ResponsePreview preview;
      const auto deltas = preview.feed(body, &context);
      check(deltas.size() == 1 && deltas[0].item_id == "m" && deltas[0].text == "hi",
            "preview parser business result changed");
      check(unwrap(completed_response(body, {}, &context)).find("output"),
            "decoder complete result lost");
      wire = "data: {\"type\":\"response.incomplete\"}\n\n";
      body =
          provider_http(request(), config, {}, nullptr, true, nullptr, &context).body;
      check(!completed_response(body, {}, &context).has_value(),
            "incomplete decoder accepted");
      wire = "data: "
             "{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\","
             "\"output\":[]}}\n\n"
             "data: "
             "{\"type\":\"response.completed\",\"response\":{\"status\":\"completed\","
             "\"output\":[]}}\n\n";
      body =
          provider_http(request(), config, {}, nullptr, true, nullptr, &context).body;
      check(!completed_response(body, {}, &context).has_value(),
            "duplicate decoder accepted");
    }
    handle.reset();
  }
  const auto all = records(session);
  const auto &encoded = only(all, 1322);
  check(codec && same(codec->event, encoded.reference()) &&
            codec->record_offset == 49 && codec->bytes == 13,
        "codec actual source range changed");
  std::vector<std::pair<RawEventRef, std::uint8_t>> begun;
  unsigned malformed = 0, completed = 0, previewed = 0;
  for (const auto &record : all) {
    ByteView bytes{record.body};
    if (record.site == 1320) {
      const auto kind = read_native<std::uint8_t>(bytes);
      begun.emplace_back(record.reference(), kind);
    } else if (record.site == 1321) {
      const auto parent = read_ref(bytes);
      const auto found =
          std::find_if(begun.begin(), begun.end(),
                       [&](const auto &entry) { return same(entry.first, parent); });
      check(found != begun.end(), "semantic decoder terminal lacks actual begin");
      const auto branch = read_native<std::uint8_t>(bytes);
      const auto stage = read_native<std::uint8_t>(bytes);
      check(branch != 6, "observed decoder branch became unresolved destructor");
      if (found->second == 6) {
        completed += branch == 1 && stage == 50 ? 1U : 0U;
        malformed += branch == 2 ? 1U : 0U;
      }
      previewed += found->second == 7 && branch == 1 && stage == 67 ? 1U : 0U;
      begun.erase(found);
    }
  }
  check(begun.empty() && completed == 1 && malformed == 2 && previewed == 1,
        "decoder/preview owner outcomes missing or multiply closed");
}
class RefusingPreparation final : public CodingProvider {
public:
  unsigned ended = 0;
  std::optional<RawEventRef> parent;
  void begin_request_group(const Value &, const RawAdapterContext *context) override {
    check(context && context->parent, "group preparation has no assembly owner");
    parent = context->parent;
    throw Error{ErrorCode::unsupported, 73};
  }
  void end_request_group() noexcept override { ++ended; }
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw std::runtime_error{"refused preparation reached transport"};
  }
};
void preparation_refusal(const Directory &directory) {
  const auto session = directory.child("preparation-refusal");
  const JournalHeader header{id<EnvironmentId>(68),
                             id<AuditStreamId>(69),
                             3,
                             {4 * 1024 * 1024, 16 * 1024 * 1024},
                             std::nullopt};
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                       NativeJournalDirectory::open(session.string()))),
                                   "audit", header, {64 * 1024 * 1024, 10000}));
  RefusingPreparation provider;
  bool refused = false;
  {
    auto runtime = unwrap(RawRuntime::open(session.string(), header.environment));
    auto handle = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{handle};
      AuditLog log{*root};
      ContextStore context{log};
      CodingEngine engine{log, context, provider, "fixture-model"};
      try {
        engine.turn({"", "arco.request()"});
      } catch (const Error &e) {
        refused = e == Error{ErrorCode::unsupported, 73};
      }
    }
    handle.reset();
  }
  check(refused && provider.ended == 1, "preparation refusal cleanup changed");
  const auto all = records(session);
  const auto &assembly = only(all, 1320);
  check(provider.parent && same(*provider.parent, assembly.reference()),
        "preparation linked to a fabricated phase");
  ByteView terminal{only(all, 1321, assembly.reference()).body};
  check(same(read_ref(terminal), assembly.reference()) &&
            read_native<std::uint8_t>(terminal) == 2 &&
            read_native<std::uint8_t>(terminal) == 14 &&
            read_native<ErrorCode>(terminal) == ErrorCode::unsupported &&
            read_native<std::int64_t>(terminal) == 73 && terminal.empty(),
        "preparation lost actual assembly failure");
}
class RetryingProvider final : public CodingProvider {
public:
  unsigned calls = 0;
  Value first;
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw std::runtime_error{"selected operation lost explicit native context"};
  }
  Value respond(const Value &value,
                const std::function<void(std::string_view)> &capture,
                const RawProviderOwner *owner,
                const RawAdapterContext *context) override {
    check(owner && context && context->owner == owner && owner->dispatch.ordinal,
          "request attempt has no actual transport owner");
    if (++calls == 1)
      first = value;
    check(first == value, "retry changed assembled request");
    ProviderAuthConfig config;
    const std::string wire = "data: "
                             "{\"type\":\"response.output_text.delta\",\"item_id\":"
                             "\"m\",\"delta\":\"hi\"}\n\n"
                             "data: "
                             "{\"type\":\"response.completed\",\"response\":{"
                             "\"status\":\"completed\",\"output\":[]}}\n\n";
    config.http = [&](const ProviderHttpRequest &,
                      ProviderHttpResponseLease &response) {
      auto extent = response.prepare(wire.size());
      std::copy(wire.begin(), wire.end(), extent.begin());
      response.commit(wire.size());
      return 200;
    };
    const auto body =
        provider_http(request(), config, capture, owner, true, nullptr, context).body;
    if (calls == 1)
      throw Error{ErrorCode::provider_transport, 56};
    return unwrap(completed_response(body, {}, context));
  }
};
void coding_retry(const Directory &directory) {
  const auto session = directory.child("coding-retry");
  const JournalHeader header{id<EnvironmentId>(48),
                             id<AuditStreamId>(49),
                             3,
                             {4 * 1024 * 1024, 16 * 1024 * 1024},
                             std::nullopt};
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                       NativeJournalDirectory::open(session.string()))),
                                   "audit", header, {64 * 1024 * 1024, 10000}));
  RetryingProvider provider;
  {
    auto runtime = unwrap(RawRuntime::open(session.string(), header.environment));
    auto handle = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{handle};
      AuditLog log{*root};
      ContextStore context{log};
      CodingEngine engine{log, context, provider, "fixture-model"};
      clock_count = 0;
      clock_overflow = false;
      track_clocks = true;
      engine.turn(
          {"retry native fixture",
           R"(arco.request({retry_policy={max_attempts=2,base_ms=1,cap_ms=1}}))"});
      track_clocks = false;
      check(!clock_overflow, "native clock transcript overflow");
      check(provider.calls == 2, "native group did not retry exactly once");
    }
    handle.reset();
  }
  const auto all = records(session);
  std::optional<RawEventRef> group, wait;
  std::vector<RawEventRef> attempts;
  unsigned assembly = 0, decisions = 0, clocks = 0, presented = 0, results = 0;
  std::size_t next_clock = 0;
  for (const auto &record : all) {
    ByteView bytes{record.body};
    check(record.site != 1200 && record.site != 1201 && record.site != 1202,
          "obsolete duplicate provider request/response hook remains");
    if (record.site == 1320) {
      const auto kind = read_native<std::uint8_t>(bytes);
      const bool parent = read_native<bool>(bytes);
      const auto predecessor = parent ? std::optional{read_ref(bytes)} : std::nullopt;
      const bool owner = read_native<bool>(bytes);
      if (kind == 1)
        ++assembly;
      if (kind == 2) {
        check(!group && predecessor, "group parent missing/duplicated");
        group = record.reference();
      }
      if (kind == 3) {
        check(group && predecessor && same(*predecessor, *group) && owner,
              "attempt parent/native identities absent");
        IdentityBytes attempt{}, invocation{}, generation{};
        for (auto *identity : {&attempt, &invocation, &generation}) {
          check(bytes.size() >= identity->size(), "short admitted identity");
          std::copy_n(bytes.begin(), identity->size(), identity->begin());
          bytes = bytes.subspan(identity->size());
        }
        const auto admitted =
            unwrap(root->attempt(unwrap(OperationAttemptId::from_bytes(attempt))));
        check(admitted.admission.invocation.bytes() == invocation && !bytes.size(),
              "attempt metric did not use actual retained native IDs");
        const auto native_invocation =
            unwrap(root->invocation(admitted.admission.invocation));
        check(native_invocation.definition.bytes() == generation,
              "attempt metric generation did not use actual admission");
        attempts.push_back(record.reference());
      }
      if (kind == 4) {
        check(!wait && group && predecessor && same(*predecessor, *group),
              "wait parent missing/duplicated");
        wait = record.reference();
      }
      presented += kind == 8 ? 1U : 0U;
    } else if (record.site == 1325) {
      check(group && same(read_ref(bytes), *group),
            "retry policy not linked to actual group");
      const auto native_group = bytes.first(16);
      check(std::any_of(native_group.begin(), native_group.end(),
                        [](std::byte byte) { return byte != std::byte{0}; }),
            "group surrogate replaced actual issued ID");
      bytes = bytes.subspan(16);
      check(read_native<unsigned>(bytes) == 2 && read_native<unsigned>(bytes) == 1 &&
                read_native<unsigned>(bytes) == 1 && bytes.empty(),
            "validated retry policy changed");
    } else if (record.site == 1326) {
      ++decisions;
      check(group && same(read_ref(bytes), *group), "retry decision wrong group");
      const auto ordinal = read_native<unsigned>(bytes);
      check(ordinal == decisions && attempts.size() >= ordinal &&
                same(read_ref(bytes), attempts[ordinal - 1]) &&
                read_native<unsigned>(bytes) == 1 &&
                read_native<bool>(bytes) == (ordinal == 1) && bytes.empty(),
            "native retry ordinal/prior/delay/eligibility changed");
    } else if (record.site == 1327) {
      ++clocks;
      check(wait && same(read_ref(bytes), *wait), "actual wait clock linkage changed");
      const auto method = read_native<clockid_t>(bytes);
      const auto result = read_native<int>(bytes);
      const auto error = read_native<int>(bytes);
      const auto seconds = read_native<time_t>(bytes);
      const auto ns = read_native<long>(bytes);
      bool found = false;
      for (; next_clock < clock_count; ++next_clock) {
        const auto &actual = clock_facts[next_clock];
        if (actual.method == method && actual.result == result &&
            actual.error == error && actual.value.tv_sec == seconds &&
            actual.value.tv_nsec == ns) {
          ++next_clock;
          found = true;
          break;
        }
      }
      check(found && method == CLOCK_MONOTONIC && result == 0 && bytes.empty(),
            "native clock fields differ from actual acquired return/errno/components");
    } else if (record.site == 1322) {
      (void)read_ref(bytes);
      results += read_native<std::uint8_t>(bytes) == 3 ? 1U : 0U;
    }
  }
  check(assembly == 1 && attempts.size() == 2 && decisions == 2 && clocks == 2 &&
            presented == 2 && results == 2,
        "assembly/retry/presentation/result acquisition missing");
}
void wire_allocation(const Directory &directory, unsigned mode,
                     const std::string &executable) {
  const auto session = directory.child("wire-cut-" + std::to_string(mode));
  const auto home = directory.child("wire-home-" + std::to_string(mode));
  const std::string payload = "post-encoder-original-" + std::string(96, 'x');
  const std::string expected = "{\"payload\":\"" + payload + "\"}";
  const auto value = Value::object({{"payload", Value{payload}}});
  RawProviderReceipt receipt;
  const RawAdapterContext context{nullptr, std::nullopt, nullptr, &receipt};
  ProviderAuthConfig owned;
  owned.directory = home;
  owned.curl_executable = "/bin/false";
  unsigned callbacks = 0;
  owned.http = [&](const ProviderHttpRequest &, ProviderHttpResponseLease &reply) {
    ++callbacks;
    (void)reply.prepare(0);
    reply.commit(0);
    return 200;
  };
  ProviderAuth auth{owned};
  if (mode == 1)
    auth.set_key("openai", "account", "private-key-no-original");
  OpenAiConfig compatibility;
  compatibility.raw_context = &context;
  compatibility.codex_home = home;
  compatibility.codex_executable = executable;
  compatibility.curl_executable = "/bin/false";
  std::optional<OpenAiLogin> login;
  if (mode == 0) {
    write_file(
        (home / "auth.json").string(),
        R"({"auth_mode":"chatgpt","tokens":{"access_token":"fixture-access","account_id":"fixture-account"}})");
    check(::chmod((home / "auth.json").c_str(), 0600) == 0, "wire auth mode");
    login.emplace(unwrap(codex_login(compatibility)));
  }
  ProviderHttpRequest existing{"https://example.com/v1/responses",
                               "POST",
                               expected,
                               {{"Authorization", "Bearer private-config-only"}},
                               30};
  ProviderAuthConfig native;
  native.curl_executable = "/bin/false";
  bool allocation = false;
  {
    auto runtime = unwrap(RawRuntime::open(session.string(), id<EnvironmentId>(61)));
    auto handle = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{handle};
      wire_allocations = {};
      wire_literal = expected;
      existing_wire = mode == 2 ? existing.body.data() : nullptr;
      wire_cut = wire_live = false;
      refused_wire_allocation = 0;
      watch_wire = true;
      try {
        if (mode == 0) {
          if (!login)
            throw std::runtime_error{"wire compatibility login absent"};
          const auto result = openai_http(compatibility, *login, "responses", &value);
          allocation =
              !result.has_value() && result.error().code == ErrorCode::allocation;
        } else if (mode == 1)
          (void)auth.request("openai", "responses", value, 30, {}, nullptr, nullptr,
                             &context);
        else
          (void)provider_http(existing, native, {}, nullptr, true, nullptr, &context);
      } catch (const std::bad_alloc &) {
        allocation = true;
      }
      watch_wire = false;
      existing_wire = nullptr;
    }
    handle.reset();
  }
  check(allocation && wire_cut && wire_live && refused_wire_allocation != 0,
        "actual post-encoder/native-body allocation cut did not execute");
  check(callbacks == 0, "allocation cut reached callback transport");
  const auto all = records(session);
  check(std::none_of(all.begin(), all.end(),
                     [](const auto &record) { return record.site == 1400; }),
        "allocation cut reached native Child spawn");
  std::cerr << "wire allocation " << mode << " acquired=" << wire_live
            << " refused=" << refused_wire_allocation << '\n';
  check(std::count_if(all.begin(), all.end(),
                      [](const auto &record) { return record.site == 1301; }) == 1,
        "PX1 original body missing or duplicated");
  const auto &begin = only(all, 1300);
  check(receipt.request && same(*receipt.request, begin.reference()) &&
            !receipt.response,
        "wire failed native request receipt is not actual/no-child");
  const auto &body = only(all, 1301, begin.reference());
  ByteView bytes{body.body};
  check(same(read_ref(bytes), begin.reference()), "wire source native request changed");
  check(take_text(bytes, read_native<std::size_t>(bytes)) == "POST",
        "wire method changed");
  const auto target = take_text(bytes, read_native<std::size_t>(bytes));
  check(target == (mode == 0   ? "https://chatgpt.com/backend-api/codex/responses"
                   : mode == 1 ? "https://api.openai.com/v1/responses"
                               : existing.url),
        "wire URL changed");
  check(read_native<std::size_t>(bytes) == expected.size() &&
            take_text(bytes, expected.size()) == expected && bytes.empty(),
        "post-encoder original lost/changed before allocation failure");
  const auto &terminal = only(all, 1303, begin.reference());
  ByteView end{terminal.body};
  (void)read_ref(end);
  check(read_native<std::uint8_t>(end) == 3 && read_native<std::uint8_t>(end) == 1 &&
            read_native<ErrorCode>(end) == ErrorCode::allocation &&
            read_native<std::int64_t>(end) == 0 && end.empty(),
        "wire allocation terminal not original observed failure");
  const auto &phase = only(all, 1320);
  const auto &settled = only(all, 1321, phase.reference());
  ByteView outcome{settled.body};
  check(same(read_ref(outcome), phase.reference()) &&
            read_native<std::uint8_t>(outcome) == 3,
        "wire allocation adapter terminal not actual exception");
  unsigned transport_links = 0;
  for (const auto &record : all)
    if (record.site == 1324) {
      ByteView linked{record.body};
      const auto owner = read_ref(linked);
      const auto source = read_ref(linked);
      if (same(owner, phase.reference()) && same(source, begin.reference()))
        ++transport_links;
    }
  check(transport_links == 1, "wire trace duplicate/missing adapter link");
  if (mode == 1) {
    const auto &resolution = only(all, 1600);
    ByteView linked{only(all, 1612).body};
    check(same(read_ref(linked), resolution.reference()) &&
              same(read_ref(linked), begin.reference()) && linked.empty(),
          "prebound HTTP/auth resolution native pair changed");
  }
}
void standalone_original(const Directory &directory,
                         const std::filesystem::path &executable) {
  const auto input = directory.root / "literal-request.json";
  const std::string literal{"{BAD\0\xff\n", 7};
  write_file(input.string(), literal);
  const auto capture = directory.root / "new-capture";
  const auto invoke = [&] {
    detail::Child child;
    child.start({executable.string(), input.string(), capture.string()}, {}, true);
    child.close_input();
    int code = 0;
    const auto message =
        child.collect(detail::Clock::now() + std::chrono::seconds{5}, 1024, &code);
    check(code == 1 && message.find("colleague stopped") != std::string::npos,
          "actual standalone parse/setup failure changed");
  };
  invoke();
  struct stat owned{};
  check(::lstat(capture.c_str(), &owned) == 0 && S_ISDIR(owned.st_mode) &&
            owned.st_uid == ::geteuid() && (owned.st_mode & 0777) == 0700,
        "standalone root native ownership/mode wrong");
  const auto all = records(capture);
  check(all.size() == 3, "standalone acquired request scope missing/duplicated");
  const auto &begin = only(all, 1320);
  const auto &body = only(all, 1322, begin.reference());
  ByteView bytes{body.body};
  check(same(read_ref(bytes), begin.reference()) &&
            read_native<std::uint8_t>(bytes) == 11 &&
            read_native<std::size_t>(bytes) == literal.size() &&
            take_text(bytes, literal.size()) == literal && bytes.empty(),
        "standalone discarded/encoded/normalized pre-parse original");
  const auto &terminal = only(all, 1321, begin.reference());
  ByteView end{terminal.body};
  (void)read_ref(end);
  check(read_native<std::uint8_t>(end) == 2,
        "standalone parser error became fabricated success");
  invoke();
  check(records(capture).size() == all.size(), "existing capture root reused/mutated");
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 1 && std::string_view{argv[1]} == "app-server") {
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.find("\"id\":\"init\"") != std::string::npos)
        std::cout << R"({"id":"init","result":{}})" << std::endl;
      else if (line.find("\"id\":\"auth\"") != std::string::npos) {
        std::cout
            << R"({"id":"auth","result":{"authMethod":"chatgpt","authToken":"fixture-access"}})"
            << std::endl;
        return 0;
      }
    }
    return 0;
  }
  if (argc == 2 && std::string_view{argv[1]} == "--px1-baseline") {
    Directory directory;
    unsigned failed = 0;
    for (unsigned mode = 0; mode < 3; ++mode)
      try {
        wire_allocation(directory, mode, std::filesystem::absolute(argv[0]).string());
      } catch (const std::exception &error) {
        ++failed;
        std::cerr << "PX1 mode " << mode << ": " << error.what() << '\n';
      }
    return failed ? 1 : 0;
  }
  if (argc == 2 && std::string_view{argv[1]} == "--native-original") {
    return ::write(STDOUT_FILENO, original.data(), original.size()) == 5 ? 7 : 8;
  }
  try {
    Directory directory;
    unsigned sequence = 0;
    for (const auto mode :
         {CallbackCase::exact, CallbackCase::partial_exact, CallbackCase::partial_error,
          CallbackCase::partial_unknown, CallbackCase::uncommitted,
          CallbackCase::before_prepare, CallbackCase::zero, CallbackCase::double_commit,
          CallbackCase::observer_error, CallbackCase::observer_unknown,
          CallbackCase::overflow, CallbackCase::receiver_refusal}) {
      std::cerr << "callback " << sequence << ": ";
      callback_case(directory, mode, sequence++);
      std::cerr << "pass\n";
    }
    std::cerr << "inactive source\n";
    inactive_source();
    std::cerr << "cancel source\n";
    bounded_admission(directory, false);
    std::cerr << "expire source\n";
    bounded_admission(directory, true);
    std::cerr << "source death\n";
    published_producer_death(directory);
    std::cerr << "native source\n";
    native_transfer(directory, std::filesystem::absolute(argv[0]).string());
    std::cerr << "decoder codec\n";
    decoder_and_codec(directory);
    std::cerr << "coding retry\n";
    coding_retry(directory);
    std::cerr << "preparation refusal\n";
    preparation_refusal(directory);
    for (unsigned mode = 0; mode < 3; ++mode)
      wire_allocation(directory, mode, std::filesystem::absolute(argv[0]).string());
    standalone_original(directory, std::filesystem::absolute(argv[0]).parent_path() /
                                       "blackbird-colleague");
    std::cout << "provider bounded original checks passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
