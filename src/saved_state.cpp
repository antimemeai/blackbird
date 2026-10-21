#include "blackbird/saved_state.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/saved_context.hpp"
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
Error file_fault(SavedFileFault which) {
  return {ErrorCode::corrupt, static_cast<std::int64_t>(which)};
}
} // namespace
Result<SavedPayload> decode_saved_payload(ByteView body, std::uint64_t file_version,
                                          std::uint64_t context_size,
                                          std::uint64_t program_size) {
  using Answer = Result<SavedPayload>;
  const auto fail = [](SavedFileFault which) {
    return Answer::failure(file_fault(which));
  };
  try {
    if ((file_version != 1 && file_version != 2) || context_size > body.size() ||
        program_size > body.size() - context_size)
      return fail(SavedFileFault::prefix);
    std::size_t offset = 0;
    const auto packet_blob = [&](std::uint64_t length) -> Result<Value> {
      auto result =
          decode_packet(body.subspan(offset, static_cast<std::size_t>(length)));
      offset += static_cast<std::size_t>(length);
      return result;
    };
    // Only malformed bytes are corrupt: a decode that runs out of memory, or meets
    // a value past its limits (capacity) or a form it does not read (unsupported),
    // keeps its own error. Any other decode error (invalid_range included) is
    // malformed bytes.
    const auto kept = [](const Error &error) {
      return error.code == ErrorCode::allocation || error.code == ErrorCode::capacity ||
             error.code == ErrorCode::unsupported;
    };
    auto context = packet_blob(context_size);
    auto programs = packet_blob(program_size);
    if (!context.has_value() && kept(context.error()))
      return Answer::failure(context.error());
    if (!programs.has_value() && kept(programs.error()))
      return Answer::failure(programs.error());
    if (!context.has_value() || !programs.has_value() ||
        !std::holds_alternative<Value::Object>(context.value().value()) ||
        !std::holds_alternative<Value::Array>(programs.value().value()))
      return fail(SavedFileFault::packets);
    if (file_version == 2) {
      // Streamed parts: each entry and each section is its own packet, so no
      // single packet carries the whole context. A list is written as its count
      // plus one; zero means the key is not streamed (absent, or kept in the core
      // for the validator to judge).
      for (const auto *key : {"entries", "sections"}) {
        if (body.size() - offset < 8)
          return fail(SavedFileFault::parts);
        const auto stored = get(body.subspan(offset, 8));
        offset += 8;
        if (stored == 0)
          continue;
        if (context.value().find(key))
          return fail(SavedFileFault::parts);
        const auto count = stored - 1;
        if (count > (body.size() - offset) / 8)
          return fail(SavedFileFault::parts);
        Value::Array parts;
        parts.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
          if (body.size() - offset < 8)
            return fail(SavedFileFault::parts);
          const auto length = get(body.subspan(offset, 8));
          offset += 8;
          if (length > body.size() - offset)
            return fail(SavedFileFault::parts);
          auto part = packet_blob(length);
          if (!part.has_value())
            return kept(part.error()) ? Answer::failure(part.error())
                                      : fail(SavedFileFault::parts);
          parts.push_back(std::move(part).value());
        }
        context.value().object().emplace_back(key, Value{std::move(parts)});
      }
    }
    for (const auto &packet : programs.value().array()) {
      const auto *label = packet.find("label");
      if (!std::holds_alternative<Value::Object>(packet.value()) || !label ||
          !std::holds_alternative<std::string>(label->value()))
        return fail(SavedFileFault::packets);
    }
    return Answer::success(
        {std::move(context).value(), std::move(programs).value(), offset});
  } catch (const std::bad_alloc &) {
    return Answer::failure({ErrorCode::allocation});
  }
}
Result<SavedPayloadSizes> append_saved_payload(std::vector<std::byte> &out,
                                               const SavedPayload &payload,
                                               std::size_t trailing) {
  using Answer = Result<SavedPayloadSizes>;
  const auto &context = payload.context;
  const auto start = out.size();
  try {
    if (!std::holds_alternative<Value::Object>(context.value()))
      return Answer::failure({ErrorCode::invalid_range});
    // The context core without its entries and sections, the programs, then each
    // entry and each section as its own length-prefixed packet. The parts share
    // the payloads already in the in-memory value.
    Value::Object core_fields;
    std::array<const Value::Array *, 2> parts{};
    for (const auto &[key, value] : context.object()) {
      if ((key == "entries" || key == "sections") &&
          std::holds_alternative<Value::Array>(value.value()))
        parts[key == "entries" ? 0 : 1] = &value.array();
      else
        core_fields.emplace_back(key, value);
    }
    const Value core{std::move(core_fields)};
    // Counted first (no bytes built), so the one buffer is sized once and every
    // packet is encoded straight into it: no per-part buffer, no body copy.
    std::size_t total = 0;
    const auto count = [&](const Value &value) -> Result<void> {
      auto size = encoded_packet_size(value);
      if (!size.has_value())
        return Result<void>::failure(size.error());
      total += size.value();
      return Result<void>::success();
    };
    if (auto counted = count(core); !counted.has_value())
      return Answer::failure(counted.error());
    if (auto counted = count(payload.programs); !counted.has_value())
      return Answer::failure(counted.error());
    for (const auto *list : parts) {
      total += 8;
      if (list)
        for (const auto &part : *list) {
          if (auto counted = count(part); !counted.has_value())
            return Answer::failure(counted.error());
          total += 8;
        }
    }
    if (total > maximum)
      return Answer::failure({ErrorCode::capacity});
    out.reserve(start + total + trailing);
    const auto failed = [&](Error error) {
      out.resize(start);
      return Answer::failure(error);
    };
    const auto put_count = [&](std::uint64_t value) {
      std::array<std::byte, 8> bytes{};
      put(MutableByteView{bytes}, value);
      out.insert(out.end(), bytes.begin(), bytes.end());
    };
    auto core_size = encode_packet_into(out, core);
    if (!core_size.has_value())
      return failed(core_size.error());
    auto programs_size = encode_packet_into(out, payload.programs);
    if (!programs_size.has_value())
      return failed(programs_size.error());
    for (const auto *list : parts) {
      put_count(list ? list->size() + 1 : 0);
      if (!list)
        continue;
      for (const auto &part : *list) {
        // The length goes before the packet: reserve it, then fill it in.
        const auto at = out.size();
        put_count(0);
        auto size = encode_packet_into(out, part);
        if (!size.has_value())
          return failed(size.error());
        put(MutableByteView{out}.subspan(at, 8), size.value());
      }
    }
    return Answer::success({core_size.value(), programs_size.value()});
  } catch (const std::bad_alloc &) {
    out.resize(start);
    return Answer::failure({ErrorCode::allocation});
  }
}
Result<EncodedSavedPayload> encode_saved_payload(const SavedPayload &payload) {
  EncodedSavedPayload result;
  auto sizes = append_saved_payload(result.bytes, payload);
  if (!sizes.has_value())
    return Result<EncodedSavedPayload>::failure(sizes.error());
  result.context_size = sizes.value().context_size;
  result.program_size = sizes.value().program_size;
  return Result<EncodedSavedPayload>::success(std::move(result));
}
namespace {
// One slot. A slot that is present and refused is recorded in refusal; only a
// missing file is "absent". Errors other than a refusal fail the load.
Result<std::optional<SavedState>>
load_slot(JournalDirectory &directory, std::string_view name, FramedJournal &journal,
          JournalCapacity capacity, unsigned slot, bool before_scan,
          const RawRetainedOrigin &raw_origin, std::optional<Error> *refusal) {
  RawRetainedFields arguments;
  arguments.native(slot);
  arguments.native(before_scan);
  RawRetainedPair trace{raw_origin, {3222, 3223}, 155, arguments.view()};
  const auto child_origin = trace.child();
  auto answer = trace.run([&]() -> Result<std::optional<SavedState>> {
    using Answer = Result<std::optional<SavedState>>;
    const auto ignored = [] { return Answer::success(std::nullopt); };
    const auto refused = [&](Error why) {
      if (refusal)
        *refusal = why;
      return Answer::success(std::nullopt);
    };
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
      return refused(extent.error());
    }
    if (extent.value() < prefix + 4 || extent.value() > maximum)
      return refused(file_fault(SavedFileFault::extent));
    std::vector<std::byte> bytes(static_cast<std::size_t>(extent.value()));
    const auto acquired = read_all(*file, bytes);
    if (!acquired.has_value()) {
      trace.finish(5, {}, acquired.error());
      return refused(acquired.error());
    }
    const ByteView view{bytes};
    const auto file_version = get(view.subspan(8, 8));
    if (get(view.first(8)) != magic || (file_version != 1 && file_version != 2) ||
        get(view.last(4)) != crc32c(view.first(view.size() - 4)))
      return refused(file_fault(SavedFileFault::checksum));
    auto canonical = encode_journal_header(journal.header());
    if (!canonical.has_value() ||
        !std::equal(canonical.value().begin(), canonical.value().end(),
                    bytes.begin() + 16))
      return refused(file_fault(SavedFileFault::header));
    const auto sequence = get(view.subspan(128, 8));
    const auto end = get(view.subspan(136, 8));
    const auto facts = get(view.subspan(144, 8));
    const auto counter = get(view.subspan(152, 8));
    const auto anchor_crc = get(view.subspan(160, 8));
    const auto context_size = get(view.subspan(168, 8));
    const auto program_size = get(view.subspan(176, 8));
    const auto unresolved = get(view.subspan(184, 8));
    if (!sequence || end < journal_header_size + commit_size ||
        facts > capacity.max_records || unresolved > facts || anchor_crc > UINT32_MAX)
      return refused(file_fault(SavedFileFault::prefix));
    // A checksummed root beyond the known prefix must not burn fewer IDs silently.
    if (end > journal.recovery_report().available_end)
      return Answer::failure(
          {ErrorCode::incomplete}); // durable root evidence prevents identity reuse
    if (!before_scan &&
        (sequence > journal.cursor().sequence || end > journal.cursor().end_offset))
      // full replay has already fenced the damaged semantic prefix
      return refused(file_fault(SavedFileFault::beyond_journal));
    auto anchor =
        journal.read_original_range(end - commit_size, commit_size, &child_origin);
    if (!anchor.has_value()) {
      trace.finish(5, {}, anchor.error());
      return refused(anchor.error());
    }
    if (anchor.value().bytes.size() != commit_size ||
        crc32c(anchor.value().bytes) != anchor_crc)
      return refused(file_fault(SavedFileFault::anchor));
    auto frame = decode_journal_frame(anchor.value().bytes, journal.header().limits);
    if (!frame.has_value() || frame.value().kind != FrameKind::commit ||
        frame.value().sequence != sequence)
      return refused(file_fault(SavedFileFault::anchor));
    const auto body = view.subspan(prefix, view.size() - prefix - 4);
    auto payload = decode_saved_payload(body, file_version, context_size, program_size);
    if (!payload.has_value()) {
      // Allocation fails the load (the slot may be good); a body this binary cannot
      // read (capacity, unsupported) or a malformed one refuses the slot, named as
      // what it is.
      if (payload.error().code == ErrorCode::allocation)
        return Answer::failure(payload.error());
      return refused(payload.error());
    }
    if (const auto valid = validate_saved_context(payload.value().context);
        !valid.has_value()) {
      // Only the validator's corrupt is a refusal; another error fails the load.
      if (valid.error().code != ErrorCode::corrupt)
        return Answer::failure(valid.error());
      return refused(valid.error());
    }
    auto offset = prefix + payload.value().end;
    SavedState saved{{journal.header().journal, sequence, end},
                     facts,
                     counter,
                     std::move(payload.value().context),
                     std::move(payload.value().programs),
                     {},
                     slot};
    for (std::uint64_t i = 0; i < unresolved; ++i) {
      if (offset > view.size() - 4 || view.size() - 4 - offset < 16)
        return refused(file_fault(SavedFileFault::unresolved));
      const auto record_sequence = get(view.subspan(offset, 8));
      const auto size = get(view.subspan(offset + 8, 8));
      offset += 16;
      if (!record_sequence || record_sequence > sequence ||
          size > journal.header().limits.max_payload || size > view.size() - 4 - offset)
        return refused(file_fault(SavedFileFault::unresolved));
      auto decoded =
          decode_retained_event(view.subspan(offset, static_cast<std::size_t>(size)),
                                journal.header().limits.max_payload, &child_origin);
      if (!decoded.has_value())
        return refused(file_fault(SavedFileFault::unresolved));
      saved.unresolved.push_back({{journal.header().journal, record_sequence},
                                  std::move(decoded).value(),
                                  RetainedEvidence::recovered});
      offset += static_cast<std::size_t>(size);
    }
    if (offset != view.size() - 4)
      return refused(file_fault(SavedFileFault::trailing));
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
                 const RawRetainedOrigin &raw_origin, SavedStateLoad *report) {
  const auto current = journal.cursor();
  RawRetainedFields arguments;
  arguments.cursor(current);
  arguments.native(capacity.max_file_bytes);
  arguments.native(capacity.max_records);
  arguments.native(before_scan);
  RawRetainedPair trace{raw_origin, {3220, 3221}, 199, arguments.view()};
  auto answer = trace.run([&]() -> Result<std::optional<SavedState>> {
    try {
      std::array<std::optional<Error>, 2> refusals{};
      auto first = load_slot(directory, name, journal, capacity, 0, before_scan,
                             trace.child(), &refusals[0]);
      if (!first.has_value())
        return first;
      auto second = load_slot(directory, name, journal, capacity, 1, before_scan,
                              trace.child(), &refusals[1]);
      if (!second.has_value())
        return second;
      if (report) {
        report->refusals.clear();
        report->alternate.reset();
        for (unsigned slot = 0; slot < 2; ++slot)
          if (refusals[slot])
            report->refusals.emplace_back(slot, *refusals[slot]);
      }
      auto &primary = first.value();
      auto &other = second.value();
      const bool second_newer =
          other && (!primary || other->boundary.sequence > primary->boundary.sequence);
      // The older accepted slot, for a store that refuses the newer one.
      if (report && primary && other)
        report->alternate = second_newer ? std::move(primary) : std::move(other);
      return second_newer ? std::move(second) : std::move(first);
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
      // The unresolved events are encoded first (small), so the one file buffer is
      // sized once and the context payload is encoded straight into it.
      std::vector<std::vector<std::byte>> events;
      std::size_t events_size = 0;
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
        if (encoded.value().size() > maximum - 16 - events_size)
          return Result<void>::failure({ErrorCode::capacity});
        events_size += 16 + encoded.value().size();
        events.push_back(std::move(encoded).value());
      }
      std::vector<std::byte> bytes(prefix);
      auto sizes = append_saved_payload(bytes, {saved.context, saved.programs, 0},
                                        events_size + 4);
      if (!sizes.has_value())
        return Result<void>::failure(sizes.error());
      if (bytes.size() > maximum - 4 || events_size > maximum - 4 - bytes.size())
        return Result<void>::failure({ErrorCode::capacity});
      const auto context_size = sizes.value().context_size;
      const auto program_size = sizes.value().program_size;
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
      for (std::size_t i = 0; i < events.size(); ++i) {
        std::array<std::byte, 16> envelope{};
        put(MutableByteView{envelope}.first(8), saved.unresolved[i].record.sequence);
        put(MutableByteView{envelope}.last(8), events[i].size());
        bytes.insert(bytes.end(), envelope.begin(), envelope.end());
        bytes.insert(bytes.end(), events[i].begin(), events[i].end());
      }
      bytes.resize(bytes.size() + 4);
      MutableByteView out{bytes};
      put(out.first(8), magic);
      put(out.subspan(8, 8), 2);
      put(out.subspan(128, 8), saved.boundary.sequence);
      put(out.subspan(136, 8), saved.boundary.end_offset);
      put(out.subspan(144, 8), saved.fact_count);
      put(out.subspan(152, 8), saved.issuer_counter);
      put(out.subspan(160, 8), crc32c(anchor.value().bytes));
      put(out.subspan(168, 8), context_size);
      put(out.subspan(176, 8), program_size);
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
