#include "blackbird/context_units.hpp"
#include "blackbird/journal.hpp"
#include "blackbird/packet.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <span>
#include <type_traits>
#include <variant>

namespace blackbird {
namespace {
[[noreturn]] void corrupt() { throw Error{ErrorCode::corrupt}; }
constexpr std::array<std::string_view, 8> class_names{
    "by_role", "instruction", "round", "result",
    "state",   "summary",     "note",  "tool_state"};
const std::string *text_member(const Value &object, std::string_view key) noexcept {
  const auto *found = object.find(key);
  return found ? std::get_if<std::string>(&found->value()) : nullptr;
}
const Value &item_of(const Value &entry) {
  const auto *item = entry.find("item");
  if (!item)
    corrupt();
  return *item;
}
bool type_is(const Value &item, std::string_view type) noexcept {
  const auto *t = text_member(item, "type");
  return t && *t == type;
}
std::optional<std::uint64_t> whole_number(const Value &value) noexcept {
  const auto *n = std::get_if<Number>(&value.value());
  if (!n)
    return std::nullopt;
  if (const auto *u = std::get_if<std::uint64_t>(&n->storage()))
    return *u;
  if (const auto *s = std::get_if<std::int64_t>(&n->storage()); s && *s >= 0)
    return static_cast<std::uint64_t>(*s);
  return std::nullopt;
}
// The label table of design §5.2; by_role for any label it does not name. A
// lua.synthetic record carries its class (S2 writes it on every one); one without
// is pre-S2, written when every user, system and developer entry was pinned, and is
// classed by role.
UnitClass label_class(std::string_view label) noexcept {
  if (label == "operator" || label == "backstop.instructions" ||
      label.starts_with("rrc:"))
    return UnitClass::instruction;
  if (label.starts_with("provider.branch:"))
    return UnitClass::round;
  if (label == "workflow-continuation" || label == "workflow.protocol-repair" ||
      label == "stop.linkage" || label == "operator.beads" ||
      label == "application.observations" || label == "initial")
    return UnitClass::result;
  if (label == "provider.request-state")
    return UnitClass::state;
  return UnitClass::by_role;
}
// The items of a record's originals, borrowed: derivation reads only type and role.
using Items = std::vector<const Value *>;
Items items_of(const Value &originals) {
  const auto *list = std::get_if<Value::Array>(&originals.value());
  if (!list)
    corrupt();
  Items items;
  items.reserve(list->size());
  for (const auto &original : *list) {
    const auto *item = original.find("item");
    if (!item || !std::holds_alternative<Value::Object>(item->value()))
      corrupt();
    items.push_back(item);
  }
  return items;
}
Items items_of_array(const Value::Array &list) {
  Items items;
  items.reserve(list.size());
  for (const auto &item : list)
    items.push_back(&item);
  return items;
}
RecordClasses classes_of(std::vector<UnitClass> classes, const Items &items,
                         std::vector<std::uint32_t> seed_keys = {});
// A seeded record (design §5.2): entries of one source unit are one round, so each
// source round stays its own unit; seeded user, system and developer entries are
// instructions, each its own unit. The source unit is the one the seed names per
// entry (lineage.source_units, written since S2); a seed without it groups by the
// source revision prefix in lineage.source_entries. Empty when the lineage does not
// list one source entry per original, so the record is classed by role instead.
std::optional<RecordClasses> seeded(const Value &packet, const Items &items) {
  const auto *lineage = packet.find("lineage");
  const auto *sources = lineage ? lineage->find("source_entries") : nullptr;
  const auto *list = sources ? std::get_if<Value::Array>(&sources->value()) : nullptr;
  if (!list || list->size() != items.size())
    return std::nullopt;
  const auto *named = lineage->find("source_units");
  const auto *units = named ? std::get_if<Value::Array>(&named->value()) : nullptr;
  if (units && units->size() != items.size())
    return std::nullopt;
  std::vector<UnitClass> classes;
  std::vector<std::uint32_t> keys;
  std::map<std::string_view, std::uint32_t> by_unit, by_prefix;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const auto *source = std::get_if<std::string>(&(*list)[i].value());
    if (!source || source->find('.') == std::string::npos)
      return std::nullopt;
    const auto ordinal = static_cast<std::uint32_t>(i);
    if (role_class(*items[i]) == UnitClass::instruction) {
      classes.push_back(UnitClass::instruction);
      keys.push_back(ordinal);
      continue;
    }
    classes.push_back(UnitClass::round);
    if (const auto *unit =
            units ? std::get_if<std::string>(&(*units)[i].value()) : nullptr)
      keys.push_back(by_unit.emplace(*unit, ordinal).first->second);
    else
      keys.push_back(
          by_prefix
              .emplace(std::string_view{source->data(), source->rfind('.')}, ordinal)
              .first->second);
  }
  return classes_of(std::move(classes), items, std::move(keys));
}
bool hex32(std::string_view text) noexcept {
  return text.size() == 32 &&
         text.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}
std::optional<IdentityBytes> identity_of(std::string_view text) noexcept {
  if (!hex32(text))
    return std::nullopt;
  const auto ref = parse_entry_id(std::string{text} + ".0");
  return ref ? std::optional{ref->record} : std::nullopt;
}
std::string identity_text(const IdentityBytes &bytes) {
  const auto id = entry_id({bytes, 0});
  return id.substr(0, 32);
}
Value number(std::uint64_t n) { return Value{Number{n}}; }
} // namespace

std::string_view class_name(UnitClass value) noexcept {
  const auto index = static_cast<std::size_t>(value);
  return index < class_names.size() ? class_names[index] : std::string_view{};
}
std::optional<UnitClass> class_named(std::string_view name) noexcept {
  for (std::size_t i = 1; i < class_names.size(); ++i)
    if (class_names[i] == name)
      return static_cast<UnitClass>(i);
  return std::nullopt;
}
std::optional<UnitClass> class_byte(std::uint64_t byte) noexcept {
  if (byte >= class_names.size())
    return std::nullopt;
  return static_cast<UnitClass>(byte);
}
UnitClass role_class(const Value &item) noexcept {
  if (const auto *role = text_member(item, "role")) {
    if (*role == "user" || *role == "system" || *role == "developer")
      return UnitClass::instruction;
    if (*role == "assistant")
      return UnitClass::round;
  }
  if (type_is(item, "reasoning") || type_is(item, "function_call"))
    return UnitClass::round;
  return UnitClass::result;
}

UnitClass RecordClasses::at(std::uint32_t ordinal) const noexcept {
  return ordinal < per_ordinal.size() ? per_ordinal[ordinal] : uniform;
}
std::uint32_t RecordClasses::key(std::uint32_t ordinal) const noexcept {
  if (ordinal < keys.size())
    return keys[ordinal];
  return at(ordinal) == UnitClass::round ? 0 : ordinal;
}
std::size_t RecordClasses::key_count() const {
  if (keys.empty())
    return uniform == UnitClass::round ? std::min<std::size_t>(count, 1) : count;
  return std::set<std::uint32_t>{keys.begin(), keys.end()}.size();
}
RecordClasses record_classes(std::vector<UnitClass> classes, const Value::Array &items,
                             std::vector<std::uint32_t> seed_keys) {
  return classes_of(std::move(classes), items_of_array(items), std::move(seed_keys));
}
namespace {
RecordClasses classes_of(std::vector<UnitClass> classes, const Items &items,
                         std::vector<std::uint32_t> seed_keys) {
  if (items.size() > std::numeric_limits<std::uint32_t>::max())
    throw Error{ErrorCode::capacity};
  RecordClasses out;
  out.count = static_cast<std::uint32_t>(items.size());
  if (classes.size() == 1 && seed_keys.empty() && classes[0] != UnitClass::by_role) {
    out.uniform = classes[0];
    return out;
  }
  if (classes.size() == 1) {
    const auto only = classes[0]; // assign must not read the vector it replaces
    classes.assign(items.size(), only);
  }
  if (classes.size() != items.size() ||
      (!seed_keys.empty() && seed_keys.size() != items.size()))
    throw Error{ErrorCode::corrupt};
  for (std::size_t i = 0; i < items.size(); ++i)
    if (classes[i] == UnitClass::by_role)
      classes[i] = role_class(*items[i]);
  if (seed_keys.empty()) {
    // Round entries share the key of the first round entry; any other entry is its
    // own unit.
    std::optional<std::uint32_t> round;
    for (std::uint32_t i = 0; i < out.count; ++i)
      if (classes[i] == UnitClass::round) {
        if (!round)
          round = i;
        seed_keys.push_back(*round);
      } else
        seed_keys.push_back(i);
  }
  // Canonical: one class with the default keys is stored uniform.
  if (!classes.empty() && std::all_of(classes.begin(), classes.end(),
                                      [&](UnitClass c) { return c == classes[0]; })) {
    bool defaults = true;
    for (std::uint32_t i = 0; i < out.count && defaults; ++i)
      defaults = seed_keys[i] == (classes[0] == UnitClass::round ? 0 : i);
    if (defaults) {
      out.uniform = classes[0];
      return out;
    }
  }
  out.per_ordinal = std::move(classes);
  out.keys = std::move(seed_keys);
  return out;
}
bool forms_round(const Items &items) noexcept {
  return std::any_of(items.begin(), items.end(), [](const Value *item) {
    return type_is(*item, "function_call") || type_is(*item, "reasoning");
  });
}
} // namespace
RecordClasses derive_classes(const Value &packet) {
  const auto *op = text_member(packet, "op");
  const auto *originals = packet.find("originals");
  if (!op || !originals)
    return {};
  const auto items = items_of(*originals);
  if (*op == "managed")
    return classes_of({UnitClass::summary}, items);
  if (*op == "cut" || *op == "repair") {
    std::vector<UnitClass> classes;
    for (const auto &original : originals->array()) {
      const auto *byte = original.find("class");
      const auto value = byte ? whole_number(*byte) : std::optional<std::uint64_t>{0};
      const auto known = value ? class_byte(*value) : std::nullopt;
      if (!known)
        corrupt();
      classes.push_back(*known);
    }
    return items.empty() ? RecordClasses{} : classes_of(std::move(classes), items);
  }
  if (*op != "append")
    return {};
  if (const auto *recorded = packet.find("class")) {
    // Declared by the program (instruction, result), or the lua.synthetic default
    // the writer recorded (result, or round for calls and reasoning).
    const auto value = whole_number(*recorded);
    const auto known = value ? class_byte(*value) : std::nullopt;
    if (!known || (*known != UnitClass::instruction && *known != UnitClass::result &&
                   *known != UnitClass::round))
      corrupt();
    return classes_of({*known}, items);
  }
  const auto *label = text_member(packet, "origin");
  if (label && *label == "session.successor")
    if (auto grouped = seeded(packet, items))
      return std::move(*grouped);
  return classes_of({label ? label_class(*label) : UnitClass::by_role}, items);
}
std::optional<UnitClass> recorded_class(std::string_view label,
                                        const Value::Array &items,
                                        std::optional<UnitClass> declared) {
  if (declared) {
    check_declared_class(*declared, items);
    return declared;
  }
  if (label != "lua.synthetic")
    return std::nullopt;
  return forms_round(items_of_array(items)) ? UnitClass::round : UnitClass::result;
}
void check_declared_class(UnitClass declared, const Value::Array &items) {
  if (declared != UnitClass::instruction && declared != UnitClass::result)
    throw Error{ErrorCode::invalid_range};
  for (const auto &item : items)
    if (type_is(item, "function_call") || type_is(item, "function_call_output") ||
        type_is(item, "reasoning"))
      throw Error{ErrorCode::invalid_range};
}

const RecordClasses *ClassMap::find(const IdentityBytes &record) const noexcept {
  const auto found = records_.find(record);
  return found == records_.end() ? nullptr : &found->second;
}
void ClassMap::set(const IdentityBytes &record, RecordClasses classes) {
  records_.insert_or_assign(record, std::move(classes));
}
void ClassMap::retain(const std::set<IdentityBytes> &live) {
  std::erase_if(records_, [&](const auto &pair) { return !live.contains(pair.first); });
}
Value ClassMap::section() const {
  Value::Array rows;
  rows.reserve(records_.size());
  for (const auto &[record, classes] : records_) {
    if (classes.per_ordinal.empty()) {
      rows.push_back(
          Value{Value::Array{Value{identity_text(record)}, number(classes.count),
                             number(static_cast<std::uint8_t>(classes.uniform))}});
      continue;
    }
    Value::Array bytes, keys;
    for (const auto c : classes.per_ordinal)
      bytes.push_back(number(static_cast<std::uint8_t>(c)));
    for (const auto k : classes.keys)
      keys.push_back(number(k));
    rows.push_back(
        Value{Value::Array{Value{identity_text(record)}, Value{std::move(bytes)},
                           Value{std::move(keys)}}});
  }
  return Value{std::move(rows)};
}
ClassMap ClassMap::read_section(const Value &value) {
  const auto *rows = std::get_if<Value::Array>(&value.value());
  if (!rows)
    corrupt();
  ClassMap out;
  const auto known = [](const Value &v) {
    const auto n = whole_number(v);
    const auto c = n ? class_byte(*n) : std::nullopt;
    if (!c || *c == UnitClass::by_role)
      corrupt();
    return *c;
  };
  for (const auto &row : *rows) {
    const auto *fields = std::get_if<Value::Array>(&row.value());
    if (!fields || fields->size() != 3)
      corrupt();
    const auto *revision = std::get_if<std::string>(&(*fields)[0].value());
    const auto record = revision ? identity_of(*revision) : std::nullopt;
    if (!record || out.records_.contains(*record))
      corrupt();
    RecordClasses classes;
    if (const auto count = whole_number((*fields)[1])) {
      if (*count > std::numeric_limits<std::uint32_t>::max())
        corrupt();
      classes.count = static_cast<std::uint32_t>(*count);
      classes.uniform = known((*fields)[2]);
    } else {
      const auto *bytes = std::get_if<Value::Array>(&(*fields)[1].value());
      const auto *keys = std::get_if<Value::Array>(&(*fields)[2].value());
      if (!bytes || !keys || bytes->empty() || bytes->size() != keys->size() ||
          bytes->size() > std::numeric_limits<std::uint32_t>::max())
        corrupt();
      classes.count = static_cast<std::uint32_t>(bytes->size());
      for (std::size_t i = 0; i < bytes->size(); ++i) {
        classes.per_ordinal.push_back(known((*bytes)[i]));
        const auto key = whole_number((*keys)[i]);
        // A unit key names an ordinal of this record at or before the entry.
        if (!key || *key > i)
          corrupt();
        classes.keys.push_back(static_cast<std::uint32_t>(*key));
      }
    }
    out.records_.emplace(*record, std::move(classes));
  }
  return out;
}
Result<void> validate_classes_section(const Value &value) noexcept {
  try {
    (void)ClassMap::read_section(value);
    return Result<void>::success();
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  } catch (const Error &error) {
    return Result<void>::failure(error);
  } catch (...) {
    return Result<void>::failure({ErrorCode::corrupt});
  }
}

std::uint64_t handle_number(const IdentityBytes &record) noexcept {
  std::uint64_t n = 0;
  for (std::size_t i = 16; i > 8; --i)
    n = (n << 8U) | std::to_integer<std::uint64_t>(record[i - 1]);
  return n;
}
std::vector<Unit> units(const Value::Array &entries, const ClassResolver &classes,
                        const std::set<ContextRef> &pins,
                        const ItemFingerprint &fingerprint) {
  const auto count = entries.size();
  std::vector<ContextRef> refs, slots;
  refs.reserve(count);
  slots.reserve(count);
  std::map<std::string_view, std::size_t> calls; // call_id -> position
  std::set<std::string_view> answered;
  for (std::size_t i = 0; i < count; ++i) {
    const auto *id = text_member(entries[i], "id");
    const auto ref = id ? parse_entry_id(*id) : std::nullopt;
    if (!ref)
      corrupt();
    refs.push_back(*ref);
    slots.push_back({ref->record, classes(ref->record).key(ref->ordinal)});
    const auto *item = entries[i].find("item");
    if (item && type_is(*item, "function_call"))
      if (const auto *call = text_member(*item, "call_id"))
        calls.emplace(*call, i);
  }
  // Membership by call ID: an output joins its live call's unit wherever it sits.
  for (std::size_t i = 0; i < count; ++i) {
    const auto *item = entries[i].find("item");
    if (!item || !type_is(*item, "function_call_output"))
      continue;
    const auto *call = text_member(*item, "call_id");
    if (const auto found = call ? calls.find(*call) : calls.end();
        found != calls.end()) {
      slots[i] = slots[found->second];
      answered.insert(*call);
    }
  }
  std::vector<Unit> out;
  std::map<ContextRef, std::size_t> index;
  std::map<std::uint64_t, std::set<IdentityBytes>> numbers;
  for (std::size_t i = 0; i < count; ++i) {
    auto [at, fresh] = index.try_emplace(slots[i], out.size());
    if (fresh) {
      const auto &record = classes(slots[i].record);
      Unit unit;
      unit.key = slots[i];
      unit.number = handle_number(slots[i].record);
      unit.unit_class = record.at(slots[i].ordinal);
      unit.handle = "r" + std::to_string(unit.number);
      if (record.key_count() > 1)
        unit.handle += "." + std::to_string(slots[i].ordinal);
      unit.pinned = pins.contains(slots[i]);
      numbers[unit.number].insert(slots[i].record);
      out.push_back(std::move(unit));
    }
    auto &unit = out[at->second];
    unit.entries.push_back(i);
    const auto &item = item_of(entries[i]);
    if (type_is(item, "function_call"))
      if (const auto *call = text_member(item, "call_id");
          call && !answered.contains(*call))
        unit.open = true;
  }
  for (auto &unit : out)
    unit.ambiguous = numbers[unit.number].size() > 1;
  // Superseded copies: an instruction whose items encode exactly as a later live
  // instruction unit's (design §5.1). Fingerprints (encoded size and CRC32C, per
  // entry) find the candidates; only a candidate pair is encoded and compared whole.
  const auto print = [&](std::size_t position) {
    if (fingerprint)
      return fingerprint(position);
    return item_fingerprint(item_of(entries[position]));
  };
  const auto encoded = [&](const Unit &unit) {
    std::vector<std::string> bytes;
    bytes.reserve(unit.entries.size());
    for (const auto position : unit.entries) {
      auto one = encode_packet_string(item_of(entries[position]));
      if (!one.has_value())
        throw one.error();
      bytes.push_back(std::move(one).value());
    }
    return bytes;
  };
  std::map<std::vector<ItemPrint>, std::vector<const Unit *>> later;
  for (auto it = out.rbegin(); it != out.rend(); ++it) {
    if (it->unit_class != UnitClass::instruction)
      continue;
    std::vector<ItemPrint> prints;
    prints.reserve(it->entries.size());
    for (const auto position : it->entries)
      prints.push_back(print(position));
    auto &same = later[std::move(prints)];
    if (!same.empty()) {
      const auto mine = encoded(*it);
      it->superseded = std::any_of(same.begin(), same.end(), [&](const Unit *other) {
        return encoded(*other) == mine;
      });
    }
    same.push_back(&*it);
  }
  return out;
}
ItemPrint item_fingerprint(const Value &item) {
  auto encoded = encode_packet_string(item);
  if (!encoded.has_value())
    throw encoded.error();
  const auto &bytes = encoded.value();
  return {bytes.size(), crc32c(std::as_bytes(std::span{bytes.data(), bytes.size()}))};
}
bool protected_unit(const Unit &unit) noexcept {
  return (unit.unit_class == UnitClass::instruction && !unit.superseded) ||
         unit.unit_class == UnitClass::tool_state || unit.pinned;
}

namespace {
constexpr std::string_view spaces = " \t";
std::string_view trim(std::string_view text) noexcept {
  const auto first = text.find_first_not_of(spaces);
  if (first == std::string_view::npos)
    return {};
  return text.substr(first, text.find_last_not_of(spaces) - first + 1);
}
// A decimal number in canonical form ("0" or no leading zero), within its type.
template <class T> std::optional<T> decimal(std::string_view digits) noexcept {
  if (digits.empty() || (digits.size() > 1 && digits[0] == '0') ||
      digits.find_first_not_of("0123456789") != std::string_view::npos)
    return std::nullopt;
  T value{};
  const auto parsed =
      std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size())
    return std::nullopt;
  return value;
}
// "r<number>", the number of a handle without its unit key.
std::optional<std::uint64_t> handle_stem(std::string_view text) noexcept {
  if (text.size() < 2 || text[0] != 'r')
    return std::nullopt;
  return decimal<std::uint64_t>(text.substr(1));
}
bool is_handle(std::string_view text) noexcept {
  const auto dot = text.find('.');
  if (dot == std::string_view::npos)
    return handle_stem(text).has_value();
  return handle_stem(text.substr(0, dot)) &&
         decimal<std::uint32_t>(text.substr(dot + 1));
}
bool failed_output(const Value &item) noexcept {
  if (!type_is(item, "function_call_output"))
    return false;
  const auto *body = item.find("output");
  if (!body || !std::holds_alternative<Value::Object>(body->value()))
    return false;
  if (body->find("error"))
    return true;
  const auto *exit = body->find("exit_code");
  const auto *n = exit ? std::get_if<Number>(&exit->value()) : nullptr;
  if (!n)
    return false;
  return std::visit(
      [](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Decimal>)
          return std::any_of(v.limbs.begin(), v.limbs.end(),
                             [](std::uint32_t limb) { return limb != 0; });
        else
          return v != T{};
      },
      n->storage());
}
} // namespace
std::variant<UnitSelection, UnitSelectionError> parse_selection(std::string_view text) {
  UnitSelection selection;
  std::size_t offset = 0;
  while (true) {
    const auto comma = text.find(',', offset);
    const auto raw =
        text.substr(offset, comma == std::string_view::npos ? std::string_view::npos
                                                            : comma - offset);
    const auto item_text = trim(raw);
    const auto at = offset;
    const auto fail = [&](std::string reason) {
      return std::variant<UnitSelection, UnitSelectionError>{
          UnitSelectionError{at, std::move(reason)}};
    };
    if (item_text.empty())
      return fail("empty item");
    UnitSelectionItem item;
    if (item_text.find_first_of(spaces) == std::string_view::npos &&
        item_text.find('=') == std::string_view::npos && item_text[0] == 'r') {
      if (const auto dash = item_text.find('-'); dash != std::string_view::npos) {
        const auto first = handle_stem(item_text.substr(0, dash));
        const auto last = handle_stem(item_text.substr(dash + 1));
        if (!first || !last)
          return fail("a range is r<number>-r<number>");
        if (*first > *last)
          return fail("a range runs from the lower handle number to the higher");
        item.kind = UnitSelectionItem::Kind::range;
        item.first = *first;
        item.last = *last;
      } else {
        if (!is_handle(item_text))
          return fail("a handle is r<number> or r<number>.<number>");
        item.kind = UnitSelectionItem::Kind::handle;
        item.text = std::string{item_text};
      }
      selection.items.push_back(std::move(item));
    } else {
      // Filter terms, all of which must hold; each term at most once.
      std::set<std::string_view> seen;
      for (std::size_t from = 0; from < item_text.size();) {
        const auto start = item_text.find_first_not_of(spaces, from);
        if (start == std::string_view::npos)
          break;
        auto end = item_text.find_first_of(spaces, start);
        if (end == std::string_view::npos)
          end = item_text.size();
        const auto term = item_text.substr(start, end - start);
        from = end;
        const auto equals = term.find('=');
        const auto name = term.substr(0, equals);
        const auto value = equals == std::string_view::npos ? std::string_view{}
                                                            : term.substr(equals + 1);
        if (!seen.insert(name).second)
          return fail("repeated filter term " + std::string{name});
        if (term == "superseded")
          item.superseded = true;
        else if (term == "failed")
          item.failed = true;
        else if (name == "tool" && equals != std::string_view::npos && !value.empty())
          item.tool = std::string{value};
        else if (name == "kind" && equals != std::string_view::npos) {
          item.unit_class = class_named(value);
          if (!item.unit_class)
            return fail("kind is one of instruction, round, result, state, summary, "
                        "note, tool_state");
        } else if (name == "before" && equals != std::string_view::npos) {
          item.before = handle_stem(value);
          if (!item.before)
            return fail("before takes a handle number, before=r<number>");
        } else
          return fail("unknown filter term " + std::string{term} +
                      "; terms are tool=NAME, kind=CLASS, superseded, failed, "
                      "before=r<number>");
      }
      item.kind = UnitSelectionItem::Kind::filter;
      selection.items.push_back(std::move(item));
    }
    if (comma == std::string_view::npos)
      break;
    offset = comma + 1;
  }
  return selection;
}
Expansion expand(const UnitSelection &selection, const std::vector<Unit> &units,
                 const Value::Array &entries, ExpandPurpose purpose) {
  std::vector<bool> matched(units.size(), false);
  std::vector<std::string> absent;
  const auto holds = [&](const Unit &unit, const UnitSelectionItem &item) {
    if (item.unit_class && unit.unit_class != *item.unit_class)
      return false;
    if (item.superseded && !superseded_copy(unit))
      return false;
    if (item.before && unit.number >= *item.before)
      return false;
    bool tool = !item.tool, failed = !item.failed;
    for (const auto position : unit.entries) {
      const auto &entry_item = item_of(entries[position]);
      if (!tool && type_is(entry_item, "function_call"))
        if (const auto *name = text_member(entry_item, "name");
            name && *name == *item.tool)
          tool = true;
      if (!failed && failed_output(entry_item))
        failed = true;
    }
    return tool && failed;
  };
  for (const auto &item : selection.items) {
    bool any = false;
    for (std::size_t u = 0; u < units.size(); ++u) {
      const auto &unit = units[u];
      const bool hit = item.kind == UnitSelectionItem::Kind::handle
                           ? unit.handle == item.text
                       : item.kind == UnitSelectionItem::Kind::range
                           ? unit.number >= item.first && unit.number <= item.last
                           : holds(unit, item);
      if (hit)
        matched[u] = any = true;
    }
    if (item.kind == UnitSelectionItem::Kind::handle && !any &&
        std::find(absent.begin(), absent.end(), item.text) == absent.end())
      absent.push_back(item.text);
  }
  Expansion out;
  for (std::size_t u = 0; u < units.size(); ++u) {
    if (!matched[u])
      continue;
    const auto &unit = units[u];
    std::string_view reason;
    if (unit.ambiguous)
      reason = "ambiguous";
    else if (purpose == ExpandPurpose::archive) {
      if (unit.open)
        reason = "open";
      else if ((unit.unit_class == UnitClass::instruction && !unit.superseded) ||
               unit.unit_class == UnitClass::tool_state)
        reason = "protected";
      else if (unit.pinned)
        reason = "pinned";
    } else if (purpose == ExpandPurpose::unpin && !unit.pinned)
      reason = "not-pinned";
    if (reason.empty())
      out.units.push_back(u);
    else
      out.refused.push_back({unit.handle, std::string{reason}});
  }
  for (auto &handle : absent)
    out.refused.push_back({std::move(handle), "absent"});
  return out;
}
} // namespace blackbird
