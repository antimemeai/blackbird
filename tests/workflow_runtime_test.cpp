#include "blackbird/tools.hpp"
#include "blackbird/workflow_runtime.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace blackbird;
namespace {
std::string stage;
void check(bool good, const char *message) {
  if (!good)
    throw std::runtime_error(message);
}
template <class IdType> IdType id(unsigned char byte) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{byte};
  return unwrap(IdType::from_bytes(bytes));
}
JournalHeader header() {
  return {id<EnvironmentId>(1),
          id<AuditStreamId>(2),
          3,
          {32 * 1024 * 1024, 96 * 1024 * 1024},
          {}};
}
constexpr JournalCapacity capacity{64 * 1024 * 1024, 20000};
HandleDomain domain(unsigned char incarnation = 4) {
  return {id<EnvironmentId>(1), id<ProcessIncarnationId>(incarnation),
          id<RegistryId>(5)};
}
struct Settled final : CustodyVerifier {
  Result<void> verify(std::span<const AttemptState> attempts) override {
    return attempts.empty() ? Result<void>::success()
                            : Result<void>::failure({ErrorCode::external_unknown});
  }
};
struct SyncFault {
  bool fail = false;
  std::size_t failures = 0;
};
class FaultFile final : public JournalFile {
public:
  FaultFile(std::unique_ptr<JournalFile> file, std::shared_ptr<SyncFault> fault)
      : file_(std::move(file)), fault_(std::move(fault)) {}
  Result<std::size_t> read_at(std::uint64_t offset, MutableByteView output) override {
    return file_->read_at(offset, output);
  }
  Result<std::size_t> write_at(std::uint64_t offset, ByteView input) override {
    return file_->write_at(offset, input);
  }
  Result<std::uint64_t> extent() override { return file_->extent(); }
  Result<void> synchronize(SyncStrength strength) override {
    if (fault_->fail) {
      ++fault_->failures;
      return Result<void>::failure({ErrorCode::io, EIO});
    }
    return file_->synchronize(strength);
  }
  Result<void> lock_writer() override { return file_->lock_writer(); }
  void release_writer() noexcept override { file_->release_writer(); }
  RawStorageResource *raw_storage_resource() noexcept override {
    return file_->raw_storage_resource();
  }
  Result<void> begin_write_batch(std::size_t bytes,
                                 RawStorageWriteContinuation &continuation) override {
    return file_->begin_write_batch(bytes, continuation);
  }
  Result<std::size_t>
  write_at_continuing(std::uint64_t offset, ByteView input,
                      RawStorageWriteContinuation &continuation) override {
    return file_->write_at_continuing(offset, input, continuation);
  }

private:
  std::unique_ptr<JournalFile> file_;
  std::shared_ptr<SyncFault> fault_;
};
class FaultDirectory final : public JournalDirectory {
public:
  FaultDirectory(NativeJournalDirectory directory, std::shared_ptr<SyncFault> fault)
      : directory_(std::move(directory)), fault_(std::move(fault)) {}
  Result<std::unique_ptr<JournalFile>>
  create_exclusive(std::string_view name) override {
    return wrap(directory_.create_exclusive(name));
  }
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view name,
                                                     FileAccess access) override {
    return wrap(directory_.open_existing(name, access));
  }
  Result<void> synchronize_directory(SyncStrength strength) override {
    return directory_.synchronize_directory(strength);
  }
  Result<void> replace_file(std::string_view source, std::string_view target) override {
    return directory_.replace_file(source, target);
  }
  Result<ScratchCleanup> reclaim_publication_scratch(std::string_view name,
                                                     std::size_t scanned,
                                                     std::size_t removed) override {
    return directory_.reclaim_publication_scratch(name, scanned, removed);
  }

private:
  Result<std::unique_ptr<JournalFile>> wrap(Result<std::unique_ptr<JournalFile>> file) {
    if (!file.has_value())
      return Result<std::unique_ptr<JournalFile>>::failure(file.error());
    return Result<std::unique_ptr<JournalFile>>::success(
        std::make_unique<FaultFile>(std::move(file.value()), fault_));
  }
  NativeJournalDirectory directory_;
  std::shared_ptr<SyncFault> fault_;
};
std::unique_ptr<RetainedState> create(const std::filesystem::path &directory,
                                      JournalCapacity bounds = capacity) {
  stage = directory.filename().string() + ": create session";
  check(std::filesystem::create_directory(directory), "session already exists");
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);
  return unwrap(
      RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(directory.string()))),
                            "audit", header(), bounds));
}
std::unique_ptr<RetainedState> reopen(const std::filesystem::path &directory,
                                      JournalCapacity bounds = capacity) {
  stage = directory.filename().string() + ": open native journal directory";
  auto native = unwrap(NativeJournalDirectory::open(directory.string()));
  stage = directory.filename().string() + ": RetainedState::open";
  auto root = unwrap(
      RetainedState::open(std::make_unique<NativeJournalDirectory>(std::move(native)),
                          "audit", header(), bounds));
  stage = directory.filename().string() + ": RetainedState::confirm_recovery";
  unwrap(root->confirm_recovery());
  Settled verifier;
  stage = directory.filename().string() + ": RetainedState::reconcile";
  unwrap(root->reconcile(verifier));
  return root;
}
Value operation(std::string name, const std::string &task) {
  return Value::object({{"op", Value{std::move(name)}}, {"id", Value{task}}});
}
Value read(WorkflowRuntime &runtime, const std::string &name) {
  return runtime.request(operation("read", name));
}
Value start(WorkflowRuntime &runtime, const std::string &name, const char *tool,
            Value arguments) {
  stage = "start: " + name;
  auto request = operation("start", name);
  request.object().emplace_back("tool", Value{tool});
  request.object().emplace_back("arguments", std::move(arguments));
  return runtime.request(request);
}
Value settled(WorkflowRuntime &runtime, const std::string &name) {
  stage = "settle: " + name;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (std::chrono::steady_clock::now() < end) {
    auto row = read(runtime, name);
    if (string_field(row, "outcome") != "pending")
      return row;
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  throw std::runtime_error("native result did not settle");
}
void shutdown(WorkflowRuntime &runtime) {
  stage = "explicit runtime shutdown";
  runtime.shutdown();
}
Value join(WorkflowRuntime &runtime, const std::string &name, const char *mode,
           const std::vector<std::string> &members, std::size_t quorum = 1) {
  Value::Array names;
  for (const auto &member : members)
    names.emplace_back(member);
  auto request = operation("join", name);
  request.object().emplace_back("mode", Value{mode});
  request.object().emplace_back("members", Value{std::move(names)});
  request.object().emplace_back("quorum", Value{Number{quorum}});
  stage = "join: " + name;
  return runtime.request(request);
}
Value join_read(WorkflowRuntime &runtime, const std::string &name) {
  return runtime.request(operation("join-read", name));
}
const Value &originals(const Value &row) {
  return field(field(row, "result"), "originals");
}
std::string original(const Value &row, std::string_view label) {
  for (const auto &item : originals(row).array())
    if (string_field(item, "label") == label)
      return string_field(item, "bytes");
  throw std::runtime_error("missing original " + std::string{label});
}
void terminal_link(RetainedState &root, const Value &row,
                   AttemptDisposition disposition) {
  stage = "native terminal linkage: " + string_field(row, "key");
  const auto attempt = string_field(row, "attempt");
  const auto result_id = string_field(row, "result_record");
  std::optional<RecordReference> terminal_record, application_record;
  std::size_t terminals = 0;
  const auto history = root.history_snapshot();
  for (std::size_t index = 0; index < history.count; ++index) {
    const auto fact = unwrap(history.read(index));
    if (const auto *observed = std::get_if<AttemptObservationEvent>(&fact.event.body);
        observed && hex_identity(observed->attempt.bytes()) == attempt &&
        observed->phase == AttemptPhase::terminal) {
      check(observed->disposition == disposition, "wrong native terminal disposition");
      terminal_record = fact.record;
      ++terminals;
    }
    if (const auto *application = std::get_if<ApplicationRecordEvent>(&fact.event.body);
        application && hex_identity(application->identity.bytes()) == result_id)
      application_record = fact.record;
  }
  // The application and terminal are adjacent semantic frames in one append.
  // A committed boundary itself consumes a sequence, so exact adjacency rules
  // out a commit separating these two records.
  check(terminals == 1 && terminal_record && application_record &&
            terminal_record->journal == application_record->journal &&
            application_record->sequence != UINT64_MAX &&
            terminal_record->sequence == application_record->sequence + 1,
        "task result and native terminal were not one atomic append");
}
void cancellation_intent(RetainedState &root, const Value &row) {
  const auto attempt = Value{string_field(row, "attempt")};
  std::size_t intents = 0;
  const auto history = root.history_snapshot();
  for (std::size_t index = 0; index < history.count; ++index) {
    const auto fact = unwrap(history.read(index));
    const auto *application = std::get_if<ApplicationRecordEvent>(&fact.event.body);
    if (!application || application->channel != ApplicationChannel::program)
      continue;
    const auto packet = unwrap(read_application_packet(*application));
    const auto *request = packet.find("request");
    const auto *decision = request ? request->find("decision") : nullptr;
    const auto *kind = decision ? decision->find("operation") : nullptr;
    if (kind && *kind == Value{"cancel"} && field(*decision, "attempt") == attempt)
      ++intents;
  }
  check(intents == 1, "cancellation did not retain its addressed control intent");
}

void write_all(int descriptor, std::string_view value) {
  while (!value.empty()) {
    const auto written = ::write(descriptor, value.data(), value.size());
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0)
      throw std::runtime_error("fixture write failed");
    value.remove_prefix(static_cast<std::size_t>(written));
  }
}
std::string prefix(const std::string &tag, bool large = false) {
  return "prefix:" + tag + "\n" + (large ? std::string(256 * 1024, 'P') : "");
}
std::string suffix(const std::string &tag) { return "suffix:" + tag + "\n"; }
int child_fixture(const std::filesystem::path &directory, const std::string &tag,
                  const std::string &mode) {
  const bool held = mode != "finish";
  const auto gate = directory / (tag + ".gate");
  const int descriptor = held ? ::open(gate.c_str(), O_RDWR | O_CLOEXEC) : -1;
  if (held && descriptor < 0)
    return 11;
  const int launches = ::open((directory / "launches").c_str(),
                              O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
  if (launches < 0)
    return 12;
  write_all(launches, tag + "\n");
  ::close(launches);
  write_all(STDOUT_FILENO, prefix(tag, mode == "partial"));
  write_file(directory / (tag + ".ready"), std::to_string(::getpid()));
  if (held) {
    char byte = 0;
    ssize_t count = 0;
    do {
      count = ::read(descriptor, &byte, 1);
    } while (count < 0 && errno == EINTR);
    ::close(descriptor);
    if (count != 1)
      return 13;
  }
  write_all(STDOUT_FILENO, suffix(tag));
  return 0;
}
Value child_arguments(const std::filesystem::path &executable,
                      const std::filesystem::path &directory, const std::string &tag,
                      const char *mode = "hold") {
  if (std::string_view{mode} != "finish")
    check(::mkfifo((directory / (tag + ".gate")).c_str(), 0600) == 0,
          "fixture gate creation failed");
  return Value::object(
      {{"argv",
        Value{Value::Array{Value{executable.string()}, Value{"--native-leaf"},
                           Value{directory.string()}, Value{tag}, Value{mode}}}},
       {"timeout_seconds", Value{Number{15}}}});
}
void wait_marker(const std::filesystem::path &directory, const std::string &tag) {
  stage = "wait child marker: " + tag;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!std::filesystem::exists(directory / (tag + ".ready"))) {
    check(std::chrono::steady_clock::now() < end, "native child did not reach gate");
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
}
void release(const std::filesystem::path &directory, const std::string &tag) {
  const int descriptor =
      ::open((directory / (tag + ".gate")).c_str(), O_WRONLY | O_CLOEXEC);
  check(descriptor >= 0, "fixture release open failed");
  write_all(descriptor, "G");
  ::close(descriptor);
}

void exact_reopen(const std::filesystem::path &directory) {
  const auto argument = Value::object({{"answer", Value{Number{42}}}});
  std::string invocation;
  {
    auto root = create(directory);
    AuditLog log{*root};
    WorkflowRuntime runtime{log, domain(), {2, 8}, nullptr};
    (void)start(runtime, "one", "echo", argument);
    const auto first = settled(runtime, "one");
    invocation = string_field(first, "invocation");
    check(string_field(first, "outcome") == "success", "wrong logical result");
    check(field(field(first, "result"), "value") == argument, "wrong exact output");
    terminal_link(*root, first, AttemptDisposition::success);
    check(string_field(start(runtime, "one", "echo", argument), "invocation") ==
              invocation,
          "same submission reexecuted");
    bool refused = false;
    try {
      (void)start(runtime, "one", "echo", Value{Number{43}});
    } catch (const Error &error) {
      refused = error.code == ErrorCode::conflict;
    }
    check(refused, "changed submission identity accepted");
    shutdown(runtime);
  }
  {
    auto root = reopen(directory);
    AuditLog log{*root};
    stage = "exact: construct restored WorkflowRuntime";
    WorkflowRuntime runtime{log, domain(9), {2, 8}, nullptr};
    const auto recovered = read(runtime, "one");
    check(string_field(recovered, "invocation") == invocation, "lost logical identity");
    check(field(field(recovered, "result"), "value") == argument,
          "lost result on reopen");
    check(string_field(start(runtime, "one", "echo", argument), "invocation") ==
              invocation,
          "recovered result reexecuted");
    shutdown(runtime);
  }
}

void concurrent_effects_and_joins(const std::filesystem::path &directory,
                                  const std::filesystem::path &executable) {
  Value selection;
  std::string launches;
  {
    auto root = create(directory);
    AuditLog log{*root};
    WorkflowRuntime runtime{log, domain(), {2, 3}, nullptr};
    const auto first = start(runtime, "alpha", "exec",
                             child_arguments(executable, directory, "alpha"));
    const auto second =
        start(runtime, "beta", "exec", child_arguments(executable, directory, "beta"));
    wait_marker(directory, "alpha");
    wait_marker(directory, "beta");
    check(string_field(first, "attempt") != string_field(second, "attempt") &&
              field(first, "released") == Value{true} &&
              field(second, "released") == Value{true} &&
              string_field(read(runtime, "alpha"), "outcome") == "pending" &&
              string_field(read(runtime, "beta"), "outcome") == "pending",
          "gated native executions were not simultaneously live and admitted");
    (void)start(runtime, "queued", "exec",
                child_arguments(executable, directory, "queued"));
    const auto waiting = join(runtime, "winner", "first_success", {"alpha", "beta"});
    check(field(waiting, "selected") == Value{false},
          "pending join selected before either gate");
    (void)join(runtime, "both", "all", {"beta", "alpha"});
    const auto cancelled = runtime.request(operation("cancel", "queued"));
    check(field(cancelled, "cancellation_observation") == Value{Number{0}},
          "queued cancellation did not prevent invocation");
    release(directory, "alpha");
    const auto alpha = settled(runtime, "alpha");
    check(original(alpha, "process.combined") == prefix("alpha") + suffix("alpha"),
          "native stdout original changed");
    selection = join_read(runtime, "winner");
    check(field(selection, "selected") == Value{true},
          "ready first-success join not selected");
    const auto &rows =
        field(field(field(selection, "state"), "selection"), "observations").array();
    check(rows.size() == 1 && string_field(rows[0], "member") == "alpha" &&
              field(field(rows[0], "result"), "cell").string().size() == 32,
          "selection lost member or native result-cell reference");
    release(directory, "beta");
    const auto beta = settled(runtime, "beta");
    const auto queued = settled(runtime, "queued");
    check(original(beta, "process.combined") == prefix("beta") + suffix("beta") &&
              string_field(queued, "outcome") == "cancelled" &&
              field(field(queued, "result"), "invoked") == Value{false} &&
              !std::filesystem::exists(directory / "queued.ready"),
          "queued cancellation invoked a child or lost successful output");
    check(join_read(runtime, "winner") == selection,
          "late result rewrote selected join");
    const auto both = join_read(runtime, "both");
    const auto &both_rows =
        field(field(field(both, "state"), "selection"), "observations").array();
    check(both_rows.size() == 2 && string_field(both_rows[0], "member") == "beta" &&
              string_field(both_rows[1], "member") == "alpha",
          "all join lost input order");
    check(field(join(runtime, "ready", "quorum", {"alpha", "beta"}, 2), "selected") ==
              Value{true},
          "join of completed tasks did not select");
    bool conflict = false;
    try {
      (void)join(runtime, "winner", "all", {"alpha", "beta"});
    } catch (const Error &error) {
      conflict = error.code == ErrorCode::conflict;
    }
    check(conflict, "changed join reused retained identity");
    terminal_link(*root, alpha, AttemptDisposition::success);
    terminal_link(*root, beta, AttemptDisposition::success);
    terminal_link(*root, queued, AttemptDisposition::cancellation);
    cancellation_intent(*root, queued);
    launches = read_file(directory / "launches");
    check((launches == "alpha\nbeta\n" || launches == "beta\nalpha\n"),
          "native effects did not occur once per invoked task");
    shutdown(runtime);
  }
  {
    auto root = reopen(directory);
    AuditLog log{*root};
    stage = "concurrency: construct restored WorkflowRuntime";
    WorkflowRuntime runtime{log, domain(9), {2, 3}, nullptr};
    check(join_read(runtime, "winner") == selection,
          "reopen changed committed selection");
    check(read_file(directory / "launches") == launches,
          "reopen replayed native effects");
    shutdown(runtime);
  }
}

void file_originals_and_bounds(const std::filesystem::path &directory) {
  auto root = create(directory);
  AuditLog log{*root};
  WorkflowRuntime runtime{log, domain(), {2, 4}, nullptr};
  const auto path = directory / "source.txt";
  const std::string initial = "header\n" + std::string(8192, 'A') + "\nUNIQUE\ntail\n";
  write_file(path, initial);
  auto arguments =
      Value::object({{"path", Value{path.string()}},
                     {"range", Value::object({{"mode", Value{"bytes"}},
                                              {"start", Value{Number{7}}},
                                              {"end", Value{Number{23}}}})}});
  (void)start(runtime, "read", "read_file", arguments);
  const auto shown = settled(runtime, "read");
  check(string_field(field(field(shown, "result"), "value"), "content") ==
                initial.substr(7, 16) &&
            original(shown, "file.read") == initial,
        "bounded presentation replaced complete read original");
  const std::string replacement = "changed\nUNIQUE\n" + std::string(8192, 'B');
  (void)start(
      runtime, "write", "write_file",
      Value::object({{"path", Value{path.string()}}, {"content", Value{replacement}}}));
  const auto written = settled(runtime, "write");
  check(original(written, "file.before") == initial &&
            original(written, "file.proposed") == replacement &&
            read_file(path) == replacement,
        "write changed before/proposed originals or actual bytes");
  (void)start(runtime, "edit", "edit_file",
              Value::object({{"path", Value{path.string()}},
                             {"old", Value{"UNIQUE"}},
                             {"new", Value{"EDITED"}}}));
  const auto edited = settled(runtime, "edit");
  auto final = replacement;
  final.replace(final.find("UNIQUE"), 6, "EDITED");
  check(original(edited, "file.before") == replacement &&
            original(edited, "file.proposed") == final && read_file(path) == final,
        "edit lost exact retained before/proposal or physical effect");
  (void)start(
      runtime, "bad-edit", "edit_file",
      Value::object(
          {{"path", Value{path.string()}}, {"old", Value{"BB"}}, {"new", Value{"x"}}}));
  const auto rejected = settled(runtime, "bad-edit");
  check(string_field(rejected, "outcome") == "unknown" &&
            original(rejected, "file.before") == final && read_file(path) == final &&
            field(field(field(rejected, "result"), "error"), "code") ==
                Value{Number{static_cast<unsigned>(ErrorCode::conflict)}},
        "duplicate edit changed file or lost captured original/error");
  bool capacity_refused = false;
  try {
    (void)start(runtime, "oversized-input", "write_file",
                Value::object({{"path", Value{path.string()}},
                               {"content", Value{std::string(512 * 1024 + 1, 'X')}}}));
  } catch (const Error &error) {
    capacity_refused = error.code == ErrorCode::capacity;
  }
  check(capacity_refused && read_file(path) == final,
        "oversized input reached an external write");
  const auto large = directory / "too-large.txt";
  write_file(large, std::string(1024 * 1024 + 1, 'L'));
  (void)start(runtime, "oversized-original", "read_file",
              Value::object({{"path", Value{large.string()}}}));
  const auto bounded = settled(runtime, "oversized-original");
  check(string_field(bounded, "outcome") == "unknown" &&
            field(field(field(bounded, "result"), "error"), "code") ==
                Value{Number{static_cast<unsigned>(ErrorCode::capacity)}},
        "oversized original was silently truncated or accepted");
  shutdown(runtime);
}

void cancellation_partial(const std::filesystem::path &directory,
                          const std::filesystem::path &executable) {
  Value retained_result;
  std::string launches;
  {
    auto root = create(directory);
    AuditLog log{*root};
    WorkflowRuntime runtime{log, domain(), {1, 2}, nullptr};
    (void)start(runtime, "cancel", "exec",
                child_arguments(executable, directory, "cancel", "partial"));
    wait_marker(directory, "cancel");
    (void)runtime.request(operation("cancel", "cancel"));
    const auto result = settled(runtime, "cancel");
    const auto partial = original(result, "process.partial");
    const auto emitted = prefix("cancel", true);
    check(string_field(result, "outcome") == "unknown" && !partial.empty() &&
              partial.size() <= emitted.size() && emitted.starts_with(partial) &&
              field(field(result, "result"), "invoked") == Value{true} &&
              field(field(result, "result"), "cancel_requested") == Value{true},
          "running cancellation invented noninvocation or lost observed partials");
    terminal_link(*root, result, AttemptDisposition::unknown);
    cancellation_intent(*root, result);
    retained_result = field(result, "result");
    launches = read_file(directory / "launches");
    check(launches == "cancel\n", "cancelled command launched more than once");
    shutdown(runtime);
  }
  {
    auto root = reopen(directory);
    AuditLog log{*root};
    stage = "cancel: construct restored WorkflowRuntime";
    WorkflowRuntime runtime{log, domain(9), {1, 2}, nullptr};
    check(field(read(runtime, "cancel"), "result") == retained_result &&
              read_file(directory / "launches") == launches,
          "reopen lost partial bytes or replayed cancelled effect");
    shutdown(runtime);
  }
}

void destructor_drains_healthy_results(const std::filesystem::path &directory,
                                       const std::filesystem::path &executable) {
  auto root = create(directory);
  AuditLog log{*root};
  std::mutex mutex;
  std::condition_variable changed;
  bool complete = false;
  {
    WorkflowRuntime runtime{log, domain(), {1, 1}, nullptr, [&] {
                              const std::lock_guard lock{mutex};
                              complete = true;
                              changed.notify_one();
                            }};
    (void)start(runtime, "scope-exit", "exec",
                child_arguments(executable, directory, "scope-exit", "finish"));
    stage = "healthy scope exit: wait completion without owner tick";
    std::unique_lock lock{mutex};
    check(changed.wait_for(lock, std::chrono::seconds{5}, [&] { return complete; }),
          "leaf never completed before destructor");
  }
  stage = "healthy scope exit: inspect retained result";
  WorkflowRuntime restored{log, domain(9), {1, 1}, nullptr};
  const auto result = read(restored, "scope-exit");
  check(string_field(result, "outcome") == "success" &&
            original(result, "process.combined") ==
                prefix("scope-exit") + suffix("scope-exit"),
        "healthy runtime destructor joined but discarded unpumped completion");
  terminal_link(*root, result, AttemptDisposition::success);
  shutdown(restored);
}

void result_credit_survives_ordinary_exhaustion(
    const std::filesystem::path &directory, const std::filesystem::path &executable) {
  constexpr JournalCapacity small{8 * 1024 * 1024, 4096};
  auto root = create(directory, small);
  AuditLog log{*root};
  WorkflowRuntime runtime{log, domain(), {1, 1}, nullptr};
  (void)start(runtime, "credited", "exec",
              child_arguments(executable, directory, "credited"));
  wait_marker(directory, "credited");
  stage = "consume ordinary headroom beside protected result credit";
  const auto held = root->protected_settlement();
  check(held && held->max_file_bytes != 0 && held->max_records >= 2,
        "no terminal credit acquired before native dispatch");
  bool refused = false;
  for (std::size_t count = 0; count < 128; ++count) {
    const auto usage = root->journal_usage();
    const auto remaining = usage.remaining_bytes();
    const auto credit = root->protected_settlement();
    if (!remaining || !credit || *remaining < credit->max_file_bytes)
      throw std::runtime_error("result credit exceeded physical remaining bytes");
    const auto normal = *remaining - credit->max_file_bytes;
    const auto overhead = std::uint64_t{56} + journal_frame_header_size;
    const auto amount =
        normal > overhead ? std::min<std::uint64_t>(256 * 1024, normal - overhead) : 1;
    const std::vector<std::byte> chunk(static_cast<std::size_t>(amount),
                                       std::byte{0x5a});
    const std::array<ByteView, 1> sources{chunk};
    const auto before = root->cursor();
    const auto appended = root->append(before, sources, {});
    if (!appended.has_value()) {
      check(appended.error().code == ErrorCode::capacity &&
                root->cursor().end_offset == before.end_offset &&
                root->state() == JournalWriterState::live,
            "ordinary capacity refusal changed committed state or poisoned writer");
      refused = true;
      break;
    }
  }
  check(refused, "fixture did not exhaust ordinary headroom");
  release(directory, "credited");
  const auto result = settled(runtime, "credited");
  check(string_field(result, "outcome") == "success" &&
            original(result, "process.combined") ==
                prefix("credited") + suffix("credited") &&
            !root->protected_settlement(),
        "reserved atomic result failed after ordinary headroom exhaustion");
  terminal_link(*root, result, AttemptDisposition::success);
  shutdown(runtime);
}

void writer_fault_contains_population(const std::filesystem::path &directory,
                                      const std::filesystem::path &executable) {
  stage = "writer fault: private native session";
  check(std::filesystem::create_directory(directory), "fault session already exists");
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace);
  auto fault = std::make_shared<SyncFault>();
  auto root = unwrap(RetainedState::create(
      std::make_unique<FaultDirectory>(
          unwrap(NativeJournalDirectory::open(directory.string())), fault),
      "audit", header(), capacity));
  AuditLog log{*root};
  std::mutex mutex;
  std::condition_variable changed;
  std::size_t completions = 0;
  WorkflowRuntime runtime{log, domain(), {2, 4}, nullptr, [&] {
                            const std::lock_guard lock{mutex};
                            ++completions;
                            changed.notify_one();
                          }};
  (void)start(runtime, "running", "exec",
              child_arguments(executable, directory, "running", "partial"));
  (void)start(runtime, "running-two", "exec",
              child_arguments(executable, directory, "running-two"));
  wait_marker(directory, "running");
  wait_marker(directory, "running-two");
  (void)start(runtime, "completed", "exec",
              child_arguments(executable, directory, "completed"));
  (void)start(runtime, "never-invoked", "exec",
              child_arguments(executable, directory, "never-invoked"));
  (void)runtime.request(operation("cancel", "completed"));
  stage = "writer fault: queued cancellation completed beside occupied workers";
  {
    std::unique_lock lock{mutex};
    check(changed.wait_for(lock, std::chrono::seconds{5},
                           [&] { return completions == 1; }),
          "completed task did not publish executor completion");
  }
  fault->fail = true;
  stage = "writer fault: failed result sync must stop running and queued leaves";
  runtime.tick();
  check(fault->failures == 1 && runtime.stopping(),
        "failed result synchronization did not contain runtime");
  {
    const std::lock_guard lock{mutex};
    check(completions == 4, "fault containment returned before joining every leaf");
  }
  const auto pid =
      static_cast<pid_t>(std::stol(read_file(directory / "running.ready")));
  errno = 0;
  check(::kill(pid, 0) == -1 && errno == ESRCH,
        "failed writer left the gated process alive");
  check(!std::filesystem::exists(directory / "never-invoked.ready") &&
            !std::filesystem::exists(directory / "completed.ready"),
        "failed writer invoked queued work during cleanup");
  const auto launches = read_file(directory / "launches");
  check(launches == "running-two\nrunning\n" || launches == "running\nrunning-two\n",
        "failed writer repeated or added an external effect");
  const auto status_request = Value::object({{"op", Value{"status"}}});
  auto verify_debt = [&] {
    const auto status = runtime.request(status_request);
    check(field(status, "stopped") == Value{true} &&
              field(status, "admission_closed") == Value{true} &&
              field(status, "recovery_required") == Value{true} &&
              field(status, "completion_debt") == Value{Number{4}},
          "failed controls discarded completion debt or reopened admission");
    for (const auto &name : {"completed", "running", "running-two", "never-invoked"})
      check(string_field(read(runtime, name), "outcome") == "pending",
            "failed writer fabricated a retained terminal result");
  };
  verify_debt();
  bool cancel_refused = false;
  try {
    (void)runtime.request(operation("cancel", "running"));
  } catch (const Error &) {
    cancel_refused = true;
  }
  check(cancel_refused, "writer fault admitted a new cancellation record");
  bool close_refused = false;
  try {
    shutdown(runtime);
  } catch (const Error &) {
    close_refused = true;
  }
  check(close_refused, "writer fault falsely acknowledged durable shutdown");
  bool start_refused = false;
  try {
    (void)start(runtime, "new", "echo", Value{Number{1}});
  } catch (const Error &error) {
    start_refused = error.code == ErrorCode::busy;
  }
  check(start_refused, "writer fault admitted a new invocation");
  verify_debt();
}
} // namespace

int main(int argc, char **argv) {
  if (argc == 5 && std::string_view{argv[1]} == "--native-leaf") {
    try {
      return child_fixture(argv[2], argv[3], argv[4]);
    } catch (...) {
      return 14;
    }
  }
  char pattern[] = "/tmp/bb-wfr-XXXXXX";
  const auto *temporary = ::mkdtemp(pattern);
  if (!temporary)
    return 2;
  const std::filesystem::path directory{temporary};
  try {
    const auto executable = std::filesystem::absolute(argv[0]);
    exact_reopen(directory / "exact");
    concurrent_effects_and_joins(directory / "concurrency", executable);
    file_originals_and_bounds(directory / "files");
    cancellation_partial(directory / "cancel", executable);
    destructor_drains_healthy_results(directory / "scope", executable);
    result_credit_survives_ordinary_exhaustion(directory / "credit", executable);
    writer_fault_contains_population(directory / "fault", executable);
    std::filesystem::remove_all(directory);
    std::cout
        << "workflow runtime: actual concurrent effects, originals, cancellation, "
           "scope drain, reserved results, joins and reopen passed\n";
  } catch (const Error &error) {
    std::cerr << stage << ": " << error_name(error.code) << ':' << error.detail << '\n';
    std::cerr << "failed private runtime session preserved at " << directory << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << stage << ": " << error.what() << '\n';
    std::cerr << "failed private runtime session preserved at " << directory << '\n';
    return 1;
  }
}
