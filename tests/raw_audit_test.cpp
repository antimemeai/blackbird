#include "blackbird/raw_audit.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

using namespace blackbird;
namespace {
void check(bool condition, int line) {
  if (!condition)
    throw std::runtime_error("raw audit check at " + std::to_string(line));
}
#define CHECK(expression) check((expression), __LINE__)
template <typename T> T require(Result<T> result) {
  CHECK(result.has_value());
  return std::move(result).value();
}
void require(Result<void> result) { CHECK(result.has_value()); }
template <typename T> T id(unsigned char value) {
  IdentityBytes bytes{};
  bytes[15] = std::byte{value};
  return require(T::from_bytes(bytes));
}
IdentityBytes lane(unsigned char value) {
  IdentityBytes bytes{};
  bytes[15] = std::byte{value};
  return bytes;
}
void little(MutableByteView output, std::uint64_t value) {
  for (auto &byte : output) {
    byte = static_cast<std::byte>(value & 255);
    value >>= 8;
  }
}
std::array<std::byte, 24> record(std::uint64_t ordinal, std::byte body) {
  std::array<std::byte, 24> bytes{};
  little(MutableByteView{bytes}.first(4), bytes.size());
  little(MutableByteView{bytes}.subspan(4, 4), 1);
  little(MutableByteView{bytes}.subspan(8, 8), ordinal);
  for (std::size_t index = 16; index < bytes.size(); ++index)
    bytes[index] =
        static_cast<std::byte>(std::to_integer<std::size_t>(body) + index - 16);
  return bytes;
}
JournalHeader header() {
  return {id<EnvironmentId>(1), id<AuditStreamId>(2), 1, {512, 2048}, std::nullopt};
}
struct Disk {
  std::vector<std::byte> cache;
  std::vector<std::byte> durable;
  std::size_t max_write = SIZE_MAX, max_read = SIZE_MAX;
  std::size_t fail_after = SIZE_MAX, written = 0, writes = 0, syncs = 0;
  std::uint64_t fail_read_at = UINT64_MAX;
  bool fail_sync = false, zero_write = false, impossible_write = false, locked = false;
  unsigned interrupted_writes = 0, interrupted_reads = 0;
};
class File final : public JournalFile {
public:
  explicit File(std::shared_ptr<Disk> disk) : disk_(std::move(disk)) {}
  Result<void> lock_writer() override {
    if (disk_->locked)
      return Result<void>::failure({ErrorCode::busy});
    disk_->locked = true;
    owns_ = true;
    return Result<void>::success();
  }
  void release_writer() noexcept override {
    if (owns_)
      disk_->locked = false;
    owns_ = false;
  }
  ~File() override { release_writer(); }
  Result<std::size_t> read_at(std::uint64_t offset, MutableByteView output) override {
    if (disk_->interrupted_reads != 0) {
      --disk_->interrupted_reads;
      return Result<std::size_t>::failure({ErrorCode::interrupted, EINTR});
    }
    if (offset >= disk_->fail_read_at)
      return Result<std::size_t>::failure({ErrorCode::io, EIO});
    if (offset >= disk_->cache.size())
      return Result<std::size_t>::success(0);
    const auto count =
        std::min({output.size(), disk_->max_read,
                  disk_->cache.size() - static_cast<std::size_t>(offset)});
    std::copy_n(disk_->cache.begin() + static_cast<std::ptrdiff_t>(offset), count,
                output.begin());
    return Result<std::size_t>::success(count);
  }
  Result<std::size_t> write_at(std::uint64_t offset, ByteView input) override {
    ++disk_->writes;
    if (disk_->interrupted_writes != 0) {
      --disk_->interrupted_writes;
      return Result<std::size_t>::failure({ErrorCode::interrupted, EINTR});
    }
    if (disk_->zero_write)
      return Result<std::size_t>::success(0);
    if (disk_->impossible_write)
      return Result<std::size_t>::success(input.size() + 1);
    if (disk_->written >= disk_->fail_after)
      return Result<std::size_t>::failure({ErrorCode::io, ENOSPC});
    const auto count =
        std::min({input.size(), disk_->max_write, disk_->fail_after - disk_->written});
    disk_->cache.resize(static_cast<std::size_t>(offset) + count);
    std::copy_n(input.begin(), count,
                disk_->cache.begin() + static_cast<std::ptrdiff_t>(offset));
    disk_->written += count;
    return Result<std::size_t>::success(count);
  }
  Result<std::uint64_t> extent() override {
    return Result<std::uint64_t>::success(disk_->cache.size());
  }
  Result<void> synchronize(SyncStrength strength) override {
    CHECK(strength == SyncStrength::full);
    ++disk_->syncs;
    if (disk_->fail_sync)
      return Result<void>::failure({ErrorCode::io, EIO});
    disk_->durable = disk_->cache;
    return Result<void>::success();
  }

private:
  std::shared_ptr<Disk> disk_;
  bool owns_ = false;
};
class Directory final : public JournalDirectory {
public:
  std::map<std::string, std::shared_ptr<Disk>> files;
  bool fail_sync = false;
  std::size_t syncs = 0;
  Result<std::unique_ptr<JournalFile>>
  create_exclusive(std::string_view name) override {
    if (files.contains(std::string{name}))
      return Result<std::unique_ptr<JournalFile>>::failure({ErrorCode::io, EEXIST});
    auto disk = std::make_shared<Disk>();
    files.emplace(name, disk);
    return Result<std::unique_ptr<JournalFile>>::success(std::make_unique<File>(disk));
  }
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view name,
                                                     FileAccess) override {
    const auto found = files.find(std::string{name});
    if (found == files.end())
      return Result<std::unique_ptr<JournalFile>>::failure({ErrorCode::io, ENOENT});
    return Result<std::unique_ptr<JournalFile>>::success(
        std::make_unique<File>(found->second));
  }
  Result<void> synchronize_directory(SyncStrength strength) override {
    CHECK(strength == SyncStrength::full);
    ++syncs;
    if (fail_sync)
      return Result<void>::failure({ErrorCode::io, EIO});
    return Result<void>::success();
  }
};
std::uint32_t checksum(ByteView bytes, std::uint32_t previous = 0) {
  auto value = ~previous;
  for (const auto byte : bytes) {
    value ^= std::to_integer<std::uint32_t>(byte);
    for (unsigned bit = 0; bit < 8; ++bit)
      value = (value & 1) ? (value >> 1) ^ 0x82f63b78U : value >> 1;
  }
  return ~value;
}
// Fixture uses independent field offsets and bit-serial CRC, never the encoder.
std::vector<std::byte> framed(std::uint16_t kind, std::uint64_t sequence,
                              std::uint64_t first, ByteView payload) {
  std::vector<std::byte> bytes(32 + payload.size());
  bytes[0] = std::byte{'A'};
  bytes[1] = std::byte{'R'};
  bytes[2] = std::byte{'J'};
  bytes[3] = std::byte{'1'};
  little(MutableByteView{bytes}.subspan(4, 2), 1);
  little(MutableByteView{bytes}.subspan(6, 2), kind);
  little(MutableByteView{bytes}.subspan(8, 8), sequence);
  little(MutableByteView{bytes}.subspan(16, 8), first);
  little(MutableByteView{bytes}.subspan(24, 4), payload.size());
  std::copy(payload.begin(), payload.end(), bytes.begin() + 32);
  little(MutableByteView{bytes}.subspan(28, 4),
         checksum(payload, checksum(ByteView{bytes}.first(28))));
  return bytes;
}
void exact_bytes_and_replay() {
  Directory directory;
  auto audit = require(RawAudit::open(directory, "raw", header()));
  const auto bytes = record(1, std::byte{0xf0});
  const RawAuditRecord input{{lane(3), RawPartition::data, 0}, bytes};
  require(audit->append(std::span{&input, 1}));
  CHECK(audit->preserved(lane(3), RawPartition::data) == bytes.size());
  CHECK(directory.files.at("raw")->durable == directory.files.at("raw")->cache);
  std::vector<std::byte> payload(32 + bytes.size());
  payload[15] = std::byte{3};
  payload[16] = std::byte{1};
  std::copy(bytes.begin(), bytes.end(), payload.begin() + 32);
  auto expected = framed(4, 1, 1, payload);
  std::array<std::byte, 24> commit{};
  little(MutableByteView{commit}.first(8), 1);
  little(MutableByteView{commit}.subspan(8, 8), 1);
  little(MutableByteView{commit}.subspan(16, 4), checksum(expected));
  const auto end = framed(65535, 2, 1, commit);
  expected.insert(expected.end(), end.begin(), end.end());
  CHECK(std::equal(expected.begin(), expected.end(),
                   directory.files.at("raw")->cache.begin() + 112,
                   directory.files.at("raw")->cache.end()));
  std::array<std::byte, 512> output{};
  const auto read = require(audit->read(audit->begin(), output));
  if (!read)
    throw std::runtime_error("missing preserved record");
  CHECK(read->origin == input.origin);
  CHECK(std::equal(bytes.begin(), bytes.end(), read->raw.begin(), read->raw.end()));
  CHECK(!require(audit->read(read->next, output)));
  const auto writes = directory.files.at("raw")->writes;
  require(audit->append(std::span{&input, 1}));
  CHECK(directory.files.at("raw")->writes == writes);
  auto changed = bytes;
  changed[20] ^= std::byte{1};
  const RawAuditRecord conflict{input.origin, changed};
  CHECK(!audit->append(std::span{&conflict, 1}).has_value());
  audit.reset();
  audit = require(RawAudit::open(directory, "raw", header()));
  CHECK(!audit->recovery_error());
  CHECK(audit->preserved(lane(3), RawPartition::data) == bytes.size());
  require(audit->append(std::span{&input, 1}));
}
void partial_writes_and_restarts() {
  const auto bytes = record(1, std::byte{0});
  const RawAuditRecord input{{lane(3), RawPartition::data, 0}, bytes};
  for (std::size_t cut = 0; cut < 144; ++cut) {
    Directory directory;
    auto audit = require(RawAudit::open(directory, "raw", header()));
    const auto disk = directory.files.at("raw");
    disk->written = 0;
    disk->fail_after = cut;
    disk->max_write = 7;
    const auto failed = audit->append(std::span{&input, 1});
    CHECK(!failed.has_value() && failed.error() == (Error{ErrorCode::io, ENOSPC}));
    CHECK(audit->preserved(lane(3), RawPartition::data) == 0);
    CHECK(audit->progress().written_bytes == cut);
    const auto retained = disk->cache;
    audit.reset();
    disk->fail_after = SIZE_MAX;
    audit = require(RawAudit::open(directory, "raw", header()));
    CHECK(audit->preserved(lane(3), RawPartition::data) == 0);
    CHECK(disk->cache == retained);
    if (cut != 0) {
      CHECK(audit->recovery_error().has_value());
      CHECK(!audit->append(std::span{&input, 1}).has_value());
    } else {
      CHECK(!audit->recovery_error());
      require(audit->append(std::span{&input, 1}));
    }
  }
  Directory directory;
  auto audit = require(RawAudit::open(directory, "raw", header()));
  const auto disk = directory.files.at("raw");
  disk->max_write = 1;
  disk->max_read = 1;
  disk->interrupted_writes = 3;
  require(audit->append(std::span{&input, 1}));
  audit.reset();
  disk->interrupted_reads = 3;
  audit = require(RawAudit::open(directory, "raw", header()));
  CHECK(audit->preserved(lane(3), RawPartition::data) == bytes.size());
}
void ambiguous_flush_and_failure() {
  Directory directory;
  auto audit = require(RawAudit::open(directory, "raw", header()));
  const auto bytes = record(1, std::byte{1});
  const RawAuditRecord input{{lane(3), RawPartition::failure, 0}, bytes};
  const auto disk = directory.files.at("raw");
  disk->fail_sync = true;
  CHECK(!audit->append(std::span{&input, 1}).has_value());
  CHECK(audit->progress().sync_attempted && audit->progress().written_bytes == 144);
  CHECK(audit->preserved(lane(3), RawPartition::failure) == 0);
  std::array<std::byte, 24> output{};
  CHECK(!require(audit->read(audit->begin(), output)));
  const auto cache = disk->cache;
  audit.reset();
  CHECK(!RawAudit::open(directory, "raw", header()).has_value());
  disk->fail_sync = false;
  audit = require(RawAudit::open(directory, "raw", header()));
  CHECK(disk->cache == cache);
  CHECK(audit->preserved(lane(3), RawPartition::failure) == bytes.size());
  const auto writes = disk->writes;
  require(audit->append(std::span{&input, 1}));
  CHECK(disk->writes == writes);
  audit.reset();
  directory.fail_sync = true;
  CHECK(!RawAudit::open(directory, "raw", header()).has_value());
}
void lanes_and_admission() {
  Directory directory;
  auto audit = require(RawAudit::open(directory, "raw", header(), 2));
  const auto first = record(1, std::byte{1}), second = record(2, std::byte{2}),
             third = record(3, std::byte{3});
  std::array<RawAuditRecord, 3> batch{
      {{{lane(3), RawPartition::data, 0}, first},
       {{lane(3), RawPartition::control, 0}, second},
       {{lane(3), RawPartition::data, first.size()}, third}}};
  require(audit->append(batch));
  CHECK(audit->preserved(lane(3), RawPartition::data) == first.size() * 2);
  CHECK(audit->preserved(lane(3), RawPartition::control) == second.size());
  const RawAuditRecord overflow{{lane(3), RawPartition::failure, 0}, first};
  CHECK(!audit->append(std::span{&overflow, 1}).has_value());
  const RawAuditRecord gap{{lane(3), RawPartition::data, first.size() * 2 + 1}, first};
  CHECK(!audit->append(std::span{&gap, 1}).has_value());
  const RawAuditRecord wrap{{lane(3), RawPartition::data, UINT64_MAX}, first};
  CHECK(!audit->append(std::span{&wrap, 1}).has_value());
  auto malformed = first;
  malformed[0] = std::byte{0};
  const RawAuditRecord bad{{lane(4), RawPartition::data, 0}, malformed};
  CHECK(!audit->append(std::span{&bad, 1}).has_value());
  audit.reset();
  audit = require(RawAudit::open(directory, "raw", header(), 2));
  CHECK(audit->preserved(lane(3), RawPartition::data) == 48);
  CHECK(audit->preserved(lane(3), RawPartition::control) == 24);
}
void unreadable_committed_bytes_are_a_hard_error() {
  Directory directory;
  auto audit = require(RawAudit::open(directory, "raw", header()));
  const auto first = record(1, std::byte{1}), second = record(2, std::byte{2});
  const RawAuditRecord initial{{lane(3), RawPartition::data, 0}, first};
  const RawAuditRecord next{{lane(3), RawPartition::data, first.size()}, second};
  require(audit->append(std::span{&initial, 1}));
  const auto first_prefix = audit->cursor();
  require(audit->append(std::span{&next, 1}));
  const auto preserved = audit->cursor();
  const auto disk = directory.files.at("raw");
  const auto original = disk->cache;
  const auto durable = disk->durable;
  const auto syncs = disk->syncs;
  disk->fail_read_at = first_prefix.offset;
  std::array<std::byte, 24> output{};
  const auto read = audit->read(first_prefix, output);
  CHECK(!read.has_value() && read.error() == (Error{ErrorCode::io, EIO}));
  CHECK(audit->cursor() == preserved);
  CHECK(audit->preserved(lane(3), RawPartition::data) == first.size() + second.size());
  CHECK(!audit->recovery_error());
  audit.reset();
  const auto reopened = RawAudit::open(directory, "raw", header());
  CHECK(!reopened.has_value() && reopened.error() == (Error{ErrorCode::io, EIO}));
  const auto reader = RawAudit::open_reader(directory, "raw", header(), preserved);
  CHECK(!reader.has_value() && reader.error() == (Error{ErrorCode::io, EIO}));
  CHECK(disk->cache == original && disk->durable == durable && disk->syncs == syncs);
  CHECK(!disk->locked);
  disk->fail_read_at = UINT64_MAX;
  audit = require(RawAudit::open(directory, "raw", header()));
  CHECK(audit->cursor() == preserved);
  CHECK(audit->preserved(lane(3), RawPartition::data) == first.size() + second.size());
}
void corrupt_tail_and_successor() {
  Directory directory;
  auto audit = require(RawAudit::open(directory, "raw", header()));
  const auto bytes = record(1, std::byte{1});
  const RawAuditRecord input{{lane(3), RawPartition::data, 0}, bytes};
  require(audit->append(std::span{&input, 1}));
  const auto prefix = audit->cursor();
  const auto disk = directory.files.at("raw");
  disk->cache.push_back(std::byte{0xde});
  const auto damaged = disk->cache;
  audit.reset();
  audit = require(RawAudit::open(directory, "raw", header()));
  CHECK(audit->recovery_error() && audit->cursor() == prefix);
  CHECK(audit->preserved(lane(3), RawPartition::data) == bytes.size());
  CHECK(disk->cache == damaged);
  auto successor = header();
  successor.journal = id<AuditStreamId>(4);
  successor.predecessor =
      JournalPredecessor{prefix.journal, prefix.sequence, prefix.offset};
  const RawOrigin inherited{lane(3), RawPartition::data, bytes.size()};
  auto next = require(RawAudit::open(directory, "next", successor, 256,
                                     SyncStrength::full, std::span{&inherited, 1}));
  const auto next_bytes = record(2, std::byte{2});
  const RawAuditRecord pending{{lane(3), RawPartition::data, bytes.size()}, next_bytes};
  require(next->append(std::span{&pending, 1}));
  CHECK(next->preserved(lane(3), RawPartition::data) == 48);
  next.reset();
  next = require(RawAudit::open(directory, "next", successor, 256, SyncStrength::full,
                                std::span{&inherited, 1}));
  CHECK(next->preserved(lane(3), RawPartition::data) == 48);
  CHECK(disk->cache == damaged);
}
void zero_impossible_and_lease() {
  for (bool impossible : {false, true}) {
    Directory directory;
    auto audit = require(RawAudit::open(directory, "raw", header()));
    CHECK(!RawAudit::open(directory, "raw", header()).has_value());
    const auto bytes = record(1, std::byte{1});
    const RawAuditRecord input{{lane(3), RawPartition::data, 0}, bytes};
    const auto disk = directory.files.at("raw");
    disk->zero_write = !impossible;
    disk->impossible_write = impossible;
    CHECK(!audit->append(std::span{&input, 1}).has_value());
    CHECK(audit->preserved(lane(3), RawPartition::data) == 0);
    CHECK(audit->progress().written_bytes == 0);
  }
}
void previous_prefix_survives_every_tail_cut() {
  const auto first = record(1, std::byte{7}), second = record(2, std::byte{9});
  const RawAuditRecord initial{{lane(3), RawPartition::data, 0}, first};
  const RawAuditRecord pending{{lane(3), RawPartition::data, first.size()}, second};
  for (std::size_t cut = 0; cut < 144; ++cut) {
    for (bool corruption : {false, true}) {
      Directory directory;
      auto audit = require(RawAudit::open(directory, "raw", header()));
      require(audit->append(std::span{&initial, 1}));
      const auto prefix = audit->cursor();
      const auto disk = directory.files.at("raw");
      if (corruption) {
        require(audit->append(std::span{&pending, 1}));
        disk->cache[static_cast<std::size_t>(prefix.offset) + cut] ^= std::byte{0x80};
      } else {
        disk->written = 0;
        disk->fail_after = cut;
        CHECK(!audit->append(std::span{&pending, 1}).has_value());
        CHECK(audit->preserved(lane(3), RawPartition::data) == first.size());
      }
      const auto retained = disk->cache;
      audit.reset();
      disk->fail_after = SIZE_MAX;
      audit = require(RawAudit::open(directory, "raw", header()));
      CHECK(audit->cursor() == prefix);
      CHECK(audit->preserved(lane(3), RawPartition::data) == first.size());
      CHECK(disk->cache == retained);
      std::array<std::byte, 24> output{};
      const auto old = require(audit->read(audit->begin(), output));
      if (!old)
        throw std::runtime_error("lost acknowledged prefix");
      CHECK(old->origin == initial.origin);
      CHECK(std::equal(first.begin(), first.end(), old->raw.begin(), old->raw.end()));
      CHECK(!require(audit->read(old->next, output)));
    }
  }
}
void tertiary_reader_does_not_take_lease_or_unflushed_tail() {
  Directory directory;
  auto audit = require(RawAudit::open(directory, "raw", header()));
  const auto first = record(1, std::byte{7}), second = record(2, std::byte{9});
  const RawAuditRecord input{{lane(3), RawPartition::data, 0}, first};
  const RawAuditRecord pending{{lane(3), RawPartition::data, first.size()}, second};
  require(audit->append(std::span{&input, 1}));
  const auto prefix = audit->cursor();
  const auto disk = directory.files.at("raw");
  disk->fail_sync = true;
  CHECK(!audit->append(std::span{&pending, 1}).has_value());
  const auto syncs = disk->syncs;
  auto reader = require(RawAudit::open_reader(directory, "raw", header(), prefix));
  CHECK(disk->locked && disk->syncs == syncs);
  CHECK(reader->preserved(lane(3), RawPartition::data) == first.size());
  std::array<std::byte, 24> output{};
  const auto read = require(reader->read(reader->begin(), output));
  if (!read)
    throw std::runtime_error("missing tertiary prefix");
  CHECK(read->origin == input.origin);
  CHECK(!require(reader->read(read->next, output)));
  CHECK(!reader->append(std::span{&pending, 1}).has_value());
  auto invalid = prefix;
  --invalid.offset;
  CHECK(!RawAudit::open_reader(directory, "raw", header(), invalid).has_value());
  reader.reset();
  CHECK(disk->locked);
}
} // namespace
int main() {
  try {
    exact_bytes_and_replay();
    partial_writes_and_restarts();
    ambiguous_flush_and_failure();
    lanes_and_admission();
    unreadable_committed_bytes_are_a_hard_error();
    corrupt_tail_and_successor();
    zero_impossible_and_lease();
    previous_prefix_survives_every_tail_cut();
    tertiary_reader_does_not_take_lease_or_unflushed_tail();
    std::puts(
        "raw audit: exact bytes, faults, restart, replay and prefix checks passed");
  } catch (const std::exception &error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
