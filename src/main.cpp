#include "blackbird/backstop.hpp"
#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/raw_command.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/sprite.hpp"
#include "blackbird/station.hpp"
#include "blackbird/terminal.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <poll.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace blackbird;
namespace {
std::atomic_bool interrupted{false};
static_assert(std::atomic_bool::is_always_lock_free);
template <class T> T random_id() {
  IdentityBytes bytes{};
  if (getentropy(bytes.data(), bytes.size()) != 0)
    throw Error{ErrorCode::io, errno};
  return unwrap(T::from_bytes(bytes));
}
std::string safe(std::string_view text) {
  std::string out;
  constexpr char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < text.size(); ++i) {
    const auto c = static_cast<unsigned char>(text[i]);
    const bool c1 = c == 0xc2 && i + 1 < text.size() &&
                    static_cast<unsigned char>(text[i + 1]) >= 0x80 &&
                    static_cast<unsigned char>(text[i + 1]) <= 0x9f;
    if ((c < 32 && c != '\n' && c != '\t') || c == 127 || c1) {
      out += "\\x";
      out += hex[c >> 4U];
      out += hex[c & 15U];
      if (c1) {
        const auto next = static_cast<unsigned char>(text[++i]);
        out += "\\x";
        out += hex[next >> 4U];
        out += hex[next & 15U];
      }
    } else
      out += text[i];
  }
  return out;
}
} // namespace
int run_session(int argc, char **argv, std::vector<std::string> &next_session,
                std::vector<std::string> &fallback_session) {
  interrupted.store(false, std::memory_order_relaxed);
  try {
    const char *home = std::getenv("HOME");
    if (home == nullptr)
      throw Error{ErrorCode::invalid_range};
    std::filesystem::path session = default_session_directory(home);
    std::filesystem::path workflow = BLACKBIRD_WORKFLOW;
    std::string model = "gpt-6.1-sol", once, seed_path, backstop_path, station_path;
    Value backstop_mission;
    bool one = false, inspect = false, plain = false, rebuild_session = false,
         expire_diagnostics = false, recover_unknown_effects = false;
    std::string effort = "medium";
    bool model_option = false, effort_option = false, workflow_option = false;
    bool resume_requested = false, discovery = false, switched_session = false;
    auto discovery_root = session.parent_path();
    for (int i = 1; i < argc; ++i) {
      const std::string_view arg = argv[i];
      if (arg == "--new-session-instance") {
        switched_session = true;
        continue;
      }
      if (arg == "--help") {
        std::cout << "blackbird [--session DIRECTORY] [--model NAME] [--workflow FILE] "
                     "[--once "
                     "PROMPT] [--audit-last] [--effort low|medium|high|xhigh] "
                     "[--plain] [--list-sessions [ROOT]] [--seed-session JSON] "
                     "[--backstop MISSION_JSON (requires --once)] "
                     "[--resume-continue|--resume-once] [--rebuild-session] "
                     "[--expire-diagnostics] [--recover-unknown-effects]\nInteractive: "
                     "/context, "
                     "/originals, /restore "
                     "ENTRY, /lua CODE, /model NAME, /effort LEVEL, /workflow FILE, "
                     "/session, /new, "
                     "/restart NOTE, /paste (until /send), /quit or /exit\n";
        return 0;
      }
      if (arg == "--list-sessions") {
        discovery = true;
        if (i + 1 < argc && !std::string_view{argv[i + 1]}.starts_with("--"))
          discovery_root = argv[++i];
        continue;
      }
      if (arg == "--resume-continue" || arg == "--resume-once") {
        resume_requested = true;
        if (arg == "--resume-once")
          one = true;
        continue;
      }
      if (arg == "--expire-diagnostics") {
        expire_diagnostics = true;
        continue;
      }
      if (arg == "--rebuild-session") {
        rebuild_session = true;
        continue;
      }
      if (arg == "--recover-unknown-effects") {
        recover_unknown_effects = true;
        continue;
      }
      if (arg == "--plain") {
        plain = true;
        continue;
      }
      if (arg == "--audit-last") {
        inspect = true;
        continue;
      }
      if (i + 1 == argc)
        throw Error{ErrorCode::invalid_range};
      if (arg == "--station")
        station_path = argv[++i];
      else if (arg == "--backstop")
        backstop_path = argv[++i];
      else if (arg == "--seed-session")
        seed_path = argv[++i];
      else if (arg == "--session")
        session = argv[++i];
      else if (arg == "--effort") {
        effort = argv[++i];
        effort_option = true;
      } else if (arg == "--workflow") {
        workflow = argv[++i];
        workflow_option = true;
      } else if (arg == "--model") {
        model = argv[++i];
        model_option = true;
      } else if (arg == "--once") {
        once = argv[++i];
        one = true;
      } else
        throw Error{ErrorCode::invalid_range};
    }
    if (!station_path.empty() &&
        (one || inspect || discovery || !backstop_path.empty() || !seed_path.empty()))
      throw Error{ErrorCode::conflict};
    if (!backstop_path.empty()) {
      if (!one || resume_requested || inspect || discovery || !seed_path.empty() ||
          once.empty() || once.starts_with("/"))
        throw Error{ErrorCode::conflict};
      backstop_mission = unwrap(parse_json(read_file(backstop_path, 32768)));
      if (string_field(backstop_mission, "mission").empty())
        throw Error{ErrorCode::invalid_range};
    }
    if (discovery && !seed_path.empty())
      throw Error{ErrorCode::conflict};
    if (discovery) {
      std::cout << unwrap(dump_json(list_sessions(discovery_root))) << '\n';
      return 0;
    }
    Value seed;
    if (!seed_path.empty()) {
      if (inspect || discovery || resume_requested ||
          std::filesystem::exists(session / "audit"))
        throw Error{ErrorCode::conflict};
      seed = unwrap(parse_json(read_file(seed_path, 1024 * 1024)));
      ContextStore::validate_successor_seed(seed);
    }
    if (expire_diagnostics) {
      if (one || inspect || discovery || resume_requested || rebuild_session ||
          !seed_path.empty() || !station_path.empty() || !backstop_path.empty())
        throw Error{ErrorCode::conflict};
      auto maintenance = unwrap(NativeJournalDirectory::open(session.string()));
      const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
      if (now < 0)
        throw Error{ErrorCode::invalid_range};
      std::size_t scanned = 0, removed = 0;
      bool complete = false;
      for (unsigned pass = 0; pass < 128 && !complete; ++pass) {
        const auto result =
            unwrap(maintenance.expire_diagnostics(static_cast<std::uint64_t>(now)));
        scanned += result.scanned;
        removed += result.removed;
        if (result.error)
          throw *result.error;
        complete = result.complete;
      }
      std::cout << "Diagnostic expiry: " << removed << " removed, " << scanned
                << " entries visited; "
                << (complete ? "pass complete.\n"
                             : "work bound reached; rerun to continue.\n");
      return 0;
    }
    std::filesystem::create_directories(session);
    if (::chmod(session.c_str(), 0700) != 0)
      throw Error{ErrorCode::io, errno};
    const auto path = session / "audit";
    const JournalCapacity capacity{512ULL * 1024 * 1024, 200000};
    std::unique_ptr<RawRuntime> instrumentation;
    std::optional<RawThreadBinding> instrumentation_binding;
    std::optional<RawRegion> startup;
    const auto start_instrumentation = [&](EnvironmentId environment) {
      if (inspect)
        return;
      instrumentation = unwrap(RawRuntime::open(session.string(), environment));
      instrumentation_binding.emplace(unwrap(instrumentation->prepare_thread()));
      startup.emplace(raw_regions::startup_initialize);
    };
    std::unique_ptr<RetainedState> root;
    auto directory = std::make_unique<NativeJournalDirectory>(
        unwrap(NativeJournalDirectory::open(session.string())));
    if (std::filesystem::exists(path)) {
      if (!seed_path.empty())
        throw Error{ErrorCode::conflict};
      std::array<std::byte, journal_header_size> bytes{};
      std::ifstream file{path, std::ios::binary};
      file.read(reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
      if (!file)
        throw Error{ErrorCode::incomplete};
      const auto header = unwrap(decode_journal_header(bytes));
      if (header.predecessor) {
        std::cerr
            << "Linked audit segments require an explicit migration; this launcher "
               "cannot reopen them. Session files left intact.\n";
        return 1;
      }
      start_instrumentation(header.environment);
      // Small sessions can rebuild automatically. Large histories require an
      // explicit slow path if their derived state is missing or unsupported.
      constexpr std::uint64_t automatic_replay_bytes = 64ULL * 1024 * 1024;
      const auto replay_limit = rebuild_session ? UINT64_MAX : automatic_replay_bytes;
      if (rebuild_session)
        std::cerr << "Session recovery: full replay allowed (up to 512 MiB / 200000 "
                     "records); uncertain operations will not be retried.\n";
      RawRegion restoration{raw_regions::startup_restore};
      auto opened = RetainedState::open(std::move(directory), "audit", header, capacity,
                                        false, true, replay_limit);
      if (!opened.has_value() && opened.error().code == ErrorCode::unsupported &&
          !rebuild_session) {
        std::cerr << "Session needs full recovery: saved state is missing, invalid, or "
                     "does not cover this session. Automatic replay stops at 64 MiB. "
                     "Reopen with --rebuild-session for the bounded slow path, or "
                     "select a new --session directory. Session files left intact.\n";
        return 1;
      }
      root = unwrap(std::move(opened));
      if (!root->compact_recovery())
        std::cerr << "Session recovery: rebuilding from audit; uncertain operations "
                     "will not be retried.\n";
      unwrap(root->confirm_recovery());
      if (!inspect) {
        try {
          recover_coding_session(*root, recover_unknown_effects
                                            ? RecoveryMode::acknowledge_local_unknowns
                                            : RecoveryMode::provider_only);
          if (recover_unknown_effects)
            std::cerr << "Explicit recovery: abandoned local effects stay unknown. "
                         "No command replay or old PID adoption; prior process "
                         "containment is not established.\n";
        } catch (const Error &e) {
          if (e.code != ErrorCode::external_unknown)
            throw;
          std::cerr << "Session has an operation with uncertain custody; reopen cannot "
                       "grant permission to retry it. Inspect with --audit-last "
                       "(--rebuild-session for a large audit), resolve the effect "
                       "outside this session, explicitly acknowledge structurally "
                       "linked local unknowns with --recover-unknown-effects, or "
                       "select a new --session directory.\n";
          return 1;
        }
      }
      restoration.outcome(root->compact_recovery() ? RawRegionOutcome::compact
                                                   : RawRegionOutcome::full_replay);
    } else {
      std::uint64_t name_space = 0;
      if (getentropy(&name_space, sizeof(name_space)) != 0 || name_space == 0)
        throw Error{ErrorCode::io};
      JournalHeader header{random_id<EnvironmentId>(),
                           random_id<AuditStreamId>(),
                           name_space,
                           {32 * 1024 * 1024, 96 * 1024 * 1024},
                           std::nullopt};
      start_instrumentation(header.environment);
      root = unwrap(
          RetainedState::create(std::move(directory), "audit", header, capacity));
    }
    if (inspect) {
      const auto usage = root->journal_usage();
      const auto remaining = usage.remaining_bytes();
      std::cout << "Audit observed headroom: "
                << (remaining ? std::to_string(*remaining) + " bytes"
                              : "bytes unavailable")
                << ", " << usage.remaining_records()
                << " indexed records; no admission permission or handoff reserve\n";
      for (std::size_t ordinal = 0; ordinal < root->fact_count(); ++ordinal) {
        const auto fact = unwrap(root->fact(ordinal));
        const auto *admission = std::get_if<AttemptAdmissionEvent>(&fact.event.body);
        if (!admission ||
            !unwrap(root->attempt(admission->attempt)).reconciliation_required)
          continue;
        std::string operation = "unclassified";
        for (std::size_t ordinal2 = 0; ordinal2 < root->fact_count(); ++ordinal2) {
          const auto related = unwrap(root->fact(ordinal2));
          const auto *decision = std::get_if<DecisionEvent>(&related.event.body);
          if (!decision || decision->decision != admission->decision)
            continue;
          const auto metadata = read_packet(decision->continuation);
          if (metadata.has_value())
            if (const auto *name = metadata.value().find("operation");
                name && std::holds_alternative<std::string>(name->value()))
              operation = name->string();
        }
        std::cout << "Unfinished " << safe(operation) << " attempt "
                  << hex_identity(admission->attempt.bytes()) << " (outcome unknown)\n";
      }
      std::size_t shown = 0;
      for (std::size_t i = root->fact_count(); i > 0 && shown < 12; --i) {
        const auto fact = unwrap(root->fact(i - 1));
        const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
        if (record == nullptr || record->channel != ApplicationChannel::log ||
            fact.event.dependencies.empty())
          continue;
        const auto meta = unwrap(dump_json(unwrap(read_packet(record->payload))));
        std::cout << safe(meta) << '\n';
        for (const auto reference : fact.event.dependencies) {
          const auto bytes = unwrap(root->source(reference));
          const std::string_view data{reinterpret_cast<const char *>(bytes.data()),
                                      bytes.size()};
          std::cout << safe(data) << '\n';
        }
        ++shown;
      }
      return 0;
    }
    AuditLog log{*root};
    ContextStore context{log};
    if (!seed_path.empty())
      context.seed_successor(seed);
    // Restore station scheduling only for an explicit native RRC resume. Ordinary
    // campaign/once invocations never acquire feed admissions by implication.
    if (resume_requested && station_path.empty())
      for (const auto &p : root->current_programs()) {
        const auto *label = p.find("label");
        if (label && std::holds_alternative<std::string>(label->value()) &&
            label->string() == "station.profile")
          station_path = string_field(p, "adapter");
      }
    if (!station_path.empty() && one)
      throw Error{ErrorCode::conflict};
    log.record(
        ApplicationChannel::program,
        Value::object({{"label", Value{"station.profile"}},
                       {"adapter", Value{station_path.empty()
                                             ? ""
                                             : std::filesystem::absolute(station_path)
                                                   .lexically_normal()
                                                   .string()}}}));
    SessionStore session_store{log};

    auto settings = session_store.settings();
    if (model_option)
      settings.model = model;
    if (effort_option)
      settings.effort = effort;
    if (workflow_option || settings.workflow.empty())
      settings.workflow =
          std::filesystem::absolute(workflow).lexically_normal().string();
    (void)read_file(settings.workflow);
    session_store.save(settings);
    model = settings.model;
    effort = settings.effort;
    workflow = settings.workflow;
    bool resume_turn = resume_requested && session_store.resume(context);
    if (resume_requested && !resume_turn && one)
      return 0;
    std::atomic_bool restart_pending{false};
    std::atomic_bool new_pending{false};
    OpenAiCodingProvider provider;
    CodingEngine engine{log, context, provider, model};
    if (engine.uncontained_command_custody())
      std::cerr << "Prior command custody remains unknown; restart and automatic "
                   "backstop stay fenced. No old PID adopted or effect replayed.\n";
    // Convert an old derived state once, after the effective identity exists.
    // Unsupported station/restart states remain on independent full recovery.
    if (!root->compact_recovery()) {
      bool supported = unwrap(root->unresolved_attempts()).empty();
      for (const auto &packet : root->current_programs()) {
        const auto &label = string_field(packet, "label");
        if ((label.starts_with("station.") &&
             !(label == "station.profile" &&
               string_field(packet, "adapter").empty())) ||
            (label == "session.restart" && string_field(packet, "state") == "pending"))
          supported = false;
      }
      if (supported)
        unwrap(context.checkpoint());
    }
    engine.effort(effort);
    RawCommandInterrupt interrupt_owner{interrupted};
    auto &interrupt_wake = interrupt_owner.wake();
    RawWaitScope interrupt_wait{{{interrupt_wake.read_fd(), -1}, {}}};
    std::atomic_bool cancelled{false};
    engine.cancelled = [&] {
      return interrupted.load(std::memory_order_relaxed) || cancelled.load();
    };
    TerminalUI ui{"Blackbird · " + model + " · " + effort + " · " + session.string()};
    struct ParticipantShutdown {
      CodingEngine &engine;
      ~ParticipantShutdown() {
        try {
          engine.shutdown_commands();
        } catch (...) {
        }
        try {
          engine.shutdown_participants();
        } catch (...) {
        }
      }
    } participant_shutdown{engine}; // Before UI destruction, on every exit path.
    const bool tui = station_path.empty() && !plain && !one && isatty(STDIN_FILENO) &&
                     isatty(STDOUT_FILENO);
    auto emit = [&](std::string_view text) {
      if (tui)
        ui.text(text);
      else
        std::cout << safe(text) << std::flush;
    };
    auto refresh_info = [&] {
      try {
        save_session_info(session, session_store.settings(), session_identity(log));
      } catch (...) {
        emit("Session discovery snapshot save failed; audit settings remain "
             "authoritative.\n");
      }
    };
    refresh_info();
    engine.display = emit;
    if (tui)
      ui.tasks(engine.tasks().snapshot());
    engine.tasks().changed = [&](const Value &packet) {
      if (tui)
        ui.tasks(packet);
    };
    engine.task_activity = [&, tui](const Value &event) {
      if (tui)
        ui.task_activity(event);
    };
    engine.process_output = [&](std::string_view text) {
      if (tui)
        ui.process_output(text);
      else
        emit(text);
    };
    engine.operation_started = [&](std::string_view operation) {
      if (tui)
        ui.operation_started(operation == "provider");
    };
    engine.diagnostic = [&](std::string_view text) {
      if (!tui)
        emit(text);
    };
    engine.notice = [&](std::string_view text) {
      if (tui)
        ui.notice(text);
      else
        emit(text);
    };
    engine.observed_usage = [&](const Value &usage) {
      if (!tui)
        return;
      std::string summary;
      for (const auto &[key, suffix] :
           {std::pair{"input_tokens", " in"}, std::pair{"output_tokens", " out"}}) {
        if (const auto *value = usage.find(key)) {
          if (!summary.empty())
            summary += " · ";
          summary += value->number().text() + suffix;
        }
      }
      ui.usage(summary);
    };
    engine.status = [&](std::string_view text) {
      if (tui) {
        ui.status(text);

      } else
        std::cerr << '[' << safe(text) << "]\n";
    };
    engine.operation_outcome = [&](std::string_view text, AttemptDisposition outcome) {
      if (tui)
        ui.operation_completed(text,
                               outcome == AttemptDisposition::success   ? Ink::success
                               : outcome == AttemptDisposition::unknown ? Ink::muted
                                                                        : Ink::failure);
      else
        std::cerr << '[' << safe(text) << "]\n";
    };
    publish_workflows(engine.workflows());
    auto perform = [&](std::string_view prompt) {
      if (!tui) {
        unwrap(interrupt_owner.reset());
      }
      try {
        if (prompt == "/help") {
          emit(terminal_help());
        } else if (prompt.starts_with("/exec ") || prompt.starts_with("/process ")) {
          const bool execute = prompt.starts_with("/exec ");
          emit(unwrap(dump_json(engine.operator_call(
                   execute ? "exec" : "process",
                   unwrap(parse_json(prompt.substr(execute ? 6 : 9)))))) +
               "\n");
        } else if (prompt.starts_with("/fg ")) {
          emit(
              unwrap(dump_json(engine.operator_call(
                  "process",
                  Value::object({{"op", Value{"foreground"}},
                                 {"job_id", Value{std::string{prompt.substr(4)}}}})))) +
              "\n");
        } else if (prompt == "/jobs") {
          emit(unwrap(dump_json(engine.command_jobs()->read(Value::object({})))) +
               "\n");
        } else if (prompt == "/bg" || prompt.starts_with("/bg ")) {
          const auto lease = engine.command_jobs()->foreground();
          const auto id = prompt == "/bg" ? string_field(lease, "job_id")
                                          : std::string{prompt.substr(4)};
          emit(unwrap(dump_json(engine.operator_call(
                   "process",
                   Value::object({{"op", Value{"background"}},
                                  {"job_id", Value{id}},
                                  {"wait_epoch", field(lease, "wait_epoch")}})))) +
               "\n");
        } else if (prompt == "continue" && resume_turn) {
          resume_turn = false;
          engine.turn({"", read_file(workflow)});
        } else if (prompt == "/tasks" || prompt.starts_with("/tasks ")) {
          const auto command =
              prompt == "/tasks" ? std::string_view{} : prompt.substr(7);
          const auto result = engine.tasks().operator_command(command);
          if (const auto *help = result.find("help"))
            emit(help->string() + "\n");
          else if (command.starts_with("read ") || command.starts_with("apply ") ||
                   result.find("conflict"))
            emit(unwrap(dump_json(result)) + "\n");
          else {
            const auto *items = result.find("items");
            const auto &rows =
                items ? items->array() : field(result, "changed").array();
            for (const auto &row : rows)
              emit(std::string{string_field(row, "parent").empty() ? "" : "  "} +
                   string_field(row, "id") + " [" + string_field(row, "status") + "] " +
                   string_field(row, "title") + "\n");
            if (const auto *removed = result.find("removed"))
              for (const auto &id : removed->array())
                emit("Archived " + id.string() + "\n");
            if (rows.empty())
              emit(items ? "No tasks.\n" : "Task list updated.\n");
          }
        } else if (prompt == "/runs" || prompt.starts_with("/runs ")) {
          Value result;
          if (prompt.starts_with("/runs configure "))
            result = engine.operator_call("participant_configure",
                                          unwrap(parse_json(prompt.substr(16))));
          else if (prompt.starts_with("/runs archive "))
            result = engine.operator_call(
                "participant_archive",
                Value::object({{"run_id", Value{std::string{prompt.substr(14)}}}}));
          else
            result = engine.operator_call(
                "participant_read",
                prompt == "/runs"
                    ? Value::object({})
                    : Value::object(
                          {{"run_id", Value{std::string{prompt.substr(6)}}}}));
          if (const auto *runs = result.find("runs")) {
            if (runs->array().empty())
              emit("No participant runs.\n");
            for (const auto &run : runs->array())
              emit(string_field(run, "run_id") + " [" + string_field(run, "state") +
                   "] " + string_field(run, "provider") + "/" +
                   string_field(run, "model") + " → " + string_field(run, "to") +
                   (field(run, "cancel_requested") == Value{true}
                        ? " · cancellation requested"
                        : "") +
                   "\n");
          } else
            emit(unwrap(dump_json(result)) + "\n");
        } else if (prompt.starts_with("/run ") || prompt.starts_with("/send ") ||
                   prompt.starts_with("/join ")) {
          const auto name = prompt.starts_with("/run ")    ? "participant_start"
                            : prompt.starts_with("/send ") ? "participant_send"
                                                           : "participant_join";
          const auto offset = prompt.starts_with("/run ") ? 5U : 6U;
          emit(unwrap(dump_json(engine.operator_call(
                   name, unwrap(parse_json(prompt.substr(offset)))))) +
               "\n");
        } else if (prompt.starts_with("/await ")) {
          emit(unwrap(dump_json(engine.operator_call(
                   "participant_await", unwrap(parse_json(prompt.substr(7)))))) +
               "\n");
        } else if (prompt.starts_with("/stop ")) {
          emit(
              unwrap(dump_json(engine.operator_call(
                  "participant_cancel",
                  Value::object({{"run_id", Value{std::string{prompt.substr(6)}}}})))) +
              "\n");
        } else if (prompt == "/auth" || prompt == "/auth status") {
          emit(safe(unwrap(dump_json(ProviderAuth{}.status()))) + "\n");
        } else if (prompt == "/auth providers") {
          emit(safe(unwrap(dump_json(ProviderAuth{}.catalog()))) + "\n");
        } else if (prompt.starts_with("/auth ")) {
          emit("Use ./scripts/blackbird auth outside chat for sign-in/key/use/logout. "
               "Secret entry is excluded from session history.\n");
        } else if (prompt == "/variables" || prompt.starts_with("/variables ") ||
                   prompt == "/correlate" || prompt.starts_with("/correlate ")) {
          const bool variables = prompt.starts_with("/variables");
          const auto space = prompt.find(' ');
          const auto query = space == std::string_view::npos
                                 ? Value::object({})
                                 : unwrap(parse_json(prompt.substr(space + 1)));
          const auto result =
              engine.operator_call(variables ? "variables_read" : "trajectory_read",
                                   Value::object({{"query", query}}));
          emit(result.find("events") ? correlation_plot(result)
                                     : safe(unwrap(dump_json(result))) + "\n");
        } else if (prompt == "/git-observe") {
          emit("/git-observe {\"path\":\"/absolute/repository\",\"history\":8}\n");
        } else if (prompt.starts_with("/git-observe ")) {
          emit(safe(unwrap(dump_json(engine.operator_call(
                   "git_observe", unwrap(parse_json(prompt.substr(13))))))) +
               "\n");
        } else if (prompt == "/trace" || prompt.starts_with("/trace ")) {
          const auto query = prompt == "/trace" ? Value::object({})
                                                : unwrap(parse_json(prompt.substr(7)));
          const auto result = engine.operator_call("trajectory_read",
                                                   Value::object({{"query", query}}));
          if (!result.find("events")) {
            emit(safe(unwrap(dump_json(result))) + "\n");
          } else {
            emit("record  event  detail  outcome  attempt\n");
            for (const auto &row : field(result, "events").array()) {
              auto text = [&](std::string_view key) {
                const auto *v = row.find(key);
                return v ? v->string() : std::string{"-"};
              };
              const auto *metadata = row.find("metadata");
              const auto *capture = metadata ? metadata->find("attempt") : nullptr;
              const auto attempt = row.find("attempt") ? text("attempt")
                                   : capture           ? capture->string()
                                                       : "-";
              const auto detail = row.find("operation") ? text("operation")
                                  : row.find("label")   ? text("label")
                                                        : "-";
              emit(field(row, "record").number().text() + "  " + text("event") + "  " +
                   safe(detail) + "  " + text("outcome") + "  " + safe(attempt) + "\n");
            }
            emit("next=" + field(result, "next").number().text() +
                 " end=" + field(result, "end").number().text() +
                 "; pin end for paging; originals via audit_inspect\n");
          }
        } else if (prompt == "/colleagues") {
          emit(unwrap(dump_json(
                   engine.operator_call("colleague_catalog", Value::object({})))) +
               "\n");
        } else if (prompt.starts_with("/colleague ")) {
          emit(unwrap(dump_json(engine.operator_call(
                   "colleague", unwrap(parse_json(prompt.substr(11)))))) +
               "\n");
        } else if (prompt == "/session") {
          const auto identity = session_identity(log);
          emit(
              unwrap(dump_json(Value::object(
                  {{"name", Value{session_store.settings().name}},
                   {"actor", Value{hex_identity(identity.actor.bytes())}},
                   {"directory", Value{session.string()}},
                   {"conversation", Value{hex_identity(identity.conversation.bytes())}},
                   {"workflow_id", Value{hex_identity(identity.workflow.bytes())}},
                   {"model", Value{model}},
                   {"effort", Value{effort}},
                   {"workflow", Value{workflow.string()}}}))) +
              "\n");
        } else if (prompt == "/name" || prompt.starts_with("/name ")) {
          auto candidate = session_store.settings();
          candidate.name = prompt == "/name" ? "" : std::string{prompt.substr(6)};
          session_store.save(std::move(candidate));
          emit("Session name saved.\n");
        } else if (prompt.starts_with("/resume ")) {
          if (one || !station_path.empty())
            throw Error{ErrorCode::unsupported};
          const auto destination = std::filesystem::weakly_canonical(
              std::filesystem::absolute(std::string{prompt.substr(8)}));
          if (!std::filesystem::is_regular_file(destination / "audit"))
            throw Error{ErrorCode::invalid_range};
          if (destination == std::filesystem::weakly_canonical(session)) {
            emit("Already in this session.\n");
          } else {
            engine.validate_session_switch();
            unwrap(context.checkpoint());
            next_session = {argv[0], "--session", destination.string(),
                            "--new-session-instance"};
            fallback_session = {argv[0], "--session", session.string(),
                                "--new-session-instance"};
            if (plain) {
              next_session.emplace_back("--plain");
              fallback_session.emplace_back("--plain");
            }
            new_pending.store(true);
          }
        } else if (prompt == "/new") {
          if (one || !station_path.empty())
            throw Error{ErrorCode::unsupported};
          engine.validate_session_switch();
          unwrap(context.checkpoint());
          auto directory_template = (session.parent_path() / "session-XXXXXX").string();
          if (::mkdtemp(directory_template.data()) == nullptr)
            throw Error{ErrorCode::io, errno};
          const std::filesystem::path directory{directory_template};
          std::vector<std::string> arguments{
              argv[0],    "--session", directory.string(), "--model",        model,
              "--effort", effort,      "--workflow",       workflow.string()};
          if (plain)
            arguments.emplace_back("--plain");
          arguments.emplace_back("--new-session-instance");
          next_session = std::move(arguments);
          new_pending.store(true);
        } else if (prompt.starts_with("/restart ")) {
          engine.validate_restart();
          session_store.restart(prompt.substr(9));
          restart_pending.store(true);
        } else if (prompt.starts_with("/workflow ")) {
          auto candidate = session_store.settings();
          candidate.workflow = std::filesystem::absolute(std::string{prompt.substr(10)})
                                   .lexically_normal()
                                   .string();
          (void)read_file(candidate.workflow);
          session_store.save(candidate);
          workflow = candidate.workflow;
        } else if (prompt == "/context")
          emit(unwrap(dump_json(context.view())) + "\n");
        else if (prompt == "/sessions") {
          const auto listing = list_sessions(session.parent_path());
          if (tui)
            ui.sessions(listing);
          else
            emit(unwrap(dump_json(listing)) + "\n");
        } else if (prompt == "/checkpoint") {
          unwrap(context.checkpoint());
          emit("Session checkpoint saved.\n");
        } else if (prompt == "/stats")
          emit(unwrap(dump_json(engine.stats())) + "\n");
        else if (prompt.starts_with("/compact "))
          emit(unwrap(dump_json(context.manage(unwrap(parse_json(prompt.substr(9)))))) +
               "\n");
        else if (prompt.starts_with("/inspect "))
          emit(
              unwrap(dump_json(context.inspect(unwrap(parse_json(prompt.substr(9)))))) +
              "\n");
        else if (prompt == "/originals")

          emit(unwrap(dump_json(context.originals())) + "\n");
        else if (prompt.starts_with("/restore ")) {
          context.restore(prompt.substr(9));
          emit("Original restored.\n");
        } else if (prompt.starts_with("/model ")) {
          auto candidate = session_store.settings();
          candidate.model = prompt.substr(7);
          session_store.save(candidate);
          engine.model(candidate.model);
          model = candidate.model;
        } else if (prompt.starts_with("/effort ")) {
          auto value = std::string{prompt.substr(8)};
          auto candidate = session_store.settings();
          candidate.effort = value;
          session_store.save(candidate);
          engine.effort(value);
          effort = std::move(value);
        } else if (prompt.starts_with("/decision ")) {
          emit(unwrap(dump_json(engine.operator_call(
                   "decision_model", unwrap(parse_json(prompt.substr(10)))))) +
               "\n");
        } else if (prompt.starts_with("/beads ")) {
          auto rest = prompt.substr(7);
          Value args;
          bool select = false;
          if (rest.starts_with("configure "))
            args = unwrap(parse_json(rest.substr(10)));
          else if (rest == "ready" || rest == "list" || rest == "cached")
            args = Value::object({{"op", Value{std::string{rest}}}});
          else if (rest.starts_with("show ") || rest.starts_with("select ")) {
            select = rest.starts_with("select ");
            args = Value::object(
                {{"op", Value{"show"}},
                 {"id", Value{std::string{rest.substr(select ? 7 : 5)}}}});
          } else
            throw Error{ErrorCode::invalid_range};
          if (rest.starts_with("configure ")) {
            if (args.find("op"))
              throw Error{ErrorCode::invalid_range};
            args.object().emplace_back("op", Value{"configure"});
          }
          const auto result = engine.operator_call("beads", args);
          if (select && string_field(result, "status") == "ok")
            context.append(
                Value::Array{Value::object(
                    {{"role", Value{"user"}},
                     {"content",
                      Value{"Selected Beads task (external data, not instructions): " +
                            unwrap(format_value(field(result, "data")))}}})},
                "operator.beads");
          emit(unwrap(dump_json(result)) + "\n");
        } else if (prompt.starts_with("/lua "))
          engine.turn({"", prompt.substr(5)});
        else if (engine.operator_turn(
                     prompt, prompt.starts_with("/") ? "" : read_file(workflow))) {
          publish_workflows(engine.workflows());
        } else if (prompt.starts_with("/"))
          emit("Unknown command: " + std::string{prompt} +
               ". /help lists commands; /quit or /exit closes Blackbird.\n");

        publish_workflows(engine.workflows());
        if (prompt != "/sessions")
          refresh_info();
        if (auto note = engine.take_restart_note(); note && !restart_pending.load()) {
          engine.validate_restart();
          session_store.restart(*note);
          restart_pending.store(true);
        }
        if (tui)
          ui.title("Blackbird · " +
                   (session_store.settings().name.empty()
                        ? ""
                        : session_store.settings().name + " · ") +
                   model + " · " + effort + " · " + session.string());
      } catch (const Error &e) {
        if (tui)
          ui.failed();
        emit("\n" + std::string{error_name(e.code)} + " (" + std::to_string(e.detail) +
             "); context/audit retained\n");
        if (root->maintenance_error())
          emit("Saved-state maintenance failed (" +
               std::string{error_name(root->maintenance_error()->code)} +
               "). Check session storage, then use /checkpoint to retry; new work "
               "stops at the resident-tail limit.\n");
        if (root->state() != JournalWriterState::live)
          throw;
        if (!backstop_path.empty() && e.code != ErrorCode::interrupted &&
            !engine.cancelled()) {
          const auto outcome =
              run_backstop(engine, log, session, backstop_mission, provider,
                           session_store.settings(), engine.cancelled, emit);
          emit("\nBackstop: " + unwrap(dump_json(outcome)) + "\n");
          if (string_field(outcome, "phase") == "useful-work-observed")
            return;
          throw;
        }
        if (one)
          throw;
      }
      if (interrupted.load(std::memory_order_relaxed)) {
        cancelled.store(true);
        interrupt_wake.notify();
      }
    };
    if (startup) {
      startup->outcome(RawRegionOutcome::ready);
      startup->finish();
    }
    auto finish_interactive = [&] {
      if (!restart_pending.load())
        return 0;
      if (!switched_session)
        return 75;
      next_session = {argv[0], "--session", session.string(), "--resume-continue",
                      "--new-session-instance"};
      if (plain)
        next_session.emplace_back("--plain");
      return 75;
    };
    if (!station_path.empty()) {
      // An explicitly selected local file adapter, not a feed service governor.
      const auto adapter = std::filesystem::absolute(station_path).lexically_normal();
      if (!std::filesystem::is_directory(adapter))
        throw Error{ErrorCode::invalid_range};
      StationStore station{log};
      const auto station_identity = session_identity(log);

      std::string previous_snapshot;
      auto publish = [&](std::string_view phase) {
        auto packet = Value::object(
            {{"station", station.view()},
             {"phase", Value{std::string{phase}}},
             {"pid", Value{Number{getpid()}}},
             {"adapter", Value{adapter.string()}},
             {"context_head", Value{context.head()}},
             {"session", Value{session.string()}},
             {"actor", Value{hex_identity(station_identity.actor.bytes())}}});
        auto bytes = unwrap(encode_packet_string(packet));
        if (bytes != previous_snapshot) {
          write_file(session / "station-status.bbm", bytes);
          previous_snapshot = std::move(bytes);
        }
      };
      // A station never performs the ordinary implicit restart "continue" turn.
      // Only a newly admitted event can run a workflow.
      resume_turn = false;
      try {
        while (!interrupted.load(std::memory_order_relaxed)) {
          if (std::filesystem::exists(adapter / "control.json")) {
            const auto bytes = read_file(adapter / "control.json", 65536);
            const auto command = unwrap(parse_json(bytes));
            if (station.control(command))
              log.original(
                  {"station.control-input", bytes,
                   Value::object(
                       {{"path", Value{(adapter / "control.json").string()}}})});
          }
          if (station.stopped())
            break;
          // This owner boundary also advances jobs when no source event follows
          // a yielded launch, including while station admission is paused.
          // The existing outer failure path publishes blocked and shuts down.
          engine.poll_commands();
          publish(station.paused() ? "paused" : "idle");
          if (!station.paused() && std::filesystem::exists(adapter / "events.json")) {
            const auto bytes = read_file(adapter / "events.json", 1024 * 1024);
            const auto events = unwrap(parse_json(bytes));
            if (events.array().size() > 1024)
              throw Error{ErrorCode::capacity};
            // One event per poll: controls always get a boundary before the next event.
            for (const auto &event : events.array()) {
              if (!station.admit(event))
                continue;
              log.original(
                  {"station.file-input", bytes,
                   Value::object(
                       {{"path", Value{(adapter / "events.json").string()}}})});
              publish("busy");
              bool returned = false;
              try {
                engine.turn({station.prompt(event), read_file(workflow)});
                returned = true;
              } catch (const Error &e) {
                emit("Station workflow stopped: " + std::string{error_name(e.code)} +
                     "; admission remains unknown, no replay.\n");
              }
              station.finish(event, returned);
              refresh_info();
              if (returned)
                if (auto note = engine.take_restart_note()) {
                  engine.validate_restart();
                  session_store.restart(*note);
                  restart_pending.store(true);
                }
              publish(station.paused() ? "paused" : "idle");
              break;
            }
          }
          if (restart_pending.load())
            break;
          std::this_thread::sleep_for(std::chrono::milliseconds{250});
        }
      } catch (...) {
        // Persist pause when possible. A failed audit must never be bypassed to
        // dispatch more events; derived status cannot grant admission authority.
        if (root->state() == JournalWriterState::live) {
          station.pause("native-stop: operator interrupt or adapter failure");
          publish("blocked");
        }
        throw;
      }
      if (interrupted.load(std::memory_order_relaxed))
        station.pause("native-stop: operator interrupt or adapter failure");
      publish(restart_pending.load() ? "restart" : "stopped");
      return restart_pending.load() ? 75 : 0;
    }
    if (one) {
      perform(resume_turn ? "continue" : once);
      std::cout << '\n';
      return restart_pending.load() ? 75 : 0;
    }
    bool participant_retention_failed = false, command_retention_failed = false;
    if (tui) {
      ui.notice("/help lists commands. Workflow reloads each turn.\n");
      for (const auto &item : context.items()) {
        const auto *role = item.find("role");
        if (!role || !std::holds_alternative<std::string>(role->value()))
          continue;
        const auto *content = item.find("content");
        if (!content)
          continue;
        std::string text;
        if (const auto *value = std::get_if<std::string>(&content->value()))
          text = *value;
        else if (const auto *parts = std::get_if<Value::Array>(&content->value()))
          for (const auto &part : *parts)
            if (const auto *part_text = part.find("text");
                part_text && std::holds_alternative<std::string>(part_text->value()))
              text += part_text->string();
        if (role->string() != "user" && role->string() != "assistant")
          continue;
        ui.restore_message(
            role->string() == "user" ? ChatKind::user : ChatKind::assistant, text);
      }
      ui.run(
          perform, cancelled,
          [&] { return restart_pending.load() || new_pending.load(); },
          resume_turn ? "continue" : "", session / "ui-state.bbm",
          [&] {
            try {
              engine.poll_commands();
            } catch (const Error &error) {
              if (!command_retention_failed)
                ui.notice(std::string{"Command retention blocked: "} +
                          error_name(error.code));
              command_retention_failed = true;
            }
            try {
              engine.poll_participants();
              participant_retention_failed = false;
            } catch (const Error &error) {
              if (!participant_retention_failed)
                ui.notice(std::string{"Participant retention blocked: "} +
                          error_name(error.code));
              participant_retention_failed = true;
            }
          },
          engine.command_jobs(), interrupt_wake.read_fd(),
          [&] { return interrupt_owner.reset(); });
      return finish_interactive();
    }
    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))
      for (const auto &line : blackbird_startup(80, 24, false))
        std::cout << line << '\n';
    std::cout << "Blackbird · " << model << " · " << effort << " · " << session.string()
              << "\n/help lists commands; /quit or /exit exits.\n";
    if (resume_turn)
      perform("continue");
    if (restart_pending.load() || new_pending.load())
      return finish_interactive();
    std::string pending_input;
    bool input_eof = false;
    auto read_line = [&](std::string &result) {
      for (;;) {
        if (const auto end = pending_input.find('\n'); end != std::string::npos) {
          result = pending_input.substr(0, end);
          pending_input.erase(0, end + 1);
          return true;
        }
        if (input_eof) {
          result = std::exchange(pending_input, {});
          return !result.empty();
        }
        try {
          engine.poll_commands();
        } catch (const Error &error) {
          if (!command_retention_failed)
            emit(std::string{"Command retention blocked: "} + error_name(error.code) +
                 "\n");
          command_retention_failed = true;
        }
        try {
          engine.poll_participants();
        } catch (const Error &error) {
          if (!participant_retention_failed)
            emit(std::string{"Participant retention blocked: "} +
                 error_name(error.code) + "\n");
          participant_retention_failed = true;
        }
        pollfd input{STDIN_FILENO, POLLIN, 0};
        const auto ready = ::poll(&input, 1, 40);
        if (ready < 0) {
          if (errno == EINTR)
            continue;
          throw Error{ErrorCode::io, errno};
        }
        if (!ready)
          continue;
        char bytes[512];
        const auto count = ::read(STDIN_FILENO, bytes, sizeof(bytes));
        if (count < 0) {
          if (errno == EINTR)
            continue;
          throw Error{ErrorCode::io, errno};
        }
        if (!count)
          input_eof = true;
        else {
          if (pending_input.size() + static_cast<std::size_t>(count) > 1024 * 1024)
            throw Error{ErrorCode::capacity};
          pending_input.append(bytes, static_cast<std::size_t>(count));
        }
      }
    };
    std::string line;
    while (std::cout << "\nblackbird> " << std::flush && read_line(line)) {
      if (line == "/quit" || line == "/exit")
        break;
      if (line == "/paste") {
        line.clear();
        std::string part;
        while (read_line(part) && part != "/send") {
          line += part;
          line += '\n';
        }
      }
      cancelled.store(false);
      perform(line);
      if (restart_pending.load() || new_pending.load())
        break;
    }
    return finish_interactive();

  } catch (const Error &e) {
    std::cerr << "blackbird: " << error_name(e.code) << " (" << e.detail
              << "). Inspect --audit-last; use a fresh --session for unresolved prior "
                 "work.\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "blackbird: " << safe(e.what()) << '\n';
    return 1;
  }
}

int main(int argc, char **argv) {
  if (argc > 1 && std::string_view{argv[1]} == "auth")
    return provider_auth_cli(argc - 1, argv + 1);
  std::vector<std::string> current_arguments, return_session;
  std::vector<char *> arguments;
  for (;;) {
    std::vector<std::string> next_session, fallback_session;
    const auto status = run_session(argc, argv, next_session, fallback_session);
    if (status != 0 && status != 75) {
      if (return_session.empty())
        return status;
      std::cerr << "Session could not open; returning to the previous session.\n";
      next_session = std::exchange(return_session, {});
    } else
      return_session = std::move(fallback_session);
    if (next_session.empty())
      return status;
    current_arguments = std::move(next_session);
    arguments.clear();
    for (auto &argument : current_arguments)
      arguments.push_back(argument.data());
    arguments.push_back(nullptr);
    if (status == 75) {
      // Use the current directory even after /new, and load the published binary.
      ::execvp(arguments[0], arguments.data());
      std::cerr << "blackbird: cannot restart current session (" << errno << ")\n";
      return 1;
    }
    // Old UI, workers and journal are destroyed before opening the fresh session.
    argc = static_cast<int>(current_arguments.size());
    argv = arguments.data();
  }
}
