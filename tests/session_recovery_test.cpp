#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
template <class T> T id(unsigned char n) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{n};
  return unwrap(T::from_bytes(bytes));
}
void check(bool good) {
  if (!good)
    throw Error{ErrorCode::corrupt};
}
struct Counter final : EffectBoundary {
  int calls = 0;
  Result<void> dispatch(const EffectIntent &) override {
    ++calls;
    return Result<void>::success();
  }
};
namespace {
struct NoProvider final : CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw Error{ErrorCode::conflict};
  }
};
void require(bool good, std::string_view message) {
  if (!good) {
    std::cerr << message << '\n';
    throw Error{ErrorCode::corrupt};
  }
}
void admit(RetainedState &root, unsigned char base, std::string_view operation,
           const Value &input, bool opened = true, std::string_view fault = {}) {
  const auto decision = id<DecisionId>(base);
  const auto invocation = id<InvocationId>(base + 1);
  const auto attempt = id<OperationAttemptId>(base + 2);
  const auto bytes = unwrap(encode_packet(input));
  auto metadata = Value::object(
      {{"operation", Value{std::string{operation}}},
       {"input_binding", Value{"invocation-v1"}},
       {"invocation", Value{hex_identity(invocation.bytes())}},
       {"revision", Value{hex_identity(id<ContextRevisionId>(10).bytes())}},
       {"generation", Value{hex_identity(id<DefinitionGenerationId>(7).bytes())}}});
  for (auto &[key, value] : metadata.object()) {
    if (key == "generation" && fault == "generation")
      value = Value{hex_identity(id<DefinitionGenerationId>(19).bytes())};
    if (key == "revision" && fault == "revision")
      value = Value{hex_identity(id<ContextRevisionId>(19).bytes())};
  }
  if (fault == "legacy-plan") {
    auto &fields = metadata.object();
    fields.erase(std::remove_if(fields.begin(), fields.end(),
                                [](const auto &entry) {
                                  return entry.first == "input_binding" ||
                                         entry.first == "invocation";
                                }),
                 fields.end());
    fields.emplace_back("input", input);
  }
  const auto packet = unwrap(encode_packet(metadata));
  const auto invoked_input =
      fault == "input"
          ? unwrap(encode_packet(Value::object({{"different", Value{true}}})))
          : bytes;
  const std::array<RetainedEvent, 3> events{
      RetainedEvent{{},
                    DecisionEvent{decision, id<ParticipantId>(4), id<ConversationId>(5),
                                  id<WorkflowId>(6), id<DefinitionGenerationId>(7),
                                  id<ContextRevisionId>(10),
                                  fault == "plan" || fault == "legacy-plan"
                                      ? std::vector<InvocationId>{invocation,
                                                                  id<InvocationId>(90)}
                                      : std::vector<InvocationId>{invocation},
                                  packet}},
      RetainedEvent{{},
                    InvocationEvent{invocation, decision, id<DefinitionGenerationId>(7),
                                    invoked_input}},
      RetainedEvent{{}, AttemptAdmissionEvent{attempt, invocation, decision, bytes}}};
  (void)unwrap(root.append(root.cursor(), {}, events));
  if (opened)
    (void)unwrap(root.submit({{}, AttemptOpenEvent{attempt}}));
}
Value::Array custody_markers(const RetainedState &root) {
  Value::Array markers;
  for (const auto &fact : root.committed_facts()) {
    const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
    if (!record || record->channel != ApplicationChannel::program)
      continue;
    const auto packet = unwrap(read_packet(record->payload));
    if (packet.find("label") && *packet.find("label") == Value{"session.recovery"})
      markers.push_back(packet);
  }
  return markers;
}
void local_recovery_cases(const std::filesystem::path &base_path,
                          const JournalHeader &header, JournalCapacity capacity) {
  for (const std::string scenario :
       {"exec", "process", "read_file", "write_file", "edit_file", "lua",
        "workflow_execute", "workflow_invoke", "unopened", "mixed-native", "plan",
        "legacy-plan", "generation", "revision", "input", "unknown-native",
        "wrapper-input", "wrapper-generation", "capacity-local"}) {
    const auto directory = base_path / ("local-" + scenario);
    std::filesystem::create_directory(directory);
    require(::chmod(directory.c_str(), 0700) == 0, "local directory permissions");
    auto root = unwrap(
        RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                  NativeJournalDirectory::open(directory.string()))),
                              "audit", header, capacity));
    AuditLog log{*root};
    ContextStore context{log};
    context.append(
        {Value::object({{"role", Value{"user"}}, {"content", Value{"LOCAL_KEEP_ME"}}})},
        "fixture");
    const auto before = context.items();
    const bool malformed = scenario == "plan" || scenario == "legacy-plan" ||
                           scenario == "generation" || scenario == "revision" ||
                           scenario == "input" || scenario == "unknown-native" ||
                           scenario.starts_with("wrapper-");
    const bool mixed = malformed || scenario == "mixed-native";
    const bool opened = scenario != "unopened";
    const std::string operation =
        mixed                                                    ? "provider"
        : scenario == "unopened" || scenario == "capacity-local" ? "exec"
                                                                 : scenario;
    const auto input = Value::object({{"fixture", Value{"native"}}});
    admit(*root, 30, operation, input, opened);
    if (mixed) {
      admit(*root, 40, "write_file", input);
      admit(*root, 50,
            scenario == "unknown-native"       ? "unrecognized"
            : scenario.starts_with("wrapper-") ? "lua"
                                               : "exec",
            input, true,
            scenario.starts_with("wrapper-") ? std::string_view{scenario}.substr(8)
            : malformed                      ? std::string_view{scenario}
                                             : "");
    }
    const auto saved_bytes = root->cursor().end_offset;
    root.reset();
    auto reopen = [&](bool exhausted = false) {
      auto opened_root = unwrap(RetainedState::open(
          std::make_unique<NativeJournalDirectory>(
              unwrap(NativeJournalDirectory::open(directory.string()))),
          "audit", header,
          exhausted ? JournalCapacity{saved_bytes, capacity.max_records} : capacity));
      unwrap(opened_root->confirm_recovery());
      return opened_root;
    };
    root = reopen();
    const auto before_facts = root->committed_facts().size();
    bool default_refused = false;
    try {
      recover_coding_session(*root);
    } catch (const Error &error) {
      default_refused = error.code == ErrorCode::external_unknown;
    }
    require(default_refused && root->state() == JournalWriterState::recovered &&
                root->committed_facts().size() == before_facts,
            "default local recovery must refuse without partial settlement");
    if (scenario == "capacity-local") {
      root.reset();
      root = reopen(true);
      bool refused = false;
      try {
        recover_coding_session(*root, RecoveryMode::acknowledge_local_unknowns);
      } catch (const Error &error) {
        refused = error.code == ErrorCode::capacity;
      }
      require(refused && root->state() == JournalWriterState::blocked &&
                  !unwrap(root->attempt(id<OperationAttemptId>(32))).observation,
              "local recovery capacity refusal must block without terminal");
      Counter refused_effect;
      require(!root->dispatch(id<OperationAttemptId>(32), refused_effect).has_value() &&
                  refused_effect.calls == 0,
              "capacity failure must close effect admission");
      root.reset();
      root = reopen();
    }
    bool refused = false;
    try {
      recover_coding_session(*root, RecoveryMode::acknowledge_local_unknowns);
    } catch (const Error &error) {
      refused = error.code == ErrorCode::external_unknown;
    }
    if (malformed) {
      require(refused && root->state() == JournalWriterState::recovered &&
                  root->committed_facts().size() == before_facts,
              "invalid native mixed recovery must append no partial settlements");
      for (const auto attempt : std::array<unsigned char, 3>{32, 42, 52})
        require(!unwrap(root->attempt(id<OperationAttemptId>(attempt))).observation,
                "invalid mixed attempt gained a terminal observation");
      continue;
    }
    require(!refused && root->state() == JournalWriterState::live,
            "explicit valid local recovery must permit independent new work");
    const std::vector<unsigned char> attempts =
        mixed ? std::vector<unsigned char>{32, 42, 52} : std::vector<unsigned char>{32};
    for (const auto number : attempts) {
      const auto attempt = unwrap(root->attempt(id<OperationAttemptId>(number)));
      require(attempt.observation &&
                  attempt.observation->phase == AttemptPhase::terminal &&
                  attempt.observation->disposition == AttemptDisposition::unknown &&
                  attempt.admission.input == unwrap(encode_packet(input)),
              "local outcome/linkage must remain terminal unknown with original input");
      Counter replay;
      require(
          !unwrap(root->dispatch(id<OperationAttemptId>(number), replay)).dispatched &&
              replay.calls == 0,
          "unknown recovered effect must never replay");
    }
    AuditLog after_log{*root};
    ContextStore after_context{after_log};
    require(after_context.items() == before, "local recovery changed retained context");
    const bool uncontained =
        opened && (operation == "exec" || operation == "process" || mixed);
    const auto markers = custody_markers(*root);
    require(markers.size() == (uncontained ? 1U : 0U), "command custody marker scope");
    if (uncontained) {
      const auto &metadata = field(markers.front(), "metadata");
      require(field(metadata, "uncontained_exec") == Value{true} &&
                  field(metadata, "attempts") ==
                      Value{Value::Array{Value{hex_identity(
                          id<OperationAttemptId>(mixed ? 52 : 32).bytes())}}},
              "durable custody marker must identify exactly the crossed command");
    }
    const auto fact_count = root->committed_facts().size();
    root.reset();
    root = reopen();
    recover_coding_session(*root);
    require(root->committed_facts().size() == fact_count &&
                custody_markers(*root) == markers,
            "ordinary rereopen must preserve unknowns and marker without duplicates");
    auto check_engine = [&] {
      AuditLog engine_log{*root};
      ContextStore engine_context{engine_log};
      NoProvider provider;
      CodingEngine engine{engine_log, engine_context, provider, "fixture"};
      require(field(engine.stats(), "uncontained_exec_recovery") == Value{uncontained},
              "engine stats lost retained command custody uncertainty");
      engine.validate_session_switch();
      bool restart_refused = false;
      try {
        engine.validate_restart();
      } catch (const Error &error) {
        restart_refused = error.code == ErrorCode::external_unknown;
      }
      require(restart_refused == uncontained,
              "restart must remain fenced exactly for recovered crossed commands");
    };
    check_engine();
    if (uncontained) {
      admit(*root, 100, "write_file", input);
      root.reset();
      root = reopen();
      recover_coding_session(*root, RecoveryMode::acknowledge_local_unknowns);
      require(custody_markers(*root) == markers,
              "later file-only recovery cleared or duplicated command custody marker");
      check_engine();
    }
    {
      AuditLog saved_log{*root};
      ContextStore saved_context{saved_log};
      unwrap(saved_context.checkpoint());
    }
    root.reset();
    root = reopen();
    require(root->compact_recovery(),
            "settled recovery fixture must exercise saved state");
    recover_coding_session(*root);
    check_engine();
  }
}
void interrupted_effects(const std::filesystem::path &base_path,
                         const JournalHeader &header, JournalCapacity capacity) {
  const auto directory = base_path / "interrupted-effects";
  std::filesystem::create_directory(directory);
  require(::chmod(directory.c_str(), 0700) == 0, "interrupted directory permissions");
  const auto written = directory / "written";
  const auto effected = directory / "command-effect";
  const std::array<std::string_view, 3> operations{"write_file", "edit_file", "exec"};
  const std::array<Value, 3> inputs{
      Value::object(
          {{"path", Value{written.string()}}, {"content", Value{"ORIGINAL"}}}),
      Value::object({{"path", Value{written.string()}},
                     {"old", Value{"ORIGINAL"}},
                     {"new", Value{"ONCE"}}}),
      Value::object(
          {{"argv", Value{Value::Array{Value{"/bin/sh"}, Value{"-c"},
                                       Value{"printf 'EFFECT\\n' >> \"$1\""},
                                       Value{"fixture"}, Value{effected.string()}}}}})};
  const auto child = ::fork();
  require(child >= 0, "interrupted fixture fork");
  if (child == 0) {
    try {
      auto root = unwrap(
          RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                    NativeJournalDirectory::open(directory.string()))),
                                "audit", header, capacity));
      AuditLog log{*root};
      ContextStore context{log};
      context.append({Value::object({{"role", Value{"user"}},
                                     {"content", Value{"CRASH_KEEP_ME"}}})},
                     "fixture");
      for (std::size_t i = 0; i < operations.size(); ++i) {
        const auto base = static_cast<unsigned char>(60 + i * 10);
        admit(*root, base, operations[i], inputs[i], false);
        struct ToolEffect final : EffectBoundary {
          std::string_view name;
          explicit ToolEffect(std::string_view operation) : name(operation) {}
          Result<void> dispatch(const EffectIntent &intent) override {
            try {
              LocalTools tools;
              (void)tools.run(name, unwrap(decode_packet(intent.input)));
              return Result<void>::success();
            } catch (const Error &error) {
              return Result<void>::failure(error);
            }
          }
        } effect{operations[i]};
        const auto dispatched =
            unwrap(root->dispatch(id<OperationAttemptId>(base + 2), effect));
        require(dispatched.dispatched && dispatched.effect.has_value() &&
                    dispatched.recording.has_value(),
                "actual interrupted effect dispatch");
      }
      // Simulate loss of the harness after effects/receipts, before any terminal
      // result. No destructor turns this into cooperative cancellation.
      ::_exit(42);
    } catch (...) {
      ::_exit(43);
    }
  }
  int status = 0;
  require(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
              WEXITSTATUS(status) == 42,
          "interrupted harness did not reach the intended effect boundary");
  require(read_file(written) == "ONCE" && read_file(effected) == "EFFECT\n",
          "pre-recovery actual effect oracle");
  auto reopen = [&] {
    auto root = unwrap(
        RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(directory.string()))),
                            "audit", header, capacity));
    unwrap(root->confirm_recovery());
    return root;
  };
  auto root = reopen();
  for (std::size_t i = 0; i < operations.size(); ++i) {
    const auto attempt = unwrap(
        root->attempt(id<OperationAttemptId>(static_cast<unsigned char>(62 + i * 10))));
    require(attempt.opened && attempt.receipt && !attempt.observation &&
                attempt.reconciliation_required,
            "interruption must retain crossed effects and unresolved outcome");
  }
  recover_coding_session(*root, RecoveryMode::acknowledge_local_unknowns);
  for (std::size_t i = 0; i < operations.size(); ++i) {
    const auto identity =
        id<OperationAttemptId>(static_cast<unsigned char>(62 + i * 10));
    const auto attempt = unwrap(root->attempt(identity));
    require(attempt.observation &&
                attempt.observation->disposition == AttemptDisposition::unknown &&
                attempt.admission.input == unwrap(encode_packet(inputs[i])),
            "receipt must not be promoted into effect success on recovery");
    if (!attempt.observation.has_value())
      throw std::runtime_error("recovered attempt lacks terminal observation");
    const auto observation = unwrap(read_packet(attempt.observation->observation));
    require(field(observation, "attempt") == Value{hex_identity(identity.bytes())} &&
                field(observation, "operation") == Value{std::string{operations[i]}} &&
                field(observation, "outcome") == Value{"unknown"},
            "unknown recovery observation lost exact original effect linkage");
    Counter replay;
    require(!unwrap(root->dispatch(identity, replay)).dispatched && replay.calls == 0,
            "interrupted command or file effect replayed");
  }
  AuditLog log{*root};
  ContextStore context{log};
  require(context.items() ==
              Value::Array{Value::object(
                  {{"role", Value{"user"}}, {"content", Value{"CRASH_KEEP_ME"}}})},
          "interrupted context was discarded");
  const auto facts = root->committed_facts().size();
  const auto markers = custody_markers(*root);
  require(markers.size() == 1 &&
              field(field(markers.front(), "metadata"), "attempts") ==
                  Value{Value::Array{
                      Value{hex_identity(id<OperationAttemptId>(82).bytes())}}},
          "actual interrupted command needs durable custody uncertainty");
  root.reset();
  root = reopen();
  recover_coding_session(*root);
  require(root->committed_facts().size() == facts &&
              custody_markers(*root) == markers && read_file(written) == "ONCE" &&
              read_file(effected) == "EFFECT\n",
          "rereopen duplicated settlement or actual effect");
}
std::string lua_text(std::string_view text) {
  std::string quoted = "\"";
  for (const char ch : text) {
    const auto byte = static_cast<unsigned char>(ch);
    quoted += '\\';
    quoted += static_cast<char>('0' + byte / 100);
    quoted += static_cast<char>('0' + (byte / 10) % 10);
    quoted += static_cast<char>('0' + byte % 10);
  }
  return quoted + "\"";
}
bool await_file(const std::filesystem::path &path, std::string_view expected) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (std::chrono::steady_clock::now() < deadline) {
    if (std::filesystem::exists(path) && read_file(path) == expected)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return false;
}
void interrupted_wrappers(const std::filesystem::path &base_path,
                          const JournalHeader &header, JournalCapacity capacity) {
  const auto directory = base_path / "interrupted-wrappers";
  std::filesystem::create_directory(directory);
  require(::chmod(directory.c_str(), 0700) == 0, "wrapper directory permissions");
  const auto written = directory / "written";
  const auto effected = directory / "command-effect";
  const auto release = directory / "release";
  const auto done = directory / "done";
  struct Release {
    std::filesystem::path path;
    std::filesystem::path finished;
    std::filesystem::path started;
    ~Release() {
      try {
        write_file(path, "release");
        if (std::filesystem::exists(started))
          (void)await_file(finished, "DONE\n");
      } catch (...) {
      }
    }
  } cleanup{release, done, effected};
  const auto exec_code =
      "return blackbird.call('exec',{argv=blackbird.array({'/bin/sh','-c'," +
      lua_text("printf 'EFFECT\\n' >> \"$1\"; n=0; while [ ! -f \"$2\" ] && "
               "[ \"$n\" -lt 1000 ]; do n=$((n+1)); sleep 0.01; "
               "done; printf 'DONE\\n' > \"$3\"") +
      ",'fixture'," + lua_text(effected.string()) + "," + lua_text(release.string()) +
      "," + lua_text(done.string()) + "}),yield_ms=0})";
  const auto source =
      "blackbird.call('write_file',{path=" + lua_text(written.string()) +
      ",content='ONCE'}); return blackbird.call('lua',{code=" + lua_text(exec_code) +
      "})";
  const auto definition =
      Value::object({{"name", Value{"recoverfixture"}},
                     {"description", Value{"Recovery fixture"}},
                     {"source", Value{source}},
                     {"aliases", Value{Value::Array{Value{"recoverfixture"}}}},
                     {"bare", Value{false}},
                     {"powerwords", Value{Value::Array{}}}});
  const auto config = Value::object({{"modules", Value{Value::Array{}}},
                                     {"model", Value{""}},
                                     {"effort", Value{""}},
                                     {"workflows", Value{Value::Array{definition}}},
                                     {"workflow_prefix", Value{""}}});
  const auto child = ::fork();
  require(child >= 0, "wrapper harness fork");
  if (child == 0) {
    try {
      auto root = unwrap(
          RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                    NativeJournalDirectory::open(directory.string()))),
                                "audit", header, capacity));
      AuditLog log{*root};
      ContextStore context{log};
      NoProvider provider;
      CodingEngine engine{log, context, provider, "fixture"};
      (void)engine.operator_call("program_config",
                                 Value::object({{"proposal", config}}));
      engine.operation_completed = [&](std::string_view completed) {
        if (completed.starts_with("exec backgrounded")) {
          if (!await_file(effected, "EFFECT\n"))
            ::_exit(44);
          ::_exit(42);
        }
      };
      (void)engine.operator_turn("/recoverfixture", "error('unexpected fallback')");
      ::_exit(45);
    } catch (const Error &error) {
      std::cerr << "wrapper crash fixture: " << error_name(error.code) << '\n';
      ::_exit(43);
    }
  }
  int status = 0;
  require(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
              WEXITSTATUS(status) == 42,
          "actual wrapper harness did not crash with nested command live");
  require(read_file(written) == "ONCE" && read_file(effected) == "EFFECT\n" &&
              !std::filesystem::exists(done),
          "wrapper effects must precede crash with command still independently live");
  auto root =
      unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                                     NativeJournalDirectory::open(directory.string()))),
                                 "audit", header, capacity));
  unwrap(root->confirm_recovery());
  const auto unresolved = unwrap(root->unresolved_attempts());
  std::vector<std::pair<OperationAttemptId, std::string>> abandoned;
  for (const auto &attempt : unresolved) {
    const auto decision = unwrap(root->decision(attempt.admission.decision));
    const auto packet = unwrap(read_packet(decision.continuation));
    abandoned.emplace_back(attempt.admission.attempt,
                           string_field(packet, "operation"));
  }
  std::vector<std::string> names;
  for (const auto &[attempt, operation] : abandoned) {
    (void)attempt;
    names.push_back(operation);
  }
  std::sort(names.begin(), names.end());
  require(names == std::vector<std::string>{"exec", "lua", "workflow_execute",
                                            "workflow_invoke"},
          "actual interrupted wrappers differ from independent expected set");
  recover_coding_session(*root, RecoveryMode::acknowledge_local_unknowns);
  for (const auto &[identity, operation] : abandoned) {
    const auto attempt = unwrap(root->attempt(identity));
    require(attempt.observation &&
                attempt.observation->disposition == AttemptDisposition::unknown,
            "wrapper or inner command recovery claimed success");
    if (!attempt.observation.has_value())
      throw std::runtime_error("recovered attempt lacks terminal observation");
    const auto observation = unwrap(read_packet(attempt.observation->observation));
    require(field(observation, "attempt") == Value{hex_identity(identity.bytes())} &&
                field(observation, "operation") == Value{operation},
            "wrapper recovery lost original effect identity");
    Counter replay;
    require(!unwrap(root->dispatch(identity, replay)).dispatched && replay.calls == 0,
            "actual workflow wrapper replayed");
  }
  require(read_file(written) == "ONCE" && read_file(effected) == "EFFECT\n" &&
              !std::filesystem::exists(done),
          "recovery repeated or adopted prior command");
  write_file(release, "release");
  const bool completed = await_file(done, "DONE\n");
  const bool marker_exists = std::filesystem::exists(done);
  const auto observed_done = marker_exists ? read_file(done) : std::string{};
  const auto observed_effect = read_file(effected);
  if (!completed || observed_done != "DONE\n" || observed_effect != "EFFECT\n")
    std::cerr << "wrapper completion=" << completed
              << " marker exists=" << marker_exists
              << " done bytes=" << observed_done.size() << " [" << observed_done
              << "] effect bytes=" << observed_effect.size() << " [" << observed_effect
              << "]\n";
  require(completed && observed_done == "DONE\n" && observed_effect == "EFFECT\n",
          "recovery killed prior command or replayed its effect");
}
} // namespace
int main() {
  char name[] = "/tmp/arco-recovery-XXXXXX";
  const auto path = mkdtemp(name);
  if (!path)
    return 2;
  try {
    const JournalHeader header{id<EnvironmentId>(1),
                               id<AuditStreamId>(2),
                               3,
                               {1024 * 1024, 4 * 1024 * 1024},
                               std::nullopt};
    const JournalCapacity capacity{16 * 1024 * 1024, 10000};
    for (const std::string operation :
         {"provider", "unopened", "capacity", "badinput", "exec", "write_file",
          "unrecognized", "mixed", "bound-provider", "bound-unopened",
          "bound-wrong-invocation", "bound-wrong-tag", "bound-extra-input",
          "bound-bad-input"}) {
      const auto directory = std::filesystem::path{path} / operation;
      std::filesystem::create_directory(directory);
      check(::chmod(directory.c_str(), 0700) == 0);
      auto root = unwrap(
          RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                    NativeJournalDirectory::open(directory.string()))),
                                "audit", header, capacity));
      AuditLog log{*root};
      ContextStore context{log};
      context.append(
          {Value::object({{"role", Value{"user"}}, {"content", Value{"KEEP_ME"}}})},
          "fixture");
      const auto before = context.items();
      const bool provider_kind = operation == "provider" || operation == "mixed" ||
                                 operation == "unopened" || operation == "capacity" ||
                                 operation == "badinput" ||
                                 operation.starts_with("bound-");
      auto metadata_packet = Value::object(
          {{"operation", Value{provider_kind ? "provider" : operation}},
           {"input",
            operation == "badinput" ? Value{Value::Array{}} : Value::object({})},
           {"revision", Value{hex_identity(id<ContextRevisionId>(10).bytes())}},
           {"generation", Value{hex_identity(id<DefinitionGenerationId>(7).bytes())}}});
      if (operation.starts_with("bound-")) {
        auto &fields = metadata_packet.object();
        if (operation != "bound-extra-input")
          fields.erase(
              std::remove_if(fields.begin(), fields.end(),
                             [](const auto &entry) { return entry.first == "input"; }),
              fields.end());
        fields.emplace_back(
            "input_binding",
            Value{operation == "bound-wrong-tag" ? "unknown-v1" : "invocation-v1"});
        fields.emplace_back(
            "invocation",
            Value{hex_identity(
                id<InvocationId>(operation == "bound-wrong-invocation" ? 20 : 9)
                    .bytes())});
      }
      const auto metadata = unwrap(encode_packet_string(metadata_packet));
      const std::vector<std::byte> input_bytes =
          operation == "bound-bad-input"
              ? std::vector<std::byte>{std::byte{0x5b}, std::byte{0x5d}}
              : unwrap(encode_packet(Value::object({})));
      const auto raw = std::as_bytes(std::span{metadata.data(), metadata.size()});
      (void)unwrap(root->submit({{},
                                 DecisionEvent{id<DecisionId>(8),
                                               id<ParticipantId>(4),
                                               id<ConversationId>(5),
                                               id<WorkflowId>(6),
                                               id<DefinitionGenerationId>(7),
                                               id<ContextRevisionId>(10),
                                               {id<InvocationId>(9)},
                                               {raw.begin(), raw.end()}}}));
      (void)unwrap(
          root->submit({{},
                        InvocationEvent{id<InvocationId>(9), id<DecisionId>(8),
                                        id<DefinitionGenerationId>(7), input_bytes}}));
      (void)unwrap(root->submit(
          {{},
           AttemptAdmissionEvent{id<OperationAttemptId>(11), id<InvocationId>(9),
                                 id<DecisionId>(8), input_bytes}}));
      if (operation != "unopened" && operation != "bound-unopened")
        (void)unwrap(root->submit({{}, AttemptOpenEvent{id<OperationAttemptId>(11)}}));
      if (operation == "mixed") {
        const std::string metadata_exec = "{\"operation\":\"exec\"}";
        const auto exec_bytes =
            std::as_bytes(std::span{metadata_exec.data(), metadata_exec.size()});
        (void)unwrap(
            root->submit({{},
                          DecisionEvent{id<DecisionId>(13),
                                        id<ParticipantId>(4),
                                        id<ConversationId>(5),
                                        id<WorkflowId>(6),
                                        id<DefinitionGenerationId>(7),
                                        id<ContextRevisionId>(10),
                                        {id<InvocationId>(12)},
                                        {exec_bytes.begin(), exec_bytes.end()}}}));
        (void)unwrap(root->submit({{},
                                   InvocationEvent{id<InvocationId>(12),
                                                   id<DecisionId>(13),
                                                   id<DefinitionGenerationId>(7),
                                                   {}}}));
        (void)unwrap(root->submit({{},
                                   AttemptAdmissionEvent{id<OperationAttemptId>(14),
                                                         id<InvocationId>(12),
                                                         id<DecisionId>(13),
                                                         {}}}));
        (void)unwrap(root->submit({{}, AttemptOpenEvent{id<OperationAttemptId>(14)}}));
      }
      const auto saved_bytes = root->cursor().end_offset;
      root.reset();
      auto reopen = [&] {
        auto result = unwrap(RetainedState::open(
            std::make_unique<NativeJournalDirectory>(
                unwrap(NativeJournalDirectory::open(directory.string()))),
            "audit", header,
            operation == "capacity" ? JournalCapacity{saved_bytes, capacity.max_records}
                                    : capacity));
        unwrap(result->confirm_recovery());
        return result;
      };
      root = reopen();
      check(unwrap(root->attempt(id<OperationAttemptId>(11))).reconciliation_required);
      const auto prior_facts = root->committed_facts().size();
      bool blocked = false;
      try {
        recover_coding_session(*root);
      } catch (const Error &e) {
        blocked = e.code == (operation == "capacity" ? ErrorCode::capacity
                                                     : ErrorCode::external_unknown);
      }
      if (operation == "capacity") {
        check(blocked && root->state() == JournalWriterState::blocked);
        check(root->committed_facts().size() == prior_facts);
        check(
            unwrap(root->attempt(id<OperationAttemptId>(11))).reconciliation_required);
        Counter effect;
        check(!root->dispatch(id<OperationAttemptId>(11), effect).has_value() &&
              effect.calls == 0);
        continue;
      }
      if (operation != "provider" && operation != "unopened" &&
          operation != "bound-provider" && operation != "bound-unopened") {
        check(blocked && root->state() == JournalWriterState::recovered);
        check(root->committed_facts().size() == prior_facts);
        check(!unwrap(root->attempt(id<OperationAttemptId>(11))).observation);
        if (operation != "exec" && operation != "write_file") {
          blocked = false;
          try {
            recover_coding_session(*root, RecoveryMode::acknowledge_local_unknowns);
          } catch (const Error &error) {
            blocked = error.code == ErrorCode::external_unknown;
          }
          require(blocked && root->state() == JournalWriterState::recovered &&
                      root->committed_facts().size() == prior_facts &&
                      !unwrap(root->attempt(id<OperationAttemptId>(11))).observation,
                  "explicit acknowledgement bypassed invalid or legacy linkage");
        }
        continue;
      }
      check(!blocked && root->state() == JournalWriterState::live);
      const auto attempt = unwrap(root->attempt(id<OperationAttemptId>(11)));
      check(!attempt.reconciliation_required && attempt.observation &&
            attempt.observation->disposition == AttemptDisposition::unknown);
      if (!attempt.observation)
        throw Error{ErrorCode::corrupt};
      const auto &observed = attempt.observation->observation;
      check(std::string_view{reinterpret_cast<const char *>(observed.data()),
                             observed.size()}
                .find("outcome unknown") != std::string_view::npos);
      Counter effect;
      check(!unwrap(root->dispatch(id<OperationAttemptId>(11), effect)).dispatched);
      check(effect.calls == 0);
      AuditLog recovered_log{*root};
      ContextStore recovered_context{recovered_log};
      check(recovered_context.items() == before);
      const auto count = root->committed_facts().size();
      root.reset();
      root = reopen();
      recover_coding_session(*root);
      check(root->committed_facts().size() == count);
    }
    local_recovery_cases(path, header, capacity);
    interrupted_effects(path, header, capacity);
    interrupted_wrappers(path, header, capacity);
    std::filesystem::remove_all(path);
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << " (" << e.detail << ")" << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
}
