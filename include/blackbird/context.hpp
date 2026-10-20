#pragma once
#include "blackbird/packet.hpp"
#include "blackbird/raw_context.hpp"
#include "blackbird/retained_state.hpp"
#include "blackbird/value.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>

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
class ContextStore {
public:
  explicit ContextStore(AuditLog &log, RawContextOrigin origin = {});
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
  Value view(RawContextOrigin origin = {}) const;
  Value stats(RawContextOrigin origin = {}) const;
  Value::Array items(RawContextOrigin origin = {}) const;
  void append(Value::Array items, std::string_view label, RawContextOrigin origin = {});
  static void validate_successor_seed(const Value &seed, RawContextOrigin origin = {});
  void seed_successor(const Value &seed, RawContextOrigin origin = {});
  Value edit(const Value &candidate, RawContextOrigin origin = {});
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
  ContextStore(AuditLog &log, bool restore_saved, RawContextOrigin origin,
               bool defer_open_checkpoint = false);
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
