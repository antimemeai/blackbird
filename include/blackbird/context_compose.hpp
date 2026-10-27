#pragma once
#include "blackbird/context_cut.hpp"
#include "blackbird/context_units.hpp"
#include "blackbird/value.hpp"
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Cut composition (CONTEXT_LIFECYCLE.md §5.5, §5.6, §6, §7.3): what a non-append
// change of the context becomes as a cut frame. Composers are pure: they read the
// live entries, their units and the effective provider profile, and return a frame
// of refs and new originals (never copies of retained entries) that the one shared
// apply turns into the next entries. When a change may happen is S3b's.
namespace blackbird {
// The chapter binding (design §4.2): what a chapter's provider prefix and hidden
// reasoning are bound to. Digests come from tools_digest and instructions_digest
// (provider_profile.hpp).
struct ChapterBinding {
  std::string provider, protocol, base_url, account, model;
  std::string profile_id;
  std::uint64_t profile_revision = 0;
  std::string profile_digest;
  std::string tools_digest, instructions_digest;
  bool operator==(const ChapterBinding &) const = default;
};
Value encode_binding(const ChapterBinding &binding);
// Throws Error{corrupt} unless the value has exactly the binding's fields.
ChapterBinding decode_binding(const Value &value);

// The current chapter and the chapter table, kept by the context store from the cut
// frames it applies, and saved as the snapshot section `chapter`
// ({binding, table: [[revision, ordinal, kind]]}).
struct ChapterRow {
  std::string revision;
  std::uint64_t ordinal = 0;
  CutKind kind = CutKind::start;
  bool operator==(const ChapterRow &) const = default;
};
struct ChapterState {
  ChapterBinding binding;
  std::vector<ChapterRow> table; // oldest first; the last row is the current chapter
  bool started() const noexcept { return !table.empty(); }
  bool operator==(const ChapterState &) const = default;
};
inline constexpr std::string_view chapter_section = "chapter";
Value chapter_section_value(const ChapterState &state);
// Throws Error{corrupt} for any value validate_chapter_section refuses.
ChapterState read_chapter_section(const Value &value);
Result<void> validate_chapter_section(const Value &value) noexcept;
// The chapter an accepted frame starts, checked against the current one without
// changing it; empty for a frame whose chapter is null. Throws Error{corrupt} when the
// chapter does not follow: ordinal + 1 and previous naming the current chapter
// (start: ordinal 1, previous null, no current chapter). The writer checks before its
// record and appends after it; playback appends after the frame applies, so no table
// is copied per cut.
struct ChapterStep {
  ChapterRow row;
  ChapterBinding binding;
};
std::optional<ChapterStep> chapter_step(const ChapterState &state, const CutFrame &frame);
// chapter_step, then appends the row and takes the binding.
void advance_chapter(ChapterState &state, const CutFrame &frame);

// Why a composer refused, with the unit it concerns when there is one.
struct CutRefusal {
  std::string reason;
  std::string unit;
  bool operator==(const CutRefusal &) const = default;
};
// What a composer reads. Everything is borrowed for the call.
struct CutInput {
  const Value::Array &entries;    // live entries, {id, item}
  const std::vector<Unit> &units; // units(entries, ...)
  const Value &profile;           // the effective provider profile
  const ChapterState &chapter;
  // The identity the writer records the frame under (ContextStore::cut with it), so
  // the handles a note names are the recorded ones.
  IdentityBytes revision{};
  CutSource source = CutSource::model;
  std::string attempt, stage;
  // A failed output for the note's flags; absent, failed_output.
  std::function<bool(const Value &item)> failed;
  // A unit's original bytes where known (shaped outputs), for the note.
  std::function<std::optional<std::uint64_t>(const Unit &unit)> original_bytes;
  // The route codec's own check of a provider-visible input (its render or export
  // over the items); it throws for input the codec refuses. Composed results it
  // refuses are refused invalid-tool-protocol. Blackbird's own placements never
  // need it; model and Lua edits (a unit moved to the start, an insert) can.
  std::function<void(const Value::Array &items)> validate;
  // The context head the entries are (ContextStore::head). The frame carries it as
  // its base, and ContextStore::cut refuses a frame composed against another head.
  std::string head = {};
};
struct Composed {
  CutFrame frame;
  // The position (in the input entries) of the first entry the cut changes.
  std::size_t p = 0;
  // request_local_user: the boundary text for the cut request's tail only.
  std::optional<std::string> request_local;
};
using ComposeResult = std::expected<Composed, CutRefusal>;

// A unit put back from its originals (design §5.7): the items as originally
// recorded, their classes, and whether the originals differ from what was archived
// (an edit before archival). The live archive note whose rows name the handle is
// rewritten without it; the unit goes in at that note, or at the tail without one.
struct CutRestore {
  std::string handle; // the handle it had when it was archived
  std::vector<Value> items;
  std::vector<UnitClass> classes;
  bool differs = false;
};
struct CutSummary {
  std::size_t unit = 0; // index into the units
  std::string text;
};
// Edit operations at entry and unit level (V1 maps context_edit onto them).
struct CutEdit {
  enum class Op : std::uint8_t { replace, remove, insert, move };
  Op op = Op::replace;
  ContextRef entry{};               // replace
  std::size_t unit = 0;             // remove, move
  std::optional<std::size_t> after; // insert, move: after this unit; empty: start
  Value item;                       // replace, insert
};
struct CutRequest {
  CutKind kind = CutKind::archival; // archival or recovery
  std::vector<std::size_t> archive; // unit indices, an expansion's units
  std::vector<CutSummary> summaries;
  std::vector<CutRestore> restores;
  std::vector<CutEdit> edits;
  bool boundary = false; // close the turn with the profile's boundary form
  // Model and Lua changes may not touch instruction, tool state or pinned units
  // (design §5.3); operator changes may.
  bool protect = true;
};
// The one core: archive, summary, restore, edit and boundary in one frame.
ComposeResult compose_cut(const CutInput &input, const CutRequest &request);
ComposeResult compose_archive(const CutInput &input, std::vector<std::size_t> units,
                              bool boundary = false);
ComposeResult compose_summary(const CutInput &input, CutSummary summary);
ComposeResult compose_restore(const CutInput &input, std::vector<CutRestore> restores);
ComposeResult compose_edit(const CutInput &input, std::vector<CutEdit> edits);
// Harness-required cuts (design §6.1, §6.3, §6.4). start binds the first chapter;
// rebind changes provider, account, model or profile (digests too); prefix changes
// tools or instructions only. Both remove only reasoning; forced drops all of it.
ComposeResult compose_start(const CutInput &input, const ChapterBinding &binding);
ComposeResult compose_rebind(const CutInput &input, const ChapterBinding &binding,
                             bool forced = false);
ComposeResult compose_prefix(const CutInput &input, const ChapterBinding &binding,
                             bool forced = false);

// The anchor (input position of the entry it follows; empty: start) for an archive
// note whose natural position is `at`, given which entries survive, under
// note.placement. Never between a call and its output; under before_next_user_entry
// never right after a tool output (placed is false when no such place exists).
struct NotePlacement {
  std::optional<std::size_t> anchor;
  bool placed = true;
};
NotePlacement place_note(const Value::Array &entries, const std::vector<bool> &removed,
                         std::size_t at, std::string_view placement);
// The anchor of a boundary note: the last surviving entry; empty when none.
std::optional<std::size_t> place_boundary(const std::vector<bool> &removed);
// What §5.6 sweeps at or after p: unit indices. notes: this cut writes an archive
// note (older notes are absorbed); boundary: this cut writes a boundary.
std::vector<std::size_t> sweep(const CutInput &input, std::size_t p,
                               const std::vector<bool> &removed, bool notes,
                               bool boundary);

// Text templates.
inline constexpr std::string_view archive_note_marker = "[Blackbird archive note]";
inline constexpr std::string_view boundary_marker = "[Blackbird turn boundary]";
inline constexpr std::string_view summary_marker = "[Blackbird summary]";
// One row: handle, tool or kind, quoted argument head, flags, sizes.
struct NoteRow {
  std::string handle;
  std::string kind;
  std::string head; // unquoted, at most 60 bytes on a character boundary
  std::string flags;
  std::uint64_t shown = 0;
  std::optional<std::uint64_t> original;
  std::vector<std::string> ids = {}; // the unit's entry IDs, for the read-back call
};
// The read-back call a note or summary names: today's context_inspect with its
// required query for one entry's original. V1 replaces it with context_inspect {unit}
// (design §11.4).
std::string inspect_call_text(std::string_view entry);
std::string archive_note_text(const std::vector<NoteRow> &rows,
                              const std::vector<std::string> &absorbed = {});
std::string boundary_text(std::string_view request, std::string_view note);
std::string summary_text(std::string_view handle, const std::vector<std::string> &ids,
                         std::string_view text);
// "0.6K", "8.0K", "1.2M": bytes in units of 1024, one decimal.
std::string size_text(std::uint64_t bytes);
// Whether an item is a note of the given marker (its text starts with it).
bool note_is(const Value &item, std::string_view marker) noexcept;
// The rows of an archive note: every line after its first two.
std::vector<std::string> note_rows(const Value &item);
} // namespace blackbird
