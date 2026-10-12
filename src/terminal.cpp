#include "blackbird/terminal.hpp"
#include "blackbird/json.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/raw_command.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/sprite.hpp"
#include "blackbird/task_view.hpp"
#include "blackbird/tools.hpp"
#include "blackbird/workflows.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <clocale>
#include <csignal>
#include <cstdlib>
#include <cwchar>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
extern char **environ;
namespace blackbird {
namespace {
volatile sig_atomic_t resize_pipe = -1;
void resize_signal(int) {
  const int saved = errno;
  if (resize_pipe >= 0) {
    const char byte = 1;
    (void)::write(resize_pipe, &byte, 1);
  }
  errno = saved;
}
using ChatCommand = ComposerChoice;
const std::array builtin_commands{
    ChatCommand{"/help", "", "Command guide (immediate in TUI)", "Chat"},
    ChatCommand{"/keys", "", "Keyboard guide (TUI only)", "Chat"},
    ChatCommand{"/commands", "",
                "Search command palette; Ctrl-Space or Ctrl-T (TUI only)", "Chat"},
    ChatCommand{
        "/edit", "",
        "Open a blank draft in $VISUAL/$EDITOR; Ctrl-G edits current draft (idle TUI)",
        "Chat"},
    ChatCommand{"/queue", "", "Show pending prompts (TUI only)", "Chat"},
    ChatCommand{"/cancel", "", "Request stop and clear pending queue (TUI only)",
                "Chat"},
    ChatCommand{"/clear", "", "Clear display, not model context (TUI only)", "Chat"},
    ChatCommand{"/drafts", "", "List recovered drafts (TUI only)", "Chat"},
    ChatCommand{"/draft", "NUMBER",
                "Load a recovered draft; explicit Enter sends (TUI only)", "Chat"},
    ChatCommand{"/quit", "", "Stop work and exit; preserve draft", "Chat"},
    ChatCommand{"/exit", "", "Alias for /quit", "Chat"},
    ChatCommand{"/model", "NAME", "Change model at the next available boundary",
                "Session"},
    ChatCommand{"/effort", "LEVEL", "Set low, medium, high or xhigh", "Session"},
    ChatCommand{"/session", "", "Current session and settings", "Session"},
    ChatCommand{"/new", "", "Start a fresh saved session; keep current settings",
                "Session"},
    ChatCommand{"/sessions", "",
                "Pick a neighbouring saved session (list in plain mode)", "Session"},
    ChatCommand{"/resume", "DIRECTORY",
                "Return to a saved session without sending a turn", "Session"},
    ChatCommand{"/name", "[NAME]", "Name this session; empty clears its name",
                "Session"},
    ChatCommand{"/checkpoint", "", "Save session state and retry maintenance",
                "Session"},
    ChatCommand{"/stats", "", "Context bytes and observed provider usage", "Session"},
    ChatCommand{"/tasks",
                "[add TITLE | sub ID TITLE | done ID | block ID REASON | rename ID "
                "TITLE | read JSON | apply JSON]",
                "Shared tasks; up/down, fold ID, expand/show/hide control pane",
                "Tasks"},
    ChatCommand{"/context", "", "Inspect editable model context", "Context"},
    ChatCommand{"/originals", "", "Retained context originals", "Context"},
    ChatCommand{"/compact", "JSON", "Managed context transformation", "Context"},
    ChatCommand{"/inspect", "JSON", "Bounded retained context inspection", "Context"},
    ChatCommand{"/restore", "ENTRY", "Restore an original context entry", "Context"},
    ChatCommand{"/lua", "CODE", "Run a Lua workflow", "Programs"},
    ChatCommand{"/workflow", "FILE", "Select the turn workflow", "Programs"},
    ChatCommand{"/restart", "NOTE", "Request native restart; build replacement first",
                "Programs"},
    ChatCommand{"/decision", "JSON", "Evaluate a native decision-model batch (Jev)",
                "Tools"},
    ChatCommand{"/exec", "JSON", "Launch a command; yield_ms releases its wait",
                "Tools"},
    ChatCommand{"/jobs", "", "Inspect owned commands immediately", "Tools"},
    ChatCommand{"/fg", "JOB", "Wait for the same command; PTY text input", "Tools"},
    ChatCommand{"/bg", "[JOB]",
                "Release foreground wait immediately; keep command running", "Tools"},
    ChatCommand{"/process", "JSON",
                "Shared command read/input/signal/resize/stop controls", "Tools"},
    ChatCommand{"/runs", "[ID | configure JSON | archive ID]",
                "Read observed participant runs, configure concurrency or archive a "
                "settled run",
                "Participants"},
    ChatCommand{"/run", "JSON",
                "Start a selected-context participant: request, optional task_id",
                "Participants"},
    ChatCommand{
        "/send", "JSON",
        "Address direction by run_id, message_id, from, text; next request boundary",
        "Participants"},
    ChatCommand{"/join", "JSON",
                "Seal run_ids, drain accepted directions, return in declared order",
                "Participants"},
    ChatCommand{
        "/await", "JSON",
        "Wait for run_id and accepted directions; keep run open. Optional timeout_ms",
        "Participants"},
    ChatCommand{"/stop", "ID",
                "Request participant cancellation; observe settlement separately",
                "Participants"},
    ChatCommand{"/auth", "status | providers",
                "Provider account metadata; sign in outside chat", "Configuration"},
    ChatCommand{"/colleagues", "",
                "Show installed colleague transports; authentication observed on call",
                "Tools"},
    ChatCommand{"/variables", "[JSON]", "Read central-variable point observations",
                "Tools"},
    ChatCommand{"/correlate", "[JSON]",
                "Plot timed work and variable observations on UTC lanes", "Tools"},
    ChatCommand{"/git-observe", "JSON",
                "Observe Git HEAD and optional bounded history; no changes", "Tools"},
    ChatCommand{"/trace", "[JSON]",
                "Read committed trajectory; bounded pages and attempt filter", "Tools"},
    ChatCommand{"/colleague", "JSON",
                "One selected-context colleague request; no tools or retries", "Tools"},
    ChatCommand{"/beads",
                "configure JSON | ready | list | show ID | select ID | cached",
                "Explicit native Beads refresh/selection (lazy binding)", "Tools"}};

std::shared_ptr<const std::vector<ChatCommand>> command_catalog() {
  thread_local std::shared_ptr<const WorkflowRegistry> previous;
  thread_local std::shared_ptr<const std::vector<ChatCommand>> cached;
  const auto registry = displayed_workflows();
  if (!cached || previous != registry) {
    auto next = std::make_shared<std::vector<ChatCommand>>(builtin_commands.begin(),
                                                           builtin_commands.end());
    if (registry)
      for (const auto &d : registry->definitions().array())
        for (const auto &alias : field(d, "aliases").array())
          next->push_back({"/" + registry->prefix() + alias.string(), "ARGS",
                           field(d, "description").string(), "Workflows"});
    cached = std::move(next);
    previous = registry;
  }
  return cached;
}

std::string search_text(std::string_view text) {
  std::string out{text};
  for (auto &c : out)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c + ('a' - 'A'));
  return out;
}
std::vector<std::size_t> palette_matches(std::string_view query,
                                         const std::vector<ChatCommand> &commands) {
  std::vector<std::size_t> matches;
  const auto needle = search_text(query);
  for (std::size_t n = 0; n < commands.size(); ++n) {
    const auto &command = commands[n];
    const auto haystack =
        search_text(std::string{command.name} + " " + std::string{command.description} +
                    " " + std::string{command.group} + " " + command.label);
    if (haystack.find(needle) != std::string::npos)
      matches.push_back(n);
  }
  auto rank = [&](std::size_t n) {
    auto name = search_text(commands[n].name);
    auto exact = needle;
    if (exact.starts_with("/"))
      exact.erase(0, 1);
    if (name.substr(1) == exact)
      return 0;
    if (name.find(needle) != std::string::npos)
      return 1;
    if (search_text(commands[n].description).find(needle) != std::string::npos)
      return 2;
    return 3;
  };
  std::stable_sort(matches.begin(), matches.end(),
                   [&](auto a, auto b) { return rank(a) < rank(b); });
  return matches;
}
bool command_token(std::string_view text) {
  return text.starts_with("/") &&
         text.find_first_of(" \t\r\n") == std::string_view::npos;
}
std::string complete_command(std::string_view text,
                             const std::vector<ChatCommand> &commands) {
  if (!command_token(text))
    return std::string{text};
  // An exact command wins over longer names (/draft versus /drafts).
  for (const auto &command : commands)
    if (command.name == text)
      return std::string{text} + (command.arguments.empty() ? "" : " ");
  std::string prefix;
  const ChatCommand *only = nullptr;
  std::size_t count = 0;
  for (const auto &command : commands) {
    if (!command.name.starts_with(text))
      continue;
    if (count++ == 0) {
      prefix = command.name;
      only = &command;
    } else {
      std::size_t n = 0;
      while (n < prefix.size() && n < command.name.size() &&
             prefix[n] == command.name[n])
        ++n;
      prefix.resize(n);
    }
  }
  if (count == 0)
    return std::string{text};
  if (count == 1 && !only->arguments.empty())
    prefix += ' ';
  return prefix;
}
bool continuation(char byte) {
  return (static_cast<unsigned char>(byte) & 0xc0U) == 0x80U;
}
std::size_t previous(std::string_view s, std::size_t cursor) {
  if (cursor == 0)
    return 0;
  --cursor;
  while (cursor > 0 && continuation(s[cursor]))
    --cursor;
  return cursor;
}
std::size_t next(std::string_view s, std::size_t cursor) {
  if (cursor < s.size())
    ++cursor;
  while (cursor < s.size() && continuation(s[cursor]))
    ++cursor;
  return cursor;
}
std::size_t line_start(std::string_view text, std::size_t cursor) {
  const auto newline =
      cursor == 0 ? std::string_view::npos : text.rfind('\n', cursor - 1);
  return newline == std::string_view::npos ? 0 : newline + 1;
}
std::size_t line_end(std::string_view text, std::size_t cursor) {
  const auto newline = text.find('\n', cursor);
  return newline == std::string_view::npos ? text.size() : newline;
}
std::size_t display_width(std::string_view text) {
  std::size_t cells = 0;
  while (!text.empty()) {
    if (text.front() == '\t') {
      cells += 2;
      text.remove_prefix(1);
      continue;
    }
    wchar_t wide{};
    std::mbstate_t state{};
    const auto n = std::mbrtowc(&wide, text.data(), text.size(), &state);
    const auto width = n != 0 && n <= text.size() ? ::wcwidth(wide) : -1;
    cells += width < 0 ? 4 : static_cast<std::size_t>(width);
    text.remove_prefix(width < 0 ? 1 : n);
  }
  return cells;
}
bool word_space(std::string_view text, std::size_t position) {
  // Whitespace-delimited words, rather than imposing a language's punctuation rules.
  const auto c = static_cast<unsigned char>(text[position]);
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}
std::size_t word_left(std::string_view text, std::size_t cursor) {
  while (cursor > 0 && word_space(text, previous(text, cursor)))
    cursor = previous(text, cursor);
  while (cursor > 0 && !word_space(text, previous(text, cursor)))
    cursor = previous(text, cursor);
  return cursor;
}
std::size_t word_right(std::string_view text, std::size_t cursor) {
  while (cursor < text.size() && word_space(text, cursor))
    cursor = next(text, cursor);
  while (cursor < text.size() && !word_space(text, cursor))
    cursor = next(text, cursor);
  return cursor;
}
// Control handoffs use a bounded writer too. CAN cancels an abandoned CSI.
void terminal_control(std::string_view bytes) {
  ChatOutput writer;
  writer.attach(STDOUT_FILENO);
  writer.start(bytes);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
  while (!writer.flush()) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("Terminal control handoff timed out");
    pollfd fd{STDOUT_FILENO, POLLOUT, 0};
    (void)::poll(&fd, 1, 10);
  }
}
class TerminalMode {
public:
  TerminalMode() {
    if (tcgetattr(STDIN_FILENO, &before_) != 0)
      throw std::runtime_error("Cannot read terminal mode");
    try {
      resume();
    } catch (...) {
      (void)tcsetattr(STDIN_FILENO, TCSANOW, &before_);
      throw;
    }
  }
  void resume() {
    auto raw = before_;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | IEXTEN | ISIG));
    raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
      throw std::runtime_error("Cannot set terminal mode");
    active_ = true;
    terminal_control("\x18\x1b[?1049h\x1b[?2004h");
  }
  void suspend() {
    if (!active_)
      return;
    terminal_control("\x18\x1b_Ga=d,d=I,i=72171,q=2;\x1b\\\x1b[?2026l\x1b[0m\x1b[?"
                     "2004l\x1b[?1049l\x1b[?25h");
    if (tcsetattr(STDIN_FILENO, TCSANOW, &before_) != 0)
      throw std::runtime_error("Cannot restore terminal for editor");
    active_ = false;
  }
  ~TerminalMode() {
    if (active_) {
      try {
        terminal_control("\x18\x1b_Ga=d,d=I,i=72171,q=2;\x1b\\\x1b[?2026l\x1b[0m\x1b[?"
                         "2004l\x1b[?1049l\x1b[?25h");
      } catch (...) {
      }
      (void)tcsetattr(STDIN_FILENO, TCSANOW, &before_);
    }
  }

private:
  termios before_{};
  bool active_ = false;
};
} // namespace
EditorResult edit_terminal_draft(std::string_view draft) {
  constexpr std::size_t limit = 1024 * 1024;
  if (draft.size() > limit)
    return {false, {}, "Draft exceeds editor limit."};
  struct Cleanup {
    std::string path;
    int fd = -1;
    bool owned = false;
    void close() {
      if (fd >= 0) {
        (void)::close(fd);
        fd = -1;
      }
    }
    ~Cleanup() {
      close();
      if (owned)
        (void)::unlink(path.c_str());
    }
  } cleanup;
  auto &path = cleanup.path;
  auto &fd = cleanup.fd;
  try {
    path = (std::filesystem::temp_directory_path() / "arco-draft-XXXXXX").string();
    {
      const std::lock_guard lock{detail::process_spawn_mutex};
      fd = ::mkstemp(path.data());
      if (fd < 0)
        throw std::runtime_error("Cannot create private editor draft");
      cleanup.owned = true;
      if (fd < 3) {
        const auto replacement = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (replacement < 0)
          throw std::runtime_error("Cannot normalize editor draft descriptor");
        (void)::close(fd);
        fd = replacement;
      }
      if (::fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        throw std::runtime_error("Cannot protect editor draft descriptor");
    }
    auto remaining = draft;
    while (!remaining.empty()) {
      const auto n = ::write(fd, remaining.data(), remaining.size());
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0)
        throw std::runtime_error("Cannot write editor draft");
      remaining.remove_prefix(static_cast<std::size_t>(n));
    }
    cleanup.close();
    const char *editor = std::getenv("VISUAL");
    if (!editor || !*editor)
      editor = std::getenv("EDITOR");
    if (!editor || !*editor)
      editor = "vi";
    std::string quoted = "'";
    for (const char c : path)
      quoted += c == '\'' ? "'\\''" : std::string(1, c);
    quoted += '\'';
    std::string command = "exec " + std::string{editor} + " " + quoted;
    std::array<char *, 4> args{const_cast<char *>("sh"), const_cast<char *>("-c"),
                               command.data(), nullptr};
    pid_t child{};
    const auto launched = [&] {
      const std::lock_guard lock{detail::process_spawn_mutex};
      return ::posix_spawn(&child, "/bin/sh", nullptr, nullptr, args.data(), environ);
    }();
    if (launched != 0)
      return {false, {}, "Editor could not start; draft retained."};
    int status{};
    pid_t waited;
    do {
      waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
      return {false, {}, "Editor cancelled or failed; draft retained."};
    fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat info{};
    if (fd < 0 || ::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode))
      return {false,
              {},
              "Editor draft unavailable or not a regular file; original retained."};
    std::string edited;
    std::array<char, 4096> bytes{};
    while (true) {
      const auto n = ::read(fd, bytes.data(), bytes.size());
      if (n < 0 && errno == EINTR)
        continue;
      if (n < 0)
        return {false, {}, "Cannot read editor draft; original retained."};
      if (n == 0)
        break;
      const auto count = static_cast<std::size_t>(n);
      if (count > limit - edited.size())
        return {false, {}, "Editor draft exceeds 1 MiB; original retained."};
      edited.append(bytes.data(), count);
    }
    return {true, std::move(edited), "Editor returned; Enter sends explicitly."};
  } catch (const std::exception &e) {
    return {false, {}, std::string{e.what()} + "; original draft retained."};
  }
}
std::string terminal_key_help() {
  return "Keyboard\n"
         "  Enter        Send; while busy, queue for the next turn\n"
         "  Alt-Enter    Insert newline (also Ctrl-J)\n"
         "  Tab          Complete a slash-command name; ambiguity never submits\n"
         "  Ctrl-G       Edit current draft in $VISUAL/$EDITOR (idle only)\n"
         "  Ctrl-Space/T Search commands; arrows select, Enter loads, Esc backs out\n"
         "  Up / Down    Move between draft lines; history at first/last line\n"
         "  Ctrl-P / N   Explicit history; restores your draft and cursor\n"
         "  Left / Right Move within the draft\n"
         "  Ctrl-A / E   Start / end of line (also Home / End)\n"
         "  Ctrl-Home/End Start / end of draft\n"
         "  Alt-B / F    Back / forward word (also Ctrl-Left / Right)\n"
         "  Ctrl-W       Delete previous word; Alt-D deletes next word\n"
         "  Ctrl-K / Y   Kill to line end / yank last deletion\n"
         "  Ctrl-U       Clear draft; Ctrl-Y restores it\n"
         "  Ctrl-C       Request stop and clear pending queue; keep unsent draft\n"
         "  Ctrl-B       Background foreground command; preserve composer\n"
         "  PTY input    Keys and Ctrl-C reach selected command; Ctrl-B returns\n"
         "  Ctrl-Q       Stop and exit; preserve unsent draft\n"
         "  PgUp / PgDn  Scroll transcript\n"
         "  Paste        Multiline text stays literal; Enter sends explicitly\n";
}
std::vector<std::string> terminal_builtin_names() {
  std::vector<std::string> names;
  for (const auto &c : builtin_commands)
    names.push_back(c.name);
  return names;
}
std::string terminal_help() {
  const auto catalog = command_catalog();
  const auto &commands = *catalog;
  std::string out = "Blackbird command guide\n";
  std::string_view group;
  for (const auto &command : commands) {
    if (command.group != group) {
      group = command.group;
      out += "\n" + std::string{group} + "\n";
    }
    out += "  " + std::string{command.name};
    if (!command.arguments.empty())
      out += " " + std::string{command.arguments};
    out += " — " + std::string{command.description} + "\n";
  }
  out +=
      "\nLocal TUI controls run immediately, even during work. Other commands queue\n"
      "behind the active turn; they do not mutate an in-flight provider request.\n\n";
  return out +
         "Keyboard: /keys · Tab completes commands · PgUp/PgDn browse this guide\n";
}
std::string terminal_command_hint(std::string_view draft) {
  const auto catalog = command_catalog();
  const auto &commands = *catalog;
  if (!draft.starts_with("/") || draft.find_first_of("\r\n") != std::string_view::npos)
    return {};
  const auto token = draft.substr(0, draft.find_first_of(" \t"));
  std::string matches;
  for (const auto &command : commands) {
    if (command.name == token) {
      return std::string{command.name} +
             (command.arguments.empty() ? "" : " " + std::string{command.arguments}) +
             " — " + std::string{command.description};
    }
    if (command.name.starts_with(token) && token == draft) {
      if (!matches.empty())
        matches += " · ";
      matches += command.name;
    }
  }
  return matches.empty() ? "Unknown command · /help lists commands"
                         : "Tab complete · " + matches;
}
void Composer::insert(char byte) {
  if (palette_open_) {
    if (static_cast<unsigned char>(byte) >= 32 && byte != 127 &&
        palette_query_.size() < 128) {
      palette_query_ += byte;
      palette_selected_ = 0;
    }
    return;
  }
  goal_column_.reset();
  if (text_.size() >= 1024 * 1024)
    return;
  text_.insert(cursor_, 1, byte);
  ++cursor_;
  if (!pasted_) {
    slash_open_ = cursor_ == text_.size() && command_token(text_) &&
                  text_.find('/', 1) == std::string::npos;
    palette_query_ = slash_open_ ? text_.substr(1) : "";
    palette_selected_ = 0;
  }
}
namespace {
constexpr std::size_t ui_limit = 1024 * 1024;
void validate_state(const TerminalState &state) {
  if (state.draft.size() > ui_limit || state.cursor > state.draft.size())
    throw Error{ErrorCode::capacity};
  for (const auto *list : {&state.history, &state.queued}) {
    if (list->size() > 128)
      throw Error{ErrorCode::capacity};
    std::size_t size = 0;
    for (const auto &value : *list) {
      if (value.size() > ui_limit - size)
        throw Error{ErrorCode::capacity};
      size += value.size();
    }
  }
}
std::string ui_hex(std::string_view value, bool decode = false) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out;
  if (!decode) {
    for (const char byte : value) {
      const auto c = static_cast<unsigned char>(byte);
      out += digits[c >> 4U];
      out += digits[c & 15U];
    }
  } else {
    if (value.size() % 2 != 0)
      throw Error{ErrorCode::corrupt};
    for (std::size_t i = 0; i < value.size(); i += 2) {
      unsigned n = 0;
      auto r = std::from_chars(value.data() + i, value.data() + i + 2, n, 16);
      if (r.ec != std::errc{} || r.ptr != value.data() + i + 2)
        throw Error{ErrorCode::corrupt};
      out += static_cast<char>(n);
    }
  }
  return out;
}
void trim_history(std::vector<std::string> &values) {
  std::size_t size = 0;
  for (const auto &value : values)
    size += value.size();
  while (values.size() > 128 || size > ui_limit) {
    size -= values.front().size();
    values.erase(values.begin());
  }
}
} // namespace
std::size_t terminal_scroll_after_output(const TerminalScrollUpdate &update) {
  if (update.scroll == 0)
    return 0;
  const auto growth =
      update.before_trim > update.previous ? update.before_trim - update.previous : 0;
  const auto maximum =
      update.after_trim > update.height ? update.after_trim - update.height : 0;
  if (update.scroll >= maximum || growth >= maximum - update.scroll)
    return maximum;
  return update.scroll + growth;
}
void trim_terminal_transcript(std::string &text) {
  if (text.size() > ui_limit) {
    constexpr std::string_view notice =
        "[Older display trimmed; full originals remain audited]\n";
    const auto minimum = text.size() - ui_limit + notice.size();
    const auto lf = text.find('\n', minimum);
    auto cut =
        lf == std::string::npos || lf - minimum > ui_limit / 8 ? minimum : lf + 1;
    while (cut < text.size() && continuation(text[cut]))
      ++cut;
    text.erase(0, cut);
    text.insert(0, notice);
  }
}
TerminalState load_terminal_state(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path))
    return {};
  const auto packet = unwrap(decode_packet_string(read_file(path, 7 * ui_limit)));
  if (string_field(packet, "encoding") != "hex-v1")
    throw Error{ErrorCode::corrupt};
  TerminalState state;
  state.draft = ui_hex(string_field(packet, "draft"), true);
  const auto &cursor = field(packet, "cursor").number().text();
  auto r = std::from_chars(cursor.data(), cursor.data() + cursor.size(), state.cursor);
  if (r.ec != std::errc{} || r.ptr != cursor.data() + cursor.size())
    throw Error{ErrorCode::corrupt};
  for (const auto &[key, list] :
       {std::pair{"history", &state.history}, std::pair{"queued", &state.queued}})
    for (const auto &value : field(packet, key).array())
      list->push_back(ui_hex(value.string(), true));
  validate_state(state);
  while (state.cursor > 0 && state.cursor < state.draft.size() &&
         continuation(state.draft[state.cursor]))
    --state.cursor;
  return state;
}
void save_terminal_state(const std::filesystem::path &path,
                         const TerminalState &state) {
  validate_state(state);
  auto encode = [](const auto &values) {
    Value::Array out;
    for (const auto &v : values)
      out.emplace_back(ui_hex(v));
    return Value{std::move(out)};
  };
  write_file(path, unwrap(encode_packet_string(
                       Value::object({{"encoding", Value{"hex-v1"}},
                                      {"draft", Value{ui_hex(state.draft)}},
                                      {"cursor", Value{Number{state.cursor}}},
                                      {"history", encode(state.history)},
                                      {"queued", encode(state.queued)}}))));
}
void Composer::restore(const TerminalState &state) {
  validate_state(state);
  text_ = state.draft;
  cursor_ = state.cursor;
  history_ = state.history;
  history_index_ = history_.size();
  draft_.clear();
  draft_cursor_ = 0;
  killed_.clear();
  goal_column_.reset();
  escape_.clear();
  pasted_ = false;
  slash_open_ = false;
  palette_open_ = false;
  palette_query_.clear();
  palette_selected_ = 0;
}
TerminalState Composer::state() const { return {text_, cursor_, history_, {}}; }
void Composer::draft(std::string text) {
  slash_open_ = false;
  if (text.size() > ui_limit)
    throw Error{ErrorCode::capacity};
  text_ = std::move(text);
  cursor_ = text_.size();
  history_index_ = history_.size();
  draft_.clear();
  draft_cursor_ = 0;
  goal_column_.reset();
}
void Composer::choices(std::vector<ComposerChoice> entries, std::string heading) {
  choices_ = std::make_shared<const std::vector<ComposerChoice>>(std::move(entries));
  choices_heading_ = std::move(heading);
  palette_open_ = true;
  slash_open_ = false;
  palette_query_.clear();
  palette_selected_ = 0;
}
void Composer::palette_move(bool up) {
  const auto catalog = choices_ && palette_open_ ? choices_ : command_catalog();
  const auto &commands = *catalog;
  const auto matches = palette_matches(palette_query_, commands);
  if (matches.empty()) {
    palette_selected_ = 0;
    return;
  }
  palette_selected_ = std::min(palette_selected_, matches.size() - 1);
  if (up)
    palette_selected_ -= palette_selected_ > 0 ? 1U : 0U;
  else
    palette_selected_ = std::min(palette_selected_ + 1, matches.size() - 1);
}
bool Composer::flush_escape() {
  if ((palette_open_ || slash_open_) && escape_ == "\x1b") {
    escape_.clear();
    palette_open_ = false;
    slash_open_ = false;
    return true;
  }
  return false;
}
std::vector<std::string> Composer::palette_lines(std::size_t rows) const {
  const auto catalog = choices_ && palette_open_ ? choices_ : command_catalog();
  const auto &commands = *catalog;
  if ((!palette_open_ && !slash_open_) || rows == 0)
    return {};
  const auto matches = palette_matches(palette_query_, commands);
  std::vector<std::string> out{slash_open_
                                   ? "Commands"
                                   : (choices_ ? choices_heading_ : "Commands") +
                                         " / " + palette_query_ + "_"};
  if (rows == 1)
    return out;
  const auto count = rows > 2 ? rows - 2 : 1;
  if (matches.empty())
    out.emplace_back("  No matching commands");
  else {
    const auto selected = std::min(palette_selected_, matches.size() - 1);
    const auto first = selected >= count ? selected - count + 1 : 0;
    for (std::size_t n = first; n < matches.size() && n - first < count; ++n) {
      const auto &command = commands[matches[n]];
      out.push_back(
          (n == selected ? "> " : "  ") +
          (command.label.empty() ? command.name : command.label) +
          (command.arguments.empty() ? "" : " " + std::string{command.arguments}) +
          " — " + std::string{command.description});
    }
  }
  if (rows > 2)
    out.emplace_back(slash_open_
                         ? "↑↓ select · Tab fill · Enter run/fill · Esc dismiss"
                         : "Type search · Up/Down select · Enter loads · Esc back");
  out.resize(rows);
  return out;
}
void Composer::history(bool older) {
  slash_open_ = false;
  if (older) {
    if (history_index_ == 0)
      return;
    if (history_index_ == history_.size()) {
      draft_ = text_;
      draft_cursor_ = cursor_;
    }
    text_ = history_[--history_index_];
    cursor_ = text_.size();
  } else {
    if (history_index_ == history_.size())
      return;
    ++history_index_;
    text_ = history_index_ == history_.size() ? draft_ : history_[history_index_];
    cursor_ = history_index_ == history_.size() ? draft_cursor_ : text_.size();
  }
  goal_column_.reset();
}
void Composer::vertical(bool up) {
  const auto begin = line_start(text_, cursor_);
  const auto end = line_end(text_, cursor_);
  if ((up && begin == 0) || (!up && end == text_.size())) {
    history(up);
    return;
  }
  if (!goal_column_)
    goal_column_ =
        display_width(std::string_view{text_}.substr(begin, cursor_ - begin));
  const auto target_begin = up ? line_start(text_, begin - 1) : end + 1;
  const auto target_end = line_end(text_, target_begin);
  std::size_t position = target_begin, cells = 0;
  while (position < target_end) {
    const auto after = next(text_, position);
    const auto width =
        display_width(std::string_view{text_}.substr(position, after - position));
    if (cells + width > *goal_column_)
      break;
    cells += width;
    position = after;
  }
  cursor_ = position;
}
void Composer::kill(std::size_t begin, std::size_t end) {
  slash_open_ = false;
  if (begin == end)
    return;
  killed_ = text_.substr(begin, end - begin);
  text_.erase(begin, end - begin);
  cursor_ = begin;
  goal_column_.reset();
}
InputResult Composer::feed(char byte) {
  const auto catalog = choices_ && palette_open_ ? choices_ : command_catalog();
  const auto &commands = *catalog;
  const auto c = static_cast<unsigned char>(byte);
  if (!escape_.empty()) {
    escape_ += byte;
    if (escape_ == "\x1b[200~") {
      slash_open_ = false;
      pasted_ = true;
      escape_.clear();
      return {};
    }
    if (escape_ == "\x1b[201~") {
      pasted_ = false;
      escape_.clear();
      return {};
    }
    if (pasted_) {
      if (std::string_view{"\x1b[201~"}.starts_with(escape_))
        return {};
      for (const char v : escape_)
        insert(v);
      escape_.clear();
      return {};
    }
    if (escape_ == "\x1b\r" || escape_ == "\x1b\n") {
      if (!palette_open_)
        insert('\n');
      escape_.clear();
      return {};
    }
    if (escape_.size() == 2 && (byte == '[' || byte == 'O'))
      return {};
    if (escape_.size() >= 3 && c >= 0x30U && c <= 0x3fU)
      return {};
    if (palette_open_) {
      if (escape_ == "\x1b[A" || escape_ == "\x1bOA")
        palette_move(true);
      if (escape_ == "\x1b[B" || escape_ == "\x1bOB")
        palette_move(false);
      escape_.clear();
      return {};
    }
    if (slash_open_ && (escape_ == "\x1b[A" || escape_ == "\x1bOA" ||
                        escape_ == "\x1b[B" || escape_ == "\x1bOB")) {
      palette_move(escape_ == "\x1b[A" || escape_ == "\x1bOA");
      escape_.clear();
      return {};
    }
    slash_open_ = false;
    const bool up = escape_ == "\x1b[A" || escape_ == "\x1bOA";
    const bool down = escape_ == "\x1b[B" || escape_ == "\x1bOB";
    if (up || down)
      vertical(up);
    else {
      goal_column_.reset();
      if (escape_ == "\x1b[D" || escape_ == "\x1bOD")
        cursor_ = previous(text_, cursor_);
      if (escape_ == "\x1b[C" || escape_ == "\x1bOC")
        cursor_ = next(text_, cursor_);
      if (escape_ == "\x1b[H" || escape_ == "\x1bOH" || escape_ == "\x1b[1~" ||
          escape_ == "\x1b[7~")
        cursor_ = line_start(text_, cursor_);
      if (escape_ == "\x1b[F" || escape_ == "\x1bOF" || escape_ == "\x1b[4~" ||
          escape_ == "\x1b[8~")
        cursor_ = line_end(text_, cursor_);
      if (escape_ == "\x1b[1;5H")
        cursor_ = 0;
      if (escape_ == "\x1b[1;5F")
        cursor_ = text_.size();
      if (escape_ == "\x1b"
                     "b" ||
          escape_ == "\x1b[1;5D" || escape_ == "\x1b[1;3D")
        cursor_ = word_left(text_, cursor_);
      if (escape_ == "\x1b"
                     "f" ||
          escape_ == "\x1b[1;5C" || escape_ == "\x1b[1;3C")
        cursor_ = word_right(text_, cursor_);
      if (escape_ == "\x1b"
                     "d")
        kill(cursor_, word_right(text_, cursor_));
      if (escape_ == "\x1b\x7f" || escape_ == "\x1b\x08")
        kill(word_left(text_, cursor_), cursor_);
      if (escape_ == "\x1b[3~" && cursor_ < text_.size())
        text_.erase(cursor_, next(text_, cursor_) - cursor_);
    }
    const auto action = escape_ == "\x1b[5~"   ? InputAction::page_up
                        : escape_ == "\x1b[6~" ? InputAction::page_down
                                               : InputAction::none;
    escape_.clear();
    return {action, {}};
  }
  if (c == 27) {
    escape_ = byte;
    return {};
  }
  if (pasted_) {
    insert(byte == '\r' ? '\n' : byte);
    return {};
  }
  if (c == 0 || c == 20) {
    slash_open_ = false;
    choices_.reset();
    palette_open_ = !palette_open_;
    palette_query_.clear();
    palette_selected_ = 0;
    return {};
  }
  if (palette_open_) {
    if (c == 3 || c == 17) {
      palette_open_ = false;
      return {c == 17 ? InputAction::quit : InputAction::none, {}};
    }
    if (c == 13) {
      const auto matches = palette_matches(palette_query_, commands);
      if (!matches.empty()) {
        const auto &command =
            commands[matches[std::min(palette_selected_, matches.size() - 1)]];
        palette_open_ = false;
        return {InputAction::choose_command,
                std::string{command.name} + (command.arguments.empty() ? "" : " ")};
      }
    } else if (c == 16 || c == 14 || c == 9)
      palette_move(c == 16);
    else if (c == 127 || c == 8) {
      palette_query_.erase(previous(palette_query_, palette_query_.size()));
      palette_selected_ = 0;
    } else
      insert(byte);
    return {};
  }
  if (c == 7)
    return {InputAction::edit, {}};
  if (c == 16 || c == 14) {
    history(c == 16);
    return {};
  }
  if (c == 23) {
    kill(word_left(text_, cursor_), cursor_);
    return {};
  }
  if (c == 11) {
    auto end = line_end(text_, cursor_);
    if (end == cursor_ && end < text_.size())
      ++end;
    kill(cursor_, end);
    return {};
  }
  if (c == 25) {
    if (killed_.size() <= ui_limit - text_.size()) {
      text_.insert(cursor_, killed_);
      cursor_ += killed_.size();
      goal_column_.reset();
    }
    return {};
  }
  if (c == 9) {
    if (slash_open_) {
      const auto matches = palette_matches(palette_query_, commands);
      if (!matches.empty()) {
        const auto &command =
            commands[matches[std::min(palette_selected_, matches.size() - 1)]];
        draft(std::string{command.name} + (command.arguments.empty() ? "" : " "));
      }
      return {};
    }
    if (cursor_ == text_.size()) {
      text_ = complete_command(text_, commands);
      cursor_ = text_.size();
      goal_column_.reset();
    }
    return {};
  }
  if (c == 3) {
    slash_open_ = false;
    return {InputAction::cancel, {}};
  }
  if (c == 17 || (c == 4 && text_.empty()))
    return {InputAction::quit, {}};
  if (c == 13) {
    if (slash_open_) {
      const auto matches = palette_matches(palette_query_, commands);
      if (!matches.empty()) {
        const auto &command =
            commands[matches[std::min(palette_selected_, matches.size() - 1)]];
        const bool arguments =
            !command.arguments.empty() && !command.arguments.starts_with("[");
        draft(std::string{command.name} + (arguments ? " " : ""));
        if (arguments)
          return {};
      }
      slash_open_ = false;
    }
    auto result = text_;
    text_.clear();
    cursor_ = 0;
    if (!result.empty() && result != "/quit" && result != "/exit") {
      history_.push_back(result);
      trim_history(history_);
    }
    history_index_ = history_.size();
    draft_.clear();
    draft_cursor_ = 0;
    goal_column_.reset();
    return {InputAction::submit, std::move(result)};
  }
  if (c == 10) {
    insert('\n');
    return {};
  }
  if (c == 1) {
    slash_open_ = false;
    goal_column_.reset();
    cursor_ = line_start(text_, cursor_);
    return {};
  }
  if (c == 5) {
    slash_open_ = false;
    goal_column_.reset();
    cursor_ = line_end(text_, cursor_);
    return {};
  }
  if (c == 21) {
    slash_open_ = false;
    kill(0, text_.size());
    cursor_ = 0;
    goal_column_.reset();
    return {};
  }
  if (c == 127 || c == 8) {
    goal_column_.reset();
    const auto p = previous(text_, cursor_);
    text_.erase(p, cursor_ - p);
    cursor_ = p;
    if (slash_open_) {
      slash_open_ = command_token(text_);
      palette_query_ = slash_open_ ? text_.substr(1) : "";
      palette_selected_ = 0;
    }
    return {};
  }
  if (c == 4 && cursor_ < text_.size()) {
    goal_column_.reset();
    text_.erase(cursor_, next(text_, cursor_) - cursor_);
    return {};
  }
  if (c >= 32 && text_.size() < 1024 * 1024)
    insert(byte);
  return {};
}
std::vector<std::string> terminal_lines(std::string_view text, std::size_t columns) {
  columns = std::max<std::size_t>(columns, 1);
  std::vector<std::string> lines(1);
  std::size_t width = 0;
  auto add = [&](std::string_view value, std::size_t cells) {
    if (width != 0 && width + cells > columns) {
      lines.emplace_back();
      width = 0;
    }
    lines.back().append(value);
    width += cells;
  };
  while (!text.empty()) {
    const auto c = static_cast<unsigned char>(text.front());
    if (c == '\n') {
      lines.emplace_back();
      width = 0;
      text.remove_prefix(1);
      continue;
    }
    if (c == '\t') {
      add("  ", 2);
      text.remove_prefix(1);
      continue;
    }
    wchar_t wide{};
    std::mbstate_t state{};
    auto count = std::mbrtowc(&wide, text.data(), text.size(), &state);
    const auto cells = count != static_cast<std::size_t>(-1) &&
                               count != static_cast<std::size_t>(-2) && count != 0
                           ? ::wcwidth(wide)
                           : -1;
    if (c < 32 || c == 127 || cells < 0) {
      constexpr char hex[] = "0123456789abcdef";
      const char escaped[] = {'\\', 'x', hex[c >> 4U], hex[c & 15U]};
      for (const auto value : escaped)
        add(std::string_view{&value, 1}, 1);
      count = 1;
    } else
      add(text.substr(0, count), static_cast<std::size_t>(cells));
    text.remove_prefix(count);
  }
  return lines;
}
std::vector<ChatRow> terminal_powerword_lines(std::string_view text,
                                              std::size_t columns) {
  const auto registry = displayed_workflows();
  std::vector<ChatRow> result;
  do {
    const auto end = text.find('\n');
    const auto raw = text.substr(0, end);
    auto spans =
        registry ? registry->color(raw) : ChatRow{{std::string{raw}, Ink::normal}};
    std::string safe;
    for (auto &span : spans) {
      span.text =
          terminal_lines(span.text, std::numeric_limits<std::size_t>::max()).front();
      safe += span.text;
    }
    std::size_t index = 0, offset = 0;
    for (const auto &line : terminal_lines(safe, columns)) {
      ChatRow row;
      std::size_t remaining = line.size();
      while (remaining && index < spans.size()) {
        const auto count = std::min(remaining, spans[index].text.size() - offset);
        if (count)
          row.push_back({spans[index].text.substr(offset, count), spans[index].ink});
        remaining -= count;
        offset += count;
        if (offset == spans[index].text.size()) {
          ++index;
          offset = 0;
        }
      }
      result.push_back(std::move(row));
    }
    if (end == std::string_view::npos)
      break;
    text.remove_prefix(end + 1);
  } while (true);
  return result;
}
void TerminalUI::post(Kind kind, std::string_view text) {
  const std::lock_guard lock{mutex_};
  if (!messages_.empty() && messages_.back().kind == kind &&
      (kind == Kind::text || kind == Kind::process))
    messages_.back().text.append(text);
  else
    messages_.push_back({kind, std::string{text}, Ink::muted, Value{}});
  if (notification_ >= 0) {
    const char byte = 1;
    (void)::write(notification_, &byte, 1);
  }
}
void TerminalUI::sessions(const Value &listing) {
  const std::lock_guard lock{mutex_};
  messages_.push_back({Kind::sessions, "", Ink::muted, listing});
  if (notification_ >= 0) {
    const char byte = 1;
    (void)::write(notification_, &byte, 1);
  }
}
void TerminalUI::text(std::string_view value) { post(Kind::text, value); }
void TerminalUI::notice(std::string_view value) { post(Kind::notice, value); }
void TerminalUI::process_output(std::string_view value) { post(Kind::process, value); }
void TerminalUI::restore_message(ChatKind kind, std::string_view value) {
  post(kind == ChatKind::user ? Kind::user : Kind::assistant, value);
}
void TerminalUI::usage(std::string_view value) { post(Kind::usage, value); }
void TerminalUI::operation_started(bool provider) {
  post(provider ? Kind::provider_start : Kind::tool_start, {});
}
void TerminalUI::status(std::string_view value) { post(Kind::status, value); }
void TerminalUI::operation_completed(std::string_view value, Ink outcome) {
  const std::lock_guard lock{mutex_};
  messages_.push_back({Kind::operation_complete, std::string{value}, outcome, Value{}});
  if (notification_ >= 0) {
    const char byte = 1;
    (void)::write(notification_, &byte, 1);
  }
}
void TerminalUI::failed() { post(Kind::failure, {}); }
void TerminalUI::title(std::string_view value) { post(Kind::title, value); }
void TerminalUI::tasks(const Value &publication) {
  const std::lock_guard lock{mutex_};
  if (string_field(publication, "label") == "task-state-v1")
    task_state_.restore(publication);
  else
    task_state_.replay(publication);
  if (publication.find("order"))
    if (const auto position = task_state_.position(task_anchor_))
      task_offset_ = *position;
  if (const auto *removed = publication.find("removed"))
    for (const auto &id : removed->array()) {
      task_activity_.erase(id.string());
      task_runs_.erase(id.string());
      task_folded_.erase(id.string());
    }
  // One mutable derived view, not a queued snapshot per update.
  tasks_dirty_ = true;
  if (notification_ >= 0) {
    const char byte = 1;
    (void)::write(notification_, &byte, 1);
  }
}
void TerminalUI::task_activity(const Value &event) {
  const std::lock_guard lock{mutex_};
  const auto &id = string_field(event, "task_id");
  if (!task_state_.item(id))
    return;
  auto &runs = task_runs_[id];
  const auto &attempt = string_field(event, "attempt");
  if (string_field(event, "phase") == "running" ||
      string_field(event, "phase") == "waiting")
    runs.insert_or_assign(attempt, event);
  else
    runs.erase(attempt);
  task_activity_.insert_or_assign(id, runs.empty() ? event : runs.begin()->second);
  if (runs.empty())
    task_runs_.erase(id);
  tasks_dirty_ = true;
  if (notification_ >= 0) {
    const char byte = 1;
    (void)::write(notification_, &byte, 1);
  }
}
void TerminalUI::run(const std::function<void(std::string_view)> &perform,
                     std::atomic_bool &cancelled,
                     const std::function<bool()> &exit_requested, std::string initial,
                     std::filesystem::path state_path,
                     const std::function<void()> &idle_work,
                     std::shared_ptr<CommandJobs> commands, int interrupt_fd,
                     const std::function<Result<void>()> &reset_interrupt) {
  (void)std::setlocale(LC_CTYPE, "");
  TerminalMode mode;
  ChatOutput output;
  output.attach(STDOUT_FILENO);
  int notifications[2];
  {
    const std::lock_guard lock{detail::process_spawn_mutex};
    if (::pipe(notifications) != 0)
      throw std::runtime_error("cannot create terminal wakeup");
    for (auto &fd : notifications) {
      if (fd < 3) {
        const auto replacement = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (replacement < 0) {
          const auto failure = errno;
          ::close(notifications[0]);
          ::close(notifications[1]);
          throw Error{ErrorCode::io, failure};
        }
        ::close(fd);
        fd = replacement;
      }
      if (::fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ||
          ::fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        const auto failure = errno;
        ::close(notifications[0]);
        ::close(notifications[1]);
        throw Error{ErrorCode::io, failure};
      }
    }
  }
  struct WakeCleanup {
    TerminalUI &ui;
    int read, write;
    struct sigaction before{};
    ~WakeCleanup() {
      resize_pipe = -1;
      (void)::sigaction(SIGWINCH, &before, nullptr);
      const std::lock_guard lock{ui.mutex_};
      ui.notification_ = -1;
      ::close(read);
      ::close(write);
    }
  } wake{*this, notifications[0], notifications[1]};
  struct sigaction action{};
  action.sa_handler = resize_signal;
  sigemptyset(&action.sa_mask);
  if (::sigaction(SIGWINCH, &action, &wake.before) != 0)
    throw std::runtime_error("cannot install terminal resize wakeup");
  resize_pipe = notifications[1];
  {
    const std::lock_guard lock{mutex_};
    notification_ = notifications[1];
  }
  RawCommandWake turn_cancel;
  RawWaitScope ui_wait{{{interrupt_fd, -1}, {}}};
  auto cancel_turn = [&] {
    cancelled.store(true);
    turn_cancel.notify();
  };
  RawRegion first_frame{raw_regions::terminal_first_frame};
  Composer composer;
  std::jthread worker;
  struct StopOnExit {
    std::atomic_bool &flag;
    RawCommandWake &wake;
    ~StopOnExit() {
      flag.store(true);
      wake.notify();
    }
  } stop{cancelled, turn_cancel};
  std::vector<std::string> queued, recovered;
  if (!state_path.empty()) {
    try {
      const auto saved = load_terminal_state(state_path);
      composer.restore(saved);
      recovered = saved.queued;
    } catch (...) {
      text("UI state damaged/unreadable; not restored. Audit untouched.\n");
    }
  }
  if (!recovered.empty())
    text("Recovered drafts: " + std::to_string(recovered.size()) +
         " · /drafts lists; /draft N loads without submitting.\n");
  bool save_failed = false;
  auto persist = [&] {
    if (state_path.empty())
      return true;
    auto saved = composer.state();
    saved.queued = recovered;
    saved.queued.insert(saved.queued.end(), queued.begin(), queued.end());
    try {
      save_terminal_state(state_path, saved);
      save_failed = false;
      return true;
    } catch (...) {
      if (!save_failed)
        text("UI state save failed; unsaved drafts retained in RAM. New work waits for "
             "a successful save.\n");
      save_failed = true;
      return false;
    }
  };

  ChatView transcript;
  bool welcoming = initial.empty();
  std::vector<std::string> welcome_lines;
  ChatGrid grid;
  ChatPainter painter;
  std::string activity = "Ready", observed_usage, small_frame;
  Value task_page;
  std::map<std::string, Value> task_activity;
  std::vector<ChatRow> task_rows;
  bool task_rows_dirty = true, tasks_hidden = false, tasks_expanded = false;
  std::size_t task_render_width = 0;
  bool live_provider = false;
  std::vector<bool> operations;
  bool busy = false, quitting = false, redraw = true, tool_active = false,
       turn_failed = false;
  std::size_t scroll = 0, previous_lines = 0;
  std::uint64_t display_generation = 0;
  bool pending_grid = false;
  auto escape_deadline = std::chrono::steady_clock::time_point::max();
  unsigned short old_rows = 0, old_columns = 0;
  std::string foreground_job, foreground_epoch, detached_epoch, child_escape,
      child_input, child_input_job, command_error;
  auto child_escape_deadline = std::chrono::steady_clock::time_point::max();
  bool foreground_pty = false, child_pasted = false;
  unsigned short child_rows = 0, child_columns = 0;
  std::size_t refused_child_bytes = 0;
  RawEventRef keyboard_read;
  auto append_child_input = [&](std::string_view bytes) {
    const auto job_size = foreground_job.size(), epoch_size = foreground_epoch.size(),
               byte_size = bytes.size();
    const bool refused = (!child_input.empty() && child_input_job != foreground_job) ||
                         bytes.size() > 4096 - child_input.size();
    const std::uint8_t branch = refused ? 2 : 1;
    (void)raw_command_linked_data(
        2042, keyboard_read,
        std::array{detail::raw_native_field(job_size),
                   detail::raw_string_field(foreground_job),
                   detail::raw_native_field(epoch_size),
                   detail::raw_string_field(foreground_epoch),
                   detail::raw_native_field(byte_size), detail::raw_string_field(bytes),
                   detail::raw_native_field(branch),
                   detail::raw_native_field(child_input.size())});
    // Keep admitted/pending bytes with their original job. Excess keyboard
    // bytes are explicitly refused; lifecycle keys must remain readable.
    if ((!child_input.empty() && child_input_job != foreground_job) ||
        bytes.size() > 4096 - child_input.size()) {
      refused_child_bytes += bytes.size();
      return;
    }
    if (child_input.empty())
      child_input_job = foreground_job;
    child_input += bytes;
  };
  auto report_refused_input = [&] {
    if (!refused_child_bytes)
      return;
    transcript += "\nPTY input refused: " + std::to_string(refused_child_bytes) +
                  " excess bytes; pending bytes preserved. Ctrl-B still backgrounds.\n";
    refused_child_bytes = 0;
    redraw = true;
  };
  auto command_request = [&](Value request) {
    try {
      commands->request(std::move(request));
      command_error.clear();
      return true;
    } catch (const Error &error) {
      const std::string message{error_name(error.code)};
      if (message != command_error)
        transcript += "\nCommand control refused: " + message + "\n";
      command_error = message;
      redraw = true;
      return false;
    } catch (const std::exception &error) {
      const std::string message{error.what()};
      if (message != command_error)
        transcript += "\nCommand control refused: " + message + "\n";
      command_error = message;
      redraw = true;
      return false;
    }
  };
  auto flush_child_input = [&] {
    if (child_input.empty())
      return true;
    auto flush_credit = raw_command_claim(50);
    const auto job_size = child_input_job.size(), byte_size = child_input.size();
    const auto flush_request =
        raw_command_data(2043, std::array{detail::raw_native_field(job_size),
                                          detail::raw_string_field(child_input_job),
                                          detail::raw_native_field(byte_size),
                                          detail::raw_string_field(child_input)});
    const bool accepted =
        command_request(Value::object({{"op", Value{"input"}},
                                       {"job_id", Value{child_input_job}},
                                       {"bytes", Value{child_input}}}));
    const Error error{};
    (void)raw_command_linked(2044, flush_request,
                             std::array{detail::raw_native_field(accepted),
                                        detail::raw_native_field(error.code),
                                        detail::raw_native_field(error.detail)},
                             flush_credit ? &*flush_credit : nullptr);
    if (accepted) {
      child_input.clear();
      child_input_job.clear();
    }
    return accepted;
  };
  auto release_command = [&](std::string_view requested) {
    if (!commands || foreground_job.empty()) {
      transcript += "\nNo foreground command wait.\n";
      return;
    }
    if (!requested.empty() && requested != foreground_job) {
      transcript += "\nBackground requires the current foreground job ID.\n";
      return;
    }
    if (command_request(
            Value::object({{"op", Value{"background"}},
                           {"job_id", Value{foreground_job}},
                           {"wait_epoch", Value{Number{foreground_epoch}}}}))) {
      detached_epoch = foreground_epoch;
      foreground_job.clear();
      foreground_pty = false;
      child_pasted = false;
      child_escape.clear();
      redraw = true;
    }
  };
  const auto *mascot_setting = std::getenv("BLACKBIRD_MASCOT");
  const bool mascot_enabled =
      !mascot_setting || std::string_view{mascot_setting} != "0";
  const bool graphics_enabled = mascot_enabled && blackbird_graphics_available();
  bool image_loaded = false;
  std::array<std::size_t, 4> image_placement{};
  std::string image_frame;
  auto started = std::chrono::steady_clock::now();
  auto second = started, tool_started = started;
  bool wake_refused = false;
  auto start = [&](const std::string &source) {
    welcoming = false;
    if (worker.joinable())
      worker.join();
    turn_cancel.retire();
    cancelled.store(false);
    auto reset = turn_cancel.clear();
    if (reset.has_value() && reset_interrupt)
      reset = reset_interrupt();
    if (!reset.has_value()) {
      cancelled.store(true);
      wake_refused = true;
      notice(std::string{"Cancellation rearm refused: "} +
             error_name(reset.error().code));
      return false;
    }
    std::string prompt;
    RawThreadHandle context;
    RawWorkerDispatch dispatch{};
    try {
      prompt = source;
      context = unwrap(raw_prepare_thread());
      dispatch = raw_worker_dispatch();
    } catch (...) {
      cancelled.store(true);
      wake_refused = true;
      notice("Turn admission refused; prompt retained.");
      return false;
    }
    busy = true;
    tool_active = false;
    turn_failed = false;
    started = std::chrono::steady_clock::now();
    transcript.append(ChatKind::user, prompt);
    observed_usage.clear();
    operations.clear();
    scroll = 0;
    activity = "Starting";
    // Each worker owns a separately registered lane. Transfer facts reference
    // the UI lane and ordinal; neither producer nor region migrates threads.
    turn_cancel.publish();
    try {
      worker = std::jthread([&, context = std::move(context), dispatch,
                             prompt = std::move(prompt)]() mutable {
        try {
          RawWaitScope wait{{{interrupt_fd, turn_cancel.read_fd()}, {}}};
          RawThreadBinding binding{std::move(context)};
          raw_worker_started(dispatch);
          RawRegion command{raw_regions::command_perform};
          try {
            perform(prompt);
            command.outcome(RawRegionOutcome::returned);
          } catch (const Error &error) {
            command.error(error);
            command.outcome(RawRegionOutcome::exception);
            post(Kind::failure, {});
            text("\nTurn failed; inspect the retained audit.\n");
          } catch (const std::exception &e) {
            command.outcome(RawRegionOutcome::exception);
            post(Kind::failure, {});
            text(std::string{"\nError: "} + e.what() + "\n");
          } catch (...) {
            command.outcome(RawRegionOutcome::exception);
            post(Kind::failure, {});
            text("\nTurn failed; inspect the retained audit.\n");
          }
          command.finish();
        } catch (...) {
          post(Kind::failure, {});
          text("\nWorker admission failed; inspect the retained raw audit.\n");
        }
        post(Kind::complete, {});
      });
    } catch (...) {
      turn_cancel.retire();
      cancelled.store(true);
      busy = false;
      wake_refused = true;
      notice("Worker launch refused; prompt retained.");
      return false;
    }
    return true;
  };
  auto edit = [&] {
    if (busy) {
      transcript += "\nEditor waits for an idle turn; draft retained.\n";
      return false;
    }
    const auto handoff_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    while (output.pending() && !output.flush()) {
      if (std::chrono::steady_clock::now() >= handoff_deadline) {
        transcript += "\nTerminal busy; editor handoff deferred. Draft retained.\n";
        return false;
      }
      pollfd fd{STDOUT_FILENO, POLLOUT, 0};
      (void)::poll(&fd, 1, 10);
    }
    if (!persist())
      return false;
    output.detach();
    mode.suspend();
    const auto edited = edit_terminal_draft(composer.text());
    mode.resume();
    image_loaded = false;
    image_placement = {};
    output.attach(STDOUT_FILENO);
    painter.invalidate();
    (void)tcflush(STDIN_FILENO,
                  TCIFLUSH); // Editor typeahead must never send the returned draft.
    if (edited.accepted && edited.text != composer.text())
      composer.draft(edited.text);
    transcript += "\n" + edited.message + "\n";
    (void)persist();
    scroll = 0;
    redraw = true;
    return true;
  };
  if (!initial.empty())
    if (!start(initial))
      queued.push_back(std::move(initial));
  while (true) {
    if (output.pending() && output.flush()) {
      first_frame.outcome(RawRegionOutcome::flushed);
      first_frame.finish();
      if (pending_grid)
        painter.commit(grid);
      else
        painter.invalidate();
    }
    std::vector<Message> incoming;
    {
      const std::lock_guard lock{mutex_};
      incoming.swap(messages_);
      if (tasks_dirty_) {
        Value::Array folded;
        for (const auto &id : task_folded_)
          folded.push_back(Value{id});
        const auto count = task_state_.size();
        if (count)
          welcoming = false;
        task_offset_ = std::min(task_offset_, count ? count - 1 : 0);
        task_page =
            task_state_.read(Value::object({{"offset", Value{Number{task_offset_}}},
                                            {"limit", Value{Number{"64"}}},
                                            {"collapsed", Value{std::move(folded)}}}));
        const auto &visible_tasks = field(task_page, "items").array();
        task_anchor_ =
            visible_tasks.empty() ? "" : string_field(visible_tasks.front(), "id");
        task_activity = task_activity_;
        tasks_dirty_ = false;
        task_rows_dirty = true;
        redraw = true;
      }
    }
    for (const auto &message : incoming) {
      if (message.kind == Kind::sessions) {
        std::vector<ComposerChoice> entries;
        const auto &listing = message.data;
        for (const auto &entry : field(listing, "sessions").array()) {
          const auto &path = string_field(entry, "path");
          if (path.find_first_of("\r\n") != std::string::npos)
            continue;
          const auto &config = field(entry, "configuration");
          const auto *name = config.find("name");
          entries.push_back({"/resume " + path, "", path, "Session",
                             name && !name->string().empty()
                                 ? name->string()
                                 : std::filesystem::path{path}.filename().string()});
        }
        composer.choices(std::move(entries), "Sessions");
      }
      if (message.kind == Kind::text || message.kind == Kind::process ||
          message.kind == Kind::failure || message.kind == Kind::user ||
          message.kind == Kind::assistant)
        welcoming = false;
      if (message.kind == Kind::text)
        transcript.append(turn_failed ? ChatKind::error : ChatKind::assistant,
                          message.text, true);
      if (message.kind == Kind::notice)
        transcript += message.text;
      if (message.kind == Kind::process)
        transcript.append(ChatKind::tool, message.text, true);
      if (message.kind == Kind::user || message.kind == Kind::assistant)
        transcript.append(message.kind == Kind::user ? ChatKind::user
                                                     : ChatKind::assistant,
                          message.text);
      if (message.kind == Kind::usage)
        observed_usage = message.text;
      if (message.kind == Kind::provider_start || message.kind == Kind::tool_start) {
        live_provider = message.kind == Kind::provider_start;
        operations.push_back(live_provider);
      }
      if (message.kind == Kind::status) {
        activity = message.text;
        tool_started = std::chrono::steady_clock::now();
        tool_active = !live_provider;
        if (!live_provider)
          transcript.append(ChatKind::tool, "▸ " + message.text + "\n");
      }
      if (message.kind == Kind::operation_complete) {
        activity = message.text;
        tool_active = false;
        if (!live_provider)
          transcript.append(
              message.outcome == Ink::failure ? ChatKind::error : ChatKind::tool,
              (message.outcome == Ink::success ? "✓ " : "• ") + message.text);
        if (!operations.empty())
          operations.pop_back();
        live_provider = !operations.empty() && operations.back();
      }
      if (message.kind == Kind::failure)
        turn_failed = true;
      if (message.kind == Kind::title)
        title_ = message.text;
      if (message.kind == Kind::complete) {
        if (worker.joinable())
          worker.join();
        turn_cancel.retire();
        busy = false;
        tool_active = false;
        activity = cancelled.load() ? "Turn cancelled"
                   : turn_failed    ? "Turn failed"
                                    : "Turn completed";
        activity += " · " +
                    std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::steady_clock::now() - started)
                                       .count()) +
                    "s";
        transcript.append(turn_failed ? ChatKind::error : ChatKind::summary,
                          activity +
                              (observed_usage.empty() ? "" : " · " + observed_usage));
      }
      redraw = true;
    }
    if (!busy && idle_work)
      idle_work();
    if (commands) {
      pollfd child_input_ready{STDIN_FILENO, POLLIN, 0};
      if (foreground_pty && !child_escape.empty() &&
          std::chrono::steady_clock::now() >= child_escape_deadline &&
          poll(&child_input_ready, 1, 0) == 0) {
        append_child_input(child_escape);
        child_escape.clear();
      }
      // Preserve the existing input batching despite bounded native reads.
      // Flush a full prefix or when the kernel has no further input ready.
      child_input_ready.revents = 0;
      const bool more_input = poll(&child_input_ready, 1, 0) > 0;
      if (child_input.size() >= 4096 || !more_input)
        (void)flush_child_input();
      const auto lease = commands->foreground();
      auto job = string_field(lease, "job_id");
      const auto epoch = field(lease, "wait_epoch").number().text();
      if (epoch == detached_epoch)
        job.clear();
      const bool pty = !job.empty() && std::get<bool>(field(lease, "pty").value());
      if (job != foreground_job || epoch != foreground_epoch || pty != foreground_pty) {
        if (!child_escape.empty()) {
          append_child_input(child_escape);
          (void)flush_child_input();
        }
        foreground_job = std::move(job);
        foreground_epoch = epoch;
        foreground_pty = pty;
        child_rows = child_columns = 0;
        child_escape.clear();
        child_pasted = false;
        redraw = true;
      }
      report_refused_input();
    }
    if (!busy && exit_requested && exit_requested())
      break;
    if (!busy && quitting)
      break;
    if (!busy && !wake_refused && !queued.empty()) {
      if (persist()) {
        if (start(queued.front()))
          queued.erase(queued.begin());
      } else {
        recovered.insert(recovered.end(), std::make_move_iterator(queued.begin()),
                         std::make_move_iterator(queued.end()));
        queued.clear();
      }
      redraw = true;
    }
    winsize dimensions{};
    (void)ioctl(STDOUT_FILENO, TIOCGWINSZ, &dimensions);
    const auto rows =
        dimensions.ws_row ? dimensions.ws_row : static_cast<unsigned short>(24);
    const auto columns =
        dimensions.ws_col ? dimensions.ws_col : static_cast<unsigned short>(80);
    if (foreground_pty && (rows != child_rows || columns != child_columns)) {
      if (command_request(Value::object(
              {{"op", Value{"resize"}},
               {"job_id", Value{foreground_job}},
               {"rows", Value{Number{std::min<unsigned short>(rows, 512)}}},
               {"columns", Value{Number{std::min<unsigned short>(columns, 512)}}}}))) {
        child_rows = rows;
        child_columns = columns;
      }
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - second >= std::chrono::milliseconds{80}) {
      second = now;
      if (busy)
        redraw = true;
    }
    if (rows != old_rows || columns != old_columns)
      redraw = true;
    if (redraw && !output.pending()) {
      RawRegion render{raw_regions::command_render};
      const bool same_width = old_columns == columns;
      const bool geometry_changed = old_columns != columns || old_rows != rows;
      old_rows = rows;
      old_columns = columns;
      redraw = false;
      const auto width = static_cast<std::size_t>(columns > 1 ? columns - 1 : 1);
      if (rows < 8 || columns < 12) {
        const auto draft = terminal_lines("> " + composer.text(), width);
        small_frame = "\x1b[?2026h\x1b[H\x1b[2J" + draft.back();
        small_frame += "\x1b[1;" +
                       std::to_string(std::min(static_cast<std::size_t>(columns),
                                               display_width(draft.back()) + 1)) +
                       "H";
        if (graphics_enabled && image_placement[2]) {
          small_frame += blackbird_graphics_erase();
          image_placement = {};
        }
        small_frame += "\x1b[?2026l";
        pending_grid = false;
        output.start(small_frame);
        if (output.flush()) {
          first_frame.outcome(RawRegionOutcome::flushed);
          first_frame.finish();
        }
        painter.invalidate();
        render.outcome(RawRegionOutcome::small_frame);
        continue;
      }
      const bool show_task_pane =
          !welcoming && !tasks_hidden && width >= 90 && rows >= 18;
      const auto pane_width =
          show_task_pane ? std::clamp(width / 3, std::size_t{28}, std::size_t{36}) : 0;
      const auto main_width = show_task_pane ? width - pane_width - 2 : width;
      const auto inner_width = main_width - 4;
      const auto composed =
          terminal_powerword_lines("> " + composer.text(), inner_width);
      const auto prefix = terminal_lines(
          "> " + composer.text().substr(0, composer.cursor()), inner_width);
      const auto draft_height = std::min<std::size_t>(
          {6, composed.size(), static_cast<std::size_t>(rows - 7)});
      const auto palette =
          composer.palette_lines(std::min<std::size_t>(8, rows - 5 - draft_height));
      const auto height =
          static_cast<std::size_t>(rows - 5) - draft_height - palette.size();
      const bool show_sprite = !welcoming && mascot_enabled &&
                               (!tasks_expanded || show_task_pane) &&
                               width >= (graphics_enabled ? 70U : 90U) &&
                               height + 2 >= blackbird_sprite_height;
      const auto header_width = show_task_pane ? main_width
                                : show_sprite  ? width - blackbird_sprite_width - 2
                                               : width;
      // The square occupies a right gutter, including beside the first chat rows.
      // Wrap into the remaining columns so a sprite can never overwrite content.
      const auto &lines = transcript.rows(header_width);
      if (welcoming && (geometry_changed || welcome_lines.empty()))
        welcome_lines =
            blackbird_startup(width - 4, height > 2 ? height - 2 : 0, mascot_enabled);
      if (display_generation != transcript.generation()) {
        scroll =
            0; // Evicted display history: return to live tail, never a false anchor.
        display_generation = transcript.generation();
      }
      if (scroll > 0 && same_width)
        scroll = terminal_scroll_after_output({.scroll = scroll,
                                               .previous = previous_lines,
                                               .before_trim = lines.size(),
                                               .after_trim = lines.size(),
                                               .height = height});
      previous_lines = lines.size();
      scroll = std::min(scroll, lines.size() > height ? lines.size() - height : 0);
      const auto end = lines.size() - scroll;
      const auto begin = end > height ? end - height : 0;
      grid.reset(width, rows);
      auto title = title_;
      const auto path = title.rfind(" · /");
      if (path != std::string::npos)
        title = title.substr(0, path) + " · " +
                std::filesystem::path(title.substr(path + 4)).filename().string();
      grid.line(0,
                {{" " + terminal_lines(title, header_width - 1)[0], Ink::assistant}});
      std::string status_line;
      if (busy)
        status_line = "Turn " +
                      std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                         now - started)
                                         .count()) +
                      "s · ";
      if (tool_active)
        status_line += "Tool " +
                       std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                          now - tool_started)
                                          .count()) +
                       "s · ";
      if (!queued.empty())
        status_line += "queued " + std::to_string(queued.size()) + " · ";
      if (scroll > 0)
        status_line += "Up " + std::to_string(scroll) + " · ";
      status_line += activity;
      constexpr std::array<std::string_view, 10> spinner{"⠋", "⠙", "⠹", "⠸", "⠼",
                                                         "⠴", "⠦", "⠧", "⠇", "⠏"};
      const auto phase =
          static_cast<std::size_t>(
              std::chrono::duration_cast<std::chrono::milliseconds>(now - started)
                  .count() /
              80) %
          spinner.size();
      const auto status_text = terminal_lines(std::string{busy ? spinner[phase] : "·"} +
                                                  " " + status_line + " ",
                                              header_width)[0];
      std::string rule;
      for (auto n = display_width(status_text); n < header_width; ++n)
        rule += "─";
      grid.line(1, {{status_text, busy ? Ink::user : Ink::muted}, {rule, Ink::border}});
      if (show_sprite) {
        const auto &sprite = blackbird_sprite(busy, phase / 2);
        const auto ink = turn_failed   ? Ink::failure
                         : tool_active ? Ink::user
                         : busy        ? Ink::assistant
                                       : Ink::muted;
        for (std::size_t row = 0; row < sprite.size(); ++row)
          if (graphics_enabled)
            grid.line(row, {{"│", ink}}, width - blackbird_sprite_width - 1);
          else
            grid.line(row, {{sprite[row], ink}}, width - blackbird_sprite_width);
      }
      std::array<std::size_t, 4> placement{};
      if (welcoming) {
        std::size_t lettering_row = 2, lettering_column = 2;
        if (graphics_enabled) {
          std::size_t lettering_width = 0;
          for (const auto &line : welcome_lines)
            lettering_width = std::max(lettering_width, display_width(line));
          const auto lettering_height = welcome_lines.size();
          const auto reserved = lettering_width + 6;
          if (width >= reserved + 24 &&
              height >= std::max<std::size_t>(12, lettering_height)) {
            const auto side = std::min(height, (width - reserved) / 2);
            placement = {2, 2, side * 2, side};
            lettering_column = side * 2 + 6;
            lettering_row += (side - lettering_height) / 2;
          } else if (height > lettering_height + 1) {
            const auto side = std::min(height - lettering_height - 1, (width - 4) / 2);
            placement = {lettering_row + lettering_height + 1, 2, side * 2, side};
          }
        }
        for (std::size_t row = 0; row < std::min(height, welcome_lines.size()); ++row)
          grid.line(row + lettering_row, {{welcome_lines[row], Ink::assistant}},
                    lettering_column);
        if (!graphics_enabled && welcome_lines.size() + 1 < height)
          grid.line(welcome_lines.size() + 3,
                    {{"  Enter continues · / commands · Workflow reloads each turn.",
                      Ink::muted}});
      } else if (!tasks_expanded || show_task_pane)
        for (std::size_t row = 0; row < height; ++row)
          if (begin + row < end)
            grid.line(row + 2, lines[begin + row]);
      if (graphics_enabled && show_sprite)
        placement = {0, width - blackbird_sprite_width, blackbird_sprite_width,
                     blackbird_sprite_height};
      const auto task_width = show_task_pane ? pane_width : main_width;
      if (task_rows_dirty || task_render_width != task_width) {
        task_rows = task_pane_rows(task_page, task_width, task_activity);
        task_render_width = task_width;
        task_rows_dirty = false;
      }
      if (show_task_pane) {
        for (std::size_t row = 0; row < rows; ++row)
          grid.line(row, {{"│", Ink::border}}, main_width);
        const auto task_start = show_sprite ? blackbird_sprite_height + 1 : 2;
        for (std::size_t row = 0; row < task_rows.size() && row + task_start + 1 < rows;
             ++row)
          grid.line(row + task_start, task_rows[row], main_width + 2);
        grid.line(rows - 1, {{"/tasks up/down · fold ID", Ink::muted}}, main_width + 2);
      } else if (tasks_expanded && !welcoming) {
        for (std::size_t row = 0; row < height && row < task_rows.size(); ++row)
          grid.line(row + 2, task_rows[row]);
      } else if (!welcoming && !busy && !tasks_hidden &&
                 !field(task_page, "items").array().empty()) {
        grid.line(1, {{terminal_lines("Tasks: " + task_summary(task_page) +
                                          " · /tasks expand",
                                      header_width)[0],
                       Ink::muted}});
      }
      std::size_t menu_row = height + 2;
      for (const auto &line : palette) {
        const auto text = terminal_lines(line, main_width)[0];
        const bool selected = text.starts_with("> ");
        if (selected || text.starts_with("  /")) {
          const auto name_end = text.find(' ', 2);
          const auto command_end =
              name_end == std::string::npos ? text.size() : name_end;
          grid.line(menu_row++, {{text.substr(0, command_end),
                                  selected ? Ink::selected : Ink::normal},
                                 {text.substr(command_end), Ink::muted}});
        } else {
          grid.line(menu_row++,
                    {{text, line.starts_with("Commands") ? Ink::heading : Ink::muted}});
        }
      }
      std::string hint = composer.palette_open() ? " command palette "
                         : composer.slash_open() ? " commands "
                         : busy ? " Enter queue · Ctrl-C stop "
                                : " Enter send · / commands · Ctrl-G editor ";
      if (!foreground_job.empty())
        hint = std::string{foreground_pty ? " PTY input "
                                          : " Enter queue · Foreground command "} +
               foreground_job + " · Ctrl-B background ";
      auto border = [&](std::pair<std::string_view, std::string_view> corners,
                        std::string_view label) {
        std::string out{corners.first};
        const auto clipped = terminal_lines(label, main_width - 2)[0];
        out += clipped;
        for (auto n = display_width(clipped); n < main_width - 2; ++n)
          out += "─";
        return out + std::string{corners.second};
      };
      grid.line(menu_row++, {{border({"╭", "╮"}, hint), Ink::border}});
      const auto cursor_row = prefix.size() - 1;
      const auto draft_begin =
          cursor_row >= draft_height ? cursor_row - draft_height + 1 : 0;
      for (std::size_t row = 0; row < draft_height; ++row) {
        const auto line = draft_begin + row < composed.size()
                              ? chat_plain(composed[draft_begin + row])
                              : "";
        ChatRow content{{"│ ", Ink::border}};
        if (draft_begin + row < composed.size()) {
          const auto &colored = composed[draft_begin + row];
          content.insert(content.end(), colored.begin(), colored.end());
        }
        content.push_back(
            {std::string(inner_width - std::min(inner_width, display_width(line)),
                         ' ') +
                 " │",
             Ink::border});
        grid.line(menu_row++, content);
      }
      grid.line(menu_row, {{border({"╰", "╯"}, ""), Ink::border}});
      const auto cursor_line = height + palette.size() + 4 + cursor_row - draft_begin;
      // Derive cursor cells with the same terminal width rules, including UTF-8.
      std::size_t cells = 0;
      std::mbstate_t state{};
      auto remain = std::string_view{prefix.back()};
      while (!remain.empty()) {
        wchar_t wide{};
        const auto n = std::mbrtowc(&wide, remain.data(), remain.size(), &state);
        if (n == 0 || n > remain.size())
          break;
        cells += static_cast<std::size_t>(std::max(0, ::wcwidth(wide)));
        remain.remove_prefix(n);
      }
      const auto display_cursor_line =
          composer.palette_open() ? height + 3 : cursor_line;
      const auto display_cursor_cells =
          composer.palette_open()
              ? display_width(palette.front().substr(0, palette.front().size() - 1)) + 1
              : cells + 3;
      const auto &packet =
          painter.prepare(grid, display_cursor_line - 1,
                          std::min(display_cursor_cells, main_width) - 1);
      if (!packet.empty()) {
        pending_grid = true;
        if (graphics_enabled && placement != image_placement) {
          image_frame.assign(packet);
          // Insert image commands before the existing synchronized-update close.
          image_frame.resize(image_frame.size() -
                             std::string_view{"\x1b[?2026l"}.size());
          image_frame += blackbird_graphics_erase();
          if (placement[2] && placement[3]) {
            if (!image_loaded) {
              image_frame += blackbird_graphics_load();
              image_loaded = true;
            }
            image_frame += blackbird_graphics_place(placement[0], placement[1],
                                                    placement[2], placement[3]);
          }
          image_frame += "\x1b[?2026l";
          image_placement = placement;
          output.start(image_frame);
        } else
          output.start(packet);
        if (output.flush()) {
          first_frame.outcome(RawRegionOutcome::flushed);
          first_frame.finish();
          painter.commit(grid);
        }
      }
      render.outcome(output.pending() ? RawRegionOutcome::queued
                                      : RawRegionOutcome::flushed);
    }
    std::array<pollfd, 3> inputs{
        {{STDIN_FILENO, POLLIN, 0},
         {notifications[0], POLLIN, 0},
         {STDOUT_FILENO, static_cast<short>(output.pending() ? POLLOUT : 0), 0}}};
    const auto ready = poll(inputs.data(), inputs.size(),
                            composer.escape_pending() || !child_escape.empty() ? 50
                            : busy || commands                                 ? 40
                                                                               : 1000);
    if (ready < 0) {
      if (errno == EINTR)
        continue;
      throw std::runtime_error("Terminal polling failed");
    }
    if (inputs[1].revents & POLLIN) {
      char bytes[128];
      while (::read(notifications[0], bytes, sizeof(bytes)) > 0) {
      }
    }
    if (!(inputs[0].revents & (POLLIN | POLLHUP | POLLERR))) {
      if (std::chrono::steady_clock::now() >= escape_deadline &&
          composer.flush_escape())
        redraw = true;
      continue;
    }
    char bytes[128];
    RawRegion input{raw_regions::command_input_save};
    std::optional<RawCapacityClaim> keyboard_data;
    if (raw_enabled())
      keyboard_data.emplace(unwrap(raw_claim_data(1155081)));
    auto keyboard_control = raw_command_claim(21950 + raw_wake_single_notify_credit);
    std::optional<RawCommandBurstScope> keyboard_burst;
    keyboard_burst.emplace(
        RawCommandBurstCredits{keyboard_data ? &*keyboard_data : nullptr,
                               keyboard_control ? &*keyboard_control : nullptr});
    const auto requested = sizeof(bytes);
    const int descriptor = STDIN_FILENO;
    const auto keyboard_request =
        raw_command_record(2040,
                           std::array{detail::raw_native_field(descriptor),
                                      detail::raw_native_field(requested)},
                           keyboard_control ? &*keyboard_control : nullptr);
    const auto count = read(STDIN_FILENO, bytes, sizeof(bytes));
    const int read_error = errno;
    const auto literal =
        count > 0 ? std::as_bytes(std::span{bytes, static_cast<std::size_t>(count)})
                  : ByteView{};
    keyboard_read =
        raw_command_linked(2041, keyboard_request,
                           std::array{detail::raw_native_field(count),
                                      detail::raw_native_field(read_error), literal},
                           count > 0          ? nullptr
                           : keyboard_control ? &*keyboard_control
                                              : nullptr);
    if (count < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        continue;
      throw Error{ErrorCode::io, errno};
    }
    if (count == 0) {
      quitting = true;
      cancel_turn();
      persist();
      continue;
    }
    bool saved_last_input = false;
    for (ssize_t i = 0; i < count; ++i) {
      saved_last_input = false; // Later bytes can change the submitted state.
      if (foreground_pty) {
        const char byte = bytes[i];
        // The outer terminal's paste envelope is transport, not child input.
        // Parse across reads; controls inside it remain literal child bytes.
        if (byte == '\x02' && !child_pasted) {
          append_child_input(child_escape);
          child_escape.clear();
          (void)flush_child_input();
          release_command({});
        } else if (!child_escape.empty() || byte == '\x1b') {
          if (child_escape.empty())
            child_escape_deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
          child_escape += byte;
          const std::string_view begin{"\x1b[200~"}, end{"\x1b[201~"};
          if (child_escape == begin) {
            child_pasted = true;
            child_escape.clear();
          } else if (child_escape == end) {
            child_pasted = false;
            child_escape.clear();
          } else if (!begin.starts_with(child_escape) &&
                     !end.starts_with(child_escape)) {
            append_child_input(child_escape);
            child_escape.clear();
          }
        } else {
          append_child_input(std::string_view{&byte, 1});
        }
        redraw = true;
        continue;
      }
      if (bytes[i] == '\x02' && !foreground_job.empty() && !composer.pasted() &&
          !composer.escape_pending()) {
        release_command({});
        continue;
      }
      const bool was_escape = composer.escape_pending();
      auto result = composer.feed(bytes[i]);
      welcoming = false; // First operator input reveals retained chat and the avatar.
      if (!was_escape && composer.escape_pending())
        escape_deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
      redraw = true;
      if (result.action == InputAction::choose_command) {
        bool load = true;
        if (!composer.text().empty()) {
          auto candidate = composer.state();
          candidate.queued = recovered;
          candidate.queued.insert(candidate.queued.end(), queued.begin(), queued.end());
          candidate.queued.push_back(composer.text());
          try {
            validate_state(candidate);
            recovered.push_back(composer.text());
          } catch (const Error &) {
            load = false;
            transcript +=
                "\nDraft storage full; original retained, command not loaded.\n";
          }
        }
        if (load) {
          const bool saved_draft = !composer.text().empty();
          composer.draft(std::move(result.text));
          transcript += saved_draft ? "\nCommand loaded; Enter sends explicitly. "
                                      "Previous draft in /drafts.\n"
                                    : "\nCommand loaded; Enter sends explicitly.\n";
        }
      }
      if (result.action == InputAction::edit && edit())
        break;
      if (result.action == InputAction::page_up)
        scroll += static_cast<std::size_t>(rows / 2);
      if (result.action == InputAction::page_down)
        scroll -= std::min(scroll, static_cast<std::size_t>(rows / 2));
      if (result.action == InputAction::cancel) {
        queued.clear();
        if (busy) {
          cancel_turn();
          activity = "Stopping";
        }
      }
      if (result.action == InputAction::quit ||
          (result.action == InputAction::submit &&
           (result.text == "/quit" || result.text == "/exit"))) {
        quitting = true;
        cancel_turn();
      }
      if (result.action == InputAction::submit && !result.text.empty() && !quitting) {
        if (commands && result.text == "/jobs") {
          try {
            transcript.append(ChatKind::assistant,
                              unwrap(dump_json(commands->read(Value::object({})))) +
                                  "\n");
          } catch (const Error &error) {
            transcript += std::string{"\nCommand discovery refused: "} +
                          error_name(error.code) + "\n";
          }
          scroll = 0;
        } else if (commands &&
                   (result.text == "/bg" || result.text.starts_with("/bg "))) {
          release_command(result.text == "/bg"
                              ? std::string_view{}
                              : std::string_view{result.text}.substr(4));
        } else if (commands && busy && result.text.starts_with("/fg ") &&
                   !string_field(commands->foreground(), "job_id").empty()) {
          transcript +=
              string_field(commands->foreground(), "job_id") == result.text.substr(4)
                  ? "\nThat command is already foreground.\n"
                  : "\nRelease the current turn before foregrounding another "
                    "command.\n";
        } else if (commands && busy && result.text.starts_with("/process ")) {
          try {
            auto request = unwrap(parse_json(std::string_view{result.text}.substr(9)));
            const auto op = string_field(request, "op");
            if (op == "read")
              transcript.append(ChatKind::assistant,
                                unwrap(dump_json(commands->read(request))) + "\n");
            else if (op == "input" || op == "resize" || op == "signal" ||
                     op == "stop" || op == "background")
              (void)command_request(std::move(request));
            else
              transcript += "\nThat process operation requires an idle turn.\n";
          } catch (const Error &error) {
            transcript += std::string{"\nInvalid process control: "} +
                          error_name(error.code) + "\n";
          } catch (const std::exception &error) {
            transcript +=
                std::string{"\nInvalid process control: "} + error.what() + "\n";
          }
        } else if (result.text == "/tasks up" || result.text == "/tasks down" ||
                   result.text == "/tasks expand" || result.text == "/tasks show" ||
                   result.text == "/tasks hide" ||
                   result.text.starts_with("/tasks fold ")) {
          const std::lock_guard lock{mutex_};
          task_anchor_.clear();
          if (result.text == "/tasks up")
            task_offset_ -= std::min(task_offset_, std::size_t{2});
          else if (result.text == "/tasks down")
            task_offset_ += 2;
          else if (result.text == "/tasks hide") {
            tasks_hidden = true;
            tasks_expanded = false;
          } else if (result.text == "/tasks show") {
            tasks_hidden = false;
            tasks_expanded = false;
          } else if (result.text == "/tasks expand") {
            tasks_hidden = false;
            tasks_expanded = !tasks_expanded;
          } else {
            const auto id = result.text.substr(12);
            if (const auto *row = task_state_.item(id);
                row && string_field(*row, "parent").empty()) {
              if (!task_folded_.erase(id))
                task_folded_.insert(id);
              task_offset_ = *task_state_.position(id);
            } else
              transcript += "\nFold requires a top-level task ID.\n";
          }
          tasks_dirty_ = true;
        } else if (result.text == "/commands") {
          (void)composer.feed(0);
        } else if (result.text == "/edit") {
          if (edit())
            break;
        } else if (result.text == "/help" || result.text == "/keys") {
          transcript +=
              "\n" + (result.text == "/help" ? terminal_help() : terminal_key_help());
          scroll = 0;
        } else if (result.text == "/queue") {
          transcript += "\nPending prompts: " + std::to_string(queued.size()) + "\n";
          for (std::size_t n = 0; n < queued.size(); ++n)
            transcript += std::to_string(n + 1) + ": " + queued[n] + "\n";
          transcript += "Recovered drafts: " + std::to_string(recovered.size()) +
                        " (use /drafts)\n";
          scroll = 0;
        } else if (result.text == "/cancel") {
          queued.clear();
          if (busy) {
            cancel_turn();
            activity = "Stopping";
          }
          transcript += busy ? "\nStop requested; pending queue cleared.\n"
                             : "\nNo active turn; pending queue cleared.\n";
          scroll = 0;
        } else if (result.text == "/clear") {
          transcript.clear();
          scroll = 0;
        } else if (result.text == "/drafts") {
          transcript +=
              "\nRecovered drafts: " + std::to_string(recovered.size()) + "\n";
          for (std::size_t n = 0; n < recovered.size(); ++n)
            transcript += std::to_string(n + 1) + ": " + recovered[n] + "\n";
        } else if (result.text.starts_with("/draft ")) {
          std::size_t n = 0;
          const auto arg = std::string_view{result.text}.substr(7);
          const auto r = std::from_chars(arg.data(), arg.data() + arg.size(), n);
          if (r.ec != std::errc{} || r.ptr != arg.data() + arg.size() || n == 0 ||
              n > recovered.size())
            transcript += "\nInvalid recovered draft number.\n";
          else {
            composer.draft(std::move(recovered[n - 1]));
            recovered.erase(recovered.begin() + static_cast<std::ptrdiff_t>(n - 1));
            transcript += "\nLoaded recovered draft; Enter submits explicitly.\n";
          }
        } else if (busy || !queued.empty()) {
          wake_refused =
              false; // An explicit submission permits the queued owner to retry.
          auto candidate = composer.state();
          candidate.queued = recovered;
          candidate.queued.insert(candidate.queued.end(), queued.begin(), queued.end());
          candidate.queued.push_back(result.text);
          try {
            validate_state(candidate);
            queued.push_back(std::move(result.text));
          } catch (const Error &) {
            composer.draft(std::move(result.text));
            transcript += "\nQueue full; prompt retained in composer.\n";
          }
        } else {
          if (persist()) {
            saved_last_input = true;
            input.outcome(RawRegionOutcome::dispatch);
            input.finish();
            keyboard_burst.reset();
            keyboard_data.reset();
            keyboard_control.reset();
            wake_refused = false;
            if (!start(result.text))
              composer.draft(std::move(result.text));
          } else
            composer.draft(std::move(result.text));
        }
      }
    }
    pollfd next_input{STDIN_FILENO, POLLIN, 0};
    if (child_input.size() >= 4096 || poll(&next_input, 1, 0) == 0)
      (void)flush_child_input();
    report_refused_input();
    // A final-byte submission already saved this exact state before dispatch.
    // Do not serialize/rename/fsync it again; trailing input still gets saved.
    if (!saved_last_input)
      persist();
  }
  persist();
}
} // namespace blackbird
