#pragma once
#include "blackbird/context_cut.hpp"
#include "blackbird/foundation.hpp"
#include "blackbird/value.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Units, classes, handles and selections (CONTEXT_LIFECYCLE.md §3, §5.1-§5.4,
// §11.1). The class of an entry belongs to the record that created it, never to the
// entry, so an edit cannot change it; the model selects whole units by handle,
// range or filter, and protected units are never selected.
namespace blackbird {
// The persisted class bytes; never renumbered. by_role appears only in records
// (cut and repair originals): it is resolved per entry by role at derivation.
enum class UnitClass : std::uint8_t {
  by_role = 0,
  instruction = 1,
  round = 2,
  result = 3,
  state = 4,
  summary = 5,
  note = 6,
  tool_state = 7
};
// "instruction", "round", "result", "state", "summary", "note", "tool_state"
// ("by_role" for the record-only byte).
std::string_view class_name(UnitClass value) noexcept;
std::optional<UnitClass> class_named(std::string_view name) noexcept;
// The class byte of a record; empty outside 0-7.
std::optional<UnitClass> class_byte(std::uint64_t byte) noexcept;
// The legacy role rule: user, system and developer messages are instructions;
// reasoning, function calls and assistant messages belong to a round; anything
// else is a result.
UnitClass role_class(const Value &item) noexcept;

// What one creating record made: the class of each of its entries and the unit key
// each belongs to. Round-class entries of one record share one key (the first
// round ordinal; seeded records: the first ordinal of their source round); every
// other entry is its own unit, keyed by its ordinal.
struct RecordClasses {
  std::uint32_t count = 0;
  UnitClass uniform = UnitClass::result; // when per_ordinal is empty
  std::vector<UnitClass> per_ordinal;    // one per ordinal, or empty
  std::vector<std::uint32_t> keys;       // one per ordinal, or empty (derived)
  UnitClass at(std::uint32_t ordinal) const noexcept;
  std::uint32_t key(std::uint32_t ordinal) const noexcept;
  // Distinct unit keys: more than one gives every handle of the record ".k".
  std::size_t key_count() const;
  bool operator==(const RecordClasses &) const = default;
};
// One class per item (or one for all), by_role resolved from the items; seed keys
// give the unit key of each ordinal of a seeded record. Canonical: a record whose
// entries share one class and the default keys is stored uniform.
RecordClasses record_classes(std::vector<UnitClass> classes, const Value::Array &items,
                             std::vector<std::uint32_t> seed_keys = {});
// The classes of every entry a creating record made, from that record alone: its
// op, origin label, recorded class, originals and seed lineage (design §5.2). The
// writer calls it on the packet it records and playback on the packet it reads, so
// the two cannot disagree. A lua.synthetic append without a class field (written
// before S2) is classed by role; a seed lineage that does not list one source entry
// per original is classed by role too. Throws Error{corrupt} for a recorded class
// byte outside its set.
RecordClasses derive_classes(const Value &packet);
// A class a program may declare for an append: instruction or result, never on
// function_call, function_call_output or reasoning items (calls and reasoning form
// a round with their outputs). Throws invalid_range.
void check_declared_class(UnitClass declared, const Value::Array &items);
// The class an append records: the declared one (checked), or for lua.synthetic its
// default (round when it holds function_call or reasoning items, else result), so
// that a lua.synthetic record without one is known to be pre-S2. Empty for every
// other label, whose class follows from the label.
std::optional<UnitClass> recorded_class(std::string_view label,
                                        const Value::Array &items,
                                        std::optional<UnitClass> declared);

// The resident map from live creating record to its classes, and its snapshot
// section `classes` ([[revision, count, class] or [revision, [class...], [key...]]]).
inline constexpr std::string_view classes_section = "classes";
class ClassMap {
public:
  const RecordClasses *find(const IdentityBytes &record) const noexcept;
  void set(const IdentityBytes &record, RecordClasses classes);
  // Keeps only the listed records (the live ones, at a checkpoint).
  void retain(const std::set<IdentityBytes> &live);
  // Moves other's records in without allocating (a record already held stays): a
  // writer builds its record's entry before its journal write and merges after.
  void merge(ClassMap &other) noexcept { records_.merge(other.records_); }
  void clear() noexcept { records_.clear(); }
  std::size_t size() const noexcept { return records_.size(); }
  Value section() const;
  // Throws Error{corrupt} for any value validate_classes_section refuses.
  static ClassMap read_section(const Value &value);
  bool operator==(const ClassMap &) const = default;

private:
  std::map<IdentityBytes, RecordClasses> records_;
};
Result<void> validate_classes_section(const Value &value) noexcept;

// One unit of the live context (design §3).
struct Unit {
  ContextRef key; // the creating record and the unit key within it
  std::string handle;
  std::uint64_t number = 0; // the issuer counter: the handle's number
  UnitClass unit_class = UnitClass::result;
  std::vector<std::size_t> entries; // positions in the entry list, ascending
  // A function_call no live output answers.
  bool open = false;
  // An instruction whose items encode exactly as a later live instruction unit's.
  bool superseded = false;
  bool pinned = false;
  // Another live record has the same counter (a different issuer namespace).
  bool ambiguous = false;
};
// The issuer counter of a record identity: bytes 8-15, little-endian.
std::uint64_t handle_number(const IdentityBytes &record) noexcept;
using ClassResolver = std::function<const RecordClasses &(const IdentityBytes &)>;
// The units of entries ({id, item} objects), in order of their first entry. A
// function_call_output whose call_id names a live function_call joins that call's
// unit wherever it sits; every other entry belongs to its record's unit key. Throws
// Error{corrupt} for an entry whose ID is not a ref.
// An item's encoded size and CRC32C: equal items have equal prints; the superseded
// comparison confirms a match on the exact encodings.
using ItemPrint = std::pair<std::uint64_t, std::uint32_t>;
ItemPrint item_fingerprint(const Value &item);
// The print of the item at a position; a store keeps them so units() does not
// encode every instruction on every call.
using ItemFingerprint = std::function<ItemPrint(std::size_t position)>;
std::vector<Unit> units(const Value::Array &entries, const ClassResolver &classes,
                        const std::set<ContextRef> &pins = {},
                        const ItemFingerprint &fingerprint = {});
// Never selected for archive, summary or removal: an instruction that is not a
// superseded copy, tool state, or a pinned unit.
bool protected_unit(const Unit &unit) noexcept;
// An instruction unit the model may archive (§23 decision 2).
inline bool superseded_copy(const Unit &unit) noexcept {
  return unit.unit_class == UnitClass::instruction && unit.superseded;
}

// A selection (design §11.1): comma-separated items, each a handle (r12, r4821.1),
// a range (r12-r40: every unit whose handle number lies in it, inclusive) or filter
// terms separated by spaces, all of which must hold: tool=NAME (the unit has a call
// to NAME), kind=CLASS, superseded, failed (an output that is an object with an
// error key or a nonzero numeric exit_code), before=rN (handle number below N).
struct UnitSelectionItem {
  enum class Kind : std::uint8_t { handle, range, filter };
  Kind kind = Kind::filter;
  std::string text; // a handle as written
  std::uint64_t first = 0, last = 0;
  std::optional<std::string> tool;
  std::optional<UnitClass> unit_class;
  bool superseded = false, failed = false;
  std::optional<std::uint64_t> before;
};
struct UnitSelection {
  std::vector<UnitSelectionItem> items;
};
struct UnitSelectionError {
  std::size_t at = 0; // byte offset of the item that does not parse
  std::string reason;
};
std::variant<UnitSelection, UnitSelectionError> parse_selection(std::string_view text);
enum class ExpandPurpose : std::uint8_t { archive, pin, unpin };
struct UnitRefusal {
  std::string unit; // a handle
  // archive: open, protected, pinned, ambiguous; unpin: not-pinned, ambiguous;
  // any purpose: absent (a handle that names no live unit).
  std::string reason;
  bool operator==(const UnitRefusal &) const = default;
};
struct Expansion {
  std::vector<std::size_t> units; // indices into the units, ascending
  // Matched units refused, in context order, then absent handles in selection order.
  std::vector<UnitRefusal> refused;
};
// Expands a selection over live units. For archive, open, protected (an
// instruction that is not a superseded copy, or tool state) and pinned units are
// refused, in that order of reasons; pin accepts any live unit; unpin accepts
// pinned units. An ambiguous unit is refused for every purpose.
Expansion expand(const UnitSelection &selection, const std::vector<Unit> &units,
                 const Value::Array &entries, ExpandPurpose purpose);
} // namespace blackbird
