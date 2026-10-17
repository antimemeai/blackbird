#include "blackbird/chat_view.hpp"
#include "blackbird/command_center.hpp"
#include "blackbird/context.hpp"
#include "blackbird/display_text.hpp"
#include "blackbird/task_view.hpp"
#include "blackbird/terminal.hpp"
#include "blackbird/tui_inspector.hpp"
#include "blackbird/tui_lua.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <clocale>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace blackbird;
namespace {
void require(bool condition, std::string_view message) {
  if (!condition)
    throw std::runtime_error(std::string{message});
}
void utf8(std::string &out, char32_t scalar) {
  if (scalar < 0x80)
    out += static_cast<char>(scalar);
  else if (scalar < 0x800) {
    out += static_cast<char>(0xc0 | (scalar >> 6));
    out += static_cast<char>(0x80 | (scalar & 63));
  } else if (scalar < 0x10000) {
    out += static_cast<char>(0xe0 | (scalar >> 12));
    out += static_cast<char>(0x80 | ((scalar >> 6) & 63));
    out += static_cast<char>(0x80 | (scalar & 63));
  } else {
    out += static_cast<char>(0xf0 | (scalar >> 18));
    out += static_cast<char>(0x80 | ((scalar >> 12) & 63));
    out += static_cast<char>(0x80 | ((scalar >> 6) & 63));
    out += static_cast<char>(0x80 | (scalar & 63));
  }
}
std::string escaped(std::string_view text) {
  std::string out;
  constexpr char digits[] = "0123456789abcdef";
  for (const char character : text) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte == '\\' || byte == '{' || byte == '}' || byte == '|') {
      out += '\\';
      out += static_cast<char>(byte);
    } else if (byte < 32 || byte == 127) {
      out += "\\x";
      out += digits[byte >> 4];
      out += digits[byte & 15];
    } else
      out += static_cast<char>(byte);
  }
  return out;
}
struct Snapshot {
  std::string name;
  ChatGrid grid;
  std::optional<std::pair<std::size_t, std::size_t>> cursor;
  std::string state;
};
std::string encode(const Snapshot &snapshot) {
  constexpr std::array names{"normal",  "muted",   "user",     "assistant",
                             "heading", "code",    "keyword",  "string",
                             "success", "failure", "selected", "border"};
  const auto &grid = snapshot.grid;
  if (grid.width == 0 || grid.height == 0)
    throw std::runtime_error("snapshot fixtures require nonzero grid geometry");
  require(grid.cells.size() == grid.width * grid.height, "invalid fixture grid");
  std::string out = "styled-grid-v1\nsize " + std::to_string(grid.width) + " " +
                    std::to_string(grid.height) + "\ncursor ";
  if (snapshot.cursor) {
    const auto [row, column] = *snapshot.cursor;
    require(row < grid.height && column < grid.width, "fixture cursor outside grid");
    out += std::to_string(row) + " " + std::to_string(column);
  } else
    out += "none";
  out += "\nstate " + escaped(snapshot.state) + "\n";
  for (std::size_t row = 0; row < grid.height; ++row) {
    out += "|";
    std::size_t column = 0;
    while (column < grid.width) {
      const auto ink = grid.cells[row * grid.width + column].ink;
      const auto index = static_cast<std::size_t>(ink);
      require(index < names.size(), "unrecognized Ink in snapshot");
      out += "{" + std::string{names[index]} + ":";
      do {
        const auto &cell = grid.cells[row * grid.width + column];
        std::string glyph;
        utf8(glyph, cell.scalar);
        require(cell.tail <= grid.tails.size() &&
                    cell.length <= grid.tails.size() - cell.tail,
                "invalid fixture cluster tail");
        glyph.append(grid.tails, cell.tail, cell.length);
        // A visible marker preserves the exact continuation cell and its Ink.
        out +=
            cell.width == 0 ? "<continuation:" + escaped(glyph) + ">" : escaped(glyph);
        ++column;
      } while (column < grid.width && grid.cells[row * grid.width + column].ink == ink);
      out += "}";
    }
    out += "|\n";
  }
  out += "cell-widths";
  for (std::size_t index = 0; index < grid.cells.size(); ++index)
    if (grid.cells[index].width != 1)
      out += " " + std::to_string(index / grid.width) + ":" +
             std::to_string(index % grid.width) + "=" +
             std::to_string(grid.cells[index].width);
  return out + "\n";
}
Snapshot rows(std::string name, const std::vector<ChatRow> &content, std::size_t width,
              std::size_t height) {
  Snapshot result{std::move(name), {}, std::nullopt, "component rows"};
  result.grid.reset(width, height);
  for (std::size_t row = 0; row < std::min(height, content.size()); ++row)
    result.grid.line(row, content[row]);
  return result;
}
InputResult feed(Composer &composer, std::string_view bytes) {
  InputResult result;
  for (const char byte : bytes) {
    auto next = composer.feed(byte);
    if (next.action != InputAction::none)
      result = std::move(next);
  }
  return result;
}
Snapshot composer_frame(std::string name, const Composer &composer, std::size_t width) {
  const auto content = terminal_powerword_lines("> " + composer.text(), width);
  const auto caret =
      terminal_caret("> " + composer.text(), {composer.cursor() + 2}, {width});
  auto result = rows(std::move(name), content, width, 5);
  result.cursor = {caret.row, std::min(caret.column.value, width - 1)};
  result.state = "draft=" + composer.text() +
                 " cursor_bytes=" + std::to_string(composer.cursor()) +
                 " pasted=" + (composer.pasted() ? "true" : "false");
  return result;
}
Snapshot palette_frame(std::string name, const Composer &composer) {
  std::vector<ChatRow> content;
  for (const auto &line : composer.palette_lines(6)) {
    const auto clipped = terminal_lines(line, 44).front();
    const bool selected = clipped.starts_with("> ");
    if (selected || clipped.starts_with("  /")) {
      const auto end = clipped.find(' ', 2);
      const auto split = end == std::string::npos ? clipped.size() : end;
      content.push_back(
          {{clipped.substr(0, split), selected ? Ink::selected : Ink::normal},
           {clipped.substr(split), Ink::muted}});
    } else
      content.push_back(
          {{clipped, line.starts_with("Commands") ? Ink::heading : Ink::muted}});
  }
  auto result = rows(std::move(name), content, 44, 6);
  result.state = "palette=" + std::string{composer.palette_open() ? "open" : "closed"} +
                 " hidden_draft=" + composer.text() +
                 " cursor_bytes=" + std::to_string(composer.cursor());
  return result;
}
Value task(std::string id, std::string title, std::string status,
           std::string parent = {}, std::string blocker = {}, std::string owner = {}) {
  return Value::object({{"id", Value{std::move(id)}},
                        {"title", Value{std::move(title)}},
                        {"status", Value{std::move(status)}},
                        {"parent", Value{std::move(parent)}},
                        {"blocker", Value{std::move(blocker)}},
                        {"owner", Value{std::move(owner)}}});
}
template <std::size_t N> tui::lua::Text<N> instrument_text(std::string_view value) {
  require(value.size() <= N, "instrument fixture fits native text capacity");
  tui::lua::Text<N> result;
  result.size = value.size();
  std::copy(value.begin(), value.end(), result.bytes.begin());
  return result;
}
void instrument_snapshots(std::vector<Snapshot> &output) {
  namespace lua = tui::lua;
  std::ifstream source_file{BLACKBIRD_INSTRUMENT_SOURCE, std::ios::binary};
  require(static_cast<bool>(source_file), "open actual authored instrument source");
  const std::string source{std::istreambuf_iterator<char>{source_file}, {}};
  auto script = unwrap(lua::Script::create(source));
  IdentityBytes environment{}, conversation{}, inbox_identity{};
  environment.fill(std::byte{0x61});
  conversation.fill(std::byte{0x62});
  inbox_identity.fill(std::byte{0x63});
  lua::PresentationInput input{{unwrap(EnvironmentId::from_bytes(environment)),
                                unwrap(ConversationId::from_bytes(conversation)),
                                {81, 1},
                                1,
                                1,
                                0,
                                0,
                                1}};
  constexpr std::array<std::string_view, 2> ids{"11111111111111111111111111111111",
                                                "22222222222222222222222222222222"};
  static constexpr char original_a[] = "A\x1b[31m\0END";
  constexpr std::array<std::string_view, 2> windows{
      std::string_view{original_a, sizeof(original_a) - 1}, "B\tPRESERVED\nB_END"};
  input.row_count = 2;
  for (std::size_t index = 0; index < ids.size(); ++index) {
    input.rows[index] = {
        instrument_text<128>(ids[index]),
        instrument_text<512>(index == 0 ? "A settled exit7" : "B running"),
        {instrument_text<256>("snapshot-fixture"), instrument_text<32>("command"),
         instrument_text<128>(ids[index]), 1}};
    input.detail_size[index] = windows[index].size();
    std::copy(windows[index].begin(), windows[index].end(),
              reinterpret_cast<char *>(input.detail[index].data()));
    input.detail_titles[index] =
        instrument_text<512>("Command run " + std::string{ids[index]} + " bytes 0.." +
                             std::to_string(windows[index].size()) + " of observed " +
                             std::to_string(windows[index].size()));
  }
  auto inbox = unwrap(lua::ActionInbox::create(inbox_identity));
  auto worker = unwrap(lua::PresentationOwner::create(inbox));
  const auto until = [](auto predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!predicate()) {
      require(std::chrono::steady_clock::now() < deadline,
              "finite authored instrument snapshot completion");
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
  };
  until([&] {
    return worker->try_compose({script, input, false}) == lua::SubmitStatus::accepted;
  });
  lua::Outcome outcome{input.stamp};
  until([&] { return worker->try_take(outcome) == lua::PollStatus::ready; });
  require(outcome.status == lua::Completion::candidate && outcome.candidate &&
              outcome.accepted_requests == 0,
          "actual authored compose returns a candidate without action admission");
  std::optional<lua::ActionPage> page;
  until([&] {
    const auto result = inbox->try_read();
    if (result.has_value())
      page = result.value();
    return page.has_value();
  });
  if (!page)
    throw std::runtime_error{"actual authored inbox observation missing"};
  require(page->size == 0, "pure authored composition leaves native inbox empty");
  auto host = unwrap(tui::SceneHost::create(input.stamp.view, lua::scene_limits));
  unwrap(lua::publish(outcome, input.stamp, host));
  auto control = unwrap(tui::CollectionInspector::create(
      host, "results", "comparison", tui::InspectorBinding::independent));
  unwrap(host.position(unwrap(host.handle("results")), ids[1], ids[0]));
  const auto selection = unwrap(control.state());
  require(selection.selected && *selection.selected == ids[1],
          "independent comparison keeps selected native row B identity");
  const auto check_grid = [&](const ChatGrid &grid) {
    std::string shown, compact;
    for (const auto &cell : grid.cells) {
      utf8(shown, cell.scalar);
      if (cell.scalar != U' ')
        utf8(compact, cell.scalar);
    }
    require(shown.find("A\\x1b[31m\\x00END") != std::string::npos &&
                shown.find("B  PRESERVED") != std::string::npos &&
                shown.find("B_END") != std::string::npos,
            "rendered authored comparison retains literal escaped source suffixes");
    require(compact.find(ids[0]) != std::string::npos &&
                compact.find(ids[1]) != std::string::npos,
            "rendered comparison identifies both original synthetic pinned windows");
    require(shown.find("Detail target differs") == std::string::npos,
            "independent comparison does not claim selected-detail mismatch");
  };
  Snapshot wide{
      "instrument-lua-wide",
      {},
      {},
      "synthetic-inputs=2;source=results.lua;selected=B;anchor=A;focus=collection;"
      "binding=independent;intents=0;editor=external"};
  wide.grid.reset(100, 16);
  unwrap(control.render(wide.grid, {0, 0, 100, 16}));
  check_grid(wide.grid);
  output.push_back(std::move(wide));
  unwrap(control.act(tui::InspectorAction::focus_detail));
  Snapshot narrow{
      "instrument-lua-narrow",
      {},
      {},
      "synthetic-inputs=2;source=results.lua;selected=B;anchor=A;focus=detail;"
      "binding=independent;intents=0;editor=external"};
  narrow.grid.reset(32, 16);
  unwrap(control.render(narrow.grid, {0, 0, 32, 16}));
  check_grid(narrow.grid);
  output.push_back(std::move(narrow));
}
std::vector<Snapshot> scenarios() {
  std::vector<Snapshot> output;
  ChatView joiners;
  joiners.append(ChatKind::assistant, "A\u200dB\u200dC \u200d \u200d XZ");
  output.push_back(rows("conversation-plain-joiners", joiners.rows(12), 12, 5));

  ChatView conversation;
  conversation.append(ChatKind::user, "Inspect the parser.");
  conversation.append(ChatKind::assistant,
                      "# Findings\nUse `read` before changing code.\n"
                      "```cpp\nauto result = \"ready\";\n```\n");
  conversation.append(ChatKind::tool, "check-parser: exit 1");
  conversation.append(ChatKind::error, "Result remains unresolved.");
  output.push_back(rows("conversation-ordinary", conversation.rows(52), 52, 20));
  output.push_back(rows("conversation-narrow", conversation.rows(24), 24, 24));

  Composer editor;
  editor.draft("abcd\nx\n12345");
  feed(editor, "\x1b[A\x1b[A");
  require(editor.cursor() == 4, "multiline sticky-column cursor changed");
  output.push_back(composer_frame("composer-multiline", editor, 20));
  Composer pasted;
  feed(pasted, "\x1b[200~/hel\t\n界é\x1b[201~");
  require(pasted.text() == "/hel\t\n界é", "paste became a command or completion");
  output.push_back(composer_frame("composer-literal-paste", pasted, 20));
  Composer emoji;
  emoji.draft("ab👩‍💻X");
  output.push_back(composer_frame("composer-joined-emoji", emoji, 6));
  output.push_back(rows("display-suffix-boundaries",
                        {{{"A️X A🏽X A⃣X", Ink::normal}},
                         {{"e", Ink::heading}, {"́X", Ink::normal}},
                         {{"́X ​X", Ink::normal}}},
                        24, 4));
  Composer unicode;
  unicode.draft("a界éz\nx\n1234");
  feed(unicode, "\x1b[A\x1b[A");
  output.push_back(composer_frame("composer-unicode", unicode, 12));

  auto parent = task("t1", "Build panel", "active", "", "", "operator");
  parent.object().emplace_back("subtasks", Value{Number{1}});
  parent.object().emplace_back("subtasks_done", Value{Number{1}});
  const auto page = Value::object(
      {{"title", Value{"Parser work"}},
       {"bead", Value{"demo-17"}},
       {"counts", Value::object({{"done", Value{Number{0}}},
                                 {"active", Value{Number{1}}},
                                 {"blocked", Value{Number{1}}},
                                 {"queued", Value{Number{1}}}})},
       {"items", Value{Value::Array{parent, task("t2", "Read 界", "done", "t1"),
                                    task("t3", "Investigate failure", "blocked", "",
                                         "Need exact original", "Kimi"),
                                    task("t4", "Compare results", "queued")}}},
       {"next", Value{}}});
  const std::map<std::string, Value> activity{
      {"t1", Value::object({{"phase", Value{"running"}}})}};
  output.push_back(rows("tasks-ordinary", task_pane_rows(page, 36, activity), 36, 18));
  output.push_back(rows("tasks-narrow", task_pane_rows(page, 18, activity), 18, 24));
  output.push_back(rows("tasks-tiny", task_pane_rows(page, 7, activity), 7, 3));

  Composer views;
  views.draft("preserve this draft");
  feed(views, "\x1b[D\x1b[D");
  views.views(tui::ViewRoute::command_center);
  feed(views, "command runs");
  output.push_back(palette_frame("view-picker", views));
  const auto route = feed(views, "\r");
  require(route.action == InputAction::select_view &&
              route.view == tui::ViewRoute::command_runs &&
              route.text == "view.command-runs" &&
              views.text() == "preserve this draft" && views.cursor() == 17,
          "view picker must emit a local route and preserve draft/cursor");

  Composer palette;
  palette.draft("unsent operator draft");
  palette.choices(
      {{"/resume /fixture/alpha", "", "first retained session", "Session", "Alpha"},
       {"/resume /fixture/beta", "", "second retained session", "Session", "Beta"}},
      "Sessions");
  feed(palette, "\x1b[B");
  output.push_back(palette_frame("palette-selected", palette));
  const auto selected = feed(palette, "\r");
  require(selected.text == "/resume /fixture/beta" &&
              selected.action == InputAction::choose_command &&
              palette.text() == "unsent operator draft",
          "palette selection changed target or hidden draft");
  output.push_back(composer_frame("palette-return-draft", palette, 32));
  palette.choices(
      {{"/resume /fixture/alpha", "", "first retained session", "Session", "Alpha"}},
      "Sessions");
  feed(palette, "zzzz");
  output.push_back(palette_frame("palette-no-match", palette));
  auto scene = unwrap(tui::SceneHost::create({71, 1}, {8, 128, 8192}));
  tui::SceneCandidate instrument;
  instrument.view = {71, 1};
  instrument.nodes = {{"list", {}, tui::NodeKind::collection, true},
                      {"detail", {}, tui::NodeKind::inspector, true}};
  const tui::TargetRef alpha{"fixture", "command", "alpha", 1};
  const tui::TargetRef beta{"fixture", "command", "beta", 1};
  instrument.collections = {
      {"list",
       {{"alpha", "alpha  settled", alpha}, {"beta", "beta   running", beta}},
       "COMMANDS",
       {}}};
  instrument.inspectors = {{"detail",
                            "alpha · retained result",
                            alpha,
                            {{{"Exit 7 | native reaped", Ink::failure}},
                             {{"Retained 30 | received 30", Ink::muted}},
                             {{"same source · e", Ink::normal}, {"́ 界", Ink::code}},
                             {{"line 2: actual bounded content", Ink::normal}},
                             {{"line 3: more readable output", Ink::normal}},
                             {{"line 4: last line", Ink::normal}}},
                            {},
                            0}};
  unwrap(scene.publish(instrument));
  auto instrument_control =
      unwrap(tui::CollectionInspector::create(scene, "list", "detail"));
  Snapshot wide_inspector{
      "inspector-wide", {}, {}, "selected=alpha;target=alpha;draft=external"};
  wide_inspector.grid.reset(84, 10);
  unwrap(instrument_control.render(wide_inspector.grid, {0, 0, 84, 10}));
  output.push_back(std::move(wide_inspector));
  unwrap(instrument_control.act(tui::InspectorAction::focus_detail));
  Snapshot narrow_inspector{
      "inspector-narrow", {}, {}, "selected=alpha;focus=detail;target=alpha"};
  narrow_inspector.grid.reset(24, 8);
  unwrap(instrument_control.render(narrow_inspector.grid, {0, 0, 24, 8}));
  output.push_back(std::move(narrow_inspector));

  TaskState center_tasks;
  center_tasks.commit(center_tasks.prepare(Value::object(
      {{"op_id", Value{"center-snapshot"}},
       {"base", Value{Number{"0"}}},
       {"ops", Value{Value::Array{Value::object(
                   {{"op", Value{"add"}},
                    {"title", Value{"Investigate retained result"}},
                    {"status", Value{"blocked"}},
                    {"owner", Value{"declared label"}},
                    {"blocker", Value{"Await independent review"}},
                    {"note", Value{"First source line\nSecond source line\n"
                                   "Third source line\nFourth source line"}}})}}}})));
  IdentityBytes center_environment{}, center_conversation{};
  center_environment.fill(std::byte{0x31});
  center_conversation.fill(std::byte{0x52});
  CommandCenter center{{unwrap(EnvironmentId::from_bytes(center_environment)),
                        unwrap(ConversationId::from_bytes(center_conversation))}};
  bool center_busy = false;
  CommandCenterSources center_sources;
  center_sources.tasks = [&](const TaskObservationQuery &query) {
    TaskPublishedObservation observed;
    observed.publication =
        center_busy ? TaskPublicationStatus::busy : TaskPublicationStatus::ready;
    if (!center_busy) {
      observed.value = center_tasks.observe(query);
      observed.origin.revision = center_tasks.revision();
    }
    return observed;
  };
  center.toggle();
  (void)center.refresh(center_sources, false);
  (void)center.refresh(center_sources, false);
  Snapshot wide_center{
      "center-wide",
      {},
      {},
      "route=center;selected-kind=task;selected-id=t1;declared=blocked;"
      "participants=unavailable;commands=unavailable"};
  wide_center.grid.reset(100, 16);
  wide_center.grid.line(1, {{"COMMAND CENTER", Ink::heading}});
  unwrap(center.render(wide_center.grid, {0, 2, 100, 14}));
  output.push_back(std::move(wide_center));
  unwrap(center.act(tui::InspectorAction::focus_detail));
  center_busy = true;
  (void)center.refresh(center_sources, false);
  Snapshot narrow_center{"center-narrow-stale",
                         {},
                         {},
                         "route=center;selected-kind=task;selected-id=t1;focus=detail;"
                         "tasks=stale-last-good;declared=blocked"};
  narrow_center.grid.reset(32, 12);
  narrow_center.grid.line(
      1, {{"COMMAND CENTER | " + std::string{center.notice()}, Ink::heading}});
  unwrap(center.render(narrow_center.grid, {0, 2, 32, 10}));
  output.push_back(std::move(narrow_center));
  instrument_snapshots(output);
  return output;
}
} // namespace
int main(int argc, char **argv) {
  try {
    require(argc == 2, "usage: tui_snapshot_test OUTPUT_DIRECTORY");
    require(std::setlocale(LC_CTYPE, "en_US.UTF-8") != nullptr ||
                std::setlocale(LC_CTYPE, "C.UTF-8") != nullptr,
            "snapshot fixtures require a UTF-8 locale");
    const std::filesystem::path directory{argv[1]};
    std::filesystem::create_directories(directory);
    const auto snapshots = scenarios();
    for (const auto &snapshot : snapshots) {
      std::ofstream file{directory / (snapshot.name + ".snap"), std::ios::binary};
      require(static_cast<bool>(file), "cannot create actual snapshot");
      file << encode(snapshot);
      file.flush();
      require(static_cast<bool>(file), "cannot write actual snapshot");
    }
    std::cout << snapshots.size() << " deterministic styled snapshots\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
