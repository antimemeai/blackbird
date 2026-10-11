#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using namespace blackbird;
void check(bool value, int line) {
  if (!value) {
    std::fprintf(stderr, "raw_reader:%d\n", line);
    std::abort();
  }
}
#define CHECK(...) check((__VA_ARGS__), __LINE__)
template <class T> T need(Result<T> value) {
  if (!value.has_value())
    std::fprintf(stderr, "unexpected error %s %lld\n", error_name(value.error().code),
                 static_cast<long long>(value.error().detail));
  CHECK(value.has_value());
  return std::move(value).value();
}
void need(Result<void> value) { CHECK(value.has_value()); }
template <class T> T present(std::optional<T> value) {
  if (!value.has_value())
    std::abort();
  return std::move(value).value();
}
void write_file(const std::filesystem::path &path, ByteView body) {
  std::ofstream file{path, std::ios::binary | std::ios::trunc};
  file.write(reinterpret_cast<const char *>(body.data()),
             static_cast<std::streamsize>(body.size()));
  CHECK(file.good());
  file.close();
  std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write);
}
std::vector<std::byte> read_file(const std::filesystem::path &path) {
  std::ifstream file{path, std::ios::binary | std::ios::ate};
  CHECK(file.good());
  const auto count = file.tellg();
  CHECK(count >= 0);
  std::vector<std::byte> body(static_cast<std::size_t>(count));
  file.seekg(0);
  file.read(reinterpret_cast<char *>(body.data()), count);
  CHECK(file.good());
  return body;
}
std::map<std::string, std::vector<std::byte>> files(const std::filesystem::path &path) {
  std::map<std::string, std::vector<std::byte>> result;
  for (const auto &entry : std::filesystem::directory_iterator{path})
    if (entry.is_regular_file())
      result.emplace(entry.path().filename().string(), read_file(entry.path()));
  return result;
}
std::uint64_t little(ByteView bytes) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i)
    value |= std::uint64_t{std::to_integer<unsigned char>(bytes[i])} << (i * 8);
  return value;
}
void put(MutableByteView body, std::uint64_t value) {
  for (auto &byte : body) {
    byte = std::byte{static_cast<unsigned char>(value & 255U)};
    value >>= 8;
  }
}
IdentityBytes identity(unsigned first) {
  IdentityBytes value{};
  value[0] = std::byte{static_cast<unsigned char>(first)};
  return value;
}
std::vector<std::byte> raw(unsigned ordinal, std::byte body) {
  std::vector<std::byte> value(17);
  put(MutableByteView{value}.first(4), value.size());
  put(MutableByteView{value}.subspan(4, 4), 9100);
  put(MutableByteView{value}.subspan(8, 8), ordinal);
  value[16] = body;
  return value;
}
struct Fixture {
  std::array<char, 64> name{};
  std::filesystem::path run;
  JournalHeader header{need(EnvironmentId::from_bytes(identity(1))),
                       need(AuditStreamId::from_bytes(identity(2))),
                       1,
                       {4096, 8192},
                       std::nullopt};
  IdentityBytes lane = identity(3);
  std::unique_ptr<RawAudit> writer;
  Fixture() {
    const std::string pattern = "/tmp/blackbird-raw-reader-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), name.begin());
    CHECK(::mkdtemp(name.data()) != nullptr);
    run = std::filesystem::path{name.data()} / "raw" / "bbfixture";
    std::filesystem::create_directories(run);
    std::filesystem::permissions(run.parent_path(), std::filesystem::perms::owner_all);
    std::filesystem::permissions(run, std::filesystem::perms::owner_all);
    std::array<std::byte, 64> descriptions{};
    std::memcpy(descriptions.data(), "BBRAWABI", 8);
    put(MutableByteView{descriptions}.subspan(8, 4), 1);
    write_file(run / "descriptions", descriptions);
    auto directory = need(NativeJournalDirectory::open(run.string()));
    writer =
        need(RawAudit::open(directory, "events.0", header, 16, SyncStrength::data));
    publish(0, writer->cursor());
  }
  ~Fixture() {
    writer.reset();
    std::filesystem::remove_all(name.data());
  }
  void publish(std::uint64_t segment, RawAuditCursor cursor) {
    std::array<std::byte, 40> body{};
    put(MutableByteView{body}.first(8), segment);
    std::copy(cursor.journal.bytes().begin(), cursor.journal.bytes().end(),
              body.begin() + 8);
    put(MutableByteView{body}.subspan(24, 8), cursor.sequence);
    put(MutableByteView{body}.subspan(32, 8), cursor.offset);
    write_file(run / "published", body);
  }
  void append(unsigned ordinal) {
    const auto body = raw(ordinal, std::byte{static_cast<unsigned char>(ordinal)});
    const std::array records{RawAuditRecord{
        {lane, RawPartition::data, std::uint64_t{ordinal - 1} * 17}, body}};
    need(writer->append(records));
  }
};
void publication_and_resume() {
  Fixture fixture;
  fixture.append(1);
  fixture.publish(0, fixture.writer->cursor());
  const auto preserved = fixture.writer->cursor();
  fixture.publish(0, {preserved.journal, 1, 193});
  CHECK(!RawRunReader::open(fixture.run.string()).has_value());
  fixture.publish(0, preserved);
  auto first = need(RawRunReader::open(fixture.run.string()));
  const auto descriptions = first->snapshot().descriptions;
  std::array<std::byte, 4096> output{};
  const auto record = present(need(first->read(output)));
  const auto expected_first = raw(1, std::byte{1});
  CHECK(std::equal(record.raw.begin(), record.raw.end(), expected_first.begin(),
                   expected_first.end()));
  CHECK(record.origin == (RawOrigin{fixture.lane, RawPartition::data, 0}));
  CHECK(!need(first->read(output)).has_value());
  const auto checkpoint = first->cursor();
  fixture.append(2); // Complete on disk, still outside publication.
  auto invisible = need(RawRunReader::open(fixture.run.string(), checkpoint));
  CHECK(!need(invisible->read(output)).has_value());
  CHECK(invisible->snapshot().segments[0].unexposed_bytes > 0);
  fixture.publish(0, fixture.writer->cursor());
  CHECK(!need(first->read(output)).has_value()); // Existing snapshot is unchanged.
  CHECK(first->snapshot().descriptions == descriptions);
  auto resumed = need(RawRunReader::open(fixture.run.string(), checkpoint));
  const auto second = present(need(resumed->read(output)));
  CHECK(second.origin.source_start == 17);
  CHECK(second.raw.back() == std::byte{2});
  CHECK(!need(resumed->read(output)).has_value());
  CHECK(resumed->cursor().audit == fixture.writer->cursor());
  auto forged = checkpoint;
  ++forged.audit.offset;
  CHECK(!RawRunReader::open(fixture.run.string(), forged).has_value());
  forged = checkpoint;
  forged.run = need(AuditStreamId::from_bytes(identity(9)));
  CHECK(!RawRunReader::open(fixture.run.string(), forged).has_value());
  const auto encoded = encode_raw_run_cursor(resumed->cursor());
  CHECK(need(decode_raw_run_cursor(encoded)) == resumed->cursor());
  auto changed = read_file(fixture.run / "descriptions");
  changed.back() ^= std::byte{1};
  write_file(fixture.run / "descriptions", changed);
  CHECK(!RawRunReader::open(fixture.run.string(), checkpoint).has_value());
}
void successor_and_damage() {
  Fixture fixture;
  fixture.append(1);
  const auto prior = fixture.writer->cursor();
  fixture.writer.reset();
  auto original = read_file(fixture.run / "events.0");
  original.push_back(std::byte{0xff});
  write_file(fixture.run / "events.0", original);
  const JournalHeader successor{
      fixture.header.environment, need(AuditStreamId::from_bytes(identity(4))), 1,
      fixture.header.limits,
      JournalPredecessor{prior.journal, prior.sequence, prior.offset}};
  const std::array prefixes{RawOrigin{fixture.lane, RawPartition::data, 17}};
  std::array<std::byte, 40> seed{};
  put(MutableByteView{seed}.first(4), 1);
  std::copy(fixture.lane.begin(), fixture.lane.end(), seed.begin() + 8);
  put(MutableByteView{seed}.subspan(24, 4), 1);
  put(MutableByteView{seed}.subspan(32, 8), 17);
  put(MutableByteView{seed}.subspan(4, 4), crc32c(ByteView{seed}.subspan(8)));
  write_file(fixture.run / "seed.1", seed);
  auto directory = need(NativeJournalDirectory::open(fixture.run.string()));
  auto writer = need(RawAudit::open(directory, "events.1", successor, 16,
                                    SyncStrength::data, prefixes));
  const auto body = raw(2, std::byte{2});
  const std::array records{
      RawAuditRecord{{fixture.lane, RawPartition::data, 17}, body}};
  need(writer->append(records));
  fixture.publish(1, writer->cursor());
  write_file(fixture.run / "closed", {});
  const auto before = read_file(fixture.run / "events.1");
  const auto unchanged = files(fixture.run);
  auto reader = need(RawRunReader::open(fixture.run.string()));
  CHECK(reader->snapshot().closed);
  CHECK(reader->snapshot().segments.size() == 2);
  CHECK(reader->snapshot().segments[0].unexposed_bytes == 1);
  std::array<std::byte, 4096> output{};
  CHECK(present(need(reader->read(output))).raw.back() == std::byte{1});
  const auto checkpoint = reader->cursor();
  CHECK(present(need(reader->read(output))).raw.back() == std::byte{2});
  CHECK(!need(reader->read(output)).has_value());
  auto resumed = need(RawRunReader::open(fixture.run.string(), checkpoint));
  CHECK(present(need(resumed->read(output))).raw.back() == std::byte{2});
  CHECK(read_file(fixture.run / "events.0") == original);
  CHECK(read_file(fixture.run / "events.1") == before);
  CHECK(files(fixture.run) == unchanged);
  auto interrupted = need(RawRunReader::open(fixture.run.string()));
  CHECK(present(need(interrupted->read(output))).raw.back() == std::byte{1});
  const auto parked = std::filesystem::path{fixture.name.data()} / "parked-segment";
  std::filesystem::rename(fixture.run / "events.1", parked);
  const auto missing = interrupted->read(output);
  CHECK(!missing.has_value() && missing.error() == (Error{ErrorCode::io, ENOENT}));
  const auto repeated = interrupted->read(output);
  CHECK(!repeated.has_value() && repeated.error() == missing.error());
  std::filesystem::rename(parked, fixture.run / "events.1");
  seed.back() ^= std::byte{1};
  write_file(fixture.run / "seed.1", seed);
  CHECK(!RawRunReader::open(fixture.run.string()).has_value());
  CHECK(read_file(fixture.run / "events.0") == original);
  CHECK(read_file(fixture.run / "events.1") == before);
  put(MutableByteView{seed}.subspan(32, 8), 18);
  put(MutableByteView{seed}.subspan(4, 4), crc32c(ByteView{seed}.subspan(8)));
  write_file(fixture.run / "seed.1", seed);
  const auto wrong_seed = RawRunReader::open(fixture.run.string());
  CHECK(!wrong_seed.has_value() && wrong_seed.error().code == ErrorCode::conflict);
  put(MutableByteView{seed}.subspan(32, 8), 17);
  put(MutableByteView{seed}.subspan(4, 4), crc32c(ByteView{seed}.subspan(8)));
  write_file(fixture.run / "seed.1", seed);
  auto wrong_header = successor;
  wrong_header.predecessor->journal = need(AuditStreamId::from_bytes(identity(5)));
  const auto encoded = need(encode_journal_header(wrong_header));
  auto changed = before;
  std::copy(encoded.begin(), encoded.end(), changed.begin());
  write_file(fixture.run / "events.1", changed);
  const auto wrong_link = RawRunReader::open(fixture.run.string());
  CHECK(!wrong_link.has_value() && wrong_link.error().code == ErrorCode::conflict);
}
void discovery_and_bad_publication() {
  Fixture fixture;
  const auto broken = fixture.run.parent_path() / "bbunfinished";
  std::filesystem::create_directory(broken);
  std::filesystem::permissions(broken, std::filesystem::perms::owner_all);
  auto entries = need(discover_raw_runs(fixture.name.data()));
  CHECK(entries.size() == 2);
  CHECK(entries[0].name == "bbfixture" && entries[0].snapshot.has_value());
  CHECK(entries[1].name == "bbunfinished" && entries[1].error.has_value());
  const auto link = fixture.run.parent_path() / "bbsymlink";
  std::filesystem::create_directory_symlink(fixture.run, link);
  entries = need(discover_raw_runs(fixture.name.data()));
  CHECK(entries.size() == 3 && entries[1].name == "bbsymlink" &&
        entries[1].error.has_value());
  CHECK(!RawRunReader::open(link.string()).has_value());
  auto cursor = fixture.writer->cursor();
  ++cursor.offset;
  fixture.publish(0, cursor);
  CHECK(!RawRunReader::open(fixture.run.string()).has_value());
  fixture.publish(1, fixture.writer->cursor());
  CHECK(!RawRunReader::open(fixture.run.string()).has_value());
  std::filesystem::remove(fixture.run / "published");
  CHECK(!RawRunReader::open(fixture.run.string()).has_value());
}
std::vector<std::byte> cli(const char *operation, const Fixture &fixture,
                           const char *resume = nullptr) {
  const auto output = std::filesystem::path{fixture.name.data()} / "reader-output";
  const auto path = fixture.run.string();
  const int descriptor =
      ::open(output.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  CHECK(descriptor >= 0);
  const auto child = ::fork();
  CHECK(child >= 0);
  if (child == 0) {
    if (::dup2(descriptor, STDOUT_FILENO) < 0)
      ::_exit(120);
    (void)::close(descriptor);
    if (resume != nullptr)
      ::execl(BLACKBIRD_RAW_READER_PATH, BLACKBIRD_RAW_READER_PATH, operation,
              path.c_str(), resume, static_cast<char *>(nullptr));
    else
      ::execl(BLACKBIRD_RAW_READER_PATH, BLACKBIRD_RAW_READER_PATH, operation,
              path.c_str(), static_cast<char *>(nullptr));
    ::_exit(121);
  }
  (void)::close(descriptor);
  int status = 0;
  CHECK(::waitpid(child, &status, 0) == child);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  return read_file(output);
}
void executable_stream() {
  Fixture fixture;
  fixture.append(1);
  fixture.publish(0, fixture.writer->cursor());
  const auto unchanged = files(fixture.run);
  CHECK(cli("descriptions", fixture) == read_file(fixture.run / "descriptions"));
  const auto stream = cli("records", fixture);
  CHECK(stream.size() == 16 + 64 + 101 + 17 + 65);
  CHECK(std::memcmp(stream.data(), "BBRAWOUT", 8) == 0);
  CHECK(little(ByteView{stream}.subspan(8, 4)) == 1);
  CHECK(little(ByteView{stream}.subspan(12, 4)) == 64);
  CHECK(std::equal(stream.begin() + 16, stream.begin() + 80,
                   unchanged.at("descriptions").begin()));
  CHECK(stream[80] == std::byte{1});
  CHECK(little(ByteView{stream}.subspan(81 + 48, 8)) == 1);
  CHECK(little(ByteView{stream}.subspan(81 + 56, 8)) == 193);
  CHECK(std::equal(stream.begin() + 145, stream.begin() + 161, fixture.lane.begin()));
  CHECK(little(ByteView{stream}.subspan(161, 4)) == 1);
  CHECK(little(ByteView{stream}.subspan(165, 4)) == 0);
  CHECK(little(ByteView{stream}.subspan(169, 8)) == 0);
  CHECK(little(ByteView{stream}.subspan(177, 4)) == 17);
  const auto expected = raw(1, std::byte{1});
  CHECK(std::equal(stream.begin() + 181, stream.begin() + 198, expected.begin()));
  CHECK(stream[198] == std::byte{0});
  CHECK(little(ByteView{stream}.subspan(199 + 48, 8)) == 2);
  CHECK(little(ByteView{stream}.subspan(199 + 56, 8)) == 249);
  const auto checkpoint_path =
      std::filesystem::path{fixture.name.data()} / "reader-checkpoint";
  write_file(checkpoint_path, ByteView{stream}.last(64));
  const auto resumed = cli("records", fixture, checkpoint_path.c_str());
  CHECK(resumed.size() == 16 + 64 + 65);
  CHECK(resumed[80] == std::byte{0});
  CHECK(std::equal(resumed.end() - 64, resumed.end(), stream.end() - 64));
  CHECK(files(fixture.run) == unchanged);
}
void runtime_catalog() {
  Fixture fixture;
  fixture.writer.reset();
  std::filesystem::remove_all(fixture.run);
  const std::array body{std::byte{0}, std::byte{255}, std::byte{65}, std::byte{127},
                        std::byte{0}};
  auto runtime = need(RawRuntime::open(fixture.name.data(), fixture.header.environment,
                                       {{65536, 4096, 4096}, 4096}));
  IdentityBytes lane{};
  {
    const auto context = need(runtime->prepare_thread());
    RawThreadBinding binding{context};
    lane = raw_lane_epoch();
    need(raw_wait_data(raw_envelope_size + body.size()));
    const std::array<ByteView, 1> parts{body};
    CHECK(need(raw_emit(9300, parts)) == 1);
  }
  runtime.reset();
  const auto entries = need(discover_raw_runs(fixture.name.data()));
  CHECK(entries.size() == 1);
  const auto snapshot = present(entries.front().snapshot);
  CHECK(snapshot.closed && !snapshot.descriptions.empty());
  const auto path =
      std::filesystem::path{fixture.name.data()} / "raw" / entries.front().name;
  const auto unchanged = files(path);
  auto reader = need(RawRunReader::open(path.string()));
  std::array<std::byte, 128> output{};
  const auto record = present(need(reader->read(output)));
  std::array<std::byte, 21> expected{};
  put(MutableByteView{expected}.first(4), expected.size());
  put(MutableByteView{expected}.subspan(4, 4), 9300);
  put(MutableByteView{expected}.subspan(8, 8), 1);
  std::copy(body.begin(), body.end(), expected.begin() + 16);
  CHECK(std::equal(record.raw.begin(), record.raw.end(), expected.begin(),
                   expected.end()));
  CHECK(record.origin == (RawOrigin{lane, RawPartition::data, 0}));
  CHECK(!need(reader->read(output)).has_value());
  CHECK(files(path) == unchanged);
}
void anchored_discovery() {
  Fixture original;
  Fixture replacement;
  original.append(1);
  original.publish(0, original.writer->cursor());
  auto held = need(NativeJournalDirectory::open(original.name.data()));
  const auto renamed = std::string{original.name.data()} + "-renamed";
  std::filesystem::permissions(
      original.run.parent_path(),
      std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
          std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
          std::filesystem::perms::others_exec);
  std::filesystem::rename(original.name.data(), renamed);
  std::filesystem::create_directory_symlink(replacement.name.data(),
                                            original.name.data());
  const auto entries = need(discover_raw_runs(std::move(held)));
  CHECK(entries.size() == 1);
  const auto snapshot = present(entries.front().snapshot);
  CHECK(snapshot.segments.front().preserved.sequence == 2);
  CHECK(!discover_raw_runs(original.name.data()).has_value());
  std::filesystem::remove(original.name.data());
  std::filesystem::rename(renamed, original.name.data());
}
void descriptions_v2() {
  Fixture fixture;
  auto descriptions = read_file(fixture.run / "descriptions");
  descriptions.resize(80);
  put(MutableByteView{descriptions}.subspan(8, 4), 2);
  descriptions.back() = std::byte{0xab};
  write_file(fixture.run / "descriptions", descriptions);
  const auto reader = need(RawRunReader::open(fixture.run.string()));
  CHECK(reader->snapshot().descriptions == descriptions);
  descriptions.resize(79);
  write_file(fixture.run / "descriptions", descriptions);
  CHECK(!RawRunReader::open(fixture.run.string()).has_value());
  descriptions.resize(80);
  put(MutableByteView{descriptions}.subspan(8, 4), 3);
  write_file(fixture.run / "descriptions", descriptions);
  CHECK(!RawRunReader::open(fixture.run.string()).has_value());
}
} // namespace
int main() {
  publication_and_resume();
  successor_and_damage();
  discovery_and_bad_publication();
  executable_stream();
  runtime_catalog();
  anchored_discovery();
  descriptions_v2();
}
