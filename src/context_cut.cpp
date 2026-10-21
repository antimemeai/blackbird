#include "blackbird/context_cut.hpp"
#include "blackbird/journal.hpp"
#include "blackbird/packet.hpp"
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
  for (const auto &o : frame.originals)
    originals.push_back(
        Value::object({{"item", o.item}, {"class", number(o.entry_class)}}));
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
      (void)object_item(member(frame.chapter, "binding"));
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
    only_keys(o, {"item", "class"});
    const auto entry_class = u64_of(member(o, "class"));
    if (entry_class > 255)
      corrupt();
    frame.originals.push_back(
        {object_item(member(o, "item")), static_cast<std::uint8_t>(entry_class)});
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
} // namespace blackbird
