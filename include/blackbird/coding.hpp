#pragma once
#include "blackbird/beads.hpp"
#include "blackbird/colleague.hpp"
#include "blackbird/command_jobs.hpp"
#include "blackbird/decision_models.hpp"
#include "blackbird/observations.hpp"
#include "blackbird/openai.hpp"
#include "blackbird/participants.hpp"
#include "blackbird/raw_program.hpp"
#include "blackbird/session.hpp"
#include "blackbird/tools.hpp"

#include "blackbird/tasks.hpp"
#include "blackbird/workflows.hpp"
#include <map>
#include <memory>
namespace blackbird {
class RawTurnTrace;
class RawOperationTrace;
class RawPolicyTrace;
class CodingProvider {
  friend class CodingEngine;
  bool request_group_owned_ = false;

public:
  virtual ~CodingProvider() = default;
  std::function<bool()> cancelled;
  virtual std::string_view provider_identity() const noexcept { return "unavailable"; }
  // One final-assembly scope owns all transport attempts. Preparation may refuse;
  // the engine calls noexcept end even when preparation throws.
  virtual void begin_request_group(const Value &, const RawAdapterContext *) {}
  virtual void end_request_group() noexcept {}
  virtual bool supports_prompt_cache_policy() const noexcept { return false; }
  virtual Value respond(const Value &request,
                        const std::function<void(std::string_view)> &capture) = 0;
  virtual Value respond(const Value &request,
                        const std::function<void(std::string_view)> &capture,
                        const RawProviderOwner *) {
    return respond(request, capture);
  }
  virtual Value respond(const Value &request,
                        const std::function<void(std::string_view)> &capture,
                        const RawProviderOwner *owner, const RawAdapterContext *) {
    return respond(request, capture, owner);
  }
};
class OpenAiCodingProvider final : public CodingProvider {
public:
  explicit OpenAiCodingProvider(OpenAiConfig config = {})
      : config_(std::move(config)) {}
  std::string_view provider_identity() const noexcept override {
    return "openai-codex";
  }
  void begin_request_group(const Value &, const RawAdapterContext *) override;
  void end_request_group() noexcept override;
  bool supports_prompt_cache_policy() const noexcept override;
  Value respond(const Value &, const std::function<void(std::string_view)> &) override;
  Value respond(const Value &, const std::function<void(std::string_view)> &,
                const RawProviderOwner *) override;
  Value respond(const Value &, const std::function<void(std::string_view)> &,
                const RawProviderOwner *, const RawAdapterContext *) override;

private:
  OpenAiConfig config_;
  std::optional<Value> request_binding_;
  bool responding_ = false;
};
enum class RecoveryMode { provider_only, acknowledge_local_unknowns };
// Explicit local recovery records uncertainty; never redispatches old effects.
void recover_coding_session(RetainedState &root,
                            RecoveryMode mode = RecoveryMode::provider_only,
                            const RawAdapterContext *context = nullptr);
struct TurnInput {
  std::string_view prompt;
  std::string_view program;
};
class CodingEngine {
public:
  CodingEngine(AuditLog &log, ContextStore &context, CodingProvider &provider,
               std::string model, DecisionModelConfig decisions = {});
  ~CodingEngine();
  CodingEngine(const CodingEngine &) = delete;
  CodingEngine &operator=(const CodingEngine &) = delete;
  void turn(TurnInput input);
  Value operator_call(std::string_view name, const Value &arguments);
  ColleagueTransport colleague_transport;
  ParticipantTransport participant_transport;
  std::shared_ptr<CommandJobs> command_jobs() const { return command_jobs_; }
  void poll_commands();
  void shutdown_commands();
  void poll_participants();
  void shutdown_participants();
  bool operator_turn(std::string_view prompt, std::string_view fallback);
  std::shared_ptr<const WorkflowRegistry> workflows() const { return workflows_; }
  // One cooperative failure ticket, consumed before creating a recovery audit.
  // A reopened/crashed engine has no such authority.
  Error claim_backstop(const RawAdapterContext *raw_context = nullptr);
  const Value &workflow_result() const noexcept { return workflow_result_; }
  ContextStore &context() noexcept { return context_; }
  TaskStore &tasks() noexcept { return tasks_; }
  void model(std::string name) { model_ = std::move(name); }
  std::function<void(std::string_view)> display;
  std::function<void(std::string_view)> diagnostic;
  std::function<void(std::string_view)> notice;
  std::function<void(const Value &)> observed_usage;
  std::function<void(std::string_view)> operation_started;
  std::function<void(const Value &)> task_activity;
  std::function<void(const Value &, RawTaskPresentation)> task_activity_native;
  std::function<void(std::string_view)> status;
  std::function<void(std::string_view)> operation_completed;
  std::function<void(std::string_view, AttemptDisposition)> operation_outcome;
  std::function<void(std::string_view)> process_output;
  std::function<bool()> cancelled;
  // Optional native effect restriction for a scoped workflow (e.g. recovery
  // evidence protection). Throwing refuses admission; it is not Lua authority.
  std::function<void(std::string_view, const Value &)> effect_policy;
  void effort(std::string value);
  Value stats() const;
  bool uncontained_command_custody() const noexcept {
    return uncontained_exec_recovery_;
  }
  void validate_session_switch(const RawAdapterContext *context = nullptr) const;
  void validate_restart(const RawAdapterContext *context = nullptr) const;
  std::optional<std::string>
  take_restart_note(const RawAdapterContext *context = nullptr);
  const std::optional<std::string> &restart_note() const noexcept {
    return restart_note_;
  }

private:
  struct Runtime;
  struct CommandTaskActivity;
  using CommandTaskMap = std::map<std::string, CommandTaskActivity>;
  bool turn_running_ = false, backstop_claimed_ = false;
  bool request_group_active_ = false;
  RawTurnTrace *turn_trace_ = nullptr;
  RawOperationTrace *operation_trace_ = nullptr;
  std::optional<Error> failed_turn_;
  Value workflow_result_;
  std::optional<Value> operator_arguments_;
  std::shared_ptr<CommandJobs> command_jobs_;
  std::unique_ptr<RetainedState::SettlementScope> settlement_;
  CommandTaskMap command_tasks_;
  std::optional<Error> command_failure_;
  bool pumping_commands_ = false;
  bool uncontained_exec_recovery_ = false;
  bool capacity_stopped_ = false;
  std::size_t operation_depth_ = 0;
  std::size_t unretained_bytes_ = 0;
  void protect_workflow(const Value::Array &entries, const Value &proposal,
                        std::optional<RawEventRef> parent = {});
  AuditLog &log_;
  ContextStore &context_;
  TaskStore tasks_;
  std::unique_ptr<Participants> participants_;
  CodingProvider &provider_;
  std::string model_;
  std::string effort_ = "medium";
  Value last_request_bytes_, usage_;
  // Calls already unanswered at workflow admission. Repair never closes calls
  // introduced by the currently running workflow.
  Value::Array repairable_outputs_;
  Value lua_tools_{Value::Array{}}, pending_lua_tools_;
  std::string tools_revision_ = "initial-empty", pending_tools_revision_;
  Value program_config_{Value::object({{"modules", Value{Value::Array{}}},
                                       {"model", Value{""}},
                                       {"effort", Value{""}}})};
  std::shared_ptr<const WorkflowRegistry> workflows_, pending_workflows_;
  unsigned workflow_depth_ = 0, workflow_invocations_ = 0;
  std::shared_ptr<RawProgramIssuer> program_issuer_;
  RawProgramLifetime pending_program_trace_, pending_tools_trace_;
  struct WorkflowContinuation {
    Value definition, arguments;
    std::string revision, selection_attempt;
    RawEventRef selection{};
    RawProgramLifetime parked;
  };
  std::vector<WorkflowContinuation> workflow_continuations_;
  bool draining_workflows_ = false;
  std::optional<Error> workflow_failure_;
  Value execute_workflow(WorkflowContinuation &invocation);
  void drain_workflows();
  Value pending_program_config_;
  std::string program_revision_ = "initial-empty", pending_program_revision_;
  Value program_config(const Value &arguments);
  Value module_source(const Value &arguments);
  Value tool_registry() const;
  Value define_tool(const Value &arguments);
  Value request_tools() const;
  Value budget_, pending_budget_;

  std::string budget_revision_ = "initial-disabled", pending_budget_revision_;
  Value context_budget(const Value &arguments);
  Value budget_view(std::string_view model, std::size_t input_bytes) const;
  Value budget_view(std::string_view model, std::size_t input_bytes,
                    RawPolicyTrace &trace) const;
  std::map<std::string, std::string> previewed_;
  void present(const Value &item);

  BeadsAdapter beads_;
  DecisionModels decision_models_;
  SessionIdentity identity_;
  std::optional<std::string> restart_note_;
  DefinitionGenerationId generation_;
  std::optional<SourceReference> root_source_;
  std::size_t root_source_length_ = 0;
  std::unique_ptr<Observations> observations_;
  std::unique_ptr<Runtime> runtime_;
  Value request(Value options = Value::object({}));
  Value call(std::string name, Value arguments);
  Value operation(std::string_view name, const Value &input,
                  const std::function<
                      Value(OperationAttemptId, InvocationId, DefinitionGenerationId,
                            CommandTaskActivity *, RawRetainedExec *)> &body,
                  RawProviderOperationContext *provider_operation = nullptr);
  friend struct Runtime;
};
} // namespace blackbird
