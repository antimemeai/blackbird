#include "blackbird/command_inspector.hpp"
#include "blackbird/context.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_ui.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <clocale>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <span>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
using Clock = std::chrono::steady_clock;
using Path = std::filesystem::path;
void require(bool good, const char *message) {
  if (!good)
    throw std::runtime_error{message};
}
template <class F> void until(F predicate, const char *message) {
  const auto deadline = Clock::now() + std::chrono::seconds{5};
  while (!predicate()) {
    require(Clock::now() < deadline, message);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
}
struct Directory {
  Path path;
  Directory() {
    char temporary[] = "/tmp/blackbird-inspector-XXXXXX";
    require(::mkdtemp(temporary) != nullptr, "private inspector directory");
    path = temporary;
  }
  ~Directory() {
    if (std::uncaught_exceptions()) {
      std::cerr << "retained inspector fixture " << path << '\n';
      return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
enum class OutputPart { first, second };
std::string output(char tag, OutputPart part) {
  auto bytes = std::string{tag == 'a' ? "alpha:" : "beta:"};
  if (part == OutputPart::first) {
    bytes += "\x1b[31mRED\x1b[0m";
    bytes.push_back('\0');
    bytes.push_back('\a');
    bytes += "\nFIRST\n";
  } else
    bytes += "SECOND\n";
  return bytes;
}
int child(const Path &gate_path, char tag) {
  const int gate = ::open(gate_path.c_str(), O_RDWR);
  if (gate < 0)
    return 91;
  for (const auto part : {OutputPart::first, OutputPart::second}) {
    const auto bytes = output(tag, part);
    if (::write(STDOUT_FILENO, bytes.data(), bytes.size()) !=
        static_cast<ssize_t>(bytes.size()))
      return 92;
    char ack = 0;
    ssize_t size;
    do {
      size = ::read(gate, &ack, 1);
    } while (size < 0 && errno == EINTR);
    if (size != 1 || ack != 'X')
      return 93;
  }
  (void)::close(gate);
  return 23;
}
CommandObservationId id(char tag) {
  CommandObservationId result;
  result.bytes.fill(tag);
  return result;
}
CommandObservation observe(const CommandJobs &jobs,
                           std::optional<CommandObservationId> selected = {}) {
  CommandObservation result;
  until(
      [&] {
        result = jobs.try_observe({{}, selected});
        return result.status != CommandObservationStatus::busy;
      },
      "finite pure owner observation");
  require(result.status == CommandObservationStatus::ready, "actual owner ready");
  return result;
}
struct WorkerPaths {
  Path directory;
  Path executable;
};
struct Worker {
  Path gate_path;
  int gate = -1;
  std::string key, retained;
  bool terminal = false;
  Worker(CommandJobs &jobs, const WorkerPaths &paths, char tag)
      : gate_path(paths.directory / std::string{tag}), key(32, tag) {
    require(::mkfifo(gate_path.c_str(), 0600) == 0, "private worker FIFO");
    gate = ::open(gate_path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    require(gate >= 0, "owned worker FIFO");
    jobs.start(
        key, Value::object(
                 {{"argv", Value{Value::Array{
                               Value{paths.executable.string()}, Value{"--child"},
                               Value{gate_path.string()}, Value{std::string(1, tag)}}}},
                  {"timeout_seconds", Value{Number{15}}}}));
  }
  ~Worker() {
    if (gate >= 0)
      (void)::close(gate);
  }
  void ack() const {
    require(::write(gate, "X", 1) == 1, "release actual output step");
  }
};
void pump(CommandJobs &jobs, std::span<Worker *const> workers) {
  for (const auto &event : jobs.drain()) {
    const auto found =
        std::find_if(workers.begin(), workers.end(),
                     [&](const Worker *w) { return w->key == event.job_id; });
    require(found != workers.end(), "actual event belongs to known worker");
    auto &worker = **found;
    if (event.bytes) {
      require(event.offset == worker.retained.size(), "exact retained byte continuity");
      worker.retained += *event.bytes;
      jobs.retained(event.job_id, worker.retained.size());
    } else {
      jobs.settled(event.job_id, event.result);
      worker.terminal = true;
    }
  }
}
void await_bytes(CommandJobs &jobs, Worker &worker, std::span<Worker *const> workers,
                 std::size_t count) {
  until(
      [&] {
        pump(jobs, workers);
        return worker.retained.size() == count;
      },
      "real worker bytes retained");
}
std::string shown(const CommandInspector &inspector) {
  const auto scene = inspector.host().snapshot();
  require(scene && scene->inspectors.size() == 1, "one accepted inspector");
  std::string result;
  for (const auto &line : scene->inspectors.front().lines) {
    for (const auto &span : line)
      result += span.text;
    result += '\n';
  }
  return result;
}
void selected(CommandInspector &inspector, CommandJobs &jobs, char tag, bool raw) {
  until(
      [&] {
        (void)inspector.refresh(&jobs, raw);
        const auto scene = inspector.host().snapshot();
        return inspector.notice().empty() && scene->inspectors.size() == 1 &&
               scene->inspectors.front().target &&
               scene->inspectors.front().target->key == std::string(32, tag) &&
               shown(inspector).find(tag == 'a' ? "alpha:" : "beta:") !=
                   std::string::npos;
      },
      "selected real command adopted");
}
void safe_view(CommandInspector &inspector, char tag) {
  const auto expected =
      std::string{tag == 'a' ? "alpha:" : "beta:"} + "\\x1b[31mRED\\x1b[0m\\x00\\x07";
  const auto text = shown(inspector);
  require(text.find(expected) != std::string::npos &&
              text.find("FIRST") != std::string::npos,
          "literal safe representation of actual selected bytes");
  require(std::none_of(text.begin(), text.end(),
                       [](unsigned char byte) { return byte < 32 && byte != '\n'; }),
          "accepted properties contain no executable terminal control");
  ChatGrid grid;
  grid.reset(100, 25);
  const auto layout = unwrap(inspector.render(grid, {0, 0, 100, 25}));
  require(layout.bounds.width == 100 && layout.bounds.height == 25,
          "real component rendered requested bounded view");
  for (const auto &cell : grid.cells)
    require(cell.scalar >= U' ' && cell.scalar != U'\x7f',
            "inspector painted literal cells rather than source control bytes");
}
void same_domain(const CommandObservation &before, const CommandObservation &after) {
  require(before.row_count == after.row_count && before.detail.id == after.detail.id &&
              before.detail.admission_order == after.detail.admission_order &&
              before.detail.received == after.detail.received &&
              before.detail.retained == after.detail.retained &&
              before.detail.terminal_retained == after.detail.terminal_retained &&
              before.detail.stop_requested == after.detail.stop_requested &&
              before.detail.phase == after.detail.phase,
          "inspection cannot replay, retain, settle, stop or advance domain work");
}
void semantic(const Path &executable) {
  Directory directory;
  CommandJobs jobs;
  Worker a{jobs, {.directory = directory.path, .executable = executable}, 'a'},
      b{jobs, {.directory = directory.path, .executable = executable}, 'b'};
  const std::array workers{&a, &b};
  await_bytes(jobs, a, workers, output('a', OutputPart::first).size());
  await_bytes(jobs, b, workers, output('b', OutputPart::first).size());
  const auto before_a = observe(jobs, id('a')), before_b = observe(jobs, id('b'));
  CommandInspector inspector;
  inspector.toggle();
  require(inspector.open() && inspector.focused(), "toggle enters local inspector");
  selected(inspector, jobs, 'a', false);
  safe_view(inspector, 'a');
  unwrap(inspector.act(tui::InspectorAction::next));
  selected(inspector, jobs, 'b', false);
  safe_view(inspector, 'b');
  inspector.focus_editor();
  require(!inspector.focused() && inspector.open(),
          "editor return preserves open view");
  inspector.toggle();
  require(!inspector.open(), "local close returns without domain action");
  inspector.toggle();
  selected(inspector, jobs, 'b', false);
  same_domain(before_a, observe(jobs, id('a')));
  same_domain(before_b, observe(jobs, id('b')));
  require(a.retained == output('a', OutputPart::first) &&
              b.retained == output('b', OutputPart::first),
          "retained originals unchanged by inspection");
  jobs.shutdown();
}
template <class T> T take(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "complete independently decoded native field");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
RawEventRef ref(ByteView &bytes) {
  return {take<IdentityBytes>(bytes), take<std::uint64_t>(bytes)};
}
std::string fixed_id(ByteView &bytes) {
  require(bytes.size() >= 32, "complete fixed command identity");
  const std::string result{reinterpret_cast<const char *>(bytes.data()), 32};
  require(result.find_first_not_of("0123456789abcdef") == std::string::npos,
          "actual valid command identity");
  bytes = bytes.subspan(32);
  return result;
}
std::optional<std::string> optional_id(ByteView &bytes) {
  return take<bool>(bytes) ? std::optional{fixed_id(bytes)} : std::nullopt;
}
struct Row {
  std::string key;
  std::uint8_t phase = 0, custody = 0;
  std::size_t received = 0, retained = 0;
  RawEventRef launch{}, terminal{};
};
Row row(ByteView &bytes) {
  Row result;
  result.key = fixed_id(bytes);
  result.phase = take<std::uint8_t>(bytes);
  result.custody = take<std::uint8_t>(bytes);
  require(result.phase <= 5 && result.custody <= 4,
          "literal native lifecycle/custody ordinals");
  (void)take<std::uint64_t>(bytes); // actual admission order
  result.received = take<std::size_t>(bytes);
  result.retained = take<std::size_t>(bytes);
  for (unsigned n = 0; n < 10; ++n)
    (void)take<bool>(bytes);
  for (unsigned n = 0; n < 3; ++n)
    (void)take<int>(bytes);
  result.launch = ref(bytes);
  result.terminal = ref(bytes);
  return result;
}
struct Source {
  RawEventRef original;
  std::size_t begin, offset, length;
};
struct Apply {
  RawEventRef record;
  std::uint64_t generation = 0;
  bool adopted = false;
  std::optional<std::string> selected;
  std::optional<Row> detail;
  std::size_t size = 0, begin = 0, next = 0;
  bool provenance = false;
  std::vector<Source> sources;
};
Apply decode_apply(ByteView bytes, RawEventRef source) {
  Apply result;
  result.record = source;
  require(take<std::uint64_t>(bytes) == 1 && take<std::uint64_t>(bytes) == 1,
          "actual inspector view owner/incarnation");
  (void)ref(bytes); // actual triggering input origin (absent in native fixture)
  result.generation = take<std::uint64_t>(bytes);
  result.adopted = take<bool>(bytes);
  const bool failed = take<bool>(bytes);
  (void)take<ErrorCode>(bytes); // ignored by contract when has_error=false
  (void)take<std::int64_t>(bytes);
  require(!failed, "successful fixture adoption has no error");
  (void)optional_id(bytes); // catalog continuation query
  result.selected = optional_id(bytes);
  require(take<std::size_t>(bytes) <= 8 && take<std::size_t>(bytes) <= 4096 &&
              take<std::size_t>(bytes) <= 16,
          "recorded finite owner query bounds");
  (void)take<std::size_t>(bytes); // byte-offset query
  const auto status = take<std::uint8_t>(bytes);
  require(status <= 4 && (!result.adopted || status == 0),
          "only a ready observation can be adopted");
  const auto count = take<std::size_t>(bytes);
  require(count <= 8, "recorded finite catalog rows");
  (void)optional_id(bytes);
  (void)take<bool>(bytes);
  for (std::size_t n = 0; n < count; ++n)
    (void)row(bytes);
  const auto selection = take<std::uint8_t>(bytes);
  require(selection <= 2, "recorded selection state");
  if (selection == 1)
    result.detail = row(bytes);
  result.size = take<std::size_t>(bytes);
  const auto sources = take<std::size_t>(bytes);
  require(result.size <= 4096 && sources <= 16, "recorded finite source window");
  (void)take<std::size_t>(bytes); // requested
  result.begin = take<std::size_t>(bytes);
  result.next = take<std::size_t>(bytes);
  (void)take<std::size_t>(bytes); // cached begin
  (void)take<std::size_t>(bytes); // retained end
  (void)take<std::size_t>(bytes); // received end
  (void)take<bool>(bytes);        // omitted prefix
  result.provenance = take<bool>(bytes);
  (void)take<std::uint8_t>(bytes); // page end reason
  for (std::size_t n = 0; n < sources; ++n)
    result.sources.push_back({ref(bytes), take<std::size_t>(bytes),
                              take<std::size_t>(bytes), take<std::size_t>(bytes)});
  require(take<std::size_t>(bytes) <= 9 && take<std::size_t>(bytes) <= 16,
          "recorded bounded actual metadata/chunk coverage");
  (void)take<std::size_t>(bytes); // binary seek comparisons
  require(bytes.empty(), "native adoption wire has no unparsed or invented fields");
  return result;
}
using RefKey = std::pair<IdentityBytes, std::uint64_t>;
void originals(const Path &directory, RawEventRef applied, std::string_view expected,
               unsigned minimum_adoptions = 1) {
  const auto runs = unwrap(discover_raw_runs(directory.string()));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "one actually closed inspector capture run");
  auto reader =
      unwrap(RawRunReader::open((directory / "raw" / runs.front().name).string()));
  std::vector<std::byte> storage(1024 * 1024);
  std::map<RefKey, std::string> reads;
  std::map<RefKey, std::pair<std::string, std::size_t>> chunks;
  std::vector<Apply> observations;
  while (const auto record = unwrap(reader->read(storage))) {
    ByteView bytes = record->raw;
    const auto length = take<std::uint32_t>(bytes), site = take<std::uint32_t>(bytes);
    const auto ordinal = take<std::uint64_t>(bytes);
    require(length == record->raw.size(), "actual native raw record envelope");
    const RawEventRef source{record->origin.lane, ordinal};
    if (site == 2008) {
      (void)ref(bytes);
      const auto size = take<ssize_t>(bytes);
      (void)take<int>(bytes);
      if (size > 0) {
        require(bytes.size() == static_cast<std::size_t>(size),
                "actual read byte range");
        reads[{source.lane, source.ordinal}] = {
            reinterpret_cast<const char *>(bytes.data()), bytes.size()};
      }
    } else if (site == 2009) {
      const auto original = ref(bytes);
      require(take<std::size_t>(bytes) == 32, "actual chunk command identity size");
      const auto key = fixed_id(bytes);
      const auto begin = take<std::size_t>(bytes);
      chunks[{original.lane, original.ordinal}] = {key, begin};
    } else if (site == 4087)
      observations.push_back(decode_apply(bytes, source));
    else
      require(site != 8001, "inspector adoption never fences unrelated capture");
  }
  unsigned adopted = 0;
  bool last_found = false;
  std::uint64_t generation = 0;
  for (const auto &view : observations) {
    require(view.generation > generation,
            "actual adopted observation generation advances");
    generation = view.generation;
    if (!view.adopted)
      continue;
    ++adopted;
    const auto selected_detail = view.detail;
    std::string reconstructed;
    for (const auto &source : view.sources) {
      const auto found = reads.find({source.original.lane, source.original.ordinal});
      const auto chunk = chunks.find({source.original.lane, source.original.ordinal});
      require(found != reads.end() && source.offset <= found->second.size() &&
                  source.length <= found->second.size() - source.offset &&
                  source.begin + source.offset == view.begin + reconstructed.size(),
              "adopted source names actual continuous native read extent");
      if (!selected_detail.has_value())
        throw std::runtime_error{"actual worker source requires selected detail"};
      const auto &detail = *selected_detail;
      require(chunk != chunks.end() && chunk->second.first == detail.key &&
                  chunk->second.second == source.begin &&
                  view.next <= detail.retained && detail.retained <= detail.received,
              "actual worker chunk identity/frontier matches selected detail");
      reconstructed += found->second.substr(source.offset, source.length);
    }
    require(reconstructed.size() == view.size && view.next == view.begin + view.size,
            "actual source ranges cover precisely the adopted byte window");
    if (view.record.lane == applied.lane && view.record.ordinal == applied.ordinal) {
      if (!selected_detail.has_value())
        throw std::runtime_error{
            "successful applied reference requires selected detail"};
      const auto &detail = *selected_detail;
      require(detail.key == std::string(32, 'a') &&
                  view.selected == std::optional{std::string(32, 'a')} &&
                  view.provenance && reconstructed == expected,
              "successful applied reference reconstructs exact selected worker bytes");
      last_found = true;
    }
  }
  require(last_found && adopted >= minimum_adoptions,
          "last actual applied source exists among successful adoptions");
}
std::vector<RawControlToken> exhaust_control(std::uint64_t full_capacity = 0) {
  std::vector<RawControlToken> holds;
  holds.reserve(64);
  if (full_capacity)
    until(
        [&] {
          auto grant = raw_try_claim_control(full_capacity);
          if (!grant.has_value())
            return false;
          holds.push_back(std::move(grant).value());
          return true;
        },
        "actual prior UI prefix acknowledged before full physical grant");
  else
    for (std::uint64_t bytes = 1ULL << 26; bytes; bytes >>= 1) {
      auto grant = raw_try_claim_control(bytes);
      if (grant.has_value())
        holds.push_back(std::move(grant).value());
    }
  const auto refused = raw_try_claim_control(1);
  require(!refused.has_value() && refused.error().code == ErrorCode::capacity,
          "actual producer CONTROL capacity physically exhausted");
  return holds;
}
void pressure() {
  Directory directory;
  IdentityBytes lane{};
  lane[0] = std::byte{93};
  const auto name = "/bb-inspector-pressure-" + std::to_string(::getpid());
  auto channel = unwrap(RawChannel::create(name, lane, {65536, 32768, 4096}));
  {
    RawProducer producer{channel};
    RawThreadBinding binding{producer, lane};
    RawCaptureFailureScope capture{true};
    CommandJobs jobs;
    CommandInspector inspector;
    inspector.toggle();
    (void)inspector.refresh(&jobs, true);
    const auto scene = inspector.host().snapshot();
    const auto applied = inspector.applied();
    require(raw_ui_source_valid(applied), "first actual successful capture adoption");
    auto holds = exhaust_control();
    (void)inspector.refresh(nullptr,
                            true); // Changed observation, never an unchanged skip.
    require(
        inspector.host().snapshot() == scene &&
            inspector.applied().lane == applied.lane &&
            inspector.applied().ordinal == applied.ordinal &&
            inspector.notice() == "stale: recording capacity" && !capture.error(),
        "try-admission refusal preserves exact prior view/source and remains stale");
    unwrap(inspector.act(tui::InspectorAction::toggle_focus));
    holds.clear();
    (void)inspector.refresh(&jobs, true);
    require(inspector.notice().empty() && !capture.error(),
            "released physical capacity permits explicit subsequent refresh");
  }
  unwrap(RawChannel::unlink(name));
}
void raw(const Path &executable, bool pressure_case) {
  Directory directory;
  IdentityBytes environment{};
  environment[0] = std::byte{94};
  RawEventRef applied;
  std::string expected;
  {
    auto runtime = unwrap(RawRuntime::open(
        directory.path.string(), unwrap(EnvironmentId::from_bytes(environment)),
        {{8 * 1024 * 1024, 65536, 4096}, 4096}));
    auto engine_handle = unwrap(runtime->prepare_thread());
    auto ui_handle = unwrap(runtime->prepare_thread());
    RawThreadBinding engine_binding{engine_handle};
    CommandJobs jobs;
    Worker a{jobs, {.directory = directory.path, .executable = executable}, 'a'};
    const std::array workers{&a};
    await_bytes(jobs, a, workers, output('a', OutputPart::first).size());
    CommandInspector inspector;
    {
      RawThreadBinding ui_binding{ui_handle};
      inspector.toggle();
      selected(inspector, jobs, 'a', true);
      safe_view(inspector, 'a');
    }
    const auto before = observe(jobs, id('a'));
    if (pressure_case) {
      RawThreadBinding ui_binding{ui_handle};
      RawCaptureFailureScope capture{true};
      const auto prior = inspector.host().snapshot();
      const auto prior_source = inspector.applied();
      auto holds = exhaust_control(65536);
      {
        RawThreadBinding command_binding{engine_handle};
        a.ack();
        await_bytes(jobs, a, workers,
                    output('a', OutputPart::first).size() +
                        output('a', OutputPart::second).size());
        jobs.request(
            Value::object({{"op", Value{"background"}}, {"job_id", Value{a.key}}}));
        const auto controls = jobs.requests();
        require(controls.size() == 1 &&
                    field(jobs.control(controls.front()), "released") == Value{false},
                "other producer's actual command controls progress under UI pressure");
      }
      (void)inspector.refresh(&jobs, true);
      require(inspector.host().snapshot() == prior &&
                  inspector.applied().lane == prior_source.lane &&
                  inspector.applied().ordinal == prior_source.ordinal &&
                  inspector.notice() == "stale: recording capacity" && !capture.error(),
              "real source advance cannot publish without raw adoption capacity");
      holds.clear();
      selected(inspector, jobs, 'a', true);
      require(shown(inspector).find("SECOND") != std::string::npos &&
                  inspector.notice().empty() && !capture.error(),
              "last good observation recovers to actual newly retained bytes");
    } else {
      RawThreadBinding ui_binding{ui_handle};
      (void)inspector.refresh(&jobs, true);
      same_domain(before, observe(jobs, id('a')));
    }
    expected = a.retained;
    applied = inspector.applied();
    require(raw_ui_source_valid(applied),
            "actual selected view has valid applied source");
    jobs.shutdown();
  }
  originals(directory.path, applied, expected, pressure_case ? 3U : 2U);
}
} // namespace
int main(int argc, char **argv) {
  try {
    require(std::setlocale(LC_ALL, "") != nullptr, "configured terminal text locale");
    if (argc == 4 && std::string_view{argv[1]} == "--child")
      return child(argv[2], argv[3][0]);
    require(argc == 2, "select semantic/raw/pressure/runtime-pressure");
    const auto executable = std::filesystem::canonical(argv[0]);
    const std::string_view mode = argv[1];
    if (mode == "semantic")
      semantic(executable);
    else if (mode == "raw" || mode == "runtime-pressure")
      raw(executable, mode == "runtime-pressure");
    else if (mode == "pressure")
      pressure();
    else
      require(false, "unknown inspector oracle case");
    std::cout << mode << ": actual command adoption assertions passed\n";
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
