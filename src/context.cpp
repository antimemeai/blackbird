#include "blackbird/context.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/saved_context.hpp"
#include "blackbird/saved_state.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <limits>
#include <set>

namespace blackbird {
Result<Value> read_packet(const ImmutableBytes &bytes, ValueLimits limits) {
  if (bytes.size() > limits.bytes)
    return Result<Value>::failure({ErrorCode::capacity});
  auto data = bytes.read();
  if (!data.has_value())
    return Result<Value>::failure(data.error());
  return decode_packet(data.value(), limits);
}
Result<Value> read_application_packet(const ApplicationRecordEvent &record) {
  return read_packet(record.payload);
}
std::string hex_identity(const IdentityBytes &bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(32);
  for (const auto b : bytes) {
    const auto n = std::to_integer<unsigned>(b);
    out += digits[n >> 4U];
    out += digits[n & 15U];
  }
  return out;
}
const Value &field(const Value &value, std::string_view name) {
  const auto *f = value.find(name);
  if (f == nullptr)
    throw Error{ErrorCode::corrupt};
  return *f;
}
const std::string &string_field(const Value &value, std::string_view name) {
  const auto &f = field(value, name);
  const auto *s = std::get_if<std::string>(&f.value());
  if (s == nullptr)
    throw Error{ErrorCode::corrupt};
  return *s;
}
namespace {

bool accepted(const Value &packet) {
  const auto &f = field(packet, "accepted");
  const auto *b = std::get_if<bool>(&f.value());
  if (b == nullptr)
    throw Error{ErrorCode::corrupt};
  return *b;
}
// Cut and repair originals are {item, class}; their ID is <revision>.<index>
// (design §4.2). Legacy append and managed originals carry their IDs.
bool derived_ids(const Value &packet) {
  const auto *op = packet.find("op");
  return op && (*op == Value{"cut"} || *op == Value{"repair"});
}
// The original at ordinal of a decoded frame, checked against its expected ID;
// empty when the frame has no such original.
std::optional<Value> packet_original(const Value &packet, std::size_t ordinal,
                                     std::string_view id) {
  const auto *fresh = packet.find("originals");
  if (!fresh || !std::holds_alternative<Value::Array>(fresh->value()) ||
      ordinal >= fresh->array().size())
    return std::nullopt;
  const auto &entry = fresh->array()[ordinal];
  if (derived_ids(packet)) {
    if (string_field(packet, "revision") + "." + std::to_string(ordinal) != id)
      throw Error{ErrorCode::corrupt};
    return Value::object(
        {{"id", Value{std::string{id}}}, {"item", field(entry, "item")}});
  }
  if (string_field(entry, "id") != id)
    throw Error{ErrorCode::corrupt};
  return entry;
}
// A creating record decoded for its originals only: everything else it carries
// (a legacy record's whole entries list, a managed candidate, a cut's lists) is
// skipped, not built.
Value creating_projection(ByteView bytes) {
  constexpr std::array<std::string_view, 19> omitted{
      "entries", "candidate", "outcome",  "insert",   "replace",   "lineage", "chapter",
      "pending", "refused",   "archived", "swept",    "reasoning", "result",  "sizes",
      "cause",   "skipped",   "stage",    "proposal", "basis"};
  return unwrap(decode_packet_projection(bytes, omitted));
}
// A creating record decoded for its classes (derive_classes): what
// creating_projection keeps plus a seeded append's lineage and a repair's skip list.
Value class_projection(ByteView bytes) {
  constexpr std::array<std::string_view, 17> omitted{
      "entries", "candidate", "outcome",  "insert",   "replace",   "chapter",
      "pending", "refused",   "archived", "swept",    "reasoning", "result",
      "sizes",   "cause",     "stage",    "proposal", "basis"};
  return unwrap(decode_packet_projection(bytes, omitted));
}
// Every {id, item} original a frame created, in ordinal order.
Value::Array packet_originals(const Value &packet) {
  const auto *fresh = packet.find("originals");
  if (!fresh)
    return {};
  if (!derived_ids(packet))
    return fresh->array();
  Value::Array out;
  const auto &revision = string_field(packet, "revision");
  std::size_t index = 0;
  for (const auto &entry : fresh->array())
    out.push_back(
        Value::object({{"id", Value{revision + "." + std::to_string(index++)}},
                       {"item", field(entry, "item")}}));
  return out;
}
} // namespace
std::string AuditLog::record(ApplicationChannel channel, const Value &packet) {
  const auto identity = issue();
  auto result = hex_identity(identity.bytes());
  (void)record(identity, channel, packet);
  return result;
}
RecordReference AuditLog::record(ApplicationRecordId identity,
                                 ApplicationChannel channel, const Value &packet,
                                 RawContextPublication *publication) {
  const auto request = publication ? raw_context_publication(*publication, identity,
                                                             channel, root_.cursor())
                                   : RawEventRef{};
  try {
    const ImmutableBytes serialized{unwrap(encode_packet(packet))};
    const auto result = unwrap(
        root_.submit({{}, ApplicationRecordEvent{identity, channel, serialized}}));
    if (publication)
      raw_context_publication_return(*publication, request, &result.record, nullptr,
                                     nullptr);
    return result.record;
  } catch (const Error &error) {
    if (publication)
      raw_context_publication_return(*publication, request, nullptr, nullptr, &error);
    throw;
  } catch (const std::bad_alloc &) {
    const Error error{ErrorCode::allocation};
    if (publication)
      raw_context_publication_return(*publication, request, nullptr, nullptr, &error);
    throw;
  } catch (...) {
    const Error error{ErrorCode::external_unknown};
    if (publication)
      raw_context_publication_return(*publication, request, nullptr, nullptr, &error);
    throw;
  }
}
JournalCursor AuditLog::record_boundary(ApplicationRecordId identity,
                                        const Value &context_packet,
                                        const Value &program_packet,
                                        RawContextPublication *publication) {
  if (program_packet == Value{}) {
    (void)record(identity, ApplicationChannel::context, context_packet, publication);
    return root_.cursor();
  }
  const auto context_request =
      publication ? raw_context_publication(*publication, identity,
                                            ApplicationChannel::context, root_.cursor())
                  : RawEventRef{};
  RawEventRef program_request{};
  bool issued = false;
  try {
    const auto program_identity = issue();
    if (publication)
      program_request = raw_context_publication(
          *publication, program_identity, ApplicationChannel::program, root_.cursor());
    issued = true;
    const std::array<RetainedEvent, 2> events{
        RetainedEvent{{},
                      ApplicationRecordEvent{identity, ApplicationChannel::context,
                                             unwrap(encode_packet(context_packet))}},
        RetainedEvent{{},
                      ApplicationRecordEvent{program_identity,
                                             ApplicationChannel::program,
                                             unwrap(encode_packet(program_packet))}}};
    const auto result = unwrap(root_.append(root_.cursor(), {}, events));
    if (publication) {
      raw_context_publication_return(*publication, context_request, nullptr, &result,
                                     nullptr);
      raw_context_publication_return(*publication, program_request, nullptr, &result,
                                     nullptr);
    }
    return result;
  } catch (...) {
    Error error{ErrorCode::external_unknown};
    try {
      throw;
    } catch (const Error &failure) {
      error = failure;
    } catch (const std::bad_alloc &) {
      error = {ErrorCode::allocation};
    } catch (...) {
    }
    if (publication) {
      raw_context_publication_return(*publication, context_request, nullptr, nullptr,
                                     &error);
      if (issued)
        raw_context_publication_return(*publication, program_request, nullptr, nullptr,
                                       &error);
    }
    throw;
  }
}
SourceReference AuditLog::retain_program(std::string_view source,
                                         DefinitionGenerationId generation) {
  const auto original = issue();
  const auto activation = issue();
  const auto cursor = root_.cursor();
  if (cursor.sequence == UINT64_MAX)
    throw Error{ErrorCode::overflow};
  const auto identity = hex_identity(generation.bytes());
  const auto capture =
      Value::object({{"label", Value{"program.source"}},
                     {"metadata", Value::object({{"generation", Value{identity}}})}});
  const auto effective = Value::object(
      {{"generation", Value{identity}}, {"activation", Value{"turn-boundary"}}});
  const auto raw = std::as_bytes(std::span{source.data(), source.size()});
  const std::array<RetainedEvent, 2> events{
      RetainedEvent{{SourceReference{cursor.journal, cursor.sequence + 1}},
                    ApplicationRecordEvent{original, ApplicationChannel::log,
                                           unwrap(encode_packet(capture))}},
      RetainedEvent{{},
                    ApplicationRecordEvent{activation, ApplicationChannel::program,
                                           unwrap(encode_packet(effective))}}};
  (void)unwrap(root_.append(cursor, std::span{&raw, 1}, events));
  return {cursor.journal, cursor.sequence + 1};
}
std::string AuditLog::original(OriginalCapture capture) {
  const auto identity = issue();
  auto output = hex_identity(identity.bytes());
  (void)original(identity, std::move(capture));
  return output;
}
std::optional<SourceReference> AuditLog::original(ApplicationRecordId identity,
                                                  OriginalCapture capture) {
  const auto label = capture.label;
  const auto raw = capture.bytes;
  auto metadata = std::move(capture.metadata);
  auto packet = Value::object(
      {{"label", Value{std::string{label}}}, {"metadata", std::move(metadata)}});
  if (label == "provider.stream") {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
    if (seconds < 0)
      throw Error{ErrorCode::invalid_range};
    const auto now = static_cast<std::uint64_t>(seconds);
    const auto expires = now + 30ULL * 24 * 60 * 60;
    const auto filename =
        std::to_string(expires) + "." + hex_identity(identity.bytes());
    auto stored = root_.store_diagnostic(
        filename, std::as_bytes(std::span{raw.data(), raw.size()}), now);
    auto &details = packet.object()[1].second.object();
    details.emplace_back("diagnostic_file", Value{filename});
    details.emplace_back("expires_at", Value{Number{expires}});
    details.emplace_back("bytes", Value{Number{raw.size()}});
    details.emplace_back("available", Value{stored.has_value()});
    if (!stored.has_value())
      details.emplace_back("storage_error",
                           Value{std::string{error_name(stored.error().code)}});
    record(identity, ApplicationChannel::log, packet);
    return std::nullopt;
  }
  const auto cursor = root_.cursor();
  if (cursor.sequence == UINT64_MAX)
    throw Error{ErrorCode::overflow};
  const SourceReference reference{cursor.journal, cursor.sequence + 1};
  const auto source = std::as_bytes(std::span{raw.data(), raw.size()});
  RetainedEvent event{{reference},
                      ApplicationRecordEvent{identity, ApplicationChannel::log,
                                             unwrap(encode_packet(packet))}};
  (void)unwrap(root_.append(cursor, std::span{&source, 1}, std::span{&event, 1}));
  return reference;
}
RawContextState ContextStore::raw_state() const noexcept {
  return {head_,     entries_.size(),      originals_.size(), captured_.size(),
          workflow_, pending_.has_value(), archive_loaded_};
}
void ContextStore::refresh_history() const {
  if (!history_stale_)
    return;
  auto next = log_.root().history_snapshot();
  historical_ = std::move(next);
  history_stale_ = false;
}
bool ContextStore::valid_entries(const Value &entries, RawContextOrigin origin) const {
  const auto *array = std::get_if<Value::Array>(&entries.value());
  if (array == nullptr)
    return false;
  std::set<std::string> ids;
  for (const auto &entry : *array) {
    // The one entry rule the validators apply too (saved_context.hpp).
    if (!saved_entry_shape(entry))
      return false;
    const auto &id = entry.find("id")->string();
    if ((!originals_.contains(id) && !original_entry(id, origin)) ||
        !ids.insert(id).second)
      return false;
  }
  return true;
}
std::string read_text(const ImmutableBytes &bytes) {
  const auto owned = unwrap(bytes.read());
  return std::string(reinterpret_cast<const char *>(owned.data()), owned.size());
}
ContextStore::Original::Original(const Value &entry,
                                 std::optional<ApplicationRecordId> identity)
    : payload(unwrap(encode_packet(entry))), id(string_field(entry, "id")),
      application(identity) {}
Value ContextStore::Original::entry(RawContextOrigin origin) const {
  if (payload.size() > ValueLimits{}.bytes)
    throw Error{ErrorCode::capacity};
  if (!packet)
    return unwrap(decode_packet(raw_context_read(
        payload, {0, packet, record, application, ApplicationChannel::context, index},
        origin)));
  auto found = packet_original(packet_value(origin), index, id);
  if (!found)
    throw Error{ErrorCode::corrupt};
  return std::move(*found);
}
Value ContextStore::Original::packet_value(RawContextOrigin origin) const {
  if (!packet || payload.size() > ValueLimits{}.bytes)
    throw Error{ErrorCode::corrupt};
  return creating_projection(raw_context_read(
      payload, {0, packet, record, application, ApplicationChannel::context, index},
      origin));
}
namespace {
template <class T> void reserve_append(std::vector<T> &values, std::size_t count) {
  if (count > values.max_size() - values.size())
    throw Error{ErrorCode::capacity};
  const auto needed = values.size() + count;
  if (needed > values.capacity()) {
    const auto growth = values.capacity() > values.max_size() / 2
                            ? values.max_size()
                            : values.capacity() * 2;
    values.reserve(std::max(needed, growth));
  }
}
template <class Visitor>
void visit_context_records(const RetainedState::FactHistory &history,
                           RawContextOrigin origin, Visitor visitor) {
  const auto count = history.count;
  constexpr std::size_t requested = 16;
  const std::uint8_t purpose = 3;
  for (std::size_t first = 0; first < count;) {
    RawContextPair capture{
        origin,
        123,
        {1817, 1818},
        std::array{detail::raw_native_field(first), detail::raw_native_field(requested),
                   detail::raw_native_field(count), detail::raw_native_field(purpose)}};
    auto page = capture.run([&] { return unwrap(history.page(first, requested)); });
    const auto rows = page.size();
    capture.finish(0, std::array{detail::raw_native_field(rows)});
    if (page.empty())
      throw Error{ErrorCode::corrupt};
    first += page.size();
    auto input = origin;
    input.parent = capture.reference();
    for (const auto &fact : page)
      if (const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
          record && record->channel == ApplicationChannel::context)
        visitor(unwrap(decode_packet(raw_context_read(
            record->payload,
            {3, true, fact.record, record->identity, record->channel, 0}, input))));
  }
}
} // namespace
Value::Array ContextStore::captured_entries(RawContextOrigin origin,
                                            ClassMap *classes) const {
  refresh_history();
  Value::Array result;
  visit_context_records(historical_, origin, [&](const Value &packet) {
    // A frame a repair skipped created nothing (design §4.5).
    if (!accepted(packet) || skipped_frame(string_field(packet, "revision")))
      return;
    if (classes)
      if (const auto ref = parse_entry_id(string_field(packet, "revision") + ".0"))
        classes->set(ref->record, derive_classes(packet));
    for (auto &entry : packet_originals(packet))
      result.push_back(std::move(entry));
  });
  return result;
}
namespace {
// Errors that mean a frame or snapshot does not hold: they enter whole-history
// playback (a snapshot) or the read-only state (a frame). Every other error,
// such as a failed read or allocation, propagates as before.
bool fault_error(const Error &error) noexcept {
  return error.code == ErrorCode::corrupt || error.code == ErrorCode::invalid_range;
}
std::uint64_t level_number(const Value &value) {
  const auto *n = std::get_if<Number>(&value.value());
  if (!n)
    throw Error{ErrorCode::corrupt};
  if (const auto *u = std::get_if<std::uint64_t>(&n->storage()))
    return *u;
  if (const auto *s = std::get_if<std::int64_t>(&n->storage()); s && *s >= 0)
    return static_cast<std::uint64_t>(*s);
  throw Error{ErrorCode::corrupt};
}
} // namespace
ContextStore::ContextStore(AuditLog &log, RawContextOrigin origin)
    : ContextStore(log, ContextOpen{}, origin) {}
ContextStore::ContextStore(AuditLog &log, DeferOpenCheckpoint, RawContextOrigin origin)
    : ContextStore(log, ContextOpen{.defer_checkpoint = true}, origin) {}
void ContextStore::finish_open(RawContextOrigin origin) { maybe_checkpoint(origin); }
ContextStore::ContextStore(AuditLog &log, ContextOpen open, RawContextOrigin origin)
    : open_(open), log_(log) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::reconstruct, 450, origin, &initial};
  trace.run([&]() -> void {
    auto &root = log.root();
    for (const auto &program : root.current_programs()) {
      const auto &label = string_field(program, "label");
      if (label == "context-format-level")
        level_ = level_number(field(program, "level"));
      // Pins are a latest-wins program record (design §5.4): every reader sees
      // the same set, whatever restored the context.
      if (label == "context-pins-v1")
        for (const auto &unit : field(program, "units").array())
          pins_.insert(decode_ref(unit));
    }
    // A newer format than this binary reads: refuse before any playback.
    if (level_ > format_level)
      throw Error{ErrorCode::unsupported, level_refused};
    // Every slot the loader refused, with its reason (A-M3).
    if (!open_.whole_history) {
      report_.slot_refusals = root.saved_load().refusals;
      report_.catalogue_faults = root.saved_load().catalogue_faults;
    }
    // The selected snapshot, then the older accepted slot, then whole history.
    std::vector<const SavedState *> candidates;
    if (!open_.whole_history) {
      if (const auto *saved = root.saved_state())
        candidates.push_back(saved);
      if (const auto &older = root.saved_load().alternate; older)
        candidates.push_back(&*older);
    }
    const SavedState *restored = nullptr;
    for (const auto *saved : candidates) {
      auto saved_branch = trace.control_credit(50);
      const auto *version = saved->context.find("version"),
                 *originals = saved->context.find("live_originals");
      const bool has_version = version != nullptr, has_originals = originals != nullptr;
      trace.event(1822, std::array{ByteView{saved->boundary.journal.bytes()},
                                   detail::raw_native_field(saved->boundary.sequence),
                                   detail::raw_native_field(saved->boundary.end_offset),
                                   detail::raw_native_field(has_version),
                                   detail::raw_native_field(has_originals)});
      const auto control = [&](std::uint8_t branch, const Error *error) {
        if (error)
          trace.control(1821,
                        std::array{detail::raw_native_field(branch),
                                   detail::raw_native_field(error->code),
                                   detail::raw_native_field(error->detail)},
                        saved_branch ? &*saved_branch : nullptr);
        else
          trace.control(1821, std::array{detail::raw_native_field(branch)},
                        saved_branch ? &*saved_branch : nullptr);
      };
      const auto refused = [&](const Error &error) {
        control(1, &error);
        head_.clear();
        entries_.clear();
        originals_.clear();
        differing_.clear();
        skipped_.clear();
        restored_sections_.clear();
        classes_.clear();
        unreadable_.clear();
        archive_loaded_ = true;
        lookup_boundary_.reset();
        if (!report_.snapshot_refusal)
          report_.snapshot_refusal = error;
        report_.store_refusals.emplace_back(saved->slot, error);
        report_.refused_by_store = true;
      };
      try {
        restore_snapshot(*saved, trace);
        restored = saved;
        control(0, nullptr);
        trace.state(1, raw_state());
        break;
      } catch (const Error &error) {
        if (!fault_error(error)) {
          control(4, &error);
          throw;
        }
        refused(error);
      } catch (const std::bad_variant_access &) {
        refused(Error{ErrorCode::corrupt});
      } catch (...) {
        Error error{ErrorCode::external_unknown};
        try {
          throw;
        } catch (const std::bad_alloc &) {
          error = {ErrorCode::allocation};
        } catch (...) {
        }
        control(4, &error);
        throw;
      }
    }
    report_.older_slot = restored && restored != root.saved_state();
    if (restored)
      report_.restored_slot = restored->slot;
    report_.playback = restored ? ContextOpenReport::Playback::suffix
                       : (report_.snapshot_refusal || !report_.slot_refusals.empty() ||
                          open_.whole_history)
                           ? ContextOpenReport::Playback::whole_history
                           : ContextOpenReport::Playback::journal;
    play_facts(trace, restored);
    trace.state(2, raw_state());
    if (!open_.probe && !open_.defer_checkpoint)
      maybe_checkpoint(trace.fresh());
    historical_ = root.history_snapshot();
    root_lifetime_ = root.lifetime();
    if (!open_.probe)
      root.set_maintenance_checkpoint(
          [this](const RawEventRef &parent) { return checkpoint({parent}); });
  });
}
void ContextStore::restore_snapshot(const SavedState &saved, RawContextTrace &trace) {
  unwrap(validate_saved_context(saved.context));
  const auto &context = saved.context;
  head_ = string_field(context, "base");
  if (field(context, "version").number().text() == "1") {
    entries_ = field(context, "entries").array();
    for (const auto &entry : field(context, "live_originals").array())
      if (!originals_.emplace(string_field(entry, "id"), Original{entry}).second)
        throw Error{ErrorCode::corrupt};
    if (!valid_entries(Value{entries_}, trace.fresh()))
      throw Error{ErrorCode::corrupt};
    // Which entries differ from their originals, by exact encoding; both are in
    // the snapshot, so this reads nothing.
    for (const auto &entry : entries_) {
      const auto &id = string_field(entry, "id");
      if (unwrap(encode_packet(entry)) != unwrap(originals_.at(id).payload.read()))
        differing_.insert(id);
    }
    archive_loaded_ = false;
    lookup_boundary_ = saved.boundary;
    // A version-1 snapshot holds no classes: each live record's are derived from
    // the record, read by identity lookup (design §4.4).
    derive_live_classes(trace.fresh());
    return;
  }
  // Version 2: a ref names the retained creating record, which holds the entry
  // exactly; an inline entry differs from its original. Each creating record is
  // read once and decoded for its originals only.
  const auto &descriptors = field(context, "entries").array();
  std::vector<ContextRef> refs;
  refs.reserve(descriptors.size());
  std::set<IdentityBytes> needed;
  for (const auto &descriptor : descriptors) {
    const auto ref = std::holds_alternative<Value::Array>(descriptor.value())
                         ? decode_ref(descriptor)
                         : parse_entry_id(string_field(descriptor, "id"));
    if (!ref)
      throw Error{ErrorCode::corrupt};
    refs.push_back(*ref);
    needed.insert(ref->record);
  }
  if (const auto *skipped = context.find("skipped"))
    for (const auto &revision : skipped->array())
      skipped_.insert(revision.string());
  struct Creating {
    RetainedFact fact;
    Value packet;
  };
  std::map<IdentityBytes, Creating> records;
  auto &root = log_.root();
  const auto accept = [&](const RetainedFact &fact) {
    const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
    if (!record || record->channel != ApplicationChannel::context ||
        fact.record.journal != saved.boundary.journal ||
        fact.record.sequence > saved.boundary.sequence ||
        !needed.contains(record->identity.bytes()) ||
        records.contains(record->identity.bytes()) ||
        skipped_.contains(hex_identity(record->identity.bytes())))
      return;
    auto packet = creating_projection(raw_context_read(
        record->payload, {2, true, fact.record, record->identity, record->channel, 0},
        trace.fresh()));
    if (accepted(packet))
      records.emplace(record->identity.bytes(), Creating{fact, std::move(packet)});
  };
  if (!root.compact_recovery()) {
    // Every fact is resident: one pass instead of a lookup per record.
    for (const auto &fact : root.tail_facts())
      accept(fact);
  } else
    for (const auto &bytes : needed) {
      const std::uint8_t family = 1;
      const auto kind = RetainedKind::application;
      const auto cursor = root.cursor();
      RawContextPair lookup{trace.fresh(),
                            158,
                            {1819, 1820},
                            std::array{detail::raw_native_field(family),
                                       detail::raw_native_field(kind), ByteView{bytes},
                                       ByteView{cursor.journal.bytes()},
                                       detail::raw_native_field(cursor.sequence),
                                       detail::raw_native_field(cursor.end_offset)}};
      const auto fact =
          lookup.run([&] { return root.lookup(root.visible(), family, kind, bytes); });
      if (!fact) {
        lookup.finish(2);
        throw Error{ErrorCode::corrupt};
      }
      const auto *application = std::get_if<ApplicationRecordEvent>(&fact->event.body);
      if (application)
        lookup.finish(0, std::array{ByteView{fact->record.journal.bytes()},
                                    detail::raw_native_field(fact->record.sequence),
                                    detail::raw_native_field(application->channel)});
      else
        lookup.finish(3, std::array{ByteView{fact->record.journal.bytes()},
                                    detail::raw_native_field(fact->record.sequence)});
      accept(*fact);
    }
  Value::Array entries;
  entries.reserve(descriptors.size());
  for (std::size_t i = 0; i < descriptors.size(); ++i) {
    const auto &ref = refs[i];
    const auto found = records.find(ref.record);
    if (found == records.end())
      throw Error{ErrorCode::corrupt};
    const auto id = entry_id(ref);
    const auto original = packet_original(found->second.packet, ref.ordinal, id);
    if (!original)
      throw Error{ErrorCode::corrupt};
    const auto *record =
        std::get_if<ApplicationRecordEvent>(&found->second.fact.event.body);
    originals_.emplace(id, Original{record->payload, ref.ordinal, id,
                                    found->second.fact.record, record->identity});
    const bool inline_entry =
        !std::holds_alternative<Value::Array>(descriptors[i].value());
    if (inline_entry)
      differing_.insert(id);
    entries.push_back(inline_entry ? descriptors[i] : *original);
  }
  entries_ = std::move(entries);
  restored_sections_ = field(context, "sections").array();
  if (const auto *section = restored_section(classes_section))
    classes_ = ClassMap::read_section(*section);
  else
    // An S1 snapshot has no classes section: derive them from the creating records
    // just read. A seeded record is read again for its lineage, which the
    // projection above skips.
    for (const auto &[bytes, creating] : records) {
      const auto *label = creating.packet.find("origin");
      if (!label || *label != Value{"session.successor"}) {
        classes_.set(bytes, derive_classes(creating.packet));
        continue;
      }
      const auto *record =
          std::get_if<ApplicationRecordEvent>(&creating.fact.event.body);
      classes_.set(bytes, derive_classes(class_projection(
                              raw_context_read(record->payload,
                                               {2, true, creating.fact.record,
                                                record->identity, record->channel, 0},
                                               trace.fresh()))));
    }
  archive_loaded_ = false;
  lookup_boundary_ = saved.boundary;
}
void ContextStore::play_facts(RawContextTrace &trace, const SavedState *restored) {
  auto &root = log_.root();
  const auto journal = root.cursor().journal;
  const auto boundary = restored ? restored->boundary.sequence : 0;
  std::optional<RetainedState::FactHistory> history;
  std::size_t first_fact = 0;
  if (root.compact_recovery() && restored != root.saved_state()) {
    // The resident tail is only the suffix after the selected snapshot. From the
    // older slot, play the facts after its boundary through the archive
    // catalogue; with no snapshot, play the whole history, under the launcher's
    // automatic replay bound.
    if (!restored && root.cursor().end_offset > open_.whole_history_limit)
      throw Error{ErrorCode::unsupported, whole_history_refused};
    history = root.history_snapshot();
    if (restored)
      first_fact = static_cast<std::size_t>(restored->fact_count);
  }
  const auto visit = [&](const auto &body) {
    const auto one = [&](const RetainedFact &fact) {
      if (restored && fact.record.journal == journal &&
          fact.record.sequence <= boundary)
        return;
      if (const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
          record && record->channel == ApplicationChannel::context)
        body(fact, *record);
    };
    if (history) {
      for (std::size_t first = std::min(first_fact, history->count);
           first < history->count;) {
        auto page = unwrap(history->page(first, 16));
        if (page.empty())
          throw Error{ErrorCode::corrupt};
        first += page.size();
        for (const auto &fact : page)
          one(fact);
      }
    } else
      for (const auto &fact : root.tail_facts())
        one(fact);
  };
  // Every repair frame's skip list first (design §4.5), from frames that decode
  // as repair frames under their own record identity; the snapshot's list was
  // restored with it.
  visit([&](const RetainedFact &fact, const ApplicationRecordEvent &record) {
    constexpr std::array<std::string_view, 8> omitted{
        "candidate", "entries", "originals", "insert",
        "replace",   "refused", "pending",   "chapter"};
    try {
      const auto bytes = raw_context_read(
          record.payload, {1, true, fact.record, record.identity, record.channel, 0},
          trace.fresh());
      const auto head = unwrap(decode_packet_projection(bytes, omitted));
      const auto *op = head.find("op");
      if (!op || *op != Value{"repair"})
        return;
      const auto frame = decode_cut_frame(unwrap(decode_packet(bytes)));
      if (frame.accepted && frame.revision == hex_identity(record.identity.bytes()))
        skipped_.insert(frame.skipped.begin(), frame.skipped.end());
    } catch (const Error &error) {
      if (!fault_error(error))
        throw;
    } catch (const std::bad_variant_access &) {
    }
  });
  visit([&](const RetainedFact &fact, const ApplicationRecordEvent &record) {
    play_frame(fact, record, trace);
  });
}
void ContextStore::play_frame(const RetainedFact &fact,
                              const ApplicationRecordEvent &record,
                              RawContextTrace &trace) {
  const auto revision = hex_identity(record.identity.bytes());
  auto branch_credit = trace.control_credit(50);
  const auto control = [&](std::uint8_t branch, const Error *error) {
    if (error)
      trace.control(1821,
                    std::array{detail::raw_native_field(branch),
                               detail::raw_native_field(error->code),
                               detail::raw_native_field(error->detail)},
                    branch_credit ? &*branch_credit : nullptr);
    else
      trace.control(1821, std::array{detail::raw_native_field(branch)},
                    branch_credit ? &*branch_credit : nullptr);
  };
  const auto decode = [&] {
    constexpr std::array<std::string_view, 1> omitted{"candidate"};
    return unwrap(decode_packet_projection(
        raw_context_read(record.payload,
                         {1, true, fact.record, record.identity, record.channel, 0},
                         trace.fresh()),
        omitted));
  };
  // After the bad frame nothing applies: keep the revision and what it created,
  // which a repair skips and may copy from.
  const auto set_aside = [&] {
    skipped_revisions_.push_back(revision);
    try {
      const auto packet = decode();
      if (accepted(packet))
        for (auto &original : packet_originals(packet))
          skipped_originals_.push_back(std::move(original));
    } catch (const Error &error) {
      if (!fault_error(error))
        throw;
    } catch (const std::bad_variant_access &) {
    }
  };
  if (read_only_) {
    ++report_.after_bad;
    set_aside();
    control(6, nullptr);
    return;
  }
  if (skipped_.contains(revision)) {
    ++report_.skipped;
    control(5, nullptr);
    return;
  }
  std::vector<std::string> inserted;
  const auto discard = [&] {
    for (const auto &id : inserted)
      originals_.erase(id);
  };
  const auto enter_read_only = [&](const Error &failure) {
    discard();
    // The read-only state (design §4.5): playback stops at the last good frame.
    read_only_ = true;
    report_.read_only = true;
    report_.bad_revision = revision;
    report_.bad_error = failure;
    control(7, &failure);
    set_aside();
  };
  try {
    const auto packet = decode();
    if (string_field(packet, "revision") != revision)
      throw Error{ErrorCode::corrupt};
    const auto op = string_field(packet, "op");
    std::vector<Original> captured;
    std::optional<Value::Array> next;
    std::optional<std::set<std::string>> status;
    // The classes of what the frame created, derived from the frame itself as the
    // writer did (design §5.2).
    ClassMap made;
    bool publishes = false;
    if (op == "append" || op == "edit" || op == "managed") {
      if (string_field(packet, "observed") != head_)
        throw Error{ErrorCode::corrupt};
      publishes = accepted(packet);
      if (publishes) {
        if (string_field(packet, "base") != head_)
          throw Error{ErrorCode::corrupt};
        if (op == "append" || op == "managed") {
          const auto &fresh = field(packet, "originals");
          if (!std::holds_alternative<Value::Array>(fresh.value()))
            throw Error{ErrorCode::corrupt};
          std::size_t ordinal = 0;
          for (const auto &entry : fresh.array()) {
            const auto &id = string_field(entry, "id");
            Original original{record.payload, ordinal++, id, fact.record,
                              record.identity};
            if (original_entry(id, trace.fresh()) ||
                !originals_.emplace(id, original).second)
              throw Error{ErrorCode::corrupt};
            inserted.push_back(id);
            captured.push_back(std::move(original));
          }
          made.set(record.identity.bytes(), derive_classes(packet));
        }
        if (const auto *format = packet.find("format")) {
          if (op != "append" || *format != Value{"append-delta-v1"} ||
              packet.find("entries"))
            throw Error{ErrorCode::corrupt};
          // An append adds entries equal to their originals: nothing to settle.
          next = entries_;
          const auto &fresh = field(packet, "originals").array();
          next->insert(next->end(), fresh.begin(), fresh.end());
          if (!valid_entries(Value{*next}, trace.fresh()))
            throw Error{ErrorCode::corrupt};
        } else {
          const auto &entries = field(packet, "entries");
          if (!valid_entries(entries, trace.fresh()))
            throw Error{ErrorCode::corrupt};
          next = entries.array();
          status = settle(*next, trace.fresh());
        }
      }
    } else if (op == "cut" || op == "repair") {
      const auto frame = decode_cut_frame(packet);
      if (frame.observed != head_)
        throw Error{ErrorCode::corrupt};
      publishes = frame.accepted;
      if (publishes) {
        if (frame.base != head_)
          throw Error{ErrorCode::corrupt};
        std::map<std::string, Value> fresh;
        for (std::size_t i = 0; i < frame.originals.size(); ++i) {
          const auto id = revision + "." + std::to_string(i);
          if (originals_.contains(id) || original_entry(id, trace.fresh()))
            throw Error{ErrorCode::corrupt};
          fresh.emplace(id, Value::object({{"id", Value{id}},
                                           {"item", frame.originals[i].item}}));
          captured.emplace_back(record.payload, i, id, fact.record, record.identity);
        }
        next = apply_cut(frame, entries_, fresh, trace.fresh());
        made.set(record.identity.bytes(), derive_classes(packet));
        for (const auto &original : captured) {
          originals_.emplace(original.id, original);
          inserted.push_back(original.id);
        }
        status = settle(*next, trace.fresh());
      }
    } else
      throw Error{ErrorCode::corrupt};
    if (publishes) {
      reserve_append(captured_, captured.size());
      for (auto &original : captured)
        captured_.push_back(std::move(original));
      entries_.swap(*next);
      if (status)
        differing_.swap(*status);
      classes_.merge(made);
      head_ = revision;
    }
    ++report_.frames;
    control(publishes ? 2 : 3, nullptr);
  } catch (const Error &failure) {
    if (!fault_error(failure)) {
      discard();
      control(4, &failure);
      throw;
    }
    enter_read_only(failure);
  } catch (const std::bad_variant_access &) {
    enter_read_only(Error{ErrorCode::corrupt});
  } catch (...) {
    discard();
    Error error{ErrorCode::external_unknown};
    try {
      throw;
    } catch (const std::bad_alloc &) {
      error = {ErrorCode::allocation};
    } catch (...) {
    }
    control(4, &error);
    throw;
  }
}
Value::Array ContextStore::apply_cut(const CutFrame &frame, const Value::Array &entries,
                                     const std::map<std::string, Value> &fresh,
                                     RawContextOrigin origin) const {
  auto next = apply_frame(frame, entries);
  // Step 7: every entry has an original; this frame's fresh ones it carries.
  for (const auto &entry : next) {
    const auto &id = string_field(entry, "id");
    if (!fresh.contains(id) && !originals_.contains(id) && !original_entry(id, origin))
      throw Error{ErrorCode::corrupt};
  }
  return next;
}
ContextStore::~ContextStore() {
  if (!open_.probe && !root_lifetime_.expired())
    log_.root().set_maintenance_checkpoint({});
}
std::optional<Value> ContextStore::original_entry(std::string_view id,
                                                  RawContextOrigin origin) const {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::original, 209, origin, &initial};
  return trace.run([&]() -> std::optional<Value> {
    auto result = [&]() -> std::optional<Value> {
      // A frame a repair skipped (or one set aside read-only) created nothing.
      if (id.size() >= 32 && skipped_frame(id.substr(0, 32)))
        return std::nullopt;
      if (const auto found = originals_.find(std::string{id});
          found != originals_.end())
        return found->second.entry(trace.fresh());
      if (archive_loaded_ || !lookup_boundary_)
        return std::nullopt;
      if (id.size() < 34 || id.size() > 53 || id[32] != '.')
        return std::nullopt;
      IdentityBytes bytes{};
      for (std::size_t i = 0; i < 16; ++i) {
        unsigned byte = 0;
        const auto result =
            std::from_chars(id.data() + i * 2, id.data() + i * 2 + 2, byte, 16);
        if (result.ec != std::errc{} || result.ptr != id.data() + i * 2 + 2 ||
            id.substr(i * 2, 2).find_first_not_of("0123456789abcdef") !=
                std::string_view::npos)
          return std::nullopt;
        bytes[i] = static_cast<std::byte>(byte);
      }
      std::size_t ordinal = 0;
      const auto parsed =
          std::from_chars(id.data() + 33, id.data() + id.size(), ordinal);
      if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size() ||
          std::to_string(ordinal) != id.substr(33))
        return std::nullopt;
      const auto identity = ApplicationRecordId::from_bytes(bytes);
      if (!identity.has_value())
        return std::nullopt;
      const std::uint8_t family = 1;
      const auto kind = RetainedKind::application;
      const auto cursor = log_.root().cursor();
      RawContextPair lookup{trace.fresh(),
                            158,
                            {1819, 1820},
                            std::array{detail::raw_native_field(family),
                                       detail::raw_native_field(kind), ByteView{bytes},
                                       ByteView{cursor.journal.bytes()},
                                       detail::raw_native_field(cursor.sequence),
                                       detail::raw_native_field(cursor.end_offset)}};
      const auto fact = lookup.run([&] {
        return log_.root().lookup(log_.root().visible(), family, kind, bytes);
      });
      if (fact) {
        const auto *application =
            std::get_if<ApplicationRecordEvent>(&fact->event.body);
        if (application)
          lookup.finish(0, std::array{ByteView{fact->record.journal.bytes()},
                                      detail::raw_native_field(fact->record.sequence),
                                      detail::raw_native_field(application->channel)});
        else
          lookup.finish(3, std::array{ByteView{fact->record.journal.bytes()},
                                      detail::raw_native_field(fact->record.sequence)});
      } else
        lookup.finish(2);
      if (!fact || fact->record.journal != lookup_boundary_->journal ||
          fact->record.sequence > lookup_boundary_->sequence)
        return std::nullopt;
      const auto *record = std::get_if<ApplicationRecordEvent>(&fact->event.body);
      if (!record || record->identity != identity.value() ||
          record->channel != ApplicationChannel::context)
        return std::nullopt;
      const auto packet = creating_projection(raw_context_read(
          record->payload,
          {2, true, fact->record, record->identity, record->channel, ordinal},
          trace.fresh()));
      if (!accepted(packet))
        return std::nullopt;
      return packet_original(packet, ordinal, id);
    }();
    trace.rows(4, result ? 1 : 0);
    return result;
  });
}
Result<void> ContextStore::checkpoint(RawContextOrigin origin) {
  try {
    const auto initial = raw_state();
    RawContextTrace trace{RawContextAction::checkpoint, 488, origin, &initial};
    return trace.run([&]() -> Result<void> {
      if (raw_enabled() && origin.input == RawContextOrigin::Input::no_new)
        throw Error{ErrorCode::capacity};
      // Nothing may record a state past a frame that did not apply (design §4.5).
      if (read_only_)
        throw Error{ErrorCode::audit_unavailable};
      // Version 2: an entry whose encoding equals its original's is a ref to the
      // retained creating record; only an entry in differing_ is written out. The
      // checkpoint reads no original and decodes nothing.
      Value::Array entries;
      entries.reserve(entries_.size());
      std::set<IdentityBytes> live;
      for (const auto &entry : entries_) {
        const auto &id = string_field(entry, "id");
        const auto ref = parse_entry_id(id);
        if (!ref)
          throw Error{ErrorCode::corrupt};
        live.insert(ref->record);
        entries.push_back(differing_.contains(id) ? entry : encode_ref(*ref));
      }
      // The classes of the live creating records (design §5.2), which this store
      // owns; a record whose classes could not be derived is derived again on
      // restore.
      classes_.retain(live);
      Value::Array sections{
          Value::object({{"name", Value{std::string{classes_section}}},
                         {"value", classes_.section()}})};
      std::set<std::string> names{std::string{classes_section}};
      for (const auto &writer : section_writers) {
        if (!writer.write || !saved_section_registered(writer.name) ||
            !names.insert(writer.name).second)
          throw Error{ErrorCode::invalid_range};
        sections.push_back(
            Value::object({{"name", Value{writer.name}}, {"value", writer.write()}}));
      }
      auto saved_context = Value::object({{"version", Value{Number{"2"}}},
                                          {"base", Value{head_}},
                                          {"entries", Value{std::move(entries)}},
                                          {"sections", Value{std::move(sections)}}});
      if (!skipped_.empty()) {
        Value::Array skipped;
        for (const auto &revision : skipped_)
          skipped.push_back(Value{revision});
        saved_context.object().emplace_back("skipped", Value{std::move(skipped)});
      }
      // The writer never saves what the shared validator would refuse. An empty
      // head with no entries is a valid empty context.
      unwrap(validate_saved_context(saved_context));
      const auto before = log_.root().cursor();
      const auto *previous = log_.root().saved_state();
      const bool had_saved = previous != nullptr;
      std::array<ByteView, 7> request_fields{};
      request_fields[0] = before.journal.bytes();
      request_fields[1] = detail::raw_native_field(before.sequence);
      request_fields[2] = detail::raw_native_field(before.end_offset);
      request_fields[3] = detail::raw_native_field(had_saved);
      std::size_t request_count = 4;
      if (previous) {
        request_fields[request_count++] = previous->boundary.journal.bytes();
        request_fields[request_count++] =
            detail::raw_native_field(previous->boundary.sequence);
        request_fields[request_count++] =
            detail::raw_native_field(previous->boundary.end_offset);
      }
      const auto request =
          trace.request(1811, std::span{request_fields}.first(request_count));
      auto save_return = [&](const Error *error) noexcept {
        const std::uint8_t branch = error ? 1 : 0;
        const auto cursor = log_.root().cursor();
        const auto *saved = log_.root().saved_state();
        const bool has_saved = saved != nullptr;
        std::array<ByteView, 10> fields{};
        std::size_t count = 0;
        fields[count++] = detail::raw_native_field(branch);
        if (error) {
          fields[count++] = detail::raw_native_field(error->code);
          fields[count++] = detail::raw_native_field(error->detail);
        }
        fields[count++] = cursor.journal.bytes();
        fields[count++] = detail::raw_native_field(cursor.sequence);
        fields[count++] = detail::raw_native_field(cursor.end_offset);
        fields[count++] = detail::raw_native_field(has_saved);
        if (saved) {
          fields[count++] = saved->boundary.journal.bytes();
          fields[count++] = detail::raw_native_field(saved->boundary.sequence);
          fields[count++] = detail::raw_native_field(saved->boundary.end_offset);
        }
        trace.reply(1812, request, std::span{fields}.first(count));
      };
      auto save = [&]() -> Result<void> {
        try {
          auto result = log_.root().save_current_state(saved_context);
          if (result.has_value())
            save_return(nullptr);
          else {
            const auto error = result.error();
            save_return(&error);
          }
          return result;
        } catch (const Error &error) {
          save_return(&error);
          throw;
        } catch (const std::bad_alloc &) {
          const Error error{ErrorCode::allocation};
          save_return(&error);
          throw;
        } catch (...) {
          const Error error{ErrorCode::external_unknown};
          save_return(&error);
          throw;
        }
      };
      auto result = save();
      if (result.has_value()) {
        history_stale_ = true;
        refresh_history();
        std::set<std::string> live_ids;
        for (const auto &entry : entries_)
          live_ids.insert(string_field(entry, "id"));
        std::erase_if(originals_,
                      [&](const auto &pair) { return !live_ids.contains(pair.first); });
        std::erase_if(differing_,
                      [&](const auto &id) { return !live_ids.contains(id); });
        std::erase_if(prints_,
                      [&](const auto &pair) { return !live_ids.contains(pair.first); });
        std::erase_if(sorted_prints_,
                      [&](const auto &pair) { return !live_ids.contains(pair.first); });
        captured_.clear();
        archive_loaded_ = false;
        // Originals pruned here are found by lookup, up to this snapshot.
        lookup_boundary_ = log_.root().saved_state()->boundary;
        trace.state(1, raw_state());
      } else
        trace.branch(1);
      checkpoint_error_ =
          result.has_value() ? std::nullopt : std::optional<Error>{result.error()};
      return result;
    });
  } catch (const Error &error) {
    checkpoint_error_ = error;
    return Result<void>::failure(error);
  } catch (const std::bad_alloc &) {
    checkpoint_error_ = Error{ErrorCode::allocation};
    return Result<void>::failure(*checkpoint_error_);
  } catch (...) {
    checkpoint_error_ = Error{ErrorCode::external_unknown};
    return Result<void>::failure(*checkpoint_error_);
  }
}
void ContextStore::maybe_checkpoint(RawContextOrigin origin) {
  const auto *saved = log_.root().saved_state();
  const auto cursor = log_.root().cursor();
  if (!head_.empty() &&
      (!saved || cursor.sequence - saved->boundary.sequence >= 64 ||
       cursor.end_offset - saved->boundary.end_offset >= 4 * 1024 * 1024))
    (void)checkpoint(
        origin); // derived accelerator failure never undoes an audit commit
}
Value ContextStore::view(RawContextOrigin origin) const {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::view, 209, origin, &initial};
  return trace.run([&] {
    auto result = Value::object({{"base", Value{head_}}, {"entries", Value{entries_}}});
    trace.rows(0, entries_.size());
    return result;
  });
}
Value ContextStore::stats(RawContextOrigin origin) const {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::stats, 642, origin, &initial};
  return trace.run([&] {
    auto number = [](std::size_t n) { return Value{Number{n}}; };
    Value::Array sizes;
    for (const auto &entry : entries_) {
      const auto &id = string_field(entry, "id");
      if (id.size() > UINT32_MAX - 56)
        throw Error{ErrorCode::overflow};
      auto data = trace.data_credit(56 + id.size());
      const auto length = unwrap(encode_packet_string(field(entry, "item"))).size();
      const auto id_length = id.size();
      trace.data(1807,
                 std::array{detail::raw_native_field(id_length),
                            detail::raw_string_field(id),
                            detail::raw_native_field(length)},
                 data ? &*data : nullptr);
      sizes.push_back(
          Value::object({{"id", field(entry, "id")}, {"item_bytes", number(length)}}));
    }
    const auto input_bytes =
        unwrap(encode_packet_string(Value{items(trace.child())})).size();
    const auto view_bytes = unwrap(encode_packet_string(view(trace.child()))).size();
    const auto entry_count = entries_.size();
    trace.event(1808, std::array{detail::raw_native_field(entry_count),
                                 detail::raw_native_field(input_bytes),
                                 detail::raw_native_field(view_bytes)});
    return Value::object({{"revision", Value{head_}},
                          {"entry_count", number(entry_count)},
                          {"input_bytes", number(input_bytes)},
                          {"view_bytes", number(view_bytes)},
                          {"entries", Value{std::move(sizes)}},
                          {"units", Value{"native binary packet bytes; not tokens"}}});
  });
}
Value::Array ContextStore::items(RawContextOrigin origin) const {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::items, 209, origin, &initial};
  return trace.run([&] {
    Value::Array items;
    items.reserve(entries_.size());
    for (const auto &entry : entries_)
      items.push_back(field(entry, "item"));
    trace.rows(1, items.size());
    return items;
  });
}
namespace {
void seed_hex(std::string_view value) {
  if (value.size() != 32 || value == std::string(32, '0') ||
      value.find_first_not_of("0123456789abcdef") != std::string_view::npos)
    throw Error{ErrorCode::invalid_range};
}
std::uint64_t seed_number(const Value &value) {
  const auto *n = std::get_if<Number>(&value.value());
  if (!n || n->text().empty())
    throw Error{ErrorCode::invalid_range};
  const auto digits = n->text();
  std::uint64_t number = 0;
  const auto r = std::from_chars(digits.data(), digits.data() + digits.size(), number);
  if (r.ec != std::errc{} || r.ptr != digits.data() + digits.size())
    throw Error{ErrorCode::invalid_range};
  return number;
}
} // namespace
void ContextStore::validate_successor_seed(const Value &seed, RawContextOrigin origin) {
  RawContextTrace trace{RawContextAction::validate_seed, 93, origin};
  trace.run([&]() -> void {
    if (unwrap(encode_packet_string(seed)).size() > 1024 * 1024 ||
        seed_number(field(seed, "version")) != 1)
      throw Error{ErrorCode::invalid_range};
    const auto &source = field(seed, "source");
    const auto &path = string_field(source, "session");
    if (path.size() > 65536 || path.find('\0') != std::string::npos ||
        !std::filesystem::path{path}.is_absolute())
      throw Error{ErrorCode::invalid_range};
    for (const auto key : {"environment", "journal", "context_revision"})
      seed_hex(string_field(source, key));
    (void)seed_number(field(source, "prefix_sequence"));
    if (seed_number(field(source, "prefix_end")) < journal_header_size)
      throw Error{ErrorCode::invalid_range};
    const auto &state = string_field(source, "status");
    if (state != "settled" && state != "unsettled" && state != "unknown")
      throw Error{ErrorCode::invalid_range};
    const auto &reason = string_field(source, "reason");
    if (reason.empty() || reason.size() > 65536)
      throw Error{ErrorCode::invalid_range};
    const auto &selected = field(seed, "entries");
    if (!std::holds_alternative<Value::Array>(selected.value()))
      throw Error{ErrorCode::invalid_range};
    const auto &entries = selected.array();
    if (entries.empty() || entries.size() > 4096)
      throw Error{ErrorCode::invalid_range};
    std::set<std::string> locators, seen, pending;
    for (const auto &entry : entries) {
      const auto &id = string_field(entry, "id");
      if (id.size() < 34 || id.size() > 53 || id[32] != '.' ||
          !locators.insert(id).second)
        throw Error{ErrorCode::invalid_range};
      seed_hex(std::string_view{id}.substr(0, 32));
      (void)seed_number(Value{Number{id.substr(33)}});
      // The source entry's unit, "<record>.<unit key>" in the source session (unit
      // S2): entries of one source unit stay one unit here.
      if (const auto *unit = entry.find("unit"))
        if (!std::holds_alternative<std::string>(unit->value()) ||
            !parse_entry_id(unit->string()))
          throw Error{ErrorCode::invalid_range};
      const auto &item = field(entry, "item");
      if (!std::holds_alternative<Value::Object>(item.value()))
        throw Error{ErrorCode::invalid_range};
      const auto *role = item.find("role");
      const auto *type = item.find("type");
      if (role) {
        if (!std::holds_alternative<std::string>(role->value()))
          throw Error{ErrorCode::invalid_range};
        const auto &r = role->string();
        if (r != "user" && r != "system" && r != "developer" && r != "assistant")
          throw Error{ErrorCode::invalid_range};
      }
      if (!type && !role)
        throw Error{ErrorCode::invalid_range};
      if (!type || (std::holds_alternative<std::string>(type->value()) &&
                    type->string() == "message")) {
        if (!role)
          throw Error{ErrorCode::invalid_range};
        const auto &content = field(item, "content");
        if (const auto *parts = std::get_if<Value::Array>(&content.value())) {
          for (const auto &part : *parts) {
            const auto &kind = string_field(part, "type");
            if (kind == "input_text" || kind == "output_text")
              (void)string_field(part, "text");
            else if (kind == "refusal")
              (void)string_field(part, "refusal");
            else
              throw Error{ErrorCode::invalid_range};
          }
        } else if (!std::holds_alternative<std::string>(content.value()))
          throw Error{ErrorCode::invalid_range};
        if (!type)
          continue;
      }
      if (!std::holds_alternative<std::string>(type->value()))
        throw Error{ErrorCode::invalid_range};
      const auto &t = type->string();
      if (t != "message" && t != "reasoning" && t != "function_call" &&
          t != "function_call_output")
        throw Error{ErrorCode::invalid_range};
      if (t != "message" && role)
        throw Error{ErrorCode::invalid_range};
      if (t == "reasoning") {
        if (const auto *native = item.find("anthropic_content")) {
          if (string_field(item, "anthropic_message_id").empty() ||
              item.find("encrypted_content"))
            throw Error{ErrorCode::invalid_range};
          const auto &kind = string_field(*native, "type");
          if (kind == "thinking") {
            (void)string_field(*native, "thinking");
            if (string_field(*native, "signature").empty())
              throw Error{ErrorCode::invalid_range};
          } else if (kind != "redacted_thinking" ||
                     string_field(*native, "data").empty())
            throw Error{ErrorCode::invalid_range};
        } else if (string_field(item, "encrypted_content").empty())
          // store:false requires self-contained reasoning, not an old ID.
          throw Error{ErrorCode::invalid_range};
        const auto &summary = field(item, "summary");
        if (!std::holds_alternative<Value::Array>(summary.value()))
          throw Error{ErrorCode::invalid_range};
        for (const auto &part : summary.array()) {
          if (string_field(part, "type") != "summary_text")
            throw Error{ErrorCode::invalid_range};
          (void)string_field(part, "text");
        }
      }
      if (t == "function_call") {
        if (string_field(item, "name").empty())
          throw Error{ErrorCode::invalid_range};
        (void)field(item, "arguments");
        const auto &call = string_field(item, "call_id");
        if (call.empty() || !seen.insert(call).second)
          throw Error{ErrorCode::conflict};
        pending.insert(call);
      } else if (t == "function_call_output") {
        (void)field(item, "output");
        if (pending.erase(string_field(item, "call_id")) != 1)
          throw Error{ErrorCode::conflict};
      }
    }
    if (!pending.empty())
      throw Error{ErrorCode::conflict};
  });
}
void ContextStore::seed_successor(const Value &seed, RawContextOrigin origin) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::seed, 1481, origin, &initial};
  trace.run([&]() -> void {
    require_writable();
    validate_successor_seed(seed, trace.child());
    if (workflow_ || pending_ || !entries_.empty() || log_.root().fact_count() != 0 ||
        log_.root().state() != JournalWriterState::live ||
        string_field(field(seed, "source"), "journal") ==
            hex_identity(log_.root().cursor().journal.bytes()))
      throw Error{ErrorCode::conflict};
    Value::Array items, locators, units;
    bool named = false;
    for (const auto &entry : field(seed, "entries").array()) {
      items.push_back(field(entry, "item"));
      locators.push_back(Value{string_field(entry, "id")});
      const auto *unit = entry.find("unit");
      named |= unit != nullptr;
      units.push_back(unit ? *unit : Value{});
    }
    auto lineage = Value::object(
        {{"version", Value{Number{"1"}}},
         {"scope",
          Value{"declared source; no inherited admissions or settlement verification"}},
         {"source", field(seed, "source")},
         {"source_entries", Value{std::move(locators)}}});
    if (named)
      lineage.object().emplace_back("source_units", Value{std::move(units)});
    append_impl(std::move(items), "session.successor", &lineage, trace.child());
  });
}
void ContextStore::append(Value::Array items, std::string_view label,
                          RawContextOrigin origin) {
  append_impl(std::move(items), label, nullptr, origin);
}
void ContextStore::append(Value::Array items, std::string_view label,
                          UnitClass declared, RawContextOrigin origin) {
  append_impl(std::move(items), label, nullptr, origin, declared);
}
void ContextStore::append_impl(Value::Array items, std::string_view label,
                               const Value *lineage, RawContextOrigin origin,
                               std::optional<UnitClass> declared) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::append,
                        origin.input == RawContextOrigin::Input::no_new ? 1080ULL
                                                                        : 1228ULL,
                        origin, &initial};
  trace.run([&]() -> void {
    require_writable();
    trace.label(0, label,
                origin.input == RawContextOrigin::Input::no_new &&
                    label == "stop.linkage");
    for (const auto &item : items)
      if (!std::holds_alternative<Value::Object>(item.value()))
        throw Error{ErrorCode::corrupt};
    const auto recorded = recorded_class(label, items, declared);
    auto identity = log_.issue();
    auto revision = hex_identity(identity.bytes());
    auto next = entries_;
    std::map<std::string, Original> originals;
    Value::Array fresh;
    for (std::size_t i = 0; i < items.size(); ++i) {
      const auto id = revision + "." + std::to_string(i);
      auto entry = Value::object({{"id", Value{id}}, {"item", items[i]}});
      if (originals_.contains(id))
        throw Error{ErrorCode::conflict};
      originals.emplace(id, Original{entry, identity});
      fresh.push_back(entry);
      next.push_back(std::move(entry));
    }
    auto packet = Value::object({{"op", Value{"append"}},
                                 {"base", Value{head_}},
                                 {"observed", Value{head_}},
                                 {"revision", Value{revision}},
                                 {"accepted", Value{true}},
                                 {"origin", Value{std::string{label}}},
                                 {"format", Value{"append-delta-v1"}},
                                 {"originals", Value{std::move(fresh)}}});
    if (lineage)
      packet.object().emplace_back("lineage", *lineage);
    // The class comes from this record: its label, or the class a lua.synthetic
    // append declared or defaulted to (which the label does not carry). Playback
    // derives it the same way.
    if (recorded)
      packet.object().emplace_back("class",
                                   Value{Number{static_cast<std::uint8_t>(*recorded)}});
    ClassMap made;
    made.set(identity.bytes(), derive_classes(packet));
    std::vector<Original> captured;
    for (const auto &entry : field(packet, "originals").array())
      captured.emplace_back(entry, identity);
    reserve_append(captured_, captured.size());
    auto expected = revision;
    if (protect)
      trace.protection([&](const RawEventRef &request) {
        protect(next, pending_proposal(trace.child()), request);
      });
    auto publication = trace.publication();
    const auto reference =
        log_.record(identity, ApplicationChannel::context, packet, &publication);
    history_stale_ = true;
    for (auto &entry : captured) {
      entry.record = reference;
      captured_.push_back(std::move(entry));
    }
    for (auto &[id, original] : originals)
      original.record = reference;
    if (pending_ && pending_->expected == head_)
      pending_->expected.swap(expected);
    entries_.swap(next);
    originals_.merge(originals);
    classes_.merge(made);
    head_.swap(revision);
    trace.state(1, raw_state());
    refresh_history();
    maybe_checkpoint(trace.child());
  });
}
std::optional<std::size_t> ContextStore::input_bytes() const noexcept {
  try {
    Value::Array items;
    items.reserve(entries_.size());
    for (const auto &entry : entries_)
      items.push_back(field(entry, "item"));
    const auto encoded = encode_packet_string(Value{std::move(items)});
    return encoded.has_value() ? std::optional{encoded.value().size()} : std::nullopt;
  } catch (...) {
    return std::nullopt; // Presentation only: never throws between record and swap.
  }
}
void ContextStore::notify(std::optional<ContextChange> change) const noexcept {
  if (!change || !changed)
    return;
  try {
    changed(*change);
  } catch (...) {
  } // Presentation cannot undo a published revision.
}
Value ContextStore::edit(const Value &candidate, RawContextOrigin origin,
                         EditKeys keys) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::edit, 1228, origin, &initial};
  std::optional<ContextChange> change;
  auto result = trace.run([&]() -> Value {
    require_writable();
    const auto *base = candidate.find("base");
    const auto *entries = candidate.find("entries");
    // A new edit names only the keys entries have: an unknown key is refused here,
    // not carried into every later snapshot.
    // A new or changed entry names only the keys entries have: an unknown key is
    // refused here, not carried into every later snapshot. An entry byte-identical
    // to the current entry under its ID passes through as it is, legacy keys
    // included, so an edit of the unchanged context is never refused.
    std::string unknown_key;
    if (keys == EditKeys::strict && entries)
      if (const auto *list = std::get_if<Value::Array>(&entries->value())) {
        std::map<std::string_view, const Value *> current;
        for (const auto &entry : entries_)
          current.emplace(string_field(entry, "id"), &entry);
        for (const auto &entry : *list) {
          if (!saved_entry_shape(entry) || edit_entry_shape(entry))
            continue;
          const auto &id = entry.find("id")->string();
          if (const auto found = current.find(id);
              found != current.end() &&
              (found->second->same(entry) ||
               unwrap(encode_packet(*found->second)) == unwrap(encode_packet(entry))))
            continue;
          for (const auto &[key, value] : entry.object())
            if (key != "id" && key != "item" &&
                !(key == "ancestry" &&
                  std::holds_alternative<Value::Array>(value.value())))
              unknown_key = "entry " + id + ": key " + key +
                            (key == "ancestry" ? " must be an array of entry IDs"
                                               : " is not id, item or ancestry");
          break;
        }
      }
    const bool representation = unknown_key.empty() && base != nullptr &&
                                std::holds_alternative<std::string>(base->value()) &&
                                entries != nullptr &&
                                valid_entries(*entries, trace.fresh());
    const bool publishes = representation && base->string() == head_;
    auto identity = log_.issue();
    auto revision = hex_identity(identity.bytes());
    Value::Array next;
    if (publishes)
      next = entries->array();
    auto outcome = Value::object(
        {{"accepted", Value{publishes}},
         {"revision", Value{revision}},
         {"current", Value{publishes ? revision : head_}},
         {"reason", Value{publishes              ? "published"
                          : representation       ? "stale-base"
                          : !unknown_key.empty() ? "unknown-entry-key"
                                                 : "invalid-representation"}}});
    if (!unknown_key.empty())
      outcome.object().emplace_back("detail", Value{unknown_key});
    auto details = candidate;
    if (std::holds_alternative<Value::Object>(details.value()))
      std::erase_if(details.object(),
                    [](const auto &pair) { return pair.first == "entries"; });
    auto packet = Value::object({{"op", Value{"edit"}},
                                 {"base", base == nullptr ? Value{} : *base},
                                 {"observed", Value{head_}},
                                 {"revision", Value{revision}},
                                 {"accepted", Value{publishes}},
                                 {"candidate", std::move(details)},
                                 {"entries", entries == nullptr ? Value{} : *entries},
                                 {"outcome", outcome}});
    if (protect)
      trace.protection([&](const RawEventRef &request) {
        protect(publishes ? next : entries_, pending_proposal(trace.child()), request);
      });
    const auto before =
        publishes && changed ? input_bytes() : std::optional<std::size_t>{};
    auto status = publishes ? settle(next, trace.fresh()) : differing_;
    auto publication = trace.publication();
    (void)log_.record(identity, ApplicationChannel::context, packet, &publication);
    history_stale_ = true;
    if (publishes) {
      entries_.swap(next);
      differing_.swap(status);
      head_.swap(revision);
      if (changed)
        change = ContextChange{next_kind_, head_, {}, before, input_bytes()};
    }
    trace.state(1, raw_state());
    trace.branch(publishes ? 0 : 1);
    refresh_history();
    maybe_checkpoint(trace.child());
    return outcome;
  });
  notify(std::move(change));
  return result;
}
void ContextStore::restore(std::string_view entry, RawContextOrigin origin) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::restore, 1388, origin, &initial};
  trace.run([&]() -> void {
    trace.label(1, entry);
    const auto original = original_entry(entry, trace.fresh());
    if (!original)
      throw Error{ErrorCode::invalid_identity};
    auto next = entries_;
    bool present = false;
    for (auto &value : next)
      if (string_field(value, "id") == entry) {
        value = Value::object(
            {{"id", Value{std::string{entry}}}, {"item", field(*original, "item")}});
        present = true;
        break;
      }
    if (!present)
      next.push_back(Value::object(
          {{"id", Value{std::string{entry}}}, {"item", field(*original, "item")}}));
    next_kind_ = ContextChangeKind::restored;
    try {
      (void)edit(
          Value::object({{"base", Value{head_}}, {"entries", Value{std::move(next)}}}),
          trace.child());
    } catch (...) {
      next_kind_ = ContextChangeKind::edited;
      throw;
    }
    next_kind_ = ContextChangeKind::edited;
  });
}
Value ContextStore::originals(RawContextOrigin origin) const {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::originals, 209, origin, &initial};
  return trace.run([&]() -> Value {
    auto out = captured_entries(trace.fresh());
    std::sort(out.begin(), out.end(), [](const Value &a, const Value &b) {
      return string_field(a, "id") < string_field(b, "id");
    });
    for (auto &entry : out)
      entry =
          Value::object({{"id", field(entry, "id")}, {"item", field(entry, "item")}});
    const auto count = out.size();
    auto result = Value{std::move(out)};
    trace.rows(3, count);
    return result;
  });
}
// Managed transformations share the native context journal and publication owner.
namespace {
bool protocol_complete(const Value::Array &entries) {
  std::set<std::string> seen, pending;
  for (const auto &entry : entries) {
    const auto &item = field(entry, "item");
    const auto *type = item.find("type");
    if (!type)
      continue;
    if (!std::holds_alternative<std::string>(type->value()))
      return false;
    if (type->string() == "function_call") {
      const auto &id = string_field(item, "call_id");
      if (id.empty() || !seen.insert(id).second)
        return false;
      pending.insert(id);
    } else if (type->string() == "function_call_output") {
      if (pending.erase(string_field(item, "call_id")) != 1)
        return false;
    }
  }
  return pending.empty();
}
// The pairing faults of a context: "call:<id>" for a call no output answers,
// "output:<id>" for an output with no open call, "duplicate:<id>" for a call ID
// used twice, "malformed" for a non-string type. A repair may not add one the head
// before the bad frame did not already have.
std::set<std::string> pairing_faults(const Value::Array &entries) {
  std::set<std::string> seen, pending, faults;
  for (const auto &entry : entries) {
    const auto &item = field(entry, "item");
    const auto *type = item.find("type");
    if (!type)
      continue;
    if (!std::holds_alternative<std::string>(type->value())) {
      faults.insert("malformed");
      continue;
    }
    if (type->string() == "function_call") {
      const auto &id = string_field(item, "call_id");
      if (!seen.insert(id).second)
        faults.insert("duplicate:" + id);
      pending.insert(id);
    } else if (type->string() == "function_call_output") {
      const auto &id = string_field(item, "call_id");
      if (pending.erase(id) != 1)
        faults.insert("output:" + id);
    }
  }
  for (const auto &id : pending)
    faults.insert("call:" + id);
  return faults;
}
bool adds_no_pairing_fault(const Value::Array &before, const Value::Array &after) {
  const auto had = pairing_faults(before);
  const auto has = pairing_faults(after);
  return std::includes(had.begin(), had.end(), has.begin(), has.end());
}
// Entries of one append share its revision, the ID before the last '.'.
std::string_view append_of(const Value &entry) {
  const std::string_view id = string_field(entry, "id");
  return id.substr(0, id.rfind('.'));
}
// A response round is atomic: a reasoning item stays with every later entry of its
// unit (message, calls), and whole_groups then binds those calls' outputs.
// Providers reject reasoning or thinking separated from the items that follow it.
// slot names an entry's unit key in its creating record, before outputs join their
// calls (design §5.2): a seeded record's source rounds are separate.
bool whole_rounds(const Value::Array &entries, const std::set<std::string> &ids,
                  const std::function<ContextRef(const Value &)> &slot) {
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto *type = field(entries[i], "item").find("type");
    if (!type || *type != Value{"reasoning"})
      continue;
    const bool chosen = ids.contains(string_field(entries[i], "id"));
    const auto unit = slot(entries[i]);
    for (auto j = i + 1; j < entries.size() && slot(entries[j]) == unit; ++j)
      if (ids.contains(string_field(entries[j], "id")) != chosen)
        return false;
  }
  return true;
}
// Membership by call ID (design §5.1): a call and its output are selected together
// wherever they sit, so a call and an output separated by an instruction can be
// archived without it. An output with no earlier call, a second output, and a
// selected call that no output answers yet are refused.
bool whole_groups(const Value::Array &entries, const std::set<std::string> &ids) {
  std::map<std::string, bool> open; // call_id -> selected
  for (const auto &entry : entries) {
    const auto &item = field(entry, "item");
    const auto *type = item.find("type");
    if (!type || !std::holds_alternative<std::string>(type->value()))
      continue;
    const bool selected = ids.contains(string_field(entry, "id"));
    if (type->string() == "function_call") {
      if (!open.emplace(string_field(item, "call_id"), selected).second)
        return false;
    } else if (type->string() == "function_call_output") {
      const auto call = open.find(string_field(item, "call_id"));
      if (call == open.end() || call->second != selected)
        return false;
      open.erase(call);
    }
  }
  // Open current tool calls are allowed only if untouched.
  return std::none_of(open.begin(), open.end(),
                      [](const auto &call) { return call.second; });
}
// The open call's whole round at the tail of basis, which a select keeps: from the
// first call no output answers, back over the reasoning and message of its append,
// to the end.
std::set<std::string> active_round(const Value::Array &basis) {
  std::set<std::string> pending_calls, active;
  std::size_t open_start = 0;
  for (std::size_t i = 0; i < basis.size(); ++i) {
    const auto &item = field(basis[i], "item");
    const auto *type = item.find("type");
    if (!type || !std::holds_alternative<std::string>(type->value()))
      continue;
    if (type->string() == "function_call") {
      if (pending_calls.empty())
        open_start = i;
      pending_calls.insert(string_field(item, "call_id"));
    } else if (type->string() == "function_call_output") {
      pending_calls.erase(string_field(item, "call_id"));
    }
  }
  if (pending_calls.empty())
    return active;
  while (open_start > 0 &&
         append_of(basis[open_start - 1]) == append_of(basis[open_start]))
    --open_start;
  for (std::size_t i = open_start; i < basis.size(); ++i)
    active.insert(string_field(basis[i], "id"));
  return active;
}
Value number(std::size_t n) { return Value{Number{n}}; }
std::size_t index_field(const Value &q, std::string_view key, std::size_t fallback) {
  const auto *v = q.find(key);
  if (!v)
    return fallback;
  if (!std::holds_alternative<Number>(v->value()))
    throw Error{ErrorCode::invalid_range};
  std::size_t n = 0;
  const auto &s = v->number().text();
  const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
  if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
    throw Error{ErrorCode::invalid_range};
  return n;
}
} // namespace
Value ContextStore::reject_managed(const Value &proposal, std::string_view why,
                                   RawContextOrigin origin, bool *settled) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::reject, 513, origin, &initial};
  return trace.run([&]() -> Value {
    require_writable();
    auto identity = log_.issue();
    auto revision = hex_identity(identity.bytes());
    auto outcome = Value::object({{"accepted", Value{false}},
                                  {"reason", Value{std::string{why}}},
                                  {"current", Value{head_}},
                                  {"revision", Value{revision}}});
    auto packet = Value::object({{"op", Value{"managed"}},
                                 {"revision", Value{revision}},
                                 {"observed", Value{head_}},
                                 {"accepted", Value{false}},
                                 {"candidate", proposal},
                                 {"outcome", outcome}});
    trace.label(2, why, true);
    auto publication = trace.publication();
    (void)log_.record(identity, ApplicationChannel::context, packet, &publication);
    if (settled)
      *settled = true;
    history_stale_ = true;
    trace.state(1, raw_state());
    trace.branch(1);
    refresh_history();
    return outcome;
  });
}
Value ContextStore::pending_proposal(RawContextOrigin origin) const {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::pending, 209, origin, &initial};
  return trace.run([&] {
    auto result = pending_ ? pending_->proposal : Value{};
    trace.rows(2, pending_ ? 1 : 0);
    return result;
  });
}
Value::Array ContextStore::stop_outputs(const Value::Array &entries, bool interrupted) {
  std::set<std::string> calls;
  for (const auto &entry : entries) {
    const auto &item = field(entry, "item");
    const auto *type = item.find("type");
    if (type && *type == Value{"function_call"})
      calls.insert(string_field(item, "call_id"));
    if (type && *type == Value{"function_call_output"})
      calls.erase(string_field(item, "call_id"));
  }
  Value::Array outputs;
  for (const auto &call : calls)
    outputs.push_back(Value::object(
        {{"type", Value{"function_call_output"}},
         {"call_id", Value{call}},
         {"output",
          Value::object(
              {{"error", Value{interrupted ? "turn_interrupted" : "turn_stopped"}},
               {"detail",
                Value{"Consult audit for effect outcome; no automatic retry"}}})}}));
  return outputs;
}
JournalCapacity ContextStore::cancellation_budget(const Value::Array &entries,
                                                  const Value &proposal,
                                                  RawContextOrigin origin) const {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::budget, 224, origin, &initial};
  return trace.run([&]() -> JournalCapacity {
    const auto limits = log_.root().journal_limits();
    // Bounded receipt/diagnostic maintenance must fit too, even in custom journals.
    if (limits.max_payload < 1024 || limits.max_batch_bytes < 2048)
      throw Error{ErrorCode::capacity};
    IdentityBytes id{};
    id[0] = std::byte{1};
    const auto identity = unwrap(ApplicationRecordId::from_bytes(id));
    const std::string revision(32, '0');
    const auto issuer = unwrap(
        encode_retained_event({{}, IssuerReservationEvent{1}}, limits.max_payload));
    const auto packet_cost = [&](const Value &packet) {
      const auto serialized = unwrap(encode_packet_string(packet));
      const auto raw = std::as_bytes(std::span{serialized.data(), serialized.size()});
      auto encoded = encode_retained_event(
          {{},
           ApplicationRecordEvent{
               identity, ApplicationChannel::context, {raw.begin(), raw.end()}}},
          limits.max_payload);
      if (!encoded.has_value()) {
        if (encoded.error().code == ErrorCode::capacity ||
            encoded.error().code == ErrorCode::invalid_range)
          throw Error{ErrorCode::capacity};
        throw encoded.error();
      }
      const auto cost = encoded.value().size() + journal_frame_header_size + 56;
      if (cost > limits.max_batch_bytes)
        throw Error{ErrorCode::capacity};
      return cost + issuer.size() + journal_frame_header_size + 56;
    };
    // This floor covers bounded workflow diagnostics in addition to exact context
    // obligations; no guessed context-size multiplier or physical limit increase.
    JournalCapacity credit{8192, 16};
    auto outputs = stop_outputs(entries, true); // longer supported representation
    if (!outputs.empty()) {
      Value::Array fresh, next = entries;
      for (std::size_t i = 0; i < outputs.size(); ++i) {
        auto entry = Value::object({{"id", Value{revision + "." + std::to_string(i)}},
                                    {"item", std::move(outputs[i])}});
        fresh.push_back(entry);
        next.push_back(std::move(entry));
      }
      auto packet = Value::object({{"op", Value{"append"}},
                                   {"base", Value{revision}},
                                   {"observed", Value{revision}},
                                   {"revision", Value{revision}},
                                   {"accepted", Value{true}},
                                   {"origin", Value{"stop.linkage"}},
                                   {"originals", Value{std::move(fresh)}},
                                   {"entries", Value{std::move(next)}}});
      credit.max_file_bytes += packet_cost(packet);
    }
    if (proposal != Value{}) {
      auto outcome = Value::object({{"accepted", Value{false}},
                                    {"reason", Value{"settlement-conflict"}},
                                    {"current", Value{revision}},
                                    {"revision", Value{revision}}});
      auto packet = Value::object({{"op", Value{"managed"}},
                                   {"revision", Value{revision}},
                                   {"observed", Value{revision}},
                                   {"accepted", Value{false}},
                                   {"candidate", proposal},
                                   {"outcome", std::move(outcome)}});
      credit.max_file_bytes += packet_cost(packet);
    }
    trace.event(1825, std::array{detail::raw_native_field(limits.max_payload),
                                 detail::raw_native_field(limits.max_batch_bytes),
                                 detail::raw_native_field(credit.max_file_bytes),
                                 detail::raw_native_field(credit.max_records)});
    return credit;
  });
}
void ContextStore::begin_workflow(RawContextOrigin origin) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::begin, 268, origin, &initial};
  trace.run([&]() -> void {
    require_writable();
    if (workflow_)
      throw Error{ErrorCode::conflict};
    workflow_ = true;
    trace.state(1, raw_state());
  });
}
Value ContextStore::manage(const Value &proposal, RawContextOrigin origin) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::manage, 2894, origin, &initial};
  return trace.run([&]() -> Value {
    require_writable();
    try {
      if (string_field(proposal, "base") != head_)
        return reject_managed(proposal, "stale-base", trace.child());
      if (pending_)
        return reject_managed(proposal, "pending-overlap", trace.child());
      const auto &mode = string_field(proposal, "mode");
      if (mode != "select" && mode != "archive" && mode != "summarize" &&
          mode != "restore")
        return reject_managed(proposal, "invalid-mode", trace.child());
      const auto &reason = string_field(proposal, "reason");
      const auto &source = string_field(proposal, "source");
      trace.label(2, reason);
      trace.label(5, source);
      if (reason.empty() || source.empty())
        return reject_managed(proposal, "missing-source-or-reason", trace.child());
      const auto &ids = field(proposal, "ids");
      if (!std::holds_alternative<Value::Array>(ids.value()))
        return reject_managed(proposal, "invalid-ids", trace.child());
      std::set<std::string> selected;
      for (const auto &id : ids.array()) {
        if (!std::holds_alternative<std::string>(id.value()) ||
            (!originals_.contains(id.string()) &&
             !original_entry(id.string(), trace.fresh())) ||
            !selected.insert(id.string()).second)
          return reject_managed(proposal, "invalid-ids", trace.child());
        if (mode != "restore" &&
            std::none_of(entries_.begin(), entries_.end(), [&](const Value &e) {
              return string_field(e, "id") == id.string();
            }))
          return reject_managed(proposal, "absent-id", trace.child());
      }
      if (mode == "summarize") {
        const auto &summary = field(proposal, "summary");
        if (selected.empty() ||
            !std::holds_alternative<Value::Object>(summary.value()) ||
            summary.find("type") ||
            (string_field(summary, "role") != "developer" &&
             string_field(summary, "role") != "assistant") ||
            string_field(summary, "content").empty())
          return reject_managed(proposal, "invalid-summary", trace.child());
      }
      // Only restore reads captured originals; otherwise use entries_ without a copy.
      std::optional<Value::Array> captured;
      // The classes of historical records, derived from the packets the capture
      // visit decodes, so no record is read twice.
      ClassMap historical;
      if (mode == "restore")
        captured = captured_entries(trace.fresh(), &historical);
      const auto &candidates = captured ? *captured : entries_;
      if (!whole_groups(candidates, selected))
        return reject_managed(proposal, "partial-tool-group", trace.child());
      begin_class_pass();
      const auto slot = [&](const Value &entry) {
        const auto ref = parse_entry_id(string_field(entry, "id"));
        if (!ref)
          throw Error{ErrorCode::corrupt};
        const auto *known = historical.find(ref->record);
        const auto &classes =
            known ? *known : record_classes_of(ref->record, candidates, trace.fresh());
        return ContextRef{ref->record, classes.key(ref->ordinal)};
      };
      if (!whole_rounds(candidates, selected, slot))
        return reject_managed(proposal, "partial-round", trace.child());
      // Selections are whole units, protected by the creating record's class
      // (design §5.1-§5.3), not by role: instructions and tool state are never
      // archived, summarised or left out of a select, except that a superseded
      // instruction copy may be archived or left out; pinned units are never
      // removed. A select keeps the open round at the tail as it always has.
      if (mode != "restore") {
        const auto kept =
            mode == "select" ? active_round(entries_) : std::set<std::string>{};
        for (const auto &unit : live_units(trace.fresh())) {
          std::size_t removed = 0;
          for (const auto position : unit.entries) {
            const auto &id = string_field(entries_[position], "id");
            removed += mode == "select" ? !selected.contains(id) && !kept.contains(id)
                                        : selected.contains(id);
          }
          if (removed == 0)
            continue;
          if ((unit.unit_class == UnitClass::instruction &&
               (!unit.superseded || mode == "summarize")) ||
              unit.unit_class == UnitClass::tool_state)
            return reject_managed(proposal, "protected-unit", trace.child());
          if (unit.pinned)
            return reject_managed(proposal, "pinned-unit", trace.child());
          if (removed != unit.entries.size())
            return reject_managed(proposal, "partial-unit", trace.child());
        }
      }
      const auto mode_length = mode.size(), selected_count = selected.size();
      const std::size_t stage_length = 0;
      trace.event(1823, std::array{detail::raw_native_field(mode_length),
                                   detail::raw_string_field(mode),
                                   detail::raw_native_field(selected_count),
                                   detail::raw_native_field(stage_length),
                                   detail::raw_string_field("")});
      if (!workflow_)
        return publish_managed(proposal, entries_, "", Value{}, trace.child());
      if (protect)
        trace.protection(
            [&](const RawEventRef &request) { protect(entries_, proposal, request); });
      auto result = reject_managed(proposal, "staged", trace.child());
      pending_ = Pending{proposal, entries_, head_, string_field(result, "revision")};
      trace.state(1, raw_state());
      trace.branch(2);
      result.object().emplace_back("staged", Value{true});
      return result;
    } catch (const Error &e) {
      if (e.code != ErrorCode::corrupt && e.code != ErrorCode::invalid_range)
        throw;
      return reject_managed(proposal, "malformed-proposal", trace.child());
    }
  });
}
Value ContextStore::finish_workflow(bool success, const Value &boundary_program,
                                    RawContextOrigin origin,
                                    WorkflowAcceptance *acceptance) {
  WorkflowAcceptance local{};
  auto &accepted_publication = acceptance ? *acceptance : local;
  accepted_publication = {};
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::finish, success ? 2978ULL : 997ULL, origin,
                        &initial};
  return trace.run([&]() -> Value {
    const bool has_boundary =
        !std::holds_alternative<std::nullptr_t>(boundary_program.value());
    trace.event(1827, std::array{detail::raw_native_field(success),
                                 detail::raw_native_field(has_boundary)});
    workflow_ = false;
    trace.state(1, raw_state());
    auto publish_program = [&] {
      const auto identity = log_.issue();
      auto publication = trace.publication();
      (void)log_.record(identity, ApplicationChannel::program, boundary_program,
                        &publication);
      accepted_publication.boundary_program_accepted = true;
      history_stale_ = true;
    };
    if (!pending_) {
      if (success && has_boundary)
        publish_program();
      refresh_history();
      return Value{};
    }
    if (!success) {
      auto pending = std::move(*pending_);
      pending_.reset();
      trace.state(2, raw_state());
      if (log_.root().state() != JournalWriterState::live) {
        trace.branch(1);
        return Value::object(
            {{"accepted", Value{false}},
             {"reason", Value{"workflow-cancelled-audit-unavailable"}}});
      }
      return reject_managed(pending.proposal, "workflow-cancelled", trace.child(),
                            &accepted_publication.context_settlement_accepted);
    }
    const auto pending = *pending_;
    try {
      auto result =
          pending.expected != head_
              ? reject_managed(pending.proposal, "settlement-conflict", trace.child(),
                               &accepted_publication.context_settlement_accepted)
              : publish_managed(pending.proposal, pending.snapshot, pending.stage,
                                boundary_program, trace.child(), &accepted_publication);
      if (!accepted(result) && has_boundary)
        publish_program();
      pending_.reset();
      trace.state(3, raw_state());
      refresh_history();
      return result;
    } catch (...) {
      if (accepted_publication.context_settlement_accepted) {
        pending_.reset();
        trace.state(3, raw_state());
      }
      throw;
    }
  });
}
Value ContextStore::publish_managed(const Value &proposal, const Value::Array &basis,
                                    std::string_view stage,
                                    const Value &boundary_program,
                                    RawContextOrigin origin,
                                    WorkflowAcceptance *acceptance) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::publish, 1809, origin, &initial};
  std::optional<ContextChange> change;
  auto result = trace.run([&]() -> Value {
    const auto &mode = string_field(proposal, "mode");
    std::set<std::string> selected;
    for (const auto &id : field(proposal, "ids").array())
      selected.insert(id.string());
    const auto mode_length = mode.size(), selected_count = selected.size(),
               stage_length = stage.size();
    trace.event(1823, std::array{detail::raw_native_field(mode_length),
                                 detail::raw_string_field(mode),
                                 detail::raw_native_field(selected_count),
                                 detail::raw_native_field(stage_length),
                                 detail::raw_string_field(stage)});
    auto identity = log_.issue();
    auto revision = hex_identity(identity.bytes());
    Value::Array next, fresh;
    const auto active_entries = active_round(basis);
    // Pins are rechecked at settlement (design §5.4): a unit pinned after this
    // proposal staged stays live, and the outcome lists it as refused.
    Value::Array refused_units;
    if (!stage.empty() && !pins_.empty() && mode != "restore") {
      std::set<std::string> staged;
      for (const auto &entry : basis)
        staged.insert(string_field(entry, "id"));
      for (const auto &unit : live_units(trace.fresh())) {
        if (!unit.pinned)
          continue;
        bool touched = false;
        for (const auto position : unit.entries) {
          const auto &id = string_field(entries_[position], "id");
          touched |= mode == "select" ? staged.contains(id) && !selected.contains(id) &&
                                            !active_entries.contains(id)
                                      : selected.contains(id);
        }
        if (!touched)
          continue;
        for (const auto position : unit.entries) {
          const auto &id = string_field(entries_[position], "id");
          if (mode == "select")
            selected.insert(id);
          else
            selected.erase(id);
        }
        refused_units.push_back(
            Value::object({{"unit", Value{unit.handle}}, {"reason", Value{"pinned"}}}));
      }
      if (mode != "select" && selected.empty())
        return reject_managed(proposal, "pinned", trace.child(),
                              acceptance ? &acceptance->context_settlement_accepted
                                         : nullptr);
    }
    Value::Array ancestry;
    for (const auto &id : field(proposal, "ids").array())
      if (selected.contains(id.string()))
        ancestry.push_back(id);
    bool inserted = false;
    for (const auto &entry : basis) {
      const bool chosen = selected.contains(string_field(entry, "id"));
      if (mode == "summarize" && chosen && !inserted) {
        auto summary = Value::object({{"id", Value{revision + ".0"}},
                                      {"item", field(proposal, "summary")},
                                      {"ancestry", Value{ancestry}}});
        next.push_back(summary);
        fresh.push_back(summary);
        inserted = true;
      }
      if ((mode == "select" &&
           (chosen || active_entries.contains(string_field(entry, "id")))) ||
          ((mode == "archive" || mode == "summarize") && !chosen) || mode == "restore")
        next.push_back(entry);
    }
    // Explicit append-only continuation settlement, retained separately from input
    // base.
    if (!stage.empty()) {
      if (entries_.size() < basis.size() ||
          !std::equal(basis.begin(), basis.end(), entries_.begin()))
        return reject_managed(proposal, "settlement-conflict", trace.child(),
                              acceptance ? &acceptance->context_settlement_accepted
                                         : nullptr);
      next.insert(next.end(),
                  entries_.begin() + static_cast<std::ptrdiff_t>(basis.size()),
                  entries_.end());
    }
    if (mode == "restore") {
      // Remove selected presentations first, then merge exact originals in capture
      // order. This also repairs a reordered/edited group, rather than retaining its
      // bad order.
      std::erase_if(next, [&](const Value &entry) {
        return selected.contains(string_field(entry, "id"));
      });
      const auto capture_order = captured_entries(trace.fresh());
      for (const auto &original : capture_order) {

        const auto &id = string_field(original, "id");
        if (!selected.contains(id))
          continue;
        auto pos = std::find_if(next.begin(), next.end(), [&](const Value &entry) {
          return string_field(entry, "id") == id;
        });
        if (pos != next.end()) {
          *pos = original;
          continue;
        }
        // Insert before the first present original captured later, never reorder live
        // entries.
        bool later = false;
        std::set<std::string> successors;
        for (const auto &entry : capture_order) {
          if (later)
            successors.insert(string_field(entry, "id"));
          if (string_field(entry, "id") == id)
            later = true;
        }
        pos = std::find_if(next.begin(), next.end(), [&](const Value &entry) {
          return successors.contains(string_field(entry, "id"));
        });
        next.insert(pos, original);
      }
    }
    if (!protocol_complete(next))
      return reject_managed(proposal, "invalid-tool-protocol", trace.child(),
                            acceptance ? &acceptance->context_settlement_accepted
                                       : nullptr);
    std::map<std::string, Original> originals;
    std::vector<Original> captured;
    for (const auto &entry : fresh) {
      if (originals_.contains(string_field(entry, "id")))
        throw Error{ErrorCode::conflict};
      originals.emplace(string_field(entry, "id"), Original{entry, identity});
      captured.emplace_back(entry, identity);
    }
    reserve_append(captured_, captured.size());
    auto outcome = Value::object({{"accepted", Value{true}},
                                  {"reason", Value{"published"}},
                                  {"revision", Value{revision}},
                                  {"current", Value{revision}}});
    if (!refused_units.empty())
      outcome.object().emplace_back("refused", Value{std::move(refused_units)});
    auto packet = Value::object({{"op", Value{"managed"}},
                                 {"base", Value{head_}},
                                 {"observed", Value{head_}},
                                 {"revision", Value{revision}},
                                 {"accepted", Value{true}},
                                 {"candidate", proposal},
                                 {"stage", Value{std::string{stage}}},
                                 {"entries", Value{next}},
                                 {"originals", Value{fresh}},
                                 {"outcome", outcome}});
    if (protect)
      trace.protection(
          [&](const RawEventRef &request) { protect(next, proposal, request); });
    const bool has_boundary =
        !std::holds_alternative<std::nullptr_t>(boundary_program.value());
    const auto before = changed ? input_bytes() : std::optional<std::size_t>{};
    std::map<std::string, Value> created;
    for (const auto &entry : fresh)
      created.emplace(string_field(entry, "id"), entry);
    auto status = settle(next, trace.fresh(), &created);
    ClassMap made;
    made.set(identity.bytes(), derive_classes(packet));
    auto publication = trace.publication();
    (void)log_.record_boundary(identity, packet, boundary_program, &publication);
    if (acceptance) {
      acceptance->context_settlement_accepted = true;
      if (has_boundary)
        acceptance->boundary_program_accepted = true;
    }
    history_stale_ = true;
    originals_.merge(originals);
    classes_.merge(made);
    for (auto &entry : captured)
      captured_.push_back(std::move(entry));
    entries_.swap(next);
    differing_.swap(status);
    head_.swap(revision);
    if (changed)
      change =
          ContextChange{ContextChangeKind::managed, head_, mode, before, input_bytes()};
    trace.state(1, raw_state());
    refresh_history();
    maybe_checkpoint(trace.child());
    return outcome;
  });
  notify(std::move(change));
  return result;
}
Value ContextStore::inspect(const Value &query, RawContextOrigin origin) const {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::inspect, 361, origin, &initial};
  return trace.run([&]() -> Value {
    const auto &kind = string_field(query, "kind");
    trace.label(3, kind);
    if (kind == "history" || kind == "index" ||
        (kind == "originals" && !query.find("entry")))
      refresh_history();
    auto snapshot = head_;
    if (kind == "history")
      visit_context_records(historical_, trace.fresh(), [&](const Value &packet) {
        snapshot = string_field(packet, "revision");
      });
    if (query.find("revision") && string_field(query, "revision") != snapshot)
      throw Error{ErrorCode::conflict};
    const auto requested = index_field(query, "offset", 0);
    const auto limit = index_field(query, "limit", 4096);
    if (limit == 0 || limit > 65536)
      throw Error{ErrorCode::invalid_range};
    const auto format =
        query.find("format") ? string_field(query, "format") : "bbm2-stream";
    if (format != "json" && format != "bbm2-stream")
      throw Error{ErrorCode::invalid_range};
    const bool json_export = format == "json";
    trace.label(4, format);
    trace.event(1826, std::array{detail::raw_native_field(requested),
                                 detail::raw_native_field(limit),
                                 detail::raw_native_field(json_export)});
    auto page_credit = trace.data_credit(48 + limit);
    // Each native packet is framed with an eight-byte little-endian length.
    // Explicit JSON export remains virtual, never materializing aggregate history.
    std::size_t total = 0;
    std::string page;
    auto emit = [&](std::string_view raw) {
      if (raw.size() > std::numeric_limits<std::size_t>::max() - total)
        throw Error{ErrorCode::capacity};
      const auto begin = total;
      total += raw.size();
      if (total > requested && page.size() < limit) {
        const auto from = requested > begin ? requested - begin : 0;
        page.append(raw.substr(from, std::min(limit - page.size(), raw.size() - from)));
      }
    };
    if (!json_export)
      emit(std::string_view{"BBMS\1", 5});
    const auto emit_value = [&](const Value &value) {
      if (json_export) {
        emit(unwrap(dump_json(value)));
        return;
      }
      const auto packet = unwrap(encode_packet_string(value));
      std::array<char, 8> length{};
      auto size = static_cast<std::uint64_t>(packet.size());
      for (unsigned i = 0; i < 8; ++i)
        length[i] = static_cast<char>((size >> (i * 8)) & 255U);
      emit(std::string_view{length.data(), length.size()});
      emit(packet);
    };
    if (kind == "history") {
      if (json_export)
        emit("[");
      bool first = true;
      visit_context_records(historical_, trace.fresh(), [&](const Value &packet) {
        if (json_export && !first)
          emit(",");
        first = false;
        emit_value(packet);
      });
      if (json_export)
        emit("]");
    } else if (kind == "index") {
      if (json_export)
        emit("[");
      std::size_t capture_index = 0;
      visit_context_records(historical_, trace.fresh(), [&](const Value &packet) {
        if (!accepted(packet) || skipped_frame(string_field(packet, "revision")))
          return;
        for (const auto &entry : packet_originals(packet)) {
          if (json_export && capture_index != 0)
            emit(",");
          auto item = Value::object(
              {{"id", field(entry, "id")},
               {"item_bytes",
                number(unwrap(encode_packet(field(entry, "item"))).size())}});
          item.object().emplace_back("capture_index", number(capture_index++));
          item.object().emplace_back("capture_revision",
                                     Value{string_field(entry, "id").substr(0, 32)});
          if (const auto *a = entry.find("ancestry"))
            item.object().emplace_back("ancestry", *a);
          emit_value(item);
        }
      });
      if (json_export)
        emit("]");
    } else if (kind == "originals") {
      if (const auto *id = query.find("entry")) {
        if (!std::holds_alternative<std::string>(id->value()))
          throw Error{ErrorCode::invalid_identity};
        const auto found = original_entry(id->string(), trace.fresh());
        if (!found)
          throw Error{ErrorCode::invalid_identity};
        emit_value(*found);
      } else {
        if (json_export)
          emit("[");
        bool first = true;
        visit_context_records(historical_, trace.fresh(), [&](const Value &packet) {
          if (!accepted(packet) || skipped_frame(string_field(packet, "revision")))
            return;
          for (const auto &entry : packet_originals(packet)) {
            if (json_export && !first)
              emit(",");
            first = false;
            emit_value(entry);
          }
        });
        if (json_export)
          emit("]");
      }
    } else
      throw Error{ErrorCode::invalid_range};
    const auto page_size = page.size();
    trace.data(
        1809,
        std::array{detail::raw_native_field(page_size), detail::raw_string_field(page)},
        page_credit ? &*page_credit : nullptr);
    constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (const auto byte : page) {
      const auto c = static_cast<unsigned char>(byte);
      hex += digits[c >> 4U];
      hex += digits[c & 15U];
    }
    const auto offset = std::min(requested, total);
    auto result = Value::object(
        {{"encoding", Value{json_export ? "hex-json-utf8" : "hex-bbm2-stream"}},
         {"bytes", Value{std::move(hex)}},
         {"total_bytes", number(total)},
         {"offset", number(offset)},
         {"next", number(offset + page.size())},
         {"revision", Value{snapshot}},
         {"context_revision", Value{head_}}});
    const auto snapshot_length = snapshot.size(), head_length = head_.size();
    trace.event(1810, std::array{detail::raw_native_field(offset),
                                 detail::raw_native_field(page_size),
                                 detail::raw_native_field(total),
                                 detail::raw_native_field(snapshot_length),
                                 detail::raw_string_field(snapshot),
                                 detail::raw_native_field(head_length),
                                 detail::raw_string_field(head_)});
    return result;
  });
}

void ContextStore::require_writable() const {
  if (read_only_)
    throw Error{ErrorCode::audit_unavailable};
}
void ContextStore::write_format_level(RawContextTrace &trace) {
  // Written once, before the first frame an older format cannot read.
  if (level_ >= format_level)
    return;
  const auto identity = log_.issue();
  auto publication = trace.publication();
  (void)log_.record(identity, ApplicationChannel::program,
                    Value::object({{"label", Value{"context-format-level"}},
                                   {"level", Value{Number{format_level}}}}),
                    &publication);
  level_ = format_level;
  history_stale_ = true;
}
const Value *ContextStore::restored_section(std::string_view name) const noexcept {
  for (const auto &section : restored_sections_)
    if (const auto *n = section.find("name");
        n && std::holds_alternative<std::string>(n->value()) && n->string() == name)
      return section.find("value");
  return nullptr;
}
Value::Array ContextStore::skipped_originals() const {
  return Value::Array{skipped_originals_.begin(), skipped_originals_.end()};
}
void AuditLog::begin_reserved() { reserved_.emplace(root_); }
bool ContextStore::skipped_frame(std::string_view revision) const noexcept {
  return skipped_.contains(std::string{revision}) ||
         std::find(skipped_revisions_.begin(), skipped_revisions_.end(), revision) !=
             skipped_revisions_.end();
}
std::optional<std::vector<std::byte>>
ContextStore::original_bytes(const std::string &id,
                             std::map<IdentityBytes, Value> &decoded,
                             RawContextOrigin origin) const {
  if (const auto found = originals_.find(id); found != originals_.end()) {
    const auto &held = found->second;
    if (!held.packet)
      return unwrap(held.payload.read()); // the encoded original itself
    if (!held.application)
      throw Error{ErrorCode::corrupt};
    auto at = decoded.find(held.application->bytes());
    if (at == decoded.end())
      at = decoded.emplace(held.application->bytes(), held.packet_value(origin)).first;
    const auto original = packet_original(at->second, held.index, id);
    if (!original)
      throw Error{ErrorCode::corrupt};
    return unwrap(encode_packet(*original));
  }
  const auto original = original_entry(id, origin);
  if (!original)
    return std::nullopt;
  return unwrap(encode_packet(*original));
}
std::set<std::string>
ContextStore::settle(const Value::Array &next, RawContextOrigin origin,
                     const std::map<std::string, Value> *created) const {
  std::map<std::string_view, const Value *> prior;
  for (const auto &entry : entries_)
    prior.emplace(string_field(entry, "id"), &entry);
  std::set<std::string> status;
  std::map<IdentityBytes, Value> decoded;
  for (const auto &entry : next) {
    const auto &id = string_field(entry, "id");
    if (const auto found = prior.find(id); found != prior.end()) {
      const auto &previous = *found->second;
      // Shared storage is the same encoding; unequal values never encode alike;
      // equal values may still encode differently (a number's representation).
      if (previous.same(entry) ||
          (previous == entry &&
           unwrap(encode_packet(previous)) == unwrap(encode_packet(entry)))) {
        if (differing_.contains(id))
          status.insert(id);
        continue;
      }
    }
    if (created && created->contains(id))
      continue;
    const auto original = original_bytes(id, decoded, origin);
    if (!original || *original != unwrap(encode_packet(entry)))
      status.insert(id);
  }
  return status;
}
Value ContextStore::cut(CutFrame frame, RawContextOrigin origin) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::cut, 1809, origin, &initial};
  return trace.run([&]() -> Value {
    require_writable();
    // The frame and the next entries are built from values this call owns: a
    // maintenance checkpoint inside the record call may prune originals_ and
    // clear captured_ (design §4.3).
    const auto identity = log_.issue();
    frame.op = CutOp::cut;
    frame.accepted = true;
    frame.reason.clear();
    frame.revision = hex_identity(identity.bytes());
    frame.base = head_;
    frame.observed = head_;
    frame.result.reset();
    std::map<std::string, Value> fresh;
    std::map<std::string, Original> originals;
    std::vector<Original> captured;
    for (std::size_t i = 0; i < frame.originals.size(); ++i) {
      const auto id = frame.revision + "." + std::to_string(i);
      if (originals_.contains(id))
        throw Error{ErrorCode::conflict};
      auto entry =
          Value::object({{"id", Value{id}}, {"item", frame.originals[i].item}});
      originals.emplace(id, Original{entry, identity});
      captured.emplace_back(entry, identity);
      fresh.emplace(id, std::move(entry));
    }
    auto next = apply_cut(frame, entries_, fresh, trace.fresh());
    frame.result = measure_entries(next, true);
    const auto packet = recordable(frame);
    auto status = settle(next, trace.fresh(), &fresh);
    ClassMap made;
    made.set(identity.bytes(), derive_classes(packet));
    reserve_append(captured_, captured.size());
    if (protect)
      trace.protection([&](const RawEventRef &request) {
        protect(next, pending_proposal(trace.child()), request);
      });
    write_format_level(trace);
    auto publication = trace.publication();
    const auto reference =
        log_.record(identity, ApplicationChannel::context, packet, &publication);
    history_stale_ = true;
    for (auto &original : captured) {
      original.record = reference;
      captured_.push_back(std::move(original));
    }
    for (auto &[id, original] : originals)
      original.record = reference;
    originals_.merge(originals);
    classes_.merge(made);
    entries_.swap(next);
    differing_.swap(status);
    head_ = frame.revision;
    trace.state(1, raw_state());
    refresh_history();
    maybe_checkpoint(trace.child());
    return Value::object({{"accepted", Value{true}}, {"revision", Value{head_}}});
  });
}
Value ContextStore::repair_preview() const {
  if (!read_only_)
    return Value{};
  Value::Array skipped, offered;
  for (const auto &revision : skipped_revisions_)
    skipped.push_back(Value{revision});
  for (const auto &original : skipped_originals_) {
    // Whether this pick alone adds no pairing fault to the head (a call left
    // unanswered before the bad frame is the head's, not the pick's).
    auto alone = entries_;
    alone.push_back(original);
    offered.push_back(
        Value::object({{"id", field(original, "id")},
                       {"item", field(original, "item")},
                       {"pairs", Value{adds_no_pairing_fault(entries_, alone)}}}));
  }
  Value::Array head_faults;
  for (const auto &fault : pairing_faults(entries_))
    head_faults.push_back(Value{fault});
  return Value::object(
      {{"head_faults", Value{std::move(head_faults)}},
       {"bad_revision", Value{report_.bad_revision}},
       {"bad_error", Value{std::string{error_name(report_.bad_error.code)}}},
       {"bad_detail", Value{Number{report_.bad_error.detail}}},
       {"skipped", Value{std::move(skipped)}},
       {"originals", Value{std::move(offered)}}});
}
Value ContextStore::repair(const Value::Array &picks, RawContextOrigin origin) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::repair, 1809, origin, &initial};
  return trace.run([&]() -> Value {
    if (!read_only_ || skipped_revisions_.empty())
      throw Error{ErrorCode::conflict};
    // Bounded writes (one level record, one frame) that must be possible at the
    // resident-tail cap, which the read-only state cannot clear by a snapshot.
    std::optional<RetainedState::MaintenanceScope> reserved;
    reserved.emplace(log_.root());
    const auto identity = log_.issue();
    CutFrame frame;
    frame.op = CutOp::repair;
    frame.revision = hex_identity(identity.bytes());
    frame.base = head_;
    frame.observed = head_;
    frame.skipped = skipped_revisions_;
    frame.cause.source = CutSource::operator_;
    // Copies of the picked originals, appended in the order picked.
    std::map<std::string, Value> fresh;
    std::map<std::string, Original> originals;
    std::vector<Original> captured;
    std::optional<ContextRef> anchor;
    if (!entries_.empty()) {
      anchor = parse_entry_id(string_field(entries_.back(), "id"));
      if (!anchor)
        throw Error{ErrorCode::corrupt};
    }
    std::set<std::string> chosen;
    for (const auto &pick : picks) {
      const auto *wanted = std::get_if<std::string>(&pick.value());
      if (!wanted || !chosen.insert(*wanted).second)
        throw Error{ErrorCode::invalid_identity};
      const auto found = std::find_if(
          skipped_originals_.begin(), skipped_originals_.end(),
          [&](const Value &o) { return string_field(o, "id") == *wanted; });
      if (found == skipped_originals_.end())
        throw Error{ErrorCode::invalid_identity};
      const auto index = static_cast<std::uint32_t>(frame.originals.size());
      frame.originals.push_back({field(*found, "item"), 0});
      frame.insert.push_back({anchor, index});
      const auto id = frame.revision + "." + std::to_string(index);
      auto entry = Value::object({{"id", Value{id}}, {"item", field(*found, "item")}});
      originals.emplace(id, Original{entry, identity});
      captured.emplace_back(entry, identity);
      fresh.emplace(id, std::move(entry));
      anchor = ContextRef{identity.bytes(), index};
    }
    auto next = apply_cut(frame, entries_, fresh, trace.fresh());
    // The pairing rule: the repair may not create an unpaired call or output the
    // head before the bad frame did not already have. A call that head left
    // unanswered (valid as written) is kept as is and does not block the repair.
    if (!adds_no_pairing_fault(entries_, next))
      throw Error{ErrorCode::conflict, repair_unpaired};
    frame.result = measure_entries(next, true);
    const auto packet = recordable(frame);
    auto status = settle(next, trace.fresh(), &fresh);
    // The copies are classed by role (class byte 0), as an unreadable record is.
    ClassMap made;
    made.set(identity.bytes(), derive_classes(packet));
    reserve_append(captured_, captured.size());
    write_format_level(trace);
    auto publication = trace.publication();
    const auto reference =
        log_.record(identity, ApplicationChannel::context, packet, &publication);
    reserved.reset();
    history_stale_ = true;
    for (auto &original : captured) {
      original.record = reference;
      captured_.push_back(std::move(original));
    }
    for (auto &[id, original] : originals)
      original.record = reference;
    originals_.merge(originals);
    classes_.merge(made);
    entries_.swap(next);
    differing_.swap(status);
    head_ = frame.revision;
    // Playback continues from the repair frame; snapshots resume.
    skipped_.insert(skipped_revisions_.begin(), skipped_revisions_.end());
    read_only_ = false;
    skipped_revisions_.clear();
    skipped_originals_.clear();
    trace.state(1, raw_state());
    refresh_history();
    maybe_checkpoint(trace.child());
    return Value::object({{"accepted", Value{true}},
                          {"revision", Value{head_}},
                          {"skipped", Value{Number{frame.skipped.size()}}}});
  });
}
std::optional<Value> ContextStore::creating_record(const IdentityBytes &record,
                                                   RawContextOrigin origin,
                                                   bool skipped) const {
  const auto revision = hex_identity(record);
  // A frame a repair skipped (or one set aside read-only) created nothing.
  if (!skipped && skipped_frame(revision))
    return std::nullopt;
  try {
    // Playback and restore hold the creating record itself.
    const auto prefix = revision + ".";
    if (const auto held = originals_.lower_bound(prefix);
        held != originals_.end() && held->first.starts_with(prefix) &&
        held->second.packet) {
      const auto &original = held->second;
      auto packet = class_projection(
          raw_context_read(original.payload,
                           {0, true, original.record, original.application,
                            ApplicationChannel::context, original.index},
                           origin));
      return accepted(packet) ? std::optional{std::move(packet)} : std::nullopt;
    }
    const std::uint8_t family = 1;
    const auto kind = RetainedKind::application;
    const auto cursor = log_.root().cursor();
    RawContextPair lookup{origin,
                          158,
                          {1819, 1820},
                          std::array{detail::raw_native_field(family),
                                     detail::raw_native_field(kind), ByteView{record},
                                     ByteView{cursor.journal.bytes()},
                                     detail::raw_native_field(cursor.sequence),
                                     detail::raw_native_field(cursor.end_offset)}};
    const auto fact = lookup.run([&] {
      return log_.root().lookup(log_.root().visible(), family, kind, record);
    });
    if (!fact) {
      lookup.finish(2);
      return std::nullopt;
    }
    const auto *application = std::get_if<ApplicationRecordEvent>(&fact->event.body);
    if (application)
      lookup.finish(0, std::array{ByteView{fact->record.journal.bytes()},
                                  detail::raw_native_field(fact->record.sequence),
                                  detail::raw_native_field(application->channel)});
    else
      lookup.finish(3, std::array{ByteView{fact->record.journal.bytes()},
                                  detail::raw_native_field(fact->record.sequence)});
    if (!application || application->identity.bytes() != record ||
        application->channel != ApplicationChannel::context)
      return std::nullopt;
    auto packet = class_projection(raw_context_read(
        application->payload,
        {2, true, fact->record, application->identity, application->channel, 0},
        origin));
    return accepted(packet) ? std::optional{std::move(packet)} : std::nullopt;
  } catch (const Error &error) {
    if (!fault_error(error))
      throw;
    return std::nullopt;
  } catch (const std::bad_variant_access &) {
    return std::nullopt;
  }
}
namespace {
// The legacy role rule over the live originals or entries of one record that
// cannot be read: ordinals with no live entry count as results.
RecordClasses role_classes(const std::map<std::uint32_t, Value> &live) {
  if (live.empty())
    return {};
  Value::Array items(static_cast<std::size_t>(live.rbegin()->first) + 1,
                     Value::object({}));
  for (const auto &[ordinal, item] : live)
    items[ordinal] = item;
  return record_classes({UnitClass::by_role}, items);
}
} // namespace
const RecordClasses &ContextStore::record_classes_of(const IdentityBytes &record,
                                                     const Value::Array &entries,
                                                     RawContextOrigin origin) const {
  if (const auto *found = classes_.find(record))
    return *found;
  if (!unreadable_.contains(record)) {
    if (const auto packet = creating_record(record, origin)) {
      try {
        classes_.set(record, derive_classes(*packet));
        return *classes_.find(record);
      } catch (const Error &error) {
        if (!fault_error(error))
          throw;
      }
    }
    unreadable_.insert(record);
  }
  // Classed by role from the live entries and never saved, so every path (this
  // open, a restore, a v1 restore) gives the same classes for the same entries.
  // Within one pass over one entry list each record is classed once.
  // The entries are grouped by record once per pass, on the first such record.
  if (by_role_source_ != &entries) {
    by_role_.clear();
    by_role_live_.clear();
    for (const auto &entry : entries)
      if (const auto ref = parse_entry_id(string_field(entry, "id")))
        by_role_live_[ref->record].emplace(ref->ordinal, field(entry, "item"));
    by_role_source_ = &entries;
  }
  if (const auto found = by_role_.find(record); found != by_role_.end())
    return found->second;
  const auto live = by_role_live_.find(record);
  return by_role_
      .emplace(record, live == by_role_live_.end() ? RecordClasses{}
                                                   : role_classes(live->second))
      .first->second;
}
void ContextStore::derive_live_classes(RawContextOrigin origin) {
  // The same path a later units() takes: a record that cannot be read is classed by
  // role from the live entries and not saved.
  begin_class_pass();
  std::set<IdentityBytes> done;
  for (const auto &entry : entries_)
    if (const auto ref = parse_entry_id(string_field(entry, "id"));
        ref && done.insert(ref->record).second)
      (void)record_classes_of(ref->record, entries_, origin);
}
std::vector<Unit> ContextStore::live_units(RawContextOrigin origin) const {
  begin_class_pass();
  return blackbird::units(
      entries_,
      [&](const IdentityBytes &record) -> const RecordClasses & {
        return record_classes_of(record, entries_, origin);
      },
      pins_,
      [&](std::size_t position) {
        const auto &entry = entries_[position];
        const auto &item = field(entry, "item");
        const auto &id = string_field(entry, "id");
        if (const auto found = prints_.find(id);
            found != prints_.end() && found->second.item.same(item))
          return found->second.print;
        const auto print = item_fingerprint(item);
        prints_.insert_or_assign(id, Print{item, print});
        return print;
      });
}
std::vector<Unit> ContextStore::units(RawContextOrigin origin) const {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::units, 209, origin, &initial};
  return trace.run([&] {
    auto result = live_units(trace.fresh());
    trace.rows(5, result.size());
    return result;
  });
}
namespace {
// The value with object keys in sorted order at every depth (a repeated key keeps
// its order); a subtree already in order is shared, not copied.
Value sorted_keys(const Value &value) {
  if (const auto *array = std::get_if<Value::Array>(&value.value())) {
    std::optional<Value::Array> changed;
    for (std::size_t i = 0; i < array->size(); ++i) {
      auto child = sorted_keys((*array)[i]);
      if (child.same((*array)[i]))
        continue;
      if (!changed)
        changed = *array;
      (*changed)[i] = std::move(child);
    }
    return changed ? Value{std::move(*changed)} : value;
  }
  const auto *object = std::get_if<Value::Object>(&value.value());
  if (object == nullptr)
    return value;
  std::optional<Value::Object> changed;
  for (std::size_t i = 0; i < object->size(); ++i) {
    auto child = sorted_keys((*object)[i].second);
    if (child.same((*object)[i].second))
      continue;
    if (!changed)
      changed = *object;
    (*changed)[i].second = std::move(child);
  }
  const auto before = [](const auto &a, const auto &b) { return a.first < b.first; };
  const auto &fields = changed ? *changed : *object;
  if (std::is_sorted(fields.begin(), fields.end(), before))
    return changed ? Value::object(std::move(*changed)) : value;
  // Sorted by key, then by position, so a repeated key keeps its order; through an
  // index, without the temporary buffer std::stable_sort allocates.
  std::vector<std::size_t> order(fields.size());
  for (std::size_t i = 0; i < order.size(); ++i)
    order[i] = i;
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return fields[a].first != fields[b].first ? fields[a].first < fields[b].first
                                              : a < b;
  });
  Value::Object sorted;
  sorted.reserve(fields.size());
  for (const auto i : order)
    sorted.push_back(fields[i]);
  return Value::object(std::move(sorted));
}
} // namespace
bool ContextStore::live_instruction(const Value &item, RawContextOrigin origin) const {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::instructions, 261, origin, &initial};
  return trace.run([&] {
    // Equal items, whatever the order of object keys at any depth: both sides are
    // compared with keys sorted. This is the query's contract only; superseded
    // copies and originals compare exact bytes. Prints find the candidates; only a
    // candidate is encoded and compared whole.
    const auto canonical = sorted_keys(item);
    const auto print = item_fingerprint(canonical);
    trace.event(1828, std::array{detail::raw_native_field(print.first),
                                 detail::raw_native_field(print.second)});
    std::optional<std::string> bytes;
    begin_class_pass();
    for (const auto &entry : entries_) {
      const auto &id = string_field(entry, "id");
      const auto ref = parse_entry_id(id);
      if (!ref)
        throw Error{ErrorCode::corrupt};
      // The class of the entry, from its creating record. An instruction is always a
      // unit of its own (outputs join only rounds), so no units are built.
      if (record_classes_of(ref->record, entries_, trace.fresh()).at(ref->ordinal) !=
          UnitClass::instruction)
        continue;
      const auto &live = field(entry, "item");
      auto found = sorted_prints_.find(id);
      if (found == sorted_prints_.end() || !found->second.item.same(live))
        found =
            sorted_prints_
                .insert_or_assign(id, Print{live, item_fingerprint(sorted_keys(live))})
                .first;
      if (found->second.print != print)
        continue;
      if (!bytes)
        bytes = unwrap(encode_packet_string(canonical));
      if (unwrap(encode_packet_string(sorted_keys(live))) == *bytes) {
        trace.rows(7, 1);
        return true;
      }
    }
    trace.rows(7, 0);
    return false;
  });
}
bool ContextStore::live_origin(std::string_view label, RawContextOrigin origin) const {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::origins, 209, origin, &initial};
  return trace.run([&] {
    trace.label(0, label);
    const auto labelled = [&](const Value &packet) {
      const auto *found = packet.find("origin");
      return found && std::holds_alternative<std::string>(found->value()) &&
             found->string() == label;
    };
    std::set<IdentityBytes> seen;
    for (const auto &entry : entries_) {
      const auto ref = parse_entry_id(string_field(entry, "id"));
      if (!ref || !seen.insert(ref->record).second)
        continue;
      const auto packet = creating_record(ref->record, trace.fresh());
      if (!packet)
        continue;
      if (labelled(*packet)) {
        trace.rows(6, 1);
        return true;
      }
      // A repair copy carries no origin: it counts when its item equals an original
      // of a skipped frame with this label, the frame the operator copied it from.
      const auto *op = packet->find("op");
      const auto *skipped = packet->find("skipped");
      if (!op || *op != Value{"repair"} || !skipped ||
          !std::holds_alternative<Value::Array>(skipped->value()))
        continue;
      std::set<std::string> copies;
      for (const auto &live : entries_)
        if (const auto at = parse_entry_id(string_field(live, "id"));
            at && at->record == ref->record)
          copies.insert(unwrap(encode_packet_string(field(live, "item"))));
      for (const auto &revision : skipped->array()) {
        const auto source = std::get_if<std::string>(&revision.value());
        const auto source_ref = source ? parse_entry_id(*source + ".0") : std::nullopt;
        if (!source_ref)
          continue;
        const auto original = creating_record(source_ref->record, trace.fresh(), true);
        if (!original || !labelled(*original))
          continue;
        // A skipped frame is one that did not apply: one whose originals do not
        // parse simply does not match, as creating_record treats a record that does
        // not decode.
        try {
          for (const auto &item : packet_originals(*original))
            if (copies.contains(unwrap(encode_packet_string(field(item, "item"))))) {
              trace.rows(6, 1);
              return true;
            }
        } catch (const Error &error) {
          if (!fault_error(error))
            throw;
        } catch (const std::bad_variant_access &) {
        }
      }
    }
    trace.rows(6, 0);
    return false;
  });
}
Value ContextStore::pin(std::string_view selection, RawContextOrigin origin) {
  return write_pins(selection, true, origin);
}
Value ContextStore::unpin(std::string_view selection, RawContextOrigin origin) {
  return write_pins(selection, false, origin);
}
Value ContextStore::write_pins(std::string_view selection, bool pin,
                               RawContextOrigin origin) {
  const auto initial = raw_state();
  RawContextTrace trace{RawContextAction::pins, 1228, origin, &initial};
  return trace.run([&]() -> Value {
    require_writable();
    trace.label(6, selection);
    const auto parsed = parse_selection(selection);
    if (const auto *error = std::get_if<UnitSelectionError>(&parsed))
      throw Error{ErrorCode::invalid_range, static_cast<std::int64_t>(error->at)};
    const auto units = live_units(trace.fresh());
    const auto expansion = expand(std::get<UnitSelection>(parsed), units, entries_,
                                  pin ? ExpandPurpose::pin : ExpandPurpose::unpin);
    // The record lists live pinned units only; a pin on a unit no longer live lapses.
    std::set<ContextRef> live, next;
    for (const auto &unit : units)
      live.insert(unit.key);
    std::copy_if(pins_.begin(), pins_.end(), std::inserter(next, next.end()),
                 [&](const ContextRef &ref) { return live.contains(ref); });
    Value::Array accepted;
    for (const auto index : expansion.units) {
      if (pin)
        next.insert(units[index].key);
      else
        next.erase(units[index].key);
      accepted.push_back(Value{units[index].handle});
    }
    if (next != pins_) {
      Value::Array refs;
      for (const auto &ref : next)
        refs.push_back(encode_ref(ref));
      const auto identity = log_.issue();
      auto publication = trace.publication();
      (void)log_.record(identity, ApplicationChannel::program,
                        Value::object({{"label", Value{"context-pins-v1"}},
                                       {"units", Value{std::move(refs)}}}),
                        &publication);
      history_stale_ = true;
      pins_.swap(next);
      trace.state(1, raw_state());
    }
    Value::Array refused;
    for (const auto &one : expansion.refused)
      refused.push_back(
          Value::object({{"unit", Value{one.unit}}, {"reason", Value{one.reason}}}));
    return Value::object({{"accepted", Value{std::move(accepted)}},
                          {"refused", Value{std::move(refused)}},
                          {"pinned", Value{Number{pins_.size()}}}});
  });
}
// A frame as it will be recorded, checked to decode back to itself: the writer
// never records what playback would read differently.
Value ContextStore::recordable(const CutFrame &frame) {
  auto packet = encode_cut_frame(frame);
  if (!(encode_cut_frame(decode_cut_frame(packet)) == packet))
    throw Error{ErrorCode::corrupt};
  return packet;
}

} // namespace blackbird
