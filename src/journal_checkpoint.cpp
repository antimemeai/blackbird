#include "blackbird/journal_writer.hpp"

#include <array>
#include <chrono>
#include <limits>

namespace blackbird {
namespace {
// Physical hints, not a durable semantic/current-context snapshot.
constexpr std::uint64_t magic = 0x31544e4948424242ULL;
constexpr std::size_t prefix_size = 160;
constexpr std::size_t entry_size = 48;
constexpr std::size_t commit_size = journal_frame_header_size + 24;
void put(MutableByteView bytes, std::uint64_t value) noexcept {
  for (auto &byte : bytes) {
    byte = static_cast<std::byte>(value & 255U);
    value >>= 8;
  }
}
std::uint64_t get(ByteView bytes) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i)
    value |= static_cast<std::uint64_t>(std::to_integer<unsigned>(bytes[i])) << (i * 8);
  return value;
}
Result<void> read_all(Storage &file, std::uint64_t offset, MutableByteView bytes) {
  std::size_t done = 0;
  unsigned interruptions = 0;
  while (done < bytes.size()) {
    auto read = file.read_at(offset + done, bytes.subspan(done));
    if (!read.has_value()) {
      if (read.error().code == ErrorCode::interrupted && ++interruptions < 8)
        continue;
      return Result<void>::failure(read.error());
    }
    if (read.value() == 0 || read.value() > bytes.size() - done)
      return Result<void>::failure(
          {read.value() == 0 ? ErrorCode::incomplete : ErrorCode::corrupt});
    done += read.value();
  }
  return Result<void>::success();
}
Result<void> write_all(Storage &file, ByteView bytes) {
  std::size_t done = 0;
  while (done < bytes.size()) {
    auto written = file.write_at(done, bytes.subspan(done));
    // A failed write/sync/publication is reported, never retried here.
    if (!written.has_value())
      return Result<void>::failure(written.error());
    if (written.value() == 0 || written.value() > bytes.size() - done)
      return Result<void>::failure({ErrorCode::incomplete});
    done += written.value();
  }
  return Result<void>::success();
}
} // namespace

Result<std::optional<FramedJournal::ScanCheckpoint>>
FramedJournal::load_scan_checkpoint(unsigned slot) {
  RawRetainedFields arguments;
  arguments.native(slot);
  RawRetainedPair trace{raw_origin_, {3228, 3229}, 138, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<std::optional<ScanCheckpoint>> {
    using Answer = Result<std::optional<ScanCheckpoint>>;
    const auto ignored = [] { return Answer::success(std::nullopt); };
    try {
      auto opened = directory_.open_existing(name_ + ".scan." + std::to_string(slot),
                                             FileAccess::read_only);
      if (!opened.has_value()) {
        trace.finish(5, {}, opened.error());
        return ignored();
      }
      auto file = std::move(opened).value();
      auto extent = file->extent();
      if (!extent.has_value()) {
        trace.finish(5, {}, extent.error());
        return ignored();
      }
      if (extent.value() < prefix_size + 4 ||
          (extent.value() - prefix_size - 4) % entry_size != 0 ||
          (extent.value() - prefix_size - 4) / entry_size > capacity_.max_records ||
          extent.value() > std::numeric_limits<std::size_t>::max())
        return ignored();
      std::vector<std::byte> bytes(static_cast<std::size_t>(extent.value()));
      const auto loaded = read_all(*file, 0, bytes);
      if (!loaded.has_value()) {
        trace.finish(5, {}, loaded.error());
        return ignored();
      }
      const ByteView view{bytes};
      if (get(view.first(8)) != magic || get(view.subspan(8, 8)) != 1 ||
          get(view.last(4)) != crc32c(view.first(view.size() - 4)))
        return ignored();
      const auto canonical = encode_journal_header(header_);
      if (!canonical.has_value() ||
          !std::equal(canonical.value().begin(), canonical.value().end(),
                      bytes.begin() + 16))
        return ignored();
      const auto sequence = get(view.subspan(128, 8));
      const auto end = get(view.subspan(136, 8));
      const auto count = get(view.subspan(144, 8));
      const auto anchor_crc = get(view.subspan(152, 8));
      auto journal_extent = file_->extent();
      if (!journal_extent.has_value()) {
        trace.finish(5, {}, journal_extent.error());
        return ignored();
      }
      if (end < journal_header_size + commit_size || sequence == 0 || count == 0 ||
          count != (bytes.size() - prefix_size - 4) / entry_size ||
          anchor_crc > UINT32_MAX)
        return ignored();
      ScanCheckpoint checkpoint{{header_.journal, sequence, end}, {}, slot};
      checkpoint.records.reserve(static_cast<std::size_t>(count));
      std::uint64_t next_sequence = 1;
      std::uint64_t next_offset = journal_header_size;
      std::uint64_t batch_first = 1;
      std::uint64_t batch_bytes = 0;
      std::uint64_t last_batch_count = 0;
      for (std::size_t i = 0; i < count; ++i) {
        const auto item = view.subspan(prefix_size + i * entry_size, entry_size);
        const auto kind = get(item.first(8));
        const auto seq = get(item.subspan(8, 8));
        const auto first = get(item.subspan(16, 8));
        const auto offset = get(item.subspan(24, 8));
        const auto size = get(item.subspan(32, 8));
        const auto checksum = get(item.subspan(40, 8));
        if (first != batch_first) {
          if (i == 0 || batch_bytes + commit_size > header_.limits.max_batch_bytes ||
              next_sequence == UINT64_MAX || next_offset > end - commit_size)
            return ignored();
          ++next_sequence; // preceding batch commit
          next_offset += commit_size;
          batch_first = next_sequence;
          batch_bytes = 0;
          last_batch_count = 0;
        }
        if ((kind != 1 && kind != 2) || seq != next_sequence || first != batch_first ||
            seq == UINT64_MAX || next_offset > end ||
            end - next_offset < journal_frame_header_size ||
            offset != next_offset + journal_frame_header_size ||
            size > header_.limits.max_payload || size > end - offset ||
            checksum > UINT32_MAX)
          return ignored();
        const auto encoded = size + journal_frame_header_size;
        batch_bytes += encoded;
        if (batch_bytes + commit_size > header_.limits.max_batch_bytes)
          return ignored();
        checkpoint.records.push_back({header_.environment, header_.journal,
                                      static_cast<FrameKind>(kind), seq, first, offset,
                                      static_cast<std::uint32_t>(size),
                                      static_cast<std::uint32_t>(checksum)});
        RawRetainedFields selected;
        selected.physical(checkpoint.records.back());
        (void)raw_retained_event(raw_origin_, {3162, 106}, selected.view());
        next_offset = offset + size;
        ++next_sequence;
        ++last_batch_count;
      }
      if (next_sequence != sequence || next_offset > end ||
          end - next_offset != commit_size)
        return ignored();
      // A correctly covered identity/boundary beyond EOF is evidence of lost data,
      // not permission to select a smaller prefix and reuse its IDs.
      if (end > journal_extent.value())
        return Answer::failure({ErrorCode::incomplete});
      std::array<std::byte, commit_size> anchor{};
      const auto acquired_anchor = read_all(*file_, next_offset, anchor);
      if (!acquired_anchor.has_value()) {
        trace.finish(5, {}, acquired_anchor.error());
        return ignored();
      }
      if (crc32c(anchor) != anchor_crc)
        return ignored();
      const auto decoded = decode_journal_frame(anchor, header_.limits);
      if (!decoded.has_value() || decoded.value().kind != FrameKind::commit ||
          decoded.value().sequence != sequence ||
          decoded.value().batch_first != batch_first ||
          decoded.value().payload.size() != 24 ||
          get(decoded.value().payload.first(8)) != batch_first ||
          get(decoded.value().payload.subspan(8, 8)) != last_batch_count ||
          get(decoded.value().payload.subspan(20, 4)) != 0)
        return ignored();
      return Answer::success(std::move(checkpoint));
    } catch (const std::bad_alloc &) {
      return Answer::failure({ErrorCode::allocation});
    }
  });
  if (!answer.has_value()) {
    trace.result(answer);
  } else {
    const auto &candidate = answer.value();
    if (!candidate.has_value()) {
      trace.finish(4);
      return answer;
    }
    const auto &selected = *candidate;
    const auto count = selected.records.size();
    RawRetainedFields returned;
    returned.cursor(selected.cursor);
    returned.native(count);
    returned.native(selected.slot);
    trace.result(answer, returned.view());
  }
  return answer;
}

Result<void> FramedJournal::publish_scan_checkpoint() {
  const auto record_count = records_.size();
  RawRetainedFields arguments;
  arguments.cursor(cursor_);
  arguments.native(record_count);
  arguments.native(checkpoint_attempt_);
  arguments.native(strength_);
  RawRetainedPair trace{raw_origin_, {3230, 3231}, 188, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  std::optional<unsigned> selected_slot;
  auto answer = trace.run([&]() -> Result<void> {
    if (in_restage_ || state_ != JournalWriterState::live || historical_ ||
        archived_records_)
      return Result<void>::failure({ErrorCode::audit_unavailable});
    if (records_.empty())
      return Result<void>::success();
    (void)directory_.reclaim_publication_scratch(name_);
    try {
      auto extent = file_->extent();
      if (!extent.has_value())
        return Result<void>::failure(extent.error());
      if (extent.value() != cursor_.end_offset)
        return Result<void>::failure({ErrorCode::conflict});
      auto a = load_scan_checkpoint(0);
      auto b = load_scan_checkpoint(1);
      if (!a.has_value() || !b.has_value())
        return Result<void>::failure(!a.has_value() ? a.error() : b.error());
      auto previous = std::move(a).value();
      auto other = std::move(b).value();
      if (other && (!previous || other->cursor.sequence > previous->cursor.sequence))
        previous = std::move(other);
      if (previous && previous->cursor.sequence == cursor_.sequence &&
          previous->cursor.end_offset == cursor_.end_offset &&
          previous->records == records_)
        return directory_.synchronize_directory(strength_);
      const unsigned slot = previous ? (previous->slot ^ 1U) : 0U;
      selected_slot = slot;
      if (records_.size() >
          (std::numeric_limits<std::size_t>::max() - prefix_size - 4) / entry_size)
        return Result<void>::failure({ErrorCode::overflow});
      std::vector<std::byte> bytes(prefix_size + records_.size() * entry_size + 4);
      MutableByteView view{bytes};
      put(view.first(8), magic);
      put(view.subspan(8, 8), 1);
      const auto header = encode_journal_header(header_);
      if (!header.has_value())
        return Result<void>::failure(header.error());
      std::copy(header.value().begin(), header.value().end(), bytes.begin() + 16);
      put(view.subspan(128, 8), cursor_.sequence);
      put(view.subspan(136, 8), cursor_.end_offset);
      put(view.subspan(144, 8), records_.size());
      std::array<std::byte, commit_size> anchor{};
      auto read = read_exact(cursor_.end_offset - commit_size, anchor);
      if (!read.has_value())
        return read;
      put(view.subspan(152, 8), crc32c(anchor));
      for (std::size_t i = 0; i < records_.size(); ++i) {
        auto item = view.subspan(prefix_size + i * entry_size, entry_size);
        const auto &record = records_[i];
        RawRetainedFields selected;
        selected.physical(record);
        (void)raw_retained_event(raw_origin_, {3162, 106}, selected.view());
        put(item.first(8), static_cast<std::uint16_t>(record.kind));
        put(item.subspan(8, 8), record.sequence);
        put(item.subspan(16, 8), record.batch_first);
        put(item.subspan(24, 8), record.payload_offset);
        put(item.subspan(32, 8), record.payload_size);
        put(item.subspan(40, 8), record.frame_checksum);
      }
      put(view.last(4), crc32c(ByteView{bytes}.first(bytes.size() - 4)));
      // Journal bytes precede root bytes; rename never stands in for a sync.
      auto synced = file_->synchronize(strength_);
      if (!synced.has_value())
        return synced;
      const auto target = name_ + ".scan." + std::to_string(slot);
      const auto temporary =
          name_ + ".scan.tmp." + std::to_string(cursor_.sequence) + "." +
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
          "." + std::to_string(++checkpoint_attempt_);
      auto created = directory_.create_exclusive(temporary);
      if (!created.has_value())
        return Result<void>::failure(created.error());
      auto file = std::move(created).value();
      auto written = write_all(*file, bytes);
      if (!written.has_value())
        return written;
      synced = file->synchronize(strength_);
      if (!synced.has_value())
        return synced;
      const auto replaced = directory_.replace_file(temporary, target);
      if (!replaced.has_value())
        return replaced;
      return directory_.synchronize_directory(strength_);
    } catch (const std::bad_alloc &) {
      return Result<void>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  returned.cursor(cursor_);
  returned.native(used_scan_checkpoint_);
  returned.native(checkpoint_attempt_);
  if (selected_slot)
    returned.native(*selected_slot);
  trace.result(answer, returned.view());
  return answer;
}
} // namespace blackbird
