#include "blackbird/command_center.hpp"
#include "blackbird/participants.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/tasks.hpp"
#include "blackbird/terminal.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <new>
#include <stdexcept>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
using Clock = std::chrono::steady_clock;
using Path = std::filesystem::path;
void require(bool value, const char *claim) {
  if (!value)
    throw std::runtime_error{claim};
}
template <class F> void until(F predicate, const char *claim) {
  const auto deadline = Clock::now() + std::chrono::seconds{5};
  while (!predicate()) {
    require(Clock::now() < deadline, claim);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
}
struct Directory {
  Path path;
  Directory() {
    char name[] = "/tmp/blackbird-center-XXXXXX";
    require(::mkdtemp(name) != nullptr, "private center fixture directory");
    path = name;
  }
  ~Directory() {
    if (std::uncaught_exceptions()) {
      std::cerr << "retained center fixture " << path << '\n';
      return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
template <class Id> Id identity(unsigned value) {
  IdentityBytes bytes{};
  bytes.front() = static_cast<std::byte>(value);
  return unwrap(Id::from_bytes(bytes));
}
CommandCenterScope scope() {
  return {identity<EnvironmentId>(101), identity<ConversationId>(102)};
}
void apply(TaskState &tasks, Value::Array operations) {
  auto prepared = tasks.prepare(
      Value::object({{"op_id", Value{"edit-" + std::to_string(tasks.revision())}},
                     {"base", Value{Number{tasks.revision()}}},
                     {"ops", Value{std::move(operations)}}}));
  tasks.commit(std::move(prepared));
}
void add_tasks(TaskState &tasks, unsigned count) {
  Value::Array operations;
  for (unsigned n = 0; n < count; ++n)
    operations.push_back(
        Value::object({{"op", Value{"add"}},
                       {"title", Value{n ? "Other task" : "TASK_LITERAL"}},
                       {"status", Value{n ? "queued" : "blocked"}},
                       {"blocker", Value{n ? "" : "BLOCKER_LITERAL"}},
                       {"note", Value{"NOTE_LITERAL"}}}));
  apply(tasks, std::move(operations));
}
RawEventRef publish(TerminalUI &ui, const TaskState &tasks, bool capture) {
  RawEventRef source;
  const auto snapshot = tasks.snapshot({}, &source);
  RawUiValueOrigin origin;
  origin.kind = 2;
  origin.revision = tasks.revision();
  if (capture)
    origin.source = source;
  ui.tasks(snapshot, origin);
  return source;
}
CommandCenterSources sources(TerminalUI &ui, CommandJobs &jobs,
                             ParticipantObservationReader participants = {}) {
  return {
      [&](const TaskObservationQuery &query) { return ui.try_observe_tasks(query); },
      std::move(participants), &jobs};
}
std::string shown(const CommandCenter &center) {
  std::string result;
  for (const auto &line : center.host().snapshot()->inspectors.front().lines) {
    for (const auto &span : line)
      result += span.text;
    result += '\n';
  }
  return result;
}
std::uint64_t target_incarnation(const CommandCenter &center) {
  const auto scene = center.host().snapshot();
  const auto &target = scene->inspectors.front().target;
  if (!target)
    throw std::runtime_error{"selected actual participant requires a native target"};
  return target->incarnation;
}
void adopt(CommandCenter &center, const CommandCenterSources &inputs, bool capture,
           std::string_view literal) {
  until(
      [&] {
        (void)center.refresh(inputs, capture);
        return shown(center).find(literal) != std::string::npos;
      },
      "actual selected source detail adopted");
}
Value answer() {
  auto content = Value::object(
      {{"type", Value{"output_text"}}, {"text", Value{"PARTICIPANT_RESULT"}}});
  auto message = Value::object({{"type", Value{"message"}},
                                {"role", Value{"assistant"}},
                                {"content", Value{Value::Array{std::move(content)}}}});
  return Value::object({{"status", Value{"completed"}},
                        {"model", Value{"fixture-model"}},
                        {"output", Value{Value::Array{std::move(message)}}}});
}
Value participant_arguments() {
  return Value::object(
      {{"request",
        Value::object(
            {{"request_id", Value{"initial"}},
             {"from", Value{"operator"}},
             {"to", Value{"expert"}},
             {"provider", Value{"openai"}},
             {"model", Value{"fixture-model"}},
             {"task", Value{"inspect"}},
             {"context", Value{Value::Array{}}},
             {"profile", Value::object({{"name", Value{"context-only"}},
                                        {"provenance", Value{"private fixture"}},
                                        {"timeout_seconds", Value{Number{10}}},
                                        {"tools", Value{"none"}},
                                        {"requests", Value{Number{1}}}})}})}});
}
std::unique_ptr<Participants> pool() {
  return std::make_unique<Participants>(
      Value{}, [](const OriginalCapture &) { return std::optional<SourceReference>{}; },
      [](const Value &) { return std::optional<ApplicationRecordId>{}; });
}
void start(Participants &participants, std::string key) {
  participants.start(std::move(key), participant_arguments(),
                     [](const Value &, const ColleagueCapture &,
                        const std::function<bool()> &,
                        const RawAdapterContext *) { return answer(); });
}
void semantic(const Path &executable) {
  TaskState tasks;
  add_tasks(tasks, 10);
  TerminalUI ui{"native center"};
  (void)publish(ui, tasks, false);
  CommandJobs jobs;
  auto inputs = sources(ui, jobs);
  CommandCenter center{scope()};
  center.toggle();
  adopt(center, inputs, false, "BLOCKER_LITERAL");
  const auto first = center.host().snapshot();
  require(
      first->collections.front().items.size() == 8 &&
          first->collections.front().title == "T8/10 P? J0" &&
          first->collections.front().items.front().label.starts_with("T t1 blocked") &&
          shown(center).find("Declared blocked") != std::string::npos,
      "eight actual catalog rows have honest total and visible declared status");
  const auto before = tasks.snapshot();
  require(!center.refresh(inputs, false) && center.host().snapshot() == first,
          "unchanged native cuts do not republish a scene");
  unwrap(center.act(tui::InspectorAction::focus_collection));
  center.page(true);
  (void)center.refresh(inputs, false);
  require(center.host().snapshot()->collections.front().items.size() == 2 &&
              center.host().snapshot()->collections.front().items.front().key ==
                  "task:t9" &&
              shown(center).find("TASK_LITERAL") != std::string::npos,
          "task catalog continuation preserves independent selected detail");
  add_tasks(tasks, 1);
  (void)publish(ui, tasks, false);
  (void)center.refresh(inputs, false); // Actual stale task cursor, then reset.
  require(center.needs_refresh(), "task revision conflict requests next bounded reset");
  (void)center.refresh(inputs, false);
  require(center.host().snapshot()->collections.front().items.front().key ==
                  "task:t1" &&
              center.host().snapshot()->collections.front().title == "T8/11 P? J0",
          "new task revision resets only task page");
  require(field(before, "revision") == Value{Number{1}} && tasks.revision() == 2,
          "only fixture edits advanced task authority");
  // Nine actual endpoints expose an eight-row page and retain selected old-owner
  // detail.
  TerminalUI empty_ui{"participant center"};
  auto owner = pool();
  owner->configure(Value::object({{"concurrency", Value{Number{16}}}}));
  for (unsigned n = 0; n < 9; ++n)
    start(*owner, "expert/" + std::to_string(n));
  auto reader = owner->observer();
  auto participant_inputs = sources(empty_ui, jobs, reader);
  CommandCenter participants{scope()};
  participants.toggle();
  adopt(participants, participant_inputs, false, "PARTICIPANT_RESULT");
  const auto owned = participants.host().snapshot();
  const auto owner_incarnation = target_incarnation(participants);
  require(owned->collections.front().items.size() == 8 &&
              owned->collections.front().title == "T? P8+ J0" &&
              shown(participants).find("Task: unbound") != std::string::npos &&
              shown(participants).find("Stop requested no | pending captures ") !=
                  std::string::npos &&
              shown(participants)
                      .find("Result source: raw unavailable | retained unavailable") !=
                  std::string::npos &&
              shown(participants).find("waiting") != std::string::npos,
          "actual participant waiting/unbound and bounded more-rows coverage are "
          "visible");
  owner.reset();
  (void)participants.refresh(participant_inputs, false);
  require(participants.host().snapshot()->collections.front().items.empty() &&
              participants.host().snapshot()->collections.front().title ==
                  "T? Pretired J0" &&
              target_incarnation(participants) == owner_incarnation &&
              participants.host().snapshot()->inspectors.front().notice.text.find(
                  "retired") != std::string::npos &&
              shown(participants).find("PARTICIPANT_RESULT") != std::string::npos,
          "retired owner clears live catalog and retains old owned detail explicitly");
  auto replacement = pool();
  start(*replacement, "expert/0");
  const auto replacement_reader = replacement->observer();
  until(
      [&] {
        return replacement_reader.try_observe({}).status ==
               ParticipantObservationStatus::ready;
      },
      "new actual endpoint query becomes available");
  participant_inputs.participants = replacement_reader;
  (void)participants.refresh(participant_inputs, false);
  require(
      participants.host().snapshot()->collections.front().items.empty() &&
          target_incarnation(participants) == owner_incarnation &&
          replacement->observer().try_observe({}).incarnation != owner_incarnation,
      "same participant spelling in new owner cannot reattach old view incarnation");
  // A genuine locally owned command with nonzero exit and retained bytes.
  const std::string command_id(32, 'a');
  jobs.start(command_id,
             Value::object({{"argv", Value{Value::Array{Value{executable.string()},
                                                        Value{"--child"}}}},
                            {"timeout_seconds", Value{Number{10}}}}));
  bool ended = false;
  std::size_t retained = 0;
  until(
      [&] {
        for (const auto &event : jobs.drain()) {
          if (event.bytes) {
            retained += event.bytes->size();
            jobs.retained(event.job_id, retained);
          } else {
            jobs.settled(event.job_id, event.result);
            ended = true;
          }
        }
        return ended;
      },
      "actual command settled once");
  CommandCenter commands{scope()};
  commands.toggle();
  const auto command_inputs = sources(empty_ui, jobs);
  adopt(commands, command_inputs, false, "COMMAND_LITERAL");
  require(
      shown(commands).find("Exit 7") != std::string::npos &&
          shown(commands).find("Source extents unavailable | capture complete yes") !=
              std::string::npos &&
          shown(commands).find("Stop requested no") != std::string::npos &&
          commands.host().snapshot()->inspectors.front().title ==
              "Command " + command_id &&
          commands.host()
              .snapshot()
              ->collections.front()
              .items.front()
              .label.starts_with("J aaaaaaaa settled"),
      "command inspection shows actual nonzero exit and full detail identity");
  ChatGrid grid;
  grid.reset(100, 30);
  const auto current = commands.host().snapshot();
  (void)unwrap(commands.render(grid, {0, 0, 100, 30}));
  require(commands.host().snapshot() == current,
          "render performs no owner query or publication");
  jobs.shutdown();
}
template <class T> T take(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "complete independent native field");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
RawEventRef reference(ByteView &bytes) {
  return {take<IdentityBytes>(bytes), take<std::uint64_t>(bytes)};
}
bool same(RawEventRef a, RawEventRef b) {
  return a.lane == b.lane && a.ordinal == b.ordinal;
}
std::string text(ByteView &bytes) {
  const auto count = take<std::size_t>(bytes);
  require(count <= bytes.size(), "actual literal bounded string extent");
  const std::string value{reinterpret_cast<const char *>(bytes.data()), count};
  bytes = bytes.subspan(count);
  return value;
}
void cursor(ByteView &bytes) {
  if (take<bool>(bytes)) {
    (void)take<std::uint64_t>(bytes);
    (void)take<std::size_t>(bytes);
  }
}
void task_query(ByteView &bytes) {
  cursor(bytes);
  if (take<bool>(bytes))
    require(take<std::uint64_t>(bytes) == 1, "actual selected native task identity");
  require(take<std::size_t>(bytes) == 8, "finite task page query");
}
void participant_query(ByteView &bytes) {
  (void)take<std::uint64_t>(bytes);
  require(!take<bool>(bytes) && !take<bool>(bytes),
          "no invented participant query IDs");
  require(take<std::size_t>(bytes) == 8 && take<std::size_t>(bytes) == 4096 &&
              take<std::size_t>(bytes) == 0,
          "finite participant query bounds");
}
void command_query(ByteView &bytes) {
  require(!take<bool>(bytes) && !take<bool>(bytes), "no invented command query IDs");
  require(take<std::size_t>(bytes) == 8 && take<std::size_t>(bytes) == 4096 &&
              take<std::size_t>(bytes) == 16 && take<std::size_t>(bytes) == 0,
          "finite command byte/chunk/page query bounds");
}
struct TaskWire {
  RawEventRef source{}, ingress{};
  std::uint64_t revision = 0;
  bool selected = false;
  std::string title, blocker;
};
std::string task_row(ByteView &bytes) {
  require(take<std::uint64_t>(bytes) == 1 && !take<bool>(bytes),
          "actual task row identity/parent");
  require(take<std::uint8_t>(bytes) == 2, "literal declared blocked ordinal");
  (void)take<std::uint64_t>(bytes);
  (void)take<std::uint64_t>(bytes);
  auto title = text(bytes);
  (void)text(bytes);
  (void)take<std::size_t>(bytes);
  (void)take<std::size_t>(bytes);
  return title;
}
TaskWire decode_snapshot(ByteView bytes) {
  TaskWire result;
  require(take<std::uint8_t>(bytes) == 1 &&
              take<IdentityBytes>(bytes) == scope().environment.bytes() &&
              take<IdentityBytes>(bytes) == scope().conversation.bytes() &&
              take<std::uint64_t>(bytes) == 2 && take<std::uint64_t>(bytes) != 0,
          "literal wire version and actual scoped view identity");
  require(take<std::uint8_t>(bytes) == 1 && take<std::uint64_t>(bytes) == 1,
          "actual independently selected task target");
  task_query(bytes);
  require(take<std::uint8_t>(bytes) == 0 && take<std::uint8_t>(bytes) == 0,
          "actual ready task publication and owner status");
  participant_query(bytes);
  require(take<std::uint8_t>(bytes) == 4 && !take<bool>(bytes),
          "actual unavailable participant reader and unretired cache");
  command_query(bytes);
  require(take<std::uint8_t>(bytes) == 0, "actual empty command owner ready");
  require(take<bool>(bytes), "actual cached task cut");
  task_query(bytes);
  require(take<std::uint8_t>(bytes) == 0 && take<std::uint8_t>(bytes) == 2 &&
              take<bool>(bytes),
          "task cache includes actual publication and source kind");
  result.source = reference(bytes);
  require(!take<bool>(bytes) && take<bool>(bytes),
          "no invented task fact, actual revision present");
  result.revision = take<std::uint64_t>(bytes);
  result.ingress = reference(bytes);
  require(take<std::uint8_t>(bytes) == 0 &&
              take<std::uint64_t>(bytes) == result.revision &&
              take<std::size_t>(bytes) == 1,
          "actual native task population/revision");
  std::size_t total = 0;
  for (unsigned n = 0; n < 5; ++n)
    total += take<std::size_t>(bytes);
  require(total == 1, "actual task status population");
  (void)text(bytes);
  (void)text(bytes);
  require(take<std::size_t>(bytes) == 1, "exact bounded task row count");
  result.title = task_row(bytes);
  cursor(bytes);
  require(!take<bool>(bytes), "actual final catalog page");
  const auto selection = take<std::uint8_t>(bytes);
  require(selection <= 1, "valid native selection");
  result.selected = selection == 1;
  if (result.selected) {
    require(task_row(bytes) == result.title,
            "independent detail identifies same actual task");
    require(text(bytes) == "NOTE_LITERAL", "literal actual task note");
    result.blocker = text(bytes);
  }
  require(take<std::size_t>(bytes) <= 9 && take<std::size_t>(bytes) <= 8960,
          "actual bounded task examination and copied bytes");
  require(!take<bool>(bytes) && take<bool>(bytes),
          "unavailable participant has no cache; real command cache exists");
  command_query(bytes);
  require(take<std::uint8_t>(bytes) == 0 && take<std::size_t>(bytes) == 0 &&
              !take<bool>(bytes) && !take<bool>(bytes) &&
              take<std::uint8_t>(bytes) == 0,
          "real empty CommandJobs result, no synthetic command rows");
  for (unsigned n = 0; n < 8; ++n)
    require(take<std::size_t>(bytes) == 0, "actual empty retained command window");
  (void)take<bool>(bytes);
  (void)take<bool>(bytes);
  (void)take<std::uint8_t>(bytes);
  require(take<std::size_t>(bytes) == 0 && take<std::size_t>(bytes) == 0 &&
              take<std::size_t>(bytes) == 0 && bytes.empty(),
          "exact finite native snapshot fully independently consumed without padding");
  return result;
}
using RefKey = std::pair<IdentityBytes, std::uint64_t>;
RefKey key(RawEventRef ref) { return {ref.lane, ref.ordinal}; }
void raw() {
  Directory directory;
  RawEventRef applied, actual_snapshot, actual_ingress, marker;
  {
    auto runtime =
        unwrap(RawRuntime::open(directory.path.string(), scope().environment));
    auto engine = unwrap(runtime->prepare_thread());
    auto ui_handle = unwrap(runtime->prepare_thread());
    RawThreadBinding engine_binding{engine};
    TaskState tasks;
    add_tasks(tasks, 1);
    RawEventRef completion;
    const auto snapshot = tasks.snapshot({}, &completion);
    actual_snapshot = completion;
    TerminalUI ui{"raw center"};
    ui.raw_capture(true);
    CommandJobs jobs;
    {
      RawThreadBinding ui_binding{ui_handle};
      ui.tasks(snapshot, {2, completion, {}, tasks.revision()});
      actual_ingress = ui.try_observe_tasks({}).ingress;
      CommandCenter center{scope()};
      center.toggle();
      const auto inputs = sources(ui, jobs);
      (void)center.refresh(inputs, true);
      unwrap(center.act(tui::InspectorAction::focus_collection));
      Composer composer;
      auto data = unwrap(raw_claim_data(composer.capture_bound(3)));
      auto control = unwrap(raw_claim_control(3 * raw_ui_feed_credit));
      InputResult result;
      std::size_t index = 0;
      for (const char byte : std::string_view{"\x1b[H"}) {
        RawUiInput input{true, {}, index++, &data, &control, {}};
        result = composer.feed(byte, &input, true);
      }
      require(result.action == InputAction::inspect_first &&
                  raw_ui_source_valid(result.source.record),
              "actual native Home action has captured result origin");
      marker = result.source.record;
      unwrap(center.act(tui::InspectorAction::first, marker));
      adopt(center, inputs, true, "BLOCKER_LITERAL");
      const auto scene = center.host().snapshot();
      applied = center.applied();
      require(raw_ui_source_valid(applied) && !center.refresh(inputs, true) &&
                  center.host().snapshot() == scene && same(center.applied(), applied),
              "unchanged wire preserves actual successful adoption identity");
    }
    jobs.shutdown();
  }
  const auto runs = unwrap(discover_raw_runs(directory.path.string()));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "one actually closed native raw capture run");
  auto reader =
      unwrap(RawRunReader::open((directory.path / "raw" / runs.front().name).string()));
  std::vector<std::byte> storage(1024 * 1024);
  std::map<RefKey, std::uint32_t> sites;
  std::map<RefKey, TaskWire> observations;
  RawEventRef adopted_source;
  while (const auto record = unwrap(reader->read(storage))) {
    ByteView bytes = record->raw;
    require(take<std::uint32_t>(bytes) == record->raw.size(),
            "actual record native extent");
    const auto site = take<std::uint32_t>(bytes);
    const RawEventRef record_ref{record->origin.lane, take<std::uint64_t>(bytes)};
    sites[key(record_ref)] = site;
    if (site == 4088)
      observations.emplace(key(record_ref), decode_snapshot(bytes));
    else if (site == 4089) {
      const auto source = reference(bytes);
      const auto origin = reference(bytes);
      if (same(record_ref, applied))
        require(same(origin, marker),
                "adoption uses actual current action result, not opener");
      require(take<std::uint64_t>(bytes) == 2 && take<std::uint64_t>(bytes) != 0 &&
                  take<std::uint64_t>(bytes) > 0 && take<bool>(bytes) &&
                  !take<bool>(bytes),
              "actual view incarnation, generation and successful adoption outcome");
      (void)take<ErrorCode>(bytes);
      (void)take<std::int64_t>(bytes);
      require(bytes.empty(),
              "CONTROL contains exactly two references and native outcome fields");
      if (same(record_ref, applied))
        adopted_source = source;
    }
  }
  const auto found = observations.find(key(adopted_source));
  require(found != observations.end() && found->second.selected &&
              found->second.title == "TASK_LITERAL" &&
              found->second.blocker == "BLOCKER_LITERAL" &&
              same(found->second.source, actual_snapshot) &&
              same(found->second.ingress, actual_ingress) &&
              found->second.revision == 1 && sites.at(key(actual_snapshot)) == 1708 &&
              sites.at(key(actual_ingress)) == 4050 && sites.at(key(marker)) == 4010,
          "applied CONTROL links actual literal task DATA and genuine "
          "source/publication provenance");
}
void pressure() {
  for (const auto partition : {RawPartition::data, RawPartition::control}) {
    IdentityBytes lane{};
    lane.front() = std::byte{103};
    const auto name = "/bb-ctr-" + std::to_string(::getpid()) +
                      (partition == RawPartition::data ? "-d" : "-c");
    auto channel =
        unwrap(RawChannel::create(name, lane, {32 * 1024 * 1024, 32768, 4096}));
    {
      RawProducer producer{channel};
      RawThreadBinding binding{producer, lane};
      RawCaptureFailureScope capture{true};
      TaskState tasks;
      add_tasks(tasks, 1);
      TerminalUI ui{"pressure center"};
      (void)publish(ui, tasks, false);
      CommandJobs jobs;
      auto inputs = sources(ui, jobs);
      CommandCenter center{scope()};
      center.toggle();
      adopt(center, inputs, true, "BLOCKER_LITERAL");
      const auto prior = center.host().snapshot();
      const auto prior_source = center.applied();
      // Advance an actual published task source before exhausting UI capacity.
      add_tasks(tasks, 1);
      (void)publish(ui, tasks, false);
      std::vector<RawCapacityClaim> data;
      std::vector<RawControlToken> control;
      for (std::uint64_t bytes = 1ULL << 26; bytes; bytes >>= 1) {
        if (partition == RawPartition::data) {
          auto grant = raw_try_claim_data(bytes);
          if (grant.has_value())
            data.push_back(std::move(grant).value());
        } else {
          auto grant = raw_try_claim_control(bytes);
          if (grant.has_value())
            control.push_back(std::move(grant).value());
        }
      }
      const auto refused = partition == RawPartition::data
                               ? raw_try_claim_data(1).has_value()
                               : raw_try_claim_control(1).has_value();
      require(!refused, "actual selected capture partition physically exhausted");
      (void)center.refresh(inputs, true);
      require(center.host().snapshot() == prior &&
                  same(center.applied(), prior_source) &&
                  center.notice() == "stale: recording capacity" && !capture.error(),
              "DATA and CONTROL admission refusal separately preserve exact prior "
              "scene/source");
      data.clear();
      control.clear();
      (void)center.refresh(inputs, true);
      require(center.host().snapshot() != prior &&
                  center.host().snapshot()->collections.front().items.size() == 2 &&
                  !same(center.applied(), prior_source) && !capture.error(),
              "released capacity permits explicit new linked adoption without losing "
              "last-good body");
    }
    unwrap(RawChannel::unlink(name));
  }
  // A fresh producer leaves exactly the actual two-reference CONTROL record.
  IdentityBytes lane{};
  lane.front() = std::byte{104};
  const auto name = "/bb-center-fit-" + std::to_string(::getpid());
  auto channel = unwrap(RawChannel::create(name, lane, {65536, 32768, 4096}));
  {
    RawProducer producer{channel};
    RawThreadBinding binding{producer, lane};
    constexpr auto exact = raw_envelope_size + 2 * 24 + 3 * sizeof(std::uint64_t) +
                           2 * sizeof(bool) + sizeof(ErrorCode) + sizeof(std::int64_t);
    auto held = unwrap(raw_try_claim_control(32768 - exact));
    CommandJobs jobs;
    CommandCenter center{scope()};
    center.toggle();
    CommandCenterSources inputs;
    inputs.commands = &jobs;
    require(center.refresh(inputs, true) && raw_ui_source_valid(center.applied()) &&
                center.notice() != "stale: recording capacity",
            "exact two-reference CONTROL remainder admits actual adoption");
  }
  unwrap(RawChannel::unlink(name));
}
} // namespace
int main(int argc, char **argv) {
  try {
    require(std::setlocale(LC_ALL, "") != nullptr, "actual terminal locale");
    if (argc == 2 && std::string_view{argv[1]} == "--child") {
      constexpr std::string_view bytes = "COMMAND_LITERAL\n";
      require(::write(STDOUT_FILENO, bytes.data(), bytes.size()) ==
                  static_cast<ssize_t>(bytes.size()),
              "actual owned child output");
      return 7;
    }
    require(argc == 2, "select semantic/raw/pressure");
    const std::string_view mode = argv[1];
    if (mode == "semantic")
      semantic(std::filesystem::canonical(argv[0]));
    else if (mode == "raw")
      raw();
    else if (mode == "pressure")
      pressure();
    else
      require(false, "known center oracle mode");
    std::cout << mode << ": actual native center assertions passed\n";
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
