#include "blackbird/coding.hpp"
#include "blackbird/raw_reader.hpp"
namespace {
unsigned fixture_provider_calls = 0;
class FixtureMainProvider final : public blackbird::CodingProvider {
public:
  blackbird::Value respond(const blackbird::Value &,
                           const std::function<void(std::string_view)> &) override {
    ++fixture_provider_calls;
    throw blackbird::Error{blackbird::ErrorCode::provider_transport};
  }
};
} // namespace
#define main embedded_blackbird_main
#define OpenAiCodingProvider FixtureMainProvider
#include "../src/main.cpp"
#undef OpenAiCodingProvider
#undef main
#include <cstdarg>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
namespace {
void require_native(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error{message};
}
using OpenFunction = int (*)(const char *, int, ...);
using ReadFunction = ssize_t (*)(int, void *, std::size_t);
using CloseFunction = int (*)(int);
using PollFunction = int (*)(pollfd *, nfds_t, int);
using SendFunction = ssize_t (*)(int, const void *, std::size_t, int);
OpenFunction native_open = nullptr;
ReadFunction native_read = nullptr;
CloseFunction native_close = nullptr;
PollFunction native_poll = nullptr;
SendFunction native_send = nullptr;
std::string workflow_operand;
unsigned workflow_opens = 0, reads_after_cut = 0;
int cut_fd = -1;
bool select_cut = false, cut_hit = false;
bool select_interrupt = false, interrupt_hit = false;
bool select_prefix_cut = false, prefix_cut_hit = false;
thread_local bool refuse_next_allocation = false;
bool select_branch_cut = false, branch_cut_hit = false;
std::optional<RawChannel> notification_channel;
std::filesystem::path branch_session;
std::exception_ptr branch_observer_failure;
std::uint64_t observed_control_end = 0;
std::atomic_bool native_entered{false}, native_returned{false};
int interrupt_read_fd = -2;
bool interrupt_poll = false;
ssize_t observed_read = 0;
int observed_poll = 0;
short observed_revents = 0;
std::vector<RawControlToken> grants;
RawChannel main_channel() {
  constexpr char digits[] = "0123456789abcdef";
  const auto lane = raw_lane_epoch();
  std::string name = "/bb";
  for (std::size_t index = 0; index < 12; ++index) {
    const auto byte = std::to_integer<unsigned char>(lane[index]);
    name += digits[byte >> 4U];
    name += digits[byte & 15U];
  }
  return unwrap(
      RawChannel::open(name, lane, {256 * 1024 * 1024, 1024 * 1024, 128 * 1024}));
}
void fill_control(std::uint64_t leave = 0) {
  auto channel = main_channel();
  for (;;) {
    const auto cursor = unwrap(channel.cursors(RawPartition::control));
    if (cursor.published == cursor.acknowledged)
      break;
    std::this_thread::yield();
  }
  grants.reserve(96);
  std::optional<RawControlToken> spare;
  if (leave)
    spare.emplace(unwrap(raw_try_claim_control(leave)));
  for (const std::uint64_t size : {65536ULL, 4096ULL, 256ULL, 16ULL, 1ULL}) {
    for (;;) {
      auto result = raw_try_claim_control(size);
      if (!result.has_value()) {
        require_native(result.error() == Error{ErrorCode::capacity},
                       "native fill error");
        break;
      }
      grants.push_back(std::move(result).value());
    }
  }
}
struct Temporary {
  std::filesystem::path path;
  Temporary() {
    char pattern[] = "/tmp/bb-main-cut-XXXXXX";
    const auto directory = ::mkdtemp(pattern);
    require_native(directory, "native private main directory");
    path = directory;
  }
  ~Temporary() {
    if (std::uncaught_exceptions())
      std::cerr << "preserved Main fixture " << path << '\n';
    else
      std::filesystem::remove_all(path);
  }
};
int invoke(std::vector<std::string> words) {
  std::vector<char *> args;
  for (auto &word : words)
    args.push_back(word.data());
  args.push_back(nullptr);
  std::vector<std::string> next, fallback;
  return run_session(static_cast<int>(words.size()), args.data(),
                     {.next = next, .fallback = fallback});
}
void main_outcomes(const std::filesystem::path &session, ErrorCode expected,
                   bool reached_branch = true) {
  const auto runs = unwrap(discover_raw_runs(session.native()));
  require_native(runs.size() == 1, "actual private Main raw run");
  const auto &snapshot = runs.front().snapshot;
  require_native(snapshot.has_value() && snapshot->closed,
                 "actual Main clean raw close");
  auto reader =
      unwrap(RawRunReader::open((session / "raw" / runs.front().name).native()));
  std::vector<std::byte> scratch(2 * 1024 * 1024);
  unsigned input = 0, dispatch = 0, original = 0, branch = 0;
  while (const auto record = unwrap(reader->read(scratch))) {
    std::uint32_t site = 0;
    std::memcpy(&site, record->raw.data() + 4, sizeof(site));
    require_native(site != 8000 && site != 8001, "unexpected Main recording failure");
    const auto body = record->raw.subspan(16);
    if (site == 4201 || site == 4204) {
      require_native(body.size() == 36 && body[24] == std::byte{3} &&
                         body[25] == std::byte{0} && body[26] == std::byte{1} &&
                         body[27] == static_cast<std::byte>(expected),
                     "actual Main failed input/dispatch Error variant");
      std::int64_t detail = -1;
      std::memcpy(&detail, body.data() + 28, sizeof(detail));
      require_native(detail == 0, "actual Main native Error detail");
      if (site == 4201)
        ++input;
      else
        ++dispatch;
    }
    if (site == 4202) {
      std::size_t length = 0;
      std::memcpy(&length, body.data() + 24, sizeof(length));
      require_native(length == 3 && body.size() == 35 &&
                         std::memcmp(body.data() + 32, "run", 3) == 0,
                     "acquired original survives native Main refusal");
      ++original;
    }
    if (site == 4205) {
      require_native(body.size() == 25 && body[24] == std::byte{40},
                     "actual Main fallback branch");
      ++branch;
    }
  }
  require_native(input == 1 && dispatch == 1 && original == 1 &&
                     branch == (reached_branch ? 1U : 0U),
                 "actual Main input/dispatch exactly once");
}
template <class T> T take_native(ByteView &body) {
  require_native(body.size() >= sizeof(T), "short Main native field");
  T result{};
  std::memcpy(&result, body.data(), sizeof(result));
  body = body.subspan(sizeof(result));
  return result;
}
RawEventRef take_reference(ByteView &body) {
  return {take_native<IdentityBytes>(body), take_native<std::uint64_t>(body)};
}
bool same_reference(const RawEventRef &left, const RawEventRef &right) {
  return left.lane == right.lane && left.ordinal == right.ordinal;
}
struct NativeRow {
  RawEventRef reference;
  std::uint32_t site;
  std::vector<std::byte> body;
};
std::vector<NativeRow> native_rows(RawChannel &channel, IdentityBytes lane) {
  std::vector<NativeRow> result;
  for (auto partition :
       {RawPartition::data, RawPartition::control, RawPartition::failure}) {
    const auto output = unwrap(channel.read(
        partition, unwrap(channel.cursors(partition)).acknowledged, 65536));
    ByteView bytes{output.bytes};
    while (!bytes.empty()) {
      const auto size = take_native<std::uint32_t>(bytes);
      const auto site = take_native<std::uint32_t>(bytes);
      const auto ordinal = take_native<std::uint64_t>(bytes);
      require_native(size >= 16 && size - 16 <= bytes.size(), "Main native envelope");
      require_native(site != 8000 && site != 8001, "Main native capture failure");
      auto body = bytes.first(size - 16);
      result.push_back({{lane, ordinal}, site, {body.begin(), body.end()}});
      bytes = bytes.subspan(size - 16);
    }
  }
  return result;
}
void no_restart_signal(int) {}
void native_fields() {
  int fds[2];
  require_native(::pipe(fds) == 0, "native field pipe");
  constexpr char literal[] = {'a', '\0', static_cast<char>(0xff), '\n'};
  require_native(::write(fds[1], literal, sizeof(literal)) == sizeof(literal),
                 "native field source");
  IdentityBytes lane{};
  lane[0] = std::byte{98};
  const auto name = "/bb-mfields-" + std::to_string(::getpid());
  auto channel = unwrap(RawChannel::create(name, lane, {48 + 512, 4096, 4096}));
  RawProducer producer{channel};
  RawEventRef parent;
  std::vector<NativeRow> positive;
  {
    RawThreadBinding binding{producer, lane};
    parent = {lane, unwrap(raw_emit(9200, {}, RawPartition::control))};
    const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
    char bytes[512];
    errno = ENOTTY;
    require_native(raw_main_read(&context, fds[0], bytes, sizeof(bytes)) == 4 &&
                       errno == ENOTTY && std::memcmp(bytes, literal, 4) == 0,
                   "exact560 grant native acquisition/errno");
    for (auto &row : native_rows(channel, lane))
      if (row.site == 4231)
        positive.push_back(std::move(row));
    unwrap(channel.acknowledge_quiescent(
        RawPartition::data, unwrap(channel.cursors(RawPartition::data)).published));
    require_native(raw_main_read(&context, -1, bytes, sizeof(bytes)) == -1 &&
                       errno == EBADF,
                   "real native failed read");
    // Interrupted syscalls are real: another thread delivers SIGUSR1 until the
    // blocking native call returns. No timer or fabricated EINTR return.
    struct sigaction action{}, previous{};
    action.sa_handler = no_restart_signal;
    sigemptyset(&action.sa_mask);
    require_native(::sigaction(SIGUSR1, &action, &previous) == 0,
                   "native signal setup");
    const auto target = ::pthread_self();
    for (const bool polling : {false, true}) {
      native_entered.store(false);
      native_returned.store(false);
      interrupt_read_fd = polling ? -2 : fds[0];
      interrupt_poll = polling;
      std::thread sender{[target] {
        while (!native_entered.load(std::memory_order_acquire))
          std::this_thread::yield();
        while (!native_returned.load(std::memory_order_acquire)) {
          (void)::pthread_kill(target, SIGUSR1);
          std::this_thread::yield();
        }
      }};
      int returned;
      if (polling) {
        pollfd input{fds[0], POLLIN, 0};
        returned = raw_main_poll(&context, input, 40);
      } else
        returned =
            static_cast<int>(raw_main_read(&context, fds[0], bytes, sizeof(bytes)));
      const auto error = errno;
      sender.join();
      interrupt_read_fd = -2;
      interrupt_poll = false;
      require_native(returned == -1 && error == EINTR, "real native syscall EINTR");
    }
    require_native(::sigaction(SIGUSR1, &previous, nullptr) == 0,
                   "native signal restore");
    ::close(fds[1]);
    require_native(raw_main_read(&context, fds[0], bytes, sizeof(bytes)) == 0,
                   "real native read EOF");
  }
  auto rows = native_rows(channel, lane);
  for (auto &row : positive)
    rows.push_back(std::move(row));
  unsigned requests = 0, returns = 0, polls = 0;
  for (const auto &row : rows) {
    ByteView body{row.body};
    if (row.site == 4230) {
      require_native(same_reference(take_reference(body), parent),
                     "literal read parent");
      const auto fd = take_native<int>(body);
      require_native(fd == fds[0] || fd == -1, "literal read descriptor");
      require_native(take_native<std::size_t>(body) == 512 && body.empty(),
                     "literal requested extent");
      ++requests;
    } else if (row.site == 4231) {
      const auto request = take_reference(body);
      const auto count = take_native<ssize_t>(body);
      const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto &entry) {
        return same_reference(entry.reference, request) && entry.site == 4230;
      });
      require_native(found != rows.end() && request.ordinal < row.reference.ordinal,
                     "actual read return graph/order");
      if (count > 0)
        require_native(count == 4 && body.size() == 4 &&
                           std::memcmp(body.data(), literal, 4) == 0,
                       "literal native positive return");
      else if (count < 0) {
        const auto error = take_native<int>(body);
        require_native(count == -1 && (error == EBADF || error == EINTR) &&
                           body.empty(),
                       "literal native failed return");
      } else
        require_native(body.empty(), "literal EOF return");
      ++returns;
    } else if (row.site == 4232) {
      require_native(same_reference(take_reference(body), parent) &&
                         take_native<int>(body) == fds[0] &&
                         take_native<short>(body) == POLLIN &&
                         take_native<int>(body) == 40 && body.empty(),
                     "literal poll request");
    } else if (row.site == 4233) {
      const auto request = take_reference(body);
      require_native(take_native<int>(body) == observed_poll && observed_poll == -1 &&
                         take_native<int>(body) == EINTR &&
                         take_native<short>(body) == observed_revents && body.empty(),
                     "independently observed actual poll return");
      require_native(request.ordinal < row.reference.ordinal, "native poll order");
      ++polls;
    }
  }
  require_native(requests == 4 && returns == 4 && polls == 1 && observed_read == -1,
                 "finite native read/poll reached set");
  ::close(fds[0]);
  unwrap(RawChannel::unlink(name));
}
void main_refusal() {
  Temporary temporary;
  const auto workflow = temporary.path / "workflow.lua";
  const auto mission = temporary.path / "mission.json";
  write_file(workflow, "error('ordinary fixture business failure')\n");
  write_file(mission, "{\"mission\":\"private owner failure fixture\"}");
  for (const bool selected : {true, false}) {
    const auto session = temporary.path / (selected ? "raw-refusal" : "business-error");
    workflow_operand = workflow.native();
    workflow_opens = reads_after_cut = 0;
    cut_hit = false;
    fixture_provider_calls = 0;
    select_cut = selected;
    const auto status =
        invoke({"fixture-blackbird", "--session", session.native(), "--workflow",
                workflow.native(), "--once", "run", "--backstop", mission.native()});
    select_cut = false;
    workflow_operand.clear();
    require_native(status == 1, "Main expected actual failure");
    if (selected) {
      if (!cut_hit || reads_after_cut || !grants.empty() || fixture_provider_calls ||
          std::filesystem::exists(session / "backstop"))
        std::cerr << "Main cut diagnostic opens=" << workflow_opens
                  << " hit=" << cut_hit << " reads=" << reads_after_cut
                  << " grants=" << grants.size()
                  << " provider=" << fixture_provider_calls << '\n';
      require_native(cut_hit && reads_after_cut == 0 && grants.empty() &&
                         fixture_provider_calls == 0 &&
                         !std::filesystem::exists(session / "backstop"),
                     "raw refusal was recovered as business failure/backstop");
    } else
      require_native(!cut_hit && fixture_provider_calls > 0 &&
                         std::filesystem::exists(session / "backstop"),
                     "ordinary eligible business error lost actual backstop selection");
    main_outcomes(session,
                  selected ? ErrorCode::capacity : ErrorCode::external_unknown);
  }
}

void main_handoff_selection() {
  Temporary temporary;
  const auto destination = temporary.path / "destination";
  require_native(invoke({"fixture-blackbird", "--session", destination.native(),
                         "--once", "/name target"}) == 0,
                 "actual destination session bootstrap");
  const auto missing = temporary.path / "missing-audit";
  std::filesystem::create_directory(missing);
  for (const std::string scenario : {"new", "resume", "missing"}) {
    const auto session = temporary.path / scenario;
    const auto command =
        scenario == "new"
            ? std::string{"/new\n"}
            : "/resume " + (scenario == "missing" ? missing : destination).native() +
                  "\n/quit\n";
    int pipes[2];
    require_native(::pipe(pipes) == 0, "actual handoff stdin");
    require_native(::write(pipes[1], command.data(), command.size()) ==
                       static_cast<ssize_t>(command.size()),
                   "actual handoff command bytes");
    ::close(pipes[1]);
    const auto old_input = ::dup(STDIN_FILENO);
    require_native(old_input >= 0 && ::dup2(pipes[0], STDIN_FILENO) == STDIN_FILENO,
                   "actual handoff input replacement");
    ::close(pipes[0]);
    std::vector<std::string> words{"fixture-blackbird", "--session", session.native(),
                                   "--plain"},
        next, fallback;
    std::vector<char *> args;
    for (auto &word : words)
      args.push_back(word.data());
    args.push_back(nullptr);
    RawSuccessorHandoff handoff;
    const auto status = run_session(static_cast<int>(words.size()), args.data(),
                                    {.next = next, .fallback = fallback}, &handoff);
    require_native(::dup2(old_input, STDIN_FILENO) == STDIN_FILENO,
                   "actual handoff input restore");
    ::close(old_input);
    require_native(status == 0,
                   "new/resume must use original in-process reopen status");
    if (scenario == "missing")
      require_native(next.empty() && fallback.empty(),
                     "missing audit changed successor arguments");
    else {
      require_native(next.size() == (scenario == "new" ? 11U : 5U) &&
                         next[0] == "fixture-blackbird" && next[1] == "--session" &&
                         std::filesystem::is_directory(next[2]) &&
                         std::find(next.begin(), next.end(), "--plain") != next.end() &&
                         std::find(next.begin(), next.end(),
                                   "--new-session-instance") != next.end(),
                     "actual selected successor argv");
      if (scenario == "resume")
        require_native(next[2] == std::filesystem::canonical(destination).native() &&
                           fallback.size() == 5 && fallback[2] == session.native(),
                       "actual resume selected/fallback operands");
    }
    handoff.consume();
    const auto runs = unwrap(discover_raw_runs(session.native()));
    require_native(runs.size() == 1 && runs[0].snapshot && runs[0].snapshot->closed,
                   "actual handoff raw close");
    auto reader = unwrap(RawRunReader::open((session / "raw" / runs[0].name).native()));
    std::vector<std::byte> scratch(2 * 1024 * 1024);
    unsigned selected = 0, consumed = 0, missing_error = 0, argv_rows = 0;
    while (const auto row = unwrap(reader->read(scratch))) {
      ByteView bytes{row->raw};
      (void)take_native<std::uint32_t>(bytes);
      const auto site = take_native<std::uint32_t>(bytes);
      (void)take_native<std::uint64_t>(bytes);
      require_native(site != 4480 && site != 4481,
                     "in-process reopen attempted native exec");
      if (site == 4467) {
        (void)take_reference(bytes);
        require_native(take_native<std::size_t>(bytes) == next.size() &&
                           !take_native<bool>(bytes) && take_native<bool>(bytes) &&
                           bytes.empty(),
                       "literal actual in-process selection");
        ++selected;
      }
      if (site == 4466) {
        (void)take_reference(bytes);
        const auto index = take_native<std::size_t>(bytes),
                   extent = take_native<std::size_t>(bytes);
        const auto &expected = argv_rows < next.size() ? next : fallback;
        require_native(
            index < expected.size() && extent == expected[index].size() + 1 &&
                bytes.size() == extent &&
                std::memcmp(bytes.data(), expected[index].c_str(), extent) == 0,
            "literal actual selected/fallback NUL operand");
        ++argv_rows;
      }
      if (site == 4482) {
        (void)take_reference(bytes);
        require_native(take_native<std::uint8_t>(bytes) == 1 && bytes.empty(),
                       "actual in-process handoff consumed");
        ++consumed;
      }
      if (site == 4461) {
        (void)take_reference(bytes);
        const auto branch = take_native<RawOperationBranch>(bytes);
        (void)take_native<std::uint8_t>(bytes);
        const bool error = take_native<bool>(bytes);
        if (error) {
          require_native(branch == RawOperationBranch::error &&
                             take_native<ErrorCode>(bytes) ==
                                 ErrorCode::invalid_range &&
                             take_native<std::int64_t>(bytes) == 0 && bytes.empty(),
                         "actual missing audit invalid_range");
          ++missing_error;
        }
      }
    }
    const auto expected = scenario == "missing" ? 0U : 1U;
    require_native(selected == expected && consumed == expected &&
                       argv_rows == next.size() + fallback.size() &&
                       missing_error == (scenario == "missing" ? 1U : 0U),
                   "native selected/refused handoff census");
  }
}
void native_refusal() {
  int fds[2];
  require_native(::pipe(fds) == 0, "native input pipe");
  constexpr char literal[] = {'a', '\0', static_cast<char>(0xff), '\n'};
  require_native(::write(fds[1], literal, sizeof(literal)) == sizeof(literal),
                 "pipe source");
  const auto name = "/bb-mcut-" + std::to_string(::getpid());
  IdentityBytes lane{};
  lane[0] = std::byte{97};
  auto channel = unwrap(RawChannel::create(name, lane, {48 + 512 - 1, 1024, 4096}));
  RawProducer producer{channel};
  bool rejected = false;
  {
    RawThreadBinding binding{producer, lane};
    const auto parent = unwrap(raw_emit(9200, {}, RawPartition::control));
    const RawAdapterContext context{nullptr, RawEventRef{lane, parent}, nullptr,
                                    nullptr};
    char bytes[512];
    try {
      (void)raw_main_read(&context, fds[0], bytes, sizeof(bytes));
    } catch (const Error &error) {
      rejected = error == Error{ErrorCode::capacity};
    }
  }
  char retained[4];
  require_native(rejected && native_read(fds[0], retained, sizeof(retained)) == 4 &&
                     std::memcmp(retained, literal, sizeof(literal)) == 0,
                 "grant-minus-one native input consumed bytes");
  ::close(fds[0]);
  ::close(fds[1]);
  unwrap(RawChannel::unlink(name));
}
void main_idle_interrupt() {
  Temporary temporary;
  int fds[2];
  require_native(::pipe(fds) == 0, "idle signal input pipe");
  constexpr std::string_view source = "/name after interrupt\n/quit\n";
  require_native(::write(fds[1], source.data(), source.size()) ==
                     static_cast<ssize_t>(source.size()),
                 "actual next input after signal");
  ::close(fds[1]);
  const auto old_input = ::dup(STDIN_FILENO);
  require_native(old_input >= 0 && ::dup2(fds[0], STDIN_FILENO) == STDIN_FILENO,
                 "fixture input replacement");
  ::close(fds[0]);
  interrupt_hit = false;
  select_interrupt = true;
  const auto session = temporary.path / "idle-signal";
  const auto status =
      invoke({"fixture-blackbird", "--session", session.native(), "--plain"});
  select_interrupt = false;
  require_native(::dup2(old_input, STDIN_FILENO) == STDIN_FILENO,
                 "fixture input restoration");
  ::close(old_input);
  require_native(interrupt_hit && status == 0, "plain signal prevented next command");
  const auto runs = unwrap(discover_raw_runs(session.native()));
  require_native(runs.size() == 1 && runs.front().snapshot &&
                     runs.front().snapshot->closed,
                 "idle signal raw closure");
  auto reader =
      unwrap(RawRunReader::open((session / "raw" / runs.front().name).native()));
  std::vector<std::byte> scratch(2 * 1024 * 1024);
  std::optional<RawEventRef> notified_incarnation, replacement;
  bool read_after_rearm = false, original = false;
  std::vector<NativeRow> signals;
  while (const auto row = unwrap(reader->read(scratch))) {
    ByteView bytes{row->raw};
    (void)take_native<std::uint32_t>(bytes);
    const auto site = take_native<std::uint32_t>(bytes);
    const RawEventRef own{row->origin.lane, take_native<std::uint64_t>(bytes)};
    require_native(site != 8000 && site != 8001, "idle signal ingest failure");
    if (site == 2056)
      notified_incarnation = take_reference(bytes);
    if (site == 2050 || site == 2060 || site == 2061 || site == 4230)
      signals.push_back({own, site, {bytes.begin(), bytes.end()}});
    if (site == 4202) {
      (void)take_reference(bytes);
      const auto size = take_native<std::size_t>(bytes);
      original = size == 21 && bytes.size() == 21 &&
                 std::memcmp(bytes.data(), "/name after interrupt", 21) == 0;
    }
  }
  std::optional<RawEventRef> reset;
  for (const auto &row : signals) {
    ByteView bytes{row.body};
    if (row.site == 2050 && notified_incarnation &&
        same_reference(take_reference(bytes), *notified_incarnation))
      replacement = row.reference;
    if (row.site == 2060 && notified_incarnation &&
        same_reference(take_reference(bytes), *notified_incarnation) &&
        take_native<std::uint8_t>(bytes) == 1)
      reset = row.reference;
  }
  bool reset_returned = false;
  for (const auto &row : signals) {
    ByteView bytes{row.body};
    if (row.site == 2061 && reset && same_reference(take_reference(bytes), *reset))
      reset_returned = take_native<std::uint8_t>(bytes) == 1;
    if (row.site == 4230 && reset)
      read_after_rearm =
          reset->lane == row.reference.lane && reset->ordinal < row.reference.ordinal;
  }
  require_native(notified_incarnation && replacement && reset && reset_returned &&
                     read_after_rearm && original,
                 "real signal incarnation/source/rearm missing");
}
void main_acquired_prefix() {
  Temporary temporary;
  int fds[2];
  require_native(::pipe(fds) == 0, "prefix input pipe");
  const std::string source(512, 'x');
  require_native(::write(fds[1], source.data(), source.size()) == 512,
                 "actual acquired prefix source");
  ::close(fds[1]);
  const auto old_input = ::dup(STDIN_FILENO);
  require_native(old_input >= 0 && ::dup2(fds[0], STDIN_FILENO) == STDIN_FILENO,
                 "prefix input replacement");
  ::close(fds[0]);
  prefix_cut_hit = false;
  select_prefix_cut = true;
  const auto session = temporary.path / "prefix-allocation";
  const auto status =
      invoke({"fixture-blackbird", "--session", session.native(), "--plain"});
  select_prefix_cut = false;
  require_native(::dup2(old_input, STDIN_FILENO) == STDIN_FILENO,
                 "prefix input restoration");
  ::close(old_input);
  require_native(status == 1 && prefix_cut_hit && !refuse_next_allocation,
                 "actual pending prefix allocation cut absent");
  const auto runs = unwrap(discover_raw_runs(session.native()));
  require_native(runs.size() == 1 && runs.front().snapshot &&
                     runs.front().snapshot->closed,
                 "prefix failure raw close");
  auto reader =
      unwrap(RawRunReader::open((session / "raw" / runs.front().name).native()));
  std::vector<std::byte> scratch(2 * 1024 * 1024);
  unsigned captured = 0;
  while (const auto row = unwrap(reader->read(scratch))) {
    ByteView bytes{row->raw};
    (void)take_native<std::uint32_t>(bytes);
    const auto site = take_native<std::uint32_t>(bytes);
    (void)take_native<std::uint64_t>(bytes);
    require_native(site != 4200 && site != 4202 && site != 4203 && site != 4205 &&
                       site != 8000 && site != 8001,
                   "prefix failure dispatched/corrupted capture");
    if (site == 4231) {
      (void)take_reference(bytes);
      require_native(take_native<ssize_t>(bytes) == 512 && bytes.size() == 512 &&
                         std::memcmp(bytes.data(), source.data(), 512) == 0,
                     "acquired prefix lost before allocation failure");
      ++captured;
    }
  }
  require_native(captured == 1, "exactly one actual acquired prefix");
}
void main_branch_refusal() {
  Temporary temporary;
  const auto workflow = temporary.path / "branch.lua";
  write_file(workflow, "error('unreached branch effect')\n");
  workflow_operand = workflow.native();
  workflow_opens = 0;
  branch_cut_hit = false;
  select_branch_cut = true;
  const auto session = temporary.path / "branch-refusal";
  branch_session = session;
  branch_observer_failure = {};
  observed_control_end = 0;
  const auto status = invoke({"fixture-blackbird", "--session", session.native(),
                              "--workflow", workflow.native(), "--once", "run"});
  select_branch_cut = false;
  notification_channel.reset();
  workflow_operand.clear();
  branch_session.clear();
  if (branch_observer_failure)
    std::rethrow_exception(branch_observer_failure);
  require_native(status == 1 && branch_cut_hit && workflow_opens == 1 && grants.empty(),
                 "branch grant-minus-one reached workflow condition");
  main_outcomes(session, ErrorCode::capacity, false);
}
std::uint32_t branch_control_site(RawCursors cursor) {
  if (!notification_channel)
    throw std::runtime_error("branch observer channel absent");
  const auto published = cursor.published;
  while (cursor.acknowledged != published) {
    auto output = notification_channel->read(RawPartition::control, cursor.acknowledged,
                                             1024 * 1024);
    if (output.has_value()) {
      ByteView records{output.value().bytes};
      std::uint32_t last = 0;
      while (!records.empty()) {
        const auto length = take_native<std::uint32_t>(records);
        last = take_native<std::uint32_t>(records);
        (void)take_native<std::uint64_t>(records);
        require_native(length >= 16 && length - 16 <= records.size(),
                       "complete branch observer native envelope");
        records = records.subspan(length - 16);
      }
      require_native(output.value().end == published && last != 0,
                     "branch observer reaches exact parked publication");
      return last;
    }
    const auto next = unwrap(notification_channel->cursors(RawPartition::control));
    if (output.error() != Error{ErrorCode::invalid_range} ||
        next.acknowledged <= cursor.acknowledged || next.published != published)
      throw output.error();
    cursor = next;
  }
  // ACK follows physical publication. The producer remains parked, so a disk
  // original ending at captured P is the same record even after ACK retired it.
  const auto runs = unwrap(discover_raw_runs(branch_session.native()));
  require_native(runs.size() == 1, "one actual branch observer raw run");
  auto reader =
      unwrap(RawRunReader::open((branch_session / "raw" / runs.front().name).native()));
  std::vector<std::byte> buffer(2 * 1024 * 1024);
  const auto lane = notification_channel->epoch();
  while (const auto row = unwrap(reader->read(buffer))) {
    if (row->origin.lane == lane && row->origin.partition == RawPartition::control &&
        row->origin.source_start + row->raw.size() == published) {
      ByteView body = row->raw;
      (void)take_native<std::uint32_t>(body);
      return take_native<std::uint32_t>(body);
    }
  }
  throw std::runtime_error("parked branch control original missing from disk");
}

} // namespace
extern "C" int open(const char *path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = static_cast<mode_t>(va_arg(args, int));
    va_end(args);
  }
  const int result =
      flags & O_CREAT ? native_open(path, flags, mode) : native_open(path, flags);
  const int error = errno;
  if (!workflow_operand.empty() && workflow_operand == path) {
    ++workflow_opens;
    if (select_branch_cut && workflow_opens == 1 && result >= 0) {
      notification_channel.emplace(main_channel());
      unwrap(notification_channel->arm_data());
    }
    if (select_cut && workflow_opens == 2 && result >= 0) {
      cut_fd = result;
      cut_hit = true;
      fill_control();
    }
  }
  errno = error;
  return result;
}
extern "C" ssize_t read(int fd, void *bytes, std::size_t size) {
  if (fd == cut_fd)
    ++reads_after_cut;
  const bool interrupting = fd == interrupt_read_fd;
  if (interrupting)
    native_entered.store(true, std::memory_order_release);
  const auto result = native_read(fd, bytes, size);
  const auto error = errno;
  if (interrupting) {
    observed_read = result;
    native_returned.store(true, std::memory_order_release);
  }
  if (select_prefix_cut && fd == STDIN_FILENO && result > 0) {
    select_prefix_cut = false;
    prefix_cut_hit = true;
    refuse_next_allocation = true;
  }
  errno = error;
  return result;
}
extern "C" int poll(pollfd *fds, nfds_t count, int timeout) {
  if (select_interrupt && count == 1 && fds[0].fd == STDIN_FILENO) {
    select_interrupt = false;
    interrupt_hit = true;
    (void)::raise(SIGINT);
  }
  const bool interrupting = interrupt_poll && count == 1;
  if (interrupting)
    native_entered.store(true, std::memory_order_release);
  const auto result = native_poll(fds, count, timeout);
  const auto error = errno;
  if (interrupting) {
    observed_poll = result;
    observed_revents = fds[0].revents;
    native_returned.store(true, std::memory_order_release);
  }
  errno = error;
  return result;
}
extern "C" ssize_t send(int fd, const void *bytes, std::size_t size, int flags) {
  // Forward exactly once before observation; a disk fallback can then rely on
  // actual recorder progress without holding its wake notification hostage.
  const auto result = native_send(fd, bytes, size, flags);
  const int error = errno;
  const bool producer_notice = size == 1 &&
                               *static_cast<const unsigned char *>(bytes) == 1 &&
                               flags == (MSG_DONTWAIT | MSG_NOSIGNAL);
  if (notification_channel && producer_notice &&
      raw_lane_epoch() == notification_channel->epoch() && !branch_observer_failure) {
    try {
      const auto cursor = unwrap(notification_channel->cursors(RawPartition::control));
      if (cursor.published != observed_control_end) {
        const auto last = branch_control_site(cursor);
        observed_control_end = cursor.published;
        if (!branch_cut_hit && last == 4203) {
          branch_cut_hit = true;
          fill_control(40);
        } else if (branch_cut_hit && last == 4204)
          grants.clear();
      }
      unwrap(notification_channel->arm_data());
    } catch (...) {
      branch_observer_failure = std::current_exception();
      grants.clear();
    }
  }
  errno = error;
  return result;
}
extern "C" int close(int fd) {
  const auto result = native_close(fd);
  const int error = errno;
  if (fd == cut_fd) {
    cut_fd = -1;
    grants.clear();
  }
  errno = error;
  return result;
}
void *operator new(std::size_t size) {
  if (refuse_next_allocation) {
    refuse_next_allocation = false;
    throw std::bad_alloc{};
  }
  if (void *value = std::malloc(size ? size : 1))
    return value;
  throw std::bad_alloc{};
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *value) noexcept { std::free(value); }
void operator delete[](void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }
void operator delete[](void *value, std::size_t) noexcept { std::free(value); }
int main() {
  native_open = reinterpret_cast<OpenFunction>(::dlsym(RTLD_NEXT, "open"));
  native_read = reinterpret_cast<ReadFunction>(::dlsym(RTLD_NEXT, "read"));
  native_close = reinterpret_cast<CloseFunction>(::dlsym(RTLD_NEXT, "close"));
  native_poll = reinterpret_cast<PollFunction>(::dlsym(RTLD_NEXT, "poll"));
  native_send = reinterpret_cast<SendFunction>(::dlsym(RTLD_NEXT, "send"));
  try {
    require_native(native_open && native_read && native_close && native_poll &&
                       native_send,
                   "native forwarding functions");
    native_refusal();
    native_fields();
    main_refusal();
    main_idle_interrupt();
    main_acquired_prefix();
    main_branch_refusal();
    main_handoff_selection();
    std::cout
        << "Main native refusal prevents read and backstop; business error preserved\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
