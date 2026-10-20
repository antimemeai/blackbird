#include "blackbird/command_center.hpp"
#include "blackbird/terminal.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <limits>
#include <type_traits>

namespace blackbird {
namespace {
constexpr std::string_view collection_key = "work", detail_key = "detail";
enum class Kind : std::uint8_t { none, task, participant, command };
struct Selection {
  Kind kind = Kind::none;
  TaskObservationId task;
  ParticipantObservationId participant;
  CommandObservationId command;
  bool operator==(const Selection &) const = default;
};
std::string task_id(TaskObservationId id) { return "t" + std::to_string(id.ordinal); }
std::string key(const Selection &selection) {
  switch (selection.kind) {
  case Kind::task:
    return "task:" + task_id(selection.task);
  case Kind::participant:
    return "participant:" + std::string{selection.participant.view()};
  case Kind::command:
    return "command:" + std::string{selection.command.view()};
  case Kind::none:
    return {};
  }
  return {};
}
Selection parse_key(std::string_view value) {
  Selection selected;
  if (value.starts_with("task:t")) {
    selected.kind = Kind::task;
    const auto number = value.substr(6);
    const auto parsed = std::from_chars(number.data(), number.data() + number.size(),
                                        selected.task.ordinal);
    if (number.empty() || number.front() == '0' || parsed.ec != std::errc{} ||
        parsed.ptr != number.data() + number.size() || !selected.task.ordinal)
      throw Error{ErrorCode::invalid_identity};
  } else if (value.starts_with("participant:")) {
    const auto id = value.substr(12);
    if (id.empty() || id.size() > selected.participant.bytes.size())
      throw Error{ErrorCode::invalid_identity};
    selected.kind = Kind::participant;
    selected.participant.size = id.size();
    std::copy(id.begin(), id.end(), selected.participant.bytes.begin());
  } else if (value.starts_with("command:")) {
    const auto id = value.substr(8);
    if (id.size() != selected.command.bytes.size())
      throw Error{ErrorCode::invalid_identity};
    selected.kind = Kind::command;
    std::copy(id.begin(), id.end(), selected.command.bytes.begin());
  } else
    throw Error{ErrorCode::invalid_identity};
  return selected;
}
tui::SceneCandidate empty_scene(tui::ViewIdentity view, std::uint64_t revision) {
  tui::SceneCandidate scene;
  scene.view = view;
  scene.base_revision = revision;
  scene.nodes = {{std::string{collection_key}, {}, tui::NodeKind::collection, true},
                 {std::string{detail_key}, {}, tui::NodeKind::inspector, true}};
  scene.collections = {{std::string{collection_key}, {}, "CURRENT SESSION", {}}};
  scene.inspectors = {{std::string{detail_key}, "DETAIL", {}, {}, {}, 0}};
  return scene;
}
tui::SceneHost initial_host() {
  static std::atomic<std::uint64_t> next{1};
  auto incarnation = next.load(std::memory_order_relaxed);
  for (;;) {
    if (!incarnation || incarnation == UINT64_MAX)
      throw Error{ErrorCode::overflow};
    if (next.compare_exchange_weak(incarnation, incarnation + 1,
                                   std::memory_order_relaxed))
      break;
  }
  const tui::ViewIdentity view{2, incarnation};
  auto host = unwrap(tui::SceneHost::create(view, {4, 32768, 196608}));
  unwrap(host.publish(empty_scene(view, 0)));
  return host;
}
// The 256-byte participant copy names its clipping and the original length.
std::string observed(const ParticipantObservationText &value) {
  std::string result{value.view()};
  if (value.truncated)
    result += " [clipped: " + std::to_string(value.size) + " of " +
              std::to_string(value.total) + " bytes]";
  return result;
}
std::string safe_label(std::string_view text) {
  std::string result;
  for (const auto &line :
       terminal_lines(text, std::numeric_limits<std::size_t>::max())) {
    if (!result.empty())
      result += ' ';
    result += line;
  }
  return result;
}
void lines(tui::Inspector &detail, std::string_view text, Ink ink = Ink::normal) {
  for (auto &line : terminal_lines(text, std::numeric_limits<std::size_t>::max()))
    detail.lines.push_back({{std::move(line), ink}});
}
std::string_view task_status(TaskDeclaredStatus status) {
  switch (status) {
  case TaskDeclaredStatus::queued:
    return "queued";
  case TaskDeclaredStatus::active:
    return "active";
  case TaskDeclaredStatus::blocked:
    return "blocked";
  case TaskDeclaredStatus::done:
    return "done";
  case TaskDeclaredStatus::dropped:
    return "dropped";
  }
  return "unavailable";
}
std::string_view participant_phase(ParticipantObservationPhase phase) {
  switch (phase) {
  case ParticipantObservationPhase::running:
    return "running";
  case ParticipantObservationPhase::waiting:
    return "waiting";
  case ParticipantObservationPhase::completed:
    return "completed";
  case ParticipantObservationPhase::cancelled:
    return "cancelled";
  case ParticipantObservationPhase::failed:
    return "failed";
  case ParticipantObservationPhase::unknown:
    return "unknown";
  }
  return "unavailable";
}
std::string_view command_phase(CommandObservationPhase phase) {
  switch (phase) {
  case CommandObservationPhase::starting:
    return "starting";
  case CommandObservationPhase::running:
    return "running";
  case CommandObservationPhase::stopped:
    return "stopped";
  case CommandObservationPhase::draining:
    return "draining";
  case CommandObservationPhase::settling:
    return "settling";
  case CommandObservationPhase::settled:
    return "settled";
  }
  return "unavailable";
}
struct Wire {
  std::array<std::byte, 65536> bytes{};
  std::size_t size = 0;
  void append(ByteView value) {
    if (value.size() > bytes.size() - size)
      throw Error{ErrorCode::capacity};
    std::copy(value.begin(), value.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(size));
    size += value.size();
  }
  template <class T> void scalar(const T &value) {
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>);
    append(std::as_bytes(std::span{&value, std::size_t{1}}));
  }
  template <class... T> void scalars(const T &...value) { (scalar(value), ...); }
  void reference(const RawEventRef &value) {
    append(value.lane);
    scalar(value.ordinal);
  }
  void text(std::string_view value) {
    scalar(value.size());
    append(std::as_bytes(std::span{value.data(), value.size()}));
  }
  template <std::size_t N> void text(const TaskObservationText<N> &value) {
    if (value.size > N)
      throw Error{ErrorCode::invalid_range};
    text(value.view());
  }
  void text(const ParticipantObservationText &value) {
    if (value.size > value.bytes.size())
      throw Error{ErrorCode::invalid_range};
    scalars(value.present, value.truncated, value.total);
    text(value.view());
  }
  void id(const std::optional<TaskObservationId> &value) {
    scalar(value.has_value());
    if (value)
      scalar(value->ordinal);
  }
  void id(const std::optional<ParticipantObservationId> &value) {
    scalar(value.has_value());
    if (value) {
      if (value->size > value->bytes.size())
        throw Error{ErrorCode::invalid_range};
      text(value->view());
    }
  }
  void id(const std::optional<CommandObservationId> &value) {
    scalar(value.has_value());
    if (value)
      append(std::as_bytes(std::span{value->bytes}));
  }
  void cursor(const std::optional<TaskObservationCursor> &value) {
    scalar(value.has_value());
    if (value)
      scalars(value->revision, value->offset);
  }
  void query(const TaskObservationQuery &q) {
    cursor(q.cursor);
    id(q.selected);
    scalar(q.row_limit);
  }
  void query(const ParticipantObservationQuery &q) {
    scalar(q.incarnation);
    id(q.after);
    id(q.selected);
    scalars(q.row_limit, q.byte_limit, q.offset);
  }
  void query(const CommandObservationQuery &q) {
    id(q.after);
    id(q.selected);
    scalars(q.row_limit, q.byte_limit, q.chunk_limit, q.offset);
  }
  void selection(const Selection &s) {
    scalar(s.kind);
    if (s.kind == Kind::task)
      scalar(s.task.ordinal);
    else if (s.kind == Kind::participant)
      text(s.participant.view());
    else if (s.kind == Kind::command)
      append(std::as_bytes(std::span{s.command.bytes}));
  }
  void origin(const RawUiValueOrigin &o) {
    scalar(o.kind);
    scalar(o.source.has_value());
    if (o.source)
      reference(*o.source);
    scalar(o.fact.has_value());
    if (o.fact)
      append(o.fact->bytes());
    scalar(o.revision.has_value());
    if (o.revision)
      scalar(*o.revision);
  }
  void row(const TaskObservationRow &r) {
    scalar(r.id.ordinal);
    id(r.parent);
    scalars(r.declared_status, r.version, r.updated);
    text(r.title);
    text(r.owner);
    scalars(r.subtasks, r.subtasks_done);
  }
  void task(const TaskPublishedObservation &p) {
    scalar(p.publication);
    origin(p.origin);
    reference(p.ingress);
    const auto &o = p.value;
    scalars(o.status, o.revision, o.total);
    for (const auto count : o.counts)
      scalar(count);
    text(o.title);
    text(o.bead);
    scalar(o.row_count);
    if (o.row_count > o.rows.size())
      throw Error{ErrorCode::invalid_range};
    for (std::size_t n = 0; n < o.row_count; ++n)
      row(o.rows[n]);
    cursor(o.next);
    scalars(o.more_rows, o.selection);
    if (o.selection == TaskObservationSelection::resident) {
      row(o.detail.row);
      text(o.detail.note);
      text(o.detail.blocker);
    }
    scalars(o.examined_rows, o.copied_text_bytes);
  }
  void row(const ParticipantObservationRow &r) {
    text(r.id.view());
    text(r.task_id);
    text(r.provider);
    text(r.model);
    text(r.from);
    text(r.to);
    scalars(r.phase, r.queued, r.accepted, r.dropped, r.pending_captures,
            r.stop_requested, r.sealed, r.business_finished, r.worker_started,
            r.worker_ended, r.worker_joined, r.cleanup_closed, r.restored, r.reopened);
    reference(r.admission);
    reference(r.dispatch);
    reference(r.end);
  }
  void original(const std::optional<ProviderOriginalReference> &v) {
    scalar(v.has_value());
    if (v) {
      reference(v->event);
      scalars(v->partition, v->record_offset, v->bytes);
    }
  }
  void retained(const std::optional<SourceReference> &v) {
    scalar(v.has_value());
    if (v) {
      append(v->journal.bytes());
      scalar(v->sequence);
    }
  }
  void participant(const ParticipantObservation &o) {
    scalars(o.status, o.incarnation, o.row_count);
    if (o.row_count > o.rows.size())
      throw Error{ErrorCode::invalid_range};
    for (std::size_t n = 0; n < o.row_count; ++n)
      row(o.rows[n]);
    id(o.next);
    scalars(o.more_rows, o.selection);
    if (o.selection == ParticipantObservationSelection::resident) {
      row(o.detail);
      const auto &r = o.result;
      text(r.initial_request);
      text(r.current_request);
      text(r.last_answered_request);
      text(r.result_status);
      text(r.remote_disposition);
      original(r.initial_original);
      original(r.current_original);
      original(r.result_original);
      retained(r.initial_retained);
      retained(r.current_retained);
      retained(r.result_retained);
      scalars(r.result_available, r.projection_known, r.projection_truncated);
    }
    const auto &w = o.window;
    scalars(w.size, w.requested, w.begin, w.next, w.total, w.more);
    if (w.size > w.bytes.size())
      throw Error{ErrorCode::invalid_range};
    append(ByteView{w.bytes.data(), w.size});
    scalars(o.examined_rows, o.examined_fields, o.seek_steps, o.copied_bytes);
  }
  void row(const CommandObservationRow &r) {
    append(std::as_bytes(std::span{r.id.bytes}));
    scalars(r.phase, r.custody, r.admission_order, r.received, r.retained, r.pty,
            r.leader_exited, r.output_closed, r.worker_finished, r.terminal_retained,
            r.capture_complete, r.stop_requested, r.timed_out, r.has_exit, r.sealed,
            r.exit_code, r.signal, r.failure);
    reference(r.launch);
    reference(r.terminal);
  }
  void command(const CommandObservation &o) {
    scalars(o.status, o.row_count);
    if (o.row_count > o.rows.size())
      throw Error{ErrorCode::invalid_range};
    for (std::size_t n = 0; n < o.row_count; ++n)
      row(o.rows[n]);
    id(o.next);
    scalars(o.more_rows, o.selection);
    if (o.selection == CommandObservationSelection::resident)
      row(o.detail);
    const auto &w = o.window;
    scalars(w.size, w.source_count, w.requested, w.begin, w.next, w.cached_begin,
            w.retained_end, w.received_end, w.omitted_prefix, w.provenance_complete,
            w.end);
    if (w.source_count > w.sources.size() || w.size > w.bytes.size())
      throw Error{ErrorCode::invalid_range};
    for (std::size_t n = 0; n < w.source_count; ++n) {
      const auto &s = w.sources[n];
      reference(s.original);
      scalars(s.chunk_begin, s.offset, s.length);
    }
    append(ByteView{w.bytes.data(), w.size});
    scalars(o.examined_rows, o.examined_chunks, o.seek_steps);
  }
};
template <class Query, class Observation> struct Cached {
  Query query;
  Observation value;
};
struct Cuts {
  std::optional<Cached<TaskObservationQuery, TaskPublishedObservation>> task;
  std::optional<Cached<ParticipantObservationQuery, ParticipantObservation>>
      participant;
  std::optional<Cached<CommandObservationQuery, CommandObservation>> command;
};
struct Attempt {
  TaskObservationQuery task;
  ParticipantObservationQuery participant;
  CommandObservationQuery command;
  TaskPublicationStatus publication = TaskPublicationStatus::uninitialized;
  TaskObservationStatus task_status = TaskObservationStatus::unavailable;
  ParticipantObservationStatus participant_status =
      ParticipantObservationStatus::unavailable;
  CommandObservationStatus command_status = CommandObservationStatus::unavailable;
  bool participant_retired = false;
};
Wire snapshot_wire(const CommandCenterScope &scope, tui::ViewIdentity view,
                   const Selection &selection, const Attempt &a, const Cuts &cuts) {
  Wire wire;
  wire.scalar(std::uint8_t{1});
  wire.append(scope.environment.bytes());
  wire.append(scope.conversation.bytes());
  wire.scalars(view.owner, view.incarnation);
  wire.selection(selection);
  wire.query(a.task);
  wire.scalars(a.publication, a.task_status);
  wire.query(a.participant);
  wire.scalars(a.participant_status, a.participant_retired);
  wire.query(a.command);
  wire.scalar(a.command_status);
  wire.scalar(cuts.task.has_value());
  if (cuts.task) {
    wire.query(cuts.task->query);
    wire.task(cuts.task->value);
  }
  wire.scalar(cuts.participant.has_value());
  if (cuts.participant) {
    wire.query(cuts.participant->query);
    wire.participant(cuts.participant->value);
  }
  wire.scalar(cuts.command.has_value());
  if (cuts.command) {
    wire.query(cuts.command->query);
    wire.command(cuts.command->value);
  }
  return wire;
}
bool retired(ParticipantObservationStatus status) {
  return status == ParticipantObservationStatus::retired ||
         status == ParticipantObservationStatus::scope_mismatch;
}
std::string source_notice(const Attempt &a) {
  const auto task = a.publication == TaskPublicationStatus::ready &&
                            a.task_status == TaskObservationStatus::ready
                        ? "ready"
                    : a.publication == TaskPublicationStatus::busy          ? "busy"
                    : a.publication == TaskPublicationStatus::uninitialized ? "unknown"
                                                                            : "stale";
  const auto participant =
      a.participant_status == ParticipantObservationStatus::ready  ? "ready"
      : a.participant_status == ParticipantObservationStatus::busy ? "busy"
      : retired(a.participant_status)                              ? "retired"
                                                                   : "unknown";
  const auto command = a.command_status == CommandObservationStatus::ready  ? "ready"
                       : a.command_status == CommandObservationStatus::busy ? "busy"
                                                                            : "unknown";
  return std::string{"T "} + task + " P " + participant + " J " + command;
}
tui::TargetRef target(const CommandCenterScope &scope, std::uint64_t incarnation,
                      std::string_view kind, std::string identity) {
  return {hex_identity(scope.environment.bytes()) + "/" +
              hex_identity(scope.conversation.bytes()),
          std::string{kind}, std::move(identity), incarnation};
}
tui::SceneCandidate prepare(const CommandCenterScope &scope, const tui::Scene &previous,
                            const Selection &selected, const Attempt &attempt,
                            const Cuts &cuts) {
  auto scene = empty_scene(previous.view, previous.revision);
  auto &list = scene.collections.front();
  list.notice = {tui::NoticeKind::none, source_notice(attempt)};
  const auto task_coverage =
      cuts.task ? std::to_string(cuts.task->value.value.row_count) + "/" +
                      std::to_string(cuts.task->value.value.total)
                : "?";
  const auto participant_coverage =
      attempt.participant_retired ? "retired"
      : cuts.participant          ? std::to_string(cuts.participant->value.row_count) +
                                        (cuts.participant->value.more_rows ? "+" : "")
                                  : "?";
  const auto command_coverage = cuts.command
                                    ? std::to_string(cuts.command->value.row_count) +
                                          (cuts.command->value.more_rows ? "+" : "")
                                    : "?";
  list.title =
      "T" + task_coverage + " P" + participant_coverage + " J" + command_coverage;
  auto &detail = scene.inspectors.front();
  if (cuts.task)
    for (std::size_t n = 0; n < cuts.task->value.value.row_count; ++n) {
      const auto &r = cuts.task->value.value.rows[n];
      const auto id = task_id(r.id);
      list.items.push_back({"task:" + id,
                            "T " + id + " " +
                                std::string{task_status(r.declared_status)} + " " +
                                safe_label(r.title.view()),
                            target(scope, previous.view.incarnation, "task", id)});
    }
  if (cuts.participant && !attempt.participant_retired)
    for (std::size_t n = 0; n < cuts.participant->value.row_count; ++n) {
      const auto &r = cuts.participant->value.rows[n];
      const auto id = std::string{r.id.view()};
      list.items.push_back(
          {"participant:" + id,
           "P " + safe_label(id.substr(0, 12)) + " " +
               std::string{participant_phase(r.phase)},
           target(scope, cuts.participant->value.incarnation, "participant", id)});
    }
  if (cuts.command)
    for (std::size_t n = 0; n < cuts.command->value.row_count; ++n) {
      const auto &r = cuts.command->value.rows[n];
      const auto id = std::string{r.id.view()};
      list.items.push_back(
          {"command:" + id,
           "J " + id.substr(0, 8) + " " + std::string{command_phase(r.phase)},
           target(scope, previous.view.incarnation, "command", id)});
    }
  if (selected.kind == Kind::none) {
    detail.notice = {tui::NoticeKind::loading, "Choose work; sources are independent"};
    return scene;
  }
  const auto selected_key = key(selected);
  const bool loaded =
      std::any_of(list.items.begin(), list.items.end(),
                  [&](const auto &item) { return item.key == selected_key; });
  if (!loaded)
    detail.notice = {tui::NoticeKind::stale,
                     "Selected identity outside loaded catalog page"};
  if (selected.kind == Kind::task) {
    const auto id = task_id(selected.task);
    detail.title = "Task " + id;
    detail.target = target(scope, previous.view.incarnation, "task", id);
    if (!cuts.task || cuts.task->query.selected != std::optional{selected.task}) {
      detail.notice = {tui::NoticeKind::loading,
                       "Selected task observation unavailable"};
      return scene;
    }
    const auto &p = cuts.task->value;
    const auto &o = p.value;
    if (o.selection != TaskObservationSelection::resident) {
      detail.notice = {tui::NoticeKind::unavailable,
                       "Task not resident; retained history not queried"};
      return scene;
    }
    const auto &r = o.detail.row;
    lines(detail, "Declared " + std::string{task_status(r.declared_status)},
          Ink::heading);
    lines(detail, r.title.view());
    lines(detail, "Owner label: " + std::string{r.owner.view()}, Ink::muted);
    lines(detail,
          "Version " + std::to_string(r.version) + " | task revision " +
              std::to_string(o.revision),
          Ink::muted);
    if (r.parent)
      lines(detail, "Parent: " + task_id(*r.parent), Ink::muted);
    lines(detail,
          "Subtasks " + std::to_string(r.subtasks_done) + "/" +
              std::to_string(r.subtasks),
          Ink::muted);
    lines(detail, "Blocker: " + std::string{o.detail.blocker.view()}, Ink::failure);
    lines(detail, o.detail.note.view());
    if (attempt.publication != TaskPublicationStatus::ready ||
        attempt.task_status != TaskObservationStatus::ready)
      detail.notice = {tui::NoticeKind::stale,
                       "Task source stale; showing prior accepted observation"};
  } else if (selected.kind == Kind::participant) {
    const auto id = std::string{selected.participant.view()};
    detail.title = "Participant " + safe_label(id);
    if (!cuts.participant ||
        cuts.participant->query.selected != std::optional{selected.participant}) {
      detail.notice = {tui::NoticeKind::loading,
                       "Selected participant observation unavailable"};
      return scene;
    }
    const auto &o = cuts.participant->value;
    detail.target = target(scope, o.incarnation, "participant", id);
    if (o.selection != ParticipantObservationSelection::resident) {
      detail.notice = {tui::NoticeKind::unavailable,
                       "Participant not resident; retained history not queried"};
      return scene;
    }
    const auto &r = o.detail;
    lines(detail, participant_phase(r.phase), Ink::heading);
    lines(detail,
          r.task_id.present && !r.task_id.view().empty()
              ? "Task: " + observed(r.task_id)
              : "Task: unbound",
          Ink::muted);
    lines(detail,
          "Provider: " + observed(r.provider) + " | model: " + observed(r.model),
          Ink::muted);
    lines(detail,
          "Queued " + std::to_string(r.queued) + " | accepted " +
              std::to_string(r.accepted) + " | dropped " + std::to_string(r.dropped),
          Ink::muted);
    lines(detail,
          std::string{"Stop requested "} + (r.stop_requested ? "yes" : "no") +
              " | pending captures " + std::to_string(r.pending_captures),
          Ink::muted);
    lines(detail,
          std::string{"Result source: raw "} +
              (o.result.result_original ? "present" : "unavailable") + " | retained " +
              (o.result.result_retained ? "present" : "unavailable"),
          Ink::muted);
    lines(detail, "Current request: " + observed(o.result.current_request), Ink::muted);
    lines(detail, "Last answered: " + observed(o.result.last_answered_request),
          Ink::muted);
    lines(detail, "Result: " + observed(o.result.result_status), Ink::muted);
    lines(detail,
          "Bytes " + std::to_string(o.window.begin) + ".." +
              std::to_string(o.window.next) + "/" + std::to_string(o.window.total),
          Ink::muted);
    detail.content_offset = o.window.begin;
    lines(detail,
          {reinterpret_cast<const char *>(o.window.bytes.data()), o.window.size});
    if (o.result.projection_truncated)
      detail.notice = {tui::NoticeKind::unavailable,
                       "Answer projection truncated; original remains separate"};
    if (attempt.participant_retired)
      detail.notice = {
          tui::NoticeKind::stale,
          "Participant observation scope retired; prior owned observation"};
    else if (attempt.participant_status != ParticipantObservationStatus::ready)
      detail.notice = {tui::NoticeKind::stale,
                       "Participant source stale; showing prior accepted observation"};
  } else {
    const auto id = std::string{selected.command.view()};
    detail.title = "Command " + id;
    detail.target = target(scope, previous.view.incarnation, "command", id);
    if (!cuts.command ||
        cuts.command->query.selected != std::optional{selected.command}) {
      detail.notice = {tui::NoticeKind::loading,
                       "Selected command observation unavailable"};
      return scene;
    }
    const auto &o = cuts.command->value;
    if (o.selection != CommandObservationSelection::resident) {
      detail.notice = {tui::NoticeKind::unavailable,
                       "Command not resident; retained history not queried"};
      return scene;
    }
    const auto &r = o.detail;
    lines(detail, command_phase(r.phase), Ink::heading);
    if (r.has_exit)
      lines(detail,
            "Exit " + std::to_string(r.exit_code) + " | signal " +
                std::to_string(r.signal) + " | failure " + std::to_string(r.failure),
            r.exit_code || r.signal || r.failure ? Ink::failure : Ink::success);
    lines(detail,
          "Retained " + std::to_string(r.retained) + " | received " +
              std::to_string(r.received) + " | sealed " + (r.sealed ? "yes" : "no"),
          Ink::muted);
    lines(detail,
          std::string{"Source extents "} +
              (o.window.provenance_complete ? "complete" : "unavailable") +
              " | capture complete " + (r.capture_complete ? "yes" : "no"),
          Ink::muted);
    lines(detail, std::string{"Stop requested "} + (r.stop_requested ? "yes" : "no"),
          Ink::muted);
    lines(detail,
          "Bytes " + std::to_string(o.window.begin) + ".." +
              std::to_string(o.window.next),
          Ink::muted);
    detail.content_offset = o.window.begin;
    lines(detail,
          {reinterpret_cast<const char *>(o.window.bytes.data()), o.window.size});
    if (o.window.omitted_prefix)
      detail.notice = {tui::NoticeKind::unavailable,
                       "Earlier bytes outside resident cache"};
    if (attempt.command_status != CommandObservationStatus::ready)
      detail.notice = {tui::NoticeKind::stale,
                       "Command source stale; showing prior accepted observation"};
  }
  return scene;
}
} // namespace
struct CommandCenter::State {
  Cuts cuts;
  Attempt attempt;
  Selection selected;
  std::array<std::optional<TaskObservationCursor>, 257> task_pages{};
  std::array<std::optional<ParticipantObservationId>, 5> participant_pages{};
  std::array<std::optional<CommandObservationId>, 33> command_pages{};
  std::size_t task_page = 0, participant_page = 0, command_page = 0;
  std::size_t participant_offset = 0, command_offset = 0;
  std::uint64_t participant_incarnation = 0, generation = 0;
  Wire last_wire;
  bool last_capture = false, participant_retired = false;
  enum class Deferred { none, recording, projection } deferred = Deferred::none;
};
CommandCenter::CommandCenter(CommandCenterScope scope)
    : scope_(std::move(scope)), host_(initial_host()),
      controls_(
          unwrap(tui::CollectionInspector::create(host_, collection_key, detail_key))),
      state_(std::make_unique<State>()) {}
CommandCenter::~CommandCenter() = default;
void CommandCenter::toggle(RawEventRef origin) {
  open_ = !open_;
  focused_ = open_;
  if (open_) {
    origin_ = origin;
    refresh_ = true;
  }
}
void CommandCenter::first_page(RawEventRef origin) {
  state_->task_pages.fill({});
  state_->participant_pages.fill({});
  state_->command_pages.fill({});
  state_->task_page = state_->participant_page = state_->command_page = 0;
  state_->participant_offset = state_->command_offset = 0;
  open_ = focused_ = refresh_ = true;
  origin_ = origin;
}
std::string_view CommandCenter::notice() const noexcept {
  if (state_->deferred == State::Deferred::recording)
    return "stale: recording capacity";
  if (state_->deferred == State::Deferred::projection)
    return "stale: view preparation refused";
  const auto &a = state_->attempt;
  if (a.publication != TaskPublicationStatus::ready ||
      a.task_status != TaskObservationStatus::ready ||
      a.participant_status != ParticipantObservationStatus::ready ||
      a.command_status != CommandObservationStatus::ready)
    return "partial: independent source availability";
  return {};
}
bool CommandCenter::refresh(const CommandCenterSources &sources, bool capture) {
  refresh_ = false;
  if (!open_)
    return false;
  bool capture_failed = false;
  try {
    Attempt attempt;
    attempt.task.cursor = state_->task_pages[state_->task_page];
    attempt.participant.after = state_->participant_pages[state_->participant_page];
    attempt.participant.incarnation = state_->participant_incarnation;
    attempt.participant.offset = state_->participant_offset;
    attempt.command.after = state_->command_pages[state_->command_page];
    attempt.command.offset = state_->command_offset;
    const auto &selected = state_->selected;
    if (selected.kind == Kind::task)
      attempt.task.selected = selected.task;
    else if (selected.kind == Kind::participant)
      attempt.participant.selected = selected.participant;
    else if (selected.kind == Kind::command)
      attempt.command.selected = selected.command;
    Cuts cuts = state_->cuts;
    TaskPublishedObservation tasks;
    try {
      if (sources.tasks)
        tasks = sources.tasks(attempt.task);
    } catch (const Error &) {
      tasks.publication = TaskPublicationStatus::stale;
    } catch (const std::bad_alloc &) {
      tasks.publication = TaskPublicationStatus::stale;
    }
    attempt.publication = tasks.publication;
    attempt.task_status = tasks.value.status;
    if (tasks.publication == TaskPublicationStatus::ready &&
        tasks.value.status == TaskObservationStatus::ready)
      cuts.task = Cached{attempt.task, tasks};
    if (tasks.value.status == TaskObservationStatus::revision_conflict) {
      state_->task_pages.fill({});
      state_->task_page = 0;
      refresh_ = true;
    }
    auto participant = sources.participants.try_observe(attempt.participant);
    attempt.participant_status = participant.status;
    state_->participant_retired =
        state_->participant_retired || retired(participant.status);
    attempt.participant_retired = state_->participant_retired;
    if (participant.status == ParticipantObservationStatus::ready) {
      cuts.participant = Cached{attempt.participant, participant};
      state_->participant_incarnation = participant.incarnation;
    }
    const auto command = sources.commands
                             ? sources.commands->try_observe(attempt.command)
                             : CommandObservation{};
    attempt.command_status = command.status;
    if (command.status == CommandObservationStatus::ready)
      cuts.command = Cached{attempt.command, command};
    if (state_->selected.kind == Kind::none) {
      if (cuts.task && cuts.task->value.value.row_count) {
        state_->selected.kind = Kind::task;
        state_->selected.task = cuts.task->value.value.rows[0].id;
      } else if (cuts.participant && !attempt.participant_retired &&
                 cuts.participant->value.row_count) {
        state_->selected.kind = Kind::participant;
        state_->selected.participant = cuts.participant->value.rows[0].id;
      } else if (cuts.command && cuts.command->value.row_count) {
        state_->selected.kind = Kind::command;
        state_->selected.command = cuts.command->value.rows[0].id;
      }
      if (state_->selected.kind != Kind::none)
        refresh_ = true;
    }
    const auto wire =
        snapshot_wire(scope_, host_.snapshot()->view, state_->selected, attempt, cuts);
    const bool recording = capture && raw_enabled();
    if (state_->deferred == State::Deferred::none &&
        recording == state_->last_capture && wire.size == state_->last_wire.size &&
        std::equal(wire.bytes.begin(),
                   wire.bytes.begin() + static_cast<std::ptrdiff_t>(wire.size),
                   state_->last_wire.bytes.begin()))
      return false;
    std::optional<RawCapacityClaim> data;
    std::optional<RawControlToken> control;
    if (recording) {
      auto held_data = raw_try_claim_data(raw_envelope_size + wire.size);
      constexpr auto outcome_bytes = raw_envelope_size + 2 * provider_ref_bytes +
                                     3 * sizeof(std::uint64_t) + 2 * sizeof(bool) +
                                     sizeof(ErrorCode) + sizeof(std::int64_t);
      auto held_control = raw_try_claim_control(outcome_bytes);
      if (!held_data.has_value() || !held_control.has_value()) {
        const bool changed = state_->deferred != State::Deferred::recording;
        state_->deferred = State::Deferred::recording;
        return changed;
      }
      data.emplace(std::move(held_data).value());
      control.emplace(std::move(held_control).value());
    }
    auto scene = prepare(scope_, *host_.snapshot(), state_->selected, attempt, cuts);
    if (state_->generation == UINT64_MAX)
      throw Error{ErrorCode::overflow};
    const auto generation = ++state_->generation;
    RawEventRef snapshot{};
    if (data) {
      Error failure{ErrorCode::audit_unavailable};
      snapshot = raw_ui_emit(4088, std::array{ByteView{wire.bytes.data(), wire.size}},
                             RawPartition::data, &*data, &failure);
      if (!raw_ui_source_valid(snapshot)) {
        capture_failed = true;
        raw_capture_failed(failure);
        throw failure;
      }
    }
    const auto accepted = host_.publish(scene);
    RawEventRef adoption{};
    if (control) {
      const bool adopted = accepted.has_value(), has_error = !adopted;
      const auto error = adopted ? Error{} : accepted.error();
      const auto view = host_.snapshot()->view;
      Error failure{ErrorCode::audit_unavailable};
      adoption = raw_ui_emit(
          4089,
          std::array{
              ByteView{snapshot.lane}, detail::raw_native_field(snapshot.ordinal),
              ByteView{origin_.lane}, detail::raw_native_field(origin_.ordinal),
              detail::raw_native_field(view.owner),
              detail::raw_native_field(view.incarnation),
              detail::raw_native_field(generation), detail::raw_native_field(adopted),
              detail::raw_native_field(has_error), detail::raw_native_field(error.code),
              detail::raw_native_field(error.detail)},
          RawPartition::control, &*control, &failure);
      if (!raw_ui_source_valid(adoption)) {
        capture_failed = true;
        raw_capture_failed(failure);
        throw failure;
      }
    }
    if (!accepted.has_value()) {
      state_->deferred = State::Deferred::projection;
      return true;
    }
    state_->cuts = std::move(cuts);
    state_->attempt = attempt;
    state_->last_wire = wire;
    state_->last_capture = recording;
    state_->deferred = State::Deferred::none;
    applied_ = adoption;
    return true;
  } catch (const Error &) {
    if (capture_failed)
      throw;
    state_->deferred = State::Deferred::projection;
    return true;
  } catch (const std::bad_alloc &) {
    state_->deferred = State::Deferred::projection;
    return true;
  } catch (const std::length_error &) {
    state_->deferred = State::Deferred::projection;
    return true;
  }
}
Result<void> CommandCenter::act(tui::InspectorAction action, RawEventRef origin) {
  const auto &focus = host_.focused();
  const bool collection = focus && focus->key == collection_key;
  const auto moved = controls_.act(action);
  if (!moved.has_value())
    return moved;
  if (collection &&
      (action == tui::InspectorAction::previous ||
       action == tui::InspectorAction::next || action == tui::InspectorAction::first ||
       action == tui::InspectorAction::last ||
       action == tui::InspectorAction::page_previous ||
       action == tui::InspectorAction::page_next)) {
    const auto current = controls_.state();
    if (!current.has_value())
      return Result<void>::failure(current.error());
    if (current.value().selected) {
      const auto selected = parse_key(*current.value().selected);
      if (selected != state_->selected) {
        state_->selected = selected;
        state_->participant_offset = state_->command_offset = 0;
      }
    }
  }
  origin_ = origin;
  refresh_ = true;
  return Result<void>::success();
}
void CommandCenter::page(bool forward, RawEventRef origin) {
  bool changed = false;
  const auto &focus = host_.focused();
  if (focus && focus->key == collection_key) {
    const auto move = [&](auto &pages, std::size_t &page, const auto &next) {
      if (forward && next && page + 1 < pages.size()) {
        pages[++page] = next;
        return true;
      }
      if (!forward && page) {
        --page;
        return true;
      }
      return false;
    };
    if (state_->selected.kind == Kind::task && state_->cuts.task)
      changed = move(state_->task_pages, state_->task_page,
                     state_->cuts.task->value.value.next);
    else if (state_->selected.kind == Kind::participant && state_->cuts.participant)
      changed = move(state_->participant_pages, state_->participant_page,
                     state_->cuts.participant->value.next);
    else if (state_->selected.kind == Kind::command && state_->cuts.command)
      changed = move(state_->command_pages, state_->command_page,
                     state_->cuts.command->value.next);
  } else if (state_->selected.kind == Kind::participant && state_->cuts.participant) {
    const auto &w = state_->cuts.participant->value.window;
    if (forward && w.more)
      state_->participant_offset = w.next;
    else if (!forward && w.begin)
      state_->participant_offset = w.begin - std::min(w.begin, std::size_t{4096});
    else
      return;
    changed = true;
  } else if (state_->selected.kind == Kind::command && state_->cuts.command) {
    const auto &w = state_->cuts.command->value.window;
    if (forward && w.next < w.retained_end)
      state_->command_offset = w.next;
    else if (!forward && w.begin > w.cached_begin)
      state_->command_offset =
          std::max(w.cached_begin, w.begin - std::min(w.begin, std::size_t{4096}));
    else
      return;
    changed = true;
  }
  if (changed) {
    origin_ = origin;
    refresh_ = true;
  }
}
Result<tui::InspectorLayout> CommandCenter::render(ChatGrid &grid, tui::Rect bounds) {
  return controls_.render(grid, bounds);
}
} // namespace blackbird
