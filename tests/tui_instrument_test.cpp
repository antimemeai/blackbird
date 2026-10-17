#include "blackbird/raw_reader.hpp"
#include "blackbird/tools.hpp"
#include "blackbird/tui_instrument.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <signal.h>
#include <stdexcept>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
using Clock = std::chrono::steady_clock;
void require(bool condition, const char *claim) {
  if (!condition)
    throw std::runtime_error{claim};
}
template <class T> T id(unsigned char first) {
  IdentityBytes bytes{};
  bytes.front() = static_cast<std::byte>(first);
  return unwrap(T::from_bytes(bytes));
}
std::shared_ptr<const tui::lua::Script> script(std::string source) {
  return unwrap(tui::lua::Script::create(std::move(source)));
}
constexpr std::string_view composition =
    "local c=ui.collection('results','REAL ROWS') "
    "for i=1,ui.rows() do ui.item(c,ui.key(i),ui.label(i),i) end "
    "ui.inspector('comparison','A / B','A '..ui.detail_title(1)..'\\n'..ui.detail(1).."
    "'\\nB '..ui.detail_title(2)..'\\n'..ui.detail(2)) ";
std::shared_ptr<const tui::lua::Script> normal() {
  return script("return {compose=function() " + std::string{composition} +
                "end, interrupt=function() ui.request('interrupt') end}");
}
struct Fixture {
  std::shared_ptr<CommandJobs> jobs = std::make_shared<CommandJobs>();
  std::unique_ptr<TuiInstrument> view;
  const std::string a = std::string(32, 'a'), b = std::string(32, 'b');
  explicit Fixture(bool missing = false) {
    auto result = TuiInstrument::create(
        {id<EnvironmentId>(81), id<ConversationId>(82)}, jobs,
        {missing ? std::shared_ptr<const tui::lua::Script>{} : normal(),
         "fixture.lua",
         {},
         {},
         false});
    require(result.has_value(), "native instrument must admit authored finite scene");
    view = std::move(result).value();
  }
  template <class F> void until(F predicate, const char *claim) {
    const auto deadline = Clock::now() + std::chrono::seconds{5};
    while (!predicate()) {
      require(Clock::now() < deadline, claim);
      view->refresh(false);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  }
  void ready() {
    view->toggle();
    until([&] { return view->effective_generation() != 0; },
          "authored composition completes");
  }
  void start(const std::string &key, std::string bytes) {
    jobs->start(key,
                Value::object({{"argv", Value{Value::Array{Value{"/usr/bin/printf"},
                                                           Value{std::move(bytes)}}}},
                               {"timeout_seconds", Value{Number{5}}}}));
    bool terminal = false;
    const auto deadline = Clock::now() + std::chrono::seconds{5};
    while (!terminal) {
      for (const auto &event : jobs->drain()) {
        if (event.bytes)
          jobs->retained(event.job_id, event.offset + event.bytes->size());
        else {
          jobs->settled(event.job_id, event.result);
          terminal = event.job_id == key;
        }
      }
      require(Clock::now() < deadline, "finite native result settles");
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  }
  std::string shown() const {
    std::string text;
    for (const auto &line : view->host().snapshot()->inspectors.front().lines) {
      for (const auto &span : line)
        text += span.text;
      text += '\n';
    }
    return text;
  }
};
void semantic() {
  Fixture f;
  f.start(f.a, "FIRST\\033[31m\\000END");
  f.start(f.b, "SECOND");
  f.ready();
  f.until(
      [&] { return f.view->host().snapshot()->collections.front().items.size() == 2; },
      "two exact native rows");
  const auto scene = f.view->host().snapshot();
  require(scene->nodes.size() == 2 && scene->collections.front().title == "REAL ROWS",
          "Lua authored components adopted");
  for (const auto &item : scene->collections.front().items) {
    require(item.target && item.key == item.target->key &&
                item.target->kind == "command",
            "actual frozen domain targets");
    require(item.key == f.a || item.key == f.b, "no synthetic target row");
  }
  unwrap(f.view->act(tui::InspectorAction::first));
  f.view->refresh(false);
  unwrap(f.view->pin(0, {}, false));
  unwrap(f.view->act(tui::InspectorAction::last));
  f.view->refresh(false);
  unwrap(f.view->pin(1, {}, false));
  f.until(
      [&] {
        return f.shown().find("FIRST") != std::string::npos &&
               f.shown().find("SECOND") != std::string::npos;
      },
      "independent A and B windows");
  if (f.shown().find("\\x1b[31m\\x00END") == std::string::npos)
    std::cerr << f.shown();
  require(f.shown().find("\\x1b[31m\\x00END") != std::string::npos,
          "literal control bytes and NUL suffix preserved");
  unwrap(f.view->act(tui::InspectorAction::focus_detail));
  ChatGrid grid;
  grid.reset(100, 20);
  unwrap(f.view->render(grid, {0, 0, 100, 20}));
  std::string cells;
  for (const auto &cell : grid.cells)
    cells += cell.scalar < 128 ? static_cast<char>(cell.scalar) : '?';
  require(cells.find("Detail target differs from selection") == std::string::npos,
          "two-pin comparison must not claim it is the selected job detail");
  const auto pins = f.shown();
  const auto interrupted = f.view->request_interrupt({}, false);
  require(!interrupted.has_value(), "settled selection cannot request interrupt");
  f.view->toggle();
  f.view->refresh(false);
  f.view->toggle();
  f.view->refresh(false);
  f.until([&] { return f.shown() == pins; }, "route close preserves immutable pins");
}
void mapped_action(const std::filesystem::path &executable) {
  Fixture f;
  for (const auto &key : {f.a, f.b}) {
    f.jobs->start(key,
                  Value::object({{"argv", Value{Value::Array{Value{executable.string()},
                                                             Value{"--child"}}}},
                                 {"timeout_seconds", Value{Number{15}}}}));
  }
  f.until(
      [&] {
        for (const auto &event : f.jobs->drain())
          if (event.bytes)
            f.jobs->retained(event.job_id, event.offset + event.bytes->size());
        const auto cut = f.jobs->try_observe({});
        return cut.status == CommandObservationStatus::ready && cut.row_count == 2 &&
               cut.rows[0].retained == 5005 && cut.rows[1].retained == 5005;
      },
      "two independent live native targets");
  f.ready();
  const auto generation = f.view->effective_generation();
  auto mapped = script(
      "return {compose=function() local c=ui.collection('results','MAPPED') "
      "for i=1,ui.rows() do if ui.key(i)=='" +
      f.b +
      "' then ui.item(c,'friendly-B','B TARGET',i) end end "
      "for i=1,ui.rows() do if ui.key(i)=='" +
      f.a +
      "' then ui.item(c,'friendly-A','A TARGET',i) end end "
      "ui.inspector('comparison','A / B',ui.detail_title(1)..'\\n'..ui.detail(1)) end, "
      "interrupt=function() ui.request('interrupt') end}");
  unwrap(f.view->source({mapped, "mapped.lua", {}, {}, false}, false));
  f.until([&] { return f.view->effective_generation() > generation; },
          "authored mapped rows activate");
  unwrap(f.view->act(tui::InspectorAction::first));
  f.view->refresh(false);
  const auto pinned = f.view->pin(0, {}, false);
  require(pinned.has_value(), "presentation key must resolve actual B target for pin");
  f.until([&] { return f.shown().find(f.b) != std::string::npos; },
          "pin title names B actual target");
  unwrap(f.view->act(tui::InspectorAction::focus_detail));
  f.view->page(true);
  const auto stale_pin = f.view->pin(1, {}, false);
  require(!stale_pin.has_value(), "page change cannot pin previous observed window");
  f.view->page(false);
  f.view->refresh(false);
  unwrap(f.view->request_interrupt({}, false));
  unwrap(f.view->act(tui::InspectorAction::last));
  std::vector<Value> requests;
  f.until(
      [&] {
        auto next = f.jobs->requests();
        requests.insert(requests.end(), next.begin(), next.end());
        return !requests.empty();
      },
      "native bridge consumes actual named handler intent");
  require(requests.size() == 1 && string_field(requests.front(), "job_id") == f.b &&
              string_field(requests.front(), "signal") == "interrupt",
          "selection change after F8 cannot substitute A for frozen B target");
  (void)f.jobs->control(requests.front());
}
std::string run_id(std::size_t index) {
  const auto suffix = std::to_string(index);
  return std::string(32 - suffix.size(), '0') + suffix;
}
void paging(const std::filesystem::path &executable) {
  Fixture f;
  for (std::size_t n = 0; n < 16; ++n)
    f.start(run_id(n), "RUN" + std::to_string(n) + ":" + std::string(5000, 'x'));
  f.jobs->start(run_id(16),
                Value::object({{"argv", Value{Value::Array{Value{executable.string()},
                                                           Value{"--paging-child"}}}},
                               {"timeout_seconds", Value{Number{15}}}}));
  f.until(
      [&] {
        for (const auto &event : f.jobs->drain())
          if (event.bytes)
            f.jobs->retained(event.job_id, event.offset + event.bytes->size());
        CommandObservationQuery query;
        CommandObservationId selected_run;
        const auto key = run_id(16);
        std::copy(key.begin(), key.end(), selected_run.bytes.begin());
        query.selected = selected_run;
        const auto observation = f.jobs->try_observe(query);
        return observation.status == CommandObservationStatus::ready &&
               observation.detail.retained == 5006;
      },
      "live last-page command emits its complete test payload");
  auto mapped = script(
      "return {compose=function() local c=ui.collection('results','COMMAND RUNS') "
      "for i=1,ui.rows() do ui.item(c,'row-'..ui.key(i),ui.label(i),i) end "
      "ui.inspector('comparison','A / B',ui.detail_title(1)..'\\n'..ui.detail(1).."
      "'\\n'..ui.detail_title(2)..'\\n'..ui.detail(2)) end, "
      "interrupt=function() ui.request('interrupt') end}");
  unwrap(f.view->source({mapped, "mapped-paging.lua", {}, {}, false}, false));
  f.ready();
  const auto exact_page = [&](std::size_t begin, std::size_t count) {
    const auto &items = f.view->host().snapshot()->collections.front().items;
    if (items.size() != count)
      return false;
    for (std::size_t n = 0; n < count; ++n)
      if (items[n].key != "row-" + run_id(begin + n) || !items[n].target ||
          items[n].target->key != run_id(begin + n))
        return false;
    return true;
  };
  f.until([&] { return exact_page(0, 8); }, "first eight exact command runs");
  f.view->page(true);
  f.until([&] { return exact_page(8, 8); },
          "forward list paging must expose command runs nine through sixteen");
  f.view->refresh(false);
  unwrap(f.view->act(tui::InspectorAction::first));
  f.view->refresh(false);
  unwrap(f.view->pin(0, {}, false));
  f.until([&] { return f.shown().find("RUN8:") != std::string::npos; },
          "mapped second-page selection pins actual run eight bytes");
  f.view->page(true);
  require(!f.view->pin(1, {}, false).has_value(),
          "pending list page cannot pin old selected output");
  f.view->refresh(false);
  require(!f.view->pin(1, {}, false).has_value(),
          "observed but undisplayed page cannot pin old selected output");
  f.view->page(true);
  f.until([&] { return exact_page(16, 1); },
          "repeated forward keys must not consume prior page continuation");
  f.view->refresh(false);
  unwrap(f.view->act(tui::InspectorAction::first));
  f.view->refresh(false);
  unwrap(f.view->pin(1, {}, false));
  f.until([&] { return f.shown().find("RUN16:") != std::string::npos; },
          "last page pins exact run sixteen");
  const auto both_pins = f.shown();
  f.view->page(true);
  for (unsigned n = 0; n < 5; ++n)
    f.view->refresh(false);
  require(exact_page(16, 1), "last run-list page forward is a no-op");
  f.view->page(false);
  require(!f.view->request_interrupt({}, false).has_value(),
          "pending list page cannot interrupt previously displayed live run");
  require(f.jobs->requests().empty(), "stale page action emits no native request");
  f.until([&] { return exact_page(8, 8); }, "backward list page");
  f.view->page(false);
  f.view->refresh(false);
  f.view->page(true);
  f.view->first_page();
  f.until([&] { return exact_page(0, 8); }, "reset rejects obsolete page composition");
  for (unsigned n = 0; n < 5; ++n)
    f.view->refresh(false);
  f.view->page(false);
  require(exact_page(0, 8), "first run-list page backward is a no-op");
  require(f.shown() == both_pins, "list paging retains both immutable pins");
  unwrap(f.view->act(tui::InspectorAction::first));
  f.view->refresh(false);
  unwrap(f.view->act(tui::InspectorAction::focus_detail));
  f.view->refresh(false);
  f.view->page(true);
  require(!f.view->pin(0, {}, false).has_value(),
          "output page change cannot pin preceding observed bytes");
  f.view->refresh(false);
  unwrap(f.view->pin(0, {}, false));
  f.until([&] { return f.shown().find("bytes 4096..5005") != std::string::npos; },
          "detail paging changes selected output window without changing run list");
  require(exact_page(0, 8) && f.shown().find("RUN16:") != std::string::npos,
          "output paging preserves run list and independent B pin");
  const auto last_good = f.view->host().snapshot();
  unwrap(f.view->source({script("return {compose=function() error('bad paging') end}"),
                         "bad-paging.lua",
                         {},
                         {},
                         false},
                        false));
  unwrap(f.view->act(tui::InspectorAction::focus_collection));
  f.view->page(true);
  f.until(
      [&] { return f.view->notice().find("source failed") != std::string_view::npos; },
      "failed page composition is visible");
  require(f.view->host().snapshot() == last_good,
          "failed page composition preserves exact last-good scene");
  unwrap(f.view->act(tui::InspectorAction::focus_detail));
  unwrap(f.view->act(tui::InspectorAction::focus_collection));
  f.view->page(false);
  f.until([&] { return f.view->host().snapshot() != last_good; },
          "backward recovery recomposes effective source after failed page");
  require(exact_page(0, 8), "failed page cannot strand native navigation");
}
void source_failures() {
  Fixture f;
  f.ready();
  const auto first_generation = f.view->effective_generation();
  const auto first_scene = f.view->host().snapshot();
  unwrap(f.view->source({script("return {compose=function() error('broken') end}"),
                         "bad.lua",
                         {},
                         {},
                         false},
                        false));
  f.until(
      [&] { return f.view->notice().find("source failed") != std::string_view::npos; },
      "bad reload visible");
  require(f.view->effective_generation() == first_generation &&
              f.view->host().snapshot() == first_scene,
          "bad source preserves active scene and generation");
  unwrap(f.view->source({normal(), "preview.lua", {}, {}, true}, false));
  f.until(
      [&] { return f.view->notice().find("Preview ready") != std::string_view::npos; },
      "preview validates");
  require(f.view->effective_generation() == first_generation &&
              f.view->host().snapshot() == first_scene,
          "preview cannot activate scene or handlers");
  unwrap(f.view->source({script("return {compose=function() while true do "
                                "pcall(function() while true do end end) end end}"),
                         "loop.lua",
                         {},
                         {},
                         false},
                        false));
  f.until(
      [&] {
        return f.view->notice().find("instruction budget") != std::string_view::npos;
      },
      "loop yields to native owner");
  unwrap(f.view->act(tui::InspectorAction::focus_detail));
  require(f.view->focused() && f.view->host().snapshot() == first_scene,
          "native focus works after Lua budget refusal");
  unwrap(f.view->source({normal(), "good.lua", {}, {}, false}, false));
  f.until([&] { return f.view->effective_generation() > first_generation; },
          "fresh valid reload recovers");
}
void recovery() {
  Fixture f{true};
  f.view->toggle();
  for (unsigned n = 0; n < 4; ++n)
    f.view->refresh(false);
  require(f.view->effective_generation() == 0 &&
              f.view->host().snapshot()->collections.front().items.empty(),
          "missing source keeps native empty seed and generation zero");
  require(!f.view->request_interrupt({}, false).has_value(),
          "source-less owner cannot dispatch");
  unwrap(f.view->source({normal(), "recovered.lua", {}, {}, false}, false));
  f.until([&] { return f.view->effective_generation() == 1; },
          "valid reload repairs missing startup source");
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char pattern[] = "/tmp/blackbird-instrument-XXXXXX";
    require(::mkdtemp(pattern) != nullptr, "private adapter capture directory");
    path = pattern;
  }
  ~Directory() {
    if (std::uncaught_exceptions()) {
      std::cerr << "retained " << path << '\n';
      return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
template <class T> T take(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "complete native raw field");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
RawEventRef take_ref(ByteView &bytes) {
  return {take<IdentityBytes>(bytes), take<std::uint64_t>(bytes)};
}
std::string_view take_text(ByteView &bytes) {
  const auto size = take<std::size_t>(bytes);
  require(size <= bytes.size(), "complete raw counted text");
  const std::string_view text{reinterpret_cast<const char *>(bytes.data()), size};
  bytes = bytes.subspan(size);
  return text;
}
using Ref = std::pair<IdentityBytes, std::uint64_t>;
Ref ref(RawEventRef value) { return {value.lane, value.ordinal}; }
void isolated_pressure() {
  Directory directory;
  auto runtime =
      unwrap(RawRuntime::open(directory.path.string(), id<EnvironmentId>(81)));
  RawThreadBinding owner{unwrap(runtime->prepare_thread())};
  for (const auto partition : {RawPartition::data, RawPartition::control}) {
    Fixture f;
    f.ready();
    IdentityBytes lane{};
    lane.front() = std::byte{85};
    const auto name = "/bb-ip-" + std::to_string(::getpid()) +
                      (partition == RawPartition::data ? "-d" : "-c");
    auto channel = unwrap(RawChannel::create(name, lane, {1048576, 32768, 4096}));
    {
      RawProducer producer{channel};
      RawThreadBinding ui{producer, lane};
      const auto before = f.view->host().snapshot();
      const auto generation = f.view->effective_generation();
      unwrap(f.view->source({normal(), "pressure-fixture.lua", {}, {}, false}, true));
      std::vector<RawCapacityClaim> data;
      std::vector<RawControlToken> control;
      for (std::uint64_t bytes = 1ULL << 21; bytes; bytes >>= 1) {
        if (partition == RawPartition::data) {
          auto credit = raw_try_claim_data(bytes);
          if (credit.has_value())
            data.push_back(std::move(credit).value());
        } else {
          auto credit = raw_try_claim_control(bytes);
          if (credit.has_value())
            control.push_back(std::move(credit).value());
        }
      }
      const auto exhausted = [&] {
        return partition == RawPartition::data ? !raw_try_claim_data(1).has_value()
                                               : !raw_try_claim_control(1).has_value();
      };
      require(
          exhausted(),
          "actual fixed producer partition exhausted; no consumer can replenish it");
      const auto deadline = Clock::now() + std::chrono::seconds{5};
      while (!f.view->notice().starts_with("Recording capacity")) {
        require(Clock::now() < deadline,
                "actual worker result reaches pressured adoption");
        f.view->refresh(true);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }
      const auto recorded = unwrap(channel.cursors(RawPartition::data)).published;
      unwrap(f.view->act(tui::InspectorAction::focus_detail));
      unwrap(f.view->act(tui::InspectorAction::focus_collection));
      require(f.view->focused(),
              "native focus remains available under recording pressure");
      for (unsigned n = 0; n < 5; ++n) {
        f.view->refresh(true);
        require(exhausted() && f.view->host().snapshot() == before &&
                    f.view->effective_generation() == generation,
                "fixed DATA and CONTROL pressure each preserve exact last-good "
                "scene/source");
        require(
            unwrap(channel.cursors(RawPartition::data)).published == recorded,
            "CONTROL refusal emits no orphan candidate DATA before joint admission");
      }
      data.clear();
      control.clear();
      const auto restored_deadline = Clock::now() + std::chrono::seconds{5};
      while (f.view->effective_generation() == generation) {
        require(Clock::now() < restored_deadline,
                "released capacity admits linked publication");
        f.view->refresh(true);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }
    }
    unwrap(RawChannel::unlink(name));
  }
}
void raw_pressure() {
  Directory directory;
  auto runtime =
      unwrap(RawRuntime::open(directory.path.string(), id<EnvironmentId>(81)));
  std::optional<RawThreadBinding> binding;
  binding.emplace(unwrap(runtime->prepare_thread()));
  RawEventRef original;
  const auto file = directory.path / "source.lua";
  write_file(file, normal()->source());
  const RawAdapterContext context{};
  const auto native_source = read_file(file, 65536, &context, &original);
  auto jobs = std::make_shared<CommandJobs>();
  {
    auto view = unwrap(TuiInstrument::create(
        {id<EnvironmentId>(81), id<ConversationId>(82)}, jobs,
        {script(native_source), file.string(), original, {}, false}));
    view->toggle();
    const auto until = [&](auto predicate, const char *claim) {
      const auto deadline = Clock::now() + std::chrono::seconds{5};
      while (!predicate()) {
        require(Clock::now() < deadline, claim);
        view->refresh(true);
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }
    };
    until([&] { return view->effective_generation() == 1; },
          "actual raw source adopts");
    // A successful preview must not replace the effective source ingress link.
    unwrap(view->source({normal(), file.string(), original, {}, true}, true));
    until([&] { return view->notice().starts_with("Preview ready"); },
          "actual raw preview completes");
    unwrap(view->act(tui::InspectorAction::focus_detail));
    const auto prior_revision = view->host().snapshot()->revision;
    until([&] { return view->host().snapshot()->revision > prior_revision; },
          "effective source recomposes after preview");
  }
  jobs.reset();
  binding.reset();
  runtime.reset();
  const auto runs = unwrap(discover_raw_runs(directory.path.string()));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "actual adapter raw originals close");
  auto reader =
      unwrap(RawRunReader::open((directory.path / "raw" / runs.front().name).string()));
  std::vector<std::byte> storage(2 * 1024 * 1024);
  std::map<Ref, bool> sources;
  std::map<Ref, std::uint64_t> inputs;
  std::map<Ref, Ref> outcomes;
  Ref active{};
  std::vector<std::pair<Ref, Ref>> adoptions;
  std::size_t source_count = 0, adopted = 0;
  bool preview_recompose = false;
  while (const auto record = unwrap(reader->read(storage))) {
    auto bytes = record->raw;
    (void)take<std::uint32_t>(bytes);
    const auto site = take<std::uint32_t>(bytes);
    const RawEventRef current{record->origin.lane, take<std::uint64_t>(bytes)};
    if (site == 4090) {
      require(ref(take_ref(bytes)) == ref(original),
              "source proposal links actual file acquisition");
      (void)take_ref(bytes);
      (void)take_text(bytes);
      const auto preview = take<bool>(bytes);
      (void)take<std::uint64_t>(bytes);
      require(bytes.empty(), "exact source schema");
      sources[ref(current)] = preview;
      if (!source_count++)
        active = ref(current);
    } else if (site == 4092) {
      require(take<IdentityBytes>(bytes) == id<EnvironmentId>(81).bytes() &&
                  take<IdentityBytes>(bytes) == id<ConversationId>(82).bytes(),
              "literal scoped compose input");
      (void)take<std::uint64_t>(bytes);
      (void)take<std::uint64_t>(bytes);
      const auto generation = take<std::uint64_t>(bytes);
      for (unsigned n = 0; n < 4; ++n)
        (void)take<std::uint64_t>(bytes);
      const auto source = ref(take_ref(bytes));
      require(sources.contains(source), "compose links an actually captured proposal");
      if (generation == 1) {
        require(source == active,
                "effective source link survives preview and its proposal reset");
        if (source_count > 1)
          preview_recompose = true;
      }
      inputs[ref(current)] = generation;
    } else if (site == 4093) {
      const auto input = ref(take_ref(bytes));
      require(inputs.contains(input),
              "returned candidate resolves actual submitted input");
      outcomes[ref(current)] = input;
    } else if (site == 4094) {
      const auto outcome = ref(take_ref(bytes)), input = ref(take_ref(bytes));
      adoptions.emplace_back(outcome, input);
      for (unsigned n = 0; n < 2; ++n)
        (void)take<IdentityBytes>(bytes);
      for (unsigned n = 0; n < 7; ++n)
        (void)take<std::uint64_t>(bytes);
      if (take<bool>(bytes))
        ++adopted;
      (void)take<ErrorCode>(bytes);
      (void)take<std::int64_t>(bytes);
      require(bytes.empty(), "exact adoption native slots");
    }
  }
  for (const auto &[outcome, input] : adoptions)
    require(outcomes.contains(outcome) && outcomes.at(outcome) == input,
            "adoption resolves actual returned literal and original input across "
            "partitions");
  require(outcomes.size() == adoptions.size(),
          "CONTROL refusal emits no orphan candidate DATA before its joint admission");
  require(preview_recompose && adopted >= 3 && source_count == 2,
          "source/preview/released pressure actual links all retained");
}
void stale() {
  Fixture f;
  f.start(f.a, "ONE");
  f.start(f.b, "TWO");
  f.ready();
  const auto generation = f.view->effective_generation();
  unwrap(f.view->source({script("return {compose=function() error('obsolete') end}"),
                         "obsolete.lua",
                         {},
                         {},
                         false},
                        false));
  f.view->refresh(false);
  unwrap(f.view->source({normal(), "replacement.lua", {}, {}, false}, false));
  unwrap(f.view->act(tui::InspectorAction::last));
  const auto chosen = unwrap(f.view->host().handle("results"));
  const auto selected_before = unwrap(f.view->host().collection_state(chosen)).selected;
  f.until([&] { return f.view->effective_generation() > generation; },
          "latest source wins over obsolete completion");
  const auto after = unwrap(f.view->host().collection_state(chosen)).selected;
  require(after == selected_before && after.has_value(),
          "stale candidate cannot reset native selection");
  const auto rejected = f.view->pin(3, {}, false);
  require(!rejected.has_value() && rejected.error().code == ErrorCode::stale_handle,
          "invalid pin is explicit refusal");
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc == 2 && (std::string_view{argv[1]} == "--child" ||
                      std::string_view{argv[1]} == "--paging-child")) {
      (void)::signal(SIGINT, SIG_IGN);
      const std::string output =
          (std::string_view{argv[1]} == "--paging-child" ? "RUN16:" : "LIVE\n") +
          std::string(5000, 'x');
      if (::write(STDOUT_FILENO, output.data(), output.size()) !=
          static_cast<ssize_t>(output.size()))
        return 91;
      for (;;)
        (void)::pause();
    }
    require(argc == 2, "one scenario");
    const std::string_view mode{argv[1]};
    if (mode == "semantic")
      semantic();
    else if (mode == "mapping")
      mapped_action(std::filesystem::absolute(argv[0]));
    else if (mode == "paging")
      paging(std::filesystem::absolute(argv[0]));
    else if (mode == "source")
      source_failures();
    else if (mode == "recovery")
      recovery();
    else if (mode == "raw") {
      raw_pressure();
      isolated_pressure();
    } else if (mode == "stale")
      stale();
    else
      throw std::runtime_error{"unknown scenario"};
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << '\n';
  }
  return 1;
}
