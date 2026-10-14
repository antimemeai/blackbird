#include "blackbird/decision_models.hpp"
#include "blackbird/json.hpp"
#include "blackbird/raw_caller.hpp"
#include "native_process.hpp"
#include <cmath>
#include <set>

namespace blackbird {
namespace {
using detail::fail;
template <class T> T take(Result<T> value) {
  if (!value.has_value())
    throw value.error();
  return std::move(value).value();
}
bool object(const Value &v) { return std::holds_alternative<Value::Object>(v.value()); }
bool array(const Value &v) { return std::holds_alternative<Value::Array>(v.value()); }
bool text(const Value &v) { return std::holds_alternative<std::string>(v.value()); }
bool description(const Value &v, bool nullable = false) {
  return text(v) || object(v) || array(v) ||
         (nullable && std::holds_alternative<std::nullptr_t>(v.value()));
}
const Value &need(const Value &v, std::string_view key, ErrorCode code) {
  const auto *p = v.find(key);
  if (!p)
    fail(code);
  return *p;
}
std::string str(const Value &v, std::string_view key, ErrorCode code) {
  const auto &p = need(v, key, code);
  if (!text(p) || p.string().empty() || p.string().find('\0') != std::string::npos)
    fail(code);
  return p.string();
}
void unique(const Value &v, ErrorCode code) {
  if (!object(v))
    fail(code);
  std::set<std::string> seen;
  for (const auto &[k, value] : v.object()) {
    (void)value;
    if (k.empty() || !seen.insert(k).second)
      fail(code);
  }
}
double number(const Value &v) {
  if (!std::holds_alternative<Number>(v.value()))
    fail(ErrorCode::corrupt);
  double result = 0;
  const auto &s = v.number().text();
  const auto p = std::from_chars(s.data(), s.data() + s.size(), result);
  if (p.ec != std::errc{} || p.ptr != s.data() + s.size() || !std::isfinite(result))
    fail(ErrorCode::corrupt);
  return result;
}
double probability(const Value &v) {
  const auto n = number(v);
  if (n < 0 || n > 1)
    fail(ErrorCode::corrupt);
  return n;
}
void tokens(const Value &v) {
  if (!std::holds_alternative<Number>(v.value()))
    fail(ErrorCode::corrupt);
  std::uint64_t n = 0;
  const auto &s = v.number().text();
  auto p = std::from_chars(s.data(), s.data() + s.size(), n);
  if (p.ec != std::errc{} || p.ptr != s.data() + s.size())
    fail(ErrorCode::corrupt);
}
int timeout(const Value &args) {
  const auto *v = args.find("timeout_seconds");
  if (!v)
    return 30;
  if (!std::holds_alternative<Number>(v->value()))
    fail(ErrorCode::invalid_range);
  int n = 0;
  const auto &s = v->number().text();
  auto p = std::from_chars(s.data(), s.data() + s.size(), n);
  if (p.ec != std::errc{} || p.ptr != s.data() + s.size() || n < 1 || n > 3600)
    fail(ErrorCode::invalid_range);
  return n;
}
std::string quote(std::string_view value) {
  std::string out = "\"";
  for (char c : value) {
    if (static_cast<unsigned char>(c) < 32 || c == 127)
      fail(ErrorCode::invalid_range);
    if (c == '\\' || c == '"')
      out += '\\';
    out += c;
  }
  return out + '"';
}
std::string trim(const std::string &s) {
  const auto first = s.find_first_not_of(" \t\r");
  if (first == std::string::npos)
    return {};
  return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}
std::string checked_key(std::string value) {
  if (value.empty() || value.size() > 8192)
    fail(ErrorCode::unsupported);
  for (char c : value)
    if (static_cast<unsigned char>(c) <= 32 || static_cast<unsigned char>(c) >= 127)
      fail(ErrorCode::unsupported);
  return value;
}
std::string credential(const DecisionModelConfig &config) {
  for (const char *name : {"TYPESAFE_API_KEY", "jev"})
    if (const auto *v = std::getenv(name); v && *v)
      return checked_key(v);
  auto path = config.credential_file;
  if (path.empty())
    if (const auto *v = std::getenv("BLACKBIRD_JEV_ENV_FILE"))
      path = v;
  if (path.empty())
    fail(ErrorCode::unsupported);
  std::ifstream file(path);
  if (!file)
    fail(ErrorCode::unsupported);
  std::string input;
  char c;
  while (file.get(c)) {
    if (input.size() >= 65536)
      fail(ErrorCode::capacity);
    input += c;
  }
  if (!file.eof())
    fail(ErrorCode::io);
  std::string primary, alternate;
  std::size_t pos = 0;
  while (pos < input.size()) {
    auto end = input.find('\n', pos);
    if (end == std::string::npos)
      end = input.size();
    auto line = trim(input.substr(pos, end - pos));
    pos = end + 1;
    if (line.starts_with("export "))
      line = trim(line.substr(7));
    const auto equals = line.find('=');
    if (equals == std::string::npos)
      continue;
    const auto name = trim(line.substr(0, equals));
    if (name != "TYPESAFE_API_KEY" && name != "jev")
      continue;
    auto value = trim(line.substr(equals + 1));
    if (value.empty())
      fail(ErrorCode::unsupported);
    if (value.front() == '\'' || value.front() == '"') {
      const auto closing = value.find(value.front(), 1);
      if (closing == std::string::npos)
        fail(ErrorCode::unsupported);
      const auto remainder = trim(value.substr(closing + 1));
      if (!remainder.empty() && !remainder.starts_with('#'))
        fail(ErrorCode::unsupported);
      value = value.substr(1, closing - 1);
    } else {
      const auto comment = value.find_first_of(" \t#");
      if (comment != std::string::npos) {
        const auto remainder = trim(value.substr(comment));
        if (!remainder.empty() && !remainder.starts_with('#'))
          fail(ErrorCode::unsupported);
        value.resize(comment);
      }
    }
    (name == "TYPESAFE_API_KEY" ? primary : alternate) = checked_key(value);
  }
  return checked_key(primary.empty() ? alternate : primary);
}
} // namespace

Value jev_request(const Value &args) {
  unique(args, ErrorCode::invalid_range);
  for (const auto &[k, v] : args.object()) {
    (void)v;
    if (k != "provider" && k != "model" && k != "state" && k != "questions" &&
        k != "timeout_seconds")
      fail(ErrorCode::invalid_range);
  }
  if (args.find("provider") && str(args, "provider", ErrorCode::invalid_range) != "jev")
    fail(ErrorCode::unsupported);
  const auto model =
      args.find("model") ? str(args, "model", ErrorCode::invalid_range) : "jev-1.13.0";
  if (!model.starts_with("jev-") || model.size() > 128)
    fail(ErrorCode::invalid_range);
  (void)timeout(args);
  const auto &state = need(args, "state", ErrorCode::invalid_range);
  if (!description(state))
    fail(ErrorCode::invalid_range);
  const auto &questions = need(args, "questions", ErrorCode::invalid_range);
  unique(questions, ErrorCode::invalid_range);
  if (questions.object().empty())
    fail(ErrorCode::invalid_range);
  for (const auto &[id, q] : questions.object()) {
    (void)id;
    unique(q, ErrorCode::invalid_range);
    const auto type = str(q, "type", ErrorCode::invalid_range);
    if (!description(need(q, "instructions", ErrorCode::invalid_range), true))
      fail(ErrorCode::invalid_range);
    for (const auto &[k, v] : q.object()) {
      (void)v;
      if (k != "type" && k != "instructions" && k != "criteria")
        fail(ErrorCode::invalid_range);
    }
    const auto *criteria = q.find("criteria");
    if (type == "choice") {
      if (!criteria)
        fail(ErrorCode::invalid_range);
      unique(*criteria, ErrorCode::invalid_range);
      if (criteria->object().empty() || criteria->object().size() > 255)
        fail(ErrorCode::invalid_range);
      for (const auto &[key, v] : criteria->object()) {
        (void)key;
        if (!description(v, true))
          fail(ErrorCode::invalid_range);
      }
    } else if (type == "score") {
      if (!criteria || !array(*criteria) || criteria->array().size() < 2 ||
          criteria->array().size() > 10)
        fail(ErrorCode::invalid_range);
      for (const auto &v : criteria->array())
        if (!description(v, true))
          fail(ErrorCode::invalid_range);
    } else if (type == "noul") {
      if (criteria) {
        unique(*criteria, ErrorCode::invalid_range);
        for (const auto &[key, v] : criteria->object())
          if ((key != "true" && key != "false") || !description(v, true))
            fail(ErrorCode::invalid_range);
      }
    } else
      fail(ErrorCode::invalid_range);
  }
  return Value::object(
      {{"model", Value{model}}, {"state", state}, {"questions", questions}});
}

void validate_jev_response(const Value &request, const Value &response) {
  unique(response, ErrorCode::corrupt);
  const auto model = str(response, "model", ErrorCode::corrupt);
  const auto requested = str(request, "model", ErrorCode::corrupt);
  if (!model.starts_with("jev-") || model == "jev-latest" || model == "jev-preview" ||
      ((requested != "jev-latest" && requested != "jev-preview") && model != requested))
    fail(ErrorCode::corrupt);
  const auto &answers = need(response, "answers", ErrorCode::corrupt);
  unique(answers, ErrorCode::corrupt);
  const auto &questions = need(request, "questions", ErrorCode::corrupt);
  if (answers.object().size() != questions.object().size())
    fail(ErrorCode::corrupt);
  const auto &usage = need(response, "usage", ErrorCode::corrupt);
  unique(usage, ErrorCode::corrupt);
  tokens(need(usage, "input_tokens", ErrorCode::corrupt));
  tokens(need(usage, "output_tokens", ErrorCode::corrupt));
  for (const auto &[id, q] : questions.object()) {
    const auto &a = need(answers, id, ErrorCode::corrupt);
    unique(a, ErrorCode::corrupt);
    const auto type = str(q, "type", ErrorCode::corrupt);
    if (str(a, "type", ErrorCode::corrupt) != type)
      fail(ErrorCode::corrupt);
    if (type == "noul") {
      (void)probability(need(a, "noul", ErrorCode::corrupt));
      continue;
    }
    (void)probability(need(a, "confidence", ErrorCode::corrupt));
    const auto &p = need(a, "probabilities", ErrorCode::corrupt);
    unique(p, ErrorCode::corrupt);
    const auto &criteria = need(q, "criteria", ErrorCode::corrupt);
    const auto count =
        type == "choice" ? criteria.object().size() : criteria.array().size();
    if (p.object().size() != count)
      fail(ErrorCode::corrupt);
    double sum = 0, weighted = 0, highest = 0;
    for (std::size_t i = 0; i < count; ++i) {
      const auto key =
          type == "choice" ? criteria.object()[i].first : std::to_string(i);
      const auto n = probability(need(p, key, ErrorCode::corrupt));
      sum += n;
      weighted += n * static_cast<double>(i);
      highest = std::max(highest, n);
    }
    if (std::abs(sum - 1) > 0.0001)
      fail(ErrorCode::corrupt);
    if (type == "choice") {
      const auto chosen = str(a, "choice", ErrorCode::corrupt);
      if (!criteria.find(chosen) ||
          probability(need(p, chosen, ErrorCode::corrupt)) < highest)
        fail(ErrorCode::corrupt);
    } else {
      const auto score = number(need(a, "score", ErrorCode::corrupt));
      if (score < 0 || score > static_cast<double>(count - 1) ||
          std::abs(score - weighted) > 0.0001)
        fail(ErrorCode::corrupt);
      const auto &legend = need(a, "legend", ErrorCode::corrupt);
      unique(legend, ErrorCode::corrupt);
      if (legend.object().size() != count)
        fail(ErrorCode::corrupt);
      for (std::size_t i = 0; i < count; ++i) {
        const auto &label = need(legend, std::to_string(i), ErrorCode::corrupt);
        if (!text(label) || (text(criteria.array()[i]) && label != criteria.array()[i]))
          fail(ErrorCode::corrupt);
      }
    }
  }
}

Value DecisionModels::evaluate(const Value &args, const RawAdapterContext *context) {
  RawCallerTrace trace{{3600, 3601},
                       context,
                       std::array{detail::raw_native_field(config_.request_limit),
                                  detail::raw_native_field(config_.response_limit)}};
  return trace.run([&] {
    trace.stage(1);
    const auto started = detail::Clock::now();
    const auto request = jev_request(args);
    trace.stage(2);
    if (trace.enabled() && config_.request_limit > UINT32_MAX - 48)
      throw Error{ErrorCode::overflow};
    auto original = trace.data(48 + config_.request_limit);
    const auto encoded = take(dump_json(request, {config_.request_limit, 500000, 128}));
    trace.bytes(3602, encoded, original);
    original.reset();
    const auto seconds = timeout(args);
    const auto deadline = started + std::chrono::seconds{seconds};
    trace.stage(3);
    const auto key = credential(config_);
    trace.stage(4);
    std::string input =
        "url = \"https://api.typesafe.ai/v1/systemone\"\nheader = " +
        quote("Authorization: Bearer " + key) +
        "\nheader = \"Content-Type: application/json\"\ndata-binary = " +
        quote(encoded) + "\n";
    detail::Child child;
    child.cancelled = cancelled;
    child.deadline_error = {ErrorCode::provider_transport, 28};
    if (trace.enabled())
      child.raw_capture(trace.reference(), true, false);
    const auto notify = [&](std::uint8_t channel, std::string_view label,
                            std::string_view bytes) {
      const auto owner = trace.context();
      const auto child_ref = child.raw_reference();
      const bool child_present = child_ref.ordinal != 0;
      const bool present = static_cast<bool>(observer);
      const auto length = bytes.size();
      std::array<ByteView, 7> fields{};
      std::size_t count = 0;
      fields[count++] = detail::raw_native_field(channel);
      fields[count++] = detail::raw_native_field(child_present);
      if (child_present) {
        fields[count++] = child_ref.lane;
        fields[count++] = detail::raw_native_field(child_ref.ordinal);
      }
      fields[count++] = detail::raw_native_field(length);
      fields[count++] = detail::raw_native_field(present);
      RawCallerTrace callback{{3606, 3607}, &owner, std::span{fields}.first(count)};
      callback.run([&] {
        if (present)
          observer(label, bytes);
        else
          callback.finish(RawOperationBranch::absent);
      });
    };
    child.output_observer = [&](std::string_view chunk) {
      notify(1, "decision_model.response", chunk);
    };
    trace.stage(5);
    child.start({config_.curl_executable, "--disable", "--silent", "--max-time",
                 std::to_string(seconds), "--proto", "=https", "--max-redirs", "0",
                 "--write-out", "\n%{http_code}", "--config", "-"});
    child.send(input, deadline);
    child.close_input();
    notify(2, "decision_model.request", encoded);
    trace.stage(6);
    auto exit_credit = trace.control(68);
    int exit = 0;
    auto raw = child.collect(deadline, config_.response_limit, &exit);
    const auto child_ref = child.raw_reference();
    trace.event(3603,
                std::array{ByteView{child_ref.lane},
                           detail::raw_native_field(child_ref.ordinal),
                           detail::raw_native_field(exit)},
                exit_credit);
    trace.stage(7);
    if (exit != 0)
      fail(ErrorCode::provider_transport, exit);
    trace.stage(8);
    const auto owner = trace.context();
    RawCallerTrace http{{3604, 3605}, &owner, {}, sizeof(int)};
    const int status = http.run([&] {
      const auto split = raw.rfind('\n');
      if (split == std::string::npos || raw.size() - split != 4)
        fail(ErrorCode::corrupt);
      int observed = 0;
      const auto code = std::string_view{raw}.substr(split + 1);
      const auto parsed =
          std::from_chars(code.data(), code.data() + code.size(), observed);
      if (parsed.ec != std::errc{} || parsed.ptr != code.data() + code.size())
        fail(ErrorCode::corrupt);
      http.finish(RawOperationBranch::returned, nullptr,
                  std::array{detail::raw_native_field(observed)});
      raw.resize(split);
      return observed;
    });
    if (status < 200 || status >= 300)
      fail(ErrorCode::external_unknown, status);
    trace.stage(9);
    const auto input_size = raw.size();
    RawCallerTrace parser{{3608, 3609},
                          &owner,
                          std::array{detail::raw_native_field(input_size)},
                          sizeof(bool)};
    auto parsed = parser.run([&] {
      auto value = parse_json(raw, {config_.response_limit, 500000, 128});
      const bool ok = value.has_value();
      const auto error = ok ? Error{} : value.error();
      parser.finish(RawOperationBranch::returned, ok ? nullptr : &error,
                    std::array{detail::raw_native_field(ok)});
      return value;
    });
    auto response = take(std::move(parsed));
    trace.stage(10);
    RawCallerTrace validator{{3610, 3611}, &owner};
    validator.run([&] { validate_jev_response(request, response); });
    trace.stage(11);
    return Value::object(
        {{"status", Value{"ok"}},
         {"provider", Value{"jev"}},
         {"requested_model", need(request, "model", ErrorCode::corrupt)},
         {"response", std::move(response)}});
  });
}
} // namespace blackbird
