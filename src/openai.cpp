#include "blackbird/openai.hpp"
#include "blackbird/json.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/raw_auth.hpp"
#include "native_process.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#ifndef F_SETNOSIGPIPE
#include <pthread.h>
#endif

extern char **environ;
namespace blackbird {
namespace {
[[noreturn]] void fail(ErrorCode code, std::int64_t detail = 0) {
  throw Error{code, detail};
}
using detail::Child;
using detail::Clock;
template <class T> T own_value(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
const std::string *text(const Value *j) {
  return j == nullptr ? nullptr : std::get_if<std::string>(&j->value());
}
std::string required(const Value &j, std::string_view key) {
  const auto *s = text(j.find(key));
  if (s == nullptr || s->empty())
    fail(ErrorCode::corrupt);
  return *s;
}
bool header_safe(std::string_view s) {
  return !s.empty() && s.size() <= 65536 &&
         s.find_first_of("\r\n\0", 0, 3) == std::string_view::npos;
}
std::filesystem::path auth_path(const OpenAiConfig &config) {
  if (!config.codex_home.empty())
    return config.codex_home / "auth.json";
  const char *home = std::getenv("CODEX_HOME");
  if (home != nullptr && *home != '\0')
    return std::filesystem::path{home} / "auth.json";
  home = std::getenv("HOME");
  if (home == nullptr)
    fail(ErrorCode::invalid_range);
  return std::filesystem::path{home} / ".codex/auth.json";
}
Value auth_file(const OpenAiConfig &config) {
  std::ifstream file{auth_path(config), std::ios::binary};
  if (!file)
    fail(ErrorCode::io);
  std::string bytes;
  char chunk[4096];
  while (file.read(chunk, sizeof(chunk)) || file.gcount() != 0) {
    if (bytes.size() + static_cast<std::size_t>(file.gcount()) > 1024 * 1024)
      fail(ErrorCode::capacity);
    bytes.append(chunk, static_cast<std::size_t>(file.gcount()));
  }
  if (!file.eof())
    fail(ErrorCode::io);
  auto parsed = parse_json(bytes, {1024 * 1024, 4096, 32});
  if (!parsed.has_value())
    throw parsed.error();
  return std::move(parsed).value();
}
Value rpc(Child &child, std::string_view message, Clock::time_point deadline) {
  auto request = parse_json(message);
  if (!request.has_value())
    throw request.error();
  const auto id = required(request.value(), "id");
  child.send(message, deadline);
  for (unsigned count = 0; count < 128; ++count) {
    auto value = parse_json(child.line(deadline, 1024 * 1024), {1024 * 1024, 4096, 32});
    if (!value.has_value())
      throw value.error();
    const auto *reply_id = value.value().find("id");
    if (reply_id == nullptr || text(reply_id) == nullptr || reply_id->string() != id)
      continue;
    if (value.value().find("error") != nullptr)
      fail(ErrorCode::external_unknown);
    const auto *result = value.value().find("result");
    if (result == nullptr)
      fail(ErrorCode::corrupt);
    return *result;
  }
  fail(ErrorCode::capacity);
}
std::string config_quote(std::string_view s) {
  std::string out{"\""};
  for (char c : s) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += c;
      break;
    }
  }
  out += '"';
  return out;
}
template <class Operation>
auto private_auth_operation(RawAuthTrace &trace, RawAuthExchangeSites sites,
                            std::uint8_t operation, Operation &&body) {
  RawAuthExchange exchange{trace, sites};
  try {
    exchange.start(operation);
    if constexpr (std::is_void_v<std::invoke_result_t<Operation>>) {
      body();
      exchange.returned();
    } else {
      auto result = body();
      exchange.returned();
      return result;
    }
  } catch (const Error &error) {
    exchange.finish(2, &error);
    throw;
  } catch (const std::bad_alloc &) {
    exchange.finish(3);
    throw;
  } catch (const std::length_error &) {
    exchange.finish(4);
    throw;
  } catch (...) {
    exchange.finish(5);
    throw;
  }
}
Value compatibility_binding(std::string account) {
  return Value::object({{"provider", Value{"openai"}},
                        {"account", Value{std::move(account)}},
                        {"revision", Value{Number{"-1"}}},
                        {"kind", Value{"codex_chatgpt"}},
                        {"base_url", Value{"https://chatgpt.com/backend-api/codex"}},
                        {"protocol", Value{"responses"}}});
}
std::optional<RawAuthParent> auth_parent(const OpenAiConfig &config) {
  if (config.raw_context && config.raw_context->parent)
    return RawAuthParent{*config.raw_context->parent, nullptr, nullptr};
  return std::nullopt;
}
} // namespace
Result<Value> openai_request_binding(const OpenAiConfig &config) {
  std::optional<RawAuthTrace> owner;
  try {
    const auto parent = auth_parent(config);
    const bool probe = config.codex_home.empty() && config.codex_executable == "codex";
    owner.emplace(RawAuthEntry{RawAuthAction::resolve_codex,
                               {"openai", {}},
                               parent ? &*parent : nullptr,
                               nullptr,
                               false,
                               probe});
    auto &trace = *owner;
    trace.start();
    const auto child = trace.child();
    if (probe) {
      auto owned = config.owned_auth;
      owned.cancelled = config.cancelled;
      ProviderAuth auth{std::move(owned)};
      if (auth.selected("openai", &child)) {
        const RawAuthParent binding_parent{trace.reference(), nullptr, nullptr};
        auto binding = auth.binding("openai", &binding_parent);
        trace.branch({1607, 1});
        trace.finish(1);
        return Result<Value>::success(std::move(binding));
      }
    }
    const auto before = private_auth_operation(trace, {1617, 1618}, 1,
                                               [&] { return auth_file(config); });
    if (required(before, "auth_mode") != "chatgpt")
      fail(ErrorCode::unsupported);
    const auto *tokens = before.find("tokens");
    if (!tokens)
      fail(ErrorCode::corrupt);
    auto binding = compatibility_binding(required(*tokens, "account_id"));
    trace.branch({1607, 2});
    trace.finish(1);
    return Result<Value>::success(std::move(binding));
  } catch (const Error &e) {
    if (owner)
      owner->finish(2, &e);
    return Result<Value>::failure(e);
  } catch (const std::bad_alloc &) {
    if (owner)
      owner->finish(3);
    return Result<Value>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    if (owner)
      owner->finish(4);
    return Result<Value>::failure({ErrorCode::capacity});
  } catch (...) {
    if (owner)
      owner->finish(5);
    throw;
  }
}
Result<OpenAiLogin> codex_login(const OpenAiConfig &config, bool refresh,
                                const Value *expected_binding) {
  std::optional<RawAuthTrace> owner;
  try {
    const bool probe = config.codex_home.empty() && config.codex_executable == "codex";
    const auto parent = auth_parent(config);
    owner.emplace(RawAuthEntry{RawAuthAction::resolve_codex,
                               {"openai", {}},
                               parent ? &*parent : nullptr,
                               nullptr,
                               expected_binding != nullptr,
                               probe});
    auto &trace = *owner;
    trace.start();
    trace.boolean(2, refresh);
    const auto child_parent = trace.child();
    if (config.codex_home.empty() && config.codex_executable == "codex") {
      auto owned = config.owned_auth;
      owned.cancelled = config.cancelled;
      ProviderAuth auth{std::move(owned)};
      if (auth.selected("openai", &child_parent)) {
        const RawAuthParent binding_parent{trace.reference(), nullptr, nullptr};
        auto binding = auth.binding("openai", &binding_parent);
        if (expected_binding && *expected_binding != binding)
          fail(ErrorCode::conflict);
        OpenAiLogin login{OpenAiLogin::Credentials{"", ""}};
        login.owned_auth_ = true;
        login.binding_ = std::move(binding);
        login.resolution_ = trace.reference();
        trace.branch({1607, 1});
        trace.stage(109);
        trace.finish(1);
        return Result<OpenAiLogin>::success(std::move(login));
      }
    }
    // Pin account before requesting refresh, and refuse a concurrent account switch.
    auto before = private_auth_operation(trace, {1617, 1618}, 1,
                                         [&] { return auth_file(config); });
    auto mode = required(before, "auth_mode");
    if (mode != "chatgpt")
      fail(ErrorCode::unsupported);
    const auto *tokens = before.find("tokens");
    if (tokens == nullptr)
      fail(ErrorCode::corrupt);
    const auto account = required(*tokens, "account_id");
    auto binding = compatibility_binding(account);
    if (expected_binding && *expected_binding != binding)
      fail(ErrorCode::conflict);
    Child child;
    child.cancelled = config.cancelled;
    std::vector<std::string> args{
        config.codex_executable,    "app-server", "--listen", "stdio://", "-c",
        "model_provider=\"openai\""};
    child.start(std::move(args), config.codex_home);
    const auto deadline = Clock::now() + std::chrono::seconds{30};
    private_auth_operation(trace, {1615, 1616}, 1, [&] {
      (void)rpc(
          child,
          "{\"id\":\"init\",\"method\":\"initialize\",\"params\":{\"clientInfo\":{"
          "\"name\":\"blackbird\",\"title\":\"Blackbird\",\"version\":\"0.1.0\"}}}\n",
          deadline);
    });
    private_auth_operation(trace, {1615, 1616}, 2, [&] {
      child.send("{\"method\":\"initialized\",\"params\":{}}\n", deadline);
    });
    auto status = private_auth_operation(trace, {1615, 1616}, 3, [&] {
      return rpc(child,
                 refresh ? "{\"id\":\"auth\",\"method\":\"getAuthStatus\",\"params\":{"
                           "\"includeToken\":true,\"refreshToken\":true}}\n"
                         : "{\"id\":\"auth\",\"method\":\"getAuthStatus\",\"params\":{"
                           "\"includeToken\":true,\"refreshToken\":false}}\n",
                 deadline);
    });
    if (required(status, "authMethod") != "chatgpt")
      fail(ErrorCode::unsupported);
    auto access = required(status, "authToken");
    auto latest = private_auth_operation(trace, {1617, 1618}, 2,
                                         [&] { return auth_file(config); });
    const auto *current = latest.find("tokens");
    if (required(latest, "auth_mode") != "chatgpt" || current == nullptr ||
        required(*current, "account_id") != account ||
        required(*current, "access_token") != access)
      fail(ErrorCode::conflict);
    if (!header_safe(access) || !header_safe(account))
      fail(ErrorCode::corrupt);
    OpenAiLogin login{OpenAiLogin::Credentials{std::move(access), account}};
    login.binding_ = std::move(binding);
    login.resolution_ = trace.reference();
    trace.branch({1607, 2});
    trace.stage(109);
    trace.finish(1);
    return Result<OpenAiLogin>::success(std::move(login));
  } catch (const Error &e) {
    if (owner)
      owner->finish(2, &e);
    return Result<OpenAiLogin>::failure(e);
  } catch (const std::bad_alloc &) {
    if (owner)
      owner->finish(3);
    return Result<OpenAiLogin>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    if (owner)
      owner->finish(4);
    return Result<OpenAiLogin>::failure({ErrorCode::capacity});
  } catch (...) {
    if (owner)
      owner->finish(5);
    throw;
  }
}
Result<std::string> openai_http(const OpenAiConfig &config, const OpenAiLogin &login,
                                std::string_view route, const Value *request) {
  std::optional<RawProviderTrace> trace;
  std::optional<RawProviderPhase> adapter;
  std::optional<RawProviderAcquisition> acquisition;
  RawAdapterContext call =
      config.raw_context ? *config.raw_context : RawAdapterContext{};
  if (config.raw_owner)
    call.owner = config.raw_owner;
  std::optional<RawWaitScope> wait_scope;
  try {
    const auto deadline = Clock::now() + std::chrono::seconds{config.timeout_seconds};
    if (config.timeout_seconds >= 1 && config.timeout_seconds <= 3600) {
      auto control = raw_wait_control();
      control.deadline = deadline;
      wait_scope.emplace(control);
    }
    adapter.emplace(provider_adapter_extra);
    adapter->start(5, &call);
    if (login.owned_auth_) {
      auto owned = config.owned_auth;
      owned.curl_executable = config.curl_executable;
      owned.cancelled = config.cancelled;
      ProviderAuth auth{std::move(owned)};
      if (route != "responses")
        throw Error{ErrorCode::unsupported};
      const auto response =
          auth.request("openai", route,
                       request ? export_provider_request(*request) : Value::object({}),
                       config.timeout_seconds, config.response_observer,
                       &login.binding_, call.owner, &call);
      adapter->finish(1);
      return Result<std::string>::success(response.body);
    }
    if (config.timeout_seconds <= 0 || config.timeout_seconds > 3600) {
      trace.emplace(call.owner, config.timeout_seconds, &login.resolution_);
      fail(ErrorCode::invalid_range);
    }
    trace.emplace(call.owner, config.timeout_seconds, &login.resolution_);
    adapter->link(trace->reference());
    if (call.receipt && trace->reference().ordinal)
      call.receipt->request = trace->reference();
    if (route != "responses" && route != "models?client_version=0.160.0")
      fail(ErrorCode::invalid_range);
    const std::string target_url =
        "https://chatgpt.com/backend-api/codex/" + std::string{route};
    std::string encoded_body;
    std::optional<RawCapacityClaim> body_credit;
    if (request != nullptr) {
      if (raw_enabled())
        body_credit.emplace(own_value(raw_claim_data(
            ValueLimits{}.bytes + raw_envelope_size + 24 + 3 * sizeof(std::size_t) +
            sizeof("POST") - 1 + target_url.size())));
      auto encoded = dump_json(export_provider_request(*request));
      if (!encoded.has_value())
        throw encoded.error();
      encoded_body = std::move(encoded).value();
    }
    trace->body(request ? "POST" : "GET", target_url, encoded_body,
                body_credit ? &*body_credit : nullptr);
    body_credit.reset();
    std::string input = "url = " + config_quote(target_url) + "\n";
    input +=
        "header = " + config_quote("Authorization: Bearer " + login.access_token_) +
        "\n";
    input +=
        "header = " + config_quote("ChatGPT-Account-ID: " + login.account_id_) + "\n";
    input += "header = \"Content-Type: application/json\"\nheader = \"originator: "
             "blackbird\"\n";
    if (request)
      input += "data-binary = " + config_quote(encoded_body) + "\n";
    Child child;
    child.deadline_error = {ErrorCode::provider_transport, 28};
    child.raw_capture(trace->reference());
    child.output_observer = config.response_observer;
    child.original_capture = call.originals;
    child.cancelled = config.cancelled;
    // --disable is first: do not load curlrc; no redirects, retries or verbose secret
    // output.
    child.start({config.curl_executable, "--disable", "--silent", "--no-buffer",
                 "--max-time", std::to_string(config.timeout_seconds), "--write-out",
                 "\n%{http_code}", "--config", "-"});
    child.send(input, deadline);
    child.close_input();
    int curl_exit = 0;
    acquisition.emplace(adapter->split(provider_native_acquisition_credit));
    acquisition->start(trace->reference(), child.raw_reference());
    if (call.receipt && acquisition->reference().ordinal)
      call.receipt->response = acquisition->reference();
    auto output = child.collect(deadline, config.response_limit, &curl_exit);
    acquisition->finish(1);
    if (curl_exit != 0)
      fail(ErrorCode::provider_transport, curl_exit);
    const auto separator = output.rfind('\n');
    if (separator == std::string::npos || output.size() - separator != 4)
      fail(ErrorCode::corrupt);
    int status = 0;
    for (std::size_t i = separator + 1; i < output.size(); ++i) {
      if (output[i] < '0' || output[i] > '9')
        fail(ErrorCode::corrupt);
      status = status * 10 + output[i] - '0';
    }
    trace->http(child.raw_reference(), status);
    if (status < 200 || status >= 300)
      fail(ErrorCode::external_unknown, status);
    output.resize(separator);
    trace->finish(1);
    adapter->finish(1);
    return Result<std::string>::success(std::move(output));
  } catch (const Error &e) {
    const Error returned = e.code == ErrorCode::interrupted && e.detail == ETIMEDOUT
                               ? Error{ErrorCode::provider_transport, 28}
                               : e;
    if (acquisition)
      acquisition->finish(2, &e);
    if (adapter)
      adapter->finish(2, &returned);
    if (trace)
      trace->finish(2, 1, returned);
    return Result<std::string>::failure(returned);
  } catch (const std::bad_alloc &) {
    if (acquisition)
      acquisition->finish(3);
    if (adapter)
      adapter->finish(3);
    if (trace)
      trace->finish(3, 1, {ErrorCode::allocation});
    return Result<std::string>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    if (acquisition)
      acquisition->finish(4);
    if (adapter)
      adapter->finish(4);
    if (trace)
      trace->finish(4, 1, {ErrorCode::capacity});
    return Result<std::string>::failure({ErrorCode::capacity});
  } catch (...) {
    if (acquisition)
      acquisition->finish(5);
    if (adapter)
      adapter->finish(5);
    if (trace)
      trace->finish(5, 1);
    throw;
  }
}
std::vector<TextPreview> ResponsePreview::feed(std::string_view bytes,
                                               const RawAdapterContext *context) {
  RawProviderPhase phase{provider_link_credit};
  return phase.run([&] {
    phase.start(7, context);
    if (context && context->receipt && context->receipt->response)
      phase.link(*context->receipt->response);
    phase.stage(60);
    if (bytes.size() > 16 * 1024 * 1024 - bytes_)
      fail(ErrorCode::capacity);
    bytes_ += bytes.size();
    phase.stage(61);
    pending_.append(bytes);
    std::vector<TextPreview> output;
    std::size_t consumed = 0;
    while (true) {
      const auto end = pending_.find('\n', consumed);
      if (end == std::string::npos)
        break;
      auto line = std::string_view{pending_}.substr(consumed, end - consumed);
      consumed = end + 1;
      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
      if (line.empty()) {
        if (!data_.empty()) {
          if (data_ == "[DONE]") {
            data_.clear();
            continue;
          }
          RawProviderPhase event_phase{provider_scalar_credit};
          RawAdapterContext event_context{nullptr, phase.reference(), nullptr, nullptr};
          event_phase.run([&] {
            event_phase.start(14, &event_context);
            event_phase.stage(62);
            auto packet = parse_json(data_);
            if (!packet.has_value())
              throw packet.error();
            event_phase.stage(63);
            const auto *type = packet.value().find("type");
            if (type && std::holds_alternative<std::string>(type->value()) &&
                type->string() == "response.output_text.delta") {
              const auto *item = packet.value().find("item_id");
              if (item && std::holds_alternative<std::string>(item->value()) &&
                  !item->string().empty()) {
                event_phase.stage(64);
                event_phase.scalar(64, 0);
                output.push_back({item->string(), required(packet.value(), "delta")});
              } else
                event_phase.scalar(63, 0);
            } else
              event_phase.scalar(63, 0);
            data_.clear();
          });
        }
      } else if (line.starts_with("data:")) {
        line.remove_prefix(5);
        if (!line.empty() && line.front() == ' ')
          line.remove_prefix(1);
        if (!data_.empty())
          data_ += '\n';
        data_.append(line);
      }
    }
    phase.stage(66);
    pending_.erase(0, consumed);
    phase.stage(67);
    return output;
  });
}
Result<Value> completed_response(std::string_view stream, ValueLimits limits,
                                 const RawAdapterContext *context) {
  std::optional<RawProviderPhase> phase;
  try {
    phase.emplace(provider_link_credit);
    phase->start(6, context);
    if (context && context->receipt && context->receipt->response)
      phase->link(*context->receipt->response);
    phase->stage(40);
    if (stream.size() > limits.bytes)
      fail(ErrorCode::capacity);
    std::string data;
    std::optional<Value> completed;
    std::size_t total_nodes = 0;
    std::vector<std::pair<std::size_t, Value>> items;
    auto event = [&] {
      if (data.empty())
        return;
      RawProviderPhase event_phase{3 * provider_scalar_credit +
                                   3 * sizeof(std::size_t) + sizeof(std::errc)};
      RawAdapterContext event_context{nullptr, phase->reference(), nullptr, nullptr};
      event_phase.run([&] {
        event_phase.start(13, &event_context);
        event_phase.stage(41);
        auto parsed = parse_json(data, limits);
        data.clear();
        if (!parsed.has_value())
          throw parsed.error();
        event_phase.stage(42);
        ++total_nodes;
        const std::array count_fields{detail::raw_native_field(total_nodes),
                                      detail::raw_native_field(limits.nodes)};
        event_phase.scalar(42, 6, count_fields);
        if (total_nodes > limits.nodes)
          fail(ErrorCode::capacity);
        event_phase.stage(43);
        const auto type = required(parsed.value(), "type");
        if (type == "error" || type == "response.failed") {
          event_phase.stage(44);
          event_phase.scalar(44, 0);
          fail(ErrorCode::external_unknown);
        }
        if (type == "response.incomplete") {
          event_phase.stage(45);
          event_phase.scalar(45, 0);
          fail(ErrorCode::incomplete);
        }
        if (completed.has_value()) {
          event_phase.stage(46);
          event_phase.scalar(46, 0);
          fail(ErrorCode::corrupt);
        }
        if (type == "response.output_item.done") {
          event_phase.stage(47);
          event_phase.scalar(47, 0);
          const auto *index = parsed.value().find("output_index");
          const auto *item = parsed.value().find("item");
          if (index == nullptr || item == nullptr ||
              !std::holds_alternative<Number>(index->value()))
            fail(ErrorCode::corrupt);
          const auto &number = index->number().text();
          std::size_t ordinal = 0;
          const auto converted =
              std::from_chars(number.data(), number.data() + number.size(), ordinal);
          const std::array converted_fields{detail::raw_native_field(ordinal),
                                            detail::raw_native_field(converted.ec)};
          event_phase.scalar(47, 5, converted_fields);
          if (converted.ec != std::errc{} ||
              converted.ptr != number.data() + number.size() || ordinal >= limits.nodes)
            fail(ErrorCode::corrupt);
          for (const auto &old : items)
            if (old.first == ordinal)
              fail(ErrorCode::conflict);
          items.emplace_back(ordinal, *item);
        }
        if (type == "response.completed") {
          event_phase.stage(48);
          event_phase.scalar(48, 0);
          if (completed.has_value())
            fail(ErrorCode::corrupt);
          const auto *response = parsed.value().find("response");
          if (response == nullptr || required(*response, "status") != "completed")
            fail(ErrorCode::corrupt);
          const auto *output = response->find("output");
          if (output == nullptr ||
              !std::holds_alternative<Value::Array>(output->value()))
            fail(ErrorCode::corrupt);
          completed = *response;
        } else if (type != "response.output_item.done")
          event_phase.scalar(43, 0);
      });
    };
    while (!stream.empty()) {
      auto end = stream.find('\n');
      if (end == std::string_view::npos)
        fail(ErrorCode::incomplete);
      auto line = stream.substr(0, end);
      stream.remove_prefix(end + 1);
      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
      if (line.empty())
        event();
      else if (line.starts_with("data:")) {
        line.remove_prefix(5);
        if (!line.empty() && line.front() == ' ')
          line.remove_prefix(1);
        if (!data.empty())
          data += '\n';
        data += line;
      }
    }
    phase->stage(49);
    if (!data.empty() || !completed.has_value())
      fail(ErrorCode::incomplete);
    // Codex's subscription stream can leave completed.output empty. Final item
    // events then carry the authoritative opaque outputs, not text deltas.
    std::sort(items.begin(), items.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    auto &fields = completed->object();
    for (auto &[name, field] : fields)
      if (name == "output") {
        auto &output = field.array();
        if (output.empty()) {
          for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].first != i)
              fail(ErrorCode::incomplete);
            output.push_back(std::move(items[i].second));
          }
        } else
          for (const auto &[ordinal, item] : items) {
            if (ordinal >= output.size() || output[ordinal] != item)
              fail(ErrorCode::conflict);
          }
      }
    phase->stage(50);
    phase->finish(1);
    return Result<Value>::success(std::move(*completed));
  } catch (const Error &e) {
    if (phase)
      phase->finish(2, &e);
    return Result<Value>::failure(e);
  } catch (const std::bad_alloc &) {
    if (phase)
      phase->finish(3);
    return Result<Value>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    if (phase)
      phase->finish(4);
    return Result<Value>::failure({ErrorCode::capacity});
  } catch (...) {
    if (phase)
      phase->finish(5);
    throw;
  }
}
} // namespace blackbird
