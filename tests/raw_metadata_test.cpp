#include "blackbird/raw_audit.hpp"
#include "blackbird/raw_runtime.hpp"

#include <array>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
std::atomic<bool> fail_metadata_read{false};
std::atomic<unsigned> failed_metadata_reads{0};
ino_t failed_inode = 0;
dev_t failed_device = 0;
} // namespace
extern "C" ssize_t read(int descriptor, void *body, std::size_t size) {
  if (fail_metadata_read.load()) {
    struct stat info{};
    if (::fstat(descriptor, &info) == 0 && info.st_ino == failed_inode &&
        info.st_dev == failed_device) {
      ++failed_metadata_reads;
      errno = EIO;
      return -1;
    }
  }
  iovec part{body, size};
  return ::readv(descriptor, &part, 1);
}

namespace {
using namespace blackbird;
void check(bool condition, int line) {
  if (!condition) {
    std::fprintf(stderr, "raw_metadata:%d\n", line);
    std::abort();
  }
}
#define CHECK(...) check((__VA_ARGS__), __LINE__)
template <class T> T require(Result<T> result) {
  if (!result.has_value())
    std::fprintf(stderr, "raw_metadata error %s:%lld\n",
                 error_name(result.error().code),
                 static_cast<long long>(result.error().detail));
  CHECK(result.has_value());
  return std::move(result).value();
}
void require(Result<void> result) { CHECK(result.has_value()); }
constexpr RawRuntimeOptions small{{4096, 4096, 4096}, 4096};
EnvironmentId environment() {
  IdentityBytes value{};
  value[0] = std::byte{19};
  return require(EnvironmentId::from_bytes(value));
}
std::vector<std::byte> read(const std::filesystem::path &path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  CHECK(input.good());
  const auto size = input.tellg();
  CHECK(size >= 0);
  std::vector<std::byte> output(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char *>(output.data()),
             static_cast<std::streamsize>(output.size()));
  CHECK(input.good());
  return output;
}
void write(const std::filesystem::path &path, ByteView input) {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  output.write(reinterpret_cast<const char *>(input.data()),
               static_cast<std::streamsize>(input.size()));
  CHECK(output.good());
}
struct Directory {
  std::array<char, 64> name{};
  Directory() {
    const std::string pattern = "/tmp/blackbird-raw-metadata-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), name.begin());
    CHECK(::mkdtemp(name.data()) != nullptr);
  }
  ~Directory() {
    // Only this fixture's names; incomplete recovery intentionally retains them.
    const auto raw = std::filesystem::path{name.data()} / "raw";
    if (std::filesystem::exists(raw))
      for (const auto &run : std::filesystem::directory_iterator{raw})
        for (const auto &file : std::filesystem::directory_iterator{run.path()}) {
          const auto filename = file.path().filename().string();
          if (!filename.starts_with("lane.") && !filename.starts_with("registered."))
            continue;
          const auto registration = read(file.path());
          if (registration.size() != 80)
            continue;
          std::array<char, 32> lane{};
          std::memcpy(lane.data(), registration.data() + 40, 32);
          if (lane[0] == '/' && lane.back() == 0) {
            const auto removed = RawChannel::unlink(lane.data());
            CHECK(removed.has_value() || (removed.error().code == ErrorCode::io &&
                                          removed.error().detail == ENOENT));
          }
        }
    std::filesystem::remove_all(name.data());
  }
};
std::filesystem::path sole_run(const Directory &directory) {
  const auto raw = std::filesystem::path{directory.name.data()} / "raw";
  auto entries = std::filesystem::directory_iterator{raw};
  CHECK(entries != std::filesystem::directory_iterator{});
  const auto path = entries->path();
  CHECK(++entries == std::filesystem::directory_iterator{});
  return path;
}
void transfer(int descriptor, void *body, std::size_t bytes, bool send) {
  const auto result =
      send ? ::write(descriptor, body, bytes) : ::read(descriptor, body, bytes);
  CHECK(result == static_cast<ssize_t>(bytes));
}
const std::array<std::byte, 7> original{std::byte{0}, std::byte{255}, std::byte{19},
                                        std::byte{0}, std::byte{87},  std::byte{2},
                                        std::byte{1}};
struct CrashRun {
  Directory directory;
  std::filesystem::path path;
  IdentityBytes epoch{};
  std::filesystem::path primary;
  std::vector<std::byte> registration;
  CrashRun() {
    std::array<int, 2> reports{}, commands{};
    CHECK(::pipe(reports.data()) == 0 && ::pipe(commands.data()) == 0);
    const auto producer = ::fork();
    CHECK(producer >= 0);
    if (producer == 0) {
      ::close(reports[0]);
      ::close(commands[1]);
      auto runtime =
          require(RawRuntime::open(directory.name.data(), environment(), small));
      RawThreadBinding binding{require(runtime->prepare_thread())};
      const auto status = require(runtime->status());
      auto identity = raw_lane_epoch();
      std::array<pid_t, 2> holders{status.supervisor, status.recorder};
      transfer(reports[1], holders.data(), sizeof(holders), true);
      transfer(reports[1], identity.data(), identity.size(), true);
      char signal = 0;
      transfer(commands[0], &signal, 1, false);
      const std::array<ByteView, 1> parts{original};
      CHECK(require(raw_emit(9090, parts)) == 1);
      transfer(reports[1], &signal, 1, true);
      transfer(commands[0], &signal, 1, false);
      ::_exit(98);
    }
    ::close(reports[1]);
    ::close(commands[0]);
    std::array<pid_t, 2> holders{};
    transfer(reports[0], holders.data(), sizeof(holders), false);
    transfer(reports[0], epoch.data(), epoch.size(), false);
    CHECK(::kill(holders[0], SIGSTOP) == 0 && ::kill(holders[1], SIGSTOP) == 0);
    char signal = 1;
    transfer(commands[1], &signal, 1, true);
    transfer(reports[0], &signal, 1, false);
    CHECK(::kill(holders[1], SIGKILL) == 0 && ::kill(holders[0], SIGKILL) == 0 &&
          ::kill(producer, SIGKILL) == 0);
    int status = 0;
    CHECK(::waitpid(producer, &status, 0) == producer && WIFSIGNALED(status));
    ::close(reports[0]);
    ::close(commands[1]);
    path = sole_run(directory);
    const int lease = ::open((path / "lanes").c_str(), O_RDWR | O_CLOEXEC);
    CHECK(lease >= 0 && ::flock(lease, LOCK_EX) == 0);
    CHECK(::close(lease) == 0);
    for (const auto &file : std::filesystem::directory_iterator{path})
      if (file.path().filename().string().starts_with("lane.")) {
        const auto body = read(file.path());
        if (body.size() == 80 && std::equal(epoch.begin(), epoch.end(), body.begin())) {
          primary = file.path();
          registration = body;
        }
      }
    CHECK(!primary.empty());
  }
  std::filesystem::path duplicate() const {
    return path / ("registered." + primary.filename().string().substr(5));
  }
  RawChannel channel() const {
    std::array<char, 32> name{};
    std::memcpy(name.data(), registration.data() + 40, 32);
    return require(RawChannel::open(name.data(), epoch, small.producer_capacities));
  }
  ~CrashRun() {
    std::array<char, 32> name{};
    std::memcpy(name.data(), registration.data() + 40, 32);
    const auto removed = RawChannel::unlink(name.data());
    CHECK(removed.has_value() ||
          (removed.error().code == ErrorCode::io && removed.error().detail == ENOENT));
  }
};
struct Record {
  RawOrigin origin;
  std::vector<std::byte> bytes;
};
std::vector<Record> records(const std::filesystem::path &path) {
  auto directory = require(NativeJournalDirectory::open(path.string()));
  const auto physical = read(path / "events.0");
  const auto header = require(decode_journal_header(ByteView{physical}.first(112)));
  const auto publication = read(path / "published");
  CHECK(publication.size() == 40);
  IdentityBytes identity{};
  std::uint64_t segment = 1, sequence = 0, offset = 0;
  std::memcpy(&segment, publication.data(), 8);
  std::memcpy(identity.data(), publication.data() + 8, 16);
  std::memcpy(&sequence, publication.data() + 24, 8);
  std::memcpy(&offset, publication.data() + 32, 8);
  CHECK(segment == 0 && identity == header.journal.bytes());
  auto audit = require(RawAudit::open_reader(directory, "events.0", header,
                                             {header.journal, sequence, offset}));
  auto cursor = audit->begin();
  std::array<std::byte, 8192> buffer{};
  std::vector<Record> output;
  while (const auto event = require(audit->read(cursor, buffer))) {
    output.push_back({event->origin, {event->raw.begin(), event->raw.end()}});
    cursor = event->next;
  }
  return output;
}
template <class T> void append(std::vector<std::byte> &output, const T &value) {
  const auto native = std::as_bytes(std::span{&value, std::size_t{1}});
  output.insert(output.end(), native.begin(), native.end());
}
std::vector<std::byte> envelope(std::uint32_t site, ByteView body,
                                std::uint64_t ordinal = 1) {
  std::vector<std::byte> output;
  const auto number = [&](std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) {
      output.push_back(std::byte{static_cast<unsigned char>(value & 255U)});
      value >>= 8;
    }
  };
  number(16 + body.size(), 4);
  number(site, 4);
  number(ordinal, 8);
  output.insert(output.end(), body.begin(), body.end());
  return output;
}
void preserved_original(const CrashRun &run) {
  const auto captured = records(run.path);
  CHECK(captured.size() == 1);
  CHECK(captured[0].origin.lane == run.epoch &&
        captured[0].origin.partition == RawPartition::data &&
        captured[0].origin.source_start == 0);
  CHECK(captured[0].bytes == envelope(9090, original));
}
void bootstrap() {
  Directory directory;
  const auto remnant = std::filesystem::path{directory.name.data()} / "raw" / "remnant";
  std::filesystem::create_directories(remnant);
  const std::array<std::byte, 12> marker{std::byte{'B'}, std::byte{'B'}, std::byte{'R'},
                                         std::byte{'A'}, std::byte{'W'}, std::byte{'B'},
                                         std::byte{'O'}, std::byte{'O'}, std::byte{1},
                                         std::byte{0},   std::byte{0},   std::byte{0}};
  write(remnant / "bootstrap", marker);
  write(remnant / "lanes", original);
  const auto dead = ::fork();
  CHECK(dead >= 0);
  if (dead == 0)
    ::_exit(0);
  int status = 0;
  CHECK(::waitpid(dead, &status, 0) == dead);
  write(remnant / "owner", std::as_bytes(std::span{&dead, std::size_t{1}}));
  {
    auto runtime =
        require(RawRuntime::open(directory.name.data(), environment(), small));
    CHECK(require(runtime->status()).recorder > 0);
  }
  CHECK(read(remnant / "lanes") ==
        std::vector<std::byte>(original.begin(), original.end()));
  CHECK(read(remnant / "bootstrap") ==
        std::vector<std::byte>(marker.begin(), marker.end()));
  CHECK(!std::filesystem::exists(remnant / "active") &&
        !std::filesystem::exists(remnant / "closed"));
}
void missing_source() {
  CrashRun run;
  std::array<char, 32> name{};
  std::memcpy(name.data(), run.registration.data() + 40, 32);
  require(RawChannel::unlink(name.data()));
  const auto recovered =
      RawRuntime::open(run.directory.name.data(), environment(), small);
  CHECK(!recovered.has_value() &&
        recovered.error().code == ErrorCode::audit_unavailable);
  CHECK(!std::filesystem::exists(run.path / "closed"));
  const auto header = require(decode_journal_header(read(run.path / "lanes")));
  std::vector<std::byte> failure;
  append(failure, ErrorCode::io);
  append(failure, std::int64_t{ENOENT});
  append(failure, std::uint64_t{2});
  append(failure, std::uint64_t{0});
  append(failure, std::uint64_t{0});
  append(failure, false);
  failure.insert(failure.end(), header.journal.bytes().begin(),
                 header.journal.bytes().end());
  failure.insert(failure.end(), run.epoch.begin(), run.epoch.end());
  append(failure, RawPartition::data);
  append(failure, std::uint64_t{0});
  append(failure, std::uint64_t{0});
  append(failure, false);
  const auto first = records(run.path);
  CHECK(first.size() == 1 && first[0].bytes == envelope(8000, failure));
  CHECK(first[0].origin.partition == RawPartition::failure &&
        first[0].origin.source_start == 0);
  // A later retry owns a new epoch/ordinal, while retaining the old failure.
  const auto retried =
      RawRuntime::open(run.directory.name.data(), environment(), small);
  CHECK(!retried.has_value() && retried.error().code == ErrorCode::audit_unavailable);
  const auto second = records(run.path);
  CHECK(second.size() == 2 && second[0].bytes == first[0].bytes &&
        second[1].bytes == first[0].bytes &&
        second[0].origin.lane != second[1].origin.lane);
  CHECK(!std::filesystem::exists(run.path / "closed"));
  unsigned retained = 0;
  for (const auto &file : std::filesystem::directory_iterator{run.path})
    if (file.path().filename().string().starts_with("lane.")) {
      const auto registration = read(file.path());
      CHECK(registration.size() == 80);
      IdentityBytes identity{};
      RawCapacities capacities{};
      std::array<char, 32> named{};
      std::memcpy(identity.data(), registration.data(), 16);
      std::memcpy(capacities.data(), registration.data() + 16, 24);
      std::memcpy(named.data(), registration.data() + 40, 32);
      if (identity != run.epoch) {
        CHECK(RawChannel::open(named.data(), identity, capacities).has_value());
        ++retained;
      }
    }
  CHECK(retained == 3);
}
void independent_copies() {
  CrashRun run;
  const auto header = read(run.path / "lanes");
  const auto independent = read(run.duplicate());
  CHECK(independent == run.registration);
  write(run.path / "lanes", original);
  write(run.primary, original);
  {
    auto runtime =
        require(RawRuntime::open(run.directory.name.data(), environment(), small));
    CHECK(require(runtime->status()).recorder > 0);
  }
  CHECK(std::filesystem::exists(run.path / "closed"));
  CHECK(read(run.path / "lanes") ==
        std::vector<std::byte>(original.begin(), original.end()));
  CHECK(read(run.primary) == std::vector<std::byte>(original.begin(), original.end()));
  CHECK(read(run.path / "active") == header && read(run.duplicate()) == independent);
  preserved_original(run);
}
void invalid_copies() {
  CrashRun run;
  write(run.primary, original);
  write(run.duplicate(), original);
  const auto reopened =
      RawRuntime::open(run.directory.name.data(), environment(), small);
  CHECK(!reopened.has_value() && reopened.error().code == ErrorCode::corrupt);
  CHECK(!std::filesystem::exists(run.path / "closed"));
  const auto channel = run.channel();
  const auto cursor = require(channel.cursors(RawPartition::data));
  CHECK(cursor.published == 23 && cursor.acknowledged == 0);
  CHECK(read(run.primary) == std::vector<std::byte>(original.begin(), original.end()) &&
        read(run.duplicate()) == read(run.primary));
}
void conflicting_copies() {
  CrashRun run;
  auto conflict = run.registration;
  // A different, structurally valid capacity is never a repair of the first.
  const std::uint64_t doubled = 8192;
  std::memcpy(conflict.data() + 16, &doubled, 8);
  write(run.duplicate(), conflict);
  const auto reopened =
      RawRuntime::open(run.directory.name.data(), environment(), small);
  CHECK(!reopened.has_value() && reopened.error().code == ErrorCode::conflict);
  CHECK(read(run.primary) == run.registration && read(run.duplicate()) == conflict);
  CHECK(require(run.channel().cursors(RawPartition::data)).acknowledged == 0);
}
void conflicting_headers() {
  CrashRun run;
  auto header = require(decode_journal_header(read(run.path / "active")));
  ++header.issuer_namespace;
  const auto altered = require(encode_journal_header(header));
  write(run.path / "active", altered);
  const auto reopened =
      RawRuntime::open(run.directory.name.data(), environment(), small);
  CHECK(!reopened.has_value() && reopened.error().code == ErrorCode::conflict);
  CHECK(read(run.path / "active") ==
        std::vector<std::byte>(altered.begin(), altered.end()));
  CHECK(require(run.channel().cursors(RawPartition::data)).acknowledged == 0);
}
void missing_lease() {
  CrashRun run;
  std::filesystem::remove(run.path / "lanes");
  const auto reopened =
      RawRuntime::open(run.directory.name.data(), environment(), small);
  CHECK(!reopened.has_value() && reopened.error().code == ErrorCode::io &&
        reopened.error().detail == ENOENT);
  CHECK(!std::filesystem::exists(run.path / "lanes") &&
        !std::filesystem::exists(run.path / "closed"));
  CHECK(require(run.channel().cursors(RawPartition::data)).published == 23);
}
void missing_activation() {
  CrashRun run;
  std::filesystem::remove(run.path / "active");
  const auto reopened =
      RawRuntime::open(run.directory.name.data(), environment(), small);
  CHECK(!reopened.has_value() && reopened.error().code == ErrorCode::audit_unavailable);
  CHECK(!std::filesystem::exists(run.path / "closed"));
  const auto current = require(run.channel().cursors(RawPartition::data));
  CHECK(current.published == 23 && current.acknowledged == 0);
}
void unreadable_copy() {
  CrashRun run;
  struct stat info{};
  CHECK(::stat((run.path / "lanes").c_str(), &info) == 0);
  failed_inode = info.st_ino;
  failed_device = info.st_dev;
  failed_metadata_reads.store(0);
  fail_metadata_read.store(true);
  const auto reopened =
      RawRuntime::open(run.directory.name.data(), environment(), small);
  fail_metadata_read.store(false);
  CHECK(failed_metadata_reads.load() == 1);
  CHECK(!reopened.has_value() && reopened.error().code == ErrorCode::io &&
        reopened.error().detail == EIO);
  CHECK(!std::filesystem::exists(run.path / "closed"));
  CHECK(require(run.channel().cursors(RawPartition::data)).acknowledged == 0);
}
void one_closure_copy() {
  CrashRun run;
  auto channel = run.channel();
  const auto raw = envelope(9090, original);
  {
    auto directory = require(NativeJournalDirectory::open(run.path.string()));
    const auto header = require(decode_journal_header(read(run.path / "lanes")));
    auto audit = require(RawAudit::open(directory, "events.0", header));
    const RawAuditRecord event{{run.epoch, RawPartition::data, 0}, raw};
    require(audit->append(std::span{&event, std::size_t{1}}));
    require(channel.acknowledge(RawPartition::data, raw.size()));
  }
  auto closure = run.registration;
  closure[72] = std::byte{1};
  const auto suffix = run.primary.filename().string().substr(5);
  write(run.path / ("retired." + suffix), closure);
  CHECK(!std::filesystem::exists(run.path / ("closed." + suffix)) &&
        !std::filesystem::exists(run.path / "sealed.0"));
  std::array<char, 32> name{};
  std::memcpy(name.data(), run.registration.data() + 40, 32);
  {
    auto reopened =
        require(RawRuntime::open(run.directory.name.data(), environment(), small));
    CHECK(require(reopened->status()).recorder > 0);
  }
  CHECK(std::filesystem::exists(run.path / "closed"));
  const auto unmapped =
      RawChannel::open(name.data(), run.epoch, small.producer_capacities);
  CHECK(!unmapped.has_value() && unmapped.error().code == ErrorCode::io &&
        unmapped.error().detail == ENOENT);
  preserved_original(run);
}
void global_closure_interrupted_unlink() {
  CrashRun run;
  auto channel = run.channel();
  const auto raw = envelope(9090, original);
  {
    auto directory = require(NativeJournalDirectory::open(run.path.string()));
    const auto header = require(decode_journal_header(read(run.path / "lanes")));
    auto audit = require(RawAudit::open(directory, "events.0", header));
    const RawAuditRecord event{{run.epoch, RawPartition::data, 0}, raw};
    require(audit->append(std::span{&event, std::size_t{1}}));
    require(channel.acknowledge(RawPartition::data, raw.size()));
  }
  write(run.path / "closed", {});
  const auto bytes = read(run.path / "events.0");
  const auto metadata = read(run.primary);
  {
    auto reopened =
        require(RawRuntime::open(run.directory.name.data(), environment(), small));
    CHECK(require(reopened->status()).recorder > 0);
  }
  CHECK(read(run.path / "events.0") == bytes && read(run.primary) == metadata);
  std::array<char, 32> name{};
  std::memcpy(name.data(), run.registration.data() + 40, 32);
  const auto unmapped =
      RawChannel::open(name.data(), run.epoch, small.producer_capacities);
  CHECK(!unmapped.has_value() && unmapped.error().code == ErrorCode::io &&
        unmapped.error().detail == ENOENT);
}
void two_missing_one_slot() {
  CrashRun run;
  const auto header = require(decode_journal_header(read(run.path / "lanes")));
  std::array<IdentityBytes, 2> missing{};
  unsigned count = 0;
  for (const auto &entry : std::filesystem::directory_iterator{run.path})
    if (entry.path().filename().string().starts_with("lane.")) {
      const auto body = read(entry.path());
      CHECK(body.size() == 80 && count < missing.size());
      std::memcpy(missing[count++].data(), body.data(), 16);
      std::array<char, 32> name{};
      std::memcpy(name.data(), body.data() + 40, 32);
      require(RawChannel::unlink(name.data()));
    }
  CHECK(count == 2);
  auto one_slot = small;
  one_slot.recorder_failure_bytes = 111;
  const auto reopened =
      RawRuntime::open(run.directory.name.data(), environment(), one_slot);
  CHECK(!reopened.has_value() && reopened.error().code == ErrorCode::audit_unavailable);
  const auto captured = records(run.path);
  CHECK(captured.size() == 2);
  const std::size_t failure_record_bytes = 16 + 1 + 8 + 24 + 1 + 32 + 4 + 16 + 1;
  std::array<bool, 2> found{};
  for (std::size_t ordinal = 0; ordinal < captured.size(); ++ordinal) {
    CHECK(captured[ordinal].origin.partition == RawPartition::failure &&
          captured[ordinal].origin.source_start == ordinal * failure_record_bytes);
    bool matched = false;
    for (std::size_t i = 0; i < missing.size(); ++i) {
      std::vector<std::byte> expected;
      append(expected, ErrorCode::io);
      append(expected, std::int64_t{ENOENT});
      append(expected, std::uint64_t{2});
      append(expected, std::uint64_t{0});
      append(expected, std::uint64_t{0});
      append(expected, false);
      expected.insert(expected.end(), header.journal.bytes().begin(),
                      header.journal.bytes().end());
      expected.insert(expected.end(), missing[i].begin(), missing[i].end());
      append(expected, RawPartition::data);
      append(expected, std::uint64_t{0});
      append(expected, std::uint64_t{0});
      append(expected, false);
      if (captured[ordinal].bytes == envelope(8000, expected, ordinal + 1)) {
        CHECK(!found[i]);
        matched = found[i] = true;
      }
    }
    CHECK(matched);
  }
  CHECK(found[0] && found[1] && captured[0].origin.lane == captured[1].origin.lane);
  CHECK(!std::filesystem::exists(run.path / "closed"));
}
void symlink_to_live_run() {
  Directory foreign, local;
  auto runtime = require(RawRuntime::open(foreign.name.data(), environment(), small));
  const auto target = sole_run(foreign);
  const auto header = read(target / "lanes");
  const auto active = read(target / "active");
  const auto original_status = require(runtime->status());
  const auto raw = std::filesystem::path{local.name.data()} / "raw";
  std::filesystem::create_directories(raw);
  const auto alias = raw / target.filename();
  std::filesystem::create_directory_symlink(target, alias);
  {
    auto independent =
        require(RawRuntime::open(local.name.data(), environment(), small));
    CHECK(require(independent->status()).supervisor != original_status.supervisor);
  }
  CHECK(read(target / "lanes") == header && read(target / "active") == active);
  CHECK(!std::filesystem::exists(target / "closed"));
  CHECK(require(runtime->status()).supervisor == original_status.supervisor);
  std::filesystem::remove(alias);
  // A foreign orphan has no live lease to hide an accidental traversal.
  // Its published bytes must also remain outside local recovery ownership.
  CrashRun orphan;
  const auto orphan_alias = raw / orphan.path.filename();
  const auto orphan_header = read(orphan.path / "lanes");
  const auto orphan_registration = read(orphan.primary);
  std::filesystem::create_directory_symlink(orphan.path, orphan_alias);
  {
    auto independent =
        require(RawRuntime::open(local.name.data(), environment(), small));
    CHECK(require(independent->status()).recorder > 0);
  }
  CHECK(!std::filesystem::exists(orphan.path / "closed"));
  CHECK(read(orphan.path / "lanes") == orphan_header &&
        read(orphan.primary) == orphan_registration);
  const auto cursor = require(orphan.channel().cursors(RawPartition::data));
  CHECK(cursor.published == 23 && cursor.acknowledged == 0);
  std::filesystem::remove(orphan_alias);
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 2 && std::string_view{argv[1]} == "missing")
    missing_source();
  else if (argc == 2 && std::string_view{argv[1]} == "bootstrap")
    bootstrap();
  else {
    std::fputs("raw_metadata: bootstrap\n", stderr);
    bootstrap();
    std::fputs("raw_metadata: independent copies\n", stderr);
    independent_copies();
    std::fputs("raw_metadata: invalid copies\n", stderr);
    invalid_copies();
    std::fputs("raw_metadata: conflicting copies\n", stderr);
    conflicting_copies();
    std::fputs("raw_metadata: conflicting headers\n", stderr);
    conflicting_headers();
    std::fputs("raw_metadata: missing lease\n", stderr);
    missing_lease();
    std::fputs("raw_metadata: missing activation\n", stderr);
    missing_activation();
    std::fputs("raw_metadata: unreadable copy\n", stderr);
    unreadable_copy();
    std::fputs("raw_metadata: missing source\n", stderr);
    missing_source();
    std::fputs("raw_metadata: one closure copy\n", stderr);
    one_closure_copy();
    std::fputs("raw_metadata: global closure before unlink\n", stderr);
    global_closure_interrupted_unlink();
    std::fputs("raw_metadata: two missing sources with one configured slot\n", stderr);
    two_missing_one_slot();
    std::fputs("raw_metadata: symlink to live foreign run\n", stderr);
    symlink_to_live_run();
  }
}
