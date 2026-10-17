#include "blackbird/json.hpp"
#include "blackbird/terminal.hpp"
#include "blackbird/tools.hpp"
#include <clocale>
#include <cstdlib>
#include <iostream>
#include <poll.h>
#include <source_location>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
using namespace blackbird;
void check(bool good, std::source_location where = std::source_location::current()) {
  if (!good)
    throw std::runtime_error("terminal contract at line " +
                             std::to_string(where.line()));
}
InputResult feed(Composer &c, std::string_view bytes) {
  InputResult result;
  for (char byte : bytes) {
    auto r = c.feed(byte);
    if (r.action != InputAction::none)
      result = std::move(r);
  }
  return result;
}
int main() {
  {
    Composer picker;
    picker.draft("unsent draft");
    picker.choices({{"/resume /tmp/a", "", "/tmp/a", "Session", "Alpha"},
                    {"/resume /tmp/b", "", "/tmp/b", "Session", "Beta"}},
                   "Sessions");
    check(picker.palette_lines(8)[0].starts_with("Sessions"));
    check(feed(picker, "\x1b[B\r").text == "/resume /tmp/b");
    check(picker.text() == "unsent draft");
    picker.choices({{"/resume /tmp/a", "", "/tmp/a", "Session", "Alpha"}}, "Sessions");
    feed(picker, "\x1b");
    check(picker.flush_escape() && picker.text() == "unsent draft");
    Composer question;
    question.draft("EFFORT_DRAFT");
    feed(question, "\x1b[D");
    const auto caret = question.cursor();
    question.choices(
        {{"/effort low", "", "Use low effort", "Effort", "low", {}, true},
         {"/effort high", "", "Use high effort", "Effort", "high", {}, true}},
        "What level of effort did you intend?");
    check(question.palette_lines(6)[0].starts_with("What level of effort"));
    check(question.palette_lines(6)[3].find("Enter answers") != std::string::npos);
    const auto answered = feed(question, "\x1b[B\r");
    check(answered.action == InputAction::answer_question &&
          answered.text == "/effort high" && question.text() == "EFFORT_DRAFT" &&
          question.cursor() == caret);
    Composer views;
    views.draft("VIEW_DRAFT_SENTRY");
    feed(views, "\x1b[D\x1b[D");
    const auto view_cursor = views.cursor();
    const auto open_views = feed(views, "\x1c");
    check(open_views.action == InputAction::select_view &&
          open_views.text == "view.picker");
    views.views(tui::ViewRoute::conversation);
    check(views.palette_open());
    check(views.palette_lines(8)[0].starts_with("Views"));
    feed(views, "command runs");
    const auto view_choice = feed(views, "\r");
    check(view_choice.view == tui::ViewRoute::command_runs);
    check(view_choice.text == "view.command-runs" &&
          view_choice.action != InputAction::choose_command &&
          view_choice.action != InputAction::submit);
    check(views.text() == "VIEW_DRAFT_SENTRY" && views.cursor() == view_cursor);
    views.views(tui::ViewRoute::command_center);
    const auto inspecting_feed = [&](std::string_view bytes) {
      InputResult result;
      for (const auto byte : bytes) {
        auto next = views.feed(byte, nullptr, true);
        check(next.action != InputAction::instrument_action &&
              next.action != InputAction::inspect_next);
        if (next.action != InputAction::none)
          result = std::move(next);
      }
      return result;
    };
    inspecting_feed("\x1b[19~\x1b[B");
    const auto modal_choice = inspecting_feed("\r");
    check(modal_choice.view == tui::ViewRoute::command_runs);
    check(views.text() == "VIEW_DRAFT_SENTRY" && views.cursor() == view_cursor);
    views.views(tui::ViewRoute::result_comparison);
    inspecting_feed("\x1b");
    const auto dismissal =
        terminal_input_dispatch(views, std::chrono::steady_clock::time_point::max(),
                                std::chrono::steady_clock::time_point::min(), 0);
    check(dismissal.redraw && !dismissal.focus_editor && !views.palette_open());
    check(views.text() == "VIEW_DRAFT_SENTRY" && views.cursor() == view_cursor);
    Composer literal_view_key;
    feed(literal_view_key, "\x1b[200~\x1c\x1b[201~");
    check(literal_view_key.text() == "\x1c" && !literal_view_key.palette_open());
    Composer optional;
    check(feed(optional, "/runs\r").text == "/runs");
    Composer slash;
    feed(slash, "/");
    check(!slash.palette_lines(8).empty());
    feed(slash, "stats");
    check(slash.palette_lines(8)[1].find("/stats") != std::string::npos);
    check(feed(slash, "\r").text == "/stats");
    feed(slash, "/ne\t");
    check(slash.text() == "/new");
    check(feed(slash, "\r").text == "/new");
    feed(slash, "/mod\t");
    check(slash.text() == "/model ");
    check(slash.palette_lines(8).empty());
    slash.draft("");
    feed(slash, "/\x1b[B\t");
    check(slash.text() == "/keys");
    slash.draft("");
    feed(slash, "/\x1b");
    check(slash.flush_escape());
    check(slash.text() == "/" && slash.palette_lines(8).empty());
    slash.draft("");
    feed(slash, "\x1b[200~/stats\x1b[201~");
    check(slash.text() == "/stats" && slash.palette_lines(8).empty());
    slash.draft("/stats");
    check(slash.palette_lines(8).empty());
    slash.draft("");
    feed(slash, "/");
    check(feed(slash, "\x03").action == InputAction::cancel);
    slash.draft("");
    feed(slash, "/model\r");
    check(slash.text() == "/model ");
    feed(slash, "custom");
    check(feed(slash, "\r").text == "/model custom");
    feed(slash, "/statx\x7f");
    check(slash.palette_lines(8)[1].find("/stats") != std::string::npos);
    slash.draft("");
    feed(slash, "/tmp/file");
    check(slash.palette_lines(8).empty());
    slash.draft("");
    feed(slash, "/zzzz");
    check(feed(slash, "\r").text == "/zzzz");
  }
  try {
    (void)std::setlocale(LC_CTYPE, "en_US.UTF-8");
    for (const auto prefix : {std::string_view{"\x1b"}, std::string_view{"\x1b["},
                              std::string_view{"\x1b[1;"}, std::string_view{"\x1bO"}}) {
      using Clock = std::chrono::steady_clock;
      const auto deadline = Clock::time_point{std::chrono::milliseconds{50}};
      for (const short events :
           {short{0}, short{POLLIN}, short{POLLHUP}, short{POLLERR}}) {
        Composer polled;
        polled.draft("draft");
        feed(polled, prefix);
        const auto early = terminal_input_dispatch(
            polled, deadline - std::chrono::milliseconds{1}, deadline, events);
        check(early.read_input == (events != 0) && !early.redraw &&
              polled.escape_pending());
        const auto due = terminal_input_dispatch(polled, deadline, deadline, events);
        check(due.read_input == (events != 0) && due.redraw &&
              !polled.escape_pending());
        feed(polled, "x");
        check(polled.text() == "draftx");
      }
      Composer expired;
      expired.draft("draft");
      feed(expired, "\x1b[D");
      feed(expired, prefix);
      check(expired.escape_pending());
      check(expired.flush_escape()); // The owner has observed the deadline.
      check(!expired.escape_pending() && !expired.flush_escape());
      check(expired.text() == "draft" && expired.cursor() == 4);
      feed(expired, "X");
      check(expired.text() == "drafXt" && expired.cursor() == 5);
      check(feed(expired, "\x03").action == InputAction::cancel);
    }
    Composer fragmented;
    fragmented.draft("one two");
    for (const char byte : std::string_view{"\x1b[1;5D"})
      check(fragmented.feed(byte).action == InputAction::none);
    check(!fragmented.escape_pending() && fragmented.cursor() == 4);
    for (const char byte : std::string_view{"\x1bOH"})
      check(fragmented.feed(byte).action == InputAction::none);
    check(!fragmented.escape_pending() && fragmented.cursor() == 0);
    feed(fragmented, "\x1b"
                     "f");
    check(fragmented.cursor() == 3);
    check(feed(fragmented, "\x1b[6~").action == InputAction::page_down);
    Composer paste_timeout;
    feed(paste_timeout, "\x1b[200~/stats\r\x03");
    check(paste_timeout.pasted() && paste_timeout.text() == "/stats\n\x03");
    // Fragmented paste termination stays recognized even when deadline service
    // occurs between its bytes. Pasted controls remain literal until termination.
    for (const char byte : std::string_view{"\x1b[201~"}) {
      check(paste_timeout.feed(byte).action == InputAction::none);
      check(!paste_timeout.flush_escape());
    }
    check(!paste_timeout.pasted() && !paste_timeout.escape_pending());
    check(feed(paste_timeout, "\r").text == "/stats\n\x03");
    Composer palette_key;
    palette_key.draft("unsent draft");
    feed(palette_key, std::string_view{"\0stats", 6});
    check(palette_key.text() == "unsent draft"); // Search never edits the hidden draft.
    check(palette_key.palette_open());
    check(palette_key.palette_lines(4)[1].starts_with("> /stats"));
    const auto selected = feed(palette_key, "\r");
    check(selected.action == InputAction::choose_command && selected.text == "/stats");
    check(!palette_key.palette_open() && palette_key.text() == "unsent draft");
    feed(palette_key, "\x14SESSION");
    check(palette_key.palette_lines(4)[1].find("/session") != std::string::npos);
    feed(palette_key, "\x1b");
    check(palette_key.flush_escape() && !palette_key.palette_open());
    check(palette_key.text() == "unsent draft" && palette_key.cursor() == 12);
    feed(palette_key, std::string_view{"\0zzzz", 5});
    check(palette_key.palette_lines(4)[1].find("No matching") != std::string::npos);
    check(feed(palette_key, "\r").action == InputAction::none &&
          palette_key.palette_open());
    feed(palette_key, "\x03");
    feed(palette_key, "\x14\x1b[200~stats\r\x07\x1b[201~");
    check(palette_key.palette_open() && palette_key.text() == "unsent draft");
    check(palette_key.palette_lines(1).size() == 1 &&
          palette_key.palette_lines(2).size() == 2);
    feed(palette_key, "\x14\x14"); // Close, reopen full catalog.
    feed(palette_key, "\x1b[B\x1b[B");
    const auto moved = palette_key.palette_lines(4);
    check(moved[2].starts_with("> /commands"));
    feed(palette_key, "\x14");
    check(!palette_key.palette_open() && palette_key.text() == "unsent draft");
    Composer editor_key;
    feed(editor_key, "draft");
    check(feed(editor_key, "\x07").action != InputAction::none &&
          editor_key.text() == "draft");
    Composer completion;
    feed(completion, "/hel\t");
    check(completion.text() == "/help" && completion.cursor() == 5);
    feed(completion, "\x15/mod\t");
    check(completion.text() == "/model ");
    feed(completion, "gpt-example\t");
    check(completion.text() == "/model gpt-example");
    feed(completion, "\x15/dra\t");
    check(completion.text() == "/drafts"); // Tab fills the visible selected row.
    feed(completion, "\x15/draft");
    feed(completion, "\t");
    check(completion.text() == "/draft ");
    feed(completion, "\x15/co\t");
    check(completion.text() == "/commands"); // Selection is shown before filling.
    feed(completion, "\x15/unknown\t");
    check(completion.text() == "/unknown");
    feed(completion, "\x15/hel\x1b[D\t");
    check(completion.text() == "/hel" && completion.cursor() == 3);
    feed(completion, "\x15\x1b[200~/hel\t\x1b[201~");
    check(completion.text() == "/hel\t"); // Paste is literal, never completion.
    check(terminal_command_hint("/co").find("/compact") != std::string::npos);
    check(terminal_command_hint("/co").find("/context") != std::string::npos);
    check(terminal_command_hint("/model foo").find("NAME") != std::string::npos);
    check(terminal_command_hint("/unknown").starts_with("Unknown command"));
    check(terminal_command_hint("ordinary draft").empty());
    check(terminal_command_hint("/lua\ncode").empty());
    check(terminal_help().find("/sessions") != std::string::npos);
    check(terminal_help().find("not model context") != std::string::npos);
    check(terminal_key_help().find("keep unsent draft") != std::string::npos);
    Composer multiline;
    feed(multiline, "history\r");
    multiline.draft("abcd\nx\n12345");
    feed(multiline, "\x1b[A");
    check(multiline.text() == "abcd\nx\n12345" && multiline.cursor() == 6);
    feed(multiline, "\x1b[A");
    check(multiline.cursor() == 4); // Sticky column survives the short middle line.
    feed(multiline, "\x1b[B\x1b[B");
    check(multiline.cursor() == 12);
    feed(multiline, "\x1b[1;5H");
    check(multiline.cursor() == 0);
    feed(multiline, "\x10"); // Explicit history, even for multiline drafts.
    check(multiline.text() == "history");
    feed(multiline, "\x0e");
    check(multiline.text() == "abcd\nx\n12345" && multiline.cursor() == 0);
    feed(multiline, "\x1b[B\x05");
    check(multiline.cursor() == 6); // Ctrl-E ends this line, not the whole draft.
    feed(multiline, "\x01");
    check(multiline.cursor() == 5);
    Composer words;
    words.draft("one é界 two");
    feed(words, "\x1b"
                "b");
    check(words.cursor() == 10);
    feed(words, "\x17");
    check(words.text() == "one two" && words.cursor() == 4);
    feed(words, "\x19");
    check(words.text() == "one é界 two" && words.cursor() == 10);
    feed(words, "\x1b[1;5H\x1b[1;5C");
    check(words.cursor() == 3);
    feed(words, "\x1b"
                "d");
    check(words.text() == "one two");
    words.draft("first\nsecond");
    feed(words, "\x1b[1;5H\x05\x0b"); // At line end, kill the newline.
    check(words.text() == "firstsecond");
    feed(words, "\x19");
    check(words.text() == "first\nsecond");
    Composer columns;
    columns.draft("a界z\nx\n1234");
    feed(columns, "\x1b[A\x1b[A");
    check(columns.cursor() == 5); // Four terminal cells, not four UTF-8 bytes.
    feed(columns, "\x1b[D\x1b[B\x1b[B");
    check(columns.cursor() ==
          columns.text().size() - 1); // Horizontal motion resets goal.
    columns.draft("a\n\nb");
    feed(columns, "\x1b[A");
    check(columns.cursor() == 2);
    feed(columns, "\x1b[A");
    check(columns.cursor() == 1);
    columns.draft("abc\ndef");
    feed(columns, "\x01\x1b[A");
    check(columns.cursor() == 0);
    feed(columns, "\x0b");
    check(columns.text() == "\ndef");
    feed(columns, "\x19");
    check(columns.text() == "abc\ndef" && columns.cursor() == 3);
    feed(columns, "\x15\x19");
    check(columns.text() == "abc\ndef");
    columns.draft("界\nx");
    feed(columns, "\x1b[A");
    check(columns.cursor() == 0); // Can't place the cursor inside a wide glyph.
    feed(columns, "\x1b[B");
    check(columns.cursor() == columns.text().size());
    columns.draft("a\tb\n123");
    feed(columns, "\x1b[A");
    check(columns.cursor() == 2); // Pasted tab has the renderer's two-cell width.
    columns.draft("éx\nab");
    feed(columns, "\x1b[A");
    check(columns.cursor() == 4); // Combining mark contributes no extra cell.
    columns.draft(std::string{"\xff\nabcd", 6});
    feed(columns, "\x1b[A");
    check(columns.cursor() == 1); // Invalid byte is displayed as four escaped cells.
    columns.restore({"", 0, {}, {}});
    feed(columns, "\x19");
    check(columns.text().empty()); // Restore does not inherit a prior kill buffer.
    Composer full;
    full.draft(std::string(1024 * 1024, 'a'));
    feed(full, "\x17");
    check(full.text().empty());
    full.draft("b");
    feed(full, "\x19");
    check(full.text() == "b" && full.cursor() == 1); // Oversized yank is atomic/no-op.
    Composer literal;
    feed(literal, "\x1b[200~a\x17\x0b\x19\x10\x0e\x1b[201~");
    check(literal.text() == "a\x17\x0b\x19\x10\x0e");
    Composer c;
    feed(c, "aéz");
    feed(c, "\x1b[D\x7f");
    check(c.text() == "az" && c.cursor() == 1);
    feed(c, "b");
    check(feed(c, "\r").text == "abz");
    feed(c, "draft\x1b[A");
    check(c.text() == "abz");
    feed(c, "\x1b[B");
    check(c.text() == "draft");
    check(feed(c, "\x03").action == InputAction::cancel && c.text() == "draft");
    feed(c, "\x15");
    feed(c, "\x1b[200~one\ntwo\rthree\x1b[201~");
    check(c.text() == "one\ntwo\nthree");
    check(feed(c, "\r").text == "one\ntwo\nthree");
    feed(c, "x\x1b\ry");
    check(c.text() == "x\ny");
    feed(c, "\x15");
    check(feed(c, "\x04").action == InputAction::quit);
    check(terminal_lines("é界x", 3) == std::vector<std::string>{"é界", "x"});
    check(terminal_lines("x\x1b[31m", 20)[0] == "x\\x1b[31m");
    check(terminal_lines("a\nb", 20) == std::vector<std::string>{"a", "b"});
    // Old end=90 (100-10). Append20, trim10: same content end is80,
    // so new110 lines require scroll30, NOT net-growth scroll20.
    check(terminal_scroll_after_output({.scroll = 10,
                                        .previous = 100,
                                        .before_trim = 120,
                                        .after_trim = 110,
                                        .height = 10}) == 30);
    check(terminal_scroll_after_output({.scroll = 0,
                                        .previous = 100,
                                        .before_trim = 120,
                                        .after_trim = 110,
                                        .height = 10}) == 0);
    check(terminal_scroll_after_output({.scroll = 80,
                                        .previous = 100,
                                        .before_trim = 120,
                                        .after_trim = 70,
                                        .height = 10}) == 60);
    check(terminal_scroll_after_output({.scroll = 10,
                                        .previous = 100,
                                        .before_trim = 120,
                                        .after_trim = 120,
                                        .height = 10}) == 30);
    std::string long_line(2 * 1024 * 1024, 'x');
    long_line += "éEND\n";
    trim_terminal_transcript(long_line);
    check(long_line.size() <= 1024 * 1024 && long_line.ends_with("éEND\n") &&
          long_line.starts_with("[Older display trimmed"));
    std::string unicode_line;
    for (std::size_t n = 0; n < 600000; ++n)
      unicode_line += "é";
    trim_terminal_transcript(unicode_line);
    check(unicode_line.size() <= 1024 * 1024 &&
          dump_json(Value{unicode_line}).has_value());
    const char *old_visual = std::getenv("VISUAL");
    const char *old_editor = std::getenv("EDITOR");
    const std::optional<std::string> visual =
        old_visual ? std::optional<std::string>{old_visual} : std::nullopt;
    const std::optional<std::string> editor =
        old_editor ? std::optional<std::string>{old_editor} : std::nullopt;
    check(setenv("VISUAL", "sh -c 'printf edited > \"$1\"' --", 1) == 0);
    auto result = edit_terminal_draft("original");
    check(result.accepted && result.text == "edited");
    check(setenv("VISUAL", "sh -c 'printf bad > \"$1\"; exit 7' --", 1) == 0);
    check(!edit_terminal_draft("original").accepted);
    check(setenv("VISUAL", "no-such-arco-editor-123", 1) == 0);
    check(!edit_terminal_draft("original").accepted);
    check(setenv("VISUAL", "sh -c 'rm \"$1\"; ln -s /dev/null \"$1\"' --", 1) == 0);
    check(!edit_terminal_draft("original").accepted);
    check(setenv("VISUAL", "sh -c 'rm \"$1\"; mkfifo \"$1\"' --", 1) == 0);
    check(!edit_terminal_draft("original").accepted);
    check(setenv("VISUAL",
                 "sh -c 'dd if=/dev/zero of=\"$1\" bs=1024 count=1025 2>/dev/null' --",
                 1) == 0);
    check(!edit_terminal_draft("original").accepted);
    check(setenv("VISUAL", "", 1) == 0);
    check(setenv("EDITOR", "sh -c 'printf fallback > \"$1\"' --", 1) == 0);
    check(edit_terminal_draft("original").text == "fallback");
    if (visual)
      check(setenv("VISUAL", visual->c_str(), 1) == 0);
    else
      check(unsetenv("VISUAL") == 0);
    if (editor)
      check(setenv("EDITOR", editor->c_str(), 1) == 0);
    else
      check(unsetenv("EDITOR") == 0);
    char path[] = "/tmp/arco-ui-state-XXXXXX";
    const auto dir = mkdtemp(path);
    check(dir != nullptr);
    const auto file = std::filesystem::path{dir} / "ui-state.bbm";
    TerminalState state{std::string{"x\xff\0", 3}, 1, {"sent"}, {"queued"}};
    save_terminal_state(file, state);
    const auto loaded = load_terminal_state(file);
    check(loaded.draft == state.draft && loaded.cursor == 1 &&
          loaded.history == state.history && loaded.queued == state.queued);
    Composer restored;
    restored.restore(loaded);
    feed(restored, "Z");
    check(restored.text() == std::string{"xZ\xff\0", 4});
    save_terminal_state(file, {"é", 1, {}, {}});
    check(load_terminal_state(file).cursor == 0);
    save_terminal_state(file, state);
    auto too_big = state;
    too_big.queued.resize(129);
    bool rejected = false;
    try {
      save_terminal_state(file, too_big);
    } catch (const Error &e) {
      rejected = e.code == ErrorCode::capacity;
    }
    check(rejected && load_terminal_state(file).queued == state.queued);
    too_big = state;
    too_big.history = {std::string(1024 * 1024, 'a'), "b"};
    rejected = false;
    try {
      save_terminal_state(file, too_big);
    } catch (const Error &e) {
      rejected = e.code == ErrorCode::capacity;
    }
    check(rejected);
    write_file(file, "broken");
    rejected = false;
    try {
      (void)load_terminal_state(file);
    } catch (const Error &) {
      rejected = true;
    }
    check(rejected);
    Composer inspection;
    inspection.draft("kept draft");
    feed(inspection, "\x1b[D");
    const auto kept_cursor = inspection.cursor();
    check(inspection.feed('\x18').action == InputAction::center_toggle);
    check(inspection.text() == "kept draft" && inspection.cursor() == kept_cursor);
    check(inspection.feed('\x18', nullptr, true).action == InputAction::center_toggle);
    check(inspection.feed('\x0f').action == InputAction::inspect_toggle);
    check(inspection.text() == "kept draft" && inspection.cursor() == kept_cursor);
    InputResult navigation;
    for (const auto byte : std::string_view{"\x1b[A"})
      navigation = inspection.feed(byte, nullptr, true);
    check(navigation.action == InputAction::inspect_previous);
    check(inspection.text() == "kept draft" && inspection.cursor() == kept_cursor);
    check(inspection.feed('\t', nullptr, true).action ==
          InputAction::inspect_toggle_focus);
    check(inspection.feed('X', nullptr, true).action ==
          InputAction::inspect_focus_editor);
    check(inspection.text() == "kept drafXt");
    inspection.draft("");
    for (const auto byte : std::string_view{"\x1b[200~"})
      navigation = inspection.feed(byte, nullptr, true);
    check(navigation.action == InputAction::inspect_focus_editor &&
          inspection.pasted());
    check(inspection.feed('\x0f').action == InputAction::none);
    check(inspection.feed('\x03').action == InputAction::none);
    check(inspection.feed('\x18').action == InputAction::none);
    feed(inspection, "\x1b[201~");
    check(inspection.text() == std::string{"\x0f\x03\x18", 3});
    TerminalUI task_ui{"Task observation fixture"};
    TaskState source_tasks;
    check(task_ui.try_observe_tasks({}).publication ==
          TaskPublicationStatus::uninitialized);
    task_ui.tasks(source_tasks.snapshot(), {2, {}, {}, source_tasks.revision()});
    check(task_ui.try_observe_tasks({}).publication == TaskPublicationStatus::ready);
    auto publish_add = [&] {
      auto prepared = source_tasks.prepare(unwrap(parse_json(
          R"({"op_id":"publication)" + std::to_string(source_tasks.revision()) +
          R"(","base":)" + std::to_string(source_tasks.revision()) +
          R"(,"ops":[{"op":"add","title":"Actual task"}]})")));
      auto packet = prepared.packet;
      source_tasks.commit(std::move(prepared));
      return packet;
    };
    auto delta = publish_add();
    task_ui.tasks(delta, {3, {}, {}, source_tasks.revision()});
    const auto accepted = task_ui.try_observe_tasks({});
    check(accepted.publication == TaskPublicationStatus::ready &&
          accepted.value.revision == 1 && accepted.value.row_count == 1 &&
          accepted.origin.revision == std::optional<std::uint64_t>{1});
    auto missed = publish_add();
    auto gap = publish_add();
    bool publication_rejected = false;
    try {
      task_ui.tasks(gap, {3, {}, {}, source_tasks.revision()});
    } catch (const Error &) {
      publication_rejected = true;
    }
    const auto stale = task_ui.try_observe_tasks({});
    check(publication_rejected && stale.publication == TaskPublicationStatus::stale &&
          stale.value.revision == 1 &&
          stale.origin.revision == std::optional<std::uint64_t>{1});
    task_ui.tasks(missed, {3, {}, {}, 2});
    check(task_ui.try_observe_tasks({}).publication == TaskPublicationStatus::stale);
    task_ui.tasks(source_tasks.snapshot(), {2, {}, {}, source_tasks.revision()});
    const auto repaired = task_ui.try_observe_tasks({});
    check(repaired.publication == TaskPublicationStatus::ready &&
          repaired.value.revision == 3 && repaired.value.row_count == 3);
    std::filesystem::remove_all(dir);

  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
