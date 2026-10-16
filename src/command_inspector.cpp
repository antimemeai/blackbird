#include "blackbird/command_inspector.hpp"
#include "blackbird/terminal.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

namespace blackbird {
namespace {
constexpr tui::ViewIdentity inspector_view{1, 1};
constexpr std::string_view collection_key = "commands", detail_key = "result";
std::string_view phase_name(CommandObservationPhase phase) {
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
std::string_view custody_name(CommandObservationCustody custody) {
  switch (custody) {
  case CommandObservationCustody::not_launched:
    return "not launched";
  case CommandObservationCustody::owned:
    return "owned";
  case CommandObservationCustody::reaped:
    return "reaped";
  case CommandObservationCustody::unavailable:
    return "unavailable";
  case CommandObservationCustody::pending:
    return "pending";
  }
  return "unavailable";
}
tui::TargetRef target(const CommandObservationId &id) {
  return {"current-session", "command", std::string{id.view()}, 1};
}
tui::SceneCandidate candidate(std::uint64_t revision) {
  tui::SceneCandidate result;
  result.view = inspector_view;
  result.base_revision = revision;
  result.nodes = {{std::string{collection_key}, {}, tui::NodeKind::collection, true},
                  {std::string{detail_key}, {}, tui::NodeKind::inspector, true}};
  result.collections = {{std::string{collection_key}, {}, "COMMANDS", {}}};
  result.inspectors = {{std::string{detail_key}, "RESULT", {}, {}, {}, 0}};
  return result;
}
tui::SceneHost initial_host() {
  auto host = unwrap(tui::SceneHost::create(inspector_view, {4, 16384, 65536}));
  unwrap(host.publish(candidate(0)));
  return host;
}
std::optional<CommandObservationId> selected(const tui::CollectionInspector &control) {
  const auto state = control.state();
  if (!state.has_value())
    throw state.error();
  const auto &key = state.value().selected;
  if (!key)
    return {};
  if (key->size() != 32)
    throw Error{ErrorCode::invalid_identity};
  CommandObservationId id;
  std::copy(key->begin(), key->end(), id.bytes.begin());
  return id;
}
struct Wire {
  std::array<std::byte, 8192> bytes{};
  std::size_t size = 0;
  void append(std::span<const std::byte> value) {
    if (value.size() > bytes.size() - size)
      throw Error{ErrorCode::overflow};
    std::copy(value.begin(), value.end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(size));
    size += value.size();
  }
  template <class T> void scalar(const T &value) {
    static_assert(std::is_trivially_copyable_v<T>);
    append(std::as_bytes(std::span{&value, std::size_t{1}}));
  }
  void reference(const RawEventRef &ref) {
    append(ref.lane);
    scalar(ref.ordinal);
  }
  void identity(const std::optional<CommandObservationId> &id) {
    scalar(id.has_value());
    if (id)
      append(std::as_bytes(std::span{id->bytes}));
  }
  void row(const CommandObservationRow &r) {
    append(std::as_bytes(std::span{r.id.bytes}));
    scalar(r.phase);
    scalar(r.custody);
    scalar(r.admission_order);
    scalar(r.received);
    scalar(r.retained);
    scalar(r.pty);
    scalar(r.leader_exited);
    scalar(r.output_closed);
    scalar(r.worker_finished);
    scalar(r.terminal_retained);
    scalar(r.capture_complete);
    scalar(r.stop_requested);
    scalar(r.timed_out);
    scalar(r.has_exit);
    scalar(r.sealed);
    scalar(r.exit_code);
    scalar(r.signal);
    scalar(r.failure);
    reference(r.launch);
    reference(r.terminal);
  }
};
Wire observation_wire(const CommandObservationQuery &q, const CommandObservation &o) {
  Wire wire;
  wire.identity(q.after);
  wire.identity(q.selected);
  wire.scalar(q.row_limit);
  wire.scalar(q.byte_limit);
  wire.scalar(q.chunk_limit);
  wire.scalar(q.offset);
  wire.scalar(o.status);
  wire.scalar(o.row_count);
  wire.identity(o.next);
  wire.scalar(o.more_rows);
  for (std::size_t i = 0; i < o.row_count; ++i)
    wire.row(o.rows[i]);
  wire.scalar(o.selection);
  if (o.selection == CommandObservationSelection::resident)
    wire.row(o.detail);
  const auto &w = o.window;
  wire.scalar(w.size);
  wire.scalar(w.source_count);
  wire.scalar(w.requested);
  wire.scalar(w.begin);
  wire.scalar(w.next);
  wire.scalar(w.cached_begin);
  wire.scalar(w.retained_end);
  wire.scalar(w.received_end);
  wire.scalar(w.omitted_prefix);
  wire.scalar(w.provenance_complete);
  wire.scalar(w.end);
  for (std::size_t i = 0; i < w.source_count; ++i) {
    const auto &source = w.sources[i];
    wire.reference(source.original);
    wire.scalar(source.chunk_begin);
    wire.scalar(source.offset);
    wire.scalar(source.length);
  }
  wire.scalar(o.examined_rows);
  wire.scalar(o.examined_chunks);
  wire.scalar(o.seek_steps);
  return wire;
}
tui::SceneCandidate prepare(const CommandObservation &o, std::uint64_t revision,
                            bool capture) {
  auto scene = candidate(revision);
  auto &list = scene.collections.front();
  if (!o.row_count)
    list.notice = {tui::NoticeKind::none, "No resident commands"};
  else if (o.more_rows)
    list.notice = {tui::NoticeKind::none, "More: Ctrl-Right"};
  for (std::size_t i = 0; i < o.row_count; ++i) {
    const auto &r = o.rows[i];
    list.items.push_back(
        {std::string{r.id.view()},
         std::string{r.id.view().substr(0, 8)} + " " + std::string{phase_name(r.phase)},
         target(r.id)});
  }
  auto &detail = scene.inspectors.front();
  if (o.selection == CommandObservationSelection::not_requested) {
    detail.notice = {tui::NoticeKind::loading, "Choose a command"};
    return scene;
  }
  if (o.selection == CommandObservationSelection::not_resident) {
    detail.notice = {tui::NoticeKind::unavailable,
                     "Not resident; retained history not queried"};
    return scene;
  }
  const auto &r = o.detail;
  const auto &w = o.window;
  detail.title = std::string{r.id.view()};
  detail.target = target(r.id);
  detail.content_offset = w.begin;
  detail.lines.push_back({{std::string{phase_name(r.phase)} + " | native " +
                               std::string{custody_name(r.custody)},
                           Ink::heading}});
  detail.lines.push_back({{"Retained " + std::to_string(r.retained) + " | received " +
                               std::to_string(r.received),
                           Ink::muted}});
  detail.lines.push_back(
      {{"Bytes " + std::to_string(w.begin) + ".." + std::to_string(w.next) +
            " | sealed " + (r.sealed ? "yes" : "no"),
        Ink::muted}});
  if (r.has_exit)
    detail.lines.push_back(
        {{"Exit " + std::to_string(r.exit_code) + " | signal " +
              std::to_string(r.signal) + " | failure " + std::to_string(r.failure),
          r.failure || r.signal || r.exit_code ? Ink::failure : Ink::success}});
  if (w.omitted_prefix)
    detail.notice = {tui::NoticeKind::unavailable,
                     "Earlier bytes outside resident cache"};
  else if (w.next < w.retained_end)
    detail.notice = {tui::NoticeKind::none, "More output: Ctrl-Right"};
  if (capture && !w.provenance_complete) {
    detail.notice = {tui::NoticeKind::unavailable,
                     "Output source reference unavailable"};
    return scene;
  }
  const std::string_view bytes{reinterpret_cast<const char *>(w.bytes.data()), w.size};
  for (auto &line : terminal_lines(bytes, std::numeric_limits<std::size_t>::max()))
    detail.lines.push_back({{std::move(line), Ink::normal}});
  return scene;
}
} // namespace
CommandInspector::CommandInspector()
    : host_(initial_host()),
      controls_(
          unwrap(tui::CollectionInspector::create(host_, collection_key, detail_key))) {
}
void CommandInspector::toggle(RawEventRef origin) {
  open_ = !open_;
  focused_ = open_;
  if (open_) {
    origin_ = origin;
    refresh_ = true;
  }
}
void CommandInspector::first_page(RawEventRef origin) {
  page_ = offset_ = 0;
  pages_.fill({});
  open_ = focused_ = refresh_ = true;
  origin_ = origin;
}
std::string_view CommandInspector::notice() const noexcept {
  switch (deferred_) {
  case Deferred::none:
    return {};
  case Deferred::owner_busy:
    return "stale: command owner busy";
  case Deferred::recording:
    return "stale: recording capacity";
  case Deferred::unavailable:
    return "stale: observation unavailable";
  case Deferred::projection:
    return "stale: view preparation refused";
  }
  return "stale";
}
bool CommandInspector::refresh(CommandJobs *commands, bool capture) {
  refresh_ = false;
  if (!open_)
    return false;
  const bool recording = capture && raw_enabled();
  auto defer = [&](Deferred state) {
    const bool changed = deferred_ != state;
    deferred_ = state;
    return changed;
  };
  CommandObservationQuery query;
  query.after = pages_[page_];
  std::optional<CommandObservationId> current;
  try {
    current = selected(controls_);
  } catch (const Error &) {
    return defer(Deferred::projection);
  }
  if (current != selected_) {
    selected_ = current;
    offset_ = 0;
  }
  query.selected = selected_;
  query.offset = offset_;
  const auto observed = commands ? commands->try_observe(query) : CommandObservation{};
  const auto wire = observation_wire(query, observed);
  if (observed.status == CommandObservationStatus::ready &&
      deferred_ == Deferred::none && recording == last_capture_ &&
      wire.size == last_wire_size_ &&
      std::equal(wire.bytes.begin(),
                 wire.bytes.begin() + static_cast<std::ptrdiff_t>(wire.size),
                 last_wire_.begin()))
    return false;
  std::optional<RawControlToken> credit;
  if (recording) {
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
      return defer(Deferred::unavailable);
    }
    const auto required = raw_envelope_size + 3 * sizeof(std::uint64_t) + 24 +
                          2 * sizeof(bool) + sizeof(ErrorCode) + sizeof(std::int64_t) +
                          wire.size;
    auto claimed = raw_try_claim_control(required);
    if (!claimed.has_value()) {
      return defer(Deferred::recording);
    }
    credit.emplace(std::move(claimed).value());
  }
  auto record = [&](bool adopted, std::optional<Error> error = {}) {
    if (!credit) {
      if (adopted)
        applied_ = {};
      return;
    }
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
      throw Error{ErrorCode::overflow};
    const auto generation = ++generation_;
    Error failure{ErrorCode::audit_unavailable};
    const bool has_error = error.has_value();
    const auto code = error ? error->code : ErrorCode{};
    const auto error_detail = error ? error->detail : std::int64_t{0};
    const auto event = raw_ui_emit(
        4087,
        std::array{detail::raw_native_field(inspector_view.owner),
                   detail::raw_native_field(inspector_view.incarnation),
                   ByteView{origin_.lane}, detail::raw_native_field(origin_.ordinal),
                   detail::raw_native_field(generation),
                   detail::raw_native_field(adopted),
                   detail::raw_native_field(has_error), detail::raw_native_field(code),
                   detail::raw_native_field(error_detail),
                   ByteView{wire.bytes.data(), wire.size}},
        RawPartition::control, &*credit, &failure);
    if (!raw_ui_source_valid(event)) {
      raw_capture_failed(failure);
      throw failure;
    }
    if (adopted)
      applied_ = event;
  };
  if (observed.status != CommandObservationStatus::ready) {
    const auto next = observed.status == CommandObservationStatus::busy
                          ? Deferred::owner_busy
                          : Deferred::unavailable;
    if (deferred_ != next)
      record(false);
    return defer(next);
  }
  try {
    auto scene = prepare(observed, host_.snapshot()->revision, recording);
    const auto accepted = host_.publish(scene);
    if (!accepted.has_value()) {
      record(false, accepted.error());
      return defer(Deferred::projection);
    }
    record(true);
    observation_ = observed;
    last_wire_ = wire.bytes;
    last_wire_size_ = wire.size;
    last_capture_ = recording;
    deferred_ = Deferred::none;
    return true;
  } catch (const std::bad_alloc &) {
    record(false, Error{ErrorCode::allocation});
    return defer(Deferred::projection);
  }
}
Result<void> CommandInspector::act(tui::InspectorAction action) {
  auto result = controls_.act(action);
  if (result.has_value())
    refresh_ = true;
  return result;
}
void CommandInspector::page(bool forward) {
  if (!observation_)
    return;
  const auto &focus = host_.focused();
  if (focus && focus->key == collection_key) {
    if (forward && observation_->more_rows && observation_->next &&
        page_ + 1 < pages_.size())
      pages_[++page_] = observation_->next;
    else if (!forward && page_)
      --page_;
    else
      return;
    offset_ = 0;
  } else {
    const auto &w = observation_->window;
    if (forward && w.next < w.retained_end)
      offset_ = w.next;
    else if (!forward && w.begin > w.cached_begin)
      offset_ =
          std::max(w.cached_begin, w.begin - std::min(w.begin, std::size_t{4096}));
    else
      return;
  }
  refresh_ = true;
}
Result<tui::InspectorLayout> CommandInspector::render(ChatGrid &grid,
                                                      tui::Rect bounds) {
  return controls_.render(grid, bounds);
}
} // namespace blackbird
