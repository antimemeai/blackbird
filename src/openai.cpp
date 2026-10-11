#include "blackbird/openai.hpp"
#include "blackbird/json.hpp"
#include "blackbird/provider_auth.hpp"
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
} // namespace
Result<OpenAiLogin> codex_login(const OpenAiConfig &config, bool refresh) {
  try {
    if (config.codex_home.empty() && config.codex_executable == "codex") {
      ProviderAuth auth;
      if (auth.selected("openai")) {
        OpenAiLogin login{"", ""};
        login.owned_auth_ = true;
        return Result<OpenAiLogin>::success(std::move(login));
      }
    }
    // Pin account before requesting refresh, and refuse a concurrent account switch.
    auto before = auth_file(config);
    auto mode = required(before, "auth_mode");
    if (mode != "chatgpt")
      fail(ErrorCode::unsupported);
    const auto *tokens = before.find("tokens");
    if (tokens == nullptr)
      fail(ErrorCode::corrupt);
    const auto account = required(*tokens, "account_id");
    Child child;
    child.cancelled = config.cancelled;
    std::vector<std::string> args{
        config.codex_executable,    "app-server", "--listen", "stdio://", "-c",
        "model_provider=\"openai\""};
    child.start(std::move(args), config.codex_home);
    const auto deadline = Clock::now() + std::chrono::seconds{30};
    (void)rpc(
        child,
        "{\"id\":\"init\",\"method\":\"initialize\",\"params\":{\"clientInfo\":{"
        "\"name\":\"blackbird\",\"title\":\"Blackbird\",\"version\":\"0.1.0\"}}}\n",
        deadline);
    child.send("{\"method\":\"initialized\",\"params\":{}}\n", deadline);
    auto status =
        rpc(child,
            refresh ? "{\"id\":\"auth\",\"method\":\"getAuthStatus\",\"params\":{"
                      "\"includeToken\":true,\"refreshToken\":true}}\n"
                    : "{\"id\":\"auth\",\"method\":\"getAuthStatus\",\"params\":{"
                      "\"includeToken\":true,\"refreshToken\":false}}\n",
            deadline);
    if (required(status, "authMethod") != "chatgpt")
      fail(ErrorCode::unsupported);
    auto access = required(status, "authToken");
    auto latest = auth_file(config);
    const auto *current = latest.find("tokens");
    if (required(latest, "auth_mode") != "chatgpt" || current == nullptr ||
        required(*current, "account_id") != account ||
        required(*current, "access_token") != access)
      fail(ErrorCode::conflict);
    if (!header_safe(access) || !header_safe(account))
      fail(ErrorCode::corrupt);
    return Result<OpenAiLogin>::success(OpenAiLogin{std::move(access), account});
  } catch (const Error &e) {
    return Result<OpenAiLogin>::failure(e);
  } catch (const std::bad_alloc &) {
    return Result<OpenAiLogin>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Result<OpenAiLogin>::failure({ErrorCode::capacity});
  }
}
Result<std::string> openai_http(const OpenAiConfig &config, const OpenAiLogin &login,
                                std::string_view route, const Value *request) {
  std::optional<RawProviderTrace> trace;
  std::optional<RawWaitScope> wait_scope;
  try {
    if (login.owned_auth_) {
      ProviderAuthConfig owned;
      owned.curl_executable = config.curl_executable;
      owned.cancelled = config.cancelled;
      ProviderAuth auth{std::move(owned)};
      if (route != "responses")
        throw Error{ErrorCode::unsupported};
      const auto response = auth.request(
          "openai", route, request ? *request : Value::object({}),
          config.timeout_seconds, config.response_observer, nullptr, config.raw_owner);
      return Result<std::string>::success(response.body);
    }
    if (config.timeout_seconds <= 0 || config.timeout_seconds > 3600) {
      trace.emplace(config.raw_owner, config.timeout_seconds);
      fail(ErrorCode::invalid_range);
    }
    const auto deadline = Clock::now() + std::chrono::seconds{config.timeout_seconds};
    auto control = raw_wait_control();
    control.deadline = deadline;
    wait_scope.emplace(control);
    trace.emplace(config.raw_owner, config.timeout_seconds);
    if (route != "responses" && route != "models?client_version=0.160.0")
      fail(ErrorCode::invalid_range);
    const std::string target_url =
        "https://chatgpt.com/backend-api/codex/" + std::string{route};
    std::string input = "url = " + config_quote(target_url) + "\n";
    input +=
        "header = " + config_quote("Authorization: Bearer " + login.access_token_) +
        "\n";
    input +=
        "header = " + config_quote("ChatGPT-Account-ID: " + login.account_id_) + "\n";
    input += "header = \"Content-Type: application/json\"\nheader = \"originator: "
             "blackbird\"\n";
    std::string encoded_body;
    if (request != nullptr) {
      auto encoded = dump_json(export_provider_request(*request));
      if (!encoded.has_value())
        throw encoded.error();
      input += "data-binary = " + config_quote(encoded.value()) + "\n";
      encoded_body = std::move(encoded).value();
    }
    trace->body(request ? "POST" : "GET", target_url, encoded_body);
    Child child;
    child.deadline_error = {ErrorCode::provider_transport, 28};
    child.raw_capture(trace->reference());
    child.output_observer = config.response_observer;
    child.cancelled = config.cancelled;
    // --disable is first: do not load curlrc; no redirects, retries or verbose secret
    // output.
    child.start({config.curl_executable, "--disable", "--silent", "--no-buffer",
                 "--max-time", std::to_string(config.timeout_seconds), "--write-out",
                 "\n%{http_code}", "--config", "-"});
    child.send(input, deadline);
    child.close_input();
    int curl_exit = 0;
    auto output = child.collect(deadline, config.response_limit, &curl_exit);
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
    return Result<std::string>::success(std::move(output));
  } catch (const Error &e) {
    const Error returned = e.code == ErrorCode::interrupted && e.detail == ETIMEDOUT
                               ? Error{ErrorCode::provider_transport, 28}
                               : e;
    if (trace)
      trace->finish(2, 1, returned);
    return Result<std::string>::failure(returned);
  } catch (const std::bad_alloc &) {
    if (trace)
      trace->finish(3, 1, {ErrorCode::allocation});
    return Result<std::string>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    if (trace)
      trace->finish(4, 1, {ErrorCode::capacity});
    return Result<std::string>::failure({ErrorCode::capacity});
  } catch (...) {
    if (trace)
      trace->finish(5, 1);
    throw;
  }
}
std::vector<TextPreview> ResponsePreview::feed(std::string_view bytes) {
  if (bytes.size() > 16 * 1024 * 1024 - bytes_)
    fail(ErrorCode::capacity);
  bytes_ += bytes.size();
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
        auto packet = parse_json(data_);
        if (!packet.has_value())
          throw packet.error();
        const auto *type = packet.value().find("type");
        if (type && std::holds_alternative<std::string>(type->value()) &&
            type->string() == "response.output_text.delta") {
          const auto *item = packet.value().find("item_id");
          if (item && std::holds_alternative<std::string>(item->value()) &&
              !item->string().empty())
            output.push_back({item->string(), required(packet.value(), "delta")});
        }
        data_.clear();
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
  pending_.erase(0, consumed);
  return output;
}
Result<Value> completed_response(std::string_view stream, ValueLimits limits) {
  try {
    if (stream.size() > limits.bytes)
      fail(ErrorCode::capacity);
    std::string data;
    std::optional<Value> completed;
    std::size_t total_nodes = 0;
    std::vector<std::pair<std::size_t, Value>> items;
    auto event = [&] {
      if (data.empty())
        return;
      auto parsed = parse_json(data, limits);
      data.clear();
      if (!parsed.has_value())
        throw parsed.error();
      if (++total_nodes > limits.nodes)
        fail(ErrorCode::capacity);
      const auto type = required(parsed.value(), "type");
      if (type == "error" || type == "response.failed")
        fail(ErrorCode::external_unknown);
      if (type == "response.incomplete")
        fail(ErrorCode::incomplete);
      if (completed.has_value())
        fail(ErrorCode::corrupt);
      if (type == "response.output_item.done") {
        const auto *index = parsed.value().find("output_index");
        const auto *item = parsed.value().find("item");
        if (index == nullptr || item == nullptr ||
            !std::holds_alternative<Number>(index->value()))
          fail(ErrorCode::corrupt);
        const auto &number = index->number().text();
        std::size_t ordinal = 0;
        const auto converted =
            std::from_chars(number.data(), number.data() + number.size(), ordinal);
        if (converted.ec != std::errc{} ||
            converted.ptr != number.data() + number.size() || ordinal >= limits.nodes)
          fail(ErrorCode::corrupt);
        for (const auto &old : items)
          if (old.first == ordinal)
            fail(ErrorCode::conflict);
        items.emplace_back(ordinal, *item);
      }
      if (type == "response.completed") {
        if (completed.has_value())
          fail(ErrorCode::corrupt);
        const auto *response = parsed.value().find("response");
        if (response == nullptr || required(*response, "status") != "completed")
          fail(ErrorCode::corrupt);
        const auto *output = response->find("output");
        if (output == nullptr || !std::holds_alternative<Value::Array>(output->value()))
          fail(ErrorCode::corrupt);
        completed = *response;
      }
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
    return Result<Value>::success(std::move(*completed));
  } catch (const Error &e) {
    return Result<Value>::failure(e);
  } catch (const std::bad_alloc &) {
    return Result<Value>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Result<Value>::failure({ErrorCode::capacity});
  }
}
} // namespace blackbird
