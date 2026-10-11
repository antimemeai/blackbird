#include "blackbird/raw_audit.hpp"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <limits>
#include <stdexcept>

namespace blackbird {
namespace {
std::uint64_t little(ByteView bytes) noexcept {
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < bytes.size(); ++index)
    value |= std::uint64_t{std::to_integer<unsigned char>(bytes[index])} << (index * 8);
  return value;
}
void put(MutableByteView bytes, std::uint64_t value) noexcept {
  for (auto &byte : bytes) {
    byte = static_cast<std::byte>(value & 255);
    value >>= 8;
  }
}
bool valid_partition(RawPartition partition) noexcept {
  return partition == RawPartition::data || partition == RawPartition::control ||
         partition == RawPartition::failure;
}
bool valid_lane(const IdentityBytes &lane) noexcept {
  return std::any_of(lane.begin(), lane.end(),
                     [](auto byte) { return byte != std::byte{0}; });
}
Result<void> validate(const RawAuditRecord &record) {
  if (!valid_lane(record.origin.lane) || !valid_partition(record.origin.partition))
    return Result<void>::failure({ErrorCode::invalid_identity});
  if (record.raw.size() < raw_envelope_size ||
      little(record.raw.first(4)) != record.raw.size() ||
      little(record.raw.subspan(4, 4)) == 0 || little(record.raw.subspan(8, 8)) == 0)
    return Result<void>::failure({ErrorCode::corrupt});
  if (record.raw.size() > UINT64_MAX - record.origin.source_start)
    return Result<void>::failure({ErrorCode::overflow});
  return Result<void>::success();
}
} // namespace

RawAudit::RawAudit(JournalHeader header, std::size_t max_lanes, SyncStrength strength)
    : header_(header), max_lanes_(max_lanes), strength_(strength),
      cursor_{header.journal, 0, journal_header_size} {
  lanes_.reserve(max_lanes);
  staged_.reserve(max_lanes);
  batch_.reserve(header.limits.max_batch_bytes);
  frame_size_ = journal_frame_header_size + header.limits.max_payload;
  frame_ = std::make_unique_for_overwrite<std::byte[]>(frame_size_);
}
RawAudit::~RawAudit() {
  if (file_ && writer_)
    file_->release_writer();
}
Result<std::unique_ptr<RawAudit>>
RawAudit::open(JournalDirectory &directory, std::string_view name, JournalHeader header,
               std::size_t max_lanes, SyncStrength strength,
               std::span<const RawOrigin> predecessor_prefixes) {
  using Answer = Result<std::unique_ptr<RawAudit>>;
  if constexpr (std::endian::native != std::endian::little)
    return Answer::failure({ErrorCode::unsupported});
  if (max_lanes == 0 || name.empty() || name == "." || name == ".." ||
      name.find('/') != std::string_view::npos ||
      name.find('\0') != std::string_view::npos)
    return Answer::failure({ErrorCode::invalid_range});
  const auto encoded = encode_journal_header(header);
  if (!encoded.has_value())
    return Answer::failure(encoded.error());
  if (header.limits.max_payload < raw_origin_size + raw_envelope_size ||
      (!predecessor_prefixes.empty() && !header.predecessor))
    return Answer::failure({ErrorCode::invalid_range});
  try {
    auto audit = std::unique_ptr<RawAudit>{new RawAudit{header, max_lanes, strength}};
    const auto seeded = audit->seed(predecessor_prefixes);
    if (!seeded.has_value())
      return Answer::failure(seeded.error());
    auto opened = directory.open_existing(name, FileAccess::read_write);
    const bool create =
        !opened.has_value() && opened.error() == Error{ErrorCode::io, ENOENT};
    if (create)
      opened = directory.create_exclusive(name);
    if (!opened.has_value())
      return Answer::failure(opened.error());
    audit->file_ = std::move(opened).value();
    const auto locked = audit->file_->lock_writer();
    if (!locked.has_value())
      return Answer::failure(locked.error());
    audit->writer_ = true;
    if (create) {
      const auto written = audit->write_exact(0, encoded.value());
      if (!written.has_value())
        return Answer::failure(written.error());
    } else {
      std::array<std::byte, journal_header_size> bytes{};
      const auto read = audit->read_exact(0, bytes);
      if (!read.has_value())
        return Answer::failure(read.error());
      const auto existing = decode_journal_header(bytes);
      if (!existing.has_value())
        return Answer::failure(existing.error());
      if (existing.value().environment != header.environment)
        return Answer::failure({ErrorCode::wrong_environment});
      if (existing.value() != header)
        return Answer::failure({ErrorCode::conflict});
      const auto recovered = audit->recover();
      if (!recovered.has_value())
        return Answer::failure(recovered.error());
    }
    const auto synced = audit->file_->synchronize(strength);
    if (!synced.has_value())
      return Answer::failure(synced.error());
    const auto named = directory.synchronize_directory(strength);
    if (!named.has_value())
      return Answer::failure(named.error());
    return Answer::success(std::move(audit));
  } catch (const std::bad_alloc &) {
    return Answer::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Answer::failure({ErrorCode::capacity});
  }
}
Result<void> RawAudit::seed(std::span<const RawOrigin> predecessor_prefixes) {
  if (!predecessor_prefixes.empty() && !header_.predecessor)
    return Result<void>::failure({ErrorCode::invalid_range});
  for (const auto &prefix : predecessor_prefixes) {
    if (!valid_lane(prefix.lane) || !valid_partition(prefix.partition))
      return Result<void>::failure({ErrorCode::invalid_identity});
    if (lanes_.size() == max_lanes_)
      return Result<void>::failure({ErrorCode::capacity});
    for (const auto &old : lanes_)
      if (old.lane == prefix.lane && old.partition == prefix.partition)
        return Result<void>::failure({ErrorCode::conflict});
    lanes_.push_back({prefix.lane, prefix.partition, prefix.source_start});
  }
  return Result<void>::success();
}
Result<std::unique_ptr<RawAudit>>
RawAudit::open_reader(JournalDirectory &directory, std::string_view name,
                      JournalHeader header, RawAuditCursor preserved_prefix,
                      std::size_t max_lanes,
                      std::span<const RawOrigin> predecessor_prefixes) {
  using Answer = Result<std::unique_ptr<RawAudit>>;
  if (max_lanes == 0 || preserved_prefix.journal != header.journal ||
      preserved_prefix.offset < journal_header_size)
    return Answer::failure({ErrorCode::invalid_range});
  const auto encoded = encode_journal_header(header);
  if (!encoded.has_value())
    return Answer::failure(encoded.error());
  if (header.limits.max_payload < raw_origin_size + raw_envelope_size)
    return Answer::failure({ErrorCode::invalid_range});
  try {
    auto audit =
        std::unique_ptr<RawAudit>{new RawAudit{header, max_lanes, SyncStrength::full}};
    const auto seeded = audit->seed(predecessor_prefixes);
    if (!seeded.has_value())
      return Answer::failure(seeded.error());
    auto file = directory.open_existing(name, FileAccess::read_only);
    if (!file.has_value())
      return Answer::failure(file.error());
    audit->file_ = std::move(file).value();
    std::array<std::byte, journal_header_size> bytes{};
    const auto read = audit->read_exact(0, bytes);
    if (!read.has_value())
      return Answer::failure(read.error());
    if (bytes != encoded.value())
      return Answer::failure({ErrorCode::conflict});
    const auto recovered = audit->recover(preserved_prefix);
    if (!recovered.has_value())
      return Answer::failure(recovered.error());
    if (audit->problem_)
      return Answer::failure(*audit->problem_);
    if (audit->cursor_ != preserved_prefix)
      return Answer::failure({ErrorCode::corrupt});
    return Answer::success(std::move(audit));
  } catch (const std::bad_alloc &) {
    return Answer::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Answer::failure({ErrorCode::capacity});
  }
}
Result<void> RawAudit::read_exact(std::uint64_t offset, MutableByteView output) {
  std::size_t read = 0;
  while (read < output.size()) {
    const auto result = file_->read_at(offset + read, output.subspan(read));
    if (!result.has_value()) {
      if (result.error().code == ErrorCode::interrupted)
        continue;
      return Result<void>::failure(result.error());
    }
    if (result.value() == 0)
      return Result<void>::failure({ErrorCode::incomplete});
    if (result.value() > output.size() - read)
      return Result<void>::failure({ErrorCode::io});
    read += result.value();
  }
  return Result<void>::success();
}
Result<void> RawAudit::write_exact(std::uint64_t offset, ByteView bytes) {
  progress_ = {offset, 0, false};
  while (progress_.written_bytes < bytes.size()) {
    const auto result = file_->write_at(
        offset + progress_.written_bytes,
        bytes.subspan(static_cast<std::size_t>(progress_.written_bytes)));
    if (!result.has_value()) {
      if (result.error().code == ErrorCode::interrupted)
        continue;
      return Result<void>::failure(result.error());
    }
    if (result.value() == 0 || result.value() > bytes.size() - progress_.written_bytes)
      return Result<void>::failure({ErrorCode::io});
    progress_.written_bytes += result.value();
  }
  return Result<void>::success();
}
Result<JournalFrameView> RawAudit::frame(std::uint64_t offset, std::uint64_t end) {
  using Answer = Result<JournalFrameView>;
  if (offset > end || end - offset < journal_frame_header_size)
    return Answer::failure({ErrorCode::incomplete});
  auto bytes = MutableByteView{frame_.get(), frame_size_};
  const auto read_header = read_exact(offset, bytes.first(journal_frame_header_size));
  if (!read_header.has_value())
    return Answer::failure(read_header.error());
  const auto size = little(ByteView{bytes}.subspan(24, 4));
  if (size > header_.limits.max_payload)
    return Answer::failure({ErrorCode::capacity});
  if (size > end - offset - journal_frame_header_size)
    return Answer::failure({ErrorCode::incomplete});
  const auto payload =
      bytes.subspan(journal_frame_header_size, static_cast<std::size_t>(size));
  const auto read_payload = read_exact(offset + journal_frame_header_size, payload);
  if (!read_payload.has_value())
    return Answer::failure(read_payload.error());
  return decode_journal_frame(bytes.first(journal_frame_header_size + payload.size()),
                              header_.limits);
}
Result<RawAuditRecord> RawAudit::raw_record(ByteView bytes) {
  if (bytes.size() < raw_origin_size + raw_envelope_size)
    return Result<RawAuditRecord>::failure({ErrorCode::corrupt});
  RawOrigin origin{};
  std::copy_n(bytes.begin(), origin.lane.size(), origin.lane.begin());
  origin.partition = static_cast<RawPartition>(little(bytes.subspan(16, 4)));
  origin.source_start = little(bytes.subspan(24, 8));
  if (little(bytes.subspan(20, 4)) != 0)
    return Result<RawAuditRecord>::failure({ErrorCode::corrupt});
  RawAuditRecord record{origin, bytes.subspan(raw_origin_size)};
  const auto valid = validate(record);
  if (!valid.has_value())
    return Result<RawAuditRecord>::failure(valid.error());
  return Result<RawAuditRecord>::success(record);
}
Result<void> RawAudit::advance(std::vector<LanePrefix> &lanes,
                               const RawAuditRecord &record) {
  for (auto &lane : lanes) {
    if (lane.lane == record.origin.lane && lane.partition == record.origin.partition) {
      if (lane.end != record.origin.source_start)
        return Result<void>::failure({ErrorCode::conflict});
      lane.end += record.raw.size();
      return Result<void>::success();
    }
  }
  if (record.origin.source_start != 0)
    return Result<void>::failure({ErrorCode::conflict});
  if (lanes.size() == max_lanes_)
    return Result<void>::failure({ErrorCode::capacity});
  lanes.push_back({record.origin.lane, record.origin.partition, record.raw.size()});
  return Result<void>::success();
}
Result<void> RawAudit::recover(std::optional<RawAuditCursor> limit) {
  const auto extent = file_->extent();
  if (!extent.has_value())
    return Result<void>::failure(extent.error());
  if (extent.value() < journal_header_size)
    return Result<void>::failure({ErrorCode::incomplete});
  if (limit && limit->offset > extent.value())
    return Result<void>::failure({ErrorCode::incomplete});
  const auto end = limit ? limit->offset : extent.value();
  std::uint64_t offset = journal_header_size;
  std::uint64_t sequence = 1;
  std::uint64_t first = 1;
  std::uint64_t count = 0;
  std::size_t batch_bytes = 0;
  std::uint32_t checksum = 0;
  staged_ = lanes_;
  while (offset < end) {
    const auto decoded = frame(offset, end);
    if (!decoded.has_value()) {
      if (decoded.error().code == ErrorCode::io)
        return Result<void>::failure(decoded.error());
      problem_ = decoded.error();
      break;
    }
    const auto &view = decoded.value();
    if (view.sequence != sequence || view.batch_first != first ||
        view.encoded_size > header_.limits.max_batch_bytes - batch_bytes) {
      problem_ = Error{ErrorCode::corrupt};
      break;
    }
    if (view.kind == FrameKind::commit) {
      if (count == 0 || view.payload.size() != 24 ||
          little(view.payload.first(8)) != first ||
          little(view.payload.subspan(8, 8)) != count ||
          little(view.payload.subspan(16, 4)) != checksum ||
          little(view.payload.subspan(20, 4)) != 0) {
        problem_ = Error{ErrorCode::corrupt};
        break;
      }
      lanes_ = staged_;
      cursor_ = {header_.journal, sequence, offset + view.encoded_size};
      first = sequence + 1;
      count = 0;
      batch_bytes = 0;
      checksum = 0;
    } else if (view.kind == FrameKind::raw) {
      const auto record = raw_record(view.payload);
      if (!record.has_value()) {
        problem_ = record.error();
        break;
      }
      const auto advanced = advance(staged_, record.value());
      if (!advanced.has_value()) {
        problem_ = advanced.error();
        break;
      }
      ++count;
      batch_bytes += view.encoded_size;
      checksum = crc32c(ByteView{frame_.get(), frame_size_}.first(view.encoded_size),
                        checksum);
    } else {
      problem_ = Error{ErrorCode::corrupt};
      break;
    }
    offset += view.encoded_size;
    if (sequence == UINT64_MAX) {
      if (offset != end)
        problem_ = Error{ErrorCode::overflow};
      break;
    }
    ++sequence;
  }
  if (!problem_ && count != 0)
    problem_ = Error{ErrorCode::incomplete};
  staged_ = lanes_;
  return Result<void>::success();
}
std::uint64_t RawAudit::preserved(const IdentityBytes &lane,
                                  RawPartition partition) const {
  for (const auto &entry : lanes_)
    if (entry.lane == lane && entry.partition == partition)
      return entry.end;
  return 0;
}
RawAuditCursor RawAudit::begin() const noexcept {
  return {header_.journal, 0, journal_header_size};
}
Result<void> RawAudit::validate_cursor(RawAuditCursor cursor) {
  if (cursor.journal != header_.journal || cursor.offset < journal_header_size ||
      cursor.offset > cursor_.offset || cursor.sequence > cursor_.sequence)
    return Result<void>::failure({ErrorCode::invalid_range});
  auto current = begin();
  while (current.offset < cursor.offset) {
    const auto decoded = frame(current.offset, cursor_.offset);
    if (!decoded.has_value())
      return Result<void>::failure(decoded.error());
    const auto &view = decoded.value();
    if (current.sequence == UINT64_MAX || view.sequence != current.sequence + 1)
      return Result<void>::failure({ErrorCode::corrupt});
    current.sequence = view.sequence;
    current.offset += view.encoded_size;
  }
  if (current != cursor)
    return Result<void>::failure({ErrorCode::invalid_range});
  return Result<void>::success();
}
Result<std::optional<RawAuditRead>> RawAudit::read(RawAuditCursor cursor,
                                                   MutableByteView output) {
  using Answer = Result<std::optional<RawAuditRead>>;
  if (cursor.journal != header_.journal || cursor.offset < journal_header_size ||
      cursor.offset > cursor_.offset || cursor.sequence > cursor_.sequence)
    return Answer::failure({ErrorCode::invalid_range});
  while (cursor.offset < cursor_.offset) {
    const auto decoded = frame(cursor.offset, cursor_.offset);
    if (!decoded.has_value())
      return Answer::failure(decoded.error());
    const auto &view = decoded.value();
    if (cursor.sequence == UINT64_MAX || view.sequence != cursor.sequence + 1)
      return Answer::failure({ErrorCode::corrupt});
    cursor.sequence = view.sequence;
    cursor.offset += view.encoded_size;
    if (view.kind == FrameKind::commit)
      continue;
    if (view.kind != FrameKind::raw)
      return Answer::failure({ErrorCode::corrupt});
    const auto record = raw_record(view.payload);
    if (!record.has_value())
      return Answer::failure(record.error());
    if (record.value().raw.size() > output.size())
      return Answer::failure({ErrorCode::capacity});
    std::copy(record.value().raw.begin(), record.value().raw.end(), output.begin());
    return Answer::success(
        RawAuditRead{record.value().origin,
                     ByteView{output}.first(record.value().raw.size()), cursor});
  }
  if (cursor.sequence != cursor_.sequence)
    return Answer::failure({ErrorCode::invalid_range});
  return Answer::success(std::nullopt);
}
Result<bool> RawAudit::duplicate(const RawAuditRecord &record) {
  auto cursor = begin();
  while (cursor.offset < cursor_.offset) {
    const auto decoded = frame(cursor.offset, cursor_.offset);
    if (!decoded.has_value())
      return Result<bool>::failure(decoded.error());
    const auto &view = decoded.value();
    cursor.offset += view.encoded_size;
    if (view.kind != FrameKind::raw)
      continue;
    const auto old = raw_record(view.payload);
    if (!old.has_value())
      return Result<bool>::failure(old.error());
    if (old.value().origin == record.origin)
      return Result<bool>::success(std::equal(record.raw.begin(), record.raw.end(),
                                              old.value().raw.begin(),
                                              old.value().raw.end()));
  }
  return Result<bool>::success(false);
}
Result<void> RawAudit::append(std::span<const RawAuditRecord> records) {
  progress_ = {cursor_.offset, 0, false};
  if (!writer_)
    return Result<void>::failure({ErrorCode::unsupported});
  if (problem_)
    return Result<void>::failure({ErrorCode::audit_unavailable});
  if (records.empty())
    return Result<void>::failure({ErrorCode::invalid_range});
  batch_.clear();
  staged_ = lanes_;
  if (cursor_.sequence == UINT64_MAX)
    return Result<void>::failure({ErrorCode::overflow});
  const auto first = cursor_.sequence + 1;
  std::uint64_t count = 0;
  for (const auto &record : records) {
    const auto valid = validate(record);
    if (!valid.has_value())
      return valid;
    const auto prior = preserved(record.origin.lane, record.origin.partition);
    if (record.origin.source_start < prior) {
      if (record.raw.size() > prior - record.origin.source_start)
        return Result<void>::failure({ErrorCode::conflict});
      const auto old = duplicate(record);
      if (!old.has_value())
        return Result<void>::failure(old.error());
      if (!old.value())
        return Result<void>::failure({ErrorCode::conflict});
      continue;
    }
    if (record.raw.size() > header_.limits.max_payload - raw_origin_size ||
        batch_.size() + 56 > header_.limits.max_batch_bytes ||
        record.raw.size() + raw_origin_size + journal_frame_header_size >
            header_.limits.max_batch_bytes - batch_.size() - 56)
      return Result<void>::failure({ErrorCode::capacity});
    if (count >= UINT64_MAX - first)
      return Result<void>::failure({ErrorCode::overflow});
    const auto advanced = advance(staged_, record);
    if (!advanced.has_value())
      return advanced;
    const auto size = journal_frame_header_size + raw_origin_size + record.raw.size();
    const auto offset = batch_.size();
    batch_.resize(offset + size);
    auto bytes = MutableByteView{batch_}.subspan(offset);
    auto origin = bytes.subspan(journal_frame_header_size, raw_origin_size);
    std::copy(record.origin.lane.begin(), record.origin.lane.end(), origin.begin());
    put(origin.subspan(16, 4), static_cast<std::uint32_t>(record.origin.partition));
    put(origin.subspan(20, 4), 0);
    put(origin.subspan(24, 8), record.origin.source_start);
    std::copy(record.raw.begin(), record.raw.end(),
              bytes.begin() + journal_frame_header_size + raw_origin_size);
    const auto encoded = encode_journal_frame_into(
        bytes, FrameKind::raw, first + count, first,
        ByteView{bytes}.subspan(journal_frame_header_size), header_.limits);
    if (!encoded.has_value())
      return Result<void>::failure(encoded.error());
    ++count;
  }
  if (count == 0)
    return Result<void>::success();
  std::array<std::byte, 24> commit{};
  put(MutableByteView{commit}.first(8), first);
  put(MutableByteView{commit}.subspan(8, 8), count);
  put(MutableByteView{commit}.subspan(16, 4), crc32c(batch_));
  const auto size = batch_.size();
  batch_.resize(size + 56);
  const auto encoded = encode_journal_frame_into(MutableByteView{batch_}.subspan(size),
                                                 FrameKind::commit, first + count,
                                                 first, commit, header_.limits);
  if (!encoded.has_value())
    return Result<void>::failure(encoded.error());
  if (cursor_.offset > INT64_MAX || batch_.size() > INT64_MAX - cursor_.offset)
    return Result<void>::failure({ErrorCode::overflow});
  const auto extent = file_->extent();
  if (!extent.has_value())
    return Result<void>::failure(extent.error());
  if (extent.value() != cursor_.offset) {
    problem_ = Error{ErrorCode::corrupt};
    return Result<void>::failure(*problem_);
  }
  const auto written = write_exact(cursor_.offset, batch_);
  if (!written.has_value()) {
    problem_ = written.error();
    return written;
  }
  progress_.sync_attempted = true;
  const auto synced = file_->synchronize(strength_);
  if (!synced.has_value()) {
    problem_ = synced.error();
    return synced;
  }
  lanes_.swap(staged_);
  cursor_ = {header_.journal, first + count, cursor_.offset + batch_.size()};
  return Result<void>::success();
}

} // namespace blackbird
