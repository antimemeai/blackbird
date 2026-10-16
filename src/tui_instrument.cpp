#include "blackbird/tui_instrument.hpp"
#include "blackbird/context.hpp"
#include "blackbird/display_text.hpp"
#include "blackbird/tui_action_bridge.hpp"
#include <algorithm>
#include <atomic>
#include <limits>

namespace blackbird {
namespace {
namespace lua = tui::lua;
constexpr std::string_view list_key = "results", detail_key = "comparison";
std::uint64_t advance(std::uint64_t &value) {
  if (value == UINT64_MAX)
    throw Error{ErrorCode::overflow};
  return ++value;
}
template <std::size_t N> void text(lua::Text<N> &out, std::string_view value) {
  if (value.size() > N)
    throw Error{ErrorCode::capacity};
  out = {};
  out.size = value.size();
  std::copy(value.begin(), value.end(), out.bytes.begin());
}
struct Wire {
  std::vector<std::byte> bytes;
  void append(ByteView value) {
    constexpr std::size_t limit = 2 * 1024 * 1024;
    if (value.size() > limit - bytes.size())
      throw Error{ErrorCode::capacity};
    bytes.insert(bytes.end(), value.begin(), value.end());
  }
  template <class T> void scalar(const T &value) {
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>);
    append(detail::raw_native_field(value));
  }
  void string(std::string_view value) {
    scalar(value.size());
    append(detail::raw_string_field(value));
  }
  void reference(RawEventRef value) {
    append(value.lane);
    scalar(value.ordinal);
  }
  void target(const lua::FixedTarget &value) {
    string(value.owner.view());
    string(value.kind.view());
    string(value.key.view());
    scalar(value.incarnation);
  }
  void stamp(const lua::Dependencies &value) {
    append(value.environment.bytes());
    append(value.conversation.bytes());
    scalar(value.view.owner);
    scalar(value.view.incarnation);
    scalar(value.source_generation);
    scalar(value.request_sequence);
    scalar(value.scene_revision);
    scalar(value.local_input_revision);
    scalar(value.query_revision);
  }
};
Result<RawEventRef> record(std::uint32_t site, const Wire &wire, bool capture,
                           RawCapacityClaim *preheld = nullptr) {
  if (!capture || !raw_enabled())
    return Result<RawEventRef>::success({});
  std::optional<RawCapacityClaim> owned;
  if (!preheld) {
    auto held = raw_try_claim_data(raw_envelope_size + wire.bytes.size());
    if (!held.has_value())
      return Result<RawEventRef>::failure(held.error());
    owned.emplace(std::move(held).value());
    preheld = &*owned;
  }
  Error error{ErrorCode::audit_unavailable};
  const auto ref = raw_ui_emit(site, std::array{ByteView{wire.bytes}},
                               RawPartition::data, preheld, &error);
  if (!raw_ui_source_valid(ref)) {
    raw_capture_failed(error);
    return Result<RawEventRef>::failure(error);
  }
  return Result<RawEventRef>::success(ref);
}
tui::SceneHost initial_host() {
  static std::atomic<std::uint64_t> next{1};
  auto value = next.load();
  for (;;) {
    if (!value || value == UINT64_MAX)
      throw Error{ErrorCode::overflow};
    if (next.compare_exchange_weak(value, value + 1))
      break;
  }
  const tui::ViewIdentity view{3, value};
  auto host = unwrap(tui::SceneHost::create(view, lua::scene_limits));
  tui::SceneCandidate seed;
  seed.view = view;
  seed.nodes = {{std::string{list_key}, {}, tui::NodeKind::collection, true},
                {std::string{detail_key}, {}, tui::NodeKind::inspector, true}};
  seed.collections = {{std::string{list_key}, {}, "RESULTS", {}}};
  seed.inspectors = {{std::string{detail_key}, "A / B comparison", {}, {}, {}, 0}};
  unwrap(host.publish(seed));
  return host;
}
std::optional<CommandObservationId> selected(const tui::SceneHost &host,
                                             const tui::CollectionInspector &controls,
                                             std::string_view owner,
                                             std::uint64_t incarnation) {
  const auto state = controls.state();
  if (!state.has_value() || !state.value().selected)
    return {};
  const auto scene = host.snapshot();
  const auto &key = *state.value().selected;
  const auto &items = scene->collections.front().items;
  const auto found = std::find_if(items.begin(), items.end(),
                                  [&](const auto &item) { return item.key == key; });
  if (found == items.end())
    return {};
  const auto &selected_target = found->target;
  if (!selected_target)
    return {};
  const auto &target = *selected_target;
  if (target.owner != owner || target.kind != "command" ||
      target.incarnation != incarnation || target.key.size() != 32 ||
      !std::all_of(target.key.begin(), target.key.end(), [](char byte) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
      }))
    return {};
  CommandObservationId id;
  std::copy(target.key.begin(), target.key.end(), id.bytes.begin());
  return id;
}
std::string_view phase(CommandObservationPhase value) {
  switch (value) {
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
  return "unknown";
}
bool same_content(const lua::PresentationInput &a, const lua::PresentationInput &b) {
  if (a.row_count != b.row_count || a.detail != b.detail ||
      a.detail_size != b.detail_size || a.detail_titles != b.detail_titles)
    return false;
  for (std::size_t n = 0; n < a.row_count; ++n)
    if (a.rows[n].key != b.rows[n].key || a.rows[n].label != b.rows[n].label ||
        a.rows[n].target != b.rows[n].target)
      return false;
  return true;
}
bool expected_shape(const tui::SceneCandidate &candidate) {
  return !candidate.draft && candidate.nodes.size() == 2 &&
         candidate.collections.size() == 1 && candidate.inspectors.size() == 1 &&
         candidate.collections.front().node == list_key &&
         candidate.inspectors.front().node == detail_key &&
         std::all_of(candidate.nodes.begin(), candidate.nodes.end(), [](const auto &n) {
           return !n.parent && n.focusable &&
                  ((n.key == list_key && n.kind == tui::NodeKind::collection) ||
                   (n.key == detail_key && n.kind == tui::NodeKind::inspector));
         });
}
void scene_wire(Wire &wire, const tui::SceneCandidate &scene) {
  wire.scalar(scene.view.owner);
  wire.scalar(scene.view.incarnation);
  wire.scalar(scene.base_revision);
  wire.scalar(scene.nodes.size());
  for (const auto &node : scene.nodes) {
    wire.string(node.key);
    wire.scalar(node.parent.has_value());
    if (node.parent)
      wire.string(*node.parent);
    wire.scalar(node.kind);
    wire.scalar(node.focusable);
  }
  wire.scalar(scene.collections.size());
  for (const auto &list : scene.collections) {
    wire.string(list.node);
    wire.string(list.title);
    wire.scalar(list.notice.kind);
    wire.string(list.notice.text);
    wire.scalar(list.items.size());
    for (const auto &item : list.items) {
      wire.string(item.key);
      wire.string(item.label);
      wire.scalar(item.target.has_value());
      if (item.target) {
        wire.string(item.target->owner);
        wire.string(item.target->kind);
        wire.string(item.target->key);
        wire.scalar(item.target->incarnation);
      }
    }
  }
  wire.scalar(scene.inspectors.size());
  for (const auto &detail : scene.inspectors) {
    wire.string(detail.node);
    wire.string(detail.title);
    wire.scalar(detail.target.has_value());
    if (detail.target) {
      wire.string(detail.target->owner);
      wire.string(detail.target->kind);
      wire.string(detail.target->key);
      wire.scalar(detail.target->incarnation);
    }
    wire.scalar(detail.notice.kind);
    wire.string(detail.notice.text);
    wire.scalar(detail.content_offset);
    wire.scalar(detail.lines.size());
    for (const auto &line : detail.lines) {
      wire.scalar(line.size());
      for (const auto &span : line) {
        wire.string(span.text);
        wire.scalar(span.ink);
      }
    }
  }
  wire.scalar(scene.draft.has_value());
  if (scene.draft) {
    wire.scalar(scene.draft->base_revision);
    wire.string(scene.draft->text);
    wire.scalar(scene.draft->cursor);
  }
}
} // namespace
struct TuiInstrument::State {
  struct Pin {
    CommandObservationId job;
    CommandObservationWindow window;
    RawEventRef original{}, origin{};
  };
  struct Invocation {
    lua::Dependencies stamp;
    InstrumentSource source;
    RawEventRef input, source_ref;
    bool compose = false;
  };
  State(CommandCenterScope selected_scope, std::shared_ptr<CommandJobs> selected_jobs)
      : scope(selected_scope), jobs(std::move(selected_jobs)), host(initial_host()),
        controls(unwrap(tui::CollectionInspector::create(
            host, list_key, detail_key, tui::InspectorBinding::independent))),
        input{{scope.environment, scope.conversation, host.snapshot()->view}},
        shutdown_owner(raw_enabled()) {}
  CommandCenterScope scope;
  std::shared_ptr<CommandJobs> jobs;
  tui::SceneHost host;
  tui::CollectionInspector controls;
  std::shared_ptr<lua::ActionInbox> inbox;
  std::unique_ptr<lua::NativeActionBridge> bridge;
  std::unique_ptr<lua::PresentationOwner> worker;
  InstrumentSource effective;
  std::optional<InstrumentSource> proposal;
  std::uint64_t desired_generation = 0, effective_generation = 0, sequence = 0;
  std::uint64_t local = 0, query = 0, incarnation = 0;
  lua::PresentationInput input;
  std::array<std::optional<Invocation>, 4> invocations{};
  std::optional<lua::Outcome> held;
  std::array<lua::Text<64>, 8> handlers{};
  std::size_t handler_count = 0;
  std::array<std::optional<Pin>, 2> pins{};
  CommandObservation observed;
  CommandObservationQuery observation_query;
  std::string owner, receipt = "No action receipt", selected_status = "No selected job";
  std::string_view notice = "Source pending";
  RawEventRef proposal_ref{}, effective_ref{}, origin{};
  RawUiOwner shutdown_owner;
  std::optional<RawCapacityClaim> shutdown_credit;
  Wire shutdown_wire;
  bool open = false, focused = false, dirty = true, input_ready = false,
       source_recorded = false;
  bool compose_active() const {
    return std::any_of(invocations.begin(), invocations.end(),
                       [](const auto &i) { return i && i->compose; });
  }
  ~State() {
    worker.reset();
    bridge.reset();
    if (!shutdown_credit || !inbox)
      return;
    shutdown_owner.require_cleanup_thread();
    RawUiOwnerBinding binding{shutdown_owner};
    try {
      const auto pending = inbox->try_read();
      if (!pending.has_value())
        throw pending.error();
      if (!pending.value().size)
        return;
      auto &wire = shutdown_wire;
      wire.scalar(
          std::uint8_t{1}); // Unknown on whole-runtime exit, not retry authority.
      wire.scalar(pending.value().size);
      for (std::size_t n = 0; n < pending.value().size; ++n) {
        const auto &entry = pending.value().entries[n];
        if (!entry)
          throw Error{ErrorCode::corrupt};
        wire.append(entry->key.inbox);
        wire.scalar(entry->key.owner);
        wire.scalar(entry->key.trigger);
        wire.scalar(entry->key.ordinal);
        wire.stamp(entry->stamp);
        wire.scalar(entry->acceptance_order);
        wire.reference(entry->input_origin);
        wire.target(entry->target);
        wire.string(entry->handler.view());
        wire.string(entry->verb.view());
      }
      Error failure{ErrorCode::audit_unavailable};
      const auto emitted = raw_ui_emit(4098, std::array{ByteView{wire.bytes}},
                                       RawPartition::data, &*shutdown_credit, &failure);
      if (!raw_ui_source_valid(emitted)) {
        raw_capture_failed(failure);
        raw_fence(failure);
      }
    } catch (const Error &error) {
      raw_capture_failed(error);
      raw_fence(error);
    } catch (...) {
      raw_capture_failed({ErrorCode::allocation});
      raw_fence({ErrorCode::allocation});
    }
  }
  void changed(RawEventRef input_origin) {
    advance(local);
    origin = input_origin;
    dirty = true;
  }
  lua::Dependencies dependencies(std::uint64_t request) const {
    return {scope.environment,
            scope.conversation,
            host.snapshot()->view,
            proposal ? desired_generation : effective_generation,
            request,
            host.snapshot()->revision,
            local,
            query};
  }
  Result<void> adopt(lua::Outcome &outcome, const Invocation &invocation,
                     bool capture) {
    InstrumentSource prepared_source = invocation.source;
    Wire result;
    result.reference(invocation.input);
    result.stamp(outcome.stamp);
    result.scalar(outcome.status);
    result.scalar(outcome.native_error.code);
    result.scalar(outcome.native_error.detail);
    result.string(outcome.diagnostic.view());
    result.scalar(outcome.accepted_requests);
    result.scalar(outcome.handler_count);
    for (std::size_t n = 0; n < outcome.handler_count; ++n)
      result.string(outcome.handlers[n].view());
    result.scalar(outcome.candidate.has_value());
    if (outcome.candidate)
      scene_wire(result, *outcome.candidate);
    const auto now = dependencies(outcome.stamp.request_sequence);
    Wire applied;
    applied.reference({});
    applied.reference(invocation.input);
    applied.stamp(now);
    constexpr auto branch_bytes =
        sizeof(bool) + sizeof(ErrorCode) + sizeof(std::int64_t);
    applied.bytes.reserve(applied.bytes.size() + branch_bytes);
    std::optional<RawControlToken> credit;
    std::optional<RawCapacityClaim> data;
    if (capture && raw_enabled()) {
      auto held_data = raw_try_claim_data(raw_envelope_size + result.bytes.size());
      if (!held_data.has_value())
        return Result<void>::failure(held_data.error());
      data.emplace(std::move(held_data).value());
      auto held_credit = raw_try_claim_control(raw_envelope_size +
                                               applied.bytes.size() + branch_bytes);
      if (!held_credit.has_value())
        return Result<void>::failure(held_credit.error());
      credit.emplace(std::move(held_credit).value());
    }
    const auto recorded = record(4093, result, capture, data ? &*data : nullptr);
    if (!recorded.has_value())
      return Result<void>::failure(recorded.error());
    applied.bytes.clear();
    applied.reference(recorded.value());
    applied.reference(invocation.input);
    applied.stamp(now);
    std::string_view next_notice;
    Result<void> accepted = Result<void>::success();
    if (!invocation.compose) {
      next_notice = outcome.accepted_requests ? "Intent accepted; delivery pending"
                                              : "Handler returned without an intent";
      if (outcome.status != lua::Completion::handler_finished)
        next_notice = outcome.accepted_requests
                          ? "Intent accepted; Lua handler failed; delivery pending"
                          : "Lua handler failed; no intent";
    } else if (outcome.stamp != now) {
      accepted = Result<void>::failure({ErrorCode::stale_handle});
      dirty = true;
    } else if (outcome.status != lua::Completion::candidate || !outcome.candidate) {
      accepted =
          Result<void>::failure({outcome.status == lua::Completion::instruction_budget
                                     ? ErrorCode::capacity
                                     : ErrorCode::invalid_range});
      next_notice = outcome.status == lua::Completion::instruction_budget
                        ? "Lua instruction budget; last good retained"
                        : "Lua source failed; last good retained";
      if (proposal && invocation.source.script == proposal->script)
        proposal.reset();
    } else if (!expected_shape(*outcome.candidate)) {
      accepted = Result<void>::failure({ErrorCode::invalid_range});
      next_notice = "Source requires results / comparison; last good retained";
      if (proposal && invocation.source.script == proposal->script)
        proposal.reset();
    } else if (invocation.source.preview) {
      auto preview_host = tui::SceneHost::create(outcome.stamp.view, lua::scene_limits);
      if (!preview_host.has_value())
        accepted = Result<void>::failure(preview_host.error());
      else {
        auto candidate = *outcome.candidate;
        candidate.base_revision = 0;
        accepted = preview_host.value().publish(candidate);
      }
      next_notice = accepted.has_value()
                        ? "Preview ready; effective source unchanged"
                        : "Preview invalid; effective source unchanged";
      proposal.reset();
    } else {
      next_notice = "Source active";
      accepted = lua::publish(outcome, now, host);
      if (accepted.has_value()) {
        effective = std::move(prepared_source);
        effective_generation = outcome.stamp.source_generation;
        handlers = outcome.handlers;
        handler_count = outcome.handler_count;
        proposal.reset();
        effective_ref = invocation.source_ref;
      } else
        next_notice = "Scene refused; last good retained";
    }
    if (!next_notice.empty())
      notice = next_notice;
    applied.scalar(accepted.has_value());
    const auto error = accepted.has_value() ? Error{} : accepted.error();
    applied.scalar(error.code);
    applied.scalar(error.detail);
    if (credit) {
      Error failure{ErrorCode::audit_unavailable};
      const auto emitted = raw_ui_emit(4094, std::array{ByteView{applied.bytes}},
                                       RawPartition::control, &*credit, &failure);
      if (!raw_ui_source_valid(emitted)) {
        raw_capture_failed(failure);
        return Result<void>::failure(failure);
      }
    }
    return Result<void>::success();
  }
};
TuiInstrument::TuiInstrument(std::unique_ptr<State> state) : state_(std::move(state)) {}
TuiInstrument::~TuiInstrument() = default;
Result<std::unique_ptr<TuiInstrument>>
TuiInstrument::create(CommandCenterScope scope, std::shared_ptr<CommandJobs> commands,
                      InstrumentSource initial) {
  try {
    if (!commands)
      return Result<std::unique_ptr<TuiInstrument>>::failure(
          {ErrorCode::invalid_identity});
    auto state = std::make_unique<State>(scope, std::move(commands));
    if (raw_enabled()) {
      state->shutdown_credit = raw_ui_data(true, raw_envelope_size + 8192);
      state->shutdown_wire.bytes.reserve(8192);
    }
    state->owner = hex_identity(scope.environment.bytes()) + '/' +
                   hex_identity(scope.conversation.bytes());
    state->inbox = unwrap(lua::ActionInbox::create(scope.conversation.bytes()));
    state->bridge = unwrap(lua::NativeActionBridge::create(
        {scope.environment, scope.conversation}, state->jobs, state->inbox));
    state->worker = unwrap(lua::PresentationOwner::create(state->inbox));
    state->incarnation = state->bridge->incarnation();
    if (initial.script) {
      state->proposal = std::move(initial);
      state->desired_generation = 1;
    } else
      state->notice = "Source unavailable; /instrument reload PATH";
    state->input_ready = true;
    for (auto &title : state->input.detail_titles)
      text(title, "Unpinned");
    return Result<std::unique_ptr<TuiInstrument>>::success(
        std::unique_ptr<TuiInstrument>{new TuiInstrument{std::move(state)}});
  } catch (const Error &error) {
    return Result<std::unique_ptr<TuiInstrument>>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<std::unique_ptr<TuiInstrument>>::failure({ErrorCode::allocation});
  }
}
void TuiInstrument::toggle(RawEventRef origin) {
  state_->open = !state_->open;
  state_->focused = state_->open;
  state_->changed(origin);
}
void TuiInstrument::first_page(RawEventRef origin) {
  state_->observation_query.offset = 0;
  state_->open = state_->focused = true;
  state_->changed(origin);
}
bool TuiInstrument::open() const noexcept { return state_->open; }
bool TuiInstrument::focused() const noexcept { return state_->focused; }
void TuiInstrument::focus_editor() noexcept { state_->focused = false; }
Result<void> TuiInstrument::source(InstrumentSource proposal, bool capture) {
  try {
    if (!proposal.script || proposal.path.size() > 4096)
      return Result<void>::failure({ErrorCode::invalid_range});
    if (state_->desired_generation == UINT64_MAX)
      return Result<void>::failure({ErrorCode::overflow});
    Wire wire;
    wire.reference(proposal.original);
    wire.reference(proposal.origin);
    wire.string(proposal.path);
    wire.scalar(proposal.preview);
    wire.scalar(state_->desired_generation + 1);
    const auto emitted = record(4090, wire, capture);
    if (!emitted.has_value())
      return Result<void>::failure(emitted.error());
    advance(state_->desired_generation);
    state_->proposal = std::move(proposal);
    state_->proposal_ref = emitted.value();
    state_->source_recorded = true;
    state_->notice = state_->proposal->preview ? "Preview pending" : "Source pending";
    state_->dirty = true;
    return Result<void>::success();
  } catch (const Error &error) {
    return Result<void>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
bool TuiInstrument::refresh(bool capture) {
  auto &s = *state_;
  bool changed = false;
  try {
    if (s.proposal && !s.source_recorded) {
      Wire wire;
      wire.reference(s.proposal->original);
      wire.reference(s.proposal->origin);
      wire.string(s.proposal->path);
      wire.scalar(s.proposal->preview);
      wire.scalar(s.desired_generation);
      const auto emitted = record(4090, wire, capture);
      if (!emitted.has_value()) {
        s.notice = "Recording capacity; last good retained";
        return true;
      }
      s.proposal_ref = emitted.value();
      s.source_recorded = true;
    }
    const auto receipts = s.bridge->try_snapshot();
    if (receipts.status == lua::BridgeSnapshotStatus::ready && receipts.size) {
      const auto &latest = receipts.entries[receipts.size - 1];
      if (latest) {
        auto receipt =
            std::string{"Receipt "} + std::string{latest->request_id.view()} + "\n";
        receipt += latest->disposition == lua::BridgeDisposition::admitted
                       ? "Mailbox admitted"
                   : latest->disposition == lua::BridgeDisposition::refused
                       ? "Mailbox refused"
                       : "Forward outcome unknown";
        if (latest->delivery_known) {
          const auto &d = latest->delivery;
          switch (d.stage) {
          case CommandDeliveryStage::job_not_resident:
            receipt += " · job not resident; unknown";
            break;
          case CommandDeliveryStage::key_not_resident:
            receipt += " · key not resident; unknown";
            break;
          case CommandDeliveryStage::reserved:
            receipt += " · reserved";
            break;
          case CommandDeliveryStage::selected:
            receipt += " · selected";
            break;
          case CommandDeliveryStage::queued:
            receipt += " · queued";
            break;
          case CommandDeliveryStage::consumed:
            receipt += d.consumption == CommandDeliveryConsumption::worker
                           ? " · worker consumed"
                       : d.consumption == CommandDeliveryConsumption::abandoned
                           ? " · abandoned"
                           : " · consumed";
            break;
          case CommandDeliveryStage::failed:
            receipt += " · delivery failed/unknown";
            break;
          }
          receipt += " · native returns " + std::to_string(d.call_count);
          for (std::size_t n = 0; n < d.call_count; ++n)
            receipt += " [" + std::to_string(d.calls[n].result) + ", errno " +
                       std::to_string(d.calls[n].result < 0 ? d.calls[n].error : 0) +
                       "]";
        }
        if (latest->delivery_status != CommandDeliveryStatus::ready)
          receipt += " · query unavailable";
        if (s.receipt != receipt) {
          s.receipt = std::move(receipt);
          changed = true;
        }
      }
    }
    if (!s.held) {
      lua::Outcome next{s.dependencies(1)};
      if (s.worker->try_take(next) == lua::PollStatus::ready)
        s.held.emplace(std::move(next));
    }
    if (s.open) {
      auto query = s.observation_query;
      query.selected = selected(s.host, s.controls, s.owner, s.incarnation);
      const auto observed = s.jobs->try_observe(query);
      if (observed.status == CommandObservationStatus::ready) {
        s.observed = observed;
        s.observation_query = query;
        auto status = observed.selection == CommandObservationSelection::resident
                          ? "Selected " + std::string{observed.detail.id.view()} + " " +
                                std::string{phase(observed.detail.phase)}
                          : std::string{"No selected job"};
        if (observed.selection != CommandObservationSelection::resident ||
            observed.detail.leader_exited || observed.detail.worker_finished)
          status += " · interrupt unavailable";
        if (observed.more_rows)
          status += " · more jobs outside this cut";
        if (s.selected_status != status) {
          s.selected_status = std::move(status);
          changed = true;
        }
        lua::PresentationInput input{s.dependencies(1)};
        input.row_count = observed.row_count;
        for (std::size_t n = 0; n < observed.row_count; ++n) {
          const auto &row = observed.rows[n];
          auto &out = input.rows[n];
          text(out.key, row.id.view());
          text(out.label,
               std::string{row.id.view()} + " " + std::string{phase(row.phase)} +
                   (row.has_exit ? " exit " + std::to_string(row.exit_code) : ""));
          text(out.target.owner, s.owner);
          text(out.target.kind, "command");
          text(out.target.key, row.id.view());
          out.target.incarnation = s.incarnation;
        }
        for (std::size_t n = 0; n < s.pins.size(); ++n) {
          const auto &pinned = s.pins[n];
          if (pinned) {
            const auto &pin = *pinned;
            input.detail[n] = pin.window.bytes;
            input.detail_size[n] = pin.window.size;
            text(
                input.detail_titles[n],
                "Job " + std::string{pin.job.view()} + " bytes " +
                    std::to_string(pin.window.begin) + ".." +
                    std::to_string(pin.window.next) + " of observed " +
                    std::to_string(pin.window.received_end) +
                    (pin.window.omitted_prefix ? " · prefix omitted" : "") +
                    (pin.window.provenance_complete ? "" : " · provenance incomplete"));
          } else
            text(input.detail_titles[n], "Unpinned");
        }
        if (!s.input_ready || !same_content(input, s.input)) {
          advance(s.query);
          s.input = std::move(input);
          s.input_ready = true;
          s.dirty = true;
        }
      }
    }
    if (s.held) {
      auto found =
          std::find_if(s.invocations.begin(), s.invocations.end(), [&](const auto &i) {
            return i && i->stamp.request_sequence == s.held->stamp.request_sequence;
          });
      if (found == s.invocations.end())
        throw Error{ErrorCode::corrupt};
      const auto &invocation = *found;
      if (!invocation)
        throw Error{ErrorCode::corrupt};
      const auto result = s.adopt(*s.held, *invocation, capture);
      if (!result.has_value()) {
        s.notice = "Recording capacity; last good retained";
        return true;
      }
      found->reset();
      s.held.reset();
      s.bridge->notify();
      changed = true;
    }
    if (s.dirty && s.input_ready && !s.compose_active()) {
      const auto source = s.proposal ? *s.proposal : s.effective;
      if (!source.script)
        return changed;
      auto slot = std::find_if(s.invocations.begin(), s.invocations.end(),
                               [](const auto &i) { return !i; });
      if (slot == s.invocations.end())
        return changed;
      auto input = s.input;
      input.stamp = s.dependencies(advance(s.sequence));
      Wire wire;
      wire.stamp(input.stamp);
      const auto source_ref = s.proposal ? s.proposal_ref : s.effective_ref;
      State::Invocation invocation{input.stamp, source, {}, source_ref, true};
      static_assert(std::is_nothrow_move_constructible_v<State::Invocation>);
      wire.reference(source_ref);
      wire.reference(source.original);
      wire.reference(s.origin);
      wire.scalar(input.row_count);
      for (std::size_t n = 0; n < input.row_count; ++n) {
        wire.string(input.rows[n].key.view());
        wire.string(input.rows[n].label.view());
        wire.target(input.rows[n].target);
      }
      for (std::size_t n = 0; n < s.pins.size(); ++n) {
        const auto &pinned = s.pins[n];
        wire.reference(pinned ? pinned->original : RawEventRef{});
        wire.string(input.detail_titles[n].view());
      }
      const auto emitted = record(4092, wire, capture);
      if (!emitted.has_value()) {
        s.notice = "Recording capacity; last good retained";
        return true;
      }
      if (s.worker->try_compose({source.script, input, source.preview}) ==
          lua::SubmitStatus::accepted) {
        invocation.input = emitted.value();
        slot->emplace(std::move(invocation));
        s.dirty = false;
      }
    }
  } catch (const Error &error) {
    s.notice = error_name(error.code);
    changed = true;
  } catch (const std::bad_alloc &) {
    s.notice = "Instrument allocation refused; last good retained";
    changed = true;
  }
  if (s.bridge->quiescent()) {
    s.notice = "Action bridge stopped; pending intents retained";
    changed = true;
  }
  return changed;
}
Result<void> TuiInstrument::act(tui::InspectorAction action, RawEventRef origin) {
  const auto before =
      selected(state_->host, state_->controls, state_->owner, state_->incarnation);
  const auto moved = state_->controls.act(action);
  if (moved.has_value()) {
    if (before !=
        selected(state_->host, state_->controls, state_->owner, state_->incarnation))
      state_->observation_query.offset = 0;
    state_->focused = true;
    state_->changed(origin);
  }
  return moved;
}
void TuiInstrument::page(bool forward, RawEventRef origin) {
  auto &s = *state_;
  if (forward && s.observed.window.next > s.observation_query.offset)
    s.observation_query.offset = s.observed.window.next;
  else if (!forward)
    s.observation_query.offset -=
        std::min(s.observation_query.offset, std::size_t{4096});
  s.changed(origin);
}
Result<void> TuiInstrument::pin(std::size_t slot, RawEventRef origin, bool capture) {
  auto &s = *state_;
  s.notice = "Pin unavailable; refresh selection";
  try {
    const auto id = selected(s.host, s.controls, s.owner, s.incarnation);
    if (slot >= s.pins.size() || !id || s.observation_query.selected != id ||
        s.observed.selection != CommandObservationSelection::resident ||
        s.observed.detail.id != *id ||
        s.observed.window.requested != s.observation_query.offset)
      return Result<void>::failure({ErrorCode::stale_handle});
    Wire wire;
    wire.append(s.scope.environment.bytes());
    wire.append(s.scope.conversation.bytes());
    wire.reference(origin);
    wire.scalar(slot);
    wire.string(id->view());
    const auto &w = s.observed.window;
    wire.scalar(w.requested);
    wire.scalar(w.begin);
    wire.scalar(w.next);
    wire.scalar(w.size);
    wire.scalar(w.cached_begin);
    wire.scalar(w.retained_end);
    wire.scalar(w.received_end);
    wire.scalar(w.omitted_prefix);
    wire.scalar(w.provenance_complete);
    wire.scalar(w.end);
    wire.append({w.bytes.data(), w.size});
    wire.scalar(w.source_count);
    for (std::size_t n = 0; n < w.source_count; ++n) {
      wire.reference(w.sources[n].original);
      wire.scalar(w.sources[n].chunk_begin);
      wire.scalar(w.sources[n].offset);
      wire.scalar(w.sources[n].length);
    }
    const auto emitted = record(4091, wire, capture);
    if (!emitted.has_value())
      return Result<void>::failure(emitted.error());
    s.pins[slot] = State::Pin{*id, w, emitted.value(), origin};
    s.changed(origin);
    s.notice = slot ? "Pinned B" : "Pinned A";
    return Result<void>::success();
  } catch (const Error &error) {
    return Result<void>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
Result<void> TuiInstrument::request_interrupt(RawEventRef origin, bool capture) {
  auto &s = *state_;
  s.notice = "Interrupt unavailable";
  try {
    const auto id = selected(s.host, s.controls, s.owner, s.incarnation);
    if (!s.open || !s.focused || !id || !s.effective.script || s.bridge->quiescent() ||
        s.observation_query.selected != id ||
        s.observed.selection != CommandObservationSelection::resident ||
        s.observed.detail.id != *id || s.observed.detail.leader_exited ||
        s.observed.detail.worker_finished ||
        (s.observed.detail.phase != CommandObservationPhase::running &&
         s.observed.detail.phase != CommandObservationPhase::stopped))
      return Result<void>::failure({ErrorCode::stale_handle});
    if (std::none_of(s.handlers.begin(),
                     s.handlers.begin() + static_cast<std::ptrdiff_t>(s.handler_count),
                     [](const auto &name) { return name.view() == "interrupt"; }))
      return Result<void>::failure({ErrorCode::unsupported});
    auto slot = std::find_if(s.invocations.begin(), s.invocations.end(),
                             [](const auto &i) { return !i; });
    if (slot == s.invocations.end())
      return Result<void>::failure({ErrorCode::capacity});
    auto trigger = s.inbox->new_trigger();
    if (!trigger.has_value())
      return Result<void>::failure(trigger.error());
    auto stamp = s.dependencies(advance(s.sequence));
    stamp.source_generation = s.effective_generation;
    lua::FixedTarget target;
    text(target.owner, s.owner);
    text(target.kind, "command");
    text(target.key, id->view());
    target.incarnation = s.incarnation;
    lua::Text<64> handler;
    text(handler, "interrupt");
    State::Invocation invocation{stamp, s.effective, {}, s.effective_ref, false};
    constexpr std::string_view pending_notice = "Handler pending";
    const auto key = trigger.value().key();
    Wire wire;
    wire.stamp(stamp);
    wire.reference(origin);
    wire.reference(s.effective_ref);
    wire.reference(s.effective.original);
    wire.target(target);
    wire.string(handler.view());
    wire.append(key.inbox);
    wire.scalar(key.owner);
    wire.scalar(key.trigger);
    wire.scalar(key.ordinal);
    const auto emitted = record(4097, wire, capture);
    if (!emitted.has_value())
      return Result<void>::failure(emitted.error());
    const auto submitted =
        s.worker->try_handle({s.effective.script, stamp, handler, target, origin,
                              std::move(trigger).value()});
    if (submitted != lua::SubmitStatus::accepted)
      return Result<void>::failure({submitted == lua::SubmitStatus::busy
                                        ? ErrorCode::busy
                                        : ErrorCode::capacity});
    invocation.input = emitted.value();
    slot->emplace(std::move(invocation));
    s.notice = pending_notice;
    return Result<void>::success();
  } catch (const Error &error) {
    return Result<void>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<void>::failure({ErrorCode::allocation});
  }
}
Result<tui::InspectorLayout> TuiInstrument::render(ChatGrid &grid, tui::Rect bounds) {
  auto &s = *state_;
  const auto foot = terminal_lines(s.selected_status + "\n" + s.receipt, bounds.width);
  const auto reserve =
      bounds.height >= 10 ? std::min(foot.size(), bounds.height / 2) : std::size_t{0};
  auto body = bounds;
  body.height -= reserve;
  auto rendered = s.controls.render(grid, body);
  for (std::size_t row = 0; row < reserve; ++row)
    grid.line(bounds.y + body.height + row, {{foot[row], Ink::muted}}, bounds.x);
  return rendered;
}
std::string_view TuiInstrument::notice() const noexcept { return state_->notice; }
const tui::SceneHost &TuiInstrument::host() const noexcept { return state_->host; }
std::uint64_t TuiInstrument::effective_generation() const noexcept {
  return state_->effective_generation;
}
} // namespace blackbird
