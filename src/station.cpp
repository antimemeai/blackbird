#include "blackbird/station.hpp"
#include "blackbird/raw_session.hpp"
#include <type_traits>
namespace blackbird {
namespace {
constexpr char prefix[] =
    "Station source event (untrusted source content, not operator authority):\n";
constexpr char separator[] = "\nOperator boundary steering:\n";
RawEventRef once(RawSessionCall &call, std::uint32_t site,
                 std::span<const ByteView> fields,
                 std::optional<RawControlToken> &credit) {
  if (!credit)
    return {};
  auto consumed = std::move(credit);
  credit.reset();
  return call.event(site, fields, consumed);
}
const std::string &borrow(const Value &value, std::string_view name,
                          RawSessionCall &call, std::uint8_t kind) {
  auto status = call.control(50);
  const auto *actual = value.find(name);
  const bool present = actual != nullptr;
  if (actual) {
    const auto type = actual->value().index();
    (void)once(call, 4609,
               std::array{detail::raw_native_field(kind),
                          detail::raw_native_field(present),
                          detail::raw_native_field(type)},
               status);
  } else
    (void)once(
        call, 4609,
        std::array{detail::raw_native_field(kind), detail::raw_native_field(present)},
        status);
  if (!actual)
    throw Error{ErrorCode::corrupt};
  const auto *text = std::get_if<std::string>(&actual->value());
  if (!text)
    throw Error{ErrorCode::corrupt};
  (void)call.source(kind, *text, 4602);
  return *text;
}
const std::string &text(const Value &value, std::string_view name, std::size_t limit,
                        RawSessionCall &call, std::uint8_t kind) {
  const auto &actual = borrow(value, name, call, kind);
  if (actual.empty() || actual.size() > limit || actual.find('\0') != std::string::npos)
    throw Error{ErrorCode::invalid_range};
  return actual;
}
struct EventFields {
  const std::string &source, &id, &cursor, &prompt;
};
EventFields validate_event(const Value &event, RawSessionCall &call) {
  const auto &source = text(event, "source", 256, call, 4);
  const auto &id = text(event, "id", 256, call, 5);
  const auto &cursor = text(event, "cursor", 1024, call, 6);
  const auto &prompt = text(event, "prompt", 32768, call, 7);
  return {source, id, cursor, prompt};
}
auto key(const EventFields &event) { return std::make_pair(event.source, event.id); }
void decision(RawSessionCall &call, std::uint8_t kind, bool actual, std::size_t size,
              std::optional<RawControlToken> &credit) {
  if (!credit)
    return;
  auto piece = credit->split(50);
  if (!piece.has_value()) {
    raw_capture_failed(piece.error());
    throw piece.error();
  }
  std::optional<RawControlToken> owned{std::move(piece).value()};
  (void)once(call, 4603,
             std::array{detail::raw_native_field(kind),
                        detail::raw_native_field(actual),
                        detail::raw_native_field(size)},
             owned);
}
template <class Prepare, class F, class State>
decltype(auto) with_state(RawSessionCall &call, Prepare &&prepare, F &&body,
                          State &&state) {
  return call.run([&]() -> decltype(auto) {
    prepare();
    try {
      if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
        body();
        state();
      } else {
        auto result = body();
        state();
        return result;
      }
    } catch (...) {
      state();
      throw;
    }
  });
}
void record(RawSessionCall &call, AuditLog &log, const Value &packet,
            ApplicationChannel channel = ApplicationChannel::program) {
  auto credit = call.bytes_credit(32);
  const auto actual = log.record(channel, packet);
  (void)call.bytes(4604, actual, credit);
}
} // namespace
StationStore::StationStore(AuditLog &log, const RawAdapterContext *capture)
    : log_(log) {
  const std::uint8_t method = 1;
  RawSessionCall call{4600, 4601, capture,
                      std::array{detail::raw_native_field(method),
                                 detail::raw_native_field(paused_),
                                 detail::raw_native_field(stopped_)}};
  std::optional<RawControlToken> final;
  auto state = [&] {
    const std::uint8_t kind = 9;
    (void)once(call, 4605,
               std::array{detail::raw_native_field(kind),
                          detail::raw_native_field(paused_),
                          detail::raw_native_field(stopped_),
                          detail::raw_native_field(controls_.size()),
                          detail::raw_native_field(events_.size())},
               final);
  };
  const auto owner = call.context();
  with_state(
      call, [&] { final = call.control(59); },
      [&] {
        for (const auto &fact : log.root().tail_facts()) {
          const auto *r = std::get_if<ApplicationRecordEvent>(&fact.event.body);
          if (!r || r->channel != ApplicationChannel::program)
            continue;
          call.stage(1);
          RawSessionCall replay{4611, 4612, &owner,
                                std::array{ByteView{r->identity.bytes()}}};
          replay.run([&] {
            replay.stage(1);
            if (r->payload.size() > ValueLimits{}.bytes)
              throw Error{ErrorCode::capacity};
            auto credit = replay.enabled()
                              ? std::optional<RawCapacityClaim>{unwrap(raw_claim_data(
                                    raw_session_bound(88, r->payload.size())))}
                              : std::nullopt;
            const auto bytes = unwrap(r->payload.read());
            const auto size = bytes.size();
            (void)raw_session_record(
                4610, replay.reference(),
                std::array{ByteView{r->identity.bytes()},
                           ByteView{fact.record.journal.bytes()},
                           detail::raw_native_field(fact.record.sequence),
                           detail::raw_native_field(size),
                           std::span<const std::byte>{bytes}},
                RawPartition::data, credit ? &*credit : nullptr);
            credit.reset();
            replay.stage(2);
            call.stage(2);
            auto p = unwrap(decode_packet(bytes));
            replay.stage(3);
            call.stage(3);
            const auto *l = p.find("label");
            if (!l || !std::holds_alternative<std::string>(l->value())) {
              auto status = replay.control(50);
              const bool present = l != nullptr;
              const std::uint8_t field_kind = 10;
              if (l)
                (void)once(replay, 4609,
                           std::array{detail::raw_native_field(field_kind),
                                      detail::raw_native_field(present),
                                      detail::raw_native_field(l->value().index())},
                           status);
              else
                (void)once(replay, 4609,
                           std::array{detail::raw_native_field(field_kind),
                                      detail::raw_native_field(present)},
                           status);
              const std::uint8_t tag = 0;
              replay.finish(RawOperationBranch::returned, nullptr,
                            std::array{detail::raw_native_field(tag)});
              return;
            }
            (void)replay.source(10, l->string(), 4602);
            std::uint8_t tag = 0, mutation = 0;
            auto bank = replay.control(59);
            auto snapshot = [&] {
              if (mutation)
                (void)once(replay, 4605,
                           std::array{detail::raw_native_field(mutation),
                                      detail::raw_native_field(paused_),
                                      detail::raw_native_field(stopped_),
                                      detail::raw_native_field(controls_.size()),
                                      detail::raw_native_field(events_.size())},
                           bank);
            };
            try {
              if (l->string() == "station.control") {
                tag = 1;
                mutation = 5;
                replay.stage(4);
                call.stage(4);
                const auto &c = field(p, "command");
                const auto &control_id = borrow(c, "id", replay, 1);
                const auto &action = borrow(c, "action", replay, 2);
                if (action == "steer")
                  (void)borrow(c, "text", replay, 3);
                replay.stage(5);
                call.stage(5);
                controls_.insert(control_id);
                apply_control(c);
              } else if (l->string() == "station.pause") {
                tag = 2;
                mutation = 6;
                replay.stage(5);
                call.stage(5);
                paused_ = true;
              } else if (l->string() == "station.admission") {
                tag = 3;
                mutation = 7;
                replay.stage(4);
                call.stage(4);
                const auto &e = field(p, "event");
                const auto fields = validate_event(e, replay);
                replay.stage(5);
                call.stage(5);
                events_[key(fields)] = "unknown";
              } else if (l->string() == "station.observation") {
                tag = 4;
                mutation = 8;
                replay.stage(4);
                call.stage(4);
                const auto &e = field(p, "event");
                const auto &source = borrow(e, "source", replay, 4),
                           &event_id = borrow(e, "id", replay, 5);
                if (!events_.contains(std::make_pair(source, event_id)))
                  throw Error{ErrorCode::corrupt};
                const auto &outcome = borrow(p, "outcome", replay, 9);
                replay.stage(5);
                call.stage(5);
                events_[std::make_pair(source, event_id)] = outcome;
              }
              snapshot();
            } catch (...) {
              snapshot();
              throw;
            }
            replay.finish(RawOperationBranch::returned, nullptr,
                          std::array{detail::raw_native_field(tag)});
          });
        }
        call.stage(6);
        for (const auto &[k, outcome] : events_) {
          auto compare = call.control(89);
          const auto source = call.source(4, k.first, 4602),
                     event_id = call.source(5, k.second, 4602);
          const bool unknown = outcome == "unknown";
          (void)once(call, 4615,
                     std::array{ByteView{source.lane},
                                detail::raw_native_field(source.ordinal),
                                ByteView{event_id.lane},
                                detail::raw_native_field(event_id.ordinal),
                                detail::raw_native_field(unknown)},
                     compare);
          if (unknown)
            paused_ = true;
        }
      },
      state);
}
void StationStore::apply_control(const Value &c) {
  const auto &a = string_field(c, "action");
  if (a == "pause")
    paused_ = true;
  else if (a == "resume") {
    paused_ = false;
    stopped_ = false;
  } else if (a == "stop") {
    stopped_ = true;
    paused_ = true;
  } else if (a == "steer")
    steer_ = string_field(c, "text");
}
bool StationStore::control(const Value &c, const RawAdapterContext *capture,
                           RawEventRef *actual_owner) {
  if (actual_owner)
    *actual_owner = {};
  const std::uint8_t method = 2;
  RawSessionCall call{4600, 4601, capture,
                      std::array{detail::raw_native_field(method),
                                 detail::raw_native_field(paused_),
                                 detail::raw_native_field(stopped_)}};
  std::optional<RawControlToken> tests, final, result_bank;
  auto state = [&] {
    const std::uint8_t mutation = 1;
    (void)once(call, 4605,
               std::array{detail::raw_native_field(mutation),
                          detail::raw_native_field(paused_),
                          detail::raw_native_field(stopped_),
                          detail::raw_native_field(controls_.size()),
                          detail::raw_native_field(events_.size())},
               final);
  };
  const bool returned = with_state(
      call,
      [&] {
        tests = call.control(100);
        final = call.control(59);
        result_bank = call.control(41);
      },
      [&] {
        call.stage(1);
        const auto &control_id = text(c, "id", 256, call, 1);
        const auto &a = text(c, "action", 32, call, 2);
        call.stage(2);
        if (a != "pause" && a != "resume" && a != "stop" && a != "steer")
          throw Error{ErrorCode::invalid_range};
        if (a == "steer")
          (void)text(c, "text", 32768, call, 3);
        call.stage(3);
        const bool duplicate = controls_.contains(control_id);
        decision(call, 1, duplicate, controls_.size(), tests);
        bool accepted = false;
        if (!duplicate) {
          call.stage(4);
          const bool full = controls_.size() >= 4096;
          decision(call, 2, full, controls_.size(), tests);
          if (full)
            throw Error{ErrorCode::capacity};
          call.stage(5);
          record(call, log_,
                 Value::object({{"label", Value{"station.control"}}, {"command", c}}));
          call.stage(6);
          controls_.insert(control_id);
          call.stage(7);
          apply_control(c);
          accepted = true;
        }
        (void)once(call, 4606, std::array{detail::raw_native_field(accepted)},
                   result_bank);
        return accepted;
      },
      state);
  if (actual_owner)
    *actual_owner = call.reference();
  return returned;
}
void StationStore::pause(std::string_view reason, const RawAdapterContext *capture) {
  const std::uint8_t method = 5;
  RawSessionCall call{4600, 4601, capture,
                      std::array{detail::raw_native_field(method),
                                 detail::raw_native_field(paused_),
                                 detail::raw_native_field(stopped_)}};
  std::optional<RawControlToken> final;
  auto state = [&] {
    const std::uint8_t kind = 2;
    (void)once(call, 4605,
               std::array{detail::raw_native_field(kind),
                          detail::raw_native_field(paused_),
                          detail::raw_native_field(stopped_),
                          detail::raw_native_field(controls_.size()),
                          detail::raw_native_field(events_.size())},
               final);
  };
  with_state(
      call, [&] { final = call.control(59); },
      [&] {
        call.stage(1);
        (void)call.source(8, reason, 4602);
        call.stage(2);
        if (reason.empty() || reason.size() > 1024)
          throw Error{ErrorCode::invalid_range};
        call.stage(3);
        record(call, log_,
               Value::object({{"label", Value{"station.pause"}},
                              {"reason", Value{std::string{reason}}}}));
        call.stage(4);
        paused_ = true;
      },
      state);
}
void StationStore::supervisor_stop(std::string_view reason,
                                   const RawAdapterContext *capture) {
  const std::uint8_t method = 8;
  RawSessionCall call{4600, 4601, capture,
                      std::array{detail::raw_native_field(method),
                                 detail::raw_native_field(paused_),
                                 detail::raw_native_field(stopped_)}};
  call.run([&] {
    call.stage(1);
    (void)call.source(8, reason, 4602);
    call.stage(2);
    if (reason.empty() || reason.size() > 1024)
      throw Error{ErrorCode::invalid_range};
    call.stage(3);
    // On the log channel: restore replays program records only, so the station
    // keeps its paused and stopped state across this stop.
    record(call, log_,
           Value::object({{"label", Value{"station.supervisor-stop"}},
                          {"reason", Value{std::string{reason}}}}),
           ApplicationChannel::log);
  });
}
bool StationStore::admit(const Value &e, const RawAdapterContext *capture,
                         RawEventRef *actual_owner) {
  if (actual_owner)
    *actual_owner = {};
  const std::uint8_t method = 3;
  RawSessionCall call{4600, 4601, capture,
                      std::array{detail::raw_native_field(method),
                                 detail::raw_native_field(paused_),
                                 detail::raw_native_field(stopped_)}};
  std::optional<RawControlToken> tests, final, result_bank;
  auto state = [&] {
    (void)once(call, 4605,
               std::array{detail::raw_native_field(method),
                          detail::raw_native_field(paused_),
                          detail::raw_native_field(stopped_),
                          detail::raw_native_field(controls_.size()),
                          detail::raw_native_field(events_.size())},
               final);
  };
  const bool returned = with_state(
      call,
      [&] {
        tests = call.control(150);
        final = call.control(59);
        result_bank = call.control(41);
      },
      [&] {
        call.stage(1);
        const auto fields = validate_event(e, call);
        call.stage(2);
        const bool withheld = paused_ || stopped_;
        decision(call, 3, withheld, events_.size(), tests);
        bool accepted = false;
        if (!withheld) {
          call.stage(3);
          const bool duplicate = events_.contains(key(fields));
          decision(call, 1, duplicate, events_.size(), tests);
          if (!duplicate) {
            call.stage(4);
            const bool full = events_.size() >= 4096;
            decision(call, 2, full, events_.size(), tests);
            if (full)
              throw Error{ErrorCode::capacity};
            (void)call.source(3, steer_, 4602);
            call.stage(5);
            record(call, log_,
                   Value::object({{"label", Value{"station.admission"}},
                                  {"event", e},
                                  {"steer", Value{steer_}}}));
            call.stage(6);
            events_.emplace(key(fields), "unknown");
            accepted = true;
          }
        }
        (void)once(call, 4606, std::array{detail::raw_native_field(accepted)},
                   result_bank);
        return accepted;
      },
      state);
  if (actual_owner)
    *actual_owner = call.reference();
  return returned;
}
void StationStore::finish(const Value &e, bool returned,
                          const RawAdapterContext *capture) {
  const std::uint8_t method = 4;
  RawSessionCall call{4600, 4601, capture,
                      std::array{detail::raw_native_field(method),
                                 detail::raw_native_field(paused_),
                                 detail::raw_native_field(stopped_),
                                 detail::raw_native_field(returned)}};
  std::optional<RawControlToken> tests, final;
  auto state = [&] {
    (void)once(call, 4605,
               std::array{detail::raw_native_field(method),
                          detail::raw_native_field(paused_),
                          detail::raw_native_field(stopped_),
                          detail::raw_native_field(controls_.size()),
                          detail::raw_native_field(events_.size())},
               final);
  };
  with_state(
      call,
      [&] {
        tests = call.control(50);
        final = call.control(59);
      },
      [&] {
        call.stage(1);
        const auto &source = borrow(e, "source", call, 4),
                   &event_id = borrow(e, "id", call, 5);
        call.stage(2);
        auto i = events_.find(std::make_pair(source, event_id));
        const bool conflict = i == events_.end() || i->second != "unknown";
        decision(call, 4, conflict, events_.size(), tests);
        if (conflict)
          throw Error{ErrorCode::conflict};
        auto credit = call.source_credit(sizeof("workflow_returned") - 1);
        call.stage(3);
        const std::string outcome = returned ? "workflow_returned" : "unknown";
        (void)call.source(9, outcome, credit, 4602);
        call.stage(4);
        record(call, log_,
               Value::object({{"label", Value{"station.observation"}},
                              {"event", e},
                              {"outcome", Value{outcome}}}));
        call.stage(5);
        i->second = outcome;
        if (!returned) {
          call.stage(6);
          paused_ = true;
        }
      },
      state);
}
std::string StationStore::prompt(const Value &e, const RawAdapterContext *capture,
                                 RawEventRef *actual_source) const {
  if (actual_source)
    *actual_source = {};
  const std::uint8_t method = 6;
  RawSessionCall call{4600, 4601, capture,
                      std::array{detail::raw_native_field(method),
                                 detail::raw_native_field(paused_),
                                 detail::raw_native_field(stopped_)}};
  RawEventRef source{};
  auto actual = call.run([&] {
    call.stage(1);
    (void)validate_event(e, call);
    (void)call.source(3, steer_, 4602);
    auto format_credit = call.bytes_credit(ValueLimits{}.bytes);
    const auto bound = raw_session_bound(
        raw_session_bound(raw_session_bound(ValueLimits{}.bytes, sizeof(prefix) - 1),
                          sizeof(separator) - 1),
        steer_.size());
    auto prompt_credit = call.bytes_credit(bound);
    call.stage(2);
    const auto formatted = unwrap(format_value(e));
    (void)call.bytes(4614, formatted, format_credit);
    call.stage(3);
    std::string result = std::string{prefix} + formatted + separator + steer_;
    call.stage(4);
    source = call.bytes(4607, result, prompt_credit);
    return result;
  });
  if (actual_source)
    *actual_source = source;
  return actual;
}
Value StationStore::view(const RawAdapterContext *capture,
                         RawEventRef *actual_owner) const {
  if (actual_owner)
    *actual_owner = {};
  const std::uint8_t method = 7;
  RawSessionCall call{4600, 4601, capture,
                      std::array{detail::raw_native_field(method),
                                 detail::raw_native_field(paused_),
                                 detail::raw_native_field(stopped_)}};
  std::optional<RawControlToken> final;
  Value::Array events;
  auto rows = [&] {
    (void)once(call, 4613, std::array{detail::raw_native_field(events.size())}, final);
  };
  auto actual = call.run([&] {
    final = call.control(48);
    try {
      call.stage(1);
      (void)call.source(3, steer_, 4602);
      for (const auto &[k, outcome] : events_) {
        call.stage(2);
        (void)call.source(4, k.first, 4602);
        (void)call.source(5, k.second, 4602);
        (void)call.source(9, outcome, 4602);
        events.push_back(Value::object({{"source", Value{k.first}},
                                        {"id", Value{k.second}},
                                        {"outcome", Value{outcome}}}));
      }
      call.stage(3);
      auto result =
          Value::object({{"paused", Value{paused_}},
                         {"stopped", Value{stopped_}},
                         {"steer", Value{steer_}},
                         {"events", Value{std::move(events)}},
                         {"semantics", Value{"at-most-once local dispatch; workflow "
                                             "return is not effect success"}}});
      const auto count = field(result, "events").array().size();
      (void)once(call, 4613, std::array{detail::raw_native_field(count)}, final);
      return result;
    } catch (...) {
      rows();
      throw;
    }
  });
  if (actual_owner)
    *actual_owner = call.reference();
  return actual;
}
} // namespace blackbird
