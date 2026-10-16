#pragma once
#include "blackbird/chat_view.hpp"
#include "blackbird/command_jobs.hpp"
#include "blackbird/display_text.hpp"
#include "blackbird/raw_ui.hpp"
#include "blackbird/tasks.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace blackbird {
class RetainedState;
enum class InputAction {
  none,
  submit,
  cancel,
  quit,
  page_up,
  page_down,
  edit,
  choose_command,
  inspect_toggle,
  inspect_previous,
  inspect_next,
  inspect_first,
  inspect_last,
  inspect_page_up,
  inspect_page_down,
  inspect_focus_collection,
  inspect_focus_detail,
  inspect_toggle_focus,
  inspect_focus_editor,
  inspect_previous_output,
  inspect_next_output
};
struct InputResult {
  InputAction action = InputAction::none;
  std::string text;
  RawUiSource source{};
};
struct TerminalState {
  std::string draft;
  std::size_t cursor = 0;
  std::vector<std::string> history, queued;
  RawUiSource draft_source{};
  std::vector<RawUiSource> history_sources{}, queued_sources{};
};
struct EditorResult {
  bool accepted = false;
  std::string text, message;
};
// Synchronous operator-selected foreground editor; caller owns terminal handoff.
EditorResult edit_terminal_draft(std::string_view draft,
                                 const RawAdapterContext *context = nullptr);
// Shared command discovery; local-only commands are labelled in help.
std::string terminal_help();
std::string terminal_key_help();
std::string terminal_command_hint(std::string_view draft);
TerminalState load_terminal_state(const std::filesystem::path &path,
                                  bool capture = false);
void save_terminal_state(const std::filesystem::path &path, const TerminalState &state,
                         bool capture = false);
struct ComposerChoice {
  std::string name, arguments, description, group;
  std::string label{};
};
class Composer {
public:
  InputResult feed(char byte, RawUiInput *raw = nullptr, bool inspecting = false);
  std::uint64_t capture_bound(std::size_t acquired_bytes) const;
  void restore(const TerminalState &state);
  TerminalState state() const;
  void draft(std::string text);
  void choices(std::vector<ComposerChoice> entries, std::string heading);
  bool palette_open() const noexcept { return palette_open_; }
  bool slash_open() const noexcept { return slash_open_; }
  bool escape_pending() const noexcept { return !escape_.empty(); }
  bool pasted() const noexcept { return pasted_; }
  bool flush_escape();
  std::vector<std::string> palette_lines(std::size_t rows) const;
  const std::string &text() const noexcept { return text_; }
  std::size_t cursor() const noexcept { return cursor_; }

private:
  InputResult feed_impl(char byte, RawUiInput *raw, bool inspecting = false);
  std::size_t history_capture_bound_ = 0, choice_capture_bound_ = 0;
  std::shared_ptr<const std::vector<ComposerChoice>> choices_;
  mutable std::shared_ptr<const std::vector<ComposerChoice>> capture_catalog_;
  std::string choices_heading_;
  std::string text_, escape_;
  std::size_t cursor_ = 0, history_index_ = 0;
  std::vector<std::string> history_;
  std::string draft_, killed_;
  std::size_t draft_cursor_ = 0;
  std::optional<std::size_t> goal_column_;
  bool pasted_ = false, palette_open_ = false;
  bool slash_open_ = false;
  std::string palette_query_;
  std::size_t palette_selected_ = 0;
  void palette_move(bool up);
  void insert(char byte);
  void history(bool older);
  void vertical(bool up);
  void kill(std::size_t begin, std::size_t end);
};
// One poll-turn decision: expiry is serviced for ready and idle input alike.
struct TerminalInputDispatch {
  bool read_input = false, redraw = false;
};
TerminalInputDispatch
terminal_input_dispatch(Composer &composer, std::chrono::steady_clock::time_point now,
                        std::chrono::steady_clock::time_point escape_deadline,
                        short events);
struct TerminalScrollUpdate {
  std::size_t scroll, previous, before_trim, after_trim, height;
};
std::size_t terminal_scroll_after_output(const TerminalScrollUpdate &update);
void trim_terminal_transcript(std::string &text);
std::vector<ChatRow> terminal_powerword_lines(std::string_view text,
                                              std::size_t columns);
// Invert a display column within one raw logical line; edits retain source bytes.
ByteOffset terminal_byte_at_column(std::string_view text, CellCount column);
DisplayPosition terminal_caret(std::string_view text, ByteOffset source,
                               CellCount columns);
std::vector<std::string> terminal_lines(std::string_view text, std::size_t columns);
class TerminalUI {
public:
  explicit TerminalUI(std::string title);
  ~TerminalUI();
  void raw_capture(bool enabled, RawUiSource initial = {}) noexcept {
    raw_capture_ = enabled;
    initial_source_ = initial;
  }
  void initial_source(RawUiSource source) noexcept { initial_source_ = source; }
  void raw_parent(RawEventRef parent) noexcept { raw_parent_ = parent; }
  void text(std::string_view text);
  void notice(std::string_view text);
  void process_output(std::string_view text);
  void restore_message(ChatKind kind, std::string_view text);
  void usage(std::string_view text);
  void operation_started(bool provider);
  void status(std::string_view text);
  void operation_completed(std::string_view text, Ink outcome = Ink::muted);
  void failed();
  void title(std::string_view text);
  void sessions(const Value &listing, RawUiValueOrigin origin = {});
  void tasks(const Value &publication, RawUiValueOrigin origin = {});
  void task_activity(const Value &event, RawUiValueOrigin origin = {});
  void run(const std::function<void(std::string_view)> &perform,
           std::atomic_bool &cancelled,
           const std::function<bool()> &exit_requested = {}, std::string initial = {},
           std::filesystem::path state_path = {},
           const std::function<void()> &idle_work = {},
           std::shared_ptr<CommandJobs> commands = {}, int interrupt_fd = -1,
           const std::function<Result<void>()> &reset_interrupt = {},
           RetainedState *raw_root = nullptr);

private:
  enum class Kind {
    text,
    notice,
    process,
    user,
    assistant,
    usage,
    provider_start,
    tool_start,
    status,
    title,
    sessions,
    complete,
    operation_complete,
    failure
  };
  struct Message {
    Kind kind;
    std::string text;
    Ink outcome = Ink::muted;
    Value data;
    RawEventRef enqueue{}, latest{};
    RawUiValueOrigin origin{};
  };
  bool raw_capture_ = false;
  RawUiSource initial_source_{};
  RawEventRef raw_parent_{};
  std::unique_ptr<class TerminalResize> resize_;
  std::mutex mutex_;
  int notification_ = -1;
  std::vector<Message> messages_;
  std::string title_;
  TaskState task_state_;
  bool tasks_dirty_ = true;
  std::map<std::string, Value> task_activity_;
  std::map<std::string, std::map<std::string, Value>> task_runs_;
  std::set<std::string> task_folded_;
  std::size_t task_offset_ = 0;
  std::string task_anchor_;
  void post(Kind kind, std::string_view text, RawCapacityClaim *preheld_data = nullptr);
};
} // namespace blackbird
