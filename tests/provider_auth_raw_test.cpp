#include "blackbird/json.hpp"
#include "blackbird/openai.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/raw_auth.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/tools.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
using NativePoll = int (*)(pollfd *, nfds_t, int);
using NativeFsync = int (*)(int);
NativePoll forward_poll = nullptr;
NativeFsync forward_fsync = nullptr;
std::atomic<int> space_notice{-1};
int signal_proof = -1;
unsigned signal_stage = 0;
std::atomic<bool> fail_sync{false};
dev_t failing_device{};
ino_t failing_inode{};
} // namespace
extern "C" int poll(pollfd *watches, nfds_t count, int timeout) {
  const int saved = errno;
  if (space_notice.load() >= 0 && count >= 2 && timeout != 0 &&
      watches[0].events == POLLIN) {
    int type = 0;
    socklen_t length = sizeof(type);
    if (::getsockopt(watches[0].fd, SOL_SOCKET, SO_TYPE, &type, &length) == 0 &&
        type == SOCK_STREAM) {
      const int notice = space_notice.exchange(-1);
      if (notice >= 0) {
        const char byte = 1;
        (void)::write(notice, &byte, 1);
      }
    }
  }
  const auto scope = blackbird::raw_wait_control();
  if (signal_proof >= 0 && count == 1 && watches[0].fd == STDIN_FILENO &&
      scope.cancellation_fds[0] >= 0 && signal_stage == 0) {
    const int endpoint = scope.cancellation_fds[0];
    const unsigned char flags =
        static_cast<unsigned char>((::fcntl(endpoint, F_GETFD) & FD_CLOEXEC ? 1 : 0) |
                                   (::fcntl(endpoint, F_GETFL) & O_NONBLOCK ? 2 : 0));
    (void)::write(signal_proof, &flags, 1);
    signal_stage = 1;
  }
  errno = saved;
  const int result = forward_poll(watches, count, timeout);
  const int returned_errno = errno;
  if (signal_proof >= 0 && count == 1 && watches[0].fd == STDIN_FILENO &&
      scope.cancellation_fds[0] >= 0 && signal_stage == 1) {
    pollfd sticky{scope.cancellation_fds[0], POLLIN, 0};
    if (forward_poll(&sticky, 1, 0) == 1 && (sticky.revents & POLLIN) != 0) {
      const char ready = 4;
      (void)::write(signal_proof, &ready, 1);
      signal_stage = 2;
    }
  }
  errno = returned_errno;
  return result;
}
extern "C" int fsync(int descriptor) {
  const int saved = errno;
  if (fail_sync.load()) {
    struct stat state{};
    if (::fstat(descriptor, &state) == 0 && state.st_dev == failing_device &&
        state.st_ino == failing_inode && fail_sync.exchange(false)) {
      errno = ENOSPC;
      return -1;
    }
  }
  errno = saved;
  return forward_fsync(descriptor);
}
using namespace blackbird;
int reply(ProviderHttpResponseLease &response, int status, std::string body) {
  auto extent = response.prepare(body.size());
  std::copy(body.begin(), body.end(), extent.begin());
  response.commit(body.size());
  return status;
}

namespace {
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error{message};
}
template <class T> T native(ByteView &bytes) {
  check(bytes.size() >= sizeof(T), "short native metric field");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
template <class T> void append(std::vector<std::byte> &bytes, const T &value) {
  const auto field = std::as_bytes(std::span{&value, std::size_t{1}});
  bytes.insert(bytes.end(), field.begin(), field.end());
}
void append_text(std::vector<std::byte> &bytes, std::string_view value) {
  append(bytes, value.size());
  const auto field = std::as_bytes(std::span{value.data(), value.size()});
  bytes.insert(bytes.end(), field.begin(), field.end());
}
void append_ref(std::vector<std::byte> &bytes, const IdentityBytes &epoch,
                std::uint64_t ordinal) {
  bytes.insert(bytes.end(), epoch.begin(), epoch.end());
  append(bytes, ordinal);
}
struct Record {
  RawOrigin origin;
  std::uint32_t site;
  std::uint64_t ordinal;
  std::vector<std::byte> body;
};
std::vector<Record> read_records(const std::filesystem::path &directory) {
  std::vector<Record> result;
  const auto entries = unwrap(discover_raw_runs(directory.string()));
  check(!entries.empty(), "actual capture run absent");
  for (const auto &entry : entries) {
    check(!entry.error && entry.snapshot.has_value(), "actual capture run unreadable");
    auto reader = unwrap(RawRunReader::open((directory / "raw" / entry.name).string()));
    std::vector<std::byte> output(2 * 1024 * 1024);
    for (;;) {
      const auto read = unwrap(reader->read(output));
      if (!read)
        break;
      ByteView bytes = read->raw;
      while (!bytes.empty()) {
        const auto length = native<std::uint32_t>(bytes);
        const auto site = native<std::uint32_t>(bytes);
        const auto ordinal = native<std::uint64_t>(bytes);
        check(length >= 16 && length - 16 <= bytes.size(), "bad preserved envelope");
        result.push_back({read->origin,
                          site,
                          ordinal,
                          {bytes.begin(), bytes.begin() + (length - 16)}});
        bytes = bytes.subspan(length - 16);
      }
    }
  }
  std::sort(result.begin(), result.end(), [](const Record &left, const Record &right) {
    return left.ordinal < right.ordinal;
  });
  return result;
}
void original_action(const std::filesystem::path &directory) {
  const auto run = directory / "capture";
  std::filesystem::create_directory(run);
  check(::chmod(run.c_str(), 0700) == 0, "private capture directory");
  IdentityBytes bytes{};
  bytes[0] = std::byte{7};
  const auto environment = unwrap(EnvironmentId::from_bytes(bytes));
  {
    auto runtime = unwrap(RawRuntime::open(run.string(), environment));
    auto handle = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{handle};
      ProviderAuthConfig config;
      config.directory = directory / "credentials";
      ProviderAuth auth{config};
      auth.set_key("openai", "local-account", "auth-raw-private-key-unique");
    }
    handle.reset();
  }
  const auto records = read_records(run);
  bool begin = false, terminal = false;
  for (const auto &record : records) {
    begin = begin || record.site == 1600;
    terminal = terminal || record.site == 1608;
  }
  check(begin, "baseline oracle: actual set_key emits no required auth1600 begin");
  check(terminal, "actual set_key emits no required auth1608 terminal");
  check(records.size() == 7, "set_key expected seven actual records");
  const std::array<std::uint32_t, 7> sites{1600, 1601, 1613, 1613, 1605, 1606, 1608};
  std::array<std::vector<std::byte>, 7> expected;
  append(expected[0], std::uint8_t{7}); // set_key actual action
  append(expected[0], false);           // no parent
  append(expected[0], std::uint8_t{0}); // no expected revisions
  const auto epoch = records[0].origin.lane;
  append_ref(expected[1], epoch, 1);
  append(expected[1], std::uint8_t{0});
  append_text(expected[1], "openai");
  append_text(expected[1], "local-account");
  for (std::size_t i = 2; i != 4; ++i) {
    append_ref(expected[i], epoch, 1);
    append(expected[i], static_cast<std::uint8_t>(i - 1));
    append(expected[i], std::int64_t{1});
  }
  append_ref(expected[4], epoch, 1);
  append(expected[4], std::uint8_t{7});
  append_ref(expected[5], epoch, 1);
  append_ref(expected[5], epoch, 5);
  append(expected[5], std::uint8_t{8}); // directory sync returned
  append(expected[5], std::uint8_t{1});
  append_ref(expected[6], epoch, 1);
  append(expected[6], std::uint8_t{1});
  append(expected[6], std::uint8_t{109}); // owner returned
  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto &record = records[i];
    check(record.site == sites[i] && record.ordinal == i + 1,
          "set_key actual source sites/ordinals changed");
    check(record.body == expected[i], "set_key literal native body differs");
    check(record.origin.lane == epoch &&
              record.origin.partition ==
                  (i == 1 ? RawPartition::data : RawPartition::control),
          "set_key producer/partition changed");
  }
  for (const auto &record : records) {
    const std::string_view body{reinterpret_cast<const char *>(record.body.data()),
                                record.body.size()};
    check(body.find("auth-raw-private-key-unique") == std::string_view::npos,
          "private key entered preserved metrics");
  }
}

void no_secret(const std::vector<Record> &records,
               std::span<const std::string_view> secrets) {
  for (const auto &record : records) {
    const std::string_view body{reinterpret_cast<const char *>(record.body.data()),
                                record.body.size()};
    for (const auto secret : secrets)
      check(body.find(secret) == std::string_view::npos,
            "private auth field preserved");
  }
}
EnvironmentId fixture_identity() {
  IdentityBytes bytes{};
  bytes[0] = std::byte{9};
  return unwrap(EnvironmentId::from_bytes(bytes));
}
void prepare_directory(const std::filesystem::path &path) {
  std::filesystem::create_directory(path);
  check(::chmod(path.c_str(), 0700) == 0, "private fixture directory");
}
void save_failure(const std::filesystem::path &directory) {
  const auto run = directory / "failure-capture",
             credentials = directory / "failure-credentials";
  prepare_directory(run);
  prepare_directory(credentials);
  ProviderAuthConfig config;
  config.directory = credentials;
  ProviderAuth auth{config};
  auth.set_key("openai", "account", "original-private-key-fixture");
  struct stat state{};
  check(::stat(credentials.c_str(), &state) == 0, "credential native identity");
  failing_device = state.st_dev;
  failing_inode = state.st_ino;
  {
    auto runtime = unwrap(RawRuntime::open(run.string(), fixture_identity()));
    auto handle = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{handle};
      fail_sync.store(true);
      bool refused = false;
      try {
        auth.set_key("openai", "account", "renamed-private-key-fixture");
      } catch (const Error &error) {
        refused = error == Error{ErrorCode::io, ENOSPC};
      }
      check(refused && !fail_sync.load(),
            "actual post-rename directory fsync failure missing");
    }
    handle.reset();
  }
  const auto records = read_records(run);
  check(records.size() == 7 && records[5].site == 1606 && records[6].site == 1608,
        "save failure owner sequence");
  const auto epoch = records[0].origin.lane;
  std::vector<std::byte> expected;
  append_ref(expected, epoch, 1);
  append_ref(expected, epoch, 5);
  append(expected, std::uint8_t{7});
  append(expected, std::uint8_t{2});
  append(expected, ErrorCode::io);
  append(expected, std::int64_t{ENOSPC});
  check(records[5].body == expected, "native Error/save rename stage lost");
  check(auth.binding("openai").find("revision")->number().text() == "2",
        "renamed credential effect erased after directory sync failure");
  const std::array secrets{std::string_view{"original-private-key-fixture"},
                           std::string_view{"renamed-private-key-fixture"}};
  no_secret(records, secrets);
}
struct Pipe {
  std::array<int, 2> descriptors{};
  Pipe() { check(::pipe(descriptors.data()) == 0, "fixture pipe"); }
  ~Pipe() {
    for (int fd : descriptors)
      (void)::close(fd);
  }
  Pipe(const Pipe &) = delete;
  Pipe &operator=(const Pipe &) = delete;
  void notify() const {
    const char byte = 1;
    check(::write(descriptors[1], &byte, 1) == 1, "fixture notice write");
  }
  char wait() const {
    pollfd watch{descriptors[0], POLLIN, 0};
    check(::poll(&watch, 1, 8000) == 1, "fixture native notice missing");
    char byte{};
    check(::read(descriptors[0], &byte, 1) == 1, "fixture notice read");
    return byte;
  }
};
void full_data_interruption(const std::filesystem::path &directory) {
  const auto run = directory / "full-capture";
  prepare_directory(run);
  Pipe notice, cancellation;
  auto runtime = unwrap(
      RawRuntime::open(run.string(), fixture_identity(), {{512, 8192, 512}, 8192}));
  auto handle = unwrap(runtime->prepare_thread());
  const auto status = unwrap(runtime->status());
  check(::kill(status.recorder, SIGSTOP) == 0, "stop actual recorder");
  Error result{ErrorCode::external_unknown};
  IdentityBytes epoch{};
  space_notice.store(notice.descriptors[1]);
  std::thread worker{[&] {
    RawThreadBinding binding{handle};
    epoch = raw_lane_epoch();
    const std::array<std::byte, 496> bytes{};
    const std::array<ByteView, 1> parts{bytes};
    (void)unwrap(raw_emit(9900, parts));
    RawWaitScope scope{{{cancellation.descriptors[0], -1}, {}}};
    ProviderAuthConfig config;
    config.directory = directory / "forbidden-effect";
    try {
      ProviderAuth{config}.set_key("openai", "account",
                                   "cancelled-private-key-fixture");
    } catch (const Error &error) {
      result = error;
    }
  }};
  try {
    notice.wait();
    cancellation.notify();
  } catch (...) {
    cancellation.notify();
    (void)::kill(status.recorder, SIGCONT);
    worker.join();
    throw;
  }
  worker.join();
  std::string lane_name = "/bb";
  constexpr char digits[] = "0123456789abcdef";
  for (std::size_t index = 0; index < 12; ++index) {
    const auto byte = std::to_integer<unsigned>(epoch[index]);
    lane_name += digits[byte >> 4U];
    lane_name += digits[byte & 15U];
  }
  auto lane = unwrap(RawChannel::open(lane_name, epoch, {512, 8192, 512}));
  const auto cut = unwrap(lane.cursors(RawPartition::data));
  check(::kill(status.recorder, SIGCONT) == 0, "resume actual recorder");
  check(cut.published == 512 && cut.acknowledged == 0,
        "genuine full original P>A DATA cut missing");
  check(result == Error{ErrorCode::interrupted, ECANCELED},
        "actual full DATA auth admission did not cancel");
  check(!std::filesystem::exists(directory / "forbidden-effect"),
        "Store effect preceded full DATA interruption");
  handle.reset();
  runtime.reset();
  const auto records = read_records(run);
  check(records.size() == 3 && records[0].site == 9900 && records[1].site == 1600 &&
            records[2].site == 1608,
        "interrupted DATA must retain actual BEGIN + terminal only");
  std::vector<std::byte> expected;
  append_ref(expected, epoch, 2);
  append(expected, std::uint8_t{2});
  append(expected, std::uint8_t{100});
  append(expected, ErrorCode::interrupted);
  append(expected, std::int64_t{ECANCELED});
  check(records[2].body == expected &&
            records[2].origin.partition == RawPartition::control,
        "held terminal failed after actual DATA-space interruption");
}
void environment_native(const std::filesystem::path &directory) {
  ProviderAuthConfig config;
  config.directory = directory / "never-opened-store";
  config.metrics_directory = directory / "owned-metrics";
  auto first = unwrap(RawAuthEnvironment::open(config));
  const auto identity = first->identity();
  auto second = unwrap(RawAuthEnvironment::open(config));
  check(identity == second->identity(),
        "native durable environment identity changed on reopen");
  check(!std::filesystem::exists(config.directory),
        "environment bootstrap entered credential Store");
  const auto offered = first->runtime_directory();
  first->verify_runtime_directory(offered);
#if defined(__APPLE__)
  const auto moved = directory / "moved-owned-metrics";
  std::filesystem::rename(config.metrics_directory, moved);
  prepare_directory(config.metrics_directory);
  bool mismatch = false;
  try {
    first->verify_runtime_directory(offered);
  } catch (const Error &error) {
    mismatch = error.code == ErrorCode::conflict;
  }
  check(mismatch,
        "canonical path replacement did not refuse native root identity mismatch");
  const auto reopened = first->runtime_directory();
  first->verify_runtime_directory(reopened);
  {
    auto runtime = unwrap(RawRuntime::open(reopened, identity));
    first->verify_runtime_directory(reopened);
  }
  check(!std::filesystem::is_empty(moved / "raw"),
        "native held-root reopen did not capture at moved root before setup");
  std::filesystem::remove(config.metrics_directory);
  std::filesystem::rename(moved, config.metrics_directory);
#endif

  const auto file = config.metrics_directory / "environment/identity";
  const int fd = ::open(file.c_str(), O_RDONLY | O_CLOEXEC);
  check(fd >= 0, "native identity file missing");
  std::array<std::byte, 40> header{};
  check(::read(fd, header.data(), header.size()) == 40, "native identity extent");
  (void)::close(fd);
  check(std::memcmp(header.data(), "BBAUTH1\0", 8) == 0, "native header magic");
  ByteView fields{header};
  fields = fields.subspan(8);
  check(native<std::uint16_t>(fields) == 1 && native<std::uint16_t>(fields) == 40 &&
            native<std::uint32_t>(fields) == 0x01020304,
        "native header ABI");
  check(std::equal(identity.bytes().begin(), identity.bytes().end(), fields.begin()),
        "native generated EnvironmentId not stored directly");
  check(::chmod(file.c_str(), 0644) == 0, "unsafe mode fixture");
  auto refused = RawAuthEnvironment::open(config);
  check(!refused.has_value() && refused.error().code == ErrorCode::conflict,
        "unsafe existing identity repaired/adopted");
  check(::chmod(file.c_str(), 0600) == 0, "restore private mode");
  const int corrupt = ::open(file.c_str(), O_WRONLY | O_CLOEXEC);
  check(corrupt >= 0, "corrupt fixture open");
  const char byte = 'x';
  check(::write(corrupt, &byte, 1) == 1, "corrupt header write");
  (void)::close(corrupt);
  refused = RawAuthEnvironment::open(config);
  check(!refused.has_value() && refused.error().code == ErrorCode::corrupt,
        "corrupt identity regenerated");
  ProviderAuthConfig incomplete;
  incomplete.directory = directory / "never-opened-store";
  incomplete.metrics_directory = directory / "incomplete-metrics";
  prepare_directory(incomplete.metrics_directory);
  prepare_directory(incomplete.metrics_directory / "environment");
  refused = RawAuthEnvironment::open(incomplete);
  check(!refused.has_value() && refused.error().code == ErrorCode::incomplete,
        "published incomplete environment overwritten");
}

void environment_native_failures(const std::filesystem::path &directory) {
  ProviderAuthConfig config;
  config.directory = directory / "bootstrap-never-store";
  config.metrics_directory = directory / "published-failed-sync";
  prepare_directory(config.metrics_directory);
  struct stat root{};
  check(::stat(config.metrics_directory.c_str(), &root) == 0,
        "bootstrap native root stat");
  failing_device = root.st_dev;
  failing_inode = root.st_ino;
  fail_sync.store(true);
  const auto failed = RawAuthEnvironment::open(config);
  check(!failed.has_value() && failed.error() == Error{ErrorCode::io, ENOSPC} &&
            !fail_sync.load(),
        "actual bootstrap post-rename root sync failure absent");
  check(std::filesystem::file_size(config.metrics_directory / "environment/identity") ==
                40 &&
            !std::filesystem::exists(config.directory),
        "failed bootstrap rolled back published originals/entered Store");
  const auto retained = unwrap(RawAuthEnvironment::open(config))->identity();
  check(unwrap(RawAuthEnvironment::open(config))->identity() == retained,
        "retry replaced retained bootstrap identity");
  ProviderAuthConfig linked;
  linked.directory = config.directory;
  linked.metrics_directory = directory / "symlink-metrics";
  std::filesystem::create_directory_symlink(config.metrics_directory,
                                            linked.metrics_directory);
  const auto refused = RawAuthEnvironment::open(linked);
  check(!refused.has_value() && refused.error().code == ErrorCode::conflict,
        "nofollow metrics root accepted symlink");
  ProviderAuthConfig concurrent;
  concurrent.directory = directory / "concurrent-never-store";
  concurrent.metrics_directory = directory / "concurrent-metrics";
  Pipe replies;
  std::array<pid_t, 2> children{};
  for (auto &child : children) {
    child = ::fork();
    check(child >= 0, "concurrent native setup fork");
    if (child == 0) {
      const auto opened = RawAuthEnvironment::open(concurrent);
      if (!opened.has_value())
        ::_exit(123);
      const auto identity = opened.value()->identity().bytes();
      if (::write(replies.descriptors[1], identity.data(), identity.size()) != 16)
        ::_exit(124);
      ::_exit(0);
    }
  }
  for (const auto child : children) {
    int status = 0;
    check(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
              WEXITSTATUS(status) == 0,
          "concurrent exclusive native environment create failed");
  }
  std::array<std::byte, 32> identities{};
  std::size_t done = 0;
  while (done != identities.size()) {
    const auto read = ::read(replies.descriptors[0], identities.data() + done,
                             identities.size() - done);
    check(read > 0, "concurrent actual identity reply missing");
    done += static_cast<std::size_t>(read);
  }
  check(
      std::equal(identities.begin(), identities.begin() + 16, identities.begin() + 16),
      "concurrent creators adopted different native environments");
  check(!std::filesystem::exists(concurrent.directory),
        "concurrent bootstrap entered Store");
}
void private_surfaces(const std::filesystem::path &directory) {
  const auto run = directory / "private-capture";
  prepare_directory(run);
  std::int64_t clock = 100;
  unsigned clocks = 0;
  {
    auto runtime = unwrap(RawRuntime::open(run.string(), fixture_identity()));
    auto handle = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{handle};
      ProviderAuthConfig config;
      config.directory = directory / "private-credentials";
      config.now = [&] {
        ++clocks;
        return clock;
      };
      unsigned private_requests = 0;
      config.http = [&](const ProviderHttpRequest &request,
                        ProviderHttpResponseLease &response) {
        ++private_requests;
        if (request.url.find("device_authorization") != std::string::npos)
          return reply(
              response, 200,
              R"({"device_code":"private-device-sentinel","user_code":"private-user-sentinel","verification_uri":"https://example.com/login","expires_in":120,"interval":5})");
        check(request.body.find("private-device-sentinel") != std::string::npos,
              "actual private device code absent from business request");
        return reply(response, 400, R"({"error":"slow_down"})");
      };
      ProviderAuth auth{config};
      auth.add_provider("public-provider", "https://example.com/v1///", "chat",
                        "PUBLIC_KEY_NAME");
      bool rejected_url = false;
      try {
        auth.add_provider(
            "invalid-provider",
            "https://private-invalid-user:private-invalid-password@example.com/v1",
            "chat", "PUBLIC_KEY_NAME");
      } catch (const Error &error) {
        rejected_url = error.code == ErrorCode::invalid_range;
      }
      check(rejected_url, "actual invalid private URL accepted");
      (void)auth.device_begin("kimi", "account");
      check(clocks == 2, "instrumentation added a device clock acquisition");
      clock = 105;
      (void)auth.device_poll("kimi", "account");
      check(clocks == 4 && private_requests == 2,
            "slow-down business acquisition/request count changed");
      const auto registration = auth.registration("kimi", "account");
      (void)registration;
      (void)auth.binding("kimi");
      (void)auth.catalog();
      (void)auth.status();
      const auto tokens = unwrap(parse_json(
          R"({"access_token":"private-access-sentinel","refresh_token":"private-refresh-sentinel","expires_in":120,"token_type":"Bearer"})"));
      auth.save_oauth("anthropic", "account", tokens, "private-client-sentinel",
                      "https://console.anthropic.com/v1/oauth/token",
                      Value::object({{"id_token", Value{"private-jwt-sentinel"}}}));
      ProviderAuthConfig owned = config;
      owned.http = [](const ProviderHttpRequest &request,
                      ProviderHttpResponseLease &response) {
        if (request.url.find("oauth/token") != std::string::npos)
          return reply(
              response, 200,
              R"({"access_token":"fresh-private-access-sentinel","refresh_token":"fresh-private-refresh-sentinel","expires_in":120,"token_type":"Bearer"})");
        bool correct = false;
        for (const auto &[name, value] : request.headers)
          if (name == "Authorization")
            correct = value == "Bearer fresh-private-access-sentinel";
        check(correct, "actual owned refresh did not supply acquired private token");
        return reply(response, 200, "{}");
      };
      ProviderAuth selected{owned};
      const auto expected_binding = selected.binding("anthropic");
      clock = 220;
      check(selected.request("anthropic", "models", Value::object({}), 30, {},
                             &expected_binding)
                    .body == "{}",
            "owned native resolution result changed");
      bool caught = false;
      try {
        auth.browser_login(
            "anthropic", "account",
            [](std::string_view) { throw std::runtime_error{"observer error"}; },
            [] { return std::string{"private-browser-code-sentinel"}; });
      } catch (const std::runtime_error &) {
        caught = true;
      }
      check(caught, "native observer unknown exception was lost");
      ProviderAuthConfig unknown = config;
      unknown.http = [](const ProviderHttpRequest &,
                        ProviderHttpResponseLease &response) -> int {
        (void)response;
        throw std::runtime_error{"HTTP error"};
      };
      try {
        (void)ProviderAuth{unknown}.device_begin("kimi", "second");
        caught = false;
      } catch (const std::runtime_error &) {
        caught = true;
      }
      check(caught, "private HTTP unknown exception was lost");
      ProviderAuthConfig failed = config;
      failed.http = [](const ProviderHttpRequest &,
                       ProviderHttpResponseLease &response) -> int {
        (void)response;
        throw Error{ErrorCode::io, ECONNRESET};
      };
      bool native_error = false;
      try {
        (void)ProviderAuth{failed}.device_begin("kimi", "third");
      } catch (const Error &error) {
        native_error = error == Error{ErrorCode::io, ECONNRESET};
      }
      check(native_error, "private HTTP original native Error variant changed");
    }
    handle.reset();
  }
  const auto records = read_records(run);
  const std::array secrets{std::string_view{"private-device-sentinel"},
                           std::string_view{"private-user-sentinel"},
                           std::string_view{"private-access-sentinel"},
                           std::string_view{"private-refresh-sentinel"},
                           std::string_view{"private-client-sentinel"},
                           std::string_view{"private-jwt-sentinel"},
                           std::string_view{"private-browser-code-sentinel"},
                           std::string_view{"fresh-private-access-sentinel"},
                           std::string_view{"fresh-private-refresh-sentinel"},
                           std::string_view{"private-invalid-user"},
                           std::string_view{"private-invalid-password"}};
  no_secret(records, secrets);
  std::vector<std::pair<std::uint8_t, std::int64_t>> begin_scalars, poll_scalars,
      owned_scalars;
  std::uint64_t begin = 0, poll = 0, browser = 0, unknown = 0, resolved = 0;
  unsigned http_success = 0, http_unknown = 0, descriptors = 0;
  std::uint64_t error_action = 0;
  std::vector<std::uint64_t> device_actions;
  const auto epoch = records.front().origin.lane;
  struct PendingExchange {
    std::uint32_t returned;
    std::uint64_t owner, request;
    std::uint8_t branch;
    int status;
  };
  std::vector<PendingExchange> exchanges;
  unsigned http_begins = 0, operation_begins = 0;
  bool browser_unknown = false, owner_unknown = false;
  for (const auto &record : records) {
    // selected.request is an application exchange, unlike the private refresh
    // and device HTTP operations. Its public body is now captured explicitly.
    check(record.site < 1400 || record.site >= 1600,
          "private auth child opted into native payload capture");
    if (record.site == 1312) {
      auto application_body = ByteView{record.body}.subspan(24);
      check(native<std::uint8_t>(application_body) == 1 &&
                native<std::size_t>(application_body) == 2 &&
                native<bool>(application_body) &&
                native<std::size_t>(application_body) == 2 &&
                application_body.size() == 2 && application_body[0] == std::byte{'{'} &&
                application_body[1] == std::byte{'}'},
            "private HTTP body mislabeled as public application response");
    }
    ByteView body{record.body};
    if (record.site == 1600) {
      const auto action = native<std::uint8_t>(body);
      if (action == 12) {
        device_actions.push_back(record.ordinal);
        if (device_actions.size() == 1)
          begin = record.ordinal;
        if (device_actions.size() == 2)
          unknown = record.ordinal;
        if (device_actions.size() == 3)
          error_action = record.ordinal;
      }
      if (action == 13)
        poll = record.ordinal;
      if (action == 14)
        resolved = record.ordinal;
      if (action == 16)
        browser = record.ordinal;
    } else if (record.site == 1613) {
      const auto lane = native<IdentityBytes>(body);
      const auto owner = native<std::uint64_t>(body);
      check(lane == epoch, "scalar actual action lane differs");
      const auto field = native<std::uint8_t>(body);
      const auto value = native<std::int64_t>(body);
      check(body.empty(), "scalar native field extent differs");
      if (owner == begin)
        begin_scalars.emplace_back(field, value);
      if (owner == poll)
        poll_scalars.emplace_back(field, value);
      if (owner == resolved)
        owned_scalars.emplace_back(field, value);
    } else if (record.site == 1609 || record.site == 1615) {
      const auto lane = native<IdentityBytes>(body);
      const auto owner = native<std::uint64_t>(body);
      const auto operation = native<std::uint8_t>(body);
      check(lane == epoch && body.empty(),
            "private begin actual action Ref/extent differs");
      if (record.site == 1609) {
        const std::array<std::uint64_t, 5> expected_owners{begin, poll, resolved,
                                                           unknown, error_action};
        const std::array<std::uint8_t, 5> operations{1, 2, 3, 1, 1},
            branches{1, 1, 1, 5, 2};
        const std::array<int, 5> statuses{200, 400, 200, 0, 0};
        check(http_begins < expected_owners.size() &&
                  owner == expected_owners[http_begins] &&
                  operation == operations[http_begins],
              "private HTTP request has wrong actual action Ref/operation");
        exchanges.push_back({1610, owner, record.ordinal, branches[http_begins],
                             statuses[http_begins]});
        ++http_begins;
      } else {
        check(owner == browser && operation == 4 && operation_begins++ == 0,
              "private PKCE request has wrong action Ref/operation");
        exchanges.push_back({1616, owner, record.ordinal, 1, 0});
      }
    } else if (record.site == 1610 || record.site == 1616) {
      const auto action_lane = native<IdentityBytes>(body);
      const auto owner = native<std::uint64_t>(body);
      const auto request_lane = native<IdentityBytes>(body);
      const auto request = native<std::uint64_t>(body);
      const auto branch = native<std::uint8_t>(body);
      const auto found = std::find_if(
          exchanges.begin(), exchanges.end(),
          [&](const PendingExchange &pending) { return pending.request == request; });
      check(found != exchanges.end() && found->returned == record.site &&
                found->owner == owner && action_lane == epoch &&
                request_lane == epoch && branch == found->branch,
            "private return actual action/request Ref or branch differs");
      if (branch == 1 && record.site == 1610) {
        check(native<int>(body) == found->status, "private HTTP native status differs");
        ++http_success;
      }
      if (branch == 5)
        ++http_unknown;
      if (branch == 2)
        check(native<ErrorCode>(body) == ErrorCode::io &&
                  native<std::int64_t>(body) == ECONNRESET,
              "private HTTP native Error fields differ");
      check(body.empty(), "private return fabricated or omitted native result fields");
      exchanges.erase(found);
    } else if (record.site == 1620) {
      ++descriptors;
      std::vector<std::byte> offered;
      offered.insert(offered.end(), record.body.begin(), record.body.begin() + 24);
      append_text(offered, "public-provider");
      append_text(offered, "https://example.com/v1///");
      append_text(offered, "chat");
      append_text(offered, "PUBLIC_KEY_NAME");
      check(record.body == offered,
            "offered validated descriptor transformed at writer/invalid URL captured");
    } else if (record.site == 1608) {
      body = body.subspan(16);
      const auto owner = native<std::uint64_t>(body);
      const auto branch = native<std::uint8_t>(body);
      if (owner == browser)
        browser_unknown = branch == 5;
      if (owner == unknown)
        owner_unknown = branch == 5;
    }
  }
  const std::vector<std::pair<std::uint8_t, std::int64_t>> expected_begin{
      {8, 120}, {9, 5}, {10, 0}, {18, 100}, {7, 220}, {19, 100}, {17, 105}};
  const std::vector<std::pair<std::uint8_t, std::int64_t>> expected_poll{
      {20, 105}, {10, 0}, {11, 0},  {12, 220}, {13, 105}, {14, 5},
      {17, 110}, {14, 5}, {15, 10}, {16, 10},  {21, 105}, {17, 115}};
  check(begin_scalars == expected_begin && poll_scalars == expected_poll,
        "actual device native scalar acquisition sequence differs");
  const std::vector<std::pair<std::uint8_t, std::int64_t>> expected_owned{
      {28, 1},   {29, 1},  {23, 225}, {24, 220}, {25, 0},
      {26, 220}, {5, 220}, {6, 120},  {7, 340}};
  check(owned_scalars == expected_owned,
        "owned authorization native acquisitions differ");
  check(http_begins == 5 && operation_begins == 1 && exchanges.empty(),
        "private operation lacks exactly one return per actual begin");
  check(descriptors == 1, "valid/invalid offered descriptor boundary differs");
  check(http_success == 3 && http_unknown == 1 && browser_unknown && owner_unknown,
        "private request/observer explicit unknown terminals missing");
}

int native_codex_helper() {
  std::string line;
  if (!std::getline(std::cin, line) ||
      line.find("\"method\":\"initialize\"") == std::string::npos)
    return 101;
  std::cout << "{\"id\":\"init\",\"result\":{}}\n" << std::flush;
  if (!std::getline(std::cin, line) ||
      line.find("\"method\":\"initialized\"") == std::string::npos)
    return 102;
  if (!std::getline(std::cin, line) ||
      line.find("\"refreshToken\":true") == std::string::npos)
    return 103;
  std::cout << "{\"id\":\"auth\",\"result\":{\"authMethod\":\"chatgpt\",\"authToken\":"
               "\"private-codex-access-sentinel\"}}\n"
            << std::flush;
  return 0;
}
int native_curl_helper() {
  std::string input{std::istreambuf_iterator<char>{std::cin},
                    std::istreambuf_iterator<char>{}};
  if (input.find("Bearer private-codex-access-sentinel") == std::string::npos ||
      input.find("ChatGPT-Account-ID: private-codex-account-sentinel") ==
          std::string::npos)
    return 104;
  std::cout << "{\"models\":[]}\n200" << std::flush;
  return 0;
}
void codex_originals(const std::filesystem::path &directory,
                     const std::string &executable) {
  const auto run = directory / "codex-capture", home = directory / "codex-private";
  prepare_directory(run);
  prepare_directory(home);
  {
    std::ofstream fixture{home / "auth.json"};
    fixture
        << R"({"auth_mode":"chatgpt","tokens":{"access_token":"private-codex-access-sentinel","account_id":"private-codex-account-sentinel"}})";
  }
  check(::chmod((home / "auth.json").c_str(), 0600) == 0,
        "native private codex fixture mode");
  {
    auto runtime = unwrap(RawRuntime::open(run.string(), fixture_identity()));
    auto handle = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{handle};
      OpenAiConfig config;
      config.codex_executable = executable;
      config.curl_executable = executable;
      config.codex_home = home;
      auto login = unwrap(codex_login(config, true));
      check(unwrap(openai_http(config, login, "models?client_version=0.160.0")) ==
                "{\"models\":[]}",
            "actual compatibility application result changed");
    }
    handle.reset();
  }
  const auto records = read_records(run);
  const std::array<std::uint32_t, 15> expected{1600, 1601, 1614, 1617, 1618,
                                               1615, 1616, 1615, 1616, 1615,
                                               1616, 1617, 1618, 1607, 1608};
  check(records.size() > expected.size(), "actual Codex/application records missing");
  for (std::size_t index = 0; index < expected.size(); ++index)
    check(records[index].site == expected[index],
          "private Codex read/RPC whole operation sequence changed");
  const auto epoch = records[0].origin.lane;
  const std::array<std::size_t, 5> request_indices{3, 5, 7, 9, 11};
  const std::array<std::uint8_t, 5> operations{1, 1, 2, 3, 2};
  for (std::size_t pair = 0; pair < request_indices.size(); ++pair) {
    const auto index = request_indices[pair];
    std::vector<std::byte> entered, returned;
    append_ref(entered, epoch, 1);
    append(entered, operations[pair]);
    append_ref(returned, epoch, 1);
    append_ref(returned, epoch, index + 1);
    append(returned, std::uint8_t{1});
    check(records[index].ordinal == index + 1 && records[index].body == entered &&
              records[index + 1].ordinal == index + 2 &&
              records[index + 1].body == returned,
          "Codex exact action/request Ref/operation/return body differs");
    check(records[index].origin.lane == epoch &&
              records[index + 1].origin.lane == epoch &&
              records[index].origin.partition == RawPartition::control &&
              records[index + 1].origin.partition == RawPartition::control,
          "Codex private pair has wrong actual producer/partition");
  }
  const auto application =
      std::find_if(records.begin(), records.end(),
                   [](const Record &record) { return record.site == 1300; });
  const auto linked =
      std::find_if(records.begin(), records.end(),
                   [](const Record &record) { return record.site == 1612; });
  check(application != records.end() && linked != records.end() &&
            linked->ordinal == application->ordinal + 1,
        "actual auth/application link not emitted with application begin");
  std::vector<std::byte> link;
  append_ref(link, epoch, 1);
  append_ref(link, epoch, application->ordinal);
  check(linked->body == link, "actual Codex/application Ref pair differs");
  const std::array secrets{std::string_view{"private-codex-access-sentinel"},
                           std::string_view{"private-codex-account-sentinel"}};
  no_secret(records, secrets);
}
void launcher_native(const std::filesystem::path &directory) {
  const auto credentials = directory / "launcher-credentials",
             metrics = directory / "launcher-metrics";
  const auto child = ::fork();
  check(child >= 0, "native launcher fork");
  if (child == 0) {
    if (::setenv("BLACKBIRD_AUTH_HOME", credentials.c_str(), 1) != 0 ||
        ::setenv("BLACKBIRD_AUTH_METRICS_HOME", metrics.c_str(), 1) != 0)
      ::_exit(120);
    const int nullfd = ::open("/dev/null", O_RDWR);
    if (nullfd < 0)
      ::_exit(121);
    (void)::dup2(nullfd, STDOUT_FILENO);
    const int errors = ::open((directory / "launcher-error.log").c_str(),
                              O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (errors < 0)
      ::_exit(122);
    (void)::dup2(errors, STDERR_FILENO);
    (void)::close(errors);
    (void)::close(nullfd);
    char command[] = "blackbird-auth", action[] = "logout", provider[] = "openai",
         account[] = "account";
    char *args[]{command, action, provider, account};
    ::_exit(provider_auth_cli(4, args));
  }
  int status = 0;
  check(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0,
        "actual launcher mutation failed");
  check(std::filesystem::exists(credentials / "credentials.bbm") &&
            std::filesystem::file_size(metrics / "environment/identity") == 40,
        "standalone mutation lacks durable environment");
  const auto records = read_records(metrics);
  check(records.size() == 11 && records[0].site == 1619 && records[1].site == 1600 &&
            records[3].site == 1600 && records.back().site == 1608,
        "standalone purpose/command/child events absent");
  const auto epoch = records[1].origin.lane;
  ByteView child_begin{records[3].body};
  check(native<std::uint8_t>(child_begin) == 9 && native<bool>(child_begin),
        "launcher logout child wrong owner");
  check(std::equal(epoch.begin(), epoch.end(), child_begin.begin()),
        "launcher child lost actual parent lane");
  child_begin = child_begin.subspan(16);
  check(native<std::uint64_t>(child_begin) == records[1].ordinal,
        "launcher child lost actual command ordinal");
}

void launcher_read_only(const std::filesystem::path &directory) {
  const auto credentials = directory / "readonly-no-credentials",
             metrics = directory / "readonly-no-metrics";
  const auto child = ::fork();
  check(child >= 0, "readonly launcher fork");
  if (child == 0) {
    if (::setenv("BLACKBIRD_AUTH_HOME", credentials.c_str(), 1) != 0 ||
        ::setenv("BLACKBIRD_AUTH_METRICS_HOME", metrics.c_str(), 1) != 0)
      ::_exit(120);
    const int nullfd = ::open("/dev/null", O_RDWR);
    if (nullfd < 0)
      ::_exit(121);
    (void)::dup2(nullfd, STDOUT_FILENO);
    (void)::dup2(nullfd, STDERR_FILENO);
    (void)::close(nullfd);
    char command[] = "blackbird-auth", action[] = "providers";
    char *args[]{command, action};
    const int result = provider_auth_cli(2, args);
    if (result != 0)
      ::_exit(result);
    char status_action[] = "status";
    args[1] = status_action;
    ::_exit(provider_auth_cli(2, args));
  }
  int status = 0;
  check(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0,
        "actual readonly launcher failed");
  check(!std::filesystem::exists(credentials) && !std::filesystem::exists(metrics),
        "status/providers created credential/capture run");
}
void launcher_signal(const std::filesystem::path &directory) {
  const auto credentials = directory / "signal-no-store",
             metrics = directory / "signal-metrics";
  Pipe input, notice, proof;
  const auto child = ::fork();
  check(child >= 0, "signal launcher fork");
  if (child == 0) {
    if (::setenv("BLACKBIRD_AUTH_HOME", credentials.c_str(), 1) != 0 ||
        ::setenv("BLACKBIRD_AUTH_METRICS_HOME", metrics.c_str(), 1) != 0)
      ::_exit(120);
    signal_proof = proof.descriptors[1];
    (void)::dup2(input.descriptors[0], STDIN_FILENO);
    (void)::dup2(notice.descriptors[1], STDERR_FILENO);
    for (const int fd : input.descriptors)
      (void)::close(fd);
    for (const int fd : notice.descriptors)
      (void)::close(fd);
    char command[] = "blackbird-auth", action[] = "key", provider[] = "openai";
    char *args[]{command, action, provider};
    const int result = provider_auth_cli(3, args);
    struct sigaction restored{};
    if (::sigaction(SIGTERM, nullptr, &restored) != 0 || restored.sa_handler != SIG_DFL)
      ::_exit(125);
    ::_exit(result);
  }
  notice.wait();
  check(proof.wait() == 3,
        "actual inherited auth signal pipe missing native CLOEXEC/NONBLOCK flags");
  check(::kill(child, SIGTERM) == 0, "native sticky auth signal delivery");
  int status = 0;
  check(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
            WEXITSTATUS(status) == 1,
        "actual signaled launcher did not return interrupted error");
  check(proof.wait() == 4, "actual signal did not leave inherited raw cancellation "
                           "endpoint sticky/readable");
  check(!std::filesystem::exists(credentials),
        "signaled secret acquisition entered credential Store");
  const auto records = read_records(metrics);
  check(records.size() == 6 && records.back().site == 1608,
        "signal command outcome did not preserve prompt + terminal");
  ByteView body{records.back().body};
  body = body.subspan(24);
  check(native<std::uint8_t>(body) == 2 && native<std::uint8_t>(body) == 100 &&
            native<ErrorCode>(body) == ErrorCode::interrupted,
        "signal Error/held command terminal lost");
}
} // namespace
int main(int argc, char **argv) {
  const auto poll_symbol = ::dlsym(RTLD_NEXT, "poll"),
             fsync_symbol = ::dlsym(RTLD_NEXT, "fsync");
  std::memcpy(&forward_poll, &poll_symbol, sizeof(forward_poll));
  std::memcpy(&forward_fsync, &fsync_symbol, sizeof(forward_fsync));
  if (!forward_poll || !forward_fsync)
    return 3;
  if (argc > 1) {
    if (std::string_view{argv[1]} == "app-server")
      return native_codex_helper();
    if (std::string_view{argv[1]} == "--disable")
      return native_curl_helper();
    return 4;
  }
#ifdef __APPLE__
  char path[] = "/private/tmp/bb-auth-raw-XXXXXX";
#else
  char path[] = "/tmp/bb-auth-raw-XXXXXX";
#endif
  const auto *temporary = ::mkdtemp(path);
  if (!temporary)
    return 2;
  try {
    original_action(temporary);
    save_failure(temporary);
    full_data_interruption(temporary);
    environment_native(temporary);
    environment_native_failures(temporary);
    private_surfaces(temporary);
    codex_originals(temporary, std::filesystem::absolute(argv[0]).string());
    launcher_native(temporary);
    launcher_read_only(temporary);
    launcher_signal(temporary);
    std::filesystem::remove_all(temporary);
    std::cout << "provider_auth_raw: passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << "provider_auth_raw native error: " << static_cast<unsigned>(error.code)
              << '/' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  std::filesystem::remove_all(temporary);
  return 1;
}
