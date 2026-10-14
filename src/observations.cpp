#include "blackbird/observations.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_caller.hpp"
#include "native_process.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <map>
#include <set>
#include <unistd.h>
namespace blackbird {
namespace {
Value integer(std::int64_t n) { return Value{Number{n}}; }
std::int64_t numeric(const Value &v) {
  const auto *n = std::get_if<Number>(&v.value());
  if (!n)
    throw Error{ErrorCode::invalid_range};
  std::int64_t out = 0;
  const auto digits = n->text();
  const auto r = std::from_chars(digits.data(), digits.data() + digits.size(), out);
  if (r.ec != std::errc{} || r.ptr != digits.data() + digits.size())
    throw Error{ErrorCode::invalid_range};
  return out;
}
bool oid(std::string_view text) {
  return (text.size() == 40 || text.size() == 64) &&
         std::all_of(text.begin(), text.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
std::string line(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
    text.pop_back();
  return text;
}
std::string display(std::string_view text) {
  std::string out;
  for (char c : text) {
    const auto b = static_cast<unsigned char>(c);
    out += b < 32 || b == 127 ? ' ' : c;
  }
  return out;
}
} // namespace
ObservationTime observation_time() {
  const auto before = std::chrono::steady_clock::now();
  const auto utc = std::chrono::system_clock::now();
  const auto after = std::chrono::steady_clock::now();
  return {
      std::chrono::duration_cast<std::chrono::nanoseconds>(utc.time_since_epoch())
          .count(),
      std::chrono::duration_cast<std::chrono::nanoseconds>(before.time_since_epoch())
          .count(),
      static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(after - before)
              .count())};
}
Observations::Observations(AuditLog &log, std::function<ObservationTime()> clock)
    : log_(log), clock_(std::move(clock)),
      clock_id_(hex_identity(log.issue().bytes())) {
  char host[256]{};
  const bool known = gethostname(host, sizeof(host) - 1) == 0;
  log_.record(
      ApplicationChannel::log,
      Value::object(
          {{"label", Value{"clock.domain"}},
           {"clock_id", Value{clock_id_}},
           {"host", Value{known ? host : "unknown"}},
           {"pid", integer(getpid())},
           {"time", time()},
           {"synchronization", Value{"unknown"}},
           {"scope",
            Value{"local process clock mapping; cross-host alignment unverified"}}}));
}
Value Observations::time() const {
  const auto t = clock_();
  return Value::object({{"clock_id", Value{clock_id_}},
                        {"utc_ns", integer(t.utc_ns)},
                        {"monotonic_ns", integer(t.monotonic_ns)},
                        {"sampling_span_ns", Value{Number{t.sampling_span_ns}}},
                        {"synchronization", Value{"unknown"}}});
}
Value Observations::sample(std::string_view name, const Value &value,
                           const Value &source, std::string_view status) {
  if (name.empty() || name.size() > 64 ||
      !std::all_of(name.begin(), name.end(),
                   [](char c) {
                     return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                            c == '.' || c == '_' || c == '-';
                   }) ||
      (status != "observed" && status != "unavailable") ||
      !std::holds_alternative<Value::Object>(source.value()) ||
      unwrap(encode_packet_string(value)).size() > 4096 ||
      unwrap(encode_packet_string(source)).size() > 4096)
    throw Error{ErrorCode::invalid_range};
  const auto identity = log_.issue();
  auto packet = Value::object({{"label", Value{"variable.sample"}},
                               {"sample_id", Value{hex_identity(identity.bytes())}},
                               {"variable", Value{std::string{name}}},
                               {"value", value},
                               {"source", source},
                               {"observation_status", Value{std::string{status}}},
                               {"time", time()}});
  // issue precedes cursor selection, including any issuer reservation record.
  const auto record = log_.root().fact_count();
  log_.record(identity, ApplicationChannel::log, packet);
  packet.object().emplace_back("record", Value{Number{record}});
  packet.object().emplace_back(
      "journal", Value{hex_identity(log_.root().cursor().journal.bytes())});
  return packet;
}
void Observations::doctrine(std::string_view content, InvocationId invocation,
                            OperationAttemptId attempt) {
  if (doctrine_id_.empty() || content != doctrine_content_) {
    auto next_content = std::string{content};
    auto next_id = hex_identity(log_.issue().bytes());
    doctrine_content_.swap(next_content);
    doctrine_id_.swap(next_id);
  }
  sample(
      "doctrine.effective",
      Value::object({{"content_observation", Value{doctrine_id_}}}),
      Value::object(
          {{"kind", Value{"native_request_field"}},
           {"invocation", Value{hex_identity(invocation.bytes())}},
           {"attempt", Value{hex_identity(attempt.bytes())}},
           {"field", Value{"instructions"}},
           {"identity_scope",
            Value{
                "consecutive equal instruction bytes within this process observer"}}}));
}
Value Observations::git(const Value &query, const std::function<bool()> &cancelled,
                        const RawAdapterContext *context) {
  RawCallerTrace trace{{3700, 3701}, context};
  return trace.run([&] {
    trace.stage(1);
    if (!std::holds_alternative<Value::Object>(query.value()))
      throw Error{ErrorCode::invalid_range};
    for (const auto &[key, value] : query.object()) {
      (void)value;
      if (key != "path" && key != "history")
        throw Error{ErrorCode::invalid_range};
    }
    trace.stage(2);
    const auto &path_source = string_field(query, "path");
    if (path_source.empty() || path_source.size() > 4096 ||
        path_source.find('\0') != std::string::npos)
      throw Error{ErrorCode::invalid_range};
    auto path_credit = trace.data(48 + path_source.size());
    const auto path = path_source;
    trace.bytes(3702, path, path_credit);
    path_credit.reset();
    trace.stage(3);
    const auto count = query.find("history") ? numeric(field(query, "history")) : 0;
    if (count < 0 || count > 64)
      throw Error{ErrorCode::invalid_range};
    auto run = [&](std::uint8_t kind, std::initializer_list<std::string_view> sources,
                   int &code) {
      const auto parent = trace.context();
      const auto argc = sources.size();
      RawCallerTrace invocation{
          {3703, 3704},
          &parent,
          std::array{detail::raw_native_field(kind), detail::raw_native_field(argc)}};
      return invocation.run([&] {
        std::vector<std::string> args;
        args.reserve(argc);
        for (const auto source : sources) {
          const auto index = args.size();
          auto credit =
              invocation.data(56 + (kind == 3 && index == 7 ? 2 : source.size()));
          if (kind == 3 && index == 7) {
            auto formatted = std::to_string(count);
            const auto size = formatted.size();
            invocation.data_event(3705,
                                  std::array{detail::raw_native_field(index),
                                             detail::raw_native_field(size),
                                             detail::raw_string_field(formatted)},
                                  credit);
            args.emplace_back(std::move(formatted));
          } else {
            args.emplace_back(source);
            const auto size = args.back().size();
            invocation.data_event(3705,
                                  std::array{detail::raw_native_field(index),
                                             detail::raw_native_field(size),
                                             detail::raw_string_field(args.back())},
                                  credit);
          }
        }
        detail::Child child;
        child.cancelled = cancelled;
        for (char **e = environ; *e; ++e)
          if (!std::string_view{*e}.starts_with("GIT_"))
            child.child_environment.emplace_back(*e);
        child.child_environment.emplace_back("GIT_OPTIONAL_LOCKS=0");
        child.child_environment.emplace_back("GIT_TERMINAL_PROMPT=0");
        child.separate_error = true;
        if (invocation.enabled())
          child.raw_capture(invocation.reference(), true, true);
        child.start(std::move(args));
        child.close_input();
        auto exit_credit = invocation.control(68);
        auto result =
            child.collect(detail::Clock::now() + std::chrono::seconds{5}, 65536, &code);
        const auto child_ref = child.raw_reference();
        invocation.event(3706,
                         std::array{ByteView{child_ref.lane},
                                    detail::raw_native_field(child_ref.ordinal),
                                    detail::raw_native_field(code)},
                         exit_credit);
        return result;
      });
    };
    int code = 0;
    trace.stage(4);
    const auto repo = line(
        run(1, {"/usr/bin/git", "-C", path, "rev-parse", "--show-toplevel"}, code));
    if (code != 0) {
      auto observed = sample("git.head", Value{},
                             Value::object({{"kind", Value{"git_cli"}},
                                            {"path", Value{path}},
                                            {"exit_code", integer(code)}}),
                             "unavailable");
      return Value::object({{"samples", Value{Value::Array{observed}}},
                            {"scope", Value{"repository lookup failed"}}});
    }
    trace.stage(5);
    const auto head = line(
        run(2, {"/usr/bin/git", "-C", path, "rev-parse", "--verify", "HEAD"}, code));
    auto source = Value::object({{"kind", Value{"git_cli"}},
                                 {"repository", Value{repo}},
                                 {"requested_path", Value{path}},
                                 {"exit_code", integer(code)}});
    Value::Array samples;
    bool head_valid = false;
    if (code == 0) {
      auto predicate_credit = trace.control(45);
      head_valid = oid(head);
      trace.event(3707,
                  std::array{detail::raw_native_field(code),
                             detail::raw_native_field(head_valid)},
                  predicate_credit);
    }
    samples.push_back(sample("git.head", head_valid ? Value{head} : Value{}, source,
                             head_valid ? "observed" : "unavailable"));
    if (head_valid && count > 0) {
      trace.stage(6);
      const auto raw =
          run(3,
              {"/usr/bin/git", "-C", path, "log", "--no-show-signature",
               "--format=%H%x00%at%x00%ct%x00%P%x00", "-n", "", head, "--"},
              code);
      trace.stage(7);
      if (code != 0)
        throw Error{ErrorCode::io, code};
      std::size_t offset = 0;
      while (offset < raw.size()) {
        if (raw[offset] == '\n') {
          ++offset;
          continue;
        }
        std::array<std::string, 4> fields;
        for (auto &f : fields) {
          const auto end = raw.find('\0', offset);
          if (end == std::string::npos)
            throw Error{ErrorCode::corrupt};
          f = raw.substr(offset, end - offset);
          offset = end + 1;
        }
        if (!oid(fields[0]) || samples.size() > static_cast<std::size_t>(count))
          throw Error{ErrorCode::corrupt};
        const auto author = numeric(Value{Number{fields[1]}});
        const auto committer = numeric(Value{Number{fields[2]}});
        samples.push_back(
            sample("git.commit",
                   Value::object({{"commit", Value{fields[0]}},
                                  {"author_unix_seconds", integer(author)},
                                  {"committer_unix_seconds", integer(committer)},
                                  {"parents", Value{fields[3]}}}),
                   source));
      }
    }
    trace.stage(8);
    return Value::object(
        {{"samples", Value{std::move(samples)}},
         {"scope",
          Value{
              "point observations; commit metadata times are not activation times; "
              "HEAD is not loaded executable identity; worktree bytes not observed"}}});
  });
}
std::string correlation_plot(const Value &page) {
  struct Point {
    std::int64_t utc;
    std::string clock, lane, label, record;
  };
  std::vector<Point> points;
  std::size_t undated = 0;
  std::set<std::string> clocks;
  for (const auto &row : field(page, "events").array()) {
    const auto *t = row.find("time");
    const auto *clock_value = t ? t->find("clock_id") : nullptr;
    const auto *utc_value = t ? t->find("utc_ns") : nullptr;
    if (!clock_value || !utc_value ||
        !std::holds_alternative<std::string>(clock_value->value())) {
      ++undated;
      continue;
    }
    std::int64_t utc;
    try {
      utc = numeric(*utc_value);
    } catch (const Error &) {
      ++undated;
      continue;
    }
    const auto clock = clock_value->string();
    const auto *variable = row.find("variable");
    const auto *operation = row.find("operation");
    const auto *label = row.find("label");
    const auto lane = variable    ? variable->string()
                      : operation ? "work." + operation->string()
                      : label     ? label->string()
                                  : string_field(row, "event");
    const auto value = row.find("value");
    points.push_back({utc, clock, lane,
                      value ? unwrap(format_value(*value)) : string_field(row, "event"),
                      field(row, "record").number().text()});
    clocks.insert(clock);
  }
  if (points.empty())
    return "No timed observations in this page; undated=" + std::to_string(undated) +
           "\n";
  auto low = points[0].utc, high = low;
  for (const auto &p : points) {
    low = std::min(low, p.utc);
    high = std::max(high, p.utc);
  }
  std::map<std::string, std::string> lanes;
  for (const auto &p : points) {
    auto &lane = lanes[p.lane];
    if (lane.empty())
      lane = std::string(61, ' ');
    const auto fraction =
        high == low
            ? 0.0L
            : (static_cast<long double>(p.utc) - static_cast<long double>(low)) /
                  (static_cast<long double>(high) - static_cast<long double>(low));
    const auto position =
        std::min<std::size_t>(60, static_cast<std::size_t>(fraction * 60.0L));
    lane[position] = lane[position] == ' ' ? 'o' : '+';
  }
  std::string out = "UTC ns " + std::to_string(low) + " .. " + std::to_string(high) +
                    " (local wall clocks; synchronization unknown)\n";
  for (const auto &[name, lane] : lanes)
    out += display(name) + " |" + lane + "|\n";
  out += "o=observed point; +=overlap at this width; no intervals inferred; undated=" +
         std::to_string(undated) + " clocks=" + std::to_string(clocks.size()) + "\n";
  for (const auto &p : points)
    out += "#" + p.record + " " + std::to_string(p.utc) + " " + display(p.lane) + " " +
           display(p.label) + " clock=" + p.clock + "\n";
  out += "next=" + field(page, "next").number().text() +
         " end=" + field(page, "end").number().text() + "\n";
  return out;
}
} // namespace blackbird
