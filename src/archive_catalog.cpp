#include "blackbird/archive_catalog.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
namespace blackbird {
namespace {
constexpr std::size_t prefix_size = 160;
constexpr std::size_t entry_size = 96;
constexpr std::uint64_t magic = 0x3154414341524242ULL;
void put(MutableByteView out, std::uint64_t n) {
  for (auto &b : out) {
    b = static_cast<std::byte>(n & 255U);
    n >>= 8;
  }
}
std::uint64_t get(ByteView in) {
  std::uint64_t n = 0;
  for (std::size_t i = 0; i < in.size(); ++i)
    n |= static_cast<std::uint64_t>(std::to_integer<unsigned>(in[i])) << (8 * i);
  return n;
}
Result<void> read_at(Storage &file, std::uint64_t offset, MutableByteView out) {
  std::size_t done = 0;
  unsigned interruptions = 0;
  while (done < out.size()) {
    auto n = file.read_at(offset + done, out.subspan(done));
    if (!n.has_value()) {
      if (n.error().code == ErrorCode::interrupted && ++interruptions < 8)
        continue;
      return Result<void>::failure(n.error());
    }
    if (!n.value() || n.value() > out.size() - done)
      return Result<void>::failure({ErrorCode::incomplete});
    done += n.value();
  }
  return Result<void>::success();
}
Result<void> write_at(Storage &file, std::uint64_t offset, ByteView in) {
  std::size_t done = 0;
  while (done < in.size()) {
    auto n = file.write_at(offset + done, in.subspan(done));
    if (!n.has_value())
      return Result<void>::failure(n.error());
    if (!n.value() || n.value() > in.size() - done)
      return Result<void>::failure({ErrorCode::incomplete});
    done += n.value();
  }
  return Result<void>::success();
}
std::array<std::byte, entry_size> encode(const ArchiveEntry &entry,
                                         std::uint32_t ordinal) {
  std::array<std::byte, entry_size> bytes{};
  MutableByteView out{bytes};
  std::copy(entry.key.begin(), entry.key.end(), bytes.begin());
  const auto &r = entry.record;
  put(out.subspan(40, 8), static_cast<std::uint16_t>(r.kind));
  put(out.subspan(48, 8), r.sequence);
  put(out.subspan(56, 8), r.batch_first);
  put(out.subspan(64, 8), r.payload_offset);
  put(out.subspan(72, 8), r.payload_size);
  put(out.subspan(80, 8), r.frame_checksum);
  put(out.subspan(88, 4), ordinal);
  put(out.subspan(92, 4), crc32c(ByteView{bytes}.first(92)));
  return bytes;
}
} // namespace
static Result<void>
publish_entries(JournalDirectory &directory, std::string_view name,
                JournalHeader header, JournalCursor boundary, std::uint64_t count,
                const std::function<Result<ArchiveEntry>(std::uint64_t)> &next,
                const RawRetainedOrigin &raw_origin) {
  try {
    if (count > UINT32_MAX || boundary.journal != header.journal ||
        boundary.end_offset < journal_header_size)
      return Result<void>::failure({ErrorCode::invalid_range});
    std::array<std::byte, prefix_size> bytes{};
    MutableByteView out{bytes};
    put(out.first(8), magic);
    put(out.subspan(8, 8), 2);
    auto h = encode_journal_header(header);
    if (!h.has_value())
      return Result<void>::failure(h.error());
    std::copy(h.value().begin(), h.value().end(), bytes.begin() + 16);
    put(out.subspan(128, 8), boundary.sequence);
    put(out.subspan(136, 8), boundary.end_offset);
    put(out.subspan(144, 8), count);
    put(out.subspan(152, 4), crc32c(ByteView{bytes}.first(152)));
    const auto temporary =
        std::string{name} + ".tmp." +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto created = directory.create_exclusive(temporary);
    if (!created.has_value())
      return Result<void>::failure(created.error());
    auto file = std::move(created).value();
    auto written = write_at(*file, 0, bytes);
    if (!written.has_value())
      return written;
    // Flush complete contiguous batches. The on-disk index is O(keys), not O(keys *
    // path).
    constexpr std::size_t batch_entries = 128;
    std::optional<RecoveryKey> prior;
    std::vector<std::byte> batch;
    batch.reserve(batch_entries * entry_size);
    for (std::size_t first = 0; first < count;) {
      batch.clear();
      const auto end = std::min<std::uint64_t>(count, first + batch_entries);
      for (std::size_t i = first; i < end; ++i) {
        auto loaded = next(i);
        if (!loaded.has_value())
          return Result<void>::failure(loaded.error());
        const auto &entry = loaded.value();
        const auto &r = entry.record;
        if ((prior && !(prior.value() < entry.key)) || r.journal != header.journal ||
            r.environment != header.environment || !r.sequence ||
            r.sequence >= boundary.sequence || !r.batch_first ||
            r.batch_first > r.sequence ||
            r.payload_offset < journal_header_size + journal_frame_header_size ||
            r.payload_offset > boundary.end_offset ||
            r.payload_size > boundary.end_offset - r.payload_offset ||
            r.payload_size > header.limits.max_payload ||
            (r.kind != FrameKind::source && r.kind != FrameKind::semantic))
          return Result<void>::failure({ErrorCode::invalid_range});
        prior = entry.key;
        RawRetainedFields selected;
        selected.native(i);
        selected.add(entry.key);
        selected.physical(entry.record);
        (void)raw_retained_event(raw_origin, {3208, 154}, selected.view());
        const auto item = encode(entry, static_cast<std::uint32_t>(i));
        batch.insert(batch.end(), item.begin(), item.end());
      }
      written = write_at(*file, prefix_size + first * entry_size, batch);
      if (!written.has_value())
        return written;
      first = end;
    }
    auto synced = file->synchronize(SyncStrength::full);
    if (!synced.has_value())
      return synced;
    auto replaced = directory.replace_file(temporary, name);
    if (!replaced.has_value())
      return replaced;
    return directory.synchronize_directory(SyncStrength::full);
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
Result<void> ArchiveCatalog::publish(JournalDirectory &directory, std::string_view name,
                                     JournalHeader header, JournalCursor boundary,
                                     std::span<const ArchiveEntry> sorted,
                                     const RawRetainedOrigin &raw_origin) {
  const auto input_count = sorted.size();
  const bool previous_present = false;
  RawRetainedFields arguments;
  arguments.header(header);
  arguments.cursor(boundary);
  arguments.native(input_count);
  arguments.native(previous_present);
  RawRetainedPair trace{raw_origin, {3200, 3201}, 228, arguments.view()};
  auto answer = trace.run([&]() -> Result<void> {
    return publish_entries(
        directory, name, header, boundary, sorted.size(),
        [&](std::uint64_t i) { return Result<ArchiveEntry>::success(sorted[i]); },
        trace.child());
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    returned.native(input_count);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<std::uint64_t> ArchiveCatalog::publish_merge(
    JournalDirectory &directory, std::string_view name, JournalHeader header,
    JournalCursor boundary, const ArchiveCatalog *previous,
    std::span<const ArchiveEntry> delta, const RawRetainedOrigin &raw_origin) {
  const auto input_count = delta.size();
  const bool previous_present = previous != nullptr;
  RawRetainedFields arguments;
  arguments.header(header);
  arguments.cursor(boundary);
  arguments.native(input_count);
  arguments.native(previous_present);
  const auto previous_count = previous ? previous->count() : 0;
  if (previous)
    arguments.native(previous_count);
  RawRetainedPair trace{raw_origin, {3200, 3201}, 228, arguments.view()};
  std::optional<RawRetainedOriginScope> previous_origin;
  if (previous)
    previous_origin.emplace(previous->raw_origin_, trace.child());
  auto answer = trace.run([&]() -> Result<std::uint64_t> {
    using Answer = Result<std::uint64_t>;
    std::uint64_t count = previous ? previous->count() : 0;
    for (std::size_t i = 0; i < delta.size(); ++i) {
      if (i && !(delta[i - 1].key < delta[i].key))
        return Answer::failure({ErrorCode::invalid_range});
      auto found =
          previous
              ? previous->find(delta[i].key)
              : Result<std::optional<PhysicalJournalRecord>>::success(std::nullopt);
      if (!found.has_value())
        return Answer::failure(found.error());
      if (!found.value())
        ++count;
    }
    std::uint64_t old = 0;
    std::size_t fresh = 0;
    std::optional<ArchiveEntry> selected;
    const auto next = [&](std::uint64_t) -> Result<ArchiveEntry> {
      if (!selected && previous && old < previous->count()) {
        auto loaded = previous->entry(old++);
        if (!loaded.has_value())
          return loaded;
        selected = std::move(loaded).value();
      }
      if (fresh < delta.size() && (!selected || !(selected->key < delta[fresh].key))) {
        if (selected && selected->key == delta[fresh].key)
          selected.reset();
        return Result<ArchiveEntry>::success(delta[fresh++]);
      }
      if (!selected)
        return Result<ArchiveEntry>::failure({ErrorCode::corrupt});
      auto result = std::move(*selected);
      selected.reset();
      return Result<ArchiveEntry>::success(std::move(result));
    };
    auto written =
        publish_entries(directory, name, header, boundary, count, next, trace.child());
    return written.has_value() ? Answer::success(count)
                               : Answer::failure(written.error());
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    returned.native(answer.value());
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<std::unique_ptr<ArchiveCatalog>>
ArchiveCatalog::open(JournalDirectory &directory, std::string_view name,
                     JournalHeader header, JournalCursor boundary,
                     std::uint64_t maximum_entries,
                     const RawRetainedOrigin &raw_origin) {
  RawRetainedFields arguments;
  arguments.header(header);
  arguments.cursor(boundary);
  arguments.native(maximum_entries);
  RawRetainedPair trace{raw_origin, {3202, 3203}, 219, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::unique_ptr<ArchiveCatalog>> {
    using Answer = Result<std::unique_ptr<ArchiveCatalog>>;
    auto opened = directory.open_existing(name, FileAccess::read_only);
    if (!opened.has_value())
      return Answer::failure(opened.error());
    auto file = std::move(opened).value();
    std::array<std::byte, prefix_size> bytes{};
    auto read = read_at(*file, 0, bytes);
    if (!read.has_value())
      return Answer::failure(read.error());
    const ByteView in{bytes};
    const auto count = get(in.subspan(144, 8));
    auto h = encode_journal_header(header);
    auto extent = file->extent();
    if (!h.has_value())
      return Answer::failure(h.error());
    if (!extent.has_value())
      return Answer::failure(extent.error());
    if (get(in.first(8)) != magic || get(in.subspan(8, 8)) != 2 ||
        !std::equal(h.value().begin(), h.value().end(), bytes.begin() + 16) ||
        boundary.journal != header.journal ||
        get(in.subspan(128, 8)) != boundary.sequence ||
        get(in.subspan(136, 8)) != boundary.end_offset ||
        get(in.subspan(152, 4)) != crc32c(in.first(152)) || get(in.last(4)) != 0 ||
        count > UINT32_MAX || count > maximum_entries ||
        count > (UINT64_MAX - prefix_size) / entry_size ||
        extent.value() != prefix_size + count * entry_size)
      return Answer::failure({ErrorCode::corrupt});
    try {
      return Answer::success(std::unique_ptr<ArchiveCatalog>{
          new ArchiveCatalog{std::move(file), header, boundary, count, trace.child()}});
    } catch (const std::bad_alloc &) {
      return Answer::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    returned.native(answer.value()->count_);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
Result<ArchiveEntry> ArchiveCatalog::entry(std::uint64_t ordinal) const {
  RawRetainedFields arguments;
  arguments.native(ordinal);
  RawRetainedPair trace{raw_origin_, {3206, 3207}, 204, arguments.view()};
  auto answer = trace.run([&]() -> Result<ArchiveEntry> {
    if (ordinal >= count_)
      return Result<ArchiveEntry>::failure({ErrorCode::invalid_range});
    std::array<std::byte, entry_size> bytes{};
    auto read = read_at(*file_, prefix_size + ordinal * entry_size, bytes);
    if (!read.has_value())
      return Result<ArchiveEntry>::failure(read.error());
    const ByteView in{bytes};
    const auto kind = get(in.subspan(40, 8)), sequence = get(in.subspan(48, 8)),
               first = get(in.subspan(56, 8));
    const auto offset = get(in.subspan(64, 8)), size = get(in.subspan(72, 8)),
               checksum = get(in.subspan(80, 8));
    if (get(in.subspan(88, 4)) != ordinal ||
        get(in.subspan(92, 4)) != crc32c(in.first(92)) || (kind != 1 && kind != 2) ||
        !sequence || sequence >= boundary_.sequence || !first || first > sequence ||
        offset < journal_header_size + journal_frame_header_size ||
        offset > boundary_.end_offset || size > boundary_.end_offset - offset ||
        size > header_.limits.max_payload || checksum > UINT32_MAX)
      return Result<ArchiveEntry>::failure({ErrorCode::corrupt});
    ArchiveEntry result{{},
                        {header_.environment, header_.journal,
                         static_cast<FrameKind>(kind), sequence, first, offset,
                         static_cast<std::uint32_t>(size),
                         static_cast<std::uint32_t>(checksum)}};
    std::copy_n(bytes.begin(), result.key.size(), result.key.begin());
    return Result<ArchiveEntry>::success(result);
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    returned.add(answer.value().key);
    returned.physical(answer.value().record);
  }
  trace.result(answer, returned.view());
  return answer;
}
Result<std::optional<PhysicalJournalRecord>>
ArchiveCatalog::find(const RecoveryKey &key) const {
  RawRetainedFields arguments;
  arguments.add(key);
  arguments.cursor(boundary_);
  RawRetainedPair trace{raw_origin_, {3204, 3205}, 228, arguments.view()};
  RawRetainedOriginScope origin_scope{raw_origin_, trace.child()};
  auto answer = trace.run([&]() -> Result<std::optional<PhysicalJournalRecord>> {
    std::uint64_t first = 0, end = count_;
    while (first < end) {
      const auto middle = first + (end - first) / 2;
      auto loaded = entry(middle);
      if (!loaded.has_value())
        return Result<std::optional<PhysicalJournalRecord>>::failure(loaded.error());
      if (loaded.value().key < key)
        first = middle + 1;
      else
        end = middle;
    }
    if (first == count_)
      return Result<std::optional<PhysicalJournalRecord>>::success(std::nullopt);
    auto loaded = entry(first);
    if (!loaded.has_value())
      return Result<std::optional<PhysicalJournalRecord>>::failure(loaded.error());
    return Result<std::optional<PhysicalJournalRecord>>::success(
        loaded.value().key == key
            ? std::optional<PhysicalJournalRecord>{loaded.value().record}
            : std::nullopt);
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    const auto &selected = answer.value();
    if (selected.has_value())
      returned.physical(*selected);
  }
  trace.result(answer, returned.view());
  return answer;
}
} // namespace blackbird
