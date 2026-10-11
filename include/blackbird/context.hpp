#pragma once
#include "blackbird/packet.hpp"
#include "blackbird/retained_state.hpp"
#include "blackbird/value.hpp"
#include <functional>
#include <map>

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
  void record(ApplicationRecordId identity, ApplicationChannel channel,
              const Value &packet);
  // Atomically append managed context plus a native successful-boundary program record.
  void record_boundary(ApplicationRecordId identity, const Value &context_packet,
                       const Value &program_packet);
  std::string original(OriginalCapture capture);
  SourceReference retain_program(std::string_view source,
                                 DefinitionGenerationId generation);
  Value inspect(const Value &query);
  Value trajectory(const Value &query);
  RetainedState &root() noexcept { return root_; }

private:
  RetainedState &root_;
};
class ContextStore {
public:
  explicit ContextStore(AuditLog &log);
  ~ContextStore();
  ContextStore(const ContextStore &) = delete;
  ContextStore &operator=(const ContextStore &) = delete;
  Result<void> checkpoint();
  const std::optional<Error> &checkpoint_error() const noexcept {
    return checkpoint_error_;
  }
  const std::string &head() const noexcept { return head_; }
  Value view() const;
  Value stats() const;
  Value::Array items() const;
  void append(Value::Array items, std::string_view origin);
  static void validate_successor_seed(const Value &seed);
  void seed_successor(const Value &seed);
  Value edit(const Value &candidate);
  void restore(std::string_view entry);
  Value originals() const;
  Value manage(const Value &proposal);
  Value inspect(const Value &query) const;
  // Native workflow owner validates prospective obligations before publication.
  std::function<void(const Value::Array &, const Value &)> protect;
  static Value::Array stop_outputs(const Value::Array &entries, bool interrupted);
  JournalCapacity cancellation_budget(const Value::Array &entries,
                                      const Value &proposal) const;
  Value pending_proposal() const;
  void begin_workflow();
  Value finish_workflow(bool success, const Value &boundary_program = Value{});

private:
  ContextStore(AuditLog &log, bool restore_saved);
  std::optional<Value> original_entry(std::string_view id) const;
  void maybe_checkpoint();
  mutable bool archive_loaded_ = true;
  std::optional<Error> checkpoint_error_;
  AuditLog &log_;
  std::weak_ptr<void> root_lifetime_;
  RetainedState::FactHistory historical_;
  std::string head_;
  Value::Array entries_;
  struct Original {
    ImmutableBytes payload;
    std::size_t index = 0;
    bool packet = false;
    std::string id;
    Original(const Value &entry);
    Original(ImmutableBytes source, std::size_t ordinal, std::string identity)
        : payload(std::move(source)), index(ordinal), packet(true),
          id(std::move(identity)) {}
    Value entry() const;
  };
  mutable std::map<std::string, Original> originals_;
  mutable std::vector<Original> captured_;
  Value::Array captured_entries() const;
  bool workflow_ = false;
  struct Pending {
    Value proposal;
    Value::Array snapshot;
    std::string expected, stage;
  };
  std::optional<Pending> pending_;
  Value publish_managed(const Value &proposal, const Value::Array &basis,
                        std::string_view stage,
                        const Value &boundary_program = Value{});
  Value reject_managed(const Value &proposal, std::string_view reason);
  bool valid_entries(const Value &entries) const;
  void append_impl(Value::Array items, std::string_view origin, const Value *lineage);
};
} // namespace blackbird
