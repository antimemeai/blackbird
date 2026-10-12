#include "blackbird/context.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_query.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
namespace blackbird {
namespace {
Value number(std::size_t n) { return Value{Number{n}}; }
std::size_t index(const Value &q, std::string_view key, std::size_t fallback,
                  RawQueryTrace &trace, RawQueryField field) {
  const auto *v = q.find(key);
  if (!v) {
    trace.field(field, fallback);
    return fallback;
  }
  const auto *n = std::get_if<Number>(&v->value());
  if (!n || n->text().empty())
    throw Error{ErrorCode::invalid_range};
  std::size_t out = 0;
  const auto &s = n->text();
  const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
  if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
    throw Error{ErrorCode::invalid_range};
  trace.field(field, out);
  return out;
}
Result<std::vector<std::byte>> query_bytes(RawEventRef parent, RecordReference record,
                                           const ImmutableBytes &bytes,
                                           RawEventRef *read = nullptr) {
  const std::array fields{ByteView{record.journal.bytes()},
                          detail::raw_native_field(record.sequence)};
  RawQueryLookup lookup{parent, RawQueryLookupKind::payload, fields, 115};
  return lookup.run([&] {
    auto result = bytes.read();
    if (result.has_value())
      lookup.bytes(result.value().size());
    else
      lookup.failure(1, result.error());
    if (read)
      *read = lookup.reference();
    return result;
  });
}
Result<Value> query_packet(RawQueryTrace &trace, RecordReference record,
                           const ImmutableBytes &bytes, ValueLimits limits = {},
                           RawEventRef *read = nullptr) {
  RawQueryPacket packet{1, trace.reference(), record, bytes.size(), limits};
  return packet.run([&] {
    if (bytes.size() > limits.bytes) {
      const Error error{ErrorCode::capacity};
      packet.finish(1, error);
      return Result<Value>::failure(error);
    }
    packet.stage(1);
    auto data = query_bytes(packet.reference(), record, bytes, read);
    if (!data.has_value()) {
      packet.finish(1, data.error());
      return Result<Value>::failure(data.error());
    }
    packet.stage(2);
    auto result = decode_packet(data.value(), limits);
    if (result.has_value())
      packet.finish(0);
    else
      packet.finish(1, result.error());
    return result;
  });
}
Value query_decode(RawQueryTrace &trace, RecordReference record, ByteView bytes,
                   ValueLimits limits, RawEventRef read) {
  const auto prior = read.ordinal ? std::optional<RawEventRef>{read} : std::nullopt;
  RawQueryPacket packet{2, trace.reference(), record, bytes.size(), limits, prior};
  return packet.run([&] {
    packet.stage(2);
    auto result = decode_packet(bytes, limits);
    if (result.has_value())
      packet.finish(0);
    else
      packet.finish(1, result.error());
    return unwrap(std::move(result));
  });
}
RetainedFact query_fact(RetainedState &root, std::size_t index, RawQueryLookup &lookup,
                        RetainedKind &kind) {
  return lookup.run([&] {
    auto result = root.fact(index);
    if (!result.has_value()) {
      lookup.failure(1, result.error());
      throw result.error();
    }
    kind = retained_kind(result.value().event.body);
    lookup.fact(result.value(), kind);
    return std::move(result).value();
  });
}
Value summary(const RetainedFact &fact, std::size_t i, RetainedKind kind,
              RawQueryTrace &trace) {
  Value row =
      Value::object({{"record", number(i)},
                     {"journal", Value{hex_identity(fact.record.journal.bytes())}},
                     {"sequence", number(fact.record.sequence)},
                     {"kind", number(static_cast<std::size_t>(kind))}});
  auto add = [&](std::string key, Value v) {
    row.object().emplace_back(std::move(key), std::move(v));
  };
  auto identity = [&](std::string key, const auto &id) {
    add(std::move(key), Value{hex_identity(id.bytes())});
  };
  Value::Array sources;
  for (const auto &s : std::span{fact.event.dependencies}.first(
           std::min<std::size_t>(64, fact.event.dependencies.size())))
    sources.push_back(
        Value::object({{"journal", Value{hex_identity(s.journal.bytes())}},
                       {"sequence", number(s.sequence)}}));
  add("sources", Value{std::move(sources)});
  add("source_count", number(fact.event.dependencies.size()));
  std::visit(
      [&](const auto &e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, ApplicationRecordEvent>) {
          identity("id", e.identity);
          add("channel", number(static_cast<std::size_t>(e.channel)));
          add("payload_bytes", number(e.payload.size()));
          // Context packets can be enormous; their full JSON is recovered only on
          // demand.
          if (e.payload.size() <= 65536 && (e.channel == ApplicationChannel::log ||
                                            e.channel == ApplicationChannel::program)) {
            const auto p = unwrap(query_packet(trace, fact.record, e.payload));
            if (const auto *t = p.find("time"))
              add("time", *t);
            if (const auto *variable = p.find("variable");
                variable && std::holds_alternative<std::string>(variable->value()) &&
                p.find("label") &&
                std::holds_alternative<std::string>(p.find("label")->value()) &&
                p.find("label")->string() == "variable.sample") {
              add("variable", *variable);
              for (const auto key :
                   {"sample_id", "value", "source", "observation_status"})
                if (const auto *v = p.find(key))
                  add(key, *v);
            }
            for (const auto key : {"label", "generation", "activation"})
              if (const auto *v = p.find(key);
                  v && std::holds_alternative<std::string>(v->value()) &&
                  v->string().size() <= 256)
                add(key, *v);
            if (const auto *m = p.find("metadata")) {
              Value::Object kept;
              for (const auto key :
                   {"attempt", "revision", "generation", "operation", "complaint"})
                if (const auto *v = m->find(key);
                    v && std::holds_alternative<std::string>(v->value()) &&
                    v->string().size() <= 256)
                  kept.emplace_back(key, *v);
              if (const auto *t = m->find("time"))
                add("time", *t);
              if (const auto *duration = m->find("duration_ns"))
                add("duration_ns", *duration);
              if (const auto *operation = m->find("operation"))
                add("operation", *operation);
              add("metadata", Value::object(std::move(kept)));
            }
          }
        } else if constexpr (std::is_same_v<T, DecisionEvent>) {
          identity("decision", e.decision);
          identity("actor", e.actor);
          identity("conversation", e.conversation);
          identity("workflow", e.workflow);
          identity("generation", e.definition);
          identity("revision", e.context);
          add("payload_bytes", number(e.continuation.size()));
          if (e.continuation.size() <= 4096) {
            const auto packet = query_packet(trace, fact.record, e.continuation);
            if (packet.has_value())
              if (const auto *t = packet.value().find("time"))
                add("time", *t);
            if (packet.has_value())
              if (const auto *name = packet.value().find("operation");
                  name && std::holds_alternative<std::string>(name->value()) &&
                  name->string().size() <= 256)
                add("operation", *name);
          }
        } else if constexpr (std::is_same_v<T, InvocationEvent>) {
          identity("invocation", e.invocation);
          identity("decision", e.decision);
          identity("generation", e.definition);
          add("payload_bytes", number(e.input.size()));
        } else if constexpr (std::is_same_v<T, AttemptAdmissionEvent>) {
          identity("attempt", e.attempt);
          identity("invocation", e.invocation);
          identity("decision", e.decision);
          add("payload_bytes", number(e.input.size()));
          add("outcome", Value{"not established by admission"});
        } else if constexpr (std::is_same_v<T, AttemptObservationEvent>) {
          identity("attempt", e.attempt);
          add("phase", number(static_cast<std::size_t>(e.phase)));
          constexpr const char *outcomes[] = {"none", "success", "failure",
                                              "cancellation", "unknown"};
          add("outcome", Value{outcomes[static_cast<std::size_t>(e.disposition)]});
          add("payload_bytes", number(e.observation.size()));
          if (e.observation.size() <= 4096) {
            const auto packet = query_packet(trace, fact.record, e.observation);
            if (packet.has_value())
              for (const auto key : {"result_id", "result_record"})
                if (const auto *v = packet.value().find(key))
                  add(key, *v);
          }
        } else if constexpr (std::is_same_v<T, AttemptOpenEvent> ||
                             std::is_same_v<T, AdapterReceiptEvent>) {
          identity("attempt", e.attempt);
        } else if constexpr (std::is_same_v<T, ComplaintEvent>) {
          identity("complaint", e.complaint);
          identity("actor", e.actor);
          add("payload_bytes", number(e.detail.size()));
        }
      },
      fact.event.body);
  return row;
}
std::vector<std::byte> payload(const RetainedBody &body, RecordReference record,
                               RawQueryTrace &trace, RawEventRef &read) {
  return std::visit(
      [&](const auto &e) -> std::vector<std::byte> {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, ApplicationRecordEvent>)
          return unwrap(query_bytes(trace.reference(), record, e.payload, &read));
        else if constexpr (std::is_same_v<T, DecisionEvent>)
          return unwrap(query_bytes(trace.reference(), record, e.continuation, &read));
        else if constexpr (std::is_same_v<T, InvocationEvent> ||
                           std::is_same_v<T, AttemptAdmissionEvent>)
          return unwrap(query_bytes(trace.reference(), record, e.input, &read));
        else if constexpr (std::is_same_v<T, AttemptObservationEvent>)
          return unwrap(query_bytes(trace.reference(), record, e.observation, &read));
        else if constexpr (std::is_same_v<T, ComplaintEvent>)
          return unwrap(query_bytes(trace.reference(), record, e.detail, &read));
        else
          return {};
      },
      body);
}
Value trajectory_query(RetainedState &root, const Value &q, RawQueryTrace &trace) {
  trace.stage(1);
  if (!std::holds_alternative<Value::Object>(q.value()))
    throw Error{ErrorCode::invalid_range};
  for (const auto &[key, value] : q.object()) {
    (void)value;
    if (key != "cursor" && key != "end" && key != "count" && key != "scan" &&
        key != "attempt" && key != "variables" && key != "variable")
      throw Error{ErrorCode::invalid_range};
  }
  const auto end = index(q, "end", root.fact_count(), trace, RawQueryField::end);
  const auto count = index(q, "count", 32, trace, RawQueryField::count),
             scan = index(q, "scan", 128, trace, RawQueryField::scan);
  if (end > root.fact_count() || count == 0 || count > 64 || scan == 0 || scan > 256)
    throw Error{ErrorCode::invalid_range};
  std::string attempt, invocation, decision;
  bool variables = false;
  std::string variable;
  if (const auto *v = q.find("variables")) {
    const auto *b = std::get_if<bool>(&v->value());
    if (!b)
      throw Error{ErrorCode::invalid_range};
    variables = *b;
    trace.field(RawQueryField::variables, variables);
  }
  if (const auto *v = q.find("variable")) {
    const auto *s = std::get_if<std::string>(&v->value());
    if (!s || s->empty() || s->size() > 64)
      throw Error{ErrorCode::invalid_range};
    variable = *s;
    trace.variable(variable);
    variables = true;
  }
  if (const auto *selector = q.find("attempt")) {
    const auto *text = std::get_if<std::string>(&selector->value());
    if (!text || text->size() != 32)
      throw Error{ErrorCode::invalid_range};
    IdentityBytes bytes{};
    auto nibble = [](char c) -> unsigned {
      if (c >= '0' && c <= '9')
        return static_cast<unsigned>(c - '0');
      if (c >= 'a' && c <= 'f')
        return static_cast<unsigned>(c - 'a') + 10U;
      throw Error{ErrorCode::invalid_range};
    };
    for (std::size_t i = 0; i < bytes.size(); ++i)
      bytes[i] = std::byte{static_cast<unsigned char>((nibble((*text)[2 * i]) << 4U) |
                                                      nibble((*text)[2 * i + 1]))};
    const auto identity = unwrap(OperationAttemptId::from_bytes(bytes));
    trace.identity_field(RawQueryField::attempt, identity.bytes());
    const std::array fields{ByteView{identity.bytes()}};
    trace.stage(2);
    RawQueryLookup lookup{trace.reference(), RawQueryLookupKind::attempt, fields, 163};
    const auto state = lookup.run([&] {
      auto result = root.attempt(identity);
      if (!result.has_value()) {
        lookup.failure(1, result.error());
        throw result.error();
      }
      lookup.attempt(result.value());
      return std::move(result).value();
    });
    attempt = *text;
    invocation = hex_identity(state.admission.invocation.bytes());
    decision = hex_identity(state.admission.decision.bytes());
  }
  const auto cursor = index(
      q, "cursor", attempt.empty() && !variables ? (end > 128 ? end - 128 : 0) : 0,
      trace, RawQueryField::cursor);
  if (cursor > end)
    throw Error{ErrorCode::invalid_range};
  const auto stop = cursor + std::min(scan, end - cursor);
  auto next = cursor;
  Value::Array rows;
  while (next < stop && rows.size() < count) {
    const std::array fields{detail::raw_native_field(next)};
    trace.stage(2);
    RawQueryLookup lookup{trace.reference(), RawQueryLookupKind::fact, fields, 158};
    RetainedKind retained_kind_value{};
    auto fact = query_fact(root, next, lookup, retained_kind_value);
    trace.stage(3);
    auto row = summary(fact, next, retained_kind_value, trace);
    ++next;
    if (variables) {
      const auto *v = row.find("variable");
      if (!v || (!variable.empty() && v->string() != variable)) {
        lookup.filter(2);
        continue;
      }
    }
    if (!attempt.empty()) {
      auto matches = [&](std::string_view key, const std::string &value) {
        const auto *v = row.find(key);
        return v && v->string() == value;
      };
      const auto *metadata = row.find("metadata");
      const auto *capture_attempt = metadata ? metadata->find("attempt") : nullptr;
      const auto *source = row.find("source");
      const auto *sample_attempt = source ? source->find("attempt") : nullptr;
      if (!matches("attempt", attempt) &&
          !(row.find("attempt") == nullptr && matches("invocation", invocation)) &&
          !(row.find("attempt") == nullptr && row.find("invocation") == nullptr &&
            matches("decision", decision)) &&
          !(capture_attempt && capture_attempt->string() == attempt) &&
          !(sample_attempt && sample_attempt->string() == attempt)) {
        lookup.filter(3);
        continue;
      }
    }
    lookup.filter(1);
    const auto kind =
        static_cast<std::size_t>(std::stoull(field(row, "kind").number().text()));
    constexpr const char *names[] = {"",
                                     "identity_reserved",
                                     "decision",
                                     "invocation",
                                     "attempt_admitted",
                                     "attempt_open",
                                     "observation",
                                     "retry",
                                     "identity_conflict",
                                     "complaint",
                                     "rejected_submission",
                                     "adapter_receipt",
                                     "recovery_choice",
                                     "provisional_capture",
                                     "application"};
    if (kind == 0 || kind >= std::size(names))
      throw Error{ErrorCode::corrupt};
    row.object().emplace_back("event", Value{names[kind]});
    rows.push_back(std::move(row));
  }
  trace.stage(4);
  const auto row_count = rows.size();
  auto result = Value::object(
      {{"schema", Value{"blackbird.trajectory.v1"}},
       {"journal", Value{hex_identity(root.cursor().journal.bytes())}},
       {"cursor", number(cursor)},
       {"end", number(end)},
       {"next", number(next)},
       {"scanned", number(next - cursor)},
       {"caught_up", Value{next == end}},
       {"events", Value{std::move(rows)}},
       {"scope",
        Value{"committed prefix only; recorded order, not inferred causation; "
              "missing settlement is not success; originals via audit_inspect"}}});
  const std::array values{end, cursor, next, row_count};
  trace.returned(4, values);
  return result;
}
} // namespace
Value AuditLog::trajectory(const Value &q, std::optional<OperationAttemptId> attempt) {
  RawQueryTrace trace{RawQueryMethod::trajectory, attempt, root_.cursor(),
                      root_.fact_count()};
  return trace.run([&] { return trajectory_query(root_, q, trace); });
}
Value AuditLog::variables(const Value &q, std::optional<OperationAttemptId> attempt) {
  RawQueryTrace trace{RawQueryMethod::variables, attempt, root_.cursor(),
                      root_.fact_count()};
  return trace.run([&] {
    trace.stage(1);
    auto query = q;
    if (query.find("variables"))
      throw Error{ErrorCode::invalid_range};
    query.object().emplace_back("variables", Value{true});
    return trajectory_query(root_, query, trace);
  });
}
Value AuditLog::inspect(const Value &q, std::optional<OperationAttemptId> attempt) {
  RawQueryTrace trace{RawQueryMethod::inspect, attempt, root_.cursor(),
                      root_.fact_count()};
  return trace.run([&] {
    trace.stage(1);
    if (!std::holds_alternative<Value::Object>(q.value()))
      throw Error{ErrorCode::invalid_range};
    const auto fact_count = root_.fact_count();
    const auto end = index(q, "end", fact_count, trace, RawQueryField::end);
    const auto count = index(q, "count", 32, trace, RawQueryField::count),
               limit = index(q, "limit", 4096, trace, RawQueryField::limit);
    if (end > fact_count || count == 0 || count > 64 || limit == 0 || limit > 65536)
      throw Error{ErrorCode::invalid_range};
    if (q.find("record")) {
      const auto i = index(q, "record", 0, trace, RawQueryField::record);
      if (i >= end)
        throw Error{ErrorCode::invalid_range};
      const std::array fact_fields{detail::raw_native_field(i)};
      trace.stage(2);
      RawQueryLookup fact_lookup{trace.reference(), RawQueryLookupKind::fact,
                                 fact_fields, 158};
      RetainedKind retained_kind_value{};
      const auto fact = query_fact(root_, i, fact_lookup, retained_kind_value);
      trace.stage(3);
      auto result = summary(fact, i, retained_kind_value, trace);
      fact_lookup.filter(1);
      RawEventRef raw_read{};
      trace.stage(2);
      auto raw = payload(fact.event.body, fact.record, trace, raw_read);
      trace.stage(1);
      if (q.find("source")) {
        const auto s = index(q, "source", 0, trace, RawQueryField::source);
        if (s >= fact.event.dependencies.size())
          throw Error{ErrorCode::invalid_range};
        const auto source = fact.event.dependencies[s];
        const std::array fields{ByteView{source.journal.bytes()},
                                detail::raw_native_field(source.sequence)};
        trace.stage(2);
        RawQueryLookup lookup{trace.reference(), RawQueryLookupKind::source, fields,
                              115};
        auto original = lookup.run([&] {
          auto acquired = root_.source(source);
          if (!acquired.has_value()) {
            lookup.failure(1, acquired.error());
            throw acquired.error();
          }
          lookup.bytes(acquired.value().size());
          return std::move(acquired).value();
        });
        raw_read = lookup.reference();
        raw = std::move(original);
      }
      if (const auto *requested = q.find("packet")) {
        trace.stage(1);
        const auto *decode = std::get_if<bool>(&requested->value());
        if (!decode)
          throw Error{ErrorCode::invalid_range};
        trace.field(RawQueryField::packet, *decode);
        if (*decode) {
          const auto *application =
              std::get_if<ApplicationRecordEvent>(&fact.event.body);
          if ((!application &&
               !std::holds_alternative<DecisionEvent>(fact.event.body) &&
               !std::holds_alternative<AttemptObservationEvent>(fact.event.body)) ||
              q.find("diagnostic") || q.find("offset"))
            throw Error{ErrorCode::invalid_range};
          if (raw.size() > limit)
            throw Error{ErrorCode::capacity};
          ValueLimits limits;
          limits.bytes = limit;
          trace.stage(3);
          auto packet = query_decode(trace, fact.record, raw, limits, raw_read);
          trace.stage(4);
          const auto expanded = unwrap(format_value(packet, limits));
          result.object().emplace_back("end", number(end));
          result.object().emplace_back("total_bytes", number(raw.size()));
          result.object().emplace_back("decoded_bytes", number(expanded.size()));
          result.object().emplace_back("encoding", Value{"decoded-metadata"});
          result.object().emplace_back("packet", std::move(packet));
          const std::array values{end, raw.size(), expanded.size()};
          trace.returned(3, values);
          return result;
        }
      }
      if (q.find("diagnostic")) {
        trace.stage(1);
        const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
        if (!record || record->channel != ApplicationChannel::log)
          throw Error{ErrorCode::invalid_range};
        RawEventRef metadata_read{};
        trace.stage(3);
        const auto packet = unwrap(
            query_packet(trace, fact.record, record->payload, {}, &metadata_read));
        if (string_field(packet, "label") != "provider.stream")
          throw Error{ErrorCode::invalid_range};
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
        if (seconds < 0)
          throw Error{ErrorCode::invalid_range};
        const auto now = static_cast<std::uint64_t>(seconds);
        const auto &name = string_field(field(packet, "metadata"), "diagnostic_file");
        const std::array fields{ByteView{fact.record.journal.bytes()},
                                detail::raw_native_field(fact.record.sequence),
                                ByteView{metadata_read.lane},
                                detail::raw_native_field(metadata_read.ordinal),
                                detail::raw_native_field(now)};
        trace.stage(2);
        RawQueryLookup lookup{trace.reference(), RawQueryLookupKind::diagnostic, fields,
                              147};
        auto original = lookup.run([&] {
          auto acquired = root_.read_diagnostic(name, now);
          if (!acquired.has_value()) {
            lookup.failure(1, acquired.error());
            throw acquired.error();
          }
          lookup.bytes(acquired.value().size());
          return std::move(acquired).value();
        });
        raw_read = lookup.reference();
        raw = std::move(original);
      }
      trace.stage(1);
      const auto offset =
          std::min(index(q, "offset", 0, trace, RawQueryField::offset), raw.size());
      const auto n = std::min(limit, raw.size() - offset);
      trace.stage(4);
      constexpr char digits[] = "0123456789abcdef";
      std::string hex;
      hex.reserve(n * 2);
      for (const auto b : std::span<const std::byte>{raw}.subspan(offset, n)) {
        const auto v = std::to_integer<unsigned>(b);
        hex += digits[v >> 4U];
        hex += digits[v & 15U];
      }
      result.object().emplace_back("end", number(end));
      result.object().emplace_back("total_bytes", number(raw.size()));
      result.object().emplace_back("offset", number(offset));
      const auto next = offset + n;
      result.object().emplace_back("next", number(next));
      result.object().emplace_back("encoding", Value{"hex-original-bytes"});
      result.object().emplace_back("hex", Value{std::move(hex)});
      const std::array values{end, raw.size(), offset, next, n};
      trace.returned(2, values);
      return result;
    }
    if (q.find("packet"))
      throw Error{ErrorCode::invalid_range};
    const auto cursor =
        std::min(index(q, "cursor", 0, trace, RawQueryField::cursor), end);
    const auto next = cursor + std::min(count, end - cursor);
    Value::Array rows;
    for (auto i = cursor; i < next; ++i) {
      const std::array fields{detail::raw_native_field(i)};
      trace.stage(2);
      RawQueryLookup lookup{trace.reference(), RawQueryLookupKind::fact, fields, 158};
      RetainedKind kind{};
      auto fact = query_fact(root_, i, lookup, kind);
      trace.stage(3);
      auto row = summary(fact, i, kind, trace);
      lookup.filter(1);
      rows.push_back(std::move(row));
    }
    trace.stage(4);
    const auto row_count = rows.size();
    auto result = Value::object(
        {{"end", number(end)},
         {"next", number(next)},
         {"records", Value{std::move(rows)}},
         {"scope", Value{"committed facts only; end pins append-only prefix; record "
                         "indices stable within this journal lineage"}}});
    const std::array values{end, next, row_count};
    trace.returned(1, values);
    return result;
  });
}
} // namespace blackbird
