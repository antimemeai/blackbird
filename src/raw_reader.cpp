#include "blackbird/raw_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace blackbird {
namespace {
template <class T> T take(Result<T> answer) {
  if (!answer.has_value())
    throw answer.error();
  return std::move(answer).value();
}
std::uint64_t little(ByteView input) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < input.size(); ++i)
    value |= std::uint64_t{std::to_integer<unsigned char>(input[i])} << (8 * i);
  return value;
}
void put(MutableByteView output, std::uint64_t value) noexcept {
  for (auto &byte : output) {
    byte = std::byte{static_cast<unsigned char>(value & 255U)};
    value >>= 8;
  }
}
void exact(JournalFile &file, std::uint64_t offset, MutableByteView output) {
  std::size_t completed = 0;
  while (completed != output.size()) {
    auto answer = file.read_at(offset + completed, output.subspan(completed));
    if (!answer.has_value() && answer.error().code == ErrorCode::interrupted)
      continue;
    const auto count = take(std::move(answer));
    if (count == 0 || count > output.size() - completed)
      throw Error{ErrorCode::incomplete};
    completed += count;
  }
}
std::vector<std::byte> file_bytes(NativeJournalDirectory &directory,
                                  std::string_view name, std::size_t maximum) {
  auto file = take(directory.open_existing(name, FileAccess::read_only));
  const auto size = take(file->extent());
  if (size > maximum)
    throw Error{ErrorCode::capacity};
  std::vector<std::byte> output(static_cast<std::size_t>(size));
  exact(*file, 0, output);
  return output;
}
JournalHeader segment_header(NativeJournalDirectory &directory, std::uint64_t number) {
  auto file = take(directory.open_existing("events." + std::to_string(number),
                                           FileAccess::read_only));
  std::array<std::byte, journal_header_size> output{};
  exact(*file, 0, output);
  return take(decode_journal_header(output));
}
std::vector<RawOrigin> seeds(NativeJournalDirectory &directory, std::uint64_t number,
                             std::size_t maximum) {
  if (maximum > (SIZE_MAX - 8) / raw_origin_size)
    throw Error{ErrorCode::overflow};
  const auto bytes = file_bytes(directory, "seed." + std::to_string(number),
                                8 + maximum * raw_origin_size);
  if (bytes.size() < 8 || little(ByteView{bytes}.first(4)) > maximum ||
      bytes.size() != 8 + little(ByteView{bytes}.first(4)) * raw_origin_size ||
      little(ByteView{bytes}.subspan(4, 4)) != crc32c(ByteView{bytes}.subspan(8)))
    throw Error{ErrorCode::corrupt};
  std::vector<RawOrigin> output;
  output.reserve((bytes.size() - 8) / raw_origin_size);
  for (std::size_t offset = 8; offset < bytes.size(); offset += raw_origin_size) {
    IdentityBytes lane{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), 16, lane.begin());
    if (little(ByteView{bytes}.subspan(offset + 20, 4)) != 0)
      throw Error{ErrorCode::corrupt};
    output.push_back(
        {lane,
         static_cast<RawPartition>(little(ByteView{bytes}.subspan(offset + 16, 4))),
         little(ByteView{bytes}.subspan(offset + 24, 8))});
  }
  return output;
}
RawRunSnapshot capture(NativeJournalDirectory &directory, RawReaderLimits limits) {
  if (limits.max_segments == 0 || limits.max_origins == 0)
    throw Error{ErrorCode::invalid_range};
  const auto publication = file_bytes(directory, "published", 40);
  if (publication.size() != 40)
    throw Error{ErrorCode::corrupt};
  const auto last = little(ByteView{publication}.first(8));
  if (last >= limits.max_segments)
    throw Error{ErrorCode::capacity};
  IdentityBytes journal{};
  std::copy_n(publication.begin() + 8, 16, journal.begin());
  const RawAuditCursor preserved{take(AuditStreamId::from_bytes(journal)),
                                 little(ByteView{publication}.subspan(24, 8)),
                                 little(ByteView{publication}.subspan(32, 8))};
  auto descriptions = file_bytes(directory, "descriptions", limits.max_descriptions);
  if (descriptions.size() < 64 || std::memcmp(descriptions.data(), "BBRAWABI", 8) != 0)
    throw Error{ErrorCode::unsupported};
  const auto description_version = little(ByteView{descriptions}.subspan(8, 4));
  if ((description_version != 1 && description_version != 2) ||
      (description_version == 2 && descriptions.size() < 80))
    throw Error{ErrorCode::unsupported};
  const auto first = segment_header(directory, 0);
  if (first.predecessor.has_value())
    throw Error{ErrorCode::conflict};
  RawRunSnapshot snapshot{
      first.journal, crc32c(descriptions), std::move(descriptions), {}, false};
  snapshot.segments.reserve(static_cast<std::size_t>(last + 1));
  for (std::uint64_t index = 0; index <= last; ++index) {
    const auto header = index == 0 ? first : segment_header(directory, index);
    if (header.environment != first.environment ||
        header.issuer_namespace != first.issuer_namespace ||
        header.limits != first.limits)
      throw Error{ErrorCode::conflict};
    for (const auto &prior : snapshot.segments)
      if (prior.header.journal == header.journal)
        throw Error{ErrorCode::conflict};
    auto origins = index == 0 ? std::vector<RawOrigin>{}
                              : seeds(directory, index, limits.max_origins);
    snapshot.segments.push_back(
        {header, {header.journal, 0, journal_header_size}, std::move(origins), 0});
    if (index != 0) {
      const auto &predecessor = header.predecessor;
      auto &prior = snapshot.segments[static_cast<std::size_t>(index - 1)];
      if (!predecessor.has_value() || predecessor->journal != prior.header.journal)
        throw Error{ErrorCode::conflict};
      prior.preserved = {predecessor->journal, predecessor->validated_sequence,
                         predecessor->validated_end_offset};
    }
  }
  if (snapshot.segments.back().header.journal != preserved.journal)
    throw Error{ErrorCode::conflict};
  snapshot.segments.back().preserved = preserved;
  for (std::size_t index = 0; index < snapshot.segments.size(); ++index) {
    auto &segment = snapshot.segments[index];
    auto audit = take(RawAudit::open_reader(
        directory, "events." + std::to_string(index), segment.header, segment.preserved,
        limits.max_origins, segment.seeds));
    if (index + 1 < snapshot.segments.size())
      for (const auto &seed : snapshot.segments[index + 1].seeds)
        if (audit->preserved(seed.lane, seed.partition) != seed.source_start)
          throw Error{ErrorCode::conflict};
    auto file = take(directory.open_existing("events." + std::to_string(index),
                                             FileAccess::read_only));
    const auto extent = take(file->extent());
    if (extent < segment.preserved.offset)
      throw Error{ErrorCode::incomplete};
    segment.unexposed_bytes = extent - segment.preserved.offset;
  }
  auto closure = directory.open_existing("closed", FileAccess::read_only);
  if (closure.has_value()) {
    if (take(closure.value()->extent()) != 0)
      throw Error{ErrorCode::corrupt};
    snapshot.closed = true;
  } else if (closure.error() != Error{ErrorCode::io, ENOENT}) {
    throw closure.error();
  }
  return snapshot;
}
struct DirectoryCloser {
  void operator()(DIR *directory) const noexcept {
    if (directory != nullptr)
      (void)::closedir(directory);
  }
};
} // namespace

std::array<std::byte, raw_run_cursor_size>
encode_raw_run_cursor(const RawRunCursor &cursor) noexcept {
  std::array<std::byte, raw_run_cursor_size> output{};
  std::copy(cursor.run.bytes().begin(), cursor.run.bytes().end(), output.begin());
  put(MutableByteView{output}.subspan(16, 4), cursor.descriptions_crc);
  put(MutableByteView{output}.subspan(24, 8), cursor.segment);
  std::copy(cursor.audit.journal.bytes().begin(), cursor.audit.journal.bytes().end(),
            output.begin() + 32);
  put(MutableByteView{output}.subspan(48, 8), cursor.audit.sequence);
  put(MutableByteView{output}.subspan(56, 8), cursor.audit.offset);
  return output;
}
Result<RawRunCursor> decode_raw_run_cursor(ByteView bytes) {
  using Answer = Result<RawRunCursor>;
  if (bytes.size() != raw_run_cursor_size || little(bytes.subspan(20, 4)) != 0)
    return Answer::failure({ErrorCode::corrupt});
  IdentityBytes run{}, journal{};
  std::copy_n(bytes.begin(), 16, run.begin());
  std::copy_n(bytes.begin() + 32, 16, journal.begin());
  auto run_id = AuditStreamId::from_bytes(run);
  auto journal_id = AuditStreamId::from_bytes(journal);
  if (!run_id.has_value())
    return Answer::failure(run_id.error());
  if (!journal_id.has_value())
    return Answer::failure(journal_id.error());
  return Answer::success({run_id.value(),
                          static_cast<std::uint32_t>(little(bytes.subspan(16, 4))),
                          little(bytes.subspan(24, 8)),
                          {journal_id.value(), little(bytes.subspan(48, 8)),
                           little(bytes.subspan(56, 8))}});
}
RawRunReader::RawRunReader(NativeJournalDirectory directory, RawRunSnapshot snapshot,
                           RawReaderLimits limits)
    : directory_(std::move(directory)), snapshot_(std::move(snapshot)), limits_(limits),
      cursor_{snapshot_.run,
              snapshot_.descriptions_crc,
              0,
              {snapshot_.segments.front().header.journal, 0, journal_header_size}} {}
Result<void> RawRunReader::load(std::uint64_t segment) {
  const auto &entry = snapshot_.segments[static_cast<std::size_t>(segment)];
  audit_.reset();
  auto answer = RawAudit::open_reader(directory_, "events." + std::to_string(segment),
                                      entry.header, entry.preserved,
                                      limits_.max_origins, entry.seeds);
  if (!answer.has_value()) {
    problem_ = answer.error();
    return Result<void>::failure(*problem_);
  }
  audit_ = std::move(answer).value();
  return Result<void>::success();
}
Result<void> RawRunReader::position(const RawRunCursor &cursor) {
  if (cursor.run != snapshot_.run ||
      cursor.descriptions_crc != snapshot_.descriptions_crc ||
      cursor.segment >= snapshot_.segments.size())
    return Result<void>::failure({ErrorCode::conflict});
  const auto loaded = load(cursor.segment);
  if (!loaded.has_value())
    return loaded;
  const auto valid = audit_->validate_cursor(cursor.audit);
  if (!valid.has_value())
    return valid;
  cursor_ = cursor;
  return Result<void>::success();
}
Result<std::unique_ptr<RawRunReader>>
RawRunReader::open(std::string_view path, std::optional<RawRunCursor> resume,
                   RawReaderLimits limits) {
  using Answer = Result<std::unique_ptr<RawRunReader>>;
  auto directory = NativeJournalDirectory::open(path);
  if (!directory.has_value())
    return Answer::failure(directory.error());
  return open(std::move(directory).value(), resume, limits);
}
Result<std::unique_ptr<RawRunReader>>
RawRunReader::open(NativeJournalDirectory directory, std::optional<RawRunCursor> resume,
                   RawReaderLimits limits) {
  using Answer = Result<std::unique_ptr<RawRunReader>>;
  try {
    auto snapshot = capture(directory, limits);
    auto reader = std::unique_ptr<RawRunReader>{
        new RawRunReader{std::move(directory), std::move(snapshot), limits}};
    const auto placed = reader->position(resume.value_or(reader->cursor_));
    if (!placed.has_value())
      return Answer::failure(placed.error());
    return Answer::success(std::move(reader));
  } catch (const Error &error) {
    return Answer::failure(error);
  } catch (const std::bad_alloc &) {
    return Answer::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Answer::failure({ErrorCode::capacity});
  }
}
Result<std::optional<RawRunRead>> RawRunReader::read(MutableByteView output) {
  using Answer = Result<std::optional<RawRunRead>>;
  if (problem_.has_value())
    return Answer::failure(*problem_);
  try {
    for (;;) {
      const auto answer = audit_->read(cursor_.audit, output);
      if (!answer.has_value())
        return Answer::failure(answer.error());
      if (answer.value().has_value()) {
        const auto &record = *answer.value();
        cursor_.audit = record.next;
        return Answer::success(RawRunRead{record.origin, record.raw, cursor_});
      }
      cursor_.audit = audit_->cursor();
      if (cursor_.segment + 1 == snapshot_.segments.size())
        return Answer::success(std::nullopt);
      const auto next = cursor_.segment + 1;
      const auto loaded = load(next);
      if (!loaded.has_value())
        return Answer::failure(loaded.error());
      cursor_.segment = next;
      cursor_.audit = audit_->begin();
    }
  } catch (const std::bad_alloc &) {
    if (!audit_)
      problem_ = Error{ErrorCode::allocation};
    return Answer::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    if (!audit_)
      problem_ = Error{ErrorCode::capacity};
    return Answer::failure({ErrorCode::capacity});
  }
}
Result<std::vector<RawRunEntry>> discover_raw_runs(std::string_view session,
                                                   RawReaderLimits limits) {
  using Answer = Result<std::vector<RawRunEntry>>;
  if (session.empty() || session.find('\0') != std::string_view::npos)
    return Answer::failure({ErrorCode::invalid_range});
  auto directory = NativeJournalDirectory::open(session);
  if (!directory.has_value())
    return Answer::failure(directory.error());
  return discover_raw_runs(std::move(directory).value(), limits);
}
Result<std::vector<RawRunEntry>>
discover_raw_runs(NativeJournalDirectory session_directory, RawReaderLimits limits) {
  using Answer = Result<std::vector<RawRunEntry>>;
  try {
    // The caller's session is private. Its raw child need not itself be mode0700
    // (older runtimes made intermediate directories with the process umask).
    const auto child = session_directory.open_child_directory("raw");
    if (!child.has_value()) {
      if (child.error() == Error{ErrorCode::io, ENOENT})
        return Answer::success({});
      return Answer::failure(child.error());
    }
    const int descriptor = child.value();
    std::unique_ptr<DIR, DirectoryCloser> directory{::fdopendir(descriptor)};
    if (!directory) {
      const auto error = errno;
      (void)::close(descriptor);
      return Answer::failure({ErrorCode::io, error});
    }
    const int directory_descriptor = ::dirfd(directory.get());
    if (directory_descriptor < 0)
      return Answer::failure({ErrorCode::io, errno});
    std::vector<RawRunEntry> entries;
    for (;;) {
      errno = 0;
      const auto *entry = ::readdir(directory.get());
      if (entry == nullptr) {
        if (errno != 0)
          return Answer::failure({ErrorCode::io, errno});
        break;
      }
      const std::string name{entry->d_name};
      if (!name.starts_with("bb"))
        continue;
      struct stat metadata{};
      if (::fstatat(directory_descriptor, name.c_str(), &metadata,
                    AT_SYMLINK_NOFOLLOW) != 0) {
        entries.push_back({name, std::nullopt, Error{ErrorCode::io, errno}});
        continue;
      }
      if (!S_ISDIR(metadata.st_mode)) {
        entries.push_back({name, std::nullopt, Error{ErrorCode::io, EPERM}});
        continue;
      }
      auto run = NativeJournalDirectory::open_at(directory_descriptor, name);
      if (!run.has_value()) {
        entries.push_back({name, std::nullopt, run.error()});
        continue;
      }
      auto reader = RawRunReader::open(std::move(run).value(), std::nullopt, limits);
      if (reader.has_value())
        entries.push_back({name, reader.value()->snapshot(), std::nullopt});
      else
        entries.push_back({name, std::nullopt, reader.error()});
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto &a, const auto &b) { return a.name < b.name; });
    return Answer::success(std::move(entries));
  } catch (const std::bad_alloc &) {
    return Answer::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Answer::failure({ErrorCode::capacity});
  }
}
} // namespace blackbird
