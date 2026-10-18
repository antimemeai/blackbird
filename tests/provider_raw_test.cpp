#include "../src/native_process.hpp"
#include "blackbird/openai.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/tools.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace blackbird;
namespace {
std::atomic<int> watched_cancel{-1};
std::atomic<bool> space_poll_entered{false};
} // namespace
// The real full-lane wait reaches its kernel poll with cancellation in its wait
// set. A cancellation written at this cut attacks wake-before-park as well as
// already parked callers; the hook never substitutes a native return.
extern "C" int poll(pollfd *fds, nfds_t count, int timeout) {
  using NativePoll = int (*)(pollfd *, nfds_t, int);
  static const auto native_poll =
      reinterpret_cast<NativePoll>(::dlsym(RTLD_NEXT, "poll"));
  if (!native_poll)
    std::abort();
  const auto watched = watched_cancel.load();
  if (watched >= 0 && count >= 2 && timeout != 0)
    for (nfds_t i = 0; i < count; ++i)
      if (fds[i].fd == watched)
        space_poll_entered.store(true);
  return native_poll(fds, count, timeout);
}
namespace {
// Owned wire formatting prepays its existing 16MiB codec/source bound.
constexpr RawCapacities owned_provider_capacities{32 * 1024 * 1024, 128 * 1024, 4096};
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error{message};
}
template <class T> T native(ByteView &bytes) {
  check(bytes.size() >= sizeof(T), "short native field");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
std::string_view text(ByteView &bytes, std::size_t count) {
  check(bytes.size() >= count, "short original body");
  const auto result =
      std::string_view{reinterpret_cast<const char *>(bytes.data()), count};
  bytes = bytes.subspan(count);
  return result;
}
void ref(ByteView &bytes, IdentityBytes epoch, std::uint64_t ordinal) {
  check(bytes.size() >= 24 && std::equal(epoch.begin(), epoch.end(), bytes.begin()),
        "wrong request lane");
  bytes = bytes.subspan(16);
  check(native<std::uint64_t>(bytes) == ordinal, "wrong request ordinal");
}
struct Record {
  std::uint32_t site;
  std::uint64_t ordinal;
  std::vector<std::byte> body;
};
std::vector<Record> records(RawChannel &channel) {
  std::vector<Record> result;
  for (auto partition :
       {RawPartition::data, RawPartition::control, RawPartition::failure}) {
    auto read = unwrap(channel.read(partition, 0, 1024 * 1024));
    ByteView bytes{read.bytes};
    while (!bytes.empty()) {
      const auto size = native<std::uint32_t>(bytes);
      const auto site = native<std::uint32_t>(bytes);
      const auto ordinal = native<std::uint64_t>(bytes);
      check(size >= 16 && size - 16 <= bytes.size(), "invalid original envelope");
      result.push_back({site, ordinal, {bytes.begin(), bytes.begin() + (size - 16)}});
      bytes = bytes.subspan(size - 16);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const Record &a, const Record &b) { return a.ordinal < b.ordinal; });
  return result;
}
const std::string normal_wire{"B\0Y\xff\n200", 8};
int fake() {
  const std::string input{std::istreambuf_iterator<char>{std::cin}, {}};
  if (input.find("Authorization: Bearer raw-test-private-token") == std::string::npos ||
      input.find("data-binary = \"{\\\"prompt\\\":\\\"fixture\\\"}\"") ==
          std::string::npos)
    return 11;
  const auto *mode = std::getenv("BB_RAW_PROVIDER_MODE");
  const std::string_view selected = mode ? mode : "normal";
  if (selected == "transport") {
    const char original[]{'P', '\0', 'R'};
    (void)::write(STDOUT_FILENO, original, sizeof(original));
    return 56;
  }
  if (selected == "overflow") {
    const std::string bytes(16384, 'x');
    std::cout << bytes;
  } else if (selected == "bad_tail") {
    std::cout << "not-a-status";
  } else if (selected == "http") {
    std::cout << "failure-body\n429";
  } else {
    (void)::write(STDOUT_FILENO, normal_wire.data(), normal_wire.size());
  }
  return 0;
}
int fake_child() {
  const std::string input{std::istreambuf_iterator<char>{std::cin}, {}};
  if (input != "hello" && input != std::string(512 * 1024, 'q'))
    return 19;
  const char out[]{'A', '\0', 'B', static_cast<char>(0xff), '\n'};
  const char err[]{'E', '\0', 'R'};
  (void)::write(STDOUT_FILENO, out, sizeof(out));
  (void)::write(STDERR_FILENO, err, sizeof(err));
  return 7;
}
int fake_auth() {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.find("\"id\":\"init\"") != std::string::npos)
      std::cout << "{\"id\":\"init\",\"result\":{}}" << std::endl;
    else if (line.find("\"id\":\"auth\"") != std::string::npos)
      std::cout << "{\"id\":\"auth\",\"result\":{\"authMethod\":\"chatgpt\","
                   "\"authToken\":\"raw-test-private-token\"}}"
                << std::endl;
  }
  return 0;
}
int staged_child() {
  const auto *gate = std::getenv("BB_RAW_PROVIDER_STAGE");
  if (!gate)
    return 21;
  {
    std::ofstream pid_file{std::string{gate} + ".pid", std::ios::binary};
    const auto pid = ::getpid();
    pid_file.write(reinterpret_cast<const char *>(&pid), sizeof(pid));
  }
  const char first[] = "first-original\n";
  if (::write(STDOUT_FILENO, first, sizeof(first) - 1) != sizeof(first) - 1)
    return 22;
  const int input = ::open(gate, O_RDONLY);
  char release = 0;
  if (input < 0 || ::read(input, &release, 1) != 1)
    return 23;
  (void)::close(input);
  const char tail[] = "unread-original\n";
  if (::write(STDOUT_FILENO, tail, sizeof(tail) - 1) != sizeof(tail) - 1)
    return 24;
  return 7;
}
template <class Predicate> void until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
  while (!predicate()) {
    check(std::chrono::steady_clock::now() < deadline,
          "native fixture cut not reached");
    std::this_thread::yield();
  }
}
std::string lane_name(IdentityBytes epoch) {
  const std::string_view hex = "0123456789abcdef";
  std::string result{"/bb"};
  for (std::size_t i = 0; i < 12; ++i) {
    const auto byte = std::to_integer<unsigned>(epoch[i]);
    result += hex[byte >> 4];
    result += hex[byte & 15U];
  }
  return result;
}
struct PausedWriters {
  RawRuntimeStatus status;
  explicit PausedWriters(RawRuntimeStatus value) : status(value) {
    check(::kill(status.supervisor, SIGSTOP) == 0 &&
              ::kill(status.recorder, SIGSTOP) == 0,
          "pause writer processes");
  }
  ~PausedWriters() {
    (void)::kill(status.supervisor, SIGCONT);
    (void)::kill(status.recorder, SIGCONT);
  }
};
void blocked_read(const std::filesystem::path &directory, const std::string &executable,
                  bool expire) {
  const auto session =
      (directory / (expire ? "expired-session" : "cancelled-session")).string();
  std::filesystem::create_directory(session);
  check(::chmod(session.c_str(), 0700) == 0, "private blocked session");
  const auto gate = session + "/gate";
  check(::mkfifo(gate.c_str(), 0600) == 0 &&
            ::setenv("BB_RAW_PROVIDER_STAGE", gate.c_str(), 1) == 0,
        "staged child gate");
  const RawCapacities capacities{16384, 16384, 4096};
  IdentityBytes identity{};
  identity[0] = std::byte{20};
  const auto environment = unwrap(EnvironmentId::from_bytes(identity));
  {
    auto runtime = unwrap(RawRuntime::open(session, environment, {capacities, 4096}));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    auto child = std::make_unique<detail::Child>();
    child->deadline_error = {ErrorCode::provider_transport, 28};
    child->raw_capture({});
    const auto deadline = detail::Clock::now() + std::chrono::seconds{10};
    child->start({executable, "--staged-child"});
    child->close_input();
    check(child->line(deadline, 64) == "first-original", "first original unavailable");
    auto channel = unwrap(
        RawChannel::open(lane_name(raw_lane_epoch()), raw_lane_epoch(), capacities));
    {
      PausedWriters paused{unwrap(runtime->status())};
      const auto cursor = unwrap(channel.cursors(RawPartition::data));
      const auto available = capacities[0] - (cursor.published - cursor.acknowledged);
      check(available >= 16, "filler admission unavailable");
      const std::string filler(static_cast<std::size_t>(available - 16), 'f');
      const std::array fields{std::as_bytes(std::span{filler.data(), filler.size()})};
      (void)unwrap(raw_emit(1399, fields));
      const auto full = unwrap(channel.cursors(RawPartition::data));
      check(capacities[0] - (full.published - full.acknowledged) < 8253,
            "read lane is not full");
      const int release = ::open(gate.c_str(), O_WRONLY);
      check(release >= 0 && ::write(release, "x", 1) == 1,
            "release second child bytes");
      check(::close(release) == 0, "close release pipe");
      pid_t peer = -1;
      std::ifstream pid_file{gate + ".pid", std::ios::binary};
      pid_file.read(reinterpret_cast<char *>(&peer), sizeof(peer));
      check(pid_file.good() && peer > 0, "staged child identity");
      until([&] {
        siginfo_t observed{};
        check(::waitid(P_PID, static_cast<id_t>(peer), &observed,
                       WEXITED | WNOHANG | WNOWAIT) == 0,
              "observe staged exit");
        return observed.si_pid == peer && observed.si_code == CLD_EXITED &&
               observed.si_status == 7;
      });
      std::array<int, 2> cancellation{};
      check(::pipe(cancellation.data()) == 0, "cancel pipe");
      watched_cancel.store(cancellation[0]);
      space_poll_entered.store(false);
      std::thread wake{[&] {
        until([] { return space_poll_entered.load(); });
        if (!expire)
          check(::write(cancellation[1], "c", 1) == 1, "cancel admitted wait");
      }};
      bool refused = false;
      {
        auto control = raw_wait_control();
        control.cancellation_fds[0] = cancellation[0];
        if (expire)
          control.deadline = detail::Clock::now() + std::chrono::milliseconds{150};
        RawWaitScope scope{control};
        try {
          (void)child->line(deadline, 64);
        } catch (const Error &error) {
          check(expire ? error == Error{ErrorCode::provider_transport, 28}
                       : error == Error{ErrorCode::interrupted, ECANCELED},
                "wrong full-lane owner outcome");
          refused = true;
        }
      }
      wake.join();
      watched_cancel.store(-1);
      check(::close(cancellation[0]) == 0 && ::close(cancellation[1]) == 0,
            "close cancel pipe");
      check(refused && space_poll_entered.load(), "full-lane wait not exercised");
      check(unwrap(channel.cursors(RawPartition::data)).published == full.published,
            "read acquired bytes after failed admission");
      child.reset(); // Prepaid cleanup completes while the recorder is still stopped.
      check(unwrap(channel.cursors(RawPartition::data)).published == full.published,
            "cleanup published an invented read");
    }
  }
  const auto runs = unwrap(discover_raw_runs(session));
  check(runs.size() == 1, "wrong blocked owner run count");
  const auto &snapshot = runs[0].snapshot;
  check(snapshot.has_value() && snapshot->closed, "blocked owner run not closed");
  auto reader = unwrap(RawRunReader::open(session + "/raw/" + runs[0].name));
  std::vector<std::byte> buffer(65536);
  std::string original;
  unsigned release = 0, refusal = 0, reaps = 0;
  while (const auto record = unwrap(reader->read(buffer))) {
    ByteView body{record->raw};
    (void)native<std::uint32_t>(body);
    const auto site = native<std::uint32_t>(body);
    (void)native<std::uint64_t>(body);
    check(site != 8001, "cancellation fenced the whole emitter");
    if (site == 1404) {
      body = body.subspan(24);
      (void)native<std::uint8_t>(body);
      (void)native<std::size_t>(body);
      const auto n = native<ssize_t>(body);
      (void)native<int>(body);
      if (n > 0)
        original += text(body, static_cast<std::size_t>(n));
    } else if (site == 1409) {
      ++release;
    } else if (site == 1410) {
      ++refusal;
    } else if (site == 1408) {
      ++reaps;
    }
  }
  check(original == "first-original\n" && release == 1 && refusal == 1 && reaps == 1,
        "blocked owner originals/cleanup changed");
}

void compatibility_exception(const std::filesystem::path &directory,
                             const std::string &executable) {
  OpenAiConfig config;
  config.codex_home = directory / "compatibility-auth";
  std::filesystem::create_directory(config.codex_home);
  config.codex_executable = executable;
  config.curl_executable = executable;
  {
    std::ofstream auth{config.codex_home / "auth.json", std::ios::binary};
    auth << "{\"auth_mode\":\"chatgpt\",\"tokens\":{\"account_id\":\"fixture-account\","
            "\"access_token\":\"raw-test-private-token\"}}";
  }
  check(::setenv("BB_RAW_PROVIDER_MODE", "normal", 1) == 0, "compat mode");
  IdentityBytes epoch{};
  epoch[0] = std::byte{18};
  const auto name = "/bbcompat" + std::to_string(::getpid());
  (void)RawChannel::unlink(name);
  auto channel = unwrap(RawChannel::create(name, epoch, owned_provider_capacities));
  RawProducer producer{channel};
  bool observed = false;
  {
    RawThreadBinding binding{producer, epoch};
    auto login = unwrap(codex_login(config));
    const auto authorization = records(channel);
    std::uint64_t action = 0;
    unsigned private_begins = 0, private_returns = 0;
    std::optional<RawEventRef> pending;
    const std::array<std::uint8_t, 5> private_operations{1, 1, 2, 3, 2};
    const std::array<std::uint32_t, 15> private_sites{1600, 1601, 1614, 1617, 1618,
                                                      1615, 1616, 1615, 1616, 1615,
                                                      1616, 1617, 1618, 1607, 1608};
    check(authorization.size() == private_sites.size(),
          "private authorization operation count changed");
    std::size_t private_index = 0;
    for (const auto &record : authorization) {
      check(record.site == private_sites[private_index++],
            "private authorization site sequence changed");
      const std::string literal{reinterpret_cast<const char *>(record.body.data()),
                                record.body.size()};
      check(literal.find("raw-test-private-token") == std::string::npos &&
                literal.find("fixture-account") == std::string::npos,
            "private authorization payload captured");
      ByteView body{record.body};
      if (record.site == 1600) {
        check(action == 0, "duplicate authorization owner");
        action = record.ordinal;
      } else if (record.site == 1615 || record.site == 1617) {
        check(action != 0 && !pending && private_begins < private_operations.size(),
              "private authorization request sequence changed");
        ref(body, epoch, action);
        check(native<std::uint8_t>(body) == private_operations[private_begins] &&
                  body.empty(),
              "private authorization request body changed");
        pending = RawEventRef{epoch, record.ordinal};
        ++private_begins;
      } else if (record.site == 1616 || record.site == 1618) {
        if (!pending)
          throw std::runtime_error{"private authorization return lacks request"};
        ref(body, epoch, action);
        ref(body, epoch, pending->ordinal);
        check(native<std::uint8_t>(body) == 1 && body.empty(),
              "private authorization return body changed");
        pending.reset();
        ++private_returns;
      } else {
        check(record.site == 1601 || record.site == 1614 || record.site == 1607 ||
                  record.site == 1608,
              "private authorization emitted a response body");
      }
    }
    check(action != 0 && private_begins == 5 && private_returns == 5 && !pending,
          "private authorization metadata missing");
    config.response_observer = [](std::string_view) {
      throw std::runtime_error{"compatibility observer failure"};
    };
    const auto request = Value::object({{"prompt", Value{"fixture"}}});
    try {
      (void)openai_http(config, login, "responses", &request);
    } catch (const std::runtime_error &error) {
      check(std::string_view{error.what()} == "compatibility observer failure",
            "compatibility error changed");
      observed = true;
    }
  }
  bool terminal = false;
  for (const auto &record : records(channel)) {
    if (record.site == 1303) {
      ByteView body{record.body};
      body = body.subspan(24);
      check(native<std::uint8_t>(body) == 5, "compatibility exception lost");
      terminal = true;
    }
    const std::string original{reinterpret_cast<const char *>(record.body.data()),
                               record.body.size()};
    check(original.find("raw-test-private-token") == std::string::npos,
          "private compatibility credential captured");
  }
  check(observed && terminal, "compatibility exception terminal absent");
  unwrap(RawChannel::unlink(name));
}

void child_originals(const std::string &executable, bool partial, bool overflow) {
  IdentityBytes epoch{};
  epoch[0] = std::byte{8};
  const auto name = "/bbrc" + std::to_string(::getpid());
  (void)RawChannel::unlink(name);
  auto channel =
      unwrap(RawChannel::create(name, epoch, {1024 * 1024, 128 * 1024, 4096}));
  RawProducer producer{channel};
  const std::string input = partial ? std::string(512 * 1024, 'q') : "hello";
  {
    RawThreadBinding binding{producer, epoch};
    detail::Child child;
    child.separate_error = true;
    child.raw_capture({}, true, true);
    const auto deadline = detail::Clock::now() + std::chrono::seconds{10};
    child.start({executable, "--raw-child"});
    child.send(input, deadline);
    child.close_input();
    int exit = 0;
    try {
      const auto out = child.collect(deadline, overflow ? 1 : 64, &exit);
      check(!overflow && out == std::string("A\0B\xff\n", 5) && exit == 7,
            "child stdout/exit changed");
      check(child.error_bytes == std::string("E\0R", 3), "child stderr changed");
    } catch (const Error &error) {
      check(overflow && error.code == ErrorCode::capacity, "wrong child overflow");
    }
  }
  std::string stdout_bytes, stderr_bytes;
  std::size_t sent = 0;
  bool partial_write = false;
  unsigned closes = 0, reap = 0, stdout_eof = 0, stderr_eof = 0;
  for (const auto &record : records(channel)) {
    ByteView body{record.body};
    check(record.site != 8001, "fixed terminal credit exhausted");
    if (record.site == 1404) {
      body = body.subspan(24);
      const auto pipe = native<std::uint8_t>(body);
      (void)native<std::size_t>(body);
      const auto n = native<ssize_t>(body);
      (void)native<int>(body);
      if (n > 0) {
        auto &result = pipe == 1 ? stdout_bytes : stderr_bytes;
        result += text(body, static_cast<std::size_t>(n));
      } else if (n == 0) {
        if (pipe == 1)
          ++stdout_eof;
        else if (pipe == 2)
          ++stderr_eof;
      }
      check(body.empty(), "read byte length mismatch");
    } else if (record.site == 1403) {
      body = body.subspan(24);
      const auto requested = native<std::size_t>(body);
      const auto accepted = native<ssize_t>(body);
      (void)native<int>(body);
      if (accepted > 0) {
        sent += static_cast<std::size_t>(accepted);
        partial_write = partial_write || static_cast<std::size_t>(accepted) < requested;
      }
      check(body.empty(), "input/config body was captured");
    } else if (record.site == 1406) {
      ++closes;
    } else if (record.site == 1408) {
      ++reap;
      body = body.subspan(24);
      const auto pid = native<pid_t>(body), result = native<pid_t>(body);
      (void)native<int>(body);
      check(native<bool>(body) && result == pid, "native final reap absent");
      const auto status = native<int>(body);
      if (!overflow)
        check(WIFEXITED(status) && WEXITSTATUS(status) == 7,
              "native wait status changed");
    }
  }
  check(sent == input.size() && (!partial || partial_write),
        "partial input facts lost");
  check(closes == 12 && reap == 1, "temporary/final endpoints not all closed");
  if (!overflow) {
    check(stdout_eof == 1 && stderr_eof == 1, "native EOF observations absent");
    check(stdout_bytes == std::string("A\0B\xff\n", 5) &&
              stderr_bytes == std::string("E\0R", 3),
          "native original bytes changed");
  } else {
    // The stderr poll can time out before the child starts, then stdout can
    // become readable first. Whichever read exceeds the one-byte limit must
    // retain that exact acquired original; the other pipe need not be read.
    check((stderr_bytes == std::string("E\0R", 3) && stdout_bytes.empty()) ||
              (stdout_bytes == std::string("A\0B\xff\n", 5) && stderr_bytes.empty()),
          "overflowing acquired original lost");
  }
  unwrap(RawChannel::unlink(name));
}
void run(const std::filesystem::path &directory, const std::string &executable,
         std::string_view mode) {
  check(::setenv("BB_RAW_PROVIDER_MODE", std::string{mode}.c_str(), 1) == 0,
        "mode env");
  ProviderAuthConfig config;
  config.directory = directory / "auth";
  config.curl_executable = executable;
  ProviderAuth auth{config};
  auth.set_key("openai", "raw-test", "raw-test-private-token");
  IdentityBytes epoch{};
  epoch[0] = std::byte{7};
  const auto name = "/bbrp" + std::to_string(::getpid());
  (void)RawChannel::unlink(name);
  auto channel = unwrap(RawChannel::create(name, epoch, owned_provider_capacities));
  RawProducer producer{channel};
  std::string observed;
  bool failed = false;
  {
    RawThreadBinding binding{producer, epoch};
    try {
      RawProviderOwner owner{};
      owner.attempt[0] = std::byte{11};
      owner.invocation[0] = std::byte{12};
      owner.generation[0] = std::byte{13};
      owner.dispatch.lane = epoch;
      owner.dispatch.ordinal = 43;
      const auto result = auth.request(
          "openai", "responses", Value::object({{"prompt", Value{"fixture"}}}), 10,
          [&](std::string_view bytes) {
            const auto before = records(channel);
            std::string retained;
            for (const auto &record : before)
              if (record.site == 1404) {
                ByteView body{record.body};
                body = body.subspan(24);
                const auto pipe = native<std::uint8_t>(body);
                (void)native<std::size_t>(body);
                const auto n = native<ssize_t>(body);
                (void)native<int>(body);
                if (pipe == 1 && n > 0)
                  retained += text(body, static_cast<std::size_t>(n));
              }
            check(retained == observed + std::string{bytes},
                  "observer ran before original publication");
            observed += bytes;
            if (mode == "observer")
              throw Error{ErrorCode::corrupt, 93};
            if (mode == "std_observer")
              throw std::runtime_error{"native observer failure"};
          },
          nullptr, &owner);
      check(mode == "normal", "expected transport failure");
      check(result.body == normal_wire.substr(0, 4), "binary body changed");
    } catch (const Error &error) {
      failed = true;
      if (mode == "transport")
        check(error.code == ErrorCode::provider_transport && error.detail == 56,
              "native child failure changed");
      else if (mode == "http")
        check(error.code == ErrorCode::provider_rate_limit && error.detail == 429,
              "HTTP failure changed");
      else if (mode == "observer")
        check(error.code == ErrorCode::corrupt && error.detail == 93,
              "observer failure changed");
      else if (mode == "bad_tail")
        check(error.code == ErrorCode::corrupt, "bad tail failure changed");
      else
        throw;
    } catch (const std::runtime_error &error) {
      check(mode == "std_observer" &&
                std::string_view{error.what()} == "native observer failure",
            "unexpected runtime_error");
      failed = true;
    }
  }
  check(failed == (mode != "normal"), "owner outcome absent");
  const auto captured = records(channel);
  const auto begin = std::find_if(captured.begin(), captured.end(),
                                  [](const Record &r) { return r.site == 1300; });
  check(begin != captured.end(), "application request begin absent");
  const auto child_begin = std::find_if(captured.begin(), captured.end(),
                                        [](const Record &r) { return r.site == 1400; });
  check(child_begin != captured.end(), "child incarnation absent");
  {
    ByteView body{child_begin->body};
    ref(body, epoch, begin->ordinal);
    check(!native<bool>(body) && !native<bool>(body) && native<bool>(body) &&
              !native<bool>(body) && body.empty(),
          "child capture policy changed");
  }
  {
    ByteView body{begin->body};
    check(native<std::uint8_t>(body) == 1, "owner context absent");
    for (const auto first : {11, 12, 13}) {
      check(body.size() >= 16 &&
                body.front() == std::byte{static_cast<unsigned char>(first)},
            "owner identity changed");
      for (std::size_t i = 1; i < 16; ++i)
        check(body[i] == std::byte{0}, "owner identity body changed");
      body = body.subspan(16);
    }
    ref(body, epoch, 43);
    check(native<int>(body) == 10 && body.empty(), "request begin layout changed");
  }
  std::string raw;
  unsigned request_bodies = 0, terminal = 0, child_release = 0;
  for (const auto &record : captured) {
    const std::string literal{reinterpret_cast<const char *>(record.body.data()),
                              record.body.size()};
    check(literal.find("raw-test-private-token") == std::string::npos,
          "private curl credential captured");
    check(record.site != 8000 && record.site != 8001, "unexpected recorder fence");
    ByteView body{record.body};
    if (record.site >= 1401 && record.site <= 1410) {
      auto identity = body;
      ref(identity, epoch, child_begin->ordinal);
    }
    if (record.site == 1301) {
      ++request_bodies;
      ref(body, epoch, begin->ordinal);
      auto length = native<std::size_t>(body);
      check(text(body, length) == "POST", "wrong method");
      length = native<std::size_t>(body);
      check(text(body, length) == "https://api.openai.com/v1/responses", "wrong URL");
      length = native<std::size_t>(body);
      check(text(body, length) == "{\"prompt\":\"fixture\"}" && body.empty(),
            "request original changed");
      check(record.ordinal > begin->ordinal, "body not after begin");
    } else if (record.site == 1404) {
      body = body.subspan(24);
      const auto pipe = native<std::uint8_t>(body);
      check(native<std::size_t>(body) == 8192, "wrong acquisition bound");
      const auto n = native<ssize_t>(body);
      (void)native<int>(body);
      if (n > 0 && pipe == 1)
        raw += text(body, static_cast<std::size_t>(n));
      check(body.empty(), "extra read bytes");
    } else if (record.site == 1303) {
      ++terminal;
      ref(body, epoch, begin->ordinal);
      const auto branch = native<std::uint8_t>(body);
      if (mode == "std_observer")
        check(branch == 5, "observed exception classified as unresolved");
    } else if (record.site == 1409) {
      ++child_release;
    }
  }
  check(request_bodies == 1 && terminal == 1 && child_release == 1,
        "missing or duplicate owner events");
  if (mode == "normal" || mode == "observer" || mode == "std_observer")
    check(raw == normal_wire, "binary original lost");
  if (mode == "transport")
    check(raw == std::string("P\0R", 3), "failed child partial original lost");
  unwrap(RawChannel::unlink(name));
}

void preserved_originals(const std::filesystem::path &directory,
                         const std::string &executable) {
  check(::setenv("BB_RAW_PROVIDER_MODE", "normal", 1) == 0, "disk mode env");
  ProviderAuthConfig config;
  config.directory = directory / "auth";
  config.curl_executable = executable;
  ProviderAuth auth{config};
  IdentityBytes identity{};
  identity[0] = std::byte{9};
  const auto environment = unwrap(EnvironmentId::from_bytes(identity));
  const auto session = (directory / "recorded-session").string();
  std::filesystem::create_directory(session);
  check(::chmod(session.c_str(), 0700) == 0, "private runtime directory");
  {
    auto runtime = unwrap(
        RawRuntime::open(session, environment, {owned_provider_capacities, 4096}));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto returned = auth.request(
        "openai", "responses", Value::object({{"prompt", Value{"fixture"}}}), 10);
    check(returned.status == 200, "preserved provider status changed");
  }
  const auto runs = unwrap(discover_raw_runs(session));
  check(runs.size() == 1, "wrong provider run count");
  const auto &entry = runs[0];
  if (entry.error.has_value())
    throw *entry.error;
  check(entry.snapshot.has_value() && entry.snapshot->closed,
        "provider run did not close");
  auto reader = unwrap(RawRunReader::open(session + "/raw/" + runs[0].name));
  std::vector<std::byte> buffer(1024 * 1024);
  std::string raw;
  unsigned bodies = 0, terminals = 0;
  while (const auto record = unwrap(reader->read(buffer))) {
    ByteView bytes{record->raw};
    const auto size = native<std::uint32_t>(bytes);
    const auto site = native<std::uint32_t>(bytes);
    (void)native<std::uint64_t>(bytes);
    check(size == record->raw.size(), "preserved envelope size changed");
    if (site == 1404) {
      bytes = bytes.subspan(24);
      const auto pipe = native<std::uint8_t>(bytes);
      (void)native<std::size_t>(bytes);
      const auto n = native<ssize_t>(bytes);
      (void)native<int>(bytes);
      if (n > 0 && pipe == 1)
        raw += text(bytes, static_cast<std::size_t>(n));
    } else if (site == 1301) {
      ++bodies;
    } else if (site == 1303) {
      ++terminals;
    }
    const std::string original{reinterpret_cast<const char *>(record->raw.data()),
                               record->raw.size()};
    check(original.find("raw-test-private-token") == std::string::npos,
          "private credential preserved by metric hook");
  }
  check(raw == normal_wire && bodies == 1 && terminals == 1,
        "provider source-to-preserved-disk originals changed");
}

// A failed check must not leave the producer and its stopped recorder holders
// running: they would hold the test's output pipe and outlive it.
struct ProducerReaper {
  pid_t producer = -1;
  std::array<pid_t, 2> holders{};
  bool armed = true;
  ~ProducerReaper() {
    if (!armed)
      return;
    for (const auto holder : holders)
      if (holder > 0) {
        (void)::kill(holder, SIGKILL);
        (void)::kill(holder, SIGCONT);
      }
    (void)::kill(producer, SIGKILL);
    int status = 0;
    (void)::waitpid(producer, &status, 0);
  }
};

void producer_death(const std::filesystem::path &directory,
                    const std::string &executable) {
  check(::setenv("BB_RAW_PROVIDER_MODE", "normal", 1) == 0, "death mode env");
  const auto session = (directory / "dead-provider-session").string();
  std::filesystem::create_directory(session);
  check(::chmod(session.c_str(), 0700) == 0, "private dead-provider session");
  IdentityBytes identity{};
  identity[0] = std::byte{23};
  const auto environment = unwrap(EnvironmentId::from_bytes(identity));
  const RawRuntimeOptions options{owned_provider_capacities, 4096};
  std::array<int, 2> reports{}, commands{};
  check(::pipe(reports.data()) == 0 && ::pipe(commands.data()) == 0,
        "producer death pipes");
  const auto producer = ::fork();
  check(producer >= 0, "fork provider producer");
  if (producer == 0) {
    (void)::close(reports[0]);
    (void)::close(commands[1]);
    try {
      auto runtime = unwrap(RawRuntime::open(session, environment, options));
      RawThreadBinding binding{unwrap(runtime->prepare_thread())};
      const auto status = unwrap(runtime->status());
      const std::array<pid_t, 2> holders{status.supervisor, status.recorder};
      check(::write(reports[1], holders.data(), sizeof(holders)) ==
                static_cast<ssize_t>(sizeof(holders)),
            "report actual recorder identities");
      const auto epoch = raw_lane_epoch();
      check(::write(reports[1], epoch.data(), epoch.size()) ==
                static_cast<ssize_t>(epoch.size()),
            "report actual provider lane");
      char command = 0;
      check(::read(commands[0], &command, 1) == 1, "await recorder stop");
      ProviderAuthConfig config;
      config.directory = directory / "auth";
      config.curl_executable = executable;
      ProviderAuth auth{config};
      (void)auth.request(
          "openai", "responses", Value::object({{"prompt", Value{"fixture"}}}), 10,
          [&](std::string_view original) {
            check(original == normal_wire, "unexpected acquired death original");
            check(::write(reports[1], "p", 1) == 1, "report raw original publication");
            // The hook has published the native bytes before this business
            // observer. Killing here prevents adapter completion/child cleanup.
            check(::read(commands[0], &command, 1) == 1, "hold producer cut");
          });
    } catch (...) {
      ::_exit(98);
    }
    ::_exit(99);
  }
  ProducerReaper reaper{producer};
  (void)::close(reports[1]);
  (void)::close(commands[0]);
  std::array<pid_t, 2> holders{};
  check(::read(reports[0], holders.data(), sizeof(holders)) ==
            static_cast<ssize_t>(sizeof(holders)),
        "read actual recorder identities");
  reaper.holders = holders;
  IdentityBytes epoch{};
  check(::read(reports[0], epoch.data(), epoch.size()) ==
            static_cast<ssize_t>(epoch.size()),
        "read actual provider lane");
  check(::kill(holders[0], SIGSTOP) == 0 && ::kill(holders[1], SIGSTOP) == 0,
        "stop provider recorder holders");
  check(::write(commands[1], "g", 1) == 1, "start actual provider acquisition");
  char published = 0;
  check(::read(reports[0], &published, 1) == 1 && published == 'p',
        "actual provider original was not published");
  {
    auto channel =
        unwrap(RawChannel::open(lane_name(epoch), epoch, options.producer_capacities));
    const auto cut = unwrap(channel.cursors(RawPartition::data));
    check(cut.published > 0 && cut.acknowledged == 0,
          "death cut original was already acknowledged");
    std::string captured;
    unsigned reads = 0;
    for (const auto &record : records(channel))
      if (record.site == 1404) {
        ByteView body{record.body};
        body = body.subspan(24);
        check(native<std::uint8_t>(body) == 1, "death cut channel changed");
        (void)native<std::size_t>(body);
        const auto n = native<ssize_t>(body);
        (void)native<int>(body);
        check(n > 0, "death cut invented EOF/error");
        captured += text(body, static_cast<std::size_t>(n));
        ++reads;
      }
    check(reads == 1 && captured == normal_wire,
          "death cut original missing from actual source lane");
  }
  check(::kill(producer, SIGKILL) == 0 && ::kill(holders[0], SIGKILL) == 0 &&
            ::kill(holders[1], SIGKILL) == 0,
        "kill all original metric holders");
  int status = 0;
  check(::waitpid(producer, &status, 0) == producer && WIFSIGNALED(status) &&
            WTERMSIG(status) == SIGKILL,
        "provider producer did not die at selected cut");
  reaper.armed = false;
  check(::close(reports[0]) == 0 && ::close(commands[1]) == 0,
        "close producer death pipes");
  std::filesystem::path old_path;
  for (const auto &entry : std::filesystem::directory_iterator{session + "/raw"})
    if (entry.is_directory()) {
      check(old_path.empty(), "multiple original provider runs");
      old_path = entry.path();
    }
  check(!old_path.empty(), "missing original provider run");
  // Signal delivery alone does not prove native process exit. The original
  // catalogue lease must be free before a fresh runtime may recover its bytes.
  const int lease = ::open((old_path / "lanes").c_str(), O_RDWR | O_CLOEXEC);
  check(lease >= 0, "open original provider lease");
  until([&] { return ::flock(lease, LOCK_EX | LOCK_NB) == 0; });
  check(::close(lease) == 0, "release original provider lease");
  {
    auto recovered = unwrap(RawRuntime::open(session, environment, options));
    check(unwrap(recovered->status()).recorder > 0, "cold recovery recorder absent");
  }
  auto reader = unwrap(RawRunReader::open(old_path.string()));
  check(reader->snapshot().closed, "dead-provider original run not closed");
  std::vector<std::byte> buffer(1024 * 1024);
  std::string original;
  unsigned requests = 0, bodies = 0, terminals = 0, cleanup = 0;
  while (const auto record = unwrap(reader->read(buffer))) {
    ByteView body{record->raw};
    (void)native<std::uint32_t>(body);
    const auto site = native<std::uint32_t>(body);
    (void)native<std::uint64_t>(body);
    if (site == 1404) {
      body = body.subspan(24);
      const auto channel = native<std::uint8_t>(body);
      (void)native<std::size_t>(body);
      const auto n = native<ssize_t>(body);
      (void)native<int>(body);
      check(channel == 1 && n > 0, "fabricated post-death read or EOF");
      original += text(body, static_cast<std::size_t>(n));
    } else if (site == 1300) {
      ++requests;
    } else if (site == 1301) {
      ++bodies;
    } else if (site == 1303) {
      ++terminals;
    } else if (site == 1409) {
      ++cleanup;
    }
  }
  check(original == normal_wire && requests == 1 && bodies == 1 && terminals == 0 &&
            cleanup == 0,
        "producer death changed originals or fabricated owner completion");
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 1 && std::string_view{argv[1]} == "--disable")
    return fake();
  if (argc > 1 && std::string_view{argv[1]} == "--raw-child")
    return fake_child();
  if (argc > 1 && std::string_view{argv[1]} == "app-server")
    return fake_auth();
  if (argc > 1 && std::string_view{argv[1]} == "--staged-child")
    return staged_child();
  char pattern[] = "/tmp/blackbird-provider-raw-XXXXXX";
  const auto *created = ::mkdtemp(pattern);
  if (!created)
    return 1;
  try {
    for (const auto mode :
         {"normal", "transport", "http", "bad_tail", "observer", "std_observer"})
      run(created, std::filesystem::canonical(argv[0]).string(), mode);
    for (const auto partial : {false, true})
      child_originals(std::filesystem::canonical(argv[0]).string(), partial, false);
    child_originals(std::filesystem::canonical(argv[0]).string(), false, true);
    preserved_originals(created, std::filesystem::canonical(argv[0]).string());
    compatibility_exception(created, std::filesystem::canonical(argv[0]).string());
    blocked_read(created, std::filesystem::canonical(argv[0]).string(), false);
    blocked_read(created, std::filesystem::canonical(argv[0]).string(), true);
    producer_death(created, std::filesystem::canonical(argv[0]).string());
    std::filesystem::remove_all(created);
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
    std::filesystem::remove_all(created);
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    std::filesystem::remove_all(created);
    return 1;
  }
}
