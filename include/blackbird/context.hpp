#pragma once
#include "blackbird/context_cut.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_context.hpp"
#include "blackbird/retained_state.hpp"
#include "blackbird/value.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>

namespace blackbird {
template <class T> T unwrap(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
inline void unwrap(Result<void> result) {
  if (!result.has_value())
    throw result.error();
}
std::string read_text(const ImmutableBytes &bytes);
Result<Value> read_packet(const ImmutableBytes &bytes, ValueLimits limits = {});
// Dispatch by the declared channel, not by guessing the payload format.
Result<Value> read_application_packet(const ApplicationRecordEvent &record);
std::string hex_identity(const IdentityBytes &bytes);
const Value &field(const Value &value, std::string_view name);
const std::string &string_field(const Value &value, std::string_view name);
struct OriginalCapture {
  std::string_view label;
  std::string_view bytes;
  Value metadata;
};
class AuditLog {
public:
  explicit AuditLog(RetainedState &root) : root_(root) {}
  ApplicationRecordId issue() { return unwrap(root_.issue<ApplicationRecordId>()); }
  std::string record(ApplicationChannel channel, const Value &packet);
  // Every record (identity reservations included) between begin_reserved and
  // end_reserved goes outside the resident-tail cap: a read-only open, whose
  // snapshot is withheld, must never fail on the cap before /repair.
  void begin_reserved();
  void end_reserved() noexcept { reserved_.reset(); }
  RecordReference record(ApplicationRecordId identity, ApplicationChannel channel,
                         const Value &packet,
                         RawContextPublication *publication = nullptr);
  // Atomically append managed context plus a native successful-boundary program record.
  JournalCursor record_boundary(ApplicationRecordId identity,
                                const Value &context_packet,
                                const Value &program_packet,
                                RawContextPublication *publication = nullptr);
  std::string original(OriginalCapture capture);
  std::optional<SourceReference> original(ApplicationRecordId identity,
                                          OriginalCapture capture);
  SourceReference retain_program(std::string_view source,
                                 DefinitionGenerationId generation);
  Value inspect(const Value &query, std::optional<OperationAttemptId> attempt = {});
  Value trajectory(const Value &query, std::optional<OperationAttemptId> attempt = {});
  Value variables(const Value &query, std::optional<OperationAttemptId> attempt = {});
  RetainedState &root() noexcept { return root_; }

private:
  RetainedState &root_;
  std::optional<RetainedState::MaintenanceScope> reserved_;
};
enum class ContextChangeKind : std::uint8_t { edited, restored, managed };
// One published context revision: an edit, a restore or an activated managed
// proposal (mode is its proposal mode). Sizes are encoded input bytes, as stats;
// a size that could not be measured is empty, never a reason to fail publication.
struct ContextChange {
  ContextChangeKind kind = ContextChangeKind::edited;
  std::string revision, mode;
  std::optional<std::size_t> bytes_before, bytes_after;
};
// How an open established the context (CONTEXT_LIFECYCLE.md §4.5).
struct ContextOpenReport {
  enum class Playback : std::uint8_t {
    journal,      // no snapshot existed: the whole journal was played back
    suffix,       // the snapshot was restored and the frames after it played back
    whole_history // a snapshot was refused: whole-history playback from fact 0
  };
  Playback playback = Playback::journal;
  // The refusal: the shared validator's (by the loader or the store) or the
  // store's resolution of the snapshot against the journal.
  std::optional<Error> snapshot_refusal;
  bool refused_by_store = false;
  // Every slot the loader refused (slot, reason), a catalogue that does not open
  // included; and whether the older accepted slot was restored instead.
  std::vector<std::pair<unsigned, Error>> slot_refusals;
  // Every slot the store refused when it restored it, with its slot.
  std::vector<std::pair<unsigned, Error>> store_refusals;
  // Accepted slots whose archive catalogue did not open; restored_slot says
  // whether that slot was still the one used.
  std::vector<std::pair<unsigned, Error>> catalogue_faults;
  std::optional<unsigned> restored_slot;
  bool older_slot = false;
  // Anything to report as a recovery: a refused slot or snapshot, a catalogue
  // fault, or read-only.
  bool recovery() const noexcept {
    return snapshot_refusal || !slot_refusals.empty() || !catalogue_faults.empty() ||
           read_only;
  }
  std::uint64_t frames = 0;  // context frames played back
  std::uint64_t skipped = 0; // frames named by repair frames
  // The read-only state: playback stopped at this frame.
  bool read_only = false;
  std::string bad_revision;
  Error bad_error{ErrorCode::corrupt};
  std::uint64_t after_bad = 0; // later context frames not played
};
struct ContextOpen {
  // Run the open-time maintenance snapshot later, with finish_open().
  bool defer_checkpoint = false;
  // Ignore the snapshot and play back the whole history (tests, inspection).
  bool whole_history = false;
  // Install no maintenance hook and write nothing at open (a second reader).
  bool probe = false;
  // Automatic whole-history playback after a refused snapshot is allowed up to
  // this journal size, as the launcher's full replay (src/main.cpp).
  std::uint64_t whole_history_limit = 64ULL * 1024 * 1024;
};
class ContextStore {
public:
  // The highest context-format-level this binary reads and writes. Level 1: cut
  // and repair frames.
  static constexpr std::uint64_t format_level = 1;
  // Error{unsupported} details from the open: the session's level is higher than
  // format_level, or whole-history playback exceeds ContextOpen's bound.
  static constexpr std::int64_t level_refused = 1, whole_history_refused = 2;
  explicit ContextStore(AuditLog &log, RawContextOrigin origin = {});
  ContextStore(AuditLog &log, ContextOpen open, RawContextOrigin origin = {});
  // Opens without the open-time maintenance snapshot. The caller runs it with
  // finish_open() once it has recorded what the open found and validated its
  // invocation, so a refused invocation cannot move the saved boundary.
  struct DeferOpenCheckpoint {};
  ContextStore(AuditLog &log, DeferOpenCheckpoint, RawContextOrigin origin = {});
  void finish_open(RawContextOrigin origin = {});
  ~ContextStore();
  ContextStore(const ContextStore &) = delete;
  ContextStore &operator=(const ContextStore &) = delete;
  Result<void> checkpoint(RawContextOrigin origin = {});
  const std::optional<Error> &checkpoint_error() const noexcept {
    return checkpoint_error_;
  }
  const std::string &head() const noexcept { return head_; }
  const ContextOpenReport &open_report() const noexcept { return report_; }
  // After a frame that does not apply: no context writes, no snapshot, until an
  // approved repair frame is accepted.
  bool read_only() const noexcept { return read_only_; }
  // Originals of the skipped frames, {id, item}, that a repair may carry.
  Value::Array skipped_originals() const;
  // Writes the repair frame: it skips the bad frame and every later context frame,
  // continues from the last good head and appends copies of the picked originals.
  Value repair(const Value::Array &picks, RawContextOrigin origin = {});
  // What /repair would do: the bad frame, the revisions it skips, the pairing
  // faults the head already has (kept as they are), and the skipped originals it
  // may keep, each with whether it alone adds no pairing fault. Null when the
  // context is not read-only.
  Value repair_preview() const;
  // Error{conflict} detail: the picks add an unpaired call or output the head did
  // not already have.
  static constexpr std::int64_t repair_unpaired = 3;
  // The cut writer (design §4.3). It applies the frame through the shared apply,
  // measures the result and records it. Staging and settlement come with S3b; S1
  // has no production caller.
  Value cut(CutFrame frame, RawContextOrigin origin = {});
  // The value a registered snapshot section held at open, or null.
  const Value *restored_section(std::string_view name) const noexcept;
  // Snapshot section writers, one per owning unit; each name must be registered.
  struct SectionWriter {
    std::string name;
    std::function<Value()> write;
  };
  std::vector<SectionWriter> section_writers;
  Value view(RawContextOrigin origin = {}) const;
  Value stats(RawContextOrigin origin = {}) const;
  Value::Array items(RawContextOrigin origin = {}) const;
  void append(Value::Array items, std::string_view label, RawContextOrigin origin = {});
  static void validate_successor_seed(const Value &seed, RawContextOrigin origin = {});
  void seed_successor(const Value &seed, RawContextOrigin origin = {});
  // strict (the model's context_edit and Lua's blackbird.edit): a new or changed
  // entry with a key other than id, item and an array ancestry is refused, reason
  // "unknown-entry-key". An entry byte-identical to the current entry under its ID
  // passes through unchanged, legacy keys included.
  enum class EditKeys : std::uint8_t { carried, strict };
  Value edit(const Value &candidate, RawContextOrigin origin = {},
             EditKeys keys = EditKeys::carried);
  void restore(std::string_view entry, RawContextOrigin origin = {});
  Value originals(RawContextOrigin origin = {}) const;
  Value manage(const Value &proposal, RawContextOrigin origin = {});
  Value inspect(const Value &query, RawContextOrigin origin = {}) const;
  // Native workflow owner validates prospective obligations before publication.
  std::function<void(const Value::Array &, const Value &, const RawEventRef &)> protect;
  // Presentation of each published revision, after its record is complete. A
  // listener failure never affects the publication.
  std::function<void(const ContextChange &)> changed;
  static Value::Array stop_outputs(const Value::Array &entries, bool interrupted);
  JournalCapacity cancellation_budget(const Value::Array &entries,
                                      const Value &proposal,
                                      RawContextOrigin origin = {}) const;
  Value pending_proposal(RawContextOrigin origin = {}) const;
  void begin_workflow(RawContextOrigin origin = {});
  Value finish_workflow(bool success, const Value &boundary_program = Value{},
                        RawContextOrigin origin = {},
                        WorkflowAcceptance *acceptance = nullptr);

private:
  void restore_snapshot(const SavedState &saved, RawContextTrace &trace);
  void play_facts(RawContextTrace &trace, const SavedState *restored);
  void play_frame(const RetainedFact &fact, const ApplicationRecordEvent &record,
                  RawContextTrace &trace);
  // differing_ for next, against entries_: an entry sharing storage with, or
  // encoding exactly like, its predecessor keeps its status; any other is compared
  // with its original's exact encoding. Computed before a record is written and
  // assigned after it, so a failed read never follows a write.
  // created: the frame's own fresh entries, equal to their originals by
  // construction (their originals are merged only after the record).
  std::set<std::string>
  settle(const Value::Array &next, RawContextOrigin origin,
         const std::map<std::string, Value> *created = nullptr) const;
  std::optional<std::vector<std::byte>>
  original_bytes(const std::string &id, std::map<IdentityBytes, Value> &decoded,
                 RawContextOrigin origin) const;
  // A frame that applies nothing: named by an accepted repair, or set aside in
  // the read-only state.
  bool skipped_frame(std::string_view revision) const noexcept;
  // The one apply shared by the writers and playback: apply_frame, then every
  // entry has an original (fresh ones from the frame).
  Value::Array apply_cut(const CutFrame &frame, const Value::Array &entries,
                         const std::map<std::string, Value> &fresh,
                         RawContextOrigin origin) const;
  void require_writable() const;
  void write_format_level(RawContextTrace &trace);
  static Value recordable(const CutFrame &frame);
  ContextOpen open_;
  ContextOpenReport report_;
  bool read_only_ = false;
  std::uint64_t level_ = 0;
  std::vector<std::string> skipped_revisions_;
  // Revisions accepted repair frames skip: from the snapshot and playback.
  std::set<std::string> skipped_;
  // Live entries whose encoding differs from their original's: written inline at a
  // checkpoint; every other entry is a ref (no decode at checkpoint).
  std::set<std::string> differing_;
  // Archive lookups of originals reach records up to this boundary: the snapshot
  // this store restored or last wrote, never a newer slot it did not restore.
  std::optional<JournalCursor> lookup_boundary_;
  std::vector<Value> skipped_originals_;
  Value::Array restored_sections_;
  std::optional<std::size_t> input_bytes() const noexcept;
  void notify(std::optional<ContextChange> change) const noexcept;
  ContextChangeKind next_kind_ = ContextChangeKind::edited;
  std::optional<Value> original_entry(std::string_view id,
                                      RawContextOrigin origin = {}) const;
  void maybe_checkpoint(RawContextOrigin origin = {});
  void refresh_history() const;
  RawContextState raw_state() const noexcept;
  mutable bool history_stale_ = false;
  mutable bool archive_loaded_ = true;
  std::optional<Error> checkpoint_error_;
  AuditLog &log_;
  std::weak_ptr<void> root_lifetime_;
  mutable RetainedState::FactHistory historical_;
  std::string head_;
  Value::Array entries_;
  struct Original {
    ImmutableBytes payload;
    std::size_t index = 0;
    bool packet = false;
    std::string id;
    std::optional<RecordReference> record;
    std::optional<ApplicationRecordId> application;
    Original(const Value &entry, std::optional<ApplicationRecordId> application = {});
    Original(ImmutableBytes source, std::size_t ordinal, std::string identity,
             RecordReference reference, ApplicationRecordId application_identity)
        : payload(std::move(source)), index(ordinal), packet(true),
          id(std::move(identity)), record(reference),
          application(application_identity) {}
    Value entry(RawContextOrigin origin) const;
    // The whole creating packet, decoded once (packet-backed originals only).
    Value packet_value(RawContextOrigin origin) const;
  };
  mutable std::map<std::string, Original> originals_;
  mutable std::vector<Original> captured_;
  Value::Array captured_entries(RawContextOrigin origin = {}) const;
  bool workflow_ = false;
  struct Pending {
    Value proposal;
    Value::Array snapshot;
    std::string expected, stage;
  };
  std::optional<Pending> pending_;
  Value publish_managed(const Value &proposal, const Value::Array &basis,
                        std::string_view stage, const Value &boundary_program = Value{},
                        RawContextOrigin origin = {},
                        WorkflowAcceptance *acceptance = nullptr);
  Value reject_managed(const Value &proposal, std::string_view reason,
                       RawContextOrigin origin = {}, bool *settled = nullptr);
  bool valid_entries(const Value &entries, RawContextOrigin origin) const;
  void append_impl(Value::Array items, std::string_view label, const Value *lineage,
                   RawContextOrigin origin);
};
} // namespace blackbird
