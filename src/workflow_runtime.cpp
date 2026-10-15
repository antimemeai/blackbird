#include "blackbird/workflow_runtime.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/tools.hpp"
#include "blackbird/workflow_join.hpp"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <map>
#include <thread>
#include <unistd.h>

namespace blackbird {
namespace {
constexpr std::string_view task_schema = "workflow.native-task";
constexpr std::string_view join_schema = "workflow.native-join";
constexpr std::uint64_t result_credit = 4 * 1024 * 1024 + 65536;
Value number(std::uint64_t n) { return Value{Number{n}}; }
std::uint64_t integer(const Value &value) {
  const auto text = value.number().text();
  std::uint64_t result = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    throw Error{ErrorCode::invalid_range};
  return result;
}
template <class IdType> IdType identity(std::string_view text) {
  if (text.size() != 32)
    throw Error{ErrorCode::invalid_identity};
  IdentityBytes bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    unsigned int value = 0;
    const auto parsed =
        std::from_chars(text.data() + i * 2, text.data() + i * 2 + 2, value, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + i * 2 + 2)
      throw Error{ErrorCode::invalid_identity};
    bytes[i] = static_cast<std::byte>(value);
  }
  return unwrap(IdType::from_bytes(bytes));
}
Value canonical(const Value &value) {
  if (const auto *object = std::get_if<Value::Object>(&value.value())) {
    std::map<std::string, Value, std::less<>> sorted;
    for (const auto &[key, child] : *object)
      if (!sorted.emplace(key, canonical(child)).second)
        throw Error{ErrorCode::invalid_range};
    Value::Object result;
    for (auto &[key, child] : sorted)
      result.emplace_back(key, std::move(child));
    return Value::object(std::move(result));
  }
  if (const auto *array = std::get_if<Value::Array>(&value.value())) {
    Value::Array result;
    result.reserve(array->size());
    for (const auto &child : *array)
      result.push_back(canonical(child));
    return Value{std::move(result)};
  }
  return value;
}
void key(std::string_view value) {
  if (value.empty() || value.size() > 200 ||
      value.find('\0') != std::string_view::npos || !valid_utf8(value))
    throw Error{ErrorCode::invalid_range};
}
WorkflowExecutorPayloadRef payload(const Value &value) {
  ValueLimits limits;
  limits.bytes = 4 * 1024 * 1024;
  const auto bytes = unwrap(encode_packet(value, limits));
  return unwrap(WorkflowExecutorPayload::copy(bytes, limits.bytes));
}
Value error_value(Error error) {
  return Value::object({{"code", number(static_cast<std::uint8_t>(error.code))},
                        {"detail", Value{Number{error.detail}}}});
}
class AcceptedDispatch final : public EffectBoundary {
public:
  Result<void> dispatch(const EffectIntent &) override {
    return Result<void>::success();
  }
};
AttemptDisposition disposition(WorkflowExecutionOutcome value) {
  switch (value) {
  case WorkflowExecutionOutcome::success:
    return AttemptDisposition::success;
  case WorkflowExecutionOutcome::failure:
    return AttemptDisposition::failure;
  case WorkflowExecutionOutcome::cancelled:
    return AttemptDisposition::cancellation;
  case WorkflowExecutionOutcome::unknown:
    return AttemptDisposition::unknown;
  }
  throw Error{ErrorCode::corrupt};
}
const char *outcome_name(WorkflowExecutionOutcome value) {
  switch (value) {
  case WorkflowExecutionOutcome::success:
    return "success";
  case WorkflowExecutionOutcome::failure:
    return "failure";
  case WorkflowExecutionOutcome::cancelled:
    return "cancelled";
  case WorkflowExecutionOutcome::unknown:
    return "unknown";
  }
  throw Error{ErrorCode::corrupt};
}
} // namespace

struct WorkflowRuntime::Impl {
  struct Join {
    WorkflowCellId cell;
    WorkflowJoin reducer;
    bool dirty = false;
  };
  struct Output {
    LocalTools tools{{}, 1024 * 1024};
    Value result;
    std::optional<Error> error;
  };
  struct Task {
    WorkflowCellId cell;
    WorkflowExecutorKey execution;
    ApplicationRecordId result_record;
    std::optional<WorkflowExecutorHandle> handle;
    std::shared_ptr<Output> output;
    std::vector<Join *> waiters;
  };
  AuditLog &log;
  WorkflowStore store;
  std::unique_ptr<RetainedState::SettlementScope> settlement;
  WorkflowExecutor executor;
  RawRuntime *capture;
  std::map<std::string, Task, std::less<>> tasks;
  std::map<OperationAttemptId, Task *> attempts;
  std::map<std::string, Join, std::less<>> joins;
  std::vector<Join *> dirty_joins;
  std::thread::id thread = std::this_thread::get_id();
  pid_t process = ::getpid();
  bool stopped = false;
  std::optional<Error> retention_fault;
  std::optional<Error> emergency_control_error;
  bool admission_fault = false;
  std::size_t outstanding = 0;
  std::string incarnation;

  Impl(AuditLog &selected, HandleDomain domain, WorkflowExecutorLimits limits,
       RawRuntime *selected_capture, WorkflowExecutor::Notify notify)
      : log(selected), store(log), executor(domain, limits, std::move(notify)),
        capture(selected_capture),
        incarnation(hex_identity(domain.incarnation.bytes())) {
    if (domain.environment != log.root().environment())
      throw Error{ErrorCode::invalid_identity};
    admission_fault = log.root().state() != JournalWriterState::live;
    const auto snapshot = store.snapshot();
    for (const auto &row : field(snapshot, "cells").array()) {
      if (string_field(field(row, "schema"), "name") != task_schema)
        continue;
      if (integer(field(field(row, "schema"), "version")) != 1)
        throw Error{ErrorCode::unsupported};
      const auto &value = field(row, "value");
      const auto name = string_field(value, "key");
      const auto cell = identity<WorkflowCellId>(string_field(row, "id"));
      Task task{cell,
                {identity<InvocationId>(string_field(value, "invocation")),
                 identity<OperationAttemptId>(string_field(value, "attempt"))},
                identity<ApplicationRecordId>(string_field(value, "result_record")),
                {},
                {},
                {}};
      const auto inserted = tasks.emplace(name, std::move(task));
      if (!inserted.second || !attempts
                                   .emplace(inserted.first->second.execution.attempt,
                                            &inserted.first->second)
                                   .second)
        throw Error{ErrorCode::corrupt};
    }
    for (const auto &row : field(snapshot, "cells").array()) {
      if (string_field(field(row, "schema"), "name") != join_schema)
        continue;
      if (integer(field(field(row, "schema"), "version")) != 1)
        throw Error{ErrorCode::unsupported};
      const auto &value = field(row, "value");
      const auto cell = identity<WorkflowCellId>(string_field(row, "id"));
      auto reducer = unwrap(WorkflowJoin::restore(field(value, "state")));
      const auto name = reducer.descriptor().id;
      const auto inserted = joins.emplace(name, Join{cell, std::move(reducer)});
      if (!inserted.second)
        throw Error{ErrorCode::corrupt};
    }
    dirty_joins.reserve(joins.size());
    for (auto &[name, join] : joins) {
      (void)name;
      if (join.reducer.committed_selection())
        continue;
      dirty_joins.push_back(&join);
      join.dirty = true;
      std::vector<std::pair<std::uint64_t, std::string>> completed;
      for (const auto &member : join.reducer.descriptor().members) {
        auto found = tasks.find(member);
        if (found == tasks.end())
          throw Error{ErrorCode::corrupt};
        found->second.waiters.push_back(&join);
        const auto task = read(found->second);
        if (const auto *sequence = task.value.find("accepted_sequence"))
          completed.emplace_back(integer(*sequence), member);
      }
      std::sort(completed.begin(), completed.end());
      for (const auto &[sequence, member] : completed) {
        (void)sequence;
        observe(join, member, read(tasks.at(member)));
      }
    }
  }
  void owner() const {
    if (::getpid() != process || std::this_thread::get_id() != thread)
      throw Error{ErrorCode::wrong_incarnation};
  }
  WorkflowCell read(const Task &task) const {
    auto found = store.read(task.cell);
    if (!found)
      throw Error{ErrorCode::corrupt};
    return *found;
  }
  WorkflowJoinObservation observation(const std::string &member,
                                      const WorkflowCell &cell) const {
    const auto outcome = string_field(cell.value, "outcome");
    const auto selected = outcome == "success"     ? WorkflowJoinOutcome::success
                          : outcome == "failure"   ? WorkflowJoinOutcome::failure
                          : outcome == "cancelled" ? WorkflowJoinOutcome::cancelled
                                                   : WorkflowJoinOutcome::unknown;
    return {member,
            cell.revision.value,
            selected,
            {integer(field(cell.value, "accepted_sequence")), 0, 0},
            Value::object({{"cell", Value{hex_identity(cell.id.bytes())}},
                           {"revision", number(cell.revision.value)}})};
  }
  void observe(Join &join, const std::string &member, const WorkflowCell &cell) {
    if (join.reducer.committed_selection() ||
        string_field(cell.value, "outcome") == "pending")
      return;
    unwrap(join.reducer.observe(observation(member, cell)));
    if (!join.dirty) {
      dirty_joins.push_back(&join);
      join.dirty = true;
    }
  }
  void observe(Task &task, const WorkflowCell &cell) {
    for (auto *join : task.waiters)
      observe(*join, string_field(cell.value, "key"), cell);
  }
  Value inspect_join(const std::string &name) const {
    const auto found = joins.find(name);
    if (found == joins.end())
      throw Error{ErrorCode::stale_handle};
    return Value::object(
        {{"id", Value{name}},
         {"state", found->second.reducer.snapshot()},
         {"selected", Value{found->second.reducer.committed_selection().has_value()}}});
  }
  void settle_joins() {
    while (!dirty_joins.empty()) {
      auto &join = *dirty_joins.back();
      const auto proposal = unwrap(join.reducer.propose());
      if (proposal && !join.reducer.committed_selection()) {
        auto accepted = unwrap(join.reducer.prepare_acceptance(*proposal));
        const auto prior = store.read(join.cell);
        if (!prior)
          throw Error{ErrorCode::corrupt};
        auto next = prior->value;
        for (auto &[name, value] : next.object())
          if (name == "state")
            value = accepted.snapshot();
        const auto expected_revision = store.revision() + 1;
        try {
          (void)store.transact({"join-select:" + join.reducer.descriptor().id,
                                {{join.cell, prior->revision}},
                                {{join.cell, prior->schema, std::move(next)}},
                                {}});
        } catch (...) {
          if (store.revision() == expected_revision)
            join.reducer = std::move(accepted);
          throw;
        }
        join.reducer = std::move(accepted);
      }
      join.dirty = false;
      dirty_joins.pop_back();
    }
  }
  Value join(const Value &request) {
    if (stopped || retention_fault || admission_fault)
      throw Error{ErrorCode::busy};
    WorkflowJoinDescriptor descriptor;
    descriptor.id = string_field(request, "id");
    key(descriptor.id);
    const auto mode = string_field(request, "mode");
    if (mode == "all")
      descriptor.mode = WorkflowJoinMode::all;
    else if (mode == "all_success")
      descriptor.mode = WorkflowJoinMode::all_success;
    else if (mode == "first")
      descriptor.mode = WorkflowJoinMode::first;
    else if (mode == "first_success")
      descriptor.mode = WorkflowJoinMode::first_success;
    else if (mode == "quorum")
      descriptor.mode = WorkflowJoinMode::quorum;
    else if (mode == "collect")
      descriptor.mode = WorkflowJoinMode::collect;
    else
      throw Error{ErrorCode::unsupported};
    if (const auto *quorum = request.find("quorum")) {
      const auto count = integer(*quorum);
      if (count > SIZE_MAX)
        throw Error{ErrorCode::capacity};
      descriptor.quorum = static_cast<std::size_t>(count);
    }
    for (const auto &member : field(request, "members").array()) {
      if (!tasks.contains(member.string()))
        throw Error{ErrorCode::stale_handle};
      descriptor.members.push_back(member.string());
    }
    const auto existing = joins.find(descriptor.id);
    if (existing != joins.end()) {
      if (existing->second.reducer.descriptor() != descriptor)
        throw Error{ErrorCode::conflict};
      return inspect_join(descriptor.id);
    }
    auto reducer = unwrap(WorkflowJoin::create(descriptor));
    const auto initial = reducer.snapshot();
    const auto cell = unwrap(log.root().issue<WorkflowCellId>());
    std::map<std::string, Join, std::less<>> staged;
    staged.emplace(descriptor.id, Join{cell, std::move(reducer)});
    auto node = staged.extract(staged.begin());
    dirty_joins.reserve(joins.size() + 1);
    for (const auto &member : descriptor.members) {
      auto &waiters = tasks.at(member).waiters;
      waiters.reserve(waiters.size() + 1);
    }
    std::vector<std::pair<std::uint64_t, std::string>> completed;
    completed.reserve(descriptor.members.size());
    for (const auto &member : descriptor.members) {
      const auto result = read(tasks.at(member));
      if (const auto *sequence = result.value.find("accepted_sequence"))
        completed.emplace_back(integer(*sequence), member);
    }
    std::sort(completed.begin(), completed.end());
    for (const auto &[sequence, member] : completed) {
      (void)sequence;
      unwrap(
          node.mapped().reducer.observe(observation(member, read(tasks.at(member)))));
    }
    auto publish = [&]() noexcept {
      auto &created = joins.insert(std::move(node)).position->second;
      dirty_joins.push_back(&created);
      created.dirty = true;
      for (const auto &member : descriptor.members)
        tasks.at(member).waiters.push_back(&created);
    };
    const auto expected_revision = store.revision() + 1;
    try {
      (void)store.transact(
          {"join:" + descriptor.id,
           {{cell, {0}}},
           {{cell, {std::string{join_schema}, 1}, Value::object({{"state", initial}})}},
           {}});
    } catch (...) {
      if (store.revision() == expected_revision)
        publish();
      admission_fault = true;
      throw;
    }
    publish();
    settle_joins();
    return inspect_join(descriptor.id);
  }
  Value inspect(const std::string &name) const {
    const auto found = tasks.find(name);
    if (found == tasks.end())
      throw Error{ErrorCode::stale_handle};
    const auto cell = read(found->second);
    auto value = cell.value;
    value.object().emplace_back(
        "declaration",
        unwrap(read_packet(
            unwrap(log.root().invocation(found->second.execution.invocation)).input)));
    value.object().emplace_back("revision", number(cell.revision.value));
    const auto attempt = unwrap(log.root().attempt(found->second.execution.attempt));
    value.object().emplace_back("released", Value{attempt.opened});
    value.object().emplace_back("reconciliation_required",
                                Value{attempt.reconciliation_required});
    if (retention_fault)
      value.object().emplace_back("retention_fault", error_value(*retention_fault));
    if (!found->second.handle && !attempt.observation) {
      value.object().emplace_back("executor_absent", Value{true});
      value.object().emplace_back("execution_state",
                                  Value{attempt.evidence == RetainedEvidence::live
                                            ? "invocation_prevented_with_retention_debt"
                                            : "unknown_after_recovery"});
    }
    if (!attempt.observation && attempt.evidence != RetainedEvidence::live)
      value.object().emplace_back("recovered_outcome", Value{"unknown"});
    return value;
  }
  Value start(const Value &request) {
    if (stopped || retention_fault || admission_fault)
      throw Error{ErrorCode::busy};
    const auto name = string_field(request, "id");
    key(name);
    const auto tool = string_field(request, "tool");
    if (tool != "exec" && tool != "read_file" && tool != "write_file" &&
        tool != "edit_file" && tool != "echo")
      throw Error{ErrorCode::unsupported};
    const auto input = canonical(field(request, "arguments"));
    ValueLimits input_limits;
    input_limits.bytes = 512 * 1024;
    (void)unwrap(encode_packet(input, input_limits));
    const auto declaration =
        Value::object({{"tool", Value{tool}},
                       {"arguments", input},
                       {"build", Value{BLACKBIRD_WORKFLOW_BUILD_ID}}});
    const auto existing = tasks.find(name);
    if (existing != tasks.end()) {
      const auto retained = unwrap(read_packet(
          unwrap(log.root().invocation(existing->second.execution.invocation)).input));
      if (field(retained, "tool") != Value{tool} ||
          field(retained, "arguments") != input)
        throw Error{ErrorCode::conflict};
      return inspect(name);
    }
    auto &root = log.root();
    const auto cell_id = unwrap(root.issue<WorkflowCellId>());
    const auto decision = unwrap(root.issue<DecisionId>());
    const auto invocation = unwrap(root.issue<InvocationId>());
    const auto attempt = unwrap(root.issue<OperationAttemptId>());
    const auto actor = unwrap(root.issue<ParticipantId>());
    const auto conversation = unwrap(root.issue<ConversationId>());
    const auto workflow = unwrap(root.issue<WorkflowId>());
    const auto generation = unwrap(root.issue<DefinitionGenerationId>());
    const auto context = unwrap(root.issue<ContextRevisionId>());
    const auto result_record = log.issue();
    const auto native_input = unwrap(encode_packet(declaration));
    const ImmutableBytes owned_input{native_input};
    const auto continuation = unwrap(
        encode_packet(Value::object({{"operation", Value{"workflow_native"}},
                                     {"key", Value{name}},
                                     {"build", Value{BLACKBIRD_WORKFLOW_BUILD_ID}}})));
    const std::array<RetainedEvent, 3> admission{
        RetainedEvent{{},
                      DecisionEvent{decision,
                                    actor,
                                    conversation,
                                    workflow,
                                    generation,
                                    context,
                                    {invocation},
                                    ImmutableBytes{continuation}}},
        RetainedEvent{{},
                      InvocationEvent{invocation, decision, generation, owned_input}},
        RetainedEvent{
            {},
            AttemptAdmissionEvent{attempt, invocation, decision, owned_input, true}}};
    const auto state =
        Value::object({{"key", Value{name}},
                       {"invocation", Value{hex_identity(invocation.bytes())}},
                       {"attempt", Value{hex_identity(attempt.bytes())}},
                       {"result_record", Value{hex_identity(result_record.bytes())}},
                       {"outcome", Value{"pending"}},
                       {"result", Value{}}});
    std::map<std::string, Task, std::less<>> staged;
    const auto output = std::make_shared<Output>();
    staged.emplace(name,
                   Task{cell_id, {invocation, attempt}, result_record, {}, output, {}});
    auto node = staged.extract(staged.begin());
    std::map<OperationAttemptId, Task *> staged_attempts;
    staged_attempts.emplace(attempt, &node.mapped());
    auto attempt_node = staged_attempts.extract(staged_attempts.begin());
    const auto lane = capture ? unwrap(capture->prepare_thread()) : RawThreadHandle{};
    if (outstanding >= UINT64_MAX / result_credit - 1)
      throw Error{ErrorCode::capacity};
    const JournalCapacity credit{(outstanding + 1) * result_credit,
                                 (outstanding + 1) * 3};
    if (settlement)
      unwrap(root.refresh_settlement(credit));
    else
      settlement = unwrap(root.protect_settlement(credit));
    const WorkflowExecutorInput arguments{{}, {}, payload(input)};
    const auto leaf = [tool, lane, invocation, attempt, generation,
                       output](const WorkflowExecutorInput &body,
                               const std::stop_token &stop) {
      std::optional<RawThreadBinding> binding;
      if (lane)
        binding.emplace(lane);
      const RawProviderOwner raw_owner{
          attempt.bytes(), invocation.bytes(), generation.bytes(), {}};
      std::optional<RawRegion> region;
      if (lane)
        region.emplace(raw_regions::command_perform);
      const auto parent =
          region ? RawEventRef{region->lane_epoch(), region->begin_ordinal()}
                 : RawEventRef{};
      const RawAdapterContext raw_context{
          &raw_owner, lane ? std::optional{parent} : std::nullopt, nullptr, nullptr};
      const auto call = unwrap(decode_packet(body.input->view()));
      if (stop.stop_requested())
        return WorkflowLeafResult{WorkflowExecutionOutcome::cancelled, {}, {}};
      auto &tools = output->tools;
      tools.cancelled = [stop] { return stop.stop_requested(); };
      auto outcome = WorkflowExecutionOutcome::success;
      try {
        output->result = tool == "echo" ? call : tools.run(tool, call, &raw_context);
        if (tool == "exec" && field(output->result, "exit_code") != Value{Number{0}})
          outcome = WorkflowExecutionOutcome::failure;
      } catch (const Error &error) {
        output->error = error;
        outcome = WorkflowExecutionOutcome::unknown;
      }
      return WorkflowLeafResult{outcome, {}, {}};
    };
    const auto submitted = executor.submit(
        {invocation, attempt}, arguments, leaf,
        [&](const WorkflowExecutorHandle &, const WorkflowExecutorKey &,
            const WorkflowExecutorInput &) {
          try {
            auto publish = [&]() noexcept {
              tasks.insert(std::move(node));
              attempts.insert(std::move(attempt_node));
              ++outstanding;
            };
            const auto expected_revision = store.revision() + 1;
            try {
              (void)store.transact({"start:" + name,
                                    {{cell_id, {0}}},
                                    {{cell_id, {std::string{task_schema}, 1}, state}},
                                    {}},
                                   admission);
            } catch (...) {
              if (store.revision() == expected_revision)
                publish();
              throw;
            }
            publish();
            AcceptedDispatch boundary;
            const auto released = unwrap(root.dispatch(attempt, boundary));
            if (!released.dispatched || !released.recording.has_value())
              return Result<void>::failure({ErrorCode::audit_unavailable});
            return Result<void>::success();
          } catch (const Error &error) {
            retention_fault = error;
            admission_fault = true;
            return Result<void>::failure(error);
          } catch (const std::bad_alloc &) {
            retention_fault = Error{ErrorCode::allocation};
            admission_fault = true;
            return Result<void>::failure(*retention_fault);
          }
        });
    if (!submitted.has_value()) {
      if (admission_fault) {
        stopped = true;
        unwrap(executor.shutdown());
        tick();
      }
      throw submitted.error();
    }
    tasks.at(name).handle = submitted.value();
    return inspect(name);
  }
  void tick() {
    owner();
    const auto result =
        executor.drain([&](const WorkflowExecutorCompletion &completion) {
          try {
            const auto found = attempts.find(completion.key.attempt);
            if (found == attempts.end() || found->second->execution != completion.key)
              return Result<void>::failure({ErrorCode::corrupt});
            const auto prior = read(*found->second);
            if (string_field(prior.value, "outcome") != "pending") {
              const auto retained = unwrap(log.root().attempt(completion.key.attempt));
              if (!retained.observation ||
                  retained.observation->phase != AttemptPhase::terminal)
                return Result<void>::failure({ErrorCode::corrupt});
              observe(*found->second, prior);
              found->second->handle.reset();
              found->second->output.reset();
              --outstanding;
              return Result<void>::success();
            }
            const auto &output = found->second->output;
            if (!output)
              return Result<void>::failure({ErrorCode::corrupt});
            Value::Array originals;
            for (const auto &original : output->tools.captured())
              originals.push_back(Value::object({{"label", Value{original.label}},
                                                 {"bytes", Value{original.bytes}}}));
            Value observed =
                Value::object({{"value", output->result},
                               {"originals", Value{std::move(originals)}}});
            observed.object().emplace_back(
                "originals_complete",
                Value{!output->error && !completion.result.exception});
            if (emergency_control_error)
              observed.object().emplace_back("emergency_control_error",
                                             error_value(*emergency_control_error));
            if (output->error)
              observed.object().emplace_back("error", error_value(*output->error));
            if (completion.result.output)
              observed = unwrap(decode_packet(completion.result.output->view()));
            if (completion.result.exception) {
              try {
                std::rethrow_exception(completion.result.exception);
              } catch (const Error &error) {
                observed.object().emplace_back("exception", error_value(error));
              } catch (const std::exception &error) {
                const auto *message = error.what();
                const auto length = message ? ::strnlen(message, 4096) : 0;
                observed.object().emplace_back(
                    "exception", Value{message ? std::string{message, length}
                                               : std::string{"native exception"}});
              } catch (...) {
                observed.object().emplace_back("exception",
                                               Value{"opaque native exception"});
              }
            }
            observed.object().emplace_back("invoked", Value{completion.invoked});
            observed.object().emplace_back("cancel_requested",
                                           Value{completion.cancellation_requested});
            ValueLimits result_limits;
            result_limits.bytes = 4 * 1024 * 1024;
            (void)unwrap(encode_packet(observed, result_limits));
            auto next = prior.value;
            for (auto &[field_name, value] : next.object()) {
              if (field_name == "outcome")
                value = Value{outcome_name(completion.result.outcome)};
              if (field_name == "result")
                value = observed;
            }
            next.object().emplace_back("accepted_sequence",
                                       number(store.revision() + 1));
            const auto bytes = unwrap(encode_packet(Value::object(
                {{"result_cell", Value{hex_identity(prior.id.bytes())}},
                 {"invoked", Value{completion.invoked}},
                 {"cancel_requested", Value{completion.cancellation_requested}}})));
            const std::array terminal{RetainedEvent{
                {},
                AttemptObservationEvent{completion.key.attempt, AttemptPhase::terminal,
                                        disposition(completion.result.outcome),
                                        ImmutableBytes{bytes}}}};
            (void)store.transact(
                {"complete:" + hex_identity(completion.key.attempt.bytes()),
                 {{prior.id, prior.revision}},
                 {{prior.id, prior.schema, std::move(next)}},
                 {}},
                terminal, true, found->second->result_record);
            observe(*found->second, read(*found->second));
            found->second->handle.reset();
            found->second->output.reset();
            --outstanding;
            return Result<void>::success();
          } catch (const Error &error) {
            return Result<void>::failure(error);
          } catch (const std::bad_alloc &) {
            return Result<void>::failure({ErrorCode::allocation});
          }
        });
    retention_fault =
        result.has_value() ? std::nullopt : std::optional<Error>{result.error()};
    if (!result.has_value()) {
      // Mandatory cleanup remains available when the writer cannot retain an
      // ordinary control. Keep exact completions/outputs for a later owner drain.
      admission_fault = true;
      stopped = true;
      emergency_control_error = result.error();
      unwrap(executor.shutdown());
    }
    if (result.has_value()) {
      try {
        if (outstanding == 0)
          settlement.reset();
        else if (settlement && result.value() != 0)
          unwrap(log.root().refresh_settlement(
              {outstanding * result_credit, outstanding * 2}));
        settle_joins();
      } catch (const Error &error) {
        retention_fault = error;
        admission_fault = true;
      }
    }
  }
};

WorkflowRuntime::WorkflowRuntime(AuditLog &log, HandleDomain domain,
                                 WorkflowExecutorLimits limits, RawRuntime *capture,
                                 WorkflowExecutor::Notify notify)
    : impl_(std::make_unique<Impl>(log, domain, limits, capture, std::move(notify))) {}
WorkflowRuntime::~WorkflowRuntime() {
  try {
    shutdown();
  } catch (...) {
    // Join before the caller's raw/log owners expire. A failed retained close
    // stays unresolved in the journal; destruction cannot authorize replay.
    (void)impl_->executor.shutdown();
    try {
      impl_->tick();
    } catch (...) {
    }
  }
}
void WorkflowRuntime::tick() { impl_->tick(); }
bool WorkflowRuntime::stopping() const noexcept { return impl_->stopped; }
void WorkflowRuntime::shutdown() {
  impl_->owner();
  if (!impl_->stopped && impl_->outstanding != 0) {
    try {
      (void)impl_->store.transact({"shutdown:" + impl_->incarnation,
                                   {},
                                   {},
                                   Value::object({{"operation", Value{"shutdown"}}})});
    } catch (...) {
      impl_->admission_fault = true;
      impl_->stopped = true;
      impl_->emergency_control_error = Error{ErrorCode::audit_unavailable};
      (void)impl_->executor.shutdown();
      impl_->tick();
      throw;
    }
  }
  impl_->stopped = true;
  unwrap(impl_->executor.shutdown());
  impl_->tick();
  if (impl_->retention_fault)
    throw *impl_->retention_fault;
  if (impl_->outstanding != 0)
    throw Error{ErrorCode::external_unknown};
  impl_->settlement.reset();
}
Value WorkflowRuntime::request(const Value &input) {
  impl_->owner();
  impl_->tick();
  const auto &operation = string_field(input, "op");
  if (operation == "join")
    return impl_->join(input);
  if (operation == "join-read")
    return impl_->inspect_join(string_field(input, "id"));
  if (operation == "status")
    return Value::object(
        {{"stopped", Value{impl_->stopped}},
         {"admission_closed", Value{impl_->stopped || impl_->admission_fault ||
                                    impl_->retention_fault.has_value()}},
         {"completion_debt", number(impl_->outstanding)},
         {"retention_fault",
          impl_->retention_fault ? error_value(*impl_->retention_fault) : Value{}},
         {"recovery_required",
          Value{impl_->log.root().state() != JournalWriterState::live}},
         {"tasks", number(impl_->tasks.size())},
         {"joins", number(impl_->joins.size())}});
  if (operation == "start")
    return impl_->start(input);
  if (operation == "read")
    return impl_->inspect(string_field(input, "id"));
  if (operation == "list") {
    Value::Array rows;
    for (const auto &[name, task] : impl_->tasks) {
      (void)task;
      rows.push_back(impl_->inspect(name));
    }
    return Value{std::move(rows)};
  }
  if (operation == "cancel") {
    const auto found = impl_->tasks.find(string_field(input, "id"));
    if (found == impl_->tasks.end())
      throw Error{ErrorCode::stale_handle};
    if (!found->second.handle)
      return impl_->inspect(found->first);
    try {
      (void)impl_->store.transact(
          {"cancel:" + hex_identity(found->second.execution.attempt.bytes()),
           {},
           {},
           Value::object(
               {{"operation", Value{"cancel"}},
                {"attempt",
                 Value{hex_identity(found->second.execution.attempt.bytes())}}})});
    } catch (const Error &error) {
      impl_->admission_fault = true;
      impl_->stopped = true;
      impl_->emergency_control_error = error;
      unwrap(impl_->executor.shutdown());
      impl_->tick();
      throw;
    }
    const auto observed = unwrap(impl_->executor.cancel(found->second.handle.value()));
    auto result = impl_->inspect(found->first);
    result.object().emplace_back("cancellation_observation",
                                 number(static_cast<std::uint8_t>(observed)));
    return result;
  }
  if (operation == "shutdown") {
    shutdown();
    return Value::object({{"stopped", Value{true}}});
  }
  throw Error{ErrorCode::unsupported};
}
} // namespace blackbird
