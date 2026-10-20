// Provider profile wiring (unit P1b, arconaut-rd7.6), driven through the real binary.
// Oracles: the bytes on disk (journal records and S0's clean-stop snapshot, read
// without a ContextStore), the P1a library over the same directories, and the exact
// refusal text. A session loads its profile set once at start; a refusal creates or
// writes nothing; BLACKBIRD_PROFILE_SET_DIGEST pins the set's content digest; /profile
// shows the layers, basis and digests in force; an override edited mid-run is unseen
// until the next session start; every provider.request records the effective
// profile; the provider-profile-set-v1 record survives the snapshot and a reopen of
// an unchanged set writes nothing.
//
// `profile_wiring_test read SESSION` prints the session's request profiles and its
// latest set record, for scripts/check-installed-profiles.
#include "blackbird/context.hpp"
#include "blackbird/journal_storage.hpp"
#include "blackbird/journal_writer.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/provider_profile.hpp"
#include "blackbird/saved_state.hpp"
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char **environ;
using namespace blackbird;
namespace {
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
std::string executable;
fs::path shipped;
fs::path workspace;
int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
std::string contents(const fs::path &path) {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
void write_text(const fs::path &path, std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  output << text;
  check(bool(output), "fixture write failed: " + path.string());
}
bool contains(std::string_view text, std::string_view needle) {
  return text.find(needle) != std::string_view::npos;
}

// One case's isolated home and credentials, a local workflow and optional
// environment entries (BLACKBIRD_PROFILES, the pin, a fake provider transport).
struct Fixture {
  fs::path root, home, bin, workflow;
  std::vector<std::string> extra;
  explicit Fixture(std::string_view name)
      : root(workspace / name), home(root / "home"), bin(root / "bin"),
        workflow(root / "local.lua") {
    fs::create_directories(home);
    fs::create_directories(bin);
    // Private, whatever the umask: the auth store refuses group-writable parents.
    for (const auto &directory : {root, home, bin})
      fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace);
    write_text(workflow, "print('LOCAL')\n");
  }
  fs::path operator_directory() const {
    return home / ".local/share/blackbird/profiles";
  }
  fs::path session(std::string_view name) const { return root / name; }
  std::vector<std::string> environment() const {
    std::vector<std::string> result;
    for (char **entry = environ; *entry; ++entry) {
      const std::string_view text{*entry};
      if (text.starts_with("HOME=") || text.starts_with("PATH=") ||
          text.starts_with("BLACKBIRD_") || text.starts_with("ARCO_") ||
          text.starts_with("OPENROUTER_"))
        continue;
      result.emplace_back(text);
    }
    const char *path = std::getenv("PATH");
    result.push_back("PATH=" + bin.string() + ":" + (path ? path : "/usr/bin:/bin"));
    result.push_back("HOME=" + home.string());
    result.push_back("BLACKBIRD_AUTH_HOME=" + (root / "auth").string());
    result.push_back("BLACKBIRD_GRAPHICS=off");
    result.push_back("BLACKBIRD_JEV_ENV_FILE=" + (root / "absent-env").string());
    result.insert(result.end(), extra.begin(), extra.end());
    return result;
  }
  // A successful OpenRouter Responses transport: one final message.
  void openrouter() {
    const auto curl = bin / "curl";
    write_text(curl,
               "#!/bin/sh\ncat > /dev/null\nprintf '%s\\n\\n' 'data: "
               "{\"type\":\"response.completed\",\"response\":{\"id\":\"resp1\","
               "\"status\":\"completed\",\"model\":\"openai/gpt-6.1-sol\",\"output\":"
               "[{\"id\":\"m1\",\"type\":\"message\",\"role\":\"assistant\",\"status\":"
               "\"completed\",\"content\":[{\"type\":\"output_text\",\"text\":"
               "\"PROFILE_FIXTURE_DONE\"}]}],\"usage\":{\"input_tokens\":1,"
               "\"output_tokens\":1}}}'\nprintf 'data: [DONE]\\n\\n\\n200'\n");
    fs::permissions(curl, fs::perms::owner_all);
    extra.push_back("OPENROUTER_API_KEY=fixture-secret");
    extra.push_back("BLACKBIRD_OPENROUTER_CURL=" + curl.string());
  }
};

// A child blackbird process; stdin is a pipe, stdout and stderr go to one file.
class Process {
public:
  Process(const Fixture &fixture, std::vector<std::string> arguments,
          std::string_view label)
      : output_(fixture.root / (std::string{label} + ".out")) {
    arguments.insert(arguments.begin(), executable);
    auto environment = fixture.environment();
    std::vector<char *> argv, envp;
    for (auto &argument : arguments)
      argv.push_back(argument.data());
    argv.push_back(nullptr);
    for (auto &entry : environment)
      envp.push_back(entry.data());
    envp.push_back(nullptr);
    int input[2] = {-1, -1};
    check(::pipe(input) == 0, "stdin pipe failed");
    for (const int descriptor : {input[0], input[1]})
      check(::fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 0, "cloexec failed");
    const int out =
        ::open(output_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    check(out >= 0, "output file open failed");
    pid_ = ::fork();
    if (pid_ == 0) {
      if (::dup2(input[0], STDIN_FILENO) < 0 || ::dup2(out, STDOUT_FILENO) < 0 ||
          ::dup2(out, STDERR_FILENO) < 0)
        _exit(126);
      ::execve(argv.front(), argv.data(), envp.data());
      _exit(127);
    }
    ::close(out);
    ::close(input[0]);
    input_ = input[1];
    check(pid_ > 0, "fork failed");
  }
  ~Process() {
    if (pid_ > 0) {
      (void)::kill(pid_, SIGKILL);
      int status = 0;
      (void)::waitpid(pid_, &status, 0);
    }
    if (input_ >= 0)
      ::close(input_);
  }
  Process(const Process &) = delete;
  Process &operator=(const Process &) = delete;
  void send(std::string_view text) {
    while (!text.empty()) {
      const auto written = ::write(input_, text.data(), text.size());
      if (written < 0 && errno == EINTR)
        continue;
      check(written > 0, "child input write failed");
      text.remove_prefix(static_cast<std::size_t>(written));
    }
  }
  void close_input() {
    if (input_ >= 0)
      ::close(input_);
    input_ = -1;
  }
  std::string text() const { return contents(output_); }
  // Waits for output, not time: the offset just past needle at or after from.
  std::size_t await_text(std::string_view needle, std::size_t from = 0) {
    const auto deadline = Clock::now() + std::chrono::seconds{30};
    while (Clock::now() < deadline) {
      if (const auto found = text().find(needle, from); found != std::string::npos)
        return found + needle.size();
      check(running() || text().find(needle, from) != std::string::npos,
            "waiting for " + std::string{needle} + ": child exited\n" + text());
      std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    throw std::runtime_error("waiting for " + std::string{needle} + ": deadline\n" +
                             text());
  }
  bool running() {
    if (pid_ <= 0)
      return false;
    int status = 0;
    if (::waitpid(pid_, &status, WNOHANG) == pid_) {
      pid_ = -1;
      status_ = status;
      return false;
    }
    return true;
  }
  int wait(std::string_view label) {
    const auto deadline = Clock::now() + std::chrono::seconds{60};
    while (running()) {
      check(Clock::now() < deadline, std::string{label} + ": exit deadline\n" + text());
      std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
    return WIFEXITED(status_) ? WEXITSTATUS(status_) : 128 + WTERMSIG(status_);
  }

private:
  fs::path output_;
  pid_t pid_ = -1;
  int input_ = -1, status_ = 0;
};

int run(const Fixture &fixture, std::string_view label,
        std::vector<std::string> arguments, std::string_view input,
        std::string *output = nullptr) {
  Process process{fixture, std::move(arguments), label};
  process.send(input);
  process.close_input();
  const auto status = process.wait(label);
  if (output)
    *output = process.text();
  return status;
}

constexpr JournalCapacity capacity{512ULL * 1024 * 1024, 200000};
// The journal and the newer saved-state slot, read from disk without a ContextStore.
struct OnDisk {
  NativeJournalDirectory directory;
  std::unique_ptr<FramedJournal> journal;
  std::optional<SavedState> saved;
};
OnDisk read_disk(const fs::path &session) {
  auto directory = unwrap(NativeJournalDirectory::open(session.string()));
  std::array<std::byte, journal_header_size> bytes{};
  std::ifstream file{session / "audit", std::ios::binary};
  file.read(reinterpret_cast<char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  check(bool(file), "journal header read failed: " + session.string());
  const auto header = unwrap(decode_journal_header(bytes));
  auto journal = unwrap(FramedJournal::open(directory, "audit", header, capacity));
  auto saved = unwrap(load_saved_state(directory, "audit", *journal, capacity));
  return {std::move(directory), std::move(journal), std::move(saved)};
}
// Every application record on `channel` labelled `wanted`, in journal order.
std::vector<Value> records(const fs::path &session, ApplicationChannel channel,
                           std::string_view wanted) {
  auto disk = read_disk(session);
  std::vector<Value> result;
  for (const auto &record : disk.journal->staged_records()) {
    if (record.kind != FrameKind::semantic)
      continue;
    const auto payload = unwrap(disk.journal->read_payload(record));
    const auto event = unwrap(
        decode_retained_event(payload, disk.journal->header().limits.max_payload));
    const auto *application = std::get_if<ApplicationRecordEvent>(&event.body);
    if (!application || application->channel != channel)
      continue;
    auto packet = unwrap(decode_packet(unwrap(application->payload.read())));
    if (const auto *label = packet.find("label");
        label && std::holds_alternative<std::string>(label->value()) &&
        label->string() == wanted)
      result.push_back(std::move(packet));
  }
  return result;
}
std::vector<Value> set_records(const fs::path &session) {
  return records(session, ApplicationChannel::program, profile_set_record_label);
}
std::vector<Value> request_profiles(const fs::path &session) {
  std::vector<Value> result;
  for (const auto &packet :
       records(session, ApplicationChannel::log, "provider.request")) {
    const auto *metadata = packet.find("metadata");
    const auto *profile = metadata ? metadata->find("profile") : nullptr;
    check(profile != nullptr, "a provider.request carries no profile");
    result.push_back(*profile);
  }
  return result;
}
std::string shown(const Value &value) { return unwrap(format_value(value)); }

// The library's view of the same directories (P1a is the oracle for selection,
// layering and digests).
ProfileDirectories directories(const Fixture &fixture,
                               const fs::path &profiles = shipped) {
  return {profiles, fixture.operator_directory()};
}
ProfileSet loaded(const ProfileDirectories &where) {
  auto set = load_profile_set(where);
  check(set.has_value(),
        "oracle load: " + (set ? std::string{} : set.error().message()));
  return std::move(set).value();
}
EffectiveProfile oracle(const ProfileSet &set, const ProfileQuery &query) {
  auto resolved = resolve_profile(set, query);
  check(resolved.has_value(),
        "oracle resolution: " +
            (resolved ? std::string{} : resolved.error().message()));
  return std::move(resolved).value();
}
// Literal pins for the shipped set at this revision of profiles/providers: change
// them with a reviewed profile edit. The binding digests were computed independently
// (Python: sha256 over the documented canonical encoding of the four route fields).
constexpr std::string_view shipped_set_digest =
    "sha256:b30cab78b1a3fed9e6355456030592502e37f020f70b80bfeb3d077776eecb08";
constexpr std::string_view shipped_content_digest =
    "sha256:a90f2e7f8944c4af7c957a4958bb87afd04b4a581c75a7ac12118f88733e09cb";
constexpr std::string_view openrouter_effective_digest =
    "sha256:7474e6f725ac4fb722bf3bc54506d7cf4e344b39852cc01471506f73412487de";
constexpr std::string_view openrouter_binding =
    "sha256:b3275242b3b0c34850caad0deca1ad5e2af2d72a8c96c7cc98ef3e4565176d6f";
constexpr std::string_view chatgpt_binding =
    "sha256:1c0eca4cc48e58238844ed040d261839e13657905638f7a4f9bbd3570019f4f7";
// The set record a start writes: the set as the library loads it, plus the start route.
Value set_record_with(const ProfileSet &set, const ProfileDirectories &where,
                      Value::Object start) {
  auto record = profile_set_record(set, where);
  record.object().emplace_back("start_route", Value{std::move(start)});
  return record;
}
const ProfileQuery openrouter_query{"openrouter", "responses",
                                    "https://openrouter.ai/api/v1", "environment",
                                    "openai/gpt-6.1-sol"};
const std::vector<std::string> openrouter_once = {
    "--provider", "openrouter", "--model", "openai/gpt-6.1-sol", "--once", "hello"};

// The `set-*` lines of a /profile or --profile-digests output, by name; each must be
// present.
class SetLines {
public:
  explicit SetLines(std::string_view output) : text_(output) {
    for (std::size_t at = 0; at < output.size();) {
      auto end = output.find('\n', at);
      if (end == std::string_view::npos)
        end = output.size();
      const auto line = output.substr(at, end - at);
      if (const auto space = line.find(' ');
          (line.starts_with("set-") || line.starts_with("pin ")) &&
          space != std::string_view::npos)
        lines_.emplace(line.substr(0, space), line.substr(space + 1));
      at = end + 1;
    }
  }
  const std::string &operator[](std::string_view name) const {
    const auto found = lines_.find(name);
    check(found != lines_.end(), "no `" + std::string{name} + "` line\n" + text_);
    return found->second;
  }

private:
  std::string text_;
  std::map<std::string, std::string, std::less<>> lines_;
};
fs::path broken_set(const Fixture &fixture, std::string_view conservative) {
  const auto directory = fixture.root / "broken";
  fs::create_directories(directory);
  for (const auto &entry : fs::directory_iterator{shipped})
    fs::copy_file(entry.path(), directory / entry.path().filename(),
                  fs::copy_options::overwrite_existing);
  write_text(directory / "conservative.lua", conservative);
  return directory;
}

// A missing, empty, unreadable or invalid profile directory (shipped or operator)
// refuses the start visibly, before the session directory is created; for an
// existing session, its journal is left byte for byte as it was.
void refusal_case() {
  struct Refusal {
    std::string_view name;
    std::string reason;
  };
  Fixture fixture{"refusal"};
  const auto missing = fixture.root / "absent-profiles";
  const auto unreadable = fixture.root / "unreadable-profiles";
  fs::create_directories(unreadable);
  const auto invalid = broken_set(fixture, "return { schema = os }\n");
  const auto no_conservative = fixture.root / "no-conservative";
  fs::create_directories(no_conservative);
  write_text(no_conservative / "openrouter-responses.lua",
             contents(shipped / "openrouter-responses.lua"));
  const auto privileged = ::geteuid() == 0;
  std::vector<std::pair<std::vector<std::string>, Refusal>> cases{
      {{"BLACKBIRD_PROFILES=" + missing.string()},
       {"missing",
        missing.string() + ": io: profile directory is missing or unreadable"}},
      {{"BLACKBIRD_PROFILES="}, {"empty", "BLACKBIRD_PROFILES is set but empty"}},
      {{"BLACKBIRD_PROFILES=" + invalid.string()},
       {"invalid", (invalid / "conservative.lua").string() + ":1:19: global:"}},
      {{"BLACKBIRD_PROFILES=" + no_conservative.string()},
       {"no-conservative", no_conservative.string() +
                               ": conservative: the shipped set has no "
                               "conservative profile"}},
  };
  if (!privileged)
    cases.push_back(
        {{"BLACKBIRD_PROFILES=" + unreadable.string()},
         {"unreadable", unreadable.string() + ": io: profile directory is missing or "
                                              "unreadable"}});
  // An existing session, created with the shipped profiles.
  const auto existing = fixture.session("existing");
  std::string created;
  check(run(fixture, "existing-create",
            {"--session", existing.string(), "--workflow", fixture.workflow.string(),
             "--once", "/context"},
            "", &created) == 0,
        "existing session creation failed\n" + created);
  const auto journal_before = contents(existing / "audit");
  if (!privileged)
    ::chmod(unreadable.c_str(), 0);
  for (const auto &[environment, refusal] : cases) {
    fixture.extra = environment;
    const auto fresh =
        fixture.session(std::string{"fresh-"} + std::string{refusal.name});
    for (const auto &session : {fresh, existing}) {
      std::string output;
      const auto label =
          std::string{refusal.name} + (session == existing ? "-existing" : "-fresh");
      const auto status = run(fixture, label,
                              {"--session", session.string(), "--workflow",
                               fixture.workflow.string(), "--once", "hello"},
                              "", &output);
      check(status == 1, label + ": status " + std::to_string(status) + "\n" + output);
      check(contains(output, "Provider profiles refused: ") &&
                contains(output, refusal.reason) &&
                contains(output, "no session was opened"),
            label + ": refusal text\n" + output);
      check(!contains(output, "LOCAL"), label + ": the workflow ran\n" + output);
    }
    check(!fs::exists(fresh),
          std::string{refusal.name} + ": created " + fresh.string());
    // Without --session the fresh directory would be made under the state root.
    std::string output;
    const auto status =
        run(fixture, std::string{refusal.name} + "-unnamed",
            {"--workflow", fixture.workflow.string(), "--once", "hello"}, "", &output);
    check(status == 1 && !fs::exists(fixture.home / ".local/state/blackbird"),
          std::string{refusal.name} + ": an unnamed start made a session\n" + output);
    check(contents(existing / "audit") == journal_before,
          std::string{refusal.name} + ": the existing journal changed");
  }
  if (!privileged)
    ::chmod(unreadable.c_str(), 0700);
  // The operator directory: unreadable refuses; absent means no overrides.
  fixture.extra.clear();
  fs::create_directories(fixture.operator_directory());
  if (!privileged) {
    ::chmod(fixture.operator_directory().c_str(), 0);
    std::string output;
    const auto status =
        run(fixture, "operator-unreadable",
            {"--session", fixture.session("operator").string(), "--workflow",
             fixture.workflow.string(), "--once", "hello"},
            "", &output);
    ::chmod(fixture.operator_directory().c_str(), 0700);
    check(status == 1 &&
              contains(output, fixture.operator_directory().string() +
                                   ": io: profile directory is missing or unreadable"),
          "operator-unreadable: status " + std::to_string(status) + "\n" + output);
    check(!fs::exists(fixture.session("operator")),
          "operator-unreadable: session made");
  }
  // An invalid override names its file; a set without overrides still starts.
  write_text(
      fixture.operator_directory() / "bad.lua",
      "return { schema = \"blackbird.provider-profile-override/1\", id = \"bad\", "
      "revision = 1, target = \"nowhere\" }\n");
  std::string output;
  auto status = run(fixture, "operator-invalid",
                    {"--session", fixture.session("operator").string(), "--workflow",
                     fixture.workflow.string(), "--once", "hello"},
                    "", &output);
  check(status == 1 &&
            contains(output, (fixture.operator_directory() / "bad.lua").string() +
                                 ": target: `nowhere` names no shipped "
                                 "profile"),
        "operator-invalid: status " + std::to_string(status) + "\n" + output);
  fs::remove_all(fixture.operator_directory());
  status = run(fixture, "operator-absent",
               {"--session", fixture.session("operator").string(), "--workflow",
                fixture.workflow.string(), "--once", "hello"},
               "", &output);
  check(status == 0 && contains(output, "LOCAL"),
        "operator-absent: status " + std::to_string(status) + "\n" + output);
}

// BLACKBIRD_PROFILE_SET_DIGEST pins the loaded set's content digest: any other value,
// the behavioural set digest included, refuses the start before any session file.
void pin_case() {
  Fixture fixture{"pin"};
  const auto set = loaded(directories(fixture));
  check(set.digest != set.content_digest, "the two set digests coincide");
  const auto wrong = std::string{"sha256:"} + std::string(64, '0');
  for (const auto &[label, pin] : std::vector<std::pair<std::string, std::string>>{
           {"wrong", wrong}, {"behavioural", set.digest}, {"empty", ""}}) {
    fixture.extra = {"BLACKBIRD_PROFILES=" + shipped.string(),
                     "BLACKBIRD_PROFILE_SET_DIGEST=" + pin};
    const auto session = fixture.session(label);
    std::string output;
    const auto status = run(fixture, label,
                            {"--session", session.string(), "--workflow",
                             fixture.workflow.string(), "--once", "hello"},
                            "", &output);
    check(status == 1 && contains(output, "Provider profiles refused: ") &&
              !contains(output, "LOCAL"),
          label + ": status " + std::to_string(status) + "\n" + output);
    if (!pin.empty())
      check(contains(output, "the loaded set's content digest " + set.content_digest +
                                 " differs from BLACKBIRD_PROFILE_SET_DIGEST " + pin),
            label + ": refusal must name both digests\n" + output);
    else
      check(contains(output, "BLACKBIRD_PROFILE_SET_DIGEST is set but empty"),
            label + ": empty pin\n" + output);
    check(!fs::exists(session), label + ": created " + session.string());
  }
  fixture.extra = {"BLACKBIRD_PROFILES=" + shipped.string(),
                   "BLACKBIRD_PROFILE_SET_DIGEST=" + set.content_digest};
  std::string output;
  const auto status = run(fixture, "pinned",
                          {"--session", fixture.session("pinned").string(),
                           "--workflow", fixture.workflow.string(), "--once", "hello"},
                          "", &output);
  check(status == 0 && contains(output, "LOCAL"),
        "pinned: status " + std::to_string(status) + "\n" + output);
  // The digest command applies the pin as well, after printing the digests.
  fixture.extra.back() = "BLACKBIRD_PROFILE_SET_DIGEST=" + wrong;
  const auto refused =
      run(fixture, "digests-pinned", {"--profile-digests"}, "", &output);
  check(refused == 1 && SetLines{output}["set-content-digest"] == set.content_digest &&
            contains(output, "differs from BLACKBIRD_PROFILE_SET_DIGEST " + wrong),
        "digests-pinned: status " + std::to_string(refused) + "\n" + output);
}

// --profile-digests prints both set digests and each shipped profile's effective
// digest, exactly as the library computes them over the same directories.
void digests_case() {
  Fixture fixture{"digests"};
  std::string output;
  check(run(fixture, "digests", {"--profile-digests"}, "", &output) == 0,
        "digests: status\n" + output);
  const auto set = loaded(directories(fixture));
  check(SetLines{output}["set-digest"] == set.digest &&
            SetLines{output}["set-content-digest"] == set.content_digest &&
            SetLines{output}["pin"] == set.content_digest &&
            SetLines{output}["set-shipped"] == shipped.string() &&
            SetLines{output}["set-operator"] ==
                fixture.operator_directory().string() + " (0 overrides)",
        "digests: set lines\n" + output);
  auto effective = effective_profiles(set);
  check(effective.has_value() && effective.value().size() == set.shipped.size() + 1,
        "oracle effective profiles");
  for (const auto &profile : effective.value()) {
    std::string layers;
    for (const auto &layer : profile.layers)
      layers += (layers.empty() ? "" : ",") + layer;
    check(contains(output, "\neffective " + profile.id + " revision " +
                               std::to_string(profile.revision) + " digest " +
                               profile.digest + " layers " + layers + "\n"),
          "digests: no line for " + profile.id + "\n" + output);
  }
}

constexpr std::string_view base_override =
    "return {\n  schema = \"blackbird.provider-profile-override/1\",\n"
    "  id = \"local-default\",\n  revision = REVISION,\n  target = \"conservative\",\n"
    "  archival = { mode = \"MODE\" },\n}\n";
std::string conservative_override(std::string_view revision, std::string_view mode) {
  std::string text{base_override};
  text.replace(text.find("REVISION"), 8, revision);
  text.replace(text.find("MODE"), 4, mode);
  return text;
}
void write_overrides(const Fixture &fixture, std::string_view revision,
                     std::string_view mode) {
  write_text(fixture.operator_directory() / "local-default.lua",
             conservative_override(revision, mode));
  write_text(fixture.operator_directory() / "local-openrouter.lua",
             "return {\n  schema = \"blackbird.provider-profile-override/1\",\n"
             "  id = \"local-openrouter\",\n  revision = 2,\n"
             "  target = \"openrouter-responses\",\n"
             "  edit_acceptance = { in_place = \"refused\", basis = "
             "\"probed:operator fixture\" },\n}\n");
}
// The occurrence-th /profile output: from its `profile` line (after a newline or the
// plain prompt) through its `set-operator` line, starting with a newline.
std::string profile_block(std::string_view output, std::size_t occurrence) {
  std::size_t at = 0, from = 0;
  for (std::size_t i = 0; i <= occurrence; ++i) {
    const auto line = output.find("\nprofile ", from);
    const auto prompted = output.find("> profile ", from);
    at = std::min(line, prompted == std::string_view::npos ? prompted : prompted + 1);
    check(at != std::string_view::npos,
          "missing /profile output\n" + std::string{output});
    from = at + 1;
  }
  const auto end = output.find("\nset-operator ", at);
  check(end != std::string_view::npos, "truncated /profile output");
  return "\n" + std::string{output.substr(at + 1, output.find('\n', end + 1) - at)};
}

// /profile shows the operator layers in order, each overridden leaf with its value and
// its layer's basis (or none), and the digests the library computes. An override
// edited while the session runs is unseen until the next session start, which records
// the changed set once; a further unchanged start records nothing.
void layering_case() {
  Fixture fixture{"layering"};
  fixture.openrouter();
  write_overrides(fixture, "3", "refused_until_probed");
  const auto session = fixture.session("s");
  const std::vector<std::string> open = {
      "--plain",    "--session", session.string(),    "--provider",
      "openrouter", "--model",   "openai/gpt-6.1-sol"};
  const auto before = loaded(directories(fixture));
  const auto effective = oracle(before, openrouter_query);
  check(effective.layers == std::vector<std::string>{"shipped:conservative/1",
                                                     "operator:local-default/3",
                                                     "shipped:openrouter-responses/1",
                                                     "operator:local-openrouter/2"},
        "oracle layers");
  std::string first, second;
  {
    Process process{fixture, open, "layering-live"};
    process.send("/profile\n");
    const auto shown_first = process.await_text("\nset-operator ");
    process.await_text("\n", shown_first);
    // The edit lands while the session runs.
    write_overrides(fixture, "4", "allowed");
    process.send("/profile\n");
    process.await_text("\nset-operator ", shown_first);
    // A request after the edit: nothing rereads the files.
    process.send("hello\n");
    process.await_text("PROFILE_FIXTURE_DONE");
    process.send("/quit\n");
    process.close_input();
    check(process.wait("layering-live") == 0,
          "layering-live: status\n" + process.text());
    const auto text = process.text();
    first = profile_block(text, 0);
    second = profile_block(text, 1);
  }
  check(contains(first, "\nprofile openrouter-responses revision 1 digest " +
                            effective.digest + " ") &&
            contains(first, "\nlayers shipped:conservative/1 < "
                            "operator:local-default/3 < shipped:openrouter-responses/1 "
                            "< operator:local-openrouter/2\n") &&
            contains(first,
                     "\n  archival.mode = \"refused_until_probed\" · basis none\n") &&
            contains(first, "\n  edit_acceptance.in_place = \"refused\" · basis "
                            "probed:operator fixture\n") &&
            contains(first, "\nset-digest " + before.digest + "\n") &&
            contains(first, "\nset-content-digest " + before.content_digest + "\n") &&
            contains(first, "\nset-operator " + fixture.operator_directory().string() +
                                " (2 overrides)\n"),
        "layering: first /profile\n" + first);
  check(second == first, "layering: a mid-run edit reached the session\n" + second);
  check(set_records(session).size() == 1, "layering: one set record per start");
  const auto requests = request_profiles(session);
  check(!requests.empty(), "layering: no request after the edit");
  for (const auto &profile : requests)
    check(profile == profile_request_record(effective, {openrouter_query, "", ""}) &&
              contains(shown(profile), "operator:local-default/3"),
          "layering: the request after a mid-run edit recorded " + shown(profile));
  // The next start loads the edited override and records the changed set.
  const auto after = loaded(directories(fixture));
  check(after.content_digest != before.content_digest, "the edit changes the set");
  std::string output;
  check(run(fixture, "layering-reopen", open, "/profile\n/quit\n", &output) == 0,
        "layering-reopen: status\n" + output);
  const auto reopened = profile_block(output, 0);
  check(contains(reopened, "operator:local-default/4") &&
            contains(reopened, "\n  archival.mode = \"allowed\" · basis none\n") &&
            contains(reopened, "\nset-content-digest " + after.content_digest + "\n"),
        "layering: the next start must load the edit\n" + reopened);
  auto recorded = set_records(session);
  check(recorded.size() == 2 &&
            recorded.back() ==
                set_record_with(after, directories(fixture),
                                {{"binding", Value{std::string{openrouter_binding}}}}),
        "layering: the reopen records the changed set once");
  check(run(fixture, "layering-quiet", open, "/quit\n", &output) == 0,
        "layering-quiet: status\n" + output);
  check(set_records(session).size() == 2,
        "layering: an unchanged start must not record the set again");
}

// The set record is a latest-wins program record: S0's clean-stop snapshot carries it
// as written, and a reopen of the unchanged set restores it without writing again.
void record_case() {
  Fixture fixture{"record"};
  const auto session = fixture.session("s");
  std::string output;
  check(run(fixture, "record-once",
            {"--session", session.string(), "--workflow", fixture.workflow.string(),
             "--once", "/context"},
            "", &output) == 0,
        "record-once: status\n" + output);
  // The default route is OpenAI's ChatGPT route (no owned account in this home).
  const auto expected =
      set_record_with(loaded(directories(fixture)), directories(fixture),
                      {{"binding", Value{std::string{chatgpt_binding}}}});
  auto written = set_records(session);
  check(written.size() == 1 && written.front() == expected,
        "record: the journal record differs from the loaded set\n" +
            (written.empty() ? std::string{"none"} : shown(written.front())));
  const auto in_snapshot = [&](std::string_view label) {
    const auto disk = read_disk(session);
    check(disk.saved.has_value(), std::string{label} + ": no saved state");
    const auto end = disk.journal->cursor();
    check(disk.saved->boundary.sequence == end.sequence,
          std::string{label} + ": the snapshot does not end the journal");
    std::vector<Value> found;
    for (const auto &program : disk.saved->programs.array())
      if (const auto *name = program.find("label");
          name && *name == Value{std::string{profile_set_record_label}})
        found.push_back(program);
    check(found.size() == 1 && found.front() == expected,
          std::string{label} + ": the snapshot holds " + std::to_string(found.size()) +
              " set records, or a different one");
  };
  in_snapshot("after the first stop");
  check(run(fixture, "record-reopen",
            {"--session", session.string(), "--once", "/context"}, "", &output) == 0,
        "record-reopen: status\n" + output);
  check(set_records(session).size() == 1, "record: the reopen wrote the set again");
  in_snapshot("after the reopen");
}

// A request against a mock OpenRouter transport records the effective profile, whose
// digest is the one --profile-digests prints; the session's set record carries the
// set digests --profile-digests prints.
void request_case() {
  Fixture fixture{"request"};
  fixture.openrouter();
  const auto session = fixture.session("s");
  std::vector<std::string> arguments{"--session", session.string()};
  arguments.insert(arguments.end(), openrouter_once.begin(), openrouter_once.end());
  std::string output;
  check(run(fixture, "request", arguments, "", &output) == 0 &&
            contains(output, "PROFILE_FIXTURE_DONE"),
        "request: status\n" + output);
  const auto expected = Value::object(
      {{"id", Value{"openrouter-responses"}},
       {"revision", Value{Number{1}}},
       {"digest", Value{std::string{openrouter_effective_digest}}},
       {"layers", Value{Value::Array{Value{"shipped:conservative/1"},
                                     Value{"shipped:openrouter-responses/1"}}}},
       {"model", Value{"openai/gpt-6.1-sol"}},
       {"binding", Value{std::string{openrouter_binding}}}});
  const auto profiles = request_profiles(session);
  check(!profiles.empty(), "request: no provider.request record");
  for (const auto &profile : profiles)
    check(profile == expected, "request: recorded " + shown(profile));
  std::string digests;
  check(run(fixture, "request-digests", {"--profile-digests"}, "", &digests) == 0,
        "request-digests\n" + digests);
  check(contains(digests, "\neffective openrouter-responses revision 1 digest " +
                              std::string{openrouter_effective_digest} + " "),
        "request: the digest command disagrees with the record\n" + digests);
  const auto recorded = set_records(session);
  check(recorded.size() == 1 &&
            string_field(recorded.front(), "digest") == shipped_set_digest &&
            string_field(recorded.front(), "content_digest") ==
                shipped_content_digest &&
            SetLines{digests}["set-digest"] == shipped_set_digest &&
            SetLines{digests}["set-content-digest"] == shipped_content_digest &&
            SetLines{digests}["pin"] == shipped_content_digest &&
            field(recorded.front(), "start_route") ==
                Value::object({{"binding", Value{std::string{openrouter_binding}}}}),
        "request: set record digests\n" + shown(recorded.front()));
}

// The definition-level model selects the profile: /model between turns re-resolves
// from the loaded set; /session and /profile stay available while effort is pending.
void model_case() {
  Fixture fixture{"model"};
  std::string output;
  const auto status =
      run(fixture, "model",
          {"--plain", "--session", fixture.session("s").string(), "--provider",
           "anthropic", "--model", "claude-opus-5-5", "--effort", "high", "--workflow",
           fixture.workflow.string()},
          "/profile\n/model claude-opus-4-5\n/profile\n/quit\n", &output);
  check(status == 0, "model: status\n" + output);
  const auto set = loaded(directories(fixture));
  const auto newer =
      oracle(set, {"anthropic", "messages", "https://api.anthropic.com/v1",
                   "environment", "claude-opus-5-5"});
  const auto older =
      oracle(set, {"anthropic", "messages", "https://api.anthropic.com/v1",
                   "environment", "claude-opus-4-5"});
  check(newer.id == "anthropic-messages-prefix-checked" &&
            older.id == "anthropic-messages",
        "model: oracle selection");
  check(contains(profile_block(output, 0),
                 "\nprofile " + newer.id + " revision 1 digest " + newer.digest) &&
            contains(profile_block(output, 1),
                     "\nprofile " + older.id + " revision 1 digest " + older.digest) &&
            contains(profile_block(output, 1), " model=claude-opus-4-5\n"),
        "model: /model must re-resolve\n" + output);
}

std::vector<fs::path> session_directories(const fs::path &parent) {
  std::vector<fs::path> found;
  for (const auto &entry : fs::directory_iterator{parent})
    if (entry.path().filename().string().starts_with("session-"))
      found.push_back(entry.path());
  return found;
}
// A copy of the shipped set; `twin` adds a second profile matching OpenRouter exactly
// as openrouter-responses does, so selecting for OpenRouter is ambiguous.
fs::path copied_set(const Fixture &fixture, std::string_view name, bool twin) {
  const auto directory = fixture.root / name;
  fs::create_directories(directory);
  for (const auto &entry : fs::directory_iterator{shipped})
    fs::copy_file(entry.path(), directory / entry.path().filename(),
                  fs::copy_options::overwrite_existing);
  if (twin) {
    auto text = contents(shipped / "openrouter-responses.lua");
    const auto id = text.find("id = \"openrouter-responses\"");
    check(id != std::string::npos, "fixture: openrouter id");
    text.replace(id, 27, "id = \"openrouter-twin\"");
    write_text(directory / "openrouter-twin.lua", text);
  }
  return directory;
}
constexpr std::string_view ambiguous_reason =
    "match: `openrouter-responses` and `openrouter-twin` match equally specifically";

// A start whose route the loaded set cannot resolve is refused before mkdtemp and
// RetainedState::create: no session directory, named or unnamed, is left. A /new
// successor refused at load or at resolution removes the directory its predecessor
// made for it.
void resolution_case() {
  Fixture fixture{"resolution"};
  fixture.extra = {"BLACKBIRD_PROFILES=" +
                   copied_set(fixture, "ambiguous", true).string()};
  const std::vector<std::string> route = {"--provider", "openrouter",
                                          "--model",    "openai/gpt-6.1-sol",
                                          "--workflow", fixture.workflow.string(),
                                          "--once",     "hello"};
  for (const bool named : {true, false}) {
    auto arguments = route;
    if (named)
      arguments.insert(arguments.begin(), {"--session", fixture.session("s").string()});
    std::string output;
    const auto status =
        run(fixture, named ? "named" : "unnamed", arguments, "", &output);
    check(status == 1 &&
              contains(output, "Provider profile refused for openrouter model "
                               "openai/gpt-6.1-sol: ") &&
              contains(output, ambiguous_reason) &&
              contains(output, "no session was opened") && !contains(output, "LOCAL"),
          "resolution: status " + std::to_string(status) + "\n" + output);
    check(!fs::exists(fixture.session("s")) &&
              !fs::exists(fixture.home / ".local/state/blackbird"),
          "resolution: a refused start left a session directory");
  }
  // Usage errors come first: no route check or profile refusal before them.
  for (const auto *flag : {"--rebuild-session", "--resume-continue"}) {
    std::string output;
    auto arguments = route;
    arguments.insert(arguments.begin(), flag);
    const auto status =
        run(fixture, std::string{"usage"} + flag, arguments, "", &output);
    check(status == 1 && contains(output, "Select an existing session") &&
              !contains(output, "Provider profile"),
          std::string{"resolution: usage error for "} + flag + "\n" + output);
  }
  // --new-session-instance typed by hand names no directory this process made: a
  // refused start leaves an empty directory, and a symbolic link to one, alone.
  {
    const auto mine = fixture.root / "mine";
    const auto target = fixture.root / "target";
    fs::create_directories(mine);
    fs::create_directories(target);
    fs::create_directory_symlink(target, fixture.root / "link");
    for (const auto &given : {mine, fixture.root / "link"}) {
      auto arguments = route;
      arguments.insert(arguments.begin(),
                       {"--session", given.string(), "--new-session-instance"});
      std::string output;
      const auto status =
          run(fixture, "hand-" + given.filename().string(), arguments, "", &output);
      check(status == 1 && contains(output, ambiguous_reason),
            "resolution: hand-typed instance status\n" + output);
    }
    check(fs::is_directory(mine) && fs::is_symlink(fixture.root / "link") &&
              fs::is_directory(target),
          "resolution: a refused start removed a directory it did not make");
  }
  // /new successors: the predecessor makes the directory, the successor is refused.
  for (const bool at_load : {true, false}) {
    const auto label = std::string{at_load ? "new-load" : "new-resolve"};
    const auto parent = fixture.root / label;
    fs::create_directories(parent);
    const auto profiles = copied_set(fixture, label + "-profiles", false);
    fixture.extra = {"BLACKBIRD_PROFILES=" + profiles.string()};
    Process process{fixture,
                    {"--plain", "--session", (parent / "s").string(), "--provider",
                     "openrouter", "--model", "openai/gpt-6.1-sol", "--workflow",
                     fixture.workflow.string()},
                    label};
    process.send("/profile\n");
    process.await_text("\nset-operator ");
    if (at_load)
      write_text(profiles / "conservative.lua", "return { schema = os }\n");
    else
      fs::copy_file(copied_set(fixture, label + "-twin", true) / "openrouter-twin.lua",
                    profiles / "openrouter-twin.lua");
    process.send("/new\n");
    process.close_input();
    const auto status = process.wait(label);
    const auto output = process.text();
    check(status == 1 && contains(output, at_load ? "Provider profiles refused: "
                                                  : std::string{ambiguous_reason}),
          label + ": status " + std::to_string(status) + "\n" + output);
    check(session_directories(parent).empty(),
          label + ": the refused successor's directory was left behind");
    check(fs::exists(parent / "s/audit"), label + ": the predecessor must remain");
  }
}

// Owner decision 2026-10-20: a registered route whose base URL the profile query
// cannot normalize starts as before, under conservative, with the reason recorded in
// the set record and shown by /profile.
void routes_case() {
  Fixture fixture{"routes"};
  const std::vector<std::pair<std::string, std::string>> routes{
      {"ipv6", "https://[::1]:8443/v1"},
      {"underscore", "https://my_host.example/v1"},
      {"long", "https://paper.example/" + std::string(600, 'p')}};
  for (const auto &[name, url] : routes) {
    std::string output;
    check(run(fixture, name + "-register",
              {"auth", "add-provider", name, url, "responses", "ROUTE_FIXTURE_KEY"}, "",
              &output) == 0,
          name + ": registration\n" + output);
    const auto session = fixture.session(name);
    const auto status =
        run(fixture, name,
            {"--session", session.string(), "--provider", name, "--model", "m",
             "--workflow", fixture.workflow.string(), "--once", "hello"},
            "", &output);
    check(status == 0 && contains(output, "LOCAL"),
          name + ": status " + std::to_string(status) + "\n" + output);
    const auto recorded = set_records(session);
    check(recorded.size() == 1, name + ": set record");
    const auto *start = recorded.front().find("start_route");
    const auto *fallback = start ? start->find("fallback") : nullptr;
    check(fallback && fallback->string().starts_with("binding.base_url: ") &&
              fallback->string().ends_with("; the conservative profile applies"),
          name + ": the set record must carry the fallback reason\n" +
              shown(recorded.front()));
    check(run(fixture, name + "-profile", {"--plain", "--session", session.string()},
              "/profile\n/quit\n", &output) == 0,
          name + ": /profile status\n" + output);
    const auto block = profile_block(output, 0);
    check(contains(block, "\nprofile conservative revision 1 ") &&
              contains(block, "\nlayers shipped:conservative/1\n") &&
              contains(block, "\nfallback " + fallback->string() + "\n"),
          name + ": /profile\n" + block);
  }
}

// Owner decision 2026-10-20: an unreadable auth store stops no session that never
// calls a provider. The start records the deferral; the first provider request is
// refused as an auth-store refusal naming the store.
void auth_case() {
  Fixture fixture{"auth"};
  const auto store = fixture.root / "auth";
  fs::create_directories(store);
  fs::permissions(store, fs::perms::owner_all, fs::perm_options::replace);
  write_text(store / "credentials.bbm", "not a BBM2 packet");
  fs::permissions(store / "credentials.bbm",
                  fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::replace);
  const auto local = fixture.session("local");
  std::string output;
  auto status = run(fixture, "local",
                    {"--session", local.string(), "--workflow",
                     fixture.workflow.string(), "--once", "hello"},
                    "", &output);
  check(status == 0 && contains(output, "LOCAL"),
        "auth: a local workflow must run as before: status " + std::to_string(status) +
            "\n" + output);
  const auto recorded = set_records(local);
  const auto *start = recorded.empty() ? nullptr : recorded.back().find("start_route");
  check(start && start->find("deferral") &&
            contains(start->find("deferral")->string(),
                     "the auth store could not be read at start (corrupt"),
        "auth: the deferral must be recorded");
  fixture.openrouter();
  status = run(fixture, "request",
               {"--session", fixture.session("request").string(), "--provider",
                "openrouter", "--model", "openai/gpt-6.1-sol", "--once", "hello"},
               "", &output);
  check(status == 1 &&
            contains(output, (store / "credentials.bbm").string() +
                                 ": auth store: the provider auth store could not be "
                                 "read (corrupt") &&
            contains(output, "provider_auth") &&
            !contains(output, "PROFILE_FIXTURE_DONE"),
        "auth: the request must be refused naming the store: status " +
            std::to_string(status) + "\n" + output);
}

// Every successor start reloads: after a mid-run edit, /new's successor and a
// restart's resumed start both load and record the edited set.
void successors_case() {
  Fixture fixture{"successors"};
  write_overrides(fixture, "3", "refused_until_probed");
  const auto parent = fixture.root / "new";
  fs::create_directories(parent);
  {
    Process process{fixture,
                    {"--plain", "--session", (parent / "s").string(), "--workflow",
                     fixture.workflow.string()},
                    "new"};
    process.send("/profile\n");
    process.await_text("\nset-operator ");
    write_overrides(fixture, "4", "allowed");
    const auto banner = process.await_text("/help lists commands");
    process.send("/new\n");
    // Plain input already read by the predecessor does not reach its successor.
    process.await_text("/help lists commands", banner);
    process.send("/profile\n/quit\n");
    process.close_input();
    check(process.wait("new") == 0, "successors: /new status\n" + process.text());
    const auto text = process.text();
    check(contains(profile_block(text, 0), "operator:local-default/3") &&
              contains(profile_block(text, 1), "operator:local-default/4"),
          "successors: /new must reload\n" + text);
  }
  const auto fresh = session_directories(parent);
  const auto edited = loaded(directories(fixture));
  check(fresh.size() == 1 && set_records(fresh.front()).size() == 1 &&
            string_field(set_records(fresh.front()).back(), "content_digest") ==
                edited.content_digest,
        "successors: the /new session records the edited set");
  // Restart: the edit lands before /restart; the resumed start records it.
  write_overrides(fixture, "5", "refused_until_probed");
  const auto session = fixture.session("restart");
  {
    Process process{fixture,
                    {"--plain", "--session", session.string(), "--workflow",
                     fixture.workflow.string()},
                    "restart"};
    process.send("/profile\n");
    process.await_text("\nset-operator ");
    write_overrides(fixture, "6", "allowed");
    process.send("/restart PROFILE_RELOAD\n");
    process.close_input();
    check(process.wait("restart") == 75,
          "successors: /restart status\n" + process.text());
  }
  check(set_records(session).size() == 1, "successors: one record before the restart");
  std::string output;
  check(run(fixture, "resumed", {"--session", session.string(), "--resume-once"}, "",
            &output) == 0,
        "successors: resumed status\n" + output);
  const auto after = loaded(directories(fixture));
  const auto recorded = set_records(session);
  check(recorded.size() == 2 &&
            string_field(recorded.back(), "content_digest") == after.content_digest,
        "successors: the restart's resumed start must record the edited set");
}

// An existing session's pre-write check resolves the model its requests will use: an
// activated program-config model, not only the saved launch model. Here only that model
// is ambiguous, and the refused reopen leaves the journal byte for byte as it was.
void config_case() {
  Fixture fixture{"config"};
  fixture.openrouter();
  write_text(
      fixture.workflow,
      "local v=blackbird.call('program_config',{}); "
      "assert(blackbird.call('program_config',{proposal={modules=blackbird.array({}),"
      "model='x/bad',effort=''},base=v.revision}).staged); print('STAGED')\n");
  const auto session = fixture.session("s");
  std::string output;
  check(run(fixture, "stage",
            {"--session", session.string(), "--provider", "openrouter", "--model",
             "openai/gpt-6.1-sol", "--workflow", fixture.workflow.string(), "--once",
             "hello"},
            "", &output) == 0 &&
            contains(output, "STAGED"),
        "config: staging status\n" + output);
  const auto profiles = copied_set(fixture, "x-bad", false);
  for (const auto *id : {"bad-a", "bad-b"})
    write_text(profiles / (std::string{id} + ".lua"),
               "return { schema = \"blackbird.provider-profile/1\", id = \"" +
                   std::string{id} +
                   "\", revision = 1, match = { provider = \"openrouter\", protocol = "
                   "\"responses\", models = { \"x/bad\" } } }\n");
  fixture.extra.push_back("BLACKBIRD_PROFILES=" + profiles.string());
  const auto journal = contents(session / "audit");
  const auto status =
      run(fixture, "reopen", {"--session", session.string(), "--once", "/context"}, "",
          &output);
  check(status == 1 &&
            contains(output, "Provider profile refused for openrouter model x/bad: ") &&
            contains(output, "`bad-a` and `bad-b` match equally specifically"),
        "config: the program-config model must be refused: status " +
            std::to_string(status) + "\n" + output);
  check(contents(session / "audit") == journal,
        "config: the refused reopen wrote to the journal");
}

void run_case(std::string_view name, void (*body)()) {
  try {
    body();
    std::cout << "PASS " << name << '\n';
  } catch (const std::exception &error) {
    ++failures;
    std::cout << "FAIL " << name << ": " << error.what() << '\n';
  } catch (const Error &error) {
    ++failures;
    std::cout << "FAIL " << name << ": " << error_name(error.code) << " ("
              << error.detail << ")\n";
  }
  std::cout.flush();
}

// For scripts/check-installed-profiles: one line per provider.request profile and
// one for the latest set record.
int read_session(const fs::path &session) {
  for (const auto &profile : request_profiles(session)) {
    std::string layers;
    for (const auto &layer : field(profile, "layers").array())
      layers += (layers.empty() ? "" : ",") + layer.string();
    std::cout << "request-profile " << string_field(profile, "id") << " revision "
              << field(profile, "revision").number().text() << " digest "
              << string_field(profile, "digest") << " layers " << layers << '\n';
  }
  const auto recorded = set_records(session);
  if (recorded.empty()) {
    std::cerr << "no " << profile_set_record_label << " record\n";
    return 1;
  }
  const auto &latest = recorded.back();
  std::cout << "set-record digest " << string_field(latest, "digest")
            << " content-digest " << string_field(latest, "content_digest")
            << " shipped " << string_field(latest, "shipped_directory") << '\n';
  return 0;
}
} // namespace

int main(int argc, char **argv) {
  if (argc == 3 && std::string_view{argv[1]} == "read") {
    try {
      return read_session(argv[2]);
    } catch (const std::exception &error) {
      std::cerr << error.what() << '\n';
      return 1;
    } catch (const Error &error) {
      std::cerr << error_name(error.code) << " (" << error.detail << ")\n";
      return 1;
    }
  }
  if (argc < 3) {
    std::cerr << "usage: profile_wiring_test BLACKBIRD PROFILES [CASE...]\n"
                 "       profile_wiring_test read SESSION\n";
    return 2;
  }
  executable = fs::absolute(argv[1]).string();
  shipped = fs::absolute(argv[2]).lexically_normal();
  std::string pattern =
      (fs::temp_directory_path() / "blackbird-profile-wiring-XXXXXX").string();
  check(::mkdtemp(pattern.data()) != nullptr, "mkdtemp failed");
  workspace = fs::canonical(pattern);
  const std::array<std::pair<std::string_view, void (*)()>, 12> cases{{
      {"refusal", refusal_case},
      {"pin", pin_case},
      {"digests", digests_case},
      {"layering", layering_case},
      {"record", record_case},
      {"request", request_case},
      {"model", model_case},
      {"resolution", resolution_case},
      {"routes", routes_case},
      {"auth", auth_case},
      {"successors", successors_case},
      {"config", config_case},
  }};
  for (const auto &[name, body] : cases) {
    bool selected = argc == 3;
    for (int i = 3; i < argc; ++i)
      selected = selected || name == argv[i];
    if (selected)
      run_case(name, body);
  }
  std::error_code ignored;
  if (failures == 0)
    fs::remove_all(workspace, ignored);
  else
    std::cout << "Fixtures kept in " << workspace.string() << '\n';
  return failures == 0 ? 0 : 1;
}
