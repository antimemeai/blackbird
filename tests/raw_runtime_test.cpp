#include "blackbird/raw_audit.hpp"
#include "blackbird/raw_runtime.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
std::atomic<bool> watch_status_send{false}, status_sent{false};
}
// Real-kernel barrier: the request is queued to a stopped supervisor before it
// is killed. No timeout or thread-start observation stands in for a pending RPC.
extern "C" ssize_t send(int endpoint, const void *data, std::size_t message_size,
                        int message_flags) {
  const auto arguments = std::pair{message_size, message_flags};
  iovec part{const_cast<void *>(data), arguments.first};
  msghdr message{};
  message.msg_iov = &part;
  message.msg_iovlen = 1;
  const auto result = ::sendmsg(endpoint, &message, arguments.second);
  if (watch_status_send.load() && result == 1 && arguments.first == 1 &&
      *static_cast<const std::byte *>(data) == std::byte{0})
    status_sent.store(true);
  return result;
}

namespace {
using namespace blackbird;
void check(bool condition, int line) {
  if (!condition) {
    std::fprintf(stderr, "raw_runtime:%d\n", line);
    std::abort();
  }
}
#define CHECK(...) check((__VA_ARGS__), __LINE__)
template <class T> T require(Result<T> result) {
  CHECK(result.has_value());
  return std::move(result).value();
}
void require(Result<void> result) { CHECK(result.has_value()); }
struct Directory {
  std::array<char, 64> name{};
  Directory() {
    const std::string pattern = "/tmp/blackbird-raw-runtime-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), name.begin());
    CHECK(::mkdtemp(name.data()) != nullptr);
  }
  ~Directory() { std::filesystem::remove_all(name.data()); }
};
EnvironmentId environment() {
  IdentityBytes identity{};
  identity[0] = std::byte{1};
  return require(EnvironmentId::from_bytes(identity));
}
constexpr RawRuntimeOptions small{{1024 * 1024, 4096, 4096}, 111};
std::filesystem::path run_path(const Directory &directory) {
  auto entries = std::filesystem::directory_iterator{
      std::filesystem::path{directory.name.data()} / "raw"};
  CHECK(entries != std::filesystem::directory_iterator{});
  return entries->path();
}
std::vector<std::byte> file_bytes(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  CHECK(input.good());
  const auto size = input.tellg();
  CHECK(size >= 0);
  input.seekg(0);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  CHECK(input.good());
  return bytes;
}
template <class T> void append(std::vector<std::byte> &output, const T &value) {
  const auto data = std::as_bytes(std::span{&value, std::size_t{1}});
  output.insert(output.end(), data.begin(), data.end());
}
std::vector<std::byte> envelope(std::uint32_t site, std::uint64_t ordinal,
                                ByteView body) {
  std::vector<std::byte> output;
  const auto add = [&](std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) {
      output.push_back(std::byte{static_cast<unsigned char>(value & 255U)});
      value >>= 8;
    }
  };
  add(16 + body.size(), 4);
  add(site, 4);
  add(ordinal, 8);
  output.insert(output.end(), body.begin(), body.end());
  return output;
}
struct Recorded {
  RawOrigin origin;
  std::vector<std::byte> raw;
};
std::vector<Recorded> recorded(const std::filesystem::path &path) {
  auto directory = require(NativeJournalDirectory::open(path.string()));
  std::vector<Recorded> records;
  for (std::uint64_t segment = 0;
       std::filesystem::exists(path / ("events." + std::to_string(segment)));
       ++segment) {
    const auto name = "events." + std::to_string(segment);
    const auto header = require(decode_journal_header(
        ByteView{file_bytes(path / name)}.first(journal_header_size)));
    std::vector<RawOrigin> seeds;
    if (segment != 0) {
      const auto seed = file_bytes(path / ("seed." + std::to_string(segment)));
      CHECK(seed.size() >= 8 && (seed.size() - 8) % 32 == 0);
      for (std::size_t offset = 8; offset < seed.size(); offset += 32) {
        RawOrigin origin{};
        std::memcpy(origin.lane.data(), seed.data() + offset, 16);
        std::memcpy(&origin.partition, seed.data() + offset + 16, 4);
        std::memcpy(&origin.source_start, seed.data() + offset + 24, 8);
        seeds.push_back(origin);
      }
    }
    auto audit = require(
        RawAudit::open(directory, name, header, 4096, SyncStrength::full, seeds));
    std::vector<std::byte> buffer(header.limits.max_payload);
    auto cursor = audit->begin();
    for (;;) {
      auto read = require(audit->read(cursor, buffer));
      if (!read)
        break;
      records.push_back({read->origin, {read->raw.begin(), read->raw.end()}});
      cursor = read->next;
    }
  }
  return records;
}
template <class Predicate> void until(Predicate predicate) {
  const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds{8};
  while (!predicate()) {
    CHECK(std::chrono::steady_clock::now() < limit);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
}
void large_record_restart_and_closure() {
  Directory directory;
  IdentityBytes epoch{};
  std::vector<std::byte> body(300 * 1024);
  for (std::size_t i = 0; i < body.size(); ++i)
    body[i] = std::byte{static_cast<unsigned char>((i * 29 + 73) & 255U)};
  {
    auto runtime =
        require(RawRuntime::open(directory.name.data(), environment(), small));
    RawThreadBinding binding{require(runtime->prepare_thread())};
    epoch = raw_lane_epoch();
    const auto status = require(runtime->status());
    CHECK(::kill(status.recorder, SIGSTOP) == 0);
    const std::array<ByteView, 1> parts{body};
    CHECK(require(raw_emit(9000, parts)) == 1);
    CHECK(::kill(status.recorder, SIGKILL) == 0);
    until([&] {
      const auto now = require(runtime->status());
      return now.recorder != status.recorder && !now.fenced;
    });
    CHECK(require(raw_emit(9000, parts)) == 2);
    for (unsigned i = 0; i < 8; ++i) {
      auto context = require(raw_prepare_thread());
      std::thread worker{[lane = std::move(context)] {
        RawThreadBinding active{lane};
        CHECK(require(raw_emit(9001, {})) == 1);
      }};
      worker.join();
    }
  }
  const auto path = run_path(directory);
  CHECK(std::filesystem::exists(path / "closed"));
  const auto records = recorded(path);
  CHECK(records.size() == 10);
  unsigned found = 0;
  std::set<IdentityBytes> worker_epochs;
  for (const auto &record : records) {
    if (record.origin.lane == epoch) {
      CHECK(record.raw == envelope(9000, ++found, body));
      CHECK(record.origin.source_start ==
            (found - 1) * static_cast<std::uint64_t>(body.size() + 16));
    } else {
      CHECK(record.origin.partition == RawPartition::data &&
            record.origin.source_start == 0);
      CHECK(record.raw == envelope(9001, 1, {}));
      CHECK(worker_epochs.insert(record.origin.lane).second);
    }
  }
  CHECK(found == 2 && worker_epochs.size() == 8);
}
void recovered_empty_source_republishes_cursor() {
  Directory directory;
  auto runtime = require(RawRuntime::open(directory.name.data(), environment(), small));
  RawThreadBinding binding{require(runtime->prepare_thread())};
  const auto path = run_path(directory);
  until([&] { return std::filesystem::exists(path / "published"); });
  auto status = require(runtime->status());
  CHECK(::kill(status.supervisor, SIGSTOP) == 0);
  CHECK(::kill(status.recorder, SIGSTOP) == 0);
  // No source bytes remain pending. A missing cursor must be repaired by
  // recovery itself, without needing an additional event to trigger append.
  std::filesystem::remove(path / "published");
  CHECK(::kill(status.recorder, SIGKILL) == 0);
  CHECK(::kill(status.supervisor, SIGCONT) == 0);
  until([&] {
    return require(runtime->status()).recorder != status.recorder &&
           std::filesystem::exists(path / "published");
  });
  const auto published = file_bytes(path / "published");
  CHECK(published.size() == 40);
  std::uint64_t sequence = 1, offset = 0;
  std::memcpy(&sequence, published.data() + 24, 8);
  std::memcpy(&offset, published.data() + 32, 8);
  CHECK(sequence == 0 && offset == journal_header_size);
}
void exact_startup_failure_credit_and_recovery() {
  Directory directory;
  IdentityBytes epoch{};
  std::filesystem::path path;
  const std::array<std::byte, 3> body{std::byte{19}, std::byte{0}, std::byte{255}};
  {
    auto runtime =
        require(RawRuntime::open(directory.name.data(), environment(), small));
    RawThreadBinding binding{require(runtime->prepare_thread())};
    epoch = raw_lane_epoch();
    path = run_path(directory);
    const auto status = require(runtime->status());
    CHECK(::kill(status.supervisor, SIGSTOP) == 0);
    CHECK(::kill(status.recorder, SIGSTOP) == 0);
    const std::array<ByteView, 1> parts{body};
    CHECK(require(raw_emit(9010, parts)) == 1);
    const auto offline = path.string() + ".offline";
    std::filesystem::rename(path, offline);
    CHECK(::kill(status.recorder, SIGKILL) == 0);
    CHECK(::kill(status.supervisor, SIGCONT) == 0);
    until([&] { return require(runtime->status()).fenced; });
    std::filesystem::rename(offline, path);
    require(runtime->recover());
    CHECK(require(raw_emit(9010, parts)) == 2);
  }
  const auto header = require(decode_journal_header(file_bytes(path / "lanes")));
  const auto records = recorded(path);
  CHECK(records.size() == 3);
  unsigned failures = 0, originals = 0;
  for (const auto &record : records) {
    if (record.origin.lane == epoch) {
      CHECK(record.raw == envelope(9010, ++originals, body));
      continue;
    }
    std::vector<std::byte> expected;
    append(expected, ErrorCode::io);
    append(expected, std::int64_t{ENOENT});
    append(expected, std::uint64_t{2});
    append(expected, std::uint64_t{0});
    append(expected, std::uint64_t{0});
    append(expected, false);
    expected.insert(expected.end(), header.journal.bytes().begin(),
                    header.journal.bytes().end());
    const IdentityBytes zero{};
    expected.insert(expected.end(), zero.begin(), zero.end());
    append(expected, RawPartition::data);
    append(expected, std::uint64_t{0});
    append(expected, std::uint64_t{0});
    append(expected, false);
    CHECK(record.origin.partition == RawPartition::failure);
    CHECK(record.raw == envelope(8000, 1, expected));
    ++failures;
  }
  CHECK(failures == 1 && originals == 2);
}
void supervisor_death_during_rpc() {
  Directory directory;
  auto runtime = require(RawRuntime::open(directory.name.data(), environment(), small));
  const auto status = require(runtime->status());
  CHECK(::kill(status.supervisor, SIGSTOP) == 0);
  int stopped = 0;
  pid_t observed;
  do {
    observed = ::waitpid(status.supervisor, &stopped, WUNTRACED);
  } while (observed < 0 && errno == EINTR);
  CHECK(observed == status.supervisor && WIFSTOPPED(stopped) &&
        WSTOPSIG(stopped) == SIGSTOP);
  watch_status_send.store(true);
  status_sent.store(false);
  std::optional<Error> failure;
  std::thread requester{[&] {
    auto response = runtime->status();
    CHECK(!response.has_value());
    failure = response.error();
  }};
  until([&] { return status_sent.load(); });
  // The observed-stopped supervisor cannot reap its recorder. Kill that child
  // while its identity is pinned, before parent death can make it exit on EOF.
  // This separate socket still has the actual queued supervisor RPC above.
  CHECK(::kill(status.recorder, SIGKILL) == 0);
  CHECK(::kill(status.supervisor, SIGKILL) == 0);
  requester.join();
  watch_status_send.store(false);
  CHECK(failure && failure->code == ErrorCode::audit_unavailable);
  // No source producer was activated; unlink only this fixture's named lanes.
  for (const auto &entry : std::filesystem::directory_iterator{run_path(directory)})
    if (entry.path().filename().string().starts_with("lane.")) {
      const auto registration = file_bytes(entry.path());
      CHECK(registration.size() == 80);
      std::array<char, 32> name{};
      std::memcpy(name.data(), registration.data() + 40, 32);
      require(RawChannel::unlink(name.data()));
    }
}
void all_holders_die_then_manifest_reopen() {
  Directory directory;
  std::array<int, 2> reports{}, commands{};
  CHECK(::pipe(reports.data()) == 0 && ::pipe(commands.data()) == 0);
  const std::array<std::byte, 5> body{std::byte{0}, std::byte{73}, std::byte{255},
                                      std::byte{0}, std::byte{7}};
  const auto producer = ::fork();
  CHECK(producer >= 0);
  if (producer == 0) {
    ::close(reports[0]);
    ::close(commands[1]);
    auto runtime =
        require(RawRuntime::open(directory.name.data(), environment(), small));
    RawThreadBinding binding{require(runtime->prepare_thread())};
    const auto status = require(runtime->status());
    const std::array<pid_t, 2> holders{status.supervisor, status.recorder};
    CHECK(::write(reports[1], holders.data(), sizeof(holders)) ==
          static_cast<ssize_t>(sizeof(holders)));
    char command = 0;
    CHECK(::read(commands[0], &command, 1) == 1);
    const std::array<ByteView, 1> parts{body};
    CHECK(require(raw_emit(9020, parts)) == 1);
    CHECK(::write(reports[1], &command, 1) == 1);
    CHECK(::read(commands[0], &command, 1) == 1);
    ::_exit(99);
  }
  ::close(reports[1]);
  ::close(commands[0]);
  std::array<pid_t, 2> holders{};
  CHECK(::read(reports[0], holders.data(), sizeof(holders)) ==
        static_cast<ssize_t>(sizeof(holders)));
  CHECK(::kill(holders[0], SIGSTOP) == 0 && ::kill(holders[1], SIGSTOP) == 0);
  const char command = 1;
  CHECK(::write(commands[1], &command, 1) == 1);
  char published = 0;
  CHECK(::read(reports[0], &published, 1) == 1);
  CHECK(::kill(holders[1], SIGKILL) == 0 && ::kill(holders[0], SIGKILL) == 0 &&
        ::kill(producer, SIGKILL) == 0);
  int status = 0;
  CHECK(::waitpid(producer, &status, 0) == producer && WIFSIGNALED(status));
  ::close(reports[0]);
  ::close(commands[1]);
  const auto old_path = run_path(directory);
  // SIGKILL delivery is not process exit. Wait for every holder to release
  // the real catalogue lease before asking a fresh process to recover it.
  const int lease = ::open((old_path / "lanes").c_str(), O_RDWR | O_CLOEXEC);
  CHECK(lease >= 0);
  until([&] { return ::flock(lease, LOCK_EX | LOCK_NB) == 0; });
  CHECK(::close(lease) == 0);
  {
    auto reopened =
        require(RawRuntime::open(directory.name.data(), environment(), small));
    CHECK(require(reopened->status()).recorder > 0);
  }
  CHECK(std::filesystem::exists(old_path / "closed"));
  const auto records = recorded(old_path);
  CHECK(records.size() == 1 && records[0].raw == envelope(9020, 1, body));
}
} // namespace
int main() {
  std::fputs("raw_runtime: large event, restart and worker closure\n", stderr);
  large_record_restart_and_closure();
  std::fputs("raw_runtime: recovery publishes an already drained cursor\n", stderr);
  recovered_empty_source_republishes_cursor();
  std::fputs("raw_runtime: one-slot failure and recovery\n", stderr);
  exact_startup_failure_credit_and_recovery();
  std::fputs("raw_runtime: supervisor death during actual RPC\n", stderr);
  supervisor_death_during_rpc();
  std::fputs("raw_runtime: all-holder death and manifest reopen\n", stderr);
  all_holders_die_then_manifest_reopen();
  CHECK(!raw_enabled());
}
