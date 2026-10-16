#include "blackbird/application.hpp"
#include "blackbird/backstop.hpp"
#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/openrouter.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/raw_command.hpp"
#include "blackbird/raw_file.hpp"
#include "blackbird/raw_input.hpp"
#include "blackbird/raw_plain.hpp"
#include "blackbird/raw_restart.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/raw_storage.hpp"
#include "blackbird/sprite.hpp"
#include "blackbird/station.hpp"
#include "blackbird/terminal.hpp"
#include "blackbird/trace_archive.hpp"
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
struct SessionSuccessorOutputs {
  std::vector<std::string> &next;
  std::vector<std::string> &fallback;
};
} // namespace
int run_session(int argc, char **argv, SessionSuccessorOutputs outputs,
                RawSuccessorHandoff *selected_handoff = nullptr,
                ApplicationConnection *application = nullptr) {
  auto &next_session = outputs.next;
  auto &fallback_session = outputs.fallback;
  RawSuccessorHandoff local_handoff;
  auto &handoff = selected_handoff ? *selected_handoff : local_handoff;
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
    std::string provider_name = "openai";
    bool provider_option = false;
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
        std::cout << "blackbird --application (native framed BBM2 interface)\n"
                     "blackbird [--session DIRECTORY] [--provider openai|openrouter] "
                     "[--model NAME] [--workflow FILE] "
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
      } else if (arg == "--provider") {
        provider_name = argv[++i];
        provider_option = true;
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
    std::unique_ptr<TraceArchiveWorker> archive_worker;
    std::unique_ptr<RawRuntime> instrumentation;
    std::optional<RawThreadBinding> instrumentation_binding;
    std::optional<RawRegion> startup;
    RawAdapterContext session_capture;
    auto warning = [&](std::string_view text) {
      write_plain_output(STDERR_FILENO, PlainOutputKind::warning,
                         std::array<std::string_view, 1>{text}, &session_capture);
    };
    RawProductStorageOwner raw_storage_owner;
    std::unique_ptr<NativeJournalDirectory> directory;
    const auto start_instrumentation = [&](EnvironmentId environment) {
      if (!inspect) {
        archive_worker = TraceArchiveWorker::start(session);
        RawRuntimeOptions options;
        options.producer_capacities = {256 * 1024 * 1024, 1024 * 1024, 128 * 1024};
        instrumentation =
            unwrap(RawRuntime::open(session.string(), environment, options));
        instrumentation_binding.emplace(unwrap(instrumentation->prepare_thread()));
        startup.emplace(raw_regions::startup_initialize);
        session_capture.parent =
            RawEventRef{startup->lane_epoch(), startup->begin_ordinal()};
        raw_storage_owner = unwrap(RawProductStorageOwner::prepare(
            RawEventRef{startup->lane_epoch(), startup->begin_ordinal()}));
      }
      directory = std::make_unique<NativeJournalDirectory>(
          unwrap(NativeJournalDirectory::open(session.string(), raw_storage_owner)));
    };
    std::unique_ptr<RetainedState> root;
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
        warning("Linked audit segments require an explicit migration; this launcher "
                "cannot reopen them. Session files left intact.\n");
        return 1;
      }
      start_instrumentation(header.environment);
      // Small sessions can rebuild automatically. Large histories require an
      // explicit slow path if their derived state is missing or unsupported.
      constexpr std::uint64_t automatic_replay_bytes = 64ULL * 1024 * 1024;
      const auto replay_limit = rebuild_session ? UINT64_MAX : automatic_replay_bytes;
      if (rebuild_session)
        warning("Session recovery: full replay allowed (up to 512 MiB / 200000 "
                "records); uncertain operations will not be retried.\n");
      RawRegion restoration{raw_regions::startup_restore};
      auto opened = RetainedState::open(std::move(directory), "audit", header, capacity,
                                        false, true, replay_limit, raw_storage_owner);
      if (!opened.has_value() && opened.error().code == ErrorCode::unsupported &&
          !rebuild_session) {
        warning("Session needs full recovery: saved state is missing, invalid, or "
                "does not cover this session. Automatic replay stops at 64 MiB. "
                "Reopen with --rebuild-session for the bounded slow path, or "
                "select a new --session directory. Session files left intact.\n");
        return 1;
      }
      root = unwrap(std::move(opened));
      if (!root->compact_recovery())
        warning("Session recovery: rebuilding from audit; uncertain operations "
                "will not be retried.\n");
      unwrap(root->confirm_recovery());
      if (!inspect) {
        RawCaptureFailureScope recovery_capture{session_capture.parent.has_value()};
        try {
          recover_coding_session(*root,
                                 recover_unknown_effects
                                     ? RecoveryMode::acknowledge_local_unknowns
                                     : RecoveryMode::provider_only,
                                 &session_capture);
          recovery_capture.require();
          if (recover_unknown_effects)
            warning("Explicit recovery: abandoned local effects stay unknown. "
                    "No command replay or old PID adoption; prior process "
                    "containment is not established.\n");
        } catch (const Error &e) {
          recovery_capture.require();
          if (e.code != ErrorCode::external_unknown)
            throw;
          warning("Session has an operation with uncertain custody; reopen cannot "
                  "grant permission to retry it. Inspect with --audit-last "
                  "(--rebuild-session for a large audit), resolve the effect "
                  "outside this session, explicitly acknowledge structurally "
                  "linked local unknowns with --recover-unknown-effects, or "
                  "select a new --session directory.\n");
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
      root = unwrap(RetainedState::create(std::move(directory), "audit", header,
                                          capacity, raw_storage_owner));
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
      context.seed_successor(
          seed, RawContextOrigin{session_capture.parent.value_or(RawEventRef{})});
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
    SessionStore session_store{log, &session_capture};

    auto settings = session_store.settings();
    if (provider_option) {
      if (provider_name != settings.provider && !model_option)
        throw Error{ErrorCode::invalid_range};
      settings.provider = provider_name;
    }
    if (model_option)
      settings.model = model;
    if (effort_option)
      settings.effort = effort;
    if (workflow_option || settings.workflow.empty())
      settings.workflow =
          std::filesystem::absolute(workflow).lexically_normal().string();
    (void)read_file(settings.workflow, 16 * 1024 * 1024, &session_capture);
    session_store.save(settings, &session_capture);
    model = settings.model;
    provider_name = settings.provider;
    effort = settings.effort;
    workflow = settings.workflow;
    bool resume_turn =
        resume_requested && session_store.resume(context, &session_capture);
    if (resume_requested && !resume_turn && one)
      return 0;
    std::atomic_bool restart_pending{false};
    std::atomic_bool new_pending{false};
    RawAdapterContext restart_capture;
    std::unique_ptr<CodingProvider> provider;
    if (provider_name == "openrouter") {
      OpenRouterConfig config;
      if (const auto *curl = std::getenv("BLACKBIRD_OPENROUTER_CURL"); curl && *curl)
        config.auth.curl_executable = curl;
      provider = std::make_unique<OpenRouterCodingProvider>(std::move(config));
    } else
      provider = std::make_unique<OpenAiCodingProvider>();
    CodingEngine engine{log, context, *provider, model};
    if (engine.uncontained_command_custody())
      warning("Prior command custody remains unknown; restart and automatic "
              "backstop stay fenced. No old PID adopted or effect replayed.\n");
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
    if (application) {
      save_session_info(session, settings, session_identity(log, &session_capture),
                        &session_capture);
      if (startup) {
        startup->outcome(RawRegionOutcome::ready);
        startup->finish();
      }
      return application->run(engine, log, settings, session);
    }
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
    ui.raw_capture(tui);
    const RawAdapterContext *output_context = &session_capture;
    auto emit = [&](std::string_view text) {
      if (tui)
        ui.text(text);
      else
        write_plain_formatted(STDOUT_FILENO, PlainOutputKind::emit, text, safe,
                              output_context);
    };
    auto refresh_info = [&](const RawAdapterContext &capture) {
      RawCaptureFailureScope failure{capture.parent && raw_enabled()};
      try {
        save_session_info(session, session_store.settings(),
                          session_identity(log, &capture), &capture);
      } catch (...) {
        failure.require();
        emit("Session discovery snapshot save failed; audit settings remain "
             "authoritative.\n");
      }
    };
    refresh_info(session_capture);
    engine.display = emit;
    if (tui) {
      RawEventRef source;
      const auto snapshot = engine.tasks().snapshot(&source);
      ui.tasks(snapshot, {2, source, {}, engine.tasks().state().revision()});
    }
    engine.tasks().changed = [&](const Value &packet) {
      if (tui) {
        const auto origin = engine.tasks().presentation_origin();
        ui.tasks(packet, {3, origin.source, origin.fact, origin.revision});
      }
    };
    engine.task_activity_native = [&, tui](const Value &event,
                                           RawTaskPresentation origin) {
      if (tui)
        ui.task_activity(event, {4, origin.source, origin.fact, origin.revision});
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
        write_plain_formatted(STDERR_FILENO, PlainOutputKind::status, text, safe,
                              output_context, "[", "]\n");
    };
    engine.operation_outcome = [&](std::string_view text, AttemptDisposition outcome) {
      if (tui)
        ui.operation_completed(text,
                               outcome == AttemptDisposition::success   ? Ink::success
                               : outcome == AttemptDisposition::unknown ? Ink::muted
                                                                        : Ink::failure);
      else
        write_plain_formatted(STDERR_FILENO, PlainOutputKind::outcome, text, safe,
                              output_context, "[", "]\n");
    };
    publish_workflows(engine.workflows());
    auto perform = [&](std::string_view prompt) {
      // Plain dispatch is synchronous: the previous foreground owner has
      // returned. Rearm its signal wake before this input's first admission.
      if (!tui)
        unwrap(interrupt_owner.reset());
      const auto kind = resume_turn && prompt == "continue" ? RawMainInputKind::resume
                        : one                               ? RawMainInputKind::once
                        : tui                               ? RawMainInputKind::tui
                                                            : RawMainInputKind::plain;
      const auto length = prompt.size();
      RawSessionCall input{
          4200, 4201, &session_capture,
          std::array{detail::raw_native_field(kind), detail::raw_native_field(length)}};
      input.run([&] {
        auto original = input.bytes_credit(length);
        (void)input.bytes(4202, prompt, original);
        const auto input_context = input.context();
        RawSessionCall dispatch{4203, 4204, &input_context};
        const auto command_context = dispatch.context();
        struct OutputSelection {
          const RawAdapterContext *&selected;
          const RawAdapterContext *previous;
          ~OutputSelection() { selected = previous; }
        } output_selection{output_context, output_context};
        output_context = &command_context;
        try {
          dispatch.run([&] {
            if (prompt == "/help") {
              raw_main_branch(dispatch, 1);
              emit(terminal_help());
            } else if (prompt.starts_with("/exec ") ||
                       prompt.starts_with("/process ")) {
              raw_main_branch(dispatch, 2);
              const bool execute = prompt.starts_with("/exec ");
              emit(unwrap(dump_json(engine.operator_call(
                       execute ? "exec" : "process",
                       unwrap(parse_json(prompt.substr(execute ? 6 : 9)))))) +
                   "\n");
            } else if (prompt.starts_with("/fg ")) {
              raw_main_branch(dispatch, 3);
              emit(unwrap(dump_json(engine.operator_call(
                       "process",
                       Value::object(
                           {{"op", Value{"foreground"}},
                            {"job_id", Value{std::string{prompt.substr(4)}}}})))) +
                   "\n");
            } else if (prompt == "/jobs") {
              raw_main_branch(dispatch, 4);
              emit(unwrap(dump_json(engine.command_jobs()->read(Value::object({})))) +
                   "\n");
            } else if (prompt == "/bg" || prompt.starts_with("/bg ")) {
              raw_main_branch(dispatch, 5);
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
              raw_main_branch(dispatch, 6);
              resume_turn = false;
              engine.turn(
                  {"", read_file(workflow, 16 * 1024 * 1024, &command_context)});
            } else if (prompt == "/tasks" || prompt.starts_with("/tasks ")) {
              raw_main_branch(dispatch, 7);
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
                       string_field(row, "id") + " [" + string_field(row, "status") +
                       "] " + string_field(row, "title") + "\n");
                if (const auto *removed = result.find("removed"))
                  for (const auto &id : removed->array())
                    emit("Archived " + id.string() + "\n");
                if (rows.empty())
                  emit(items ? "No tasks.\n" : "Task list updated.\n");
              }
            } else if (prompt == "/runs" || prompt.starts_with("/runs ")) {
              raw_main_branch(dispatch, 8);
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
              raw_main_branch(dispatch, 9);
              const auto name = prompt.starts_with("/run ")    ? "participant_start"
                                : prompt.starts_with("/send ") ? "participant_send"
                                                               : "participant_join";
              const auto offset = prompt.starts_with("/run ") ? 5U : 6U;
              emit(unwrap(dump_json(engine.operator_call(
                       name, unwrap(parse_json(prompt.substr(offset)))))) +
                   "\n");
            } else if (prompt.starts_with("/await ")) {
              raw_main_branch(dispatch, 10);
              emit(unwrap(dump_json(engine.operator_call(
                       "participant_await", unwrap(parse_json(prompt.substr(7)))))) +
                   "\n");
            } else if (prompt.starts_with("/stop ")) {
              raw_main_branch(dispatch, 11);
              emit(unwrap(dump_json(engine.operator_call(
                       "participant_cancel",
                       Value::object(
                           {{"run_id", Value{std::string{prompt.substr(6)}}}})))) +
                   "\n");
            } else if (prompt == "/auth" || prompt == "/auth status") {
              raw_main_branch(dispatch, 12);
              emit(safe(unwrap(dump_json(ProviderAuth{}.status()))) + "\n");
            } else if (prompt == "/auth providers") {
              raw_main_branch(dispatch, 13);
              emit(safe(unwrap(dump_json(ProviderAuth{}.catalog()))) + "\n");
            } else if (prompt.starts_with("/auth ")) {
              raw_main_branch(dispatch, 14);
              emit("Use ./scripts/blackbird auth outside chat for "
                   "sign-in/key/use/logout. "
                   "Secret entry is excluded from session history.\n");
            } else if (prompt == "/variables" || prompt.starts_with("/variables ") ||
                       prompt == "/correlate" || prompt.starts_with("/correlate ")) {
              raw_main_branch(dispatch, 15);
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
              raw_main_branch(dispatch, 16);
              emit("/git-observe {\"path\":\"/absolute/repository\",\"history\":8}\n");
            } else if (prompt.starts_with("/git-observe ")) {
              raw_main_branch(dispatch, 17);
              emit(safe(unwrap(dump_json(engine.operator_call(
                       "git_observe", unwrap(parse_json(prompt.substr(13))))))) +
                   "\n");
            } else if (prompt == "/trace" || prompt.starts_with("/trace ")) {
              raw_main_branch(dispatch, 18);
              const auto query = prompt == "/trace"
                                     ? Value::object({})
                                     : unwrap(parse_json(prompt.substr(7)));
              const auto result = engine.operator_call(
                  "trajectory_read", Value::object({{"query", query}}));
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
                  emit(field(row, "record").number().text() + "  " + text("event") +
                       "  " + safe(detail) + "  " + text("outcome") + "  " +
                       safe(attempt) + "\n");
                }
                emit("next=" + field(result, "next").number().text() +
                     " end=" + field(result, "end").number().text() +
                     "; pin end for paging; originals via audit_inspect\n");
              }
            } else if (prompt == "/colleagues") {
              raw_main_branch(dispatch, 19);
              emit(unwrap(dump_json(
                       engine.operator_call("colleague_catalog", Value::object({})))) +
                   "\n");
            } else if (prompt.starts_with("/colleague ")) {
              raw_main_branch(dispatch, 20);
              emit(unwrap(dump_json(engine.operator_call(
                       "colleague", unwrap(parse_json(prompt.substr(11)))))) +
                   "\n");
            } else if (prompt == "/session") {
              raw_main_branch(dispatch, 21);
              const auto identity = session_identity(log, &command_context);
              emit(unwrap(dump_json(Value::object(
                       {{"name", Value{session_store.settings().name}},
                        {"actor", Value{hex_identity(identity.actor.bytes())}},
                        {"directory", Value{session.string()}},
                        {"conversation",
                         Value{hex_identity(identity.conversation.bytes())}},
                        {"workflow_id", Value{hex_identity(identity.workflow.bytes())}},
                        {"model", Value{model}},
                        {"provider", Value{provider_name}},
                        {"effort", Value{effort}},
                        {"workflow", Value{workflow.string()}}}))) +
                   "\n");
            } else if (prompt == "/name" || prompt.starts_with("/name ")) {
              raw_main_branch(dispatch, 22);
              auto candidate = session_store.settings();
              candidate.name = prompt == "/name" ? "" : std::string{prompt.substr(6)};
              session_store.save(candidate, &command_context);
              emit("Session name saved.\n");
            } else if (prompt.starts_with("/resume ")) {
              raw_main_branch(dispatch, 23);
              if (one || !station_path.empty())
                throw Error{ErrorCode::unsupported};
              const std::uint8_t method = 1;
              RawSessionCall selection{4460, 4461, &command_context,
                                       std::array{detail::raw_native_field(method)}};
              selection.run([&] {
                (void)raw_restart_source(selection, 6, session.native());
                const auto source = prompt.substr(8);
                (void)raw_restart_source(selection, 6, source);
                const auto parent = selection.context();
                std::error_code error;
                const auto absolute =
                    raw_file_absolute(std::filesystem::path{source}, error, &parent);
                if (error)
                  throw std::filesystem::filesystem_error("absolute", source, error);
                const auto destination = raw_restart_canonical(selection, absolute);
                auto operand = selection.source_credit(
                    raw_session_bound(destination.native().size(), 6));
                const auto audit_path = destination / "audit";
                (void)raw_restart_source(selection, 6, audit_path.native(), operand);
                if (!raw_restart_regular(selection, audit_path))
                  throw Error{ErrorCode::invalid_range};
                if (destination == raw_restart_canonical(selection, session)) {
                  emit("Already in this session.\n");
                } else {
                  engine.validate_session_switch(&parent);
                  unwrap(context.checkpoint());
                  handoff.select(selection);
                  const auto destination_text =
                      raw_restart_path(selection, destination);
                  const auto previous_text = raw_restart_path(selection, session);
                  std::vector<std::string> selected, fallback;
                  auto add = [&](std::vector<std::string> &arguments,
                                 std::string_view value) {
                    arguments.emplace_back(
                        raw_restart_argument(selection, arguments.size(), value));
                  };
                  for (const auto value :
                       {std::string_view{argv[0]}, std::string_view{"--session"},
                        std::string_view{destination_text},
                        std::string_view{"--new-session-instance"}})
                    add(selected, value);
                  for (const auto value :
                       {std::string_view{argv[0]}, std::string_view{"--session"},
                        std::string_view{previous_text},
                        std::string_view{"--new-session-instance"}})
                    add(fallback, value);
                  if (plain) {
                    add(selected, "--plain");
                    add(fallback, "--plain");
                  }
                  auto mutation = selection.control(50);
                  next_session = std::move(selected);
                  fallback_session = std::move(fallback);
                  new_pending.store(true);
                  const auto count = next_session.size();
                  const bool exec = false;
                  (void)selection.event(4467,
                                        std::array{detail::raw_native_field(count),
                                                   detail::raw_native_field(exec),
                                                   detail::raw_native_field(plain)},
                                        mutation);
                }
              });
            } else if (prompt == "/new") {
              raw_main_branch(dispatch, 24);
              if (one || !station_path.empty())
                throw Error{ErrorCode::unsupported};
              const std::uint8_t method = 2;
              RawSessionCall selection{4460, 4461, &command_context,
                                       std::array{detail::raw_native_field(method)}};
              selection.run([&] {
                (void)raw_restart_source(selection, 6, session.native());
                const auto parent = selection.context();
                engine.validate_session_switch(&parent);
                unwrap(context.checkpoint());
                const auto parent_path = session.parent_path();
                auto template_credit = selection.source_credit(
                    raw_session_bound(parent_path.native().size(), 16));
                auto directory_template = (parent_path / "session-XXXXXX").string();
                (void)raw_restart_source(selection, 6, directory_template,
                                         template_credit);
                raw_restart_directory(selection, directory_template);
                const std::filesystem::path directory{directory_template};
                handoff.select(selection);
                const auto directory_text = raw_restart_path(selection, directory);
                const auto workflow_text = raw_restart_path(selection, workflow);
                std::vector<std::string> arguments;
                for (const auto value :
                     {std::string_view{argv[0]}, std::string_view{"--session"},
                      std::string_view{directory_text}, std::string_view{"--model"},
                      std::string_view{model}, std::string_view{"--provider"},
                      std::string_view{provider_name}, std::string_view{"--effort"},
                      std::string_view{effort}, std::string_view{"--workflow"},
                      std::string_view{workflow_text}})
                  arguments.emplace_back(
                      raw_restart_argument(selection, arguments.size(), value));
                if (plain)
                  arguments.emplace_back(
                      raw_restart_argument(selection, arguments.size(), "--plain"));
                arguments.emplace_back(raw_restart_argument(selection, arguments.size(),
                                                            "--new-session-instance"));
                auto mutation = selection.control(50);
                next_session = std::move(arguments);
                new_pending.store(true);
                const auto count = next_session.size();
                const bool exec = false;
                (void)selection.event(4467,
                                      std::array{detail::raw_native_field(count),
                                                 detail::raw_native_field(exec),
                                                 detail::raw_native_field(plain)},
                                      mutation);
              });
            } else if (prompt.starts_with("/restart ")) {
              raw_main_branch(dispatch, 25);
              engine.validate_restart(&command_context);
              session_store.restart(prompt.substr(9), &command_context);
              restart_capture = command_context;
              restart_pending.store(true);
            } else if (prompt.starts_with("/workflow ")) {
              raw_main_branch(dispatch, 26);
              auto candidate = session_store.settings();
              candidate.workflow =
                  std::filesystem::absolute(std::string{prompt.substr(10)})
                      .lexically_normal()
                      .string();
              (void)read_file(candidate.workflow, 16 * 1024 * 1024, &command_context);
              session_store.save(candidate, &command_context);
              workflow = candidate.workflow;
            } else if (prompt == "/context") {
              raw_main_branch(dispatch, 27);
              emit(unwrap(dump_json(context.view())) + "\n");
            } else if (prompt == "/sessions") {
              raw_main_branch(dispatch, 28);
              RawEventRef listing_source;
              const auto listing = list_sessions(session.parent_path(),
                                                 &command_context, &listing_source);
              if (tui)
                ui.sessions(listing, {1, listing_source, {}, {}});
              else
                emit(unwrap(dump_json(listing)) + "\n");
            } else if (prompt == "/checkpoint") {
              raw_main_branch(dispatch, 29);
              unwrap(context.checkpoint());
              emit("Session checkpoint saved.\n");
            } else if (prompt == "/stats") {
              raw_main_branch(dispatch, 30);
              emit(unwrap(dump_json(engine.stats())) + "\n");
            } else if (prompt.starts_with("/compact ")) {
              raw_main_branch(dispatch, 31);
              emit(unwrap(dump_json(
                       context.manage(unwrap(parse_json(prompt.substr(9)))))) +
                   "\n");
            } else if (prompt.starts_with("/inspect ")) {
              raw_main_branch(dispatch, 32);
              emit(unwrap(dump_json(
                       context.inspect(unwrap(parse_json(prompt.substr(9)))))) +
                   "\n");
            } else if (prompt == "/originals") {
              raw_main_branch(dispatch, 33);

              emit(unwrap(dump_json(context.originals())) + "\n");
            } else if (prompt.starts_with("/restore ")) {
              raw_main_branch(dispatch, 34);
              context.restore(prompt.substr(9));
              emit("Original restored.\n");
            } else if (prompt.starts_with("/model ")) {
              raw_main_branch(dispatch, 35);
              auto candidate = session_store.settings();
              candidate.model = prompt.substr(7);
              session_store.save(candidate, &command_context);
              engine.model(candidate.model);
              model = candidate.model;
            } else if (prompt.starts_with("/effort ")) {
              raw_main_branch(dispatch, 36);
              auto value = std::string{prompt.substr(8)};
              auto candidate = session_store.settings();
              candidate.effort = value;
              session_store.save(candidate, &command_context);
              engine.effort(value);
              effort = std::move(value);
            } else if (prompt.starts_with("/decision ")) {
              raw_main_branch(dispatch, 37);
              emit(unwrap(dump_json(engine.operator_call(
                       "decision_model", unwrap(parse_json(prompt.substr(10)))))) +
                   "\n");
            } else if (prompt.starts_with("/beads ")) {
              raw_main_branch(dispatch, 38);
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
                          Value{"Selected Beads task (external data, not "
                                "instructions): " +
                                unwrap(format_value(field(result, "data")))}}})},
                    "operator.beads");
              emit(unwrap(dump_json(result)) + "\n");
            } else if (prompt.starts_with("/lua ")) {
              raw_main_branch(dispatch, 39);
              engine.turn({"", prompt.substr(5)});
            } else {
              raw_main_branch(dispatch, 40);
              if (engine.operator_turn(
                      prompt,
                      prompt.starts_with("/")
                          ? ""
                          : read_file(workflow, 16 * 1024 * 1024, &command_context))) {
                publish_workflows(engine.workflows());
              } else if (prompt.starts_with("/"))
                emit("Unknown command: " + std::string{prompt} +
                     ". /help lists commands; /quit or /exit closes Blackbird.\n");
            }

            publish_workflows(engine.workflows());
            if (prompt != "/sessions")
              refresh_info(command_context);
            if (auto note = engine.take_restart_note(&command_context);
                note && !restart_pending.load()) {
              engine.validate_restart(&command_context);
              session_store.restart(*note, &command_context);
              restart_capture = command_context;
              restart_pending.store(true);
            }
            if (tui)
              ui.title("Blackbird · " +
                       (session_store.settings().name.empty()
                            ? ""
                            : session_store.settings().name + " · ") +
                       model + " · " + effort + " · " + session.string());
          });
        } catch (const Error &e) {
          dispatch.require_capture();
          input.require_capture();
          if (tui)
            ui.failed();
          emit("\n" + std::string{error_name(e.code)} + " (" +
               std::to_string(e.detail) + "); context/audit retained\n");
          if (root->maintenance_error())
            emit("Saved-state maintenance failed (" +
                 std::string{error_name(root->maintenance_error()->code)} +
                 "). Check session storage, then use /checkpoint to retry; new work "
                 "stops at the resident-tail limit.\n");
          if (root->state() != JournalWriterState::live)
            throw;
          if (!backstop_path.empty() && e.code != ErrorCode::interrupted &&
              !engine.cancelled()) {
            const auto outcome = run_backstop(engine, log, session, backstop_mission,
                                              *provider, session_store.settings(),
                                              engine.cancelled, emit, &command_context);
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
      });
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
      const std::uint8_t method = 3;
      RawSessionCall selection{4460, 4461, &restart_capture,
                               std::array{detail::raw_native_field(method)}};
      selection.run([&] {
        handoff.select(selection);
        const auto path = raw_restart_path(selection, session);
        std::vector<std::string> arguments;
        for (const auto value :
             {std::string_view{argv[0]}, std::string_view{"--session"},
              std::string_view{path}, std::string_view{"--resume-continue"},
              std::string_view{"--new-session-instance"}})
          arguments.emplace_back(
              raw_restart_argument(selection, arguments.size(), value));
        if (plain)
          arguments.emplace_back(
              raw_restart_argument(selection, arguments.size(), "--plain"));
        auto mutation = selection.control(50);
        next_session = std::move(arguments);
        const auto count = next_session.size();
        const bool exec = true;
        (void)selection.event(4467,
                              std::array{detail::raw_native_field(count),
                                         detail::raw_native_field(exec),
                                         detail::raw_native_field(plain)},
                              mutation);
      });
      return 75;
    };
    if (!station_path.empty()) {
      // Explicit local file adapter; the existing synchronous owner/cadence stays.
      RawSessionCall station_call{4620, 4621, &session_capture};
      return station_call.run([&] {
        const auto station_context = station_call.context();
        station_call.stage(1);
        std::error_code absolute_error;
        const auto absolute =
            raw_file_absolute(station_path, absolute_error, &station_context);
        if (absolute_error)
          throw std::filesystem::filesystem_error("absolute", station_path,
                                                  absolute_error);
        auto adapter_credit =
            station_call.bytes_credit(raw_session_bound(absolute.native().size(), 1));
        const auto adapter = absolute.lexically_normal();
        (void)station_call.bytes(4622, adapter.native(), adapter_credit);
        auto filesystem = [&](const std::filesystem::path &path, bool directory) {
          const std::uint8_t operation = directory ? 1 : 2;
          RawSessionCall probe{4630, 4639, &station_context,
                               std::array{detail::raw_native_field(operation)}};
          return probe.run([&] {
            (void)probe.source(2, path.native(), 4640);
            auto actual_credit = probe.control(
                45 + (directory ? sizeof(std::filesystem::file_type) : 0));
            std::error_code error;
            bool actual;
            std::filesystem::file_type type{};
            if (directory) {
              const auto status = std::filesystem::status(path, error);
              type = status.type();
              actual = std::filesystem::is_directory(status);
            } else
              actual = std::filesystem::exists(path, error);
            const int code = error.value();
            if (directory)
              (void)probe.event(4631,
                                std::array{detail::raw_native_field(actual),
                                           detail::raw_native_field(code),
                                           detail::raw_native_field(type)},
                                actual_credit);
            else
              (void)probe.event(4631,
                                std::array{detail::raw_native_field(actual),
                                           detail::raw_native_field(code)},
                                actual_credit);
            actual_credit.reset();
            if (error && (!directory || type != std::filesystem::file_type::not_found))
              throw std::filesystem::filesystem_error(directory ? "status" : "exists",
                                                      path, error);
            return actual;
          });
        };
        if (!filesystem(adapter, true))
          throw Error{ErrorCode::invalid_range};
        station_call.stage(2);
        auto identity_credit = station_call.control(56);
        const auto station_identity = session_identity(log, &station_context);
        (void)station_call.event(4644,
                                 std::array{ByteView{station_identity.actor.bytes()}},
                                 identity_credit);
        identity_credit.reset();
        station_call.stage(3);
        StationStore station{log, &station_context};
        auto native_path = [&](const std::filesystem::path &base,
                               std::string_view operand, std::uint8_t role) {
          auto credit = station_call.source_credit(raw_session_bound(
              raw_session_bound(base.native().size(), operand.size()), 1));
          auto path = base / operand;
          (void)station_call.source(role, path.native(), credit, 4640);
          return path;
        };
        auto path_text = [](RawSessionCall &call, const std::filesystem::path &path,
                            std::uint8_t role) {
          auto credit = call.source_credit(path.native().size());
          auto actual = path.string();
          (void)call.source(role, actual, credit, 4640);
          return actual;
        };
        std::string previous_snapshot;
        auto publish = [&](std::string_view phase) {
          RawSessionCall publication{4632, 4633, &station_context};
          publication.run([&] {
            const auto owner = publication.context();
            auto packet_credit = publication.bytes_credit(ValueLimits{}.bytes);
            (void)publication.source(1, phase, 4640);
            auto view_credit = publication.control(64);
            RawEventRef view_owner{};
            auto view = station.view(&owner, &view_owner);
            (void)publication.event(
                4645,
                std::array{ByteView{view_owner.lane},
                           detail::raw_native_field(view_owner.ordinal)},
                view_credit);
            view_credit.reset();
            auto pid_credit = publication.control(81 + sizeof(pid_t));
            const std::uint8_t getpid_operation = 1;
            const auto pid_request = publication.event(
                4642, std::array{detail::raw_native_field(getpid_operation)},
                pid_credit);
            const auto pid = getpid();
            (void)raw_session_record(
                4643, pid_request, std::array{detail::raw_native_field(pid)},
                RawPartition::control, pid_credit ? &*pid_credit : nullptr);
            pid_credit.reset();
            auto adapter_text = path_text(publication, adapter, 2);
            const auto &head = context.head();
            (void)publication.source(4, head, 4640);
            auto session_text = path_text(publication, session, 3);
            auto actor_credit = publication.source_credit(32);
            auto actor = hex_identity(station_identity.actor.bytes());
            (void)publication.source(5, actor, actor_credit, 4640);
            auto packet = Value::object({{"station", std::move(view)},
                                         {"phase", Value{std::string{phase}}},
                                         {"pid", Value{Number{pid}}},
                                         {"adapter", Value{std::move(adapter_text)}},
                                         {"context_head", Value{head}},
                                         {"session", Value{std::move(session_text)}},
                                         {"actor", Value{std::move(actor)}}});
            auto bytes = unwrap(encode_packet_string(packet));
            const auto original = publication.bytes(4634, bytes, packet_credit);
            auto compare = publication.control(41);
            const bool changed = bytes != previous_snapshot;
            (void)publication.event(4635, std::array{detail::raw_native_field(changed)},
                                    compare);
            compare.reset();
            if (changed) {
              auto completion = publication.control(41);
              const auto destination = native_path(session, "station-status.bbm", 8);
              const RawFileInput input{original, bytes.size()};
              write_file(destination, bytes, &owner, &input);
              const bool returned = true;
              (void)publication.event(
                  4636, std::array{detail::raw_native_field(returned)}, completion);
              completion.reset();
              previous_snapshot = std::move(bytes);
            }
          });
        };
        auto parse = [&](std::string_view bytes, RawEventRef file_owner,
                         std::uint8_t kind) {
          const auto size = bytes.size();
          RawSessionCall parser{4624, 4626, &station_context,
                                std::array{ByteView{file_owner.lane},
                                           detail::raw_native_field(file_owner.ordinal),
                                           detail::raw_native_field(kind),
                                           detail::raw_native_field(size)}};
          const auto parser_context = parser.context();
          auto actual = parser.run([&] {
            auto result_credit = parser.control(57);
            auto value = unwrap(parse_json(bytes));
            const auto variant = value.value().index();
            const auto *array = std::get_if<Value::Array>(&value.value());
            const bool present = array != nullptr;
            if (array)
              (void)parser.event(4625,
                                 std::array{detail::raw_native_field(variant),
                                            detail::raw_native_field(present),
                                            detail::raw_native_field(array->size())},
                                 result_credit);
            else
              (void)parser.event(4625,
                                 std::array{detail::raw_native_field(variant),
                                            detail::raw_native_field(present)},
                                 result_credit);
            result_credit.reset();
            return value;
          });
          return std::make_pair(std::move(actual), parser_context);
        };
        // A station never performs the ordinary implicit restart continue turn.
        resume_turn = false;
        try {
          while (!interrupted.load(std::memory_order_relaxed)) {
            station_call.stage(4);
            const auto control_path = native_path(adapter, "control.json", 6);
            if (filesystem(control_path, false)) {
              RawEventRef file_owner{};
              const auto bytes =
                  read_file(control_path, 65536, &station_context, &file_owner);
              const auto command = parse(bytes, file_owner, 1);
              station_call.stage(5);
              if (station.control(command.first, &command.second))
                log.original(
                    {"station.control-input", bytes,
                     Value::object(
                         {{"path", Value{path_text(station_call, control_path, 6)}}})});
            }
            if (station.stopped())
              break;
            station_call.stage(6);
            engine.poll_commands();
            station_call.stage(7);
            publish(station.paused() ? "paused" : "idle");
            if (!station.paused()) {
              station_call.stage(8);
              const auto events_path = native_path(adapter, "events.json", 7);
              if (filesystem(events_path, false)) {
                RawEventRef file_owner{};
                const auto bytes =
                    read_file(events_path, 1024 * 1024, &station_context, &file_owner);
                const auto parsed = parse(bytes, file_owner, 2);
                const auto &events = parsed.first;
                if (events.array().size() > 1024)
                  throw Error{ErrorCode::capacity};
                for (const auto &event : events.array()) {
                  station_call.stage(9);
                  RawEventRef admission{};
                  if (!station.admit(event, &parsed.second, &admission))
                    continue;
                  log.original(
                      {"station.file-input", bytes,
                       Value::object({{"path", Value{path_text(station_call,
                                                               events_path, 7)}}})});
                  station_call.stage(10);
                  publish("busy");
                  bool returned = false;
                  try {
                    station_call.stage(11);
                    RawEventRef prompt_source{};
                    const auto prompt =
                        station.prompt(event, &station_context, &prompt_source);
                    station_call.stage(12);
                    RawEventRef workflow_source{};
                    const auto program = read_file(workflow, 16 * 1024 * 1024,
                                                   &station_context, &workflow_source);
                    station_call.stage(13);
                    RawSessionCall invoke{
                        4637, 4638, &station_context,
                        std::array{ByteView{admission.lane},
                                   detail::raw_native_field(admission.ordinal),
                                   ByteView{prompt_source.lane},
                                   detail::raw_native_field(prompt_source.ordinal),
                                   ByteView{workflow_source.lane},
                                   detail::raw_native_field(workflow_source.ordinal)}};
                    invoke.run([&] { engine.turn({prompt, program}); });
                    returned = true;
                  } catch (const Error &e) {
                    station_call.require_capture();
                    emit(
                        "Station workflow stopped: " + std::string{error_name(e.code)} +
                        "; admission remains unknown, no replay.\n");
                  }
                  station_call.stage(14);
                  station.finish(event, returned, &station_context);
                  station_call.stage(15);
                  refresh_info(session_capture);
                  if (returned)
                    if (auto note = engine.take_restart_note(&station_context)) {
                      station_call.stage(16);
                      engine.validate_restart(&station_context);
                      session_store.restart(*note, &station_context);
                      restart_pending.store(true);
                    }
                  station_call.stage(17);
                  publish(station.paused() ? "paused" : "idle");
                  break;
                }
              }
            }
            if (restart_pending.load())
              break;
            std::this_thread::sleep_for(std::chrono::milliseconds{250});
          }
        } catch (...) {
          station_call.require_capture();
          if (root->state() == JournalWriterState::live) {
            station.pause("native-stop: operator interrupt or adapter failure",
                          &station_context);
            publish("blocked");
          }
          throw;
        }
        if (interrupted.load(std::memory_order_relaxed))
          station.pause("native-stop: operator interrupt or adapter failure",
                        &station_context);
        station_call.stage(18);
        publish(restart_pending.load() ? "restart" : "stopped");
        return restart_pending.load() ? 75 : 0;
      });
    }
    if (one) {
      perform(resume_turn ? "continue" : once);
      write_plain_output(STDOUT_FILENO, PlainOutputKind::newline,
                         std::array<std::string_view, 1>{"\n"}, &session_capture);
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
      const std::string_view initial = resume_turn ? "continue" : "";
      const auto initial_kind =
          resume_turn ? RawMainInputKind::resume : RawMainInputKind::tui;
      const auto initial_length = initial.size();
      RawSessionCall initial_input{
          4200, 4201, &session_capture,
          std::array{detail::raw_native_field(initial_kind),
                     detail::raw_native_field(initial_length)}};
      initial_input.run([&] {
        auto initial_credit = initial_input.bytes_credit(initial_length);
        const auto initial_original =
            initial_input.bytes(4202, initial, initial_credit);
        ui.initial_source({initial_original, 0, initial_length});
        ui.raw_parent(initial_input.reference());
        struct JoinedHandoff {
          RawSuccessorHandoff &handoff;
          ~JoinedHandoff() { (void)handoff.joined(); }
        } joined{handoff};
        ui.run(
            perform, cancelled,
            [&] { return restart_pending.load() || new_pending.load(); },
            std::string{initial}, session / "ui-state.bbm",
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
            [&] { return interrupt_owner.reset(); }, root.get());
      });
      return finish_interactive();
    }
    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))
      for (const auto &line : blackbird_startup(80, 24, false))
        write_plain_output(STDOUT_FILENO, PlainOutputKind::greeting,
                           std::array<std::string_view, 2>{line, "\n"},
                           &session_capture);
    write_plain_output(STDOUT_FILENO, PlainOutputKind::greeting,
                       std::array<std::string_view, 7>{
                           "Blackbird · ", model, " · ", effort, " · ",
                           session.native(),
                           "\n/help lists commands; /quit or /exit exits.\n"},
                       &session_capture);
    if (resume_turn)
      perform("continue");
    if (restart_pending.load() || new_pending.load())
      return finish_interactive();
    std::string pending_input;
    bool input_eof = false;
    auto read_line = [&](std::string &result) {
      auto rearm_idle = [&] {
        if (interrupted.load(std::memory_order_relaxed))
          unwrap(interrupt_owner.reset());
      };
      for (;;) {
        // This reader owns no foreground work. An idle SIGINT retires its old
        // wake incarnation before acquiring another input or buffered command.
        rearm_idle();
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
        int ready;
        try {
          ready = raw_main_poll(&session_capture, input, 40);
        } catch (const Error &error) {
          if (error.code != ErrorCode::interrupted ||
              !interrupted.load(std::memory_order_relaxed))
            throw;
          // Only this owner's actual signal can interrupt idle admission. Its
          // refused poll acquired nothing; reset remains fallible and native.
          rearm_idle();
          continue;
        }
        if (ready < 0) {
          if (errno == EINTR)
            continue;
          throw Error{ErrorCode::io, errno};
        }
        if (!ready)
          continue;
        char bytes[512];
        ssize_t count;
        try {
          rearm_idle();
          count = raw_main_read(&session_capture, STDIN_FILENO, bytes, sizeof(bytes));
        } catch (const Error &error) {
          if (error.code != ErrorCode::interrupted ||
              !interrupted.load(std::memory_order_relaxed))
            throw;
          rearm_idle();
          continue;
        }
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
    for (;;) {
      write_plain_output(STDOUT_FILENO, PlainOutputKind::prompt,
                         std::array<std::string_view, 1>{"\nblackbird> "},
                         &session_capture);
      if (!read_line(line))
        break;
      if (line == "/quit" || line == "/exit") {
        raw_main_control(&session_capture, line == "/quit" ? 1 : 2, line);
        break;
      }
      if (line == "/paste") {
        raw_main_control(&session_capture, 3, line);
        line.clear();
        std::string part;
        while (read_line(part) && part != "/send") {
          line += part;
          line += '\n';
        }
        if (part == "/send")
          raw_main_control(&session_capture, 4, part);
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
  if (argc > 1 && std::string_view{argv[1]} == "--application") {
    ApplicationConnection application;
    std::string id;
    try {
      if (argc != 2)
        throw Error{ErrorCode::invalid_range};
      const auto open = application.open();
      id = string_field(open, "id");
      std::vector<std::string> storage{argv[0], "--session",
                                       string_field(open, "session")};
      for (const auto name : {"model", "effort"})
        if (const auto *value = open.find(name)) {
          storage.push_back("--" + std::string{name});
          storage.push_back(value->string());
        }
      std::vector<char *> arguments;
      for (auto &argument : storage)
        arguments.push_back(argument.data());
      arguments.push_back(nullptr);
      std::vector<std::string> next, fallback;
      const auto status =
          run_session(static_cast<int>(storage.size()), arguments.data(),
                      {.next = next, .fallback = fallback}, nullptr, &application);
      if (status)
        application.fatal(id, "Session could not open or application transport failed");
      return status;
    } catch (const Error &failure) {
      application.fatal(id, error_name(failure.code));
      return 1;
    } catch (const std::exception &failure) {
      application.fatal(id, failure.what());
      return 1;
    }
  }
  try {
    std::vector<std::string> current_arguments, return_session;
    std::vector<char *> arguments;
    for (;;) {
      std::vector<std::string> next_session, fallback_session;
      RawSuccessorHandoff handoff;
      const auto status = run_session(
          argc, argv, {.next = next_session, .fallback = fallback_session}, &handoff);
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
      if (status == 75)
        handoff.prepare_exec(current_arguments.size());
      arguments.clear();
      for (auto &argument : current_arguments)
        arguments.push_back(argument.data());
      arguments.push_back(nullptr);
      if (status == 75) {
        // Use the current directory even after /new, and load the published binary.
        (void)handoff.execute(arguments);
        std::cerr << "blackbird: cannot restart current session (" << errno << ")\n";
        return 1;
      }
      // Old UI, workers and journal are destroyed before opening the fresh session.
      handoff.consume();
      argc = static_cast<int>(current_arguments.size());
      argv = arguments.data();
    }
  } catch (const Error &error) {
    std::cerr << "blackbird: " << error_name(error.code) << " (" << error.detail
              << ")\n";
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "blackbird: " << safe(error.what()) << '\n';
    return 1;
  }
}
