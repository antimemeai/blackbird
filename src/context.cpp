#include "blackbird/context.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"
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
} // namespace
std::string AuditLog::record(ApplicationChannel channel, const Value &packet) {
  auto identity = issue();
  record(identity, channel, packet);
  return hex_identity(identity.bytes());
}
void AuditLog::record(ApplicationRecordId identity, ApplicationChannel channel,
                      const Value &packet) {
  const ImmutableBytes serialized{unwrap(encode_packet(packet))};
  (void)unwrap(
      root_.submit({{}, ApplicationRecordEvent{identity, channel, serialized}}));
}
void AuditLog::record_boundary(ApplicationRecordId identity,
                               const Value &context_packet,
                               const Value &program_packet) {
  if (program_packet == Value{}) {
    record(identity, ApplicationChannel::context, context_packet);
    return;
  }
  const auto program_identity = issue();
  const std::array<RetainedEvent, 2> events{
      RetainedEvent{{},
                    ApplicationRecordEvent{identity, ApplicationChannel::context,
                                           unwrap(encode_packet(context_packet))}},
      RetainedEvent{{},
                    ApplicationRecordEvent{program_identity,
                                           ApplicationChannel::program,
                                           unwrap(encode_packet(program_packet))}}};
  (void)unwrap(root_.append(root_.cursor(), {}, events));
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
  const auto label = capture.label;
  const auto raw = capture.bytes;
  auto metadata = std::move(capture.metadata);
  auto identity = issue();
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
    return hex_identity(identity.bytes());
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
  return hex_identity(identity.bytes());
}
bool ContextStore::valid_entries(const Value &entries) const {
  const auto *array = std::get_if<Value::Array>(&entries.value());
  if (array == nullptr)
    return false;
  std::set<std::string> ids;
  for (const auto &entry : *array) {
    const auto *id = entry.find("id");
    const auto *item = entry.find("item");
    if (id == nullptr || item == nullptr ||
        !std::holds_alternative<std::string>(id->value()) ||
        !std::holds_alternative<Value::Object>(item->value()))
      return false;
    if ((!originals_.contains(id->string()) && !original_entry(id->string())) ||
        !ids.insert(id->string()).second)
      return false;
  }
  return true;
}
std::string read_text(const ImmutableBytes &bytes) {
  const auto owned = unwrap(bytes.read());
  return std::string(reinterpret_cast<const char *>(owned.data()), owned.size());
}
ContextStore::Original::Original(const Value &entry)
    : payload(unwrap(encode_packet(entry))), id(string_field(entry, "id")) {}
Value ContextStore::Original::entry() const {
  const auto decoded = unwrap(read_packet(payload));
  if (!packet)
    return decoded;
  const auto &fresh = field(decoded, "originals").array();
  if (index >= fresh.size() || string_field(fresh[index], "id") != id)
    throw Error{ErrorCode::corrupt};
  return fresh[index];
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
void visit_context_records(const RetainedState::FactHistory &history, Visitor visitor) {
  // Explicit history is fallible, one bounded page at a time. No retained
  // metadata vector or historical keydir is installed by this traversal.
  const auto count = history.count;
  for (std::size_t first = 0; first < count;) {
    auto page = unwrap(history.page(first, 16));
    if (page.empty())
      throw Error{ErrorCode::corrupt};
    first += page.size();
    for (const auto &fact : page)
      if (const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
          record && record->channel == ApplicationChannel::context)
        visitor(unwrap(read_packet(record->payload)));
  }
}
} // namespace
Value::Array ContextStore::captured_entries() const {
  Value::Array result;
  visit_context_records(historical_, [&](const Value &packet) {
    if (!accepted(packet))
      return;
    if (const auto *fresh = packet.find("originals"))
      for (const auto &entry : fresh->array())
        result.push_back(entry);
  });
  return result;
}
ContextStore::ContextStore(AuditLog &log) : ContextStore(log, true) {}
ContextStore::ContextStore(AuditLog &log, bool restore_saved) : log_(log) {
  std::uint64_t boundary = 0;
  if (restore_saved)
    if (const auto *saved = log.root().saved_state()) {
      try {
        if (field(saved->context, "version").number().text() != "1")
          throw Error{ErrorCode::corrupt};
        head_ = string_field(saved->context, "base");
        if (head_.size() != 32)
          throw Error{ErrorCode::corrupt};
        entries_ = field(saved->context, "entries").array();
        for (const auto &entry : field(saved->context, "live_originals").array()) {
          if (!originals_.emplace(string_field(entry, "id"), Original{entry}).second)
            throw Error{ErrorCode::corrupt};
        }
        if (!valid_entries(Value{entries_}))
          throw Error{ErrorCode::corrupt};
        boundary = saved->boundary.sequence;
        archive_loaded_ = false;
      } catch (const Error &) {
        head_.clear();
        entries_.clear();
        originals_.clear();
      } catch (const std::bad_variant_access &) {
        head_.clear();
        entries_.clear();
        originals_.clear();
      }
    }
  for (const auto &fact : log.root().tail_facts()) {
    if (boundary && fact.record.journal == log.root().cursor().journal &&
        fact.record.sequence <= boundary)
      continue;
    const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
    if (record == nullptr || record->channel != ApplicationChannel::context)
      continue;
    constexpr std::array<std::string_view, 1> omitted{"candidate"};
    const auto packet =
        unwrap(decode_packet_projection(unwrap(record->payload.read()), omitted));
    const auto revision = hex_identity(record->identity.bytes());
    if (string_field(packet, "revision") != revision ||
        string_field(packet, "observed") != head_)
      throw Error{ErrorCode::corrupt};
    const auto op = string_field(packet, "op");
    if (op != "append" && op != "edit" && op != "managed")
      throw Error{ErrorCode::corrupt};
    const bool publishes = accepted(packet);
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
          Original original{record->payload, ordinal++, id};
          captured_.push_back(original);
          if (original_entry(id) || !originals_.emplace(id, std::move(original)).second)
            throw Error{ErrorCode::corrupt};
        }
      }
      if (const auto *format = packet.find("format")) {
        if (op != "append" || *format != Value{"append-delta-v1"} ||
            packet.find("entries"))
          throw Error{ErrorCode::corrupt};
        auto next = entries_;
        const auto &fresh = field(packet, "originals").array();
        next.insert(next.end(), fresh.begin(), fresh.end());
        if (!valid_entries(Value{next}))
          throw Error{ErrorCode::corrupt};
        entries_.swap(next);
      } else {
        const auto &entries = field(packet, "entries");
        if (!valid_entries(entries))
          throw Error{ErrorCode::corrupt};
        entries_ = entries.array();
      }
      head_ = revision;
    }
  }
  if (restore_saved)
    maybe_checkpoint();
  historical_ = log_.root().history_snapshot();
  root_lifetime_ = log_.root().lifetime();
  log_.root().set_maintenance_checkpoint([this] { return checkpoint(); });
}
ContextStore::~ContextStore() {
  if (!root_lifetime_.expired())
    log_.root().set_maintenance_checkpoint({});
}
std::optional<Value> ContextStore::original_entry(std::string_view id) const {
  if (const auto found = originals_.find(std::string{id}); found != originals_.end())
    return found->second.entry();
  if (archive_loaded_ || !log_.root().saved_state())
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
  const auto parsed = std::from_chars(id.data() + 33, id.data() + id.size(), ordinal);
  if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size() ||
      std::to_string(ordinal) != id.substr(33))
    return std::nullopt;
  const auto identity = ApplicationRecordId::from_bytes(bytes);
  if (!identity.has_value())
    return std::nullopt;
  const auto fact =
      log_.root().lookup(log_.root().visible(), 1, RetainedKind::application, bytes);
  if (!fact || fact->record.journal != log_.root().saved_state()->boundary.journal ||
      fact->record.sequence > log_.root().saved_state()->boundary.sequence)
    return std::nullopt;
  const auto *record = std::get_if<ApplicationRecordEvent>(&fact->event.body);
  if (!record || record->identity != identity.value() ||
      record->channel != ApplicationChannel::context)
    return std::nullopt;
  const auto packet = unwrap(read_packet(record->payload));
  if (!accepted(packet))
    return std::nullopt;
  const auto *fresh = packet.find("originals");
  if (!fresh || ordinal >= fresh->array().size())
    return std::nullopt;
  const auto &entry = fresh->array()[ordinal];
  if (string_field(entry, "id") != id)
    throw Error{ErrorCode::corrupt};
  return entry;
}
Result<void> ContextStore::checkpoint() {
  try {
    Value::Array live;
    for (const auto &entry : entries_) {
      const auto found = original_entry(string_field(entry, "id"));
      if (!found)
        return Result<void>::failure({ErrorCode::corrupt});
      live.push_back(*found);
    }
    auto result = log_.root().save_current_state(
        Value::object({{"version", Value{Number{"1"}}},
                       {"base", Value{head_}},
                       {"entries", Value{entries_}},
                       {"live_originals", Value{std::move(live)}}}));
    if (result.has_value()) {
      historical_ = log_.root().history_snapshot();
      // Published originals are now addressable through the owned archive.
      // Historical inspection uses the pinned reader; keep no resident keydir.
      std::set<std::string> live_ids;
      for (const auto &entry : entries_)
        live_ids.insert(string_field(entry, "id"));
      std::erase_if(originals_,
                    [&](const auto &pair) { return !live_ids.contains(pair.first); });
      captured_.clear();
      archive_loaded_ = false;
    }
    checkpoint_error_ =
        result.has_value() ? std::nullopt : std::optional<Error>{result.error()};
    return result;
  } catch (const Error &error) {
    checkpoint_error_ = error;
    return Result<void>::failure(error);
  } catch (const std::bad_alloc &) {
    checkpoint_error_ = Error{ErrorCode::allocation};
    return Result<void>::failure(*checkpoint_error_);
  }
}
void ContextStore::maybe_checkpoint() {
  const auto *saved = log_.root().saved_state();
  const auto cursor = log_.root().cursor();
  if (!head_.empty() &&
      (!saved || cursor.sequence - saved->boundary.sequence >= 64 ||
       cursor.end_offset - saved->boundary.end_offset >= 4 * 1024 * 1024))
    (void)checkpoint(); // derived accelerator failure never undoes an audit commit
}
Value ContextStore::view() const {
  return Value::object({{"base", Value{head_}}, {"entries", Value{entries_}}});
}
Value ContextStore::stats() const {
  auto number = [](std::size_t n) { return Value{Number{n}}; };
  Value::Array sizes;
  for (const auto &entry : entries_)
    sizes.push_back(Value::object(
        {{"id", field(entry, "id")},
         {"item_bytes",
          number(unwrap(encode_packet_string(field(entry, "item"))).size())}}));
  return Value::object(
      {{"revision", Value{head_}},
       {"entry_count", number(entries_.size())},
       {"input_bytes", number(unwrap(encode_packet_string(Value{items()})).size())},
       {"view_bytes", number(unwrap(encode_packet_string(view())).size())},
       {"entries", Value{std::move(sizes)}},
       {"units", Value{"native binary packet bytes; not tokens"}}});
}
Value::Array ContextStore::items() const {
  Value::Array items;
  items.reserve(entries_.size());
  for (const auto &entry : entries_)
    items.push_back(field(entry, "item"));
  return items;
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
void ContextStore::validate_successor_seed(const Value &seed) {
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
      // store:false requires self-contained encrypted reasoning, not an old ID.
      if (string_field(item, "encrypted_content").empty())
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
}
void ContextStore::seed_successor(const Value &seed) {
  validate_successor_seed(seed);
  if (workflow_ || pending_ || !entries_.empty() || log_.root().fact_count() != 0 ||
      log_.root().state() != JournalWriterState::live ||
      string_field(field(seed, "source"), "journal") ==
          hex_identity(log_.root().cursor().journal.bytes()))
    throw Error{ErrorCode::conflict};
  Value::Array items, locators;
  for (const auto &entry : field(seed, "entries").array()) {
    items.push_back(field(entry, "item"));
    locators.push_back(Value{string_field(entry, "id")});
  }
  const auto lineage = Value::object(
      {{"version", Value{Number{"1"}}},
       {"scope",
        Value{"declared source; no inherited admissions or settlement verification"}},
       {"source", field(seed, "source")},
       {"source_entries", Value{std::move(locators)}}});
  append_impl(std::move(items), "session.successor", &lineage);
}
void ContextStore::append(Value::Array items, std::string_view origin) {
  append_impl(std::move(items), origin, nullptr);
}
void ContextStore::append_impl(Value::Array items, std::string_view origin,
                               const Value *lineage) {
  for (const auto &item : items)
    if (!std::holds_alternative<Value::Object>(item.value()))
      throw Error{ErrorCode::corrupt};
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
    originals.emplace(id, Original{entry});
    fresh.push_back(entry);
    next.push_back(std::move(entry));
  }
  auto packet = Value::object({{"op", Value{"append"}},
                               {"base", Value{head_}},
                               {"observed", Value{head_}},
                               {"revision", Value{revision}},
                               {"accepted", Value{true}},
                               {"origin", Value{std::string{origin}}},
                               {"format", Value{"append-delta-v1"}},
                               {"originals", Value{std::move(fresh)}}});
  if (lineage)
    packet.object().emplace_back("lineage", *lineage);
  std::vector<Original> captured;
  for (const auto &entry : field(packet, "originals").array())
    captured.push_back(entry);
  reserve_append(captured_, captured.size());
  auto expected = revision;
  if (protect)
    protect(next, pending_proposal());
  log_.record(identity, ApplicationChannel::context, packet);
  historical_ = log_.root().history_snapshot();
  for (auto &entry : captured)
    captured_.push_back(std::move(entry));
  if (pending_ && pending_->expected == head_)
    pending_->expected.swap(expected);
  entries_.swap(next);
  originals_.merge(originals);
  head_.swap(revision);
  maybe_checkpoint();
}
Value ContextStore::edit(const Value &candidate) {
  const auto *base = candidate.find("base");
  const auto *entries = candidate.find("entries");
  const bool representation = base != nullptr &&
                              std::holds_alternative<std::string>(base->value()) &&
                              entries != nullptr && valid_entries(*entries);
  const bool publishes = representation && base->string() == head_;
  auto identity = log_.issue();
  auto revision = hex_identity(identity.bytes());
  Value::Array next;
  if (publishes)
    next = entries->array();
  auto outcome =
      Value::object({{"accepted", Value{publishes}},
                     {"revision", Value{revision}},
                     {"current", Value{publishes ? revision : head_}},
                     {"reason", Value{publishes        ? "published"
                                      : representation ? "stale-base"
                                                       : "invalid-representation"}}});
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
    protect(publishes ? next : entries_, pending_proposal());
  log_.record(identity, ApplicationChannel::context, packet);
  historical_ = log_.root().history_snapshot();
  if (publishes) {
    entries_.swap(next);
    head_.swap(revision);
  }
  maybe_checkpoint();
  return outcome;
}
void ContextStore::restore(std::string_view entry) {
  const auto original = original_entry(entry);
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
  (void)edit(
      Value::object({{"base", Value{head_}}, {"entries", Value{std::move(next)}}}));
}
Value ContextStore::originals() const {
  auto out = captured_entries();
  std::sort(out.begin(), out.end(), [](const Value &a, const Value &b) {
    return string_field(a, "id") < string_field(b, "id");
  });
  for (auto &entry : out)
    entry = Value::object({{"id", field(entry, "id")}, {"item", field(entry, "item")}});
  return Value{std::move(out)};
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
bool whole_groups(const Value::Array &entries, const std::set<std::string> &ids) {
  std::set<std::string> pending;
  std::size_t start = 0;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto &item = field(entries[i], "item");
    const auto *type = item.find("type");
    if (!type || !std::holds_alternative<std::string>(type->value()))
      continue;
    if (type->string() == "function_call") {
      if (pending.empty())
        start = i;
      pending.insert(string_field(item, "call_id"));
    } else if (type->string() == "function_call_output") {
      if (pending.erase(string_field(item, "call_id")) != 1)
        return false;
      if (pending.empty()) {
        const bool selected = ids.contains(string_field(entries[start], "id"));
        for (std::size_t j = start; j <= i; ++j)
          if (ids.contains(string_field(entries[j], "id")) != selected)
            return false;
      }
    }
  }
  // Open current tool batches are allowed only if untouched.
  if (!pending.empty())
    for (std::size_t j = start; j < entries.size(); ++j)
      if (ids.contains(string_field(entries[j], "id")))
        return false;
  return true;
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
Value ContextStore::reject_managed(const Value &proposal, std::string_view why) {
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
  log_.record(identity, ApplicationChannel::context, packet);
  historical_ = log_.root().history_snapshot();
  return outcome;
}
Value ContextStore::pending_proposal() const {
  return pending_ ? pending_->proposal : Value{};
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
                                                  const Value &proposal) const {
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
  return credit;
}
void ContextStore::begin_workflow() {
  if (workflow_)
    throw Error{ErrorCode::conflict};
  workflow_ = true;
}
Value ContextStore::manage(const Value &proposal) {
  try {
    if (string_field(proposal, "base") != head_)
      return reject_managed(proposal, "stale-base");
    if (pending_)
      return reject_managed(proposal, "pending-overlap");
    const auto &mode = string_field(proposal, "mode");
    if (mode != "select" && mode != "archive" && mode != "summarize" &&
        mode != "restore")
      return reject_managed(proposal, "invalid-mode");
    if (string_field(proposal, "reason").empty() ||
        string_field(proposal, "source").empty())
      return reject_managed(proposal, "missing-source-or-reason");
    const auto &ids = field(proposal, "ids");
    if (!std::holds_alternative<Value::Array>(ids.value()))
      return reject_managed(proposal, "invalid-ids");
    std::set<std::string> selected;
    for (const auto &id : ids.array()) {
      if (!std::holds_alternative<std::string>(id.value()) ||
          (!originals_.contains(id.string()) && !original_entry(id.string())) ||
          !selected.insert(id.string()).second)
        return reject_managed(proposal, "invalid-ids");
      if (mode != "restore" &&
          std::none_of(entries_.begin(), entries_.end(), [&](const Value &e) {
            return string_field(e, "id") == id.string();
          }))
        return reject_managed(proposal, "absent-id");
    }
    if (mode == "summarize") {
      const auto &summary = field(proposal, "summary");
      if (selected.empty() || !std::holds_alternative<Value::Object>(summary.value()) ||
          summary.find("type") ||
          (string_field(summary, "role") != "developer" &&
           string_field(summary, "role") != "assistant") ||
          string_field(summary, "content").empty())
        return reject_managed(proposal, "invalid-summary");
    }
    if (!whole_groups(mode == "restore" ? captured_entries() : entries_, selected))
      return reject_managed(proposal, "partial-tool-group");
    for (const auto &entry : entries_) {
      const auto &id = string_field(entry, "id");
      const auto *role = field(entry, "item").find("role");
      const bool pinned = role && (*role == Value{"user"} || *role == Value{"system"} ||
                                   *role == Value{"developer"});
      if (pinned &&
          ((mode == "select" && !selected.contains(id)) ||
           ((mode == "archive" || mode == "summarize") && selected.contains(id))))
        return reject_managed(proposal, "mandatory-entry");
    }
    if (!workflow_)
      return publish_managed(proposal, entries_, "");
    if (protect)
      protect(entries_, proposal);
    auto result = reject_managed(proposal, "staged");
    pending_ = Pending{proposal, entries_, head_, string_field(result, "revision")};
    result.object().emplace_back("staged", Value{true});
    return result;
  } catch (const Error &e) {
    if (e.code != ErrorCode::corrupt && e.code != ErrorCode::invalid_range)
      throw;
    return reject_managed(proposal, "malformed-proposal");
  }
}
Value ContextStore::finish_workflow(bool success, const Value &boundary_program) {
  workflow_ = false;
  if (!pending_) {
    if (success && boundary_program != Value{})
      log_.record(ApplicationChannel::program, boundary_program);
    historical_ = log_.root().history_snapshot();
    return Value{};
  }
  auto pending = std::move(*pending_);
  if (!success) {
    pending_.reset();
    if (log_.root().state() != JournalWriterState::live)
      return Value::object({{"accepted", Value{false}},
                            {"reason", Value{"workflow-cancelled-audit-unavailable"}}});
    return reject_managed(pending.proposal, "workflow-cancelled");
  }
  auto result = pending.expected != head_
                    ? reject_managed(pending.proposal, "settlement-conflict")
                    : publish_managed(pending.proposal, pending.snapshot, pending.stage,
                                      boundary_program);
  if (!accepted(result) && boundary_program != Value{})
    log_.record(ApplicationChannel::program, boundary_program);
  pending_.reset();
  historical_ = log_.root().history_snapshot();
  return result;
}
Value ContextStore::publish_managed(const Value &proposal, const Value::Array &basis,
                                    std::string_view stage,
                                    const Value &boundary_program) {
  const auto &mode = string_field(proposal, "mode");
  std::set<std::string> selected;
  for (const auto &id : field(proposal, "ids").array())
    selected.insert(id.string());
  auto identity = log_.issue();
  auto revision = hex_identity(identity.bytes());
  Value::Array next, fresh;
  std::set<std::string> pending_calls, active_entries;
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
  if (!pending_calls.empty())
    for (std::size_t i = open_start; i < basis.size(); ++i)
      active_entries.insert(string_field(basis[i], "id"));
  bool inserted = false;
  for (const auto &entry : basis) {
    const bool chosen = selected.contains(string_field(entry, "id"));
    if (mode == "summarize" && chosen && !inserted) {
      auto summary = Value::object({{"id", Value{revision + ".0"}},
                                    {"item", field(proposal, "summary")},
                                    {"ancestry", field(proposal, "ids")}});
      next.push_back(summary);
      fresh.push_back(summary);
      inserted = true;
    }
    if ((mode == "select" &&
         (chosen || active_entries.contains(string_field(entry, "id")))) ||
        ((mode == "archive" || mode == "summarize") && !chosen) || mode == "restore")
      next.push_back(entry);
  }
  // Explicit append-only continuation settlement, retained separately from input base.
  if (!stage.empty()) {
    if (entries_.size() < basis.size() ||
        !std::equal(basis.begin(), basis.end(), entries_.begin()))
      return reject_managed(proposal, "settlement-conflict");
    next.insert(next.end(),
                entries_.begin() + static_cast<std::ptrdiff_t>(basis.size()),
                entries_.end());
  }
  if (mode == "restore") {
    // Remove selected presentations first, then merge exact originals in capture order.
    // This also repairs a reordered/edited group, rather than retaining its bad order.
    std::erase_if(next, [&](const Value &entry) {
      return selected.contains(string_field(entry, "id"));
    });
    const auto capture_order = captured_entries();
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
    return reject_managed(proposal, "invalid-tool-protocol");
  std::map<std::string, Original> originals;
  std::vector<Original> captured;
  for (const auto &entry : fresh) {
    if (originals_.contains(string_field(entry, "id")))
      throw Error{ErrorCode::conflict};
    originals.emplace(string_field(entry, "id"), Original{entry});
    captured.push_back(entry);
  }
  reserve_append(captured_, captured.size());
  auto outcome = Value::object({{"accepted", Value{true}},
                                {"reason", Value{"published"}},
                                {"revision", Value{revision}},
                                {"current", Value{revision}}});
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
    protect(next, proposal);
  log_.record_boundary(identity, packet, boundary_program);
  historical_ = log_.root().history_snapshot();
  originals_.merge(originals);
  for (auto &entry : captured)
    captured_.push_back(std::move(entry));
  entries_.swap(next);
  head_.swap(revision);
  maybe_checkpoint();
  return outcome;
}
Value ContextStore::inspect(const Value &query) const {
  const auto &kind = string_field(query, "kind");
  auto snapshot = head_;
  if (kind == "history")
    visit_context_records(historical_, [&](const Value &packet) {
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
    visit_context_records(historical_, [&](const Value &packet) {
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
    visit_context_records(historical_, [&](const Value &packet) {
      if (!accepted(packet))
        return;
      const auto *fresh = packet.find("originals");
      if (!fresh)
        return;
      for (const auto &entry : fresh->array()) {
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
      const auto found = original_entry(id->string());
      if (!found)
        throw Error{ErrorCode::invalid_identity};
      emit_value(*found);
    } else {
      if (json_export)
        emit("[");
      bool first = true;
      visit_context_records(historical_, [&](const Value &packet) {
        if (!accepted(packet))
          return;
        const auto *fresh = packet.find("originals");
        if (!fresh)
          return;
        for (const auto &entry : fresh->array()) {
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
  constexpr char digits[] = "0123456789abcdef";
  std::string hex;
  for (const auto byte : page) {
    const auto c = static_cast<unsigned char>(byte);
    hex += digits[c >> 4U];
    hex += digits[c & 15U];
  }
  const auto offset = std::min(requested, total);
  return Value::object(
      {{"encoding", Value{json_export ? "hex-json-utf8" : "hex-bbm2-stream"}},
       {"bytes", Value{std::move(hex)}},
       {"total_bytes", number(total)},
       {"offset", number(offset)},
       {"next", number(offset + page.size())},
       {"revision", Value{snapshot}},
       {"context_revision", Value{head_}}});
}

} // namespace blackbird
