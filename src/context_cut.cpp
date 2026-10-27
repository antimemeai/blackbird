#include "blackbird/context_cut.hpp"
#include "blackbird/context_compose.hpp"
#include "blackbird/context_units.hpp"
#include "blackbird/journal.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/tools.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <list>
#include <map>
#include <set>
#include <span>

namespace blackbird {
namespace {
[[noreturn]] void corrupt() { throw Error{ErrorCode::corrupt}; }
int hex_value(char c) noexcept {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}
std::optional<IdentityBytes> parse_identity(std::string_view text) noexcept {
  if (text.size() != 32)
    return std::nullopt;
  IdentityBytes bytes{};
  for (std::size_t i = 0; i < 16; ++i) {
    const auto high = hex_value(text[i * 2]), low = hex_value(text[i * 2 + 1]);
    if (high < 0 || low < 0)
      return std::nullopt;
    bytes[i] = static_cast<std::byte>(high * 16 + low);
  }
  return bytes;
}
std::string identity_text(const IdentityBytes &bytes) {
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
const Value &member(const Value &object, std::string_view key) {
  const auto *found = object.find(key);
  if (!found)
    corrupt();
  return *found;
}
const std::string &text_of(const Value &value) {
  const auto *s = std::get_if<std::string>(&value.value());
  if (!s)
    corrupt();
  return *s;
}
const Value::Array &array_of(const Value &value) {
  const auto *a = std::get_if<Value::Array>(&value.value());
  if (!a)
    corrupt();
  return *a;
}
bool bool_of(const Value &value) {
  const auto *b = std::get_if<bool>(&value.value());
  if (!b)
    corrupt();
  return *b;
}
std::uint64_t u64_of(const Value &value) {
  const auto *n = std::get_if<Number>(&value.value());
  if (!n)
    corrupt();
  if (const auto *u = std::get_if<std::uint64_t>(&n->storage()))
    return *u;
  if (const auto *s = std::get_if<std::int64_t>(&n->storage()); s && *s >= 0)
    return static_cast<std::uint64_t>(*s);
  corrupt();
}
const std::string &revision_of(const Value &value) {
  const auto &text = text_of(value);
  if (!parse_identity(text))
    corrupt();
  return text;
}
// A head: a revision, or null for the empty head before the first context frame.
Value head_value(const std::string &head) {
  return head.empty() ? Value{} : Value{head};
}
std::string head_of(const Value &value) {
  if (std::holds_alternative<std::nullptr_t>(value.value()))
    return {};
  return revision_of(value);
}
// Only the listed keys may appear: a frame field nothing reads is refused.
void only_keys(const Value &object, std::initializer_list<std::string_view> keys) {
  const auto *fields = std::get_if<Value::Object>(&object.value());
  if (!fields)
    corrupt();
  std::set<std::string_view> seen;
  for (const auto &[key, value] : *fields) {
    (void)value;
    if (std::find(keys.begin(), keys.end(), key) == keys.end() ||
        !seen.insert(key).second)
      corrupt();
  }
}
template <class Enum, std::size_t N>
Enum named(const Value &value, const std::array<std::string_view, N> &names) {
  const auto &text = text_of(value);
  for (std::size_t i = 0; i < N; ++i)
    if (names[i] == text)
      return static_cast<Enum>(i);
  corrupt();
}
constexpr std::array<std::string_view, 5> kind_names{"start", "archival", "rebind",
                                                     "prefix", "recovery"};
constexpr std::array<std::string_view, 6> change_names{
    "archive", "edit", "restore", "summary", "compaction", "boundary"};
constexpr std::array<std::string_view, 7> source_names{
    "model", "step", "operator", "recovery", "rebind", "prefix", "start"};
Value number(std::uint64_t n) { return Value{Number{n}}; }
Value refs_value(const std::vector<ContextRef> &refs) {
  Value::Array out;
  out.reserve(refs.size());
  for (const auto &ref : refs)
    out.push_back(encode_ref(ref));
  return Value{std::move(out)};
}
std::vector<ContextRef> refs_of(const Value &value) {
  std::vector<ContextRef> out;
  for (const auto &ref : array_of(value))
    out.push_back(decode_ref(ref));
  return out;
}
const Value &object_item(const Value &value) {
  if (!std::holds_alternative<Value::Object>(value.value()))
    corrupt();
  return value;
}
ContextRef entry_ref(const Value &entry) {
  const auto ref = parse_entry_id(text_of(member(entry, "id")));
  if (!ref)
    corrupt();
  return *ref;
}
} // namespace

std::optional<ContextRef> parse_entry_id(std::string_view id) noexcept {
  if (id.size() < 34 || id.size() > 43 || id[32] != '.')
    return std::nullopt;
  const auto identity = parse_identity(id.substr(0, 32));
  if (!identity)
    return std::nullopt;
  const auto digits = id.substr(33);
  if (digits.size() > 1 && digits[0] == '0')
    return std::nullopt;
  std::uint32_t ordinal = 0;
  const auto parsed =
      std::from_chars(digits.data(), digits.data() + digits.size(), ordinal);
  if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size())
    return std::nullopt;
  return ContextRef{*identity, ordinal};
}
std::string entry_id(const ContextRef &ref) {
  return identity_text(ref.record) + "." + std::to_string(ref.ordinal);
}
Value encode_ref(const ContextRef &ref) {
  return Value{Value::Array{Value{identity_text(ref.record)}, number(ref.ordinal)}};
}
ContextRef decode_ref(const Value &value) {
  const auto &pair = array_of(value);
  if (pair.size() != 2)
    corrupt();
  const auto identity = parse_identity(text_of(pair[0]));
  const auto ordinal = u64_of(pair[1]);
  if (!identity || ordinal > std::numeric_limits<std::uint32_t>::max())
    corrupt();
  return {*identity, static_cast<std::uint32_t>(ordinal)};
}

Value encode_cut_frame(const CutFrame &frame) {
  const bool repair = frame.op == CutOp::repair;
  Value::Object out{{"op", Value{repair ? "repair" : "cut"}},
                    {"format", Value{std::string{repair ? repair_format : cut_format}}},
                    {"revision", Value{frame.revision}},
                    {"base", head_value(frame.base)},
                    {"observed", head_value(frame.observed)},
                    {"accepted", Value{frame.accepted}}};
  if (!frame.accepted) {
    out.emplace_back("reason", Value{frame.reason});
    return Value{std::move(out)};
  }
  if (!repair) {
    out.emplace_back(
        "kind", Value{std::string{kind_names[static_cast<std::size_t>(frame.kind)]}});
    Value::Array changes;
    for (std::size_t i = 0; i < change_names.size(); ++i)
      if (frame.changes & (1U << i))
        changes.push_back(Value{std::string{change_names[i]}});
    out.emplace_back("changes", Value{std::move(changes)});
    out.emplace_back("chapter", frame.chapter);
    out.emplace_back("archived", refs_value(frame.archived));
    out.emplace_back("swept", refs_value(frame.swept));
    out.emplace_back("reasoning", refs_value(frame.reasoning));
    Value::Array replace;
    for (const auto &r : frame.replace)
      replace.push_back(Value::object({{"ref", encode_ref(r.ref)}, {"item", r.item}}));
    out.emplace_back("replace", Value{std::move(replace)});
  } else {
    Value::Array skipped;
    for (const auto &revision : frame.skipped)
      skipped.push_back(Value{revision});
    out.emplace_back("skipped", Value{std::move(skipped)});
  }
  Value::Array insert;
  for (const auto &i : frame.insert) {
    Value::Object one{{"anchor", i.anchor ? encode_ref(*i.anchor) : Value{"start"}}};
    if (const auto *fresh = std::get_if<std::uint32_t>(&i.entry))
      one.emplace_back("fresh", number(*fresh));
    else
      one.emplace_back("existing", encode_ref(std::get<ContextRef>(i.entry)));
    insert.push_back(Value{std::move(one)});
  }
  out.emplace_back("insert", Value{std::move(insert)});
  Value::Array originals;
  for (const auto &o : frame.originals) {
    auto one = Value::object({{"item", o.item}, {"class", number(o.entry_class)}});
    if (o.unit)
      one.object().emplace_back("unit", number(*o.unit));
    originals.push_back(std::move(one));
  }
  out.emplace_back("originals", Value{std::move(originals)});
  if (!repair) {
    out.emplace_back("refused",
                     frame.refused == Value{} ? Value{Value::Array{}} : frame.refused);
    out.emplace_back("pending", frame.pending);
  }
  const auto result = frame.result.value_or(CutResult{});
  out.emplace_back("result",
                   Value::object({{"count", number(result.count)},
                                  {"crc32c", number(result.crc32c)},
                                  {"encoded_bytes", number(result.encoded_bytes)}}));
  if (!repair)
    out.emplace_back("sizes", Value::object({{"before", number(frame.sizes.before)},
                                             {"prefix", number(frame.sizes.prefix)}}));
  Value::Object cause{
      {"source",
       Value{std::string{source_names[static_cast<std::size_t>(frame.cause.source)]}}}};
  if (!frame.cause.attempt.empty())
    cause.emplace_back("attempt", Value{frame.cause.attempt});
  if (!frame.cause.stage.empty())
    cause.emplace_back("stage", Value{frame.cause.stage});
  if (frame.cause.forced)
    cause.emplace_back("forced", Value{true});
  out.emplace_back("cause", Value{std::move(cause)});
  return Value{std::move(out)};
}

CutFrame decode_cut_frame(const Value &packet) {
  CutFrame frame;
  const auto &op = text_of(member(packet, "op"));
  if (op == "cut")
    frame.op = CutOp::cut;
  else if (op == "repair")
    frame.op = CutOp::repair;
  else
    corrupt();
  const bool repair = frame.op == CutOp::repair;
  if (text_of(member(packet, "format")) != (repair ? repair_format : cut_format))
    corrupt();
  frame.revision = revision_of(member(packet, "revision"));
  frame.observed = head_of(member(packet, "observed"));
  frame.accepted = bool_of(member(packet, "accepted"));
  if (!frame.accepted) {
    // Staged and refused frames publish nothing; S3b owns their other fields.
    if (repair)
      corrupt();
    frame.base = frame.observed;
    frame.reason = text_of(member(packet, "reason"));
    if (frame.reason.empty())
      corrupt();
    return frame;
  }
  frame.base = head_of(member(packet, "base"));
  if (repair)
    only_keys(packet, {"op", "format", "revision", "base", "observed", "accepted",
                       "skipped", "insert", "originals", "result", "cause"});
  else
    only_keys(packet, {"op",       "format",    "revision", "base",    "observed",
                       "accepted", "kind",      "changes",  "chapter", "archived",
                       "swept",    "reasoning", "replace",  "insert",  "originals",
                       "refused",  "pending",   "result",   "sizes",   "cause"});
  if (!repair) {
    frame.kind = named<CutKind>(member(packet, "kind"), kind_names);
    for (const auto &change : array_of(member(packet, "changes"))) {
      const auto bit = static_cast<std::uint8_t>(
          1U << static_cast<unsigned>(named<std::uint8_t>(change, change_names)));
      if (frame.changes & bit)
        corrupt();
      frame.changes |= bit;
    }
    frame.chapter = member(packet, "chapter");
    if (!std::holds_alternative<std::nullptr_t>(frame.chapter.value())) {
      only_keys(frame.chapter, {"ordinal", "previous", "binding"});
      if (u64_of(member(frame.chapter, "ordinal")) == 0)
        corrupt();
      if (const auto &previous = member(frame.chapter, "previous");
          !std::holds_alternative<std::nullptr_t>(previous.value()))
        (void)revision_of(previous);
      (void)decode_binding(member(frame.chapter, "binding"));
    }
    frame.archived = refs_of(member(packet, "archived"));
    frame.swept = refs_of(member(packet, "swept"));
    frame.reasoning = refs_of(member(packet, "reasoning"));
    for (const auto &r : array_of(member(packet, "replace"))) {
      only_keys(r, {"ref", "item"});
      frame.replace.push_back(
          {decode_ref(member(r, "ref")), object_item(member(r, "item"))});
    }
    frame.refused = member(packet, "refused");
    (void)array_of(frame.refused);
    frame.pending = member(packet, "pending");
    if (!std::holds_alternative<std::nullptr_t>(frame.pending.value()))
      (void)object_item(frame.pending);
    const auto &sizes = member(packet, "sizes");
    only_keys(sizes, {"before", "prefix"});
    frame.sizes = {u64_of(member(sizes, "before")), u64_of(member(sizes, "prefix"))};
  } else {
    for (const auto &revision : array_of(member(packet, "skipped")))
      frame.skipped.push_back(revision_of(revision));
    if (frame.skipped.empty())
      corrupt();
  }
  for (const auto &i : array_of(member(packet, "insert"))) {
    CutInsert one;
    const auto &anchor = member(i, "anchor");
    if (const auto *s = std::get_if<std::string>(&anchor.value())) {
      if (*s != "start")
        corrupt();
    } else
      one.anchor = decode_ref(anchor);
    const auto *fresh = i.find("fresh");
    const auto *existing = i.find("existing");
    if (fresh && !existing) {
      only_keys(i, {"anchor", "fresh"});
      const auto index = u64_of(*fresh);
      if (index > std::numeric_limits<std::uint32_t>::max())
        corrupt();
      one.entry = static_cast<std::uint32_t>(index);
    } else if (existing && !fresh) {
      only_keys(i, {"anchor", "existing"});
      if (repair)
        corrupt(); // a repair re-inserts nothing it removed
      one.entry = decode_ref(*existing);
    } else
      corrupt();
    frame.insert.push_back(std::move(one));
  }
  for (const auto &o : array_of(member(packet, "originals"))) {
    only_keys(o, {"item", "class", "unit"});
    const auto entry_class = u64_of(member(o, "class"));
    if (entry_class > 255)
      corrupt();
    CutOriginal original{object_item(member(o, "item")),
                         static_cast<std::uint8_t>(entry_class), std::nullopt};
    if (const auto *unit = o.find("unit")) {
      // A unit key names an original at or before this one; a repair writes none.
      const auto key = u64_of(*unit);
      if (repair || key > frame.originals.size())
        corrupt();
      original.unit = static_cast<std::uint32_t>(key);
    }
    frame.originals.push_back(std::move(original));
  }
  const auto &result = member(packet, "result");
  only_keys(result, {"count", "crc32c", "encoded_bytes"});
  const auto crc = u64_of(member(result, "crc32c"));
  if (crc > std::numeric_limits<std::uint32_t>::max())
    corrupt();
  frame.result =
      CutResult{u64_of(member(result, "count")), static_cast<std::uint32_t>(crc),
                u64_of(member(result, "encoded_bytes"))};
  const auto &cause = member(packet, "cause");
  only_keys(cause, {"source", "attempt", "stage", "forced"});
  frame.cause.source = named<CutSource>(member(cause, "source"), source_names);
  if (const auto *attempt = cause.find("attempt"))
    frame.cause.attempt = revision_of(*attempt);
  if (const auto *stage = cause.find("stage"))
    frame.cause.stage = revision_of(*stage);
  if (const auto *forced = cause.find("forced"))
    frame.cause.forced = bool_of(*forced);
  if (repair && frame.cause.source != CutSource::operator_)
    corrupt();
  return frame;
}

namespace {
// CRC32C over the 20-byte native form of each ref, in order.
std::uint32_t refs_crc(std::span<const ContextRef> refs) noexcept {
  std::uint32_t crc = 0;
  std::array<std::byte, 20> bytes{};
  for (const auto &ref : refs) {
    std::copy(ref.record.begin(), ref.record.end(), bytes.begin());
    for (unsigned i = 0; i < 4; ++i)
      bytes[16 + i] = static_cast<std::byte>((ref.ordinal >> (8 * i)) & 255U);
    crc = crc32c(bytes, crc);
  }
  return crc;
}
} // namespace

CutResult measure_entries(const Value::Array &entries, bool encoded) {
  std::vector<ContextRef> refs;
  refs.reserve(entries.size());
  for (const auto &entry : entries)
    refs.push_back(entry_ref(entry));
  CutResult result{entries.size(), refs_crc(refs), 0};
  if (encoded) {
    // Counted, not built: the size the array of items would encode to, with no
    // packet limit, since a context may exceed one packet.
    const ValueLimits unbounded{std::numeric_limits<std::size_t>::max(),
                                std::numeric_limits<std::size_t>::max(), 128};
    // BBM2 header, array tag and count, then each item.
    std::size_t total = 4 + 1;
    for (auto n = entries.size(); n >= 128; n >>= 7)
      ++total;
    ++total;
    for (const auto &entry : entries) {
      auto size = encoded_packet_size(member(entry, "item"), unbounded);
      if (!size.has_value())
        throw size.error();
      total += size.value() - 4;
    }
    result.encoded_bytes = total;
  }
  return result;
}

Value::Array apply_frame(const CutFrame &frame, const Value::Array &entries) {
  if (!frame.accepted)
    corrupt();
  // One ref parse per input entry; positions are list nodes found through an
  // index, so each step is a lookup, not a scan.
  using Entries = std::list<std::pair<ContextRef, Value>>;
  Entries next;
  std::map<ContextRef, Entries::iterator> at;
  for (const auto &entry : entries) {
    const auto ref = entry_ref(entry);
    const auto node = next.emplace(next.end(), ref, entry);
    if (!at.emplace(ref, node).second)
      corrupt();
  }
  // 1-2. archived, swept and reasoning are disjoint, every listed ref is
  // present; remove them, keeping each for an existing re-insert.
  std::map<ContextRef, Value> taken;
  for (const auto *list : {&frame.archived, &frame.swept, &frame.reasoning})
    for (const auto &ref : *list) {
      const auto found = at.find(ref);
      if (found == at.end())
        corrupt(); // absent, or already removed by an earlier list
      taken.emplace(ref, std::move(found->second->second));
      next.erase(found->second);
      at.erase(found);
    }
  // 3. Each replace names a present ref; its item replaces the item under that ID.
  std::set<ContextRef> replaced;
  for (const auto &r : frame.replace) {
    const auto found = at.find(r.ref);
    if (found == at.end() || !replaced.insert(r.ref).second)
      corrupt();
    auto &entry = found->second->second;
    for (auto &[key, value] : entry.object())
      if (key == "item")
        value = object_item(r.item);
  }
  // 4-5. Inserts in listed order, each immediately after its anchor; no anchor
  // twice; every fresh original exactly once; an existing entry only if this
  // frame removed it, once. A fresh ID never names a present entry.
  std::set<std::optional<ContextRef>> anchors;
  std::vector<bool> fresh_used(frame.originals.size(), false);
  const auto revision = parse_identity(frame.revision);
  for (const auto &insert : frame.insert) {
    if (!anchors.insert(insert.anchor).second)
      corrupt();
    auto position = next.begin();
    if (insert.anchor) {
      const auto found = at.find(*insert.anchor);
      if (found == at.end())
        corrupt();
      position = std::next(found->second);
    }
    ContextRef ref;
    Value entry;
    if (const auto *fresh = std::get_if<std::uint32_t>(&insert.entry)) {
      if (!revision || *fresh >= frame.originals.size() || fresh_used[*fresh])
        corrupt();
      fresh_used[*fresh] = true;
      ref = {*revision, *fresh};
      entry =
          Value::object({{"id", Value{frame.revision + "." + std::to_string(*fresh)}},
                         {"item", object_item(frame.originals[*fresh].item)}});
    } else {
      ref = std::get<ContextRef>(insert.entry);
      const auto found = taken.find(ref);
      if (found == taken.end())
        corrupt(); // not removed by this frame, or already re-inserted
      entry = std::move(found->second);
      taken.erase(found);
    }
    const auto node = next.emplace(position, ref, std::move(entry));
    if (!at.emplace(ref, node).second)
      corrupt();
  }
  if (std::find(fresh_used.begin(), fresh_used.end(), false) != fresh_used.end())
    corrupt();
  // 6. The result's count and CRC32C equal the recorded result.
  if (frame.result) {
    std::vector<ContextRef> refs;
    refs.reserve(next.size());
    for (const auto &[ref, entry] : next)
      refs.push_back(ref);
    if (next.size() != frame.result->count || refs_crc(refs) != frame.result->crc32c)
      corrupt();
  }
  Value::Array out;
  out.reserve(next.size());
  for (auto &[ref, entry] : next)
    out.push_back(std::move(entry));
  return out;
}
// ---- Chapters and bindings (design §3, §4.2, §4.4).
Value encode_binding(const ChapterBinding &binding) {
  return Value::object(
      {{"provider", Value{binding.provider}},
       {"protocol", Value{binding.protocol}},
       {"base_url", Value{binding.base_url}},
       {"account", Value{binding.account}},
       {"model", Value{binding.model}},
       {"profile", Value::object({{"id", Value{binding.profile_id}},
                                  {"revision", number(binding.profile_revision)},
                                  {"digest", Value{binding.profile_digest}}})},
       {"tools_digest", Value{binding.tools_digest}},
       {"instructions_digest", Value{binding.instructions_digest}}});
}
ChapterBinding decode_binding(const Value &value) {
  only_keys(value, {"provider", "protocol", "base_url", "account", "model", "profile",
                    "tools_digest", "instructions_digest"});
  const auto &profile = member(value, "profile");
  only_keys(profile, {"id", "revision", "digest"});
  return {text_of(member(value, "provider")),
          text_of(member(value, "protocol")),
          text_of(member(value, "base_url")),
          text_of(member(value, "account")),
          text_of(member(value, "model")),
          text_of(member(profile, "id")),
          u64_of(member(profile, "revision")),
          text_of(member(profile, "digest")),
          text_of(member(value, "tools_digest")),
          text_of(member(value, "instructions_digest"))};
}
Value chapter_section_value(const ChapterState &state) {
  Value::Array table;
  table.reserve(state.table.size());
  for (const auto &row : state.table)
    table.push_back(Value{Value::Array{
        Value{row.revision}, number(row.ordinal),
        Value{std::string{kind_names[static_cast<std::size_t>(row.kind)]}}}});
  return Value::object(
      {{"binding", encode_binding(state.binding)}, {"table", Value{std::move(table)}}});
}
ChapterState read_chapter_section(const Value &value) {
  only_keys(value, {"binding", "table"});
  ChapterState state;
  state.binding = decode_binding(member(value, "binding"));
  std::set<std::string> seen;
  for (const auto &row : array_of(member(value, "table"))) {
    const auto &fields = array_of(row);
    if (fields.size() != 3)
      corrupt();
    ChapterRow one{revision_of(fields[0]), u64_of(fields[1]),
                   named<CutKind>(fields[2], kind_names)};
    // Ordinals run 1, 2, 3...; only the first chapter is a start.
    if (one.ordinal != state.table.size() + 1 ||
        (one.kind == CutKind::start) != state.table.empty() ||
        !seen.insert(one.revision).second)
      corrupt();
    state.table.push_back(std::move(one));
  }
  if (state.table.empty())
    corrupt();
  return state;
}
Result<void> validate_chapter_section(const Value &value) noexcept {
  try {
    (void)read_chapter_section(value);
    return Result<void>::success();
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  } catch (const Error &error) {
    return Result<void>::failure(error);
  } catch (...) {
    return Result<void>::failure({ErrorCode::corrupt});
  }
}
std::optional<ChapterStep> chapter_step(const ChapterState &state, const CutFrame &frame) {
  if (!frame.accepted || frame.op != CutOp::cut ||
      std::holds_alternative<std::nullptr_t>(frame.chapter.value()))
    return std::nullopt;
  const auto ordinal = u64_of(member(frame.chapter, "ordinal"));
  const auto &previous = member(frame.chapter, "previous");
  auto binding = decode_binding(member(frame.chapter, "binding"));
  if (state.table.empty()) {
    if (frame.kind != CutKind::start || ordinal != 1 ||
        !std::holds_alternative<std::nullptr_t>(previous.value()))
      corrupt();
  } else if (frame.kind == CutKind::start ||
             ordinal != state.table.back().ordinal + 1 ||
             std::holds_alternative<std::nullptr_t>(previous.value()) ||
             revision_of(previous) != state.table.back().revision)
    corrupt();
  return ChapterStep{{frame.revision, ordinal, frame.kind}, std::move(binding)};
}
void advance_chapter(ChapterState &state, const CutFrame &frame) {
  if (auto step = chapter_step(state, frame)) {
    state.table.push_back(std::move(step->row));
    state.binding = std::move(step->binding);
  }
}

// ---- Composition (design §5.5, §5.6, §6, §7.3).
namespace {
constexpr auto npos = std::numeric_limits<std::size_t>::max();
const std::string *text_field(const Value &value, std::string_view key) noexcept {
  const auto *found = value.find(key);
  return found ? std::get_if<std::string>(&found->value()) : nullptr;
}
bool type_is(const Value &item, std::string_view type) noexcept {
  const auto *t = text_field(item, "type");
  return t && *t == type;
}
bool role_is(const Value &item, std::string_view role) noexcept {
  const auto *r = text_field(item, "role");
  return r && *r == role;
}
const Value &item_at(const Value::Array &entries, std::size_t position) {
  return member(entries[position], "item");
}
// The text of a message item: a string, or its text parts in order.
std::string text_content(const Value &item) {
  const auto *content = item.find("content");
  if (!content)
    return {};
  if (const auto *s = std::get_if<std::string>(&content->value()))
    return *s;
  std::string out;
  if (const auto *parts = std::get_if<Value::Array>(&content->value()))
    for (const auto &part : *parts)
      if (const auto *t = text_field(part, "text"))
        out += *t;
  return out;
}
// The fields composition reads from the effective profile.
struct Rules {
  std::string at_cut, at_rebind, note_role, note_placement, boundary_form,
      state_placement, tool_change, archival;
  bool boundary_enabled = false;
};
// A profile field by family and name ("reasoning", "at_cut").
struct ProfileField {
  std::string_view family, name;
};
const Value &profile_at(const Value &profile, ProfileField field) {
  const auto *table = profile.find(field.family);
  const auto *value = table ? table->find(field.name) : nullptr;
  if (!value)
    throw Error{ErrorCode::invalid_range};
  return *value;
}
std::string profile_text(const Value &profile, ProfileField field) {
  const auto *text = std::get_if<std::string>(&profile_at(profile, field).value());
  if (!text)
    throw Error{ErrorCode::invalid_range};
  return *text;
}
Rules rules_of(const Value &profile) {
  Rules rules;
  rules.at_cut = profile_text(profile, {"reasoning", "at_cut"});
  rules.at_rebind = profile_text(profile, {"reasoning", "at_rebind"});
  rules.note_role = profile_text(profile, {"note", "role"});
  rules.note_placement = profile_text(profile, {"note", "placement"});
  rules.state_placement = profile_text(profile, {"request_state", "placement"});
  rules.tool_change = profile_text(profile, {"tool_change", "mode"});
  rules.archival = profile_text(profile, {"archival", "mode"});
  const auto &boundary = profile_at(profile, {"turn", "boundary"});
  rules.boundary_form = text_of(member(boundary, "form"));
  rules.boundary_enabled = bool_of(member(boundary, "enabled"));
  return rules;
}
// A note item in the profile's role; its text is one input_text part, so the wire
// carries the template exactly (a string would gain the Anthropic codec's prefix).
Value note_item(std::string_view role, std::string text) {
  return Value::object(
      {{"type", Value{"message"}},
       {"role", Value{std::string{role}}},
       {"content",
        Value{Value::Array{Value::object(
            {{"type", Value{"input_text"}}, {"text", Value{std::move(text)}}})}}}});
}
std::string note_role(const Rules &rules) {
  return rules.note_role == "developer_as_user_text" ? std::string{"developer"}
                                                     : rules.note_role;
}
// The bytes an item adds to an encoded array of items (measure_entries' counting).
std::uint64_t item_bytes(const Value &item) {
  const ValueLimits unbounded{std::numeric_limits<std::size_t>::max(),
                              std::numeric_limits<std::size_t>::max(), 128};
  auto size = encoded_packet_size(item, unbounded);
  if (!size.has_value())
    throw size.error();
  return size.value() - 4;
}
// Each entry's item bytes, measured once per composition (B7): the sizes, the
// note's rows and the result all read them.
std::vector<std::uint64_t> sizes_of(const Value::Array &entries) {
  std::vector<std::uint64_t> sizes;
  sizes.reserve(entries.size());
  for (std::size_t i = 0; i < entries.size(); ++i)
    sizes.push_back(item_bytes(item_at(entries, i)));
  return sizes;
}
// For each position, whether an alive call at or before it has its alive output
// after it: an entry placed right after such a position would split a call group.
std::vector<bool> open_groups(const Value::Array &entries,
                              const std::vector<bool> &removed) {
  const auto n = entries.size();
  std::map<std::string_view, std::size_t> calls;
  std::vector<int> delta(n + 1, 0);
  for (std::size_t i = 0; i < n; ++i) {
    if (removed[i])
      continue;
    const auto &item = item_at(entries, i);
    const auto *id = text_field(item, "call_id");
    if (!id)
      continue;
    if (type_is(item, "function_call"))
      calls.emplace(*id, i);
    else if (type_is(item, "function_call_output"))
      if (const auto found = calls.find(*id); found != calls.end()) {
        ++delta[found->second];
        --delta[i];
        calls.erase(found);
      }
  }
  std::vector<bool> open(n, false);
  int depth = 0;
  for (std::size_t i = 0; i < n; ++i) {
    depth += delta[i];
    open[i] = depth > 0;
  }
  return open;
}
// The position of the first alive call no alive output answers; npos when none.
// Nothing may be placed at or after it: it would sit between the call and the
// output it is still waiting for.
std::size_t first_unanswered(const Value::Array &entries,
                             const std::vector<bool> &removed) {
  std::map<std::string_view, std::size_t> calls;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (removed[i])
      continue;
    const auto &item = item_at(entries, i);
    const auto *id = text_field(item, "call_id");
    if (!id)
      continue;
    if (type_is(item, "function_call"))
      calls.emplace(*id, i);
    else if (type_is(item, "function_call_output"))
      calls.erase(*id);
  }
  std::size_t first = npos;
  for (const auto &[id, position] : calls)
    first = std::min(first, position);
  return first;
}
std::optional<std::size_t> next_alive(const std::vector<bool> &removed,
                                      std::size_t from) {
  for (std::size_t i = from; i < removed.size(); ++i)
    if (!removed[i])
      return i;
  return std::nullopt;
}
std::optional<std::size_t> alive_before(const std::vector<bool> &removed,
                                        std::size_t at) {
  for (std::size_t i = std::min(at, removed.size()); i > 0; --i)
    if (!removed[i - 1])
      return i - 1;
  return std::nullopt;
}
// The nearest surviving entry at or before a position (design §4.3 step 4).
std::optional<std::size_t> alive_at_or_before(const std::vector<bool> &removed,
                                              std::optional<std::size_t> at) {
  if (!at)
    return std::nullopt;
  return alive_before(removed, *at + 1);
}
bool assistant_side(const Value &item) noexcept {
  return role_is(item, "assistant") || type_is(item, "function_call") ||
         type_is(item, "reasoning");
}
// The handle text of a row: its first word.
std::string_view row_handle(std::string_view row) noexcept {
  return row.substr(0, row.find(' '));
}
// The entry IDs a row lists after "ids".
std::vector<std::string> row_ids(std::string_view row) {
  std::vector<std::string> ids;
  const auto at = row.find("  ids ");
  if (at == std::string_view::npos)
    return ids;
  auto rest = row.substr(at + 6);
  while (!rest.empty()) {
    const auto space = rest.find(' ');
    const auto word = rest.substr(0, space);
    if (!word.empty())
      ids.emplace_back(word);
    if (space == std::string_view::npos)
      break;
    rest.remove_prefix(space + 1);
  }
  return ids;
}
std::string clean_head(std::string text) {
  for (auto &c : text)
    if (c == '\n' || c == '\r' || c == '\t')
      c = ' ';
  const auto cut = observation_prefix_size(text, 60);
  if (cut < text.size())
    return text.substr(0, cut) + "\xe2\x80\xa6";
  return text;
}
std::string pad(std::string text, std::size_t width) {
  if (text.size() < width)
    text.append(width - text.size(), ' ');
  return text;
}
CutRefusal refusal(std::string reason, std::string unit = {}) {
  return {std::move(reason), std::move(unit)};
}
std::string joined_ids(const std::vector<std::string> &ids) {
  std::string out;
  for (const auto &id : ids)
    out += (out.empty() ? "" : " ") + id;
  return out;
}
// The encoded bytes of an array of items (measure_entries' counting): BBM2 header,
// array tag and count, then each item.
std::uint64_t array_bytes(std::uint64_t count, std::uint64_t items) {
  std::uint64_t total = 4 + 1;
  for (auto n = count; n >= 128; n >>= 7)
    ++total;
  return total + 1 + items;
}
} // namespace

std::string inspect_call_text(std::string_view entry) {
  // V1 replaces this with context_inspect {unit} (design §11.4); until then the call
  // names today's tool and its required query exactly (src/tools.cpp context_inspect).
  return "context_inspect {\"query\": {\"kind\": \"originals\", \"entry\": \"" +
         std::string{entry} + "\", \"format\": \"json\"}}";
}
std::string size_text(std::uint64_t bytes) {
  const auto unit = bytes < 1024ULL * 1024 ? 1024ULL
                    : bytes < 1ULL << 30U  ? 1024ULL * 1024
                                           : 1ULL << 30U;
  const char suffix = unit == 1024 ? 'K' : unit == 1024ULL * 1024 ? 'M' : 'G';
  const auto tenths = (bytes * 10 + unit / 2) / unit;
  return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + suffix;
}
std::string archive_note_text(const std::vector<NoteRow> &rows,
                              const std::vector<std::string> &absorbed) {
  // The header's example call names the first listed entry.
  std::string first;
  if (!rows.empty() && !rows.front().ids.empty())
    first = rows.front().ids.front();
  for (std::size_t i = 0; first.empty() && i < absorbed.size(); ++i)
    if (const auto ids = row_ids(absorbed[i]); !ids.empty())
      first = ids.front();
  std::string out{archive_note_marker};
  out += " These units left your live context; nothing was summarised.\n"
         "Read one with " +
         inspect_call_text(first) + ", giving an entry ID from its row.";
  std::size_t handle = 0, kind = 0, head = 0, flags = 0, sizes = 0;
  std::vector<std::string> sized;
  for (const auto &row : rows) {
    auto text = size_text(row.shown);
    if (row.original && *row.original != row.shown)
      text += " of " + size_text(*row.original);
    handle = std::max(handle, row.handle.size());
    kind = std::max(kind, row.kind.size());
    head = std::max(head, row.head.size() + 2);
    flags = std::max(flags, row.flags.size());
    sizes = std::max(sizes, text.size());
    sized.push_back(std::move(text));
  }
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto &row = rows[i];
    out += "\n" + pad(row.handle, handle) + "  " + pad(row.kind, kind) + "  " +
           pad("\"" + row.head + "\"", head) + "  " + pad(row.flags, flags) + "  " +
           pad(sized[i], sizes) + "  ids " + joined_ids(row.ids);
  }
  for (const auto &row : absorbed)
    out += "\n" + row;
  return out;
}
std::string boundary_text(std::string_view request, std::string_view note) {
  std::string out{boundary_marker};
  out += " This note is from Blackbird, not the operator. Blackbird closed the turn "
         "here so that earlier rounds could leave your context. Your task is "
         "unchanged: continue from where you were.";
  if (!request.empty()) {
    out += " The operator's request is " + std::string{request};
    out +=
        note.empty() ? "." : "; the archive note " + std::string{note} + " lists what left.";
  } else if (!note.empty())
    out += " The archive note " + std::string{note} + " lists what left.";
  out += " No reply to this note is needed.";
  return out;
}
std::string summary_text(std::string_view handle, const std::vector<std::string> &ids,
                         std::string_view text) {
  return std::string{summary_marker} + " This replaces " + std::string{handle} +
         ", which you summarised. Its originals stay readable with " +
         inspect_call_text(ids.empty() ? std::string{} : ids.front()) +
         ", for each of its entries: " + joined_ids(ids) + ".\n" + std::string{text};
}
bool note_is(const Value &item, std::string_view marker) noexcept {
  try {
    return text_content(item).starts_with(marker);
  } catch (...) {
    return false;
  }
}
std::vector<std::string> note_rows(const Value &item) {
  std::vector<std::string> rows;
  const auto text = text_content(item);
  std::size_t start = 0, line = 0;
  while (start <= text.size()) {
    const auto end = text.find('\n', start);
    auto piece =
        text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (line++ >= 2 && !piece.empty())
      rows.push_back(std::move(piece));
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return rows;
}

NotePlacement place_note(const Value::Array &entries, const std::vector<bool> &removed,
                         std::size_t at, std::string_view placement) {
  const auto open = open_groups(entries, removed);
  auto anchor = alive_before(removed, at);
  const auto inside = [&] { return anchor && open[*anchor]; };
  const auto step = [&] {
    const auto next = next_alive(removed, anchor ? *anchor + 1 : 0);
    if (next)
      anchor = *next;
    return next.has_value();
  };
  while (inside() && step()) {
  }
  if (placement == "after_user_turn_before_assistant") {
    while (true) {
      const auto next = next_alive(removed, anchor ? *anchor + 1 : 0);
      if (!next || assistant_side(item_at(entries, *next)))
        break;
      anchor = *next;
    }
    while (inside() && step()) {
    }
  } else if (placement == "before_next_user_entry") {
    // Just before the next user entry; never right after a tool output (§5.5): a
    // tool output there moves the note past that user entry, and with no later user
    // entry it cannot be placed.
    while (true) {
      while (true) {
        const auto next = next_alive(removed, anchor ? *anchor + 1 : 0);
        if (!next || role_is(item_at(entries, *next), "user"))
          break;
        anchor = *next;
      }
      if (!anchor || !type_is(item_at(entries, *anchor), "function_call_output"))
        break;
      const auto user = next_alive(removed, *anchor + 1);
      if (!user)
        return {anchor, false};
      anchor = *user;
    }
    while (inside() && step()) {
    }
  }
  return {anchor, true};
}
std::optional<std::size_t> place_boundary(const std::vector<bool> &removed) {
  return alive_before(removed, removed.size());
}
std::vector<std::size_t> sweep(const CutInput &input, std::size_t p,
                               const std::vector<bool> &removed, bool notes,
                               bool boundary) {
  const auto &entries = input.entries;
  const bool retaining =
      profile_text(input.profile, {"request_state", "placement"}) == "retained_in_place";
  // The newest live boundary stays unless this cut writes a newer one. A cut never
  // removes it (it is never selectable), so it is the newest of all entries.
  std::size_t newest = npos;
  if (!boundary)
    for (const auto &unit : input.units) {
      const auto first = unit.entries.front();
      if (unit.unit_class == UnitClass::note &&
          note_is(item_at(entries, first), boundary_marker) &&
          (newest == npos || first > newest))
        newest = first;
    }
  std::vector<std::size_t> out;
  for (std::size_t u = 0; u < input.units.size(); ++u) {
    const auto &unit = input.units[u];
    if (unit.pinned || unit.open || unit.entries.front() < p ||
        std::any_of(unit.entries.begin(), unit.entries.end(),
                    [&](std::size_t position) { return removed[position]; }))
      continue;
    const auto &first = item_at(entries, unit.entries.front());
    bool swept = false;
    if (unit.unit_class == UnitClass::state)
      swept = true;
    else if (unit.unit_class == UnitClass::note && note_is(first, archive_note_marker))
      swept = notes;
    else if (unit.unit_class == UnitClass::note && note_is(first, boundary_marker))
      swept = unit.entries.front() != newest;
    else if (unit.unit_class == UnitClass::round && retaining) {
      bool any = false, reads = true;
      for (const auto position : unit.entries) {
        const auto &item = item_at(entries, position);
        if (!type_is(item, "function_call"))
          continue;
        any = true;
        const auto *name = text_field(item, "name");
        const auto *arguments = item.find("arguments");
        reads = reads && name &&
                (*name == "context_view" || *name == "context_stats" ||
                 (*name == "context_budget" && !(arguments && arguments->find("proposal"))));
      }
      swept = any && reads;
    }
    if (swept)
      out.push_back(u);
  }
  return out;
}

namespace {
// The frame every composer fills: revision, the head it read, chapter and cause.
CutFrame frame_for(const CutInput &input, CutKind kind, const ChapterBinding &binding,
                   CutSource source) {
  CutFrame frame;
  frame.kind = kind;
  frame.revision = identity_text(input.revision);
  frame.base = input.head;
  frame.observed = input.head;
  const auto ordinal = input.chapter.started() ? input.chapter.table.back().ordinal + 1 : 1;
  frame.chapter = Value::object(
      {{"ordinal", number(ordinal)},
       {"previous",
        input.chapter.started() ? Value{input.chapter.table.back().revision} : Value{}},
       {"binding", encode_binding(binding)}});
  frame.cause.source = source;
  frame.cause.attempt = input.attempt;
  frame.cause.stage = input.stage;
  return frame;
}
std::uint64_t sum_before(const std::vector<std::uint64_t> &sizes, std::size_t end) {
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < std::min(end, sizes.size()); ++i)
    total += sizes[i];
  return total;
}
void fill_sizes(CutFrame &frame, const std::vector<std::uint64_t> &sizes, std::size_t p) {
  frame.sizes = {sum_before(sizes, sizes.size()), sum_before(sizes, p)};
}
// The family a reasoning item belongs to, against a target protocol.
bool foreign_reasoning(const Value &item, std::string_view protocol) {
  const bool anthropic = item.find("anthropic_content") != nullptr;
  if (protocol == "messages")
    return !anthropic;
  if (anthropic)
    return true;
  if (protocol == "responses-portable") {
    const auto *encrypted = text_field(item, "encrypted_content");
    return encrypted && !encrypted->empty();
  }
  return false;
}
// Harness-required cuts remove only reasoning: dropped holds the positions.
ComposeResult harness_cut(const CutInput &input, CutKind kind,
                          const ChapterBinding &binding, std::vector<bool> dropped,
                          std::size_t p, bool forced, const Rules &rules) {
  const auto &entries = input.entries;
  if (rules.at_cut == "drop_after_change")
    for (std::size_t i = p; i < entries.size(); ++i)
      if (type_is(item_at(entries, i), "reasoning"))
        dropped[i] = true;
  // Design §6.2, must_keep: "a cut that would drop reasoning on a retained assistant
  // message is refused", forced or not.
  if (rules.at_cut == "must_keep" &&
      std::find(dropped.begin(), dropped.end(), true) != dropped.end())
    return std::unexpected(refusal("must-keep-reasoning"));
  auto frame = frame_for(input, kind, binding,
                         kind == CutKind::rebind ? CutSource::rebind : CutSource::prefix);
  frame.cause.forced = forced;
  const auto sizes = sizes_of(entries);
  std::vector<ContextRef> order;
  order.reserve(entries.size());
  std::uint64_t kept = 0;
  for (std::size_t i = 0; i < entries.size(); ++i)
    if (dropped[i])
      frame.reasoning.push_back(entry_ref(entries[i]));
    else {
      order.push_back(entry_ref(entries[i]));
      kept += sizes[i];
    }
  frame.result = CutResult{order.size(), refs_crc(order), array_bytes(order.size(), kept)};
  fill_sizes(frame, sizes, p);
  return Composed{std::move(frame), p, std::nullopt};
}
} // namespace

ComposeResult compose_start(const CutInput &input, const ChapterBinding &binding) {
  if (input.chapter.started())
    return std::unexpected(refusal("chapter-started"));
  auto frame = frame_for(input, CutKind::start, binding, CutSource::start);
  const auto sizes = sizes_of(input.entries);
  std::vector<ContextRef> order;
  order.reserve(input.entries.size());
  for (const auto &entry : input.entries)
    order.push_back(entry_ref(entry));
  frame.result = CutResult{order.size(), refs_crc(order),
                           array_bytes(order.size(), sum_before(sizes, sizes.size()))};
  fill_sizes(frame, sizes, input.entries.size());
  return Composed{std::move(frame), input.entries.size(), std::nullopt};
}
ComposeResult compose_rebind(const CutInput &input, const ChapterBinding &binding,
                             bool forced) {
  if (!input.chapter.started())
    return std::unexpected(refusal("chapter-not-started"));
  const auto &current = input.chapter.binding;
  if (binding == current)
    return std::unexpected(refusal("binding-unchanged"));
  const auto rules = rules_of(input.profile);
  const auto &entries = input.entries;
  // Design §6.3: keep_in_family keeps reasoning within the family (the same provider
  // route and protocol family, any account or model); drop_foreign also drops it
  // under another account.
  const bool same_route =
      binding.provider == current.provider && binding.base_url == current.base_url;
  const bool same_account = same_route && binding.account == current.account;
  const bool keep_own = rules.at_rebind == "keep_in_family" ? same_route : same_account;
  std::size_t p = binding.tools_digest != current.tools_digest ||
                          binding.instructions_digest != current.instructions_digest
                      ? 0
                      : entries.size();
  std::vector<bool> dropped(entries.size(), false);
  for (std::size_t i = 0; i < entries.size(); ++i) {
    const auto &item = item_at(entries, i);
    if (!type_is(item, "reasoning"))
      continue;
    if (forced || foreign_reasoning(item, binding.protocol) || !keep_own) {
      dropped[i] = true;
      p = std::min(p, i);
    }
  }
  return harness_cut(input, CutKind::rebind, binding, std::move(dropped), p, forced,
                     rules);
}
ComposeResult compose_prefix(const CutInput &input, const ChapterBinding &binding,
                             bool forced) {
  if (!input.chapter.started())
    return std::unexpected(refusal("chapter-not-started"));
  const auto &current = input.chapter.binding;
  auto same_prefix = binding;
  same_prefix.tools_digest = current.tools_digest;
  same_prefix.instructions_digest = current.instructions_digest;
  if (!(same_prefix == current))
    return std::unexpected(refusal("binding-change"));
  if (binding == current)
    return std::unexpected(refusal("binding-unchanged"));
  const auto rules = rules_of(input.profile);
  if (rules.tool_change != "cut")
    return std::unexpected(refusal("tool-change-unsupported"));
  std::vector<bool> dropped(input.entries.size(), false);
  if (forced)
    for (std::size_t i = 0; i < input.entries.size(); ++i)
      dropped[i] = type_is(item_at(input.entries, i), "reasoning");
  return harness_cut(input, CutKind::prefix, binding, std::move(dropped), 0, forced,
                     rules);
}

namespace {
// One planned insert. `after` is the input position it naturally follows (empty:
// the head); `key` orders inserts that end up on one anchor: the position each
// stands for, then restores, summaries, edits, the note, the boundary.
struct Plan {
  std::optional<std::size_t> after;
  std::pair<std::size_t, int> key;
  std::variant<std::uint32_t, std::size_t> entry; // fresh original, or input position
  bool placed = false;                             // after is already a surviving entry
};
} // namespace

ComposeResult compose_cut(const CutInput &input, const CutRequest &request) {
  const auto &entries = input.entries;
  const auto &units = input.units;
  const auto n = entries.size();
  if (!input.chapter.started())
    return std::unexpected(refusal("chapter-not-started"));
  if (request.kind != CutKind::archival && request.kind != CutKind::recovery)
    throw Error{ErrorCode::invalid_range};
  if (request.archive.empty() && request.summaries.empty() && request.restores.empty() &&
      request.edits.empty() && !request.boundary)
    return std::unexpected(refusal("empty"));
  const auto rules = rules_of(input.profile);
  std::vector<std::size_t> unit_of(n, npos);
  for (std::size_t u = 0; u < units.size(); ++u)
    for (const auto position : units[u].entries) {
      if (position >= n || unit_of[position] != npos)
        throw Error{ErrorCode::invalid_range};
      unit_of[position] = u;
    }
  if (std::find(unit_of.begin(), unit_of.end(), npos) != unit_of.end())
    throw Error{ErrorCode::invalid_range};
  const auto first_of = [&](std::size_t u) { return units[u].entries.front(); };
  const auto last_of = [&](std::size_t u) { return units[u].entries.back(); };
  const auto ids_of = [&](std::size_t u) {
    std::vector<std::string> ids;
    for (const auto position : units[u].entries)
      ids.push_back(text_of(member(entries[position], "id")));
    return ids;
  };
  // The newest live boundary is never selectable (design §5.2).
  std::size_t newest = npos;
  for (std::size_t i = 0; i < n; ++i)
    if (units[unit_of[i]].unit_class == UnitClass::note &&
        note_is(item_at(entries, i), boundary_marker))
      newest = i;
  const bool removing =
      !request.archive.empty() || !request.summaries.empty() ||
      std::any_of(request.edits.begin(), request.edits.end(),
                  [](const CutEdit &edit) { return edit.op != CutEdit::Op::insert; });
  if (rules.archival == "refused_until_probed" &&
      (removing || request.kind == CutKind::recovery ||
       std::any_of(request.restores.begin(), request.restores.end(),
                   [](const CutRestore &r) { return r.differs; })))
    return std::unexpected(refusal("archival-refused"));
  if (request.boundary && !rules.boundary_enabled)
    return std::unexpected(refusal("boundary-disabled"));
  const bool drop = rules.at_cut == "drop_after_change";
  for (const auto &restore : request.restores) {
    if (restore.items.size() != restore.classes.size() || restore.items.empty())
      throw Error{ErrorCode::invalid_range};
    // Under drop_after_change a restored unit's reasoning is not restored; a unit that
    // is only reasoning would come back as nothing while its row disappeared.
    if (drop && std::all_of(restore.items.begin(), restore.items.end(),
                            [](const Value &item) { return type_is(item, "reasoning"); }))
      return std::unexpected(refusal("restore-reasoning-only", restore.handle));
  }

  // 0 alive; 1 chosen (archive, summary, remove, move); 2 swept; 3 reasoning.
  std::vector<std::uint8_t> state(n, 0);
  std::size_t p = n;
  const auto take = [&](std::size_t u) -> std::optional<CutRefusal> {
    if (u >= units.size())
      return refusal("absent");
    const auto &unit = units[u];
    if (unit.open)
      return refusal("open", unit.handle);
    if (unit.ambiguous)
      return refusal("ambiguous", unit.handle);
    if (request.protect && protected_unit(unit))
      return refusal("protected", unit.handle);
    if (newest != npos &&
        std::find(unit.entries.begin(), unit.entries.end(), newest) != unit.entries.end())
      return refusal("protected", unit.handle);
    for (const auto position : unit.entries) {
      if (state[position] != 0)
        return refusal("duplicate-unit", unit.handle);
      state[position] = 1;
    }
    p = std::min(p, first_of(u));
    return std::nullopt;
  };
  std::vector<std::size_t> chosen; // archived and removed: listed in the note
  std::vector<std::size_t> moved;
  for (const auto u : request.archive) {
    if (auto refused = take(u))
      return std::unexpected(std::move(*refused));
    chosen.push_back(u);
  }
  for (const auto &summary : request.summaries)
    if (auto refused = take(summary.unit))
      return std::unexpected(std::move(*refused));
  for (const auto &edit : request.edits)
    if (edit.op == CutEdit::Op::remove || edit.op == CutEdit::Op::move) {
      if (auto refused = take(edit.unit))
        return std::unexpected(std::move(*refused));
      (edit.op == CutEdit::Op::remove ? chosen : moved).push_back(edit.unit);
    }
  std::map<std::size_t, Value> replaced;
  for (const auto &edit : request.edits) {
    if (edit.op == CutEdit::Op::replace) {
      std::size_t position = npos;
      for (std::size_t i = 0; i < n && position == npos; ++i)
        if (entry_ref(entries[i]) == edit.entry)
          position = i;
      if (position == npos || state[position] != 0)
        return std::unexpected(refusal("absent"));
      const auto &unit = units[unit_of[position]];
      if (request.protect && protected_unit(unit))
        return std::unexpected(refusal("protected", unit.handle));
      const auto &old = item_at(entries, position);
      if (type_is(old, "reasoning"))
        return std::unexpected(refusal("reasoning-edit", unit.handle));
      if (!std::holds_alternative<Value::Object>(edit.item.value()) ||
          text_field(edit.item, "type") == nullptr) {
        if (text_field(old, "type") != nullptr)
          return std::unexpected(refusal("replace-type", unit.handle));
      } else if (!type_is(old, *text_field(edit.item, "type")))
        return std::unexpected(refusal("replace-type", unit.handle));
      if (const auto *call = text_field(old, "call_id");
          call && (!text_field(edit.item, "call_id") ||
                   *text_field(edit.item, "call_id") != *call))
        return std::unexpected(refusal("replace-call-id", unit.handle));
      // Provider-native fields are never edited: an item that keeps one is unchanged.
      if ((edit.item.find("anthropic_content") || edit.item.find("encrypted_content")) &&
          !(edit.item == old))
        return std::unexpected(refusal("provider-native-field", unit.handle));
      if (!replaced.emplace(position, edit.item).second)
        return std::unexpected(refusal("duplicate-entry", unit.handle));
      p = std::min(p, position);
    }
    if (edit.op == CutEdit::Op::insert || edit.op == CutEdit::Op::move) {
      if (edit.op == CutEdit::Op::insert) {
        const auto &item = edit.item;
        if (!std::holds_alternative<Value::Object>(item.value()) ||
            type_is(item, "function_call") || type_is(item, "function_call_output") ||
            type_is(item, "reasoning") || item.find("anthropic_content") ||
            item.find("encrypted_content"))
          return std::unexpected(refusal("invalid-item"));
      }
      if (edit.after && *edit.after >= units.size())
        return std::unexpected(refusal("absent"));
      p = std::min(p, edit.after ? last_of(*edit.after) + 1 : 0);
    }
  }
  // Restores: the live archive note whose rows name the handle.
  const auto note_unit = [&](std::size_t position) {
    return units[unit_of[position]].unit_class == UnitClass::note &&
           note_is(item_at(entries, position), archive_note_marker);
  };
  std::vector<std::size_t> restore_note;
  for (const auto &restore : request.restores) {
    std::size_t note = npos;
    for (std::size_t i = 0; i < n && note == npos; ++i)
      if (state[i] == 0 && note_unit(i))
        for (const auto &row : note_rows(item_at(entries, i)))
          if (row_handle(row) == restore.handle) {
            note = i;
            break;
          }
    restore_note.push_back(note);
    p = std::min(p, note == npos ? n : note);
  }
  // Sweeps (design §5.6): at or after p only, so p never moves. A unit the change
  // replaces is not swept: one frame cannot both replace and remove an entry.
  const bool writes_note = !chosen.empty();
  std::vector<std::size_t> absorbed;
  {
    std::vector<bool> taken(n);
    for (std::size_t i = 0; i < n; ++i)
      taken[i] = state[i] != 0 || replaced.contains(i);
    for (const auto u : sweep(input, p, taken, writes_note, request.boundary)) {
      if (units[u].unit_class == UnitClass::note && note_unit(first_of(u)))
        absorbed.push_back(first_of(u));
      for (const auto position : units[u].entries)
        state[position] = 2;
    }
  }
  std::set<std::string, std::less<>> restored;
  for (const auto &restore : request.restores)
    restored.insert(restore.handle);
  // A note a restore names, not absorbed here, loses the restored rows: replaced
  // with its header naming a remaining entry, or swept once no row remains.
  for (const auto note : restore_note) {
    if (note == npos || state[note] != 0 || replaced.contains(note))
      continue;
    const auto &item = item_at(entries, note);
    std::vector<std::string> rows;
    for (auto &row : note_rows(item))
      if (!restored.contains(row_handle(row)))
        rows.push_back(std::move(row));
    if (rows.empty()) {
      state[note] = 2;
      continue;
    }
    Value::Object fields;
    for (const auto &[key, value] : item.object())
      if (key != "content")
        fields.emplace_back(key, value);
    fields.emplace_back("content",
                        Value{Value::Array{Value::object(
                            {{"type", Value{"input_text"}},
                             {"text", Value{archive_note_text({}, rows)}}})}});
    replaced.emplace(note, Value{std::move(fields)});
  }
  // Reasoning (design §6.2): at or after p, re-inserted entries included.
  std::set<std::size_t> moved_reasoning;
  for (const auto u : moved)
    for (const auto position : units[u].entries)
      if (drop && type_is(item_at(entries, position), "reasoning")) {
        state[position] = 3;
        moved_reasoning.insert(position);
      }
  if (drop)
    for (std::size_t i = p; i < n; ++i)
      if (state[i] == 0 && type_is(item_at(entries, i), "reasoning"))
        state[i] = 3;
  std::vector<bool> gone(n);
  for (std::size_t i = 0; i < n; ++i)
    gone[i] = state[i] != 0;
  const auto sizes = sizes_of(entries);

  // Originals, in order: restored units, summaries, inserts, the note, the boundary.
  auto frame = frame_for(input, request.kind, input.chapter.binding, input.source);
  std::vector<UnitClass> classes;    // resolved, for unit keys
  std::vector<std::uint32_t> wanted; // the unit key each original should have
  std::vector<Plan> plans;
  constexpr auto own = std::numeric_limits<std::uint32_t>::max();
  const auto add = [&](Value item, UnitClass entry_class, std::uint32_t key) {
    const auto index = static_cast<std::uint32_t>(frame.originals.size());
    classes.push_back(entry_class == UnitClass::by_role ? role_class(item) : entry_class);
    frame.originals.push_back(
        {std::move(item), static_cast<std::uint8_t>(entry_class), std::nullopt});
    wanted.push_back(key == own ? index : key);
    return index;
  };
  const std::string role = note_role(rules);
  // Restored units go in at their note (after it, or in its place when it leaves);
  // without a live note, at the tail (before a boundary).
  for (std::size_t k = 0; k < request.restores.size(); ++k) {
    const auto &restore = request.restores[k];
    const auto note = restore_note[k];
    const auto after = note == npos ? (n == 0 ? std::optional<std::size_t>{}
                                              : std::optional<std::size_t>{n - 1})
                                    : std::optional<std::size_t>{note};
    std::uint32_t key = own;
    for (std::size_t j = 0; j < restore.items.size(); ++j) {
      if (drop && type_is(restore.items[j], "reasoning"))
        continue;
      const auto index = add(restore.items[j], restore.classes[j], key);
      if (key == own)
        key = index;
      plans.push_back({after, {(note == npos ? n : note + 1), 0}, index, false});
    }
  }
  const auto placed = [&](std::size_t at) -> std::optional<NotePlacement> {
    auto place = place_note(entries, gone, at, rules.note_placement);
    if (!place.placed)
      return std::nullopt;
    return place;
  };
  for (const auto &summary : request.summaries) {
    const auto index = add(note_item(role, summary_text(units[summary.unit].handle,
                                                        ids_of(summary.unit), summary.text)),
                           UnitClass::summary, own);
    const auto place = placed(first_of(summary.unit));
    if (!place)
      return std::unexpected(refusal("note-placement", units[summary.unit].handle));
    plans.push_back({place->anchor, {first_of(summary.unit), 1}, index, true});
  }
  for (const auto &edit : request.edits) {
    if (edit.op != CutEdit::Op::insert && edit.op != CutEdit::Op::move)
      continue;
    const auto after =
        edit.after ? std::optional{last_of(*edit.after)} : std::optional<std::size_t>{};
    const std::pair<std::size_t, int> key{after ? *after + 1 : 0, 2};
    if (edit.op == CutEdit::Op::insert)
      plans.push_back({after, key, add(edit.item, UnitClass::result, own), false});
    else
      for (const auto position : units[edit.unit].entries)
        if (!moved_reasoning.contains(position))
          plans.push_back({after, key, position, false});
  }
  std::optional<std::uint32_t> note_index;
  if (writes_note) {
    std::vector<std::size_t> listed = chosen;
    std::sort(listed.begin(), listed.end(),
              [&](std::size_t a, std::size_t b) { return first_of(a) < first_of(b); });
    std::vector<NoteRow> rows;
    for (const auto u : listed) {
      const auto &unit = units[u];
      NoteRow row;
      row.handle = unit.handle;
      row.ids = ids_of(u);
      std::size_t calls = 0;
      std::string call_name, message, flags;
      const Value *arguments = nullptr;
      for (const auto position : unit.entries) {
        const auto &item = item_at(entries, position);
        row.shown += sizes[position];
        if (type_is(item, "function_call")) {
          if (calls++ == 0) {
            const auto *name = text_field(item, "name");
            call_name = name ? *name : std::string{"call"};
            arguments = item.find("arguments");
          }
        } else if (type_is(item, "function_call_output")) {
          if (!flags.empty())
            continue;
          const bool failed = input.failed ? input.failed(item) : failed_output(item);
          const auto *output = item.find("output");
          const auto *exit = output ? output->find("exit_code") : nullptr;
          if (failed)
            flags = "failed";
          if (exit && std::holds_alternative<Number>(exit->value()))
            flags += (flags.empty() ? "" : " ") + std::string{"exit="} +
                     exit->number().text();
        } else if (message.empty() && !type_is(item, "reasoning"))
          message = text_content(item);
      }
      if (calls > 0) {
        row.kind = call_name + (calls > 1 ? "+" + std::to_string(calls - 1) : "");
        // The head: the first text argument, in the call's own order.
        if (arguments && std::holds_alternative<Value::Object>(arguments->value()))
          for (const auto &[key, value] : arguments->object())
            if (const auto *s = std::get_if<std::string>(&value.value())) {
              row.head = clean_head(*s);
              break;
            }
      } else {
        row.kind = unit.unit_class == UnitClass::round
                       ? std::string{"message"}
                       : std::string{class_name(unit.unit_class)};
        row.head = clean_head(message);
      }
      row.flags = flags;
      if (input.original_bytes)
        row.original = input.original_bytes(unit);
      rows.push_back(std::move(row));
    }
    std::vector<std::string> carried;
    for (const auto position : absorbed)
      for (auto &row : note_rows(item_at(entries, position)))
        if (!restored.contains(row_handle(row)))
          carried.push_back(std::move(row));
    std::size_t first = n;
    for (const auto u : chosen)
      first = std::min(first, first_of(u));
    note_index =
        add(note_item(role, archive_note_text(rows, carried)), UnitClass::note, own);
    const auto place = placed(first);
    if (!place)
      return std::unexpected(refusal("note-placement"));
    plans.push_back({place->anchor, {first, 3}, *note_index, true});
  }
  std::optional<std::uint32_t> boundary_index;
  std::optional<std::string> request_local;
  if (request.boundary) {
    if (rules.boundary_form == "request_local_user")
      request_local = std::string{}; // its text is filled with the handles below
    else {
      boundary_index =
          add(note_item(rules.boundary_form == "system_message" ? "system" : "user", {}),
              UnitClass::note, own);
      // The boundary is the last entry (design §7.3): chained after everything else
      // on the last surviving entry.
      plans.push_back({place_boundary(gone), {n + 1, 4}, *boundary_index, true});
    }
  }
  // Unit keys: written where they differ from the default grouping.
  std::optional<std::uint32_t> first_round;
  std::set<std::uint32_t> keys;
  for (std::uint32_t i = 0; i < frame.originals.size(); ++i) {
    std::uint32_t fallback = i;
    if (classes[i] == UnitClass::round) {
      if (!first_round)
        first_round = i;
      fallback = *first_round;
    }
    if (wanted[i] != fallback)
      frame.originals[i].unit = wanted[i];
    keys.insert(wanted[i]);
  }
  const auto number_text = std::to_string(handle_number(input.revision));
  const auto handle_of = [&](std::uint32_t index) {
    return "r" + number_text +
           (keys.size() > 1 ? "." + std::to_string(wanted[index]) : std::string{});
  };
  if (request.boundary) {
    // The operator's request: the latest live user-role instruction.
    std::string asked;
    for (std::size_t u = units.size(); u > 0 && asked.empty(); --u) {
      const auto &unit = units[u - 1];
      if (unit.unit_class == UnitClass::instruction && !gone[unit.entries.front()] &&
          role_is(item_at(entries, unit.entries.front()), "user"))
        asked = unit.handle;
    }
    auto text = boundary_text(asked, note_index ? handle_of(*note_index) : std::string{});
    if (boundary_index)
      frame.originals[*boundary_index].item = note_item(
          rules.boundary_form == "system_message" ? "system" : "user", std::move(text));
    else
      request_local = std::move(text);
  }
  // Anchors resolve against the context after every removal in this frame (design
  // §4.3 step 4): a natural anchor this frame removes gives way to the nearest
  // surviving entry before it.
  for (auto &plan : plans)
    if (!plan.placed) {
      plan.after = alive_at_or_before(gone, plan.after);
      plan.placed = true;
    }
  // Nothing goes inside a call group, nor after a call still waiting for its output.
  {
    const auto open = open_groups(entries, gone);
    const auto waiting = first_unanswered(entries, gone);
    for (const auto &plan : plans)
      if (plan.after && (open[*plan.after] || (waiting != npos && *plan.after >= waiting)))
        return std::unexpected(refusal("inside-tool-group"));
  }
  // Chain the inserts: per anchor, in key order.
  std::stable_sort(plans.begin(), plans.end(),
                   [](const Plan &a, const Plan &b) { return a.key < b.key; });
  std::map<std::optional<std::size_t>, ContextRef> chained;
  for (const auto &plan : plans) {
    std::optional<ContextRef> anchor;
    if (const auto found = chained.find(plan.after); found != chained.end())
      anchor = found->second;
    else if (plan.after)
      anchor = entry_ref(entries[*plan.after]);
    ContextRef ref;
    if (const auto *fresh = std::get_if<std::uint32_t>(&plan.entry)) {
      ref = {input.revision, *fresh};
      frame.insert.push_back({anchor, *fresh});
    } else {
      ref = entry_ref(entries[std::get<std::size_t>(plan.entry)]);
      frame.insert.push_back({anchor, ref});
    }
    chained[plan.after] = ref;
  }
  // The result in order: its items (for the pairing check and the codec), refs and
  // bytes (the frame's result, so the writer measures nothing again).
  std::vector<const Value *> order;
  std::vector<ContextRef> refs;
  std::uint64_t bytes = 0;
  {
    std::map<std::optional<std::size_t>, std::vector<const Plan *>> after;
    for (const auto &plan : plans)
      after[plan.after].push_back(&plan);
    const auto emit = [&](std::optional<std::size_t> anchor) {
      const auto found = after.find(anchor);
      if (found == after.end())
        return;
      for (const auto *plan : found->second)
        if (const auto *fresh = std::get_if<std::uint32_t>(&plan->entry)) {
          const auto &item = frame.originals[*fresh].item;
          order.push_back(&item);
          refs.push_back({input.revision, *fresh});
          bytes += item_bytes(item);
        } else {
          const auto position = std::get<std::size_t>(plan->entry);
          order.push_back(&item_at(entries, position));
          refs.push_back(entry_ref(entries[position]));
          bytes += sizes[position];
        }
    };
    emit(std::nullopt);
    for (std::size_t i = 0; i < n; ++i)
      if (!gone[i]) {
        if (const auto found = replaced.find(i); found != replaced.end()) {
          order.push_back(&found->second);
          bytes += item_bytes(found->second);
        } else {
          order.push_back(&item_at(entries, i));
          bytes += sizes[i];
        }
        refs.push_back(entry_ref(entries[i]));
        emit(i);
      }
  }
  // Pairing (codec-independent): every output follows its one call, and no call is
  // left unanswered that the input answered. Then the route codec's own check.
  {
    const auto faults = [](const std::vector<const Value *> &items) {
      std::set<std::string_view> open, seen; // views into items that outlive the check
      std::size_t bad = 0;
      for (const auto *item : items) {
        const auto *id = text_field(*item, "call_id");
        if (!id)
          continue;
        if (type_is(*item, "function_call")) {
          bad += seen.insert(*id).second ? 0 : 1;
          open.insert(*id);
        } else if (type_is(*item, "function_call_output"))
          bad += open.erase(*id) == 1 ? 0 : 1;
      }
      return std::pair{bad, open};
    };
    std::vector<const Value *> before;
    before.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
      before.push_back(&item_at(entries, i));
    const auto [had_bad, had_open] = faults(before);
    const auto [has_bad, has_open] = faults(order);
    if (has_bad > had_bad ||
        !std::includes(had_open.begin(), had_open.end(), has_open.begin(), has_open.end()))
      return std::unexpected(refusal("invalid-tool-protocol"));
    if (input.validate) {
      Value::Array items;
      items.reserve(order.size());
      for (const auto *item : order)
        items.push_back(*item); // shared storage: a reference count, not a copy
      try {
        input.validate(items);
      } catch (const Error &) {
        return std::unexpected(refusal("invalid-tool-protocol"));
      }
    }
  }
  // The removal lists and replacements.
  for (std::size_t i = 0; i < n; ++i) {
    if (state[i] == 1)
      frame.archived.push_back(entry_ref(entries[i]));
    else if (state[i] == 2)
      frame.swept.push_back(entry_ref(entries[i]));
    else if (state[i] == 3)
      frame.reasoning.push_back(entry_ref(entries[i]));
  }
  for (auto &[position, item] : replaced)
    frame.replace.push_back({entry_ref(entries[position]), std::move(item)});
  if (!chosen.empty())
    frame.changes |= cut_archive;
  if (!request.summaries.empty())
    frame.changes |= cut_summary;
  if (!request.restores.empty())
    frame.changes |= cut_restore;
  if (!request.edits.empty())
    frame.changes |= cut_edit;
  if (request.boundary)
    frame.changes |= cut_boundary;
  frame.result = CutResult{refs.size(), refs_crc(refs), array_bytes(refs.size(), bytes)};
  fill_sizes(frame, sizes, p);
  return Composed{std::move(frame), p, std::move(request_local)};
}
ComposeResult compose_archive(const CutInput &input, std::vector<std::size_t> units,
                              bool boundary) {
  CutRequest request;
  request.archive = std::move(units);
  request.boundary = boundary;
  return compose_cut(input, request);
}
ComposeResult compose_summary(const CutInput &input, CutSummary summary) {
  CutRequest request;
  request.summaries.push_back(std::move(summary));
  return compose_cut(input, request);
}
ComposeResult compose_restore(const CutInput &input, std::vector<CutRestore> restores) {
  CutRequest request;
  request.restores = std::move(restores);
  return compose_cut(input, request);
}
ComposeResult compose_edit(const CutInput &input, std::vector<CutEdit> edits) {
  CutRequest request;
  request.edits = std::move(edits);
  return compose_cut(input, request);
}
} // namespace blackbird
