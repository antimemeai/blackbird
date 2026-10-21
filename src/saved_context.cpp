#include "blackbird/saved_context.hpp"
#include "blackbird/context_cut.hpp"
#include "blackbird/context_units.hpp"
#include <map>
#include <mutex>
#include <set>

namespace blackbird {
namespace {
struct Registry {
  std::mutex lock;
  std::map<std::string, SavedSectionValidator, std::less<>> validators;
};
// Production sections register here when their owning unit lands (S2 classes,
// S3a chapter, S3b pending, E1 review), so every reader of a snapshot (the loader
// included, in any binary) knows them.
Registry &registry() {
  static Registry instance{{},
                           {{std::string{classes_section},
                             SavedSectionValidator{validate_classes_section}}}};
  return instance;
}
Result<void> fault(SavedContextFault which) {
  return Result<void>::failure({ErrorCode::corrupt, static_cast<std::int64_t>(which)});
}
bool hex32(const Value &value) {
  const auto *s = std::get_if<std::string>(&value.value());
  return s && s->size() == 32 &&
         s->find_first_not_of("0123456789abcdef") == std::string::npos;
}
const std::string &field_text(const Value &object, std::string_view key) {
  return object.find(key)->string();
}
bool version_is(const Value &value, std::string_view text) {
  const auto *n = std::get_if<Number>(&value.value());
  return n && n->text() == text;
}
// Every live entry ID is unique: a store with two entries under one ID cannot
// address either.
template <class Key> bool unique(std::set<Key> &seen, const Key &key) {
  return seen.insert(key).second;
}
Result<void> validate_v1(const Value &context) {
  const auto *entries = context.find("entries");
  const auto *originals = context.find("live_originals");
  if (!entries || !std::holds_alternative<Value::Array>(entries->value()))
    return fault(SavedContextFault::entries);
  if (!originals || !std::holds_alternative<Value::Array>(originals->value()))
    return fault(SavedContextFault::live_originals);
  std::set<std::string> original_ids;
  for (const auto &original : originals->array()) {
    const auto *id = original.find("id");
    if (!id || !std::holds_alternative<std::string>(id->value()))
      return fault(SavedContextFault::live_originals);
    if (!unique(original_ids, id->string()))
      return fault(SavedContextFault::duplicate_id);
  }
  // The checks the store made on a v1 snapshot that need no journal: the entry
  // rule, unique IDs, and an original for every entry (the v1 writer saved one
  // for each), so the loader refuses what the store would, and the other slot
  // is used instead of whole-history playback.
  std::set<std::string> ids;
  for (const auto &entry : entries->array()) {
    if (!saved_entry_shape(entry))
      return fault(SavedContextFault::entry);
    const auto &id = entry.find("id")->string();
    if (!unique(ids, id))
      return fault(SavedContextFault::duplicate_id);
    if (!original_ids.contains(id))
      return fault(SavedContextFault::missing_original);
  }
  return Result<void>::success();
}
Result<void> validate_v2(const Value &context) {
  static const std::set<std::string_view> allowed{
      "version",         "base",
      "entries",         "sections",
      "skipped",         "physical_records",
      "archive_entries", "semantic_coverage"};
  for (const auto &[key, value] : context.object()) {
    (void)value;
    if (!allowed.contains(key))
      return fault(SavedContextFault::unknown_key);
  }
  const auto *entries = context.find("entries");
  if (!entries || !std::holds_alternative<Value::Array>(entries->value()))
    return fault(SavedContextFault::entries);
  // An empty base is the empty context before the first context frame.
  if (field_text(context, "base").empty() && !entries->array().empty())
    return fault(SavedContextFault::base);
  std::set<ContextRef> ids;
  for (const auto &entry : entries->array()) {
    ContextRef ref;
    if (std::holds_alternative<Value::Array>(entry.value())) {
      try {
        ref = decode_ref(entry);
      } catch (const Error &) {
        return fault(SavedContextFault::entry);
      }
    } else {
      const auto parsed = saved_entry_shape(entry)
                              ? parse_entry_id(entry.find("id")->string())
                              : std::nullopt;
      if (!parsed)
        return fault(SavedContextFault::entry);
      ref = *parsed;
    }
    if (!unique(ids, ref))
      return fault(SavedContextFault::duplicate_id);
  }
  // The revisions accepted repair frames skip (S1): persisted, so a skipped
  // frame's originals stay unresolvable after the repair is behind a snapshot.
  if (const auto *skipped = context.find("skipped")) {
    if (!std::holds_alternative<Value::Array>(skipped->value()))
      return fault(SavedContextFault::skipped);
    std::set<std::string> revisions;
    for (const auto &revision : skipped->array())
      if (!hex32(revision) || !unique(revisions, revision.string()))
        return fault(SavedContextFault::skipped);
  }
  const auto *sections = context.find("sections");
  if (!sections || !std::holds_alternative<Value::Array>(sections->value()))
    return fault(SavedContextFault::sections);
  std::set<std::string> names;
  for (const auto &section : sections->array()) {
    const auto *name = section.find("name");
    const auto *value = section.find("value");
    if (!std::holds_alternative<Value::Object>(section.value()) ||
        section.object().size() != 2 || !name ||
        !std::holds_alternative<std::string>(name->value()) || !value ||
        !unique(names, name->string()))
      return fault(SavedContextFault::sections);
    SavedSectionValidator validator;
    {
      std::scoped_lock hold{registry().lock};
      const auto found = registry().validators.find(name->string());
      if (found == registry().validators.end())
        return fault(SavedContextFault::unregistered_section);
      validator = found->second;
    }
    if (!validator(*value).has_value())
      return fault(SavedContextFault::section_value);
  }
  return Result<void>::success();
}
} // namespace
bool saved_entry_shape(const Value &entry) noexcept {
  const auto *id = entry.find("id");
  const auto *item = entry.find("item");
  return std::holds_alternative<Value::Object>(entry.value()) && id &&
         std::holds_alternative<std::string>(id->value()) &&
         parse_entry_id(id->string()) && item &&
         std::holds_alternative<Value::Object>(item->value());
}
bool edit_entry_shape(const Value &entry) noexcept {
  if (!saved_entry_shape(entry))
    return false;
  for (const auto &[key, value] : entry.object())
    if (key != "id" && key != "item" &&
        !(key == "ancestry" && std::holds_alternative<Value::Array>(value.value())))
      return false;
  return true;
}
Result<void> validate_saved_context(const Value &context) noexcept {
  try {
    if (!std::holds_alternative<Value::Object>(context.value()))
      return fault(SavedContextFault::not_object);
    const auto *version = context.find("version");
    const auto *base = context.find("base");
    if (!version)
      return fault(SavedContextFault::version);
    const bool v1 = version_is(*version, "1"), v2 = version_is(*version, "2");
    if (!v1 && !v2)
      return fault(SavedContextFault::version);
    if (!base || !std::holds_alternative<std::string>(base->value()) ||
        (!hex32(*base) && !(v2 && base->string().empty())))
      return fault(SavedContextFault::base);
    return v1 ? validate_v1(context) : validate_v2(context);
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  } catch (const Error &error) {
    return Result<void>::failure(error);
  } catch (...) {
    return Result<void>::failure({ErrorCode::external_unknown});
  }
}
SavedSectionRegistration::SavedSectionRegistration(std::string name,
                                                   SavedSectionValidator validator)
    : name_(std::move(name)) {
  std::scoped_lock hold{registry().lock};
  if (!validator || !registry().validators.emplace(name_, std::move(validator)).second)
    throw Error{ErrorCode::conflict};
}
SavedSectionRegistration::~SavedSectionRegistration() {
  std::scoped_lock hold{registry().lock};
  registry().validators.erase(name_);
}
bool saved_section_registered(std::string_view name) noexcept {
  std::scoped_lock hold{registry().lock};
  return registry().validators.contains(name);
}
} // namespace blackbird
