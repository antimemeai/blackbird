#pragma once
#include "blackbird/foundation.hpp"
#include "blackbird/value.hpp"
#include <functional>
#include <string>
#include <string_view>

// The one saved-context validator (CONTEXT_LIFECYCLE.md §4.4). The snapshot loader
// (load_slot) and the ContextStore both call it, so they cannot disagree about a
// snapshot's shape. Resolution of entry refs against the journal is the
// ContextStore's; a refusal at either point leads to whole-history playback.
namespace blackbird {
// Why a saved context was refused: Error{corrupt, detail}.
enum class SavedContextFault : std::int64_t {
  not_object = 1,
  version,
  base,
  entries,
  entry,
  duplicate_id,
  live_originals,
  sections,
  unregistered_section,
  section_value,
  unknown_key,
  missing_original,
  skipped
};
// The one entry rule for the store, the edit path and both validators: an object
// with a canonical entry ID ("<32 hex>.<u32 ordinal>") and an object item. Any
// other key (ancestry, or a legacy extra) is carried verbatim, so whatever the
// store holds can be saved and restored.
bool saved_entry_shape(const Value &entry) noexcept;
// The stricter rule for new model and Lua edits (context_edit, blackbird.edit):
// only id, item and an array ancestry.
bool edit_entry_shape(const Value &entry) noexcept;
// Version 1: {version 1, base, entries, live_originals}: unique entry IDs, each
// with a live original; extra top-level keys tolerated as before. Version 2:
// {version 2, base, entries, sections[, skipped]} plus RetainedState's
// physical_records, archive_entries and semantic_coverage; each entry is a ref
// [identity, ordinal] (the entry's encoding equals its original's) or an inline
// entry (saved_entry_shape); an empty base only with no entries; skipped lists the
// revisions repair frames skip; sections [{name, value}] each pass the validator
// registered under its name. A section with no registered validator is refused.
Result<void> validate_saved_context(const Value &context) noexcept;

// A section's validator, registered by the unit that owns the section's state.
// The writer and reader stay with that owner (ContextStore::section_writers and
// ContextStore::restored_section).
using SavedSectionValidator = std::function<Result<void>(const Value &)>;
class SavedSectionRegistration {
public:
  SavedSectionRegistration(std::string name, SavedSectionValidator validator);
  ~SavedSectionRegistration();
  SavedSectionRegistration(const SavedSectionRegistration &) = delete;
  SavedSectionRegistration &operator=(const SavedSectionRegistration &) = delete;

private:
  std::string name_;
};
bool saved_section_registered(std::string_view name) noexcept;
} // namespace blackbird
