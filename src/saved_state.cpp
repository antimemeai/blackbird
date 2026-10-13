#include "blackbird/saved_state.hpp"
#include "blackbird/packet.hpp"
#include <algorithm>
#include <chrono>
#include <limits>

namespace blackbird {
namespace {
constexpr std::uint64_t magic = 0x3154535641534242ULL; // BBSAVST1
constexpr std::size_t prefix = 192;
constexpr std::size_t maximum = 64 * 1024 * 1024;
constexpr std::size_t commit_size = journal_frame_header_size + 24;
void put(MutableByteView out, std::uint64_t value) {
  for (auto &b : out) {
    b = static_cast<std::byte>(value & 255U);
    value >>= 8;
  }
}
std::uint64_t get(ByteView in) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < in.size(); ++i)
    value |= static_cast<std::uint64_t>(std::to_integer<unsigned>(in[i])) << (8 * i);
  return value;
}
Result<void> read_all(Storage &file, MutableByteView out) {
  std::size_t done = 0;
  unsigned interruptions = 0;
  while (done < out.size()) {
    auto result = file.read_at(done, out.subspan(done));
    if (!result.has_value()) {
      if (result.error().code == ErrorCode::interrupted && ++interruptions < 8)
        continue;
      return Result<void>::failure(result.error());
    }
    if (!result.value() || result.value() > out.size() - done)
      return Result<void>::failure({ErrorCode::incomplete});
    done += result.value();
  }
  return Result<void>::success();
}
Result<void> write_all(Storage &file, ByteView in) {
  std::size_t done = 0;
  while (done < in.size()) {
    auto result = file.write_at(done, in.subspan(done));
    // Uncertain failed writes are not blindly replayed.
    if (!result.has_value())
      return Result<void>::failure(result.error());
    if (!result.value() || result.value() > in.size() - done)
      return Result<void>::failure({ErrorCode::incomplete});
    done += result.value();
  }
  return Result<void>::success();
}
Result<std::optional<SavedState>>
load_slot(JournalDirectory &directory, std::string_view name, FramedJournal &journal,
          JournalCapacity capacity, unsigned slot, bool before_scan,
          const RawRetainedOrigin &raw_origin) {
  RawRetainedFields arguments;
  arguments.native(slot);
  arguments.native(before_scan);
  RawRetainedPair trace{raw_origin, {3222, 3223}, 155, arguments.view()};
  const auto child_origin = trace.child();
  auto answer = trace.run([&]() -> Result<std::optional<SavedState>> {
    using Answer = Result<std::optional<SavedState>>;
    const auto ignored = [] { return Answer::success(std::nullopt); };
    auto opened = directory.open_existing(
        std::string{name} + ".state." + std::to_string(slot), FileAccess::read_only);
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
    if (extent.value() < prefix + 4 || extent.value() > maximum)
      return ignored();
    std::vector<std::byte> bytes(static_cast<std::size_t>(extent.value()));
    const auto acquired = read_all(*file, bytes);
    if (!acquired.has_value()) {
      trace.finish(5, {}, acquired.error());
      return ignored();
    }
    const ByteView view{bytes};
    if (get(view.first(8)) != magic || get(view.subspan(8, 8)) != 1 ||
        get(view.last(4)) != crc32c(view.first(view.size() - 4)))
      return ignored();
    auto canonical = encode_journal_header(journal.header());
    if (!canonical.has_value() ||
        !std::equal(canonical.value().begin(), canonical.value().end(),
                    bytes.begin() + 16))
      return ignored();
    const auto sequence = get(view.subspan(128, 8));
    const auto end = get(view.subspan(136, 8));
    const auto facts = get(view.subspan(144, 8));
    const auto counter = get(view.subspan(152, 8));
    const auto anchor_crc = get(view.subspan(160, 8));
    const auto context_size = get(view.subspan(168, 8));
    const auto program_size = get(view.subspan(176, 8));
    const auto unresolved = get(view.subspan(184, 8));
    if (!sequence || end < journal_header_size + commit_size ||
        facts > capacity.max_records || unresolved > facts || anchor_crc > UINT32_MAX ||
        context_size > view.size() - prefix - 4 ||
        program_size > view.size() - prefix - 4 - context_size)
      return ignored();
    // A checksummed root beyond the known prefix must not burn fewer IDs silently.
    if (end > journal.recovery_report().available_end)
      return Answer::failure(
          {ErrorCode::incomplete}); // durable root evidence prevents identity reuse
    if (!before_scan &&
        (sequence > journal.cursor().sequence || end > journal.cursor().end_offset))
      return ignored(); // full replay has already fenced the damaged semantic prefix
    auto anchor =
        journal.read_original_range(end - commit_size, commit_size, &child_origin);
    if (!anchor.has_value()) {
      trace.finish(5, {}, anchor.error());
      return ignored();
    }
    if (anchor.value().bytes.size() != commit_size ||
        crc32c(anchor.value().bytes) != anchor_crc)
      return ignored();
    auto frame = decode_journal_frame(anchor.value().bytes, journal.header().limits);
    if (!frame.has_value() || frame.value().kind != FrameKind::commit ||
        frame.value().sequence != sequence)
      return ignored();
    auto offset = prefix;
    const auto packet_blob = [&](std::uint64_t length) -> Result<Value> {
      auto result = decode_packet_string(
          std::string_view{reinterpret_cast<const char *>(bytes.data() + offset),
                           static_cast<std::size_t>(length)});
      offset += static_cast<std::size_t>(length);
      return result;
    };
    auto context = packet_blob(context_size);
    auto programs = packet_blob(program_size);
    if (!context.has_value() || !programs.has_value())
      return ignored();
    if (!std::holds_alternative<Value::Object>(context.value().value()) ||
        !std::holds_alternative<Value::Array>(programs.value().value()))
      return ignored();
    const auto *version = context.value().find("version");
    const auto *base = context.value().find("base");
    const auto *live = context.value().find("entries");
    const auto *originals = context.value().find("live_originals");
    if (!version || !std::holds_alternative<Number>(version->value()) ||
        version->number().text() != "1" || !base ||
        !std::holds_alternative<std::string>(base->value()) ||
        base->string().size() != 32 || !live ||
        !std::holds_alternative<Value::Array>(live->value()) || !originals ||
        !std::holds_alternative<Value::Array>(originals->value()))
      return ignored();
    for (const auto &packet : programs.value().array()) {
      if (!std::holds_alternative<Value::Object>(packet.value()))
        return ignored();
      const auto *label = packet.find("label");
      if (!label || !std::holds_alternative<std::string>(label->value()))
        return ignored();
    }
    SavedState saved{{journal.header().journal, sequence, end},
                     facts,
                     counter,
                     std::move(context).value(),
                     std::move(programs).value(),
                     {},
                     slot};
    for (std::uint64_t i = 0; i < unresolved; ++i) {
      if (offset > view.size() - 4 || view.size() - 4 - offset < 16)
        return ignored();
      const auto record_sequence = get(view.subspan(offset, 8));
      const auto size = get(view.subspan(offset + 8, 8));
      offset += 16;
      if (!record_sequence || record_sequence > sequence ||
          size > journal.header().limits.max_payload || size > view.size() - 4 - offset)
        return ignored();
      auto decoded =
          decode_retained_event(view.subspan(offset, static_cast<std::size_t>(size)),
                                journal.header().limits.max_payload, &child_origin);
      if (!decoded.has_value())
        return ignored();
      saved.unresolved.push_back({{journal.header().journal, record_sequence},
                                  std::move(decoded).value(),
                                  RetainedEvidence::recovered});
      offset += static_cast<std::size_t>(size);
    }
    if (offset != view.size() - 4)
      return ignored();
    return Answer::success(std::move(saved));
  });
  RawRetainedFields returned;
  if (!answer.has_value()) {
    trace.result(answer);
  } else {
    const auto &candidate = answer.value();
    if (!candidate.has_value()) {
      trace.finish(4);
      return answer;
    }
    const auto &selected = *candidate;
    const auto unresolved = selected.unresolved.size();
    returned.cursor(selected.boundary);
    returned.native(selected.slot);
    returned.native(selected.fact_count);
    returned.native(selected.issuer_counter);
    returned.native(unresolved);
    trace.finish(0, returned.view());
  }
  return answer;
}
} // namespace
Result<std::optional<SavedState>>
load_saved_state(JournalDirectory &directory, std::string_view name,
                 FramedJournal &journal, JournalCapacity capacity, bool before_scan,
                 const RawRetainedOrigin &raw_origin) {
  const auto current = journal.cursor();
  RawRetainedFields arguments;
  arguments.cursor(current);
  arguments.native(capacity.max_file_bytes);
  arguments.native(capacity.max_records);
  arguments.native(before_scan);
  RawRetainedPair trace{raw_origin, {3220, 3221}, 199, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::optional<SavedState>> {
    try {
      auto first =
          load_slot(directory, name, journal, capacity, 0, before_scan, trace.child());
      if (!first.has_value())
        return first;
      auto second =
          load_slot(directory, name, journal, capacity, 1, before_scan, trace.child());
      if (!second.has_value())
        return second;
      const auto &primary = first.value();
      const auto &alternate = second.value();
      if (alternate &&
          (!primary || alternate->boundary.sequence > primary->boundary.sequence))
        return second;
      return first;
    } catch (const std::bad_alloc &) {
      return Result<std::optional<SavedState>>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (!answer.has_value()) {
    trace.result(answer);
  } else {
    const auto &candidate = answer.value();
    if (!candidate.has_value()) {
      trace.finish(4);
      return answer;
    }
    const auto &selected = *candidate;
    const auto unresolved = selected.unresolved.size();
    returned.cursor(selected.boundary);
    returned.native(selected.slot);
    returned.native(selected.fact_count);
    returned.native(selected.issuer_counter);
    returned.native(unresolved);
    trace.finish(0, returned.view());
  }
  return answer;
}
Result<void> publish_saved_state(JournalDirectory &directory, std::string_view name,
                                 FramedJournal &journal, const SavedState &saved,
                                 unsigned slot, const RawRetainedOrigin &raw_origin) {
  const auto unresolved = saved.unresolved.size();
  RawRetainedFields arguments;
  arguments.cursor(saved.boundary);
  arguments.native(saved.fact_count);
  arguments.native(saved.issuer_counter);
  arguments.native(slot);
  arguments.native(unresolved);
  RawRetainedPair trace{raw_origin, {3224, 3225}, 186, arguments.view()};
  const auto child_origin = trace.child();
  auto answer = trace.run([&]() -> Result<void> {
    try {
      if (slot > 1 || journal.state() != JournalWriterState::live ||
          saved.boundary.journal != journal.cursor().journal ||
          saved.boundary.sequence != journal.cursor().sequence ||
          saved.boundary.end_offset != journal.cursor().end_offset ||
          saved.boundary.end_offset < journal_header_size + commit_size)
        return Result<void>::failure({ErrorCode::audit_unavailable});
      auto context = encode_packet_string(saved.context);
      auto programs = encode_packet_string(saved.programs);
      if (!context.has_value() || !programs.has_value())
        return Result<void>::failure(!context.has_value() ? context.error()
                                                          : programs.error());
      if (context.value().size() > maximum - prefix - 4 ||
          programs.value().size() > maximum - prefix - 4 - context.value().size())
        return Result<void>::failure({ErrorCode::capacity});
      std::vector<std::byte> bytes(prefix);
      auto header = encode_journal_header(journal.header());
      if (!header.has_value())
        return Result<void>::failure(header.error());
      std::copy(header.value().begin(), header.value().end(), bytes.begin() + 16);
      auto anchor = journal.read_original_range(saved.boundary.end_offset - commit_size,
                                                commit_size, &child_origin);
      if (!anchor.has_value())
        return Result<void>::failure(anchor.error());
      if (anchor.value().observed_extent != saved.boundary.end_offset ||
          anchor.value().bytes.size() != commit_size)
        return Result<void>::failure({ErrorCode::conflict});
      auto append = [&](ByteView body) {
        bytes.insert(bytes.end(), body.begin(), body.end());
      };
      append(std::as_bytes(std::span{context.value().data(), context.value().size()}));
      append(
          std::as_bytes(std::span{programs.value().data(), programs.value().size()}));
      for (const auto &fact : saved.unresolved) {
        RawRetainedFields selected;
        selected.reference(fact.record);
        selected.native(fact.evidence);
        (void)raw_retained_event(child_origin, {3227, 65}, selected.view());
        if (fact.record.journal != saved.boundary.journal || !fact.record.sequence ||
            fact.record.sequence > saved.boundary.sequence)
          return Result<void>::failure({ErrorCode::invalid_range});
        auto encoded = encode_retained_event(
            fact.event, journal.header().limits.max_payload, &child_origin);
        if (!encoded.has_value())
          return Result<void>::failure(encoded.error());
        if (bytes.size() > maximum - 20 ||
            encoded.value().size() > maximum - 20 - bytes.size())
          return Result<void>::failure({ErrorCode::capacity});
        std::array<std::byte, 16> envelope{};
        put(MutableByteView{envelope}.first(8), fact.record.sequence);
        put(MutableByteView{envelope}.last(8), encoded.value().size());
        append(envelope);
        append(encoded.value());
      }
      bytes.resize(bytes.size() + 4);
      MutableByteView out{bytes};
      put(out.first(8), magic);
      put(out.subspan(8, 8), 1);
      put(out.subspan(128, 8), saved.boundary.sequence);
      put(out.subspan(136, 8), saved.boundary.end_offset);
      put(out.subspan(144, 8), saved.fact_count);
      put(out.subspan(152, 8), saved.issuer_counter);
      put(out.subspan(160, 8), crc32c(anchor.value().bytes));
      put(out.subspan(168, 8), context.value().size());
      put(out.subspan(176, 8), programs.value().size());
      put(out.subspan(184, 8), saved.unresolved.size());
      put(out.last(4), crc32c(ByteView{bytes}.first(bytes.size() - 4)));
      const auto target = std::string{name} + ".state." + std::to_string(slot);
      const auto temporary =
          target + ".tmp." +
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
      auto created = directory.create_exclusive(temporary);
      if (!created.has_value())
        return Result<void>::failure(created.error());
      auto file = std::move(created).value();
      auto written = write_all(*file, bytes);
      if (!written.has_value())
        return written;
      auto synced = file->synchronize(SyncStrength::full);
      if (!synced.has_value())
        return synced;
      auto replaced = directory.replace_file(temporary, target);
      if (!replaced.has_value())
        return replaced;
      return directory.synchronize_directory(SyncStrength::full);
    } catch (const std::bad_alloc &) {
      return Result<void>::failure({ErrorCode::allocation});
    }
  });
  RawRetainedFields returned;
  if (answer.has_value()) {
    returned.cursor(saved.boundary);
    returned.native(slot);
    trace.result(answer, returned.view());
  } else
    trace.result(answer);
  return answer;
}
} // namespace blackbird
