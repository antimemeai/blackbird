#include "blackbird/provider_auth.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/tools.hpp"
#include "native_process.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace blackbird {
namespace {
const Value &required_field(const Value &v, std::string_view k) {
  const auto *p = v.find(k);
  if (!p)
    throw Error{ErrorCode::corrupt};
  return *p;
}
std::mutex store_mutex;
constexpr std::size_t store_limit = 1024 * 1024;
std::string str(const Value &v, std::string_view k, std::string fallback = {}) {
  const auto *p = v.find(k);
  if (!p)
    return fallback;
  if (!std::holds_alternative<std::string>(p->value()))
    throw Error{ErrorCode::corrupt};
  return p->string();
}
std::int64_t number(const Value &v, std::string_view k, std::int64_t fallback = 0) {
  const auto *p = v.find(k);
  if (!p)
    return fallback;
  if (!std::holds_alternative<Number>(p->value()))
    throw Error{ErrorCode::corrupt};
  const auto &s = p->number().text();
  std::int64_t n = 0;
  const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
  if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
    throw Error{ErrorCode::corrupt};
  return n;
}
Value integer(std::int64_t n) { return Value{Number{n}}; }
void put(Value &v, std::string_view k, Value val) {
  for (auto &[name, value] : v.object())
    if (name == k) {
      value = std::move(val);
      return;
    }
  v.object().emplace_back(k, std::move(val));
}
void identifier(std::string_view s) {
  if (s.empty() || s.size() > 64 || !std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
      }))
    throw Error{ErrorCode::invalid_range};
}
void secret(std::string_view s) {
  if (s.empty() || s.size() > 32768 ||
      s.find_first_of("\r\n\0", 0, 3) != std::string_view::npos)
    throw Error{ErrorCode::invalid_range};
}
void https_url(std::string_view s) {
  if (!s.starts_with("https://") || s.size() > 2048 || s.size() <= 8 ||
      s.find_first_of(" \t\r\n\0?#\\", 0, 9) != std::string_view::npos ||
      s.find('@') != std::string_view::npos)
    throw Error{ErrorCode::invalid_range};
}
std::string quote(std::string_view s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\')
      out += '\\';
    else if (c == '\n') {
      out += "\\n";
      continue;
    } else if (c == '\r') {
      out += "\\r";
      continue;
    }
    out += c;
  }
  return out + '"';
}
std::string form(std::string_view s) {
  std::string out;
  constexpr char hex[] = "0123456789ABCDEF";
  for (char raw : s) {
    const auto c = static_cast<unsigned char>(raw);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~')
      out += static_cast<char>(c);
    else {
      out += '%';
      out += hex[c >> 4U];
      out += hex[c & 15U];
    }
  }
  return out;
}
std::int64_t now(const ProviderAuthConfig &c) {
  return c.now ? c.now()
               : std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
}
struct Fd {
  int value = -1;
  explicit Fd(int n) : value(n) {}
  ~Fd() {
    if (value >= 0)
      (void)::close(value);
  }
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;
};
void check_file(int fd, bool directory = false) {
  struct stat st{};
  if (fd < 0 || ::fstat(fd, &st) != 0)
    throw Error{ErrorCode::io, errno};
  if (st.st_uid != ::geteuid() || (st.st_mode & 0077) != 0 ||
      (directory ? !S_ISDIR(st.st_mode) : (!S_ISREG(st.st_mode) || st.st_nlink != 1)))
    throw Error{ErrorCode::conflict};
}
class Store {
public:
  std::unique_lock<std::mutex> thread_lock{store_mutex};
  Fd dir{-1}, lock{-1};
  Value data = Value::object({{"version", integer(1)},
                              {"providers", Value::object({})},
                              {"accounts", Value{Value::Array{}}},
                              {"active", Value::object({})},
                              {"selection_revision", Value::object({})},
                              {"pending", Value{Value::Array{}}}});
  Store(const ProviderAuthConfig &c, bool create) {
    const auto p = c.directory;
    if (create) {
      std::filesystem::create_directories(p.parent_path());
      if (::mkdir(p.c_str(), 0700) != 0 && errno != EEXIST)
        throw Error{ErrorCode::io, errno};
    }
    dir.value = ::open(p.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dir.value < 0 && errno == ENOENT && !create)
      return;
    check_file(dir.value, true);
    lock.value =
        ::openat(dir.value, "lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    check_file(lock.value);
    const auto until = detail::Clock::now() + std::chrono::seconds{30};
    while (::flock(lock.value, LOCK_EX | LOCK_NB) != 0) {
      if (errno != EWOULDBLOCK && errno != EAGAIN)
        throw Error{ErrorCode::io, errno};
      if (c.cancelled && c.cancelled())
        throw Error{ErrorCode::interrupted};
      if (detail::Clock::now() >= until)
        throw Error{ErrorCode::busy};
      std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    Fd file{::openat(dir.value, "credentials.bbm", O_RDONLY | O_NOFOLLOW | O_CLOEXEC)};
    if (file.value < 0 && errno == ENOENT)
      return;
    check_file(file.value);
    std::string bytes;
    char chunk[4096];
    for (;;) {
      const auto n = ::read(file.value, chunk, sizeof(chunk));
      if (n < 0 && errno == EINTR)
        continue;
      if (n < 0)
        throw Error{ErrorCode::io, errno};
      if (n == 0)
        break;
      if (bytes.size() + static_cast<std::size_t>(n) > store_limit)
        throw Error{ErrorCode::capacity};
      bytes.append(chunk, static_cast<std::size_t>(n));
    }
    data = unwrap(decode_packet_string(bytes, {store_limit, 32768, 32}));
    if (!data.find("selection_revision"))
      data.object().emplace_back("selection_revision", Value::object({}));
    if (number(data, "version") != 1)
      throw Error{ErrorCode::unsupported};
    for (const auto k : {"providers", "active"})
      (void)required_field(data, k).object();
    for (const auto k : {"accounts", "pending"})
      (void)required_field(data, k).array();
  }
  Value &field(std::string_view name) {
    for (auto &[k, v] : data.object())
      if (k == name)
        return v;
    throw Error{ErrorCode::corrupt};
  }
  Value *account(std::string_view provider, std::string_view account) {
    for (auto &v : field("accounts").array())
      if (str(v, "provider") == provider && str(v, "account") == account)
        return &v;
    return nullptr;
  }
  void save() {
    auto bytes = unwrap(encode_packet_string(data));
    if (bytes.size() > store_limit)
      throw Error{ErrorCode::capacity};
    std::array<unsigned char, 16> random{};
    if (::getentropy(random.data(), random.size()) != 0)
      throw Error{ErrorCode::io, errno};
    std::string name = ".credentials-";
    constexpr char hex[] = "0123456789abcdef";
    for (auto c : random) {
      name += hex[c >> 4U];
      name += hex[c & 15U];
    }
    Fd file{::openat(dir.value, name.c_str(),
                     O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
    if (file.value < 0)
      throw Error{ErrorCode::io, errno};
    try {
      std::size_t done = 0;
      while (done < bytes.size()) {
        const auto n = ::write(file.value, bytes.data() + done, bytes.size() - done);
        if (n < 0 && errno == EINTR)
          continue;
        if (n <= 0)
          throw Error{ErrorCode::io, errno};
        done += static_cast<std::size_t>(n);
      }
      if (::fsync(file.value) != 0)
        throw Error{ErrorCode::io, errno};
      if (::renameat(dir.value, name.c_str(), dir.value, "credentials.bbm") != 0)
        throw Error{ErrorCode::io, errno};
      if (::fsync(dir.value) != 0)
        throw Error{ErrorCode::io, errno};
    } catch (...) {
      (void)::unlinkat(dir.value, name.c_str(), 0);
      throw;
    }
  }
};
Value builtin(std::string_view id) {
  const auto make = [&](const char *base, const char *protocol, const char *env,
                        const char *flow = "none", const char *client = "",
                        const char *device = "", const char *token = "",
                        const char *scope = "") {
    return Value::object({{"id", Value{std::string{id}}},
                          {"base_url", Value{base}},
                          {"protocol", Value{protocol}},
                          {"environment", Value{env}},
                          {"login", Value{flow}},
                          {"client_id", Value{client}},
                          {"device_url", Value{device}},
                          {"token_url", Value{token}},
                          {"scope", Value{scope}}});
  };
  if (id == "openai")
    return make("https://api.openai.com/v1", "responses", "OPENAI_API_KEY", "browser",
                "dynamic_agent_client", "",
                "https://auth.openai.com/api/accounts/oauth/token",
                "openid profile email offline_access resource.invoke "
                "chatgpt.tokens.use.direct");
  if (id == "anthropic")
    return make("https://api.anthropic.com/v1", "messages", "ANTHROPIC_API_KEY",
                "browser", "9d1c250a-e61b-44d9-88ed-5944d1962f5e", "",
                "https://platform.claude.com/v1/oauth/token",
                "org:create_api_key user:profile user:inference");
  if (id == "kimi")
    return make("https://api.kimi.com/coding/v1", "messages", "KIMI_API_KEY", "device",
                "17e5f671-d194-4dfb-9706-5516cb48c098",
                "https://auth.kimi.com/api/oauth/device_authorization",
                "https://auth.kimi.com/api/oauth/token");
  if (id == "mimo")
    return make("https://api.xiaomimimo.com/v1", "chat", "MIMO_API_KEY");
  if (id == "grok")
    return make("https://api.x.ai/v1", "chat", "XAI_API_KEY", "device",
                "b1a00492-073a-47ea-816f-4c329264a828",
                "https://auth.x.ai/oauth2/device/code",
                "https://auth.x.ai/oauth2/token",
                "openid profile email offline_access grok-cli:access api:access");
  if (id == "moonshot")
    return make("https://api.moonshot.ai/v1", "chat", "MOONSHOT_API_KEY");
  if (id == "openrouter")
    return make("https://openrouter.ai/api/v1", "chat", "OPENROUTER_API_KEY");
  throw Error{ErrorCode::unsupported};
}
Value descriptor(Store &s, std::string_view id) {
  identifier(id);
  try {
    return builtin(id);
  } catch (const Error &e) {
    if (e.code != ErrorCode::unsupported)
      throw;
  }
  const auto *p = s.field("providers").find(id);
  if (!p)
    throw Error{ErrorCode::unsupported};
  return *p;
}
ProviderHttpResponse http(const ProviderAuthConfig &c, const ProviderHttpRequest &r) {
  return c.http ? c.http(r) : provider_http(r, c);
}
Value token_record(const Value &tokens, std::int64_t clock,
                   std::string_view prior_refresh = {}) {
  const auto access = str(tokens, "access_token"),
             refresh = str(tokens, "refresh_token", std::string{prior_refresh});
  secret(access);
  secret(refresh);
  if (str(tokens, "token_type", "Bearer") != "Bearer" &&
      str(tokens, "token_type") != "bearer")
    throw Error{ErrorCode::corrupt};
  const auto expiry = number(tokens, "expires_in");
  if (expiry <= 0 || expiry > 31 * 86400)
    throw Error{ErrorCode::corrupt};
  return Value::object({{"kind", Value{"oauth"}},
                        {"access", Value{access}},
                        {"refresh", Value{refresh}},
                        {"expires_at", integer(clock + expiry)},
                        {"scope", Value{str(tokens, "scope")}},
                        {"state", Value{"ready"}}});
}
void save_account(Store &s, std::string_view provider, std::string_view account,
                  Value value) {
  identifier(provider);
  identifier(account);
  put(value, "provider", Value{std::string{provider}});
  put(value, "account", Value{std::string{account}});
  if (auto *p = s.account(provider, account)) {
    put(value, "revision", integer(number(*p, "revision") + 1));
    *p = std::move(value);
  } else {
    if (s.field("accounts").array().size() >= 128)
      throw Error{ErrorCode::capacity};
    put(value, "revision", integer(1));
    s.field("accounts").array().push_back(std::move(value));
  }
  put(s.field("active"), provider, Value{std::string{account}});
  put(s.field("selection_revision"), provider,
      integer(number(s.field("selection_revision"), provider) + 1));
}
std::vector<std::pair<std::string, std::string>>
oauth_headers(std::string_view provider) {
  std::vector<std::pair<std::string, std::string>> out{
      {"Content-Type", "application/x-www-form-urlencoded"},
      {"Accept", "application/json"}};
  if (provider == "kimi") {
    out.emplace_back("X-Msh-Platform", "kimi_cli");
    out.emplace_back("X-Msh-Version", "blackbird-0.1");
  }
  return out;
}
} // namespace
void require_provider_success(int status) {
  if (status == 401 || status == 403)
    throw Error{ErrorCode::provider_auth, status};
  if (status == 429)
    throw Error{ErrorCode::provider_rate_limit, status};
  if (status < 200 || status >= 300)
    throw Error{ErrorCode::external_unknown, status};
}
ProviderHttpResponse
provider_http(const ProviderHttpRequest &r, const ProviderAuthConfig &c,
              const std::function<void(std::string_view)> &observer,
              const RawProviderOwner *raw_owner, bool application) {
  std::optional<RawProviderTrace> trace;
  std::optional<RawWaitScope> wait_scope;
  std::optional<detail::Clock::time_point> initial_deadline;
  try {
    if (application && r.timeout_seconds >= 1 && r.timeout_seconds <= 3600) {
      initial_deadline = detail::Clock::now() + std::chrono::seconds{r.timeout_seconds};
      auto control = raw_wait_control();
      control.deadline = initial_deadline;
      wait_scope.emplace(control);
    }
    if (application)
      trace.emplace(raw_owner, r.timeout_seconds);
    https_url(r.url);
    if (r.timeout_seconds < 1 || r.timeout_seconds > 3600 ||
        r.body.size() > 16 * 1024 * 1024 || (r.method != "GET" && r.method != "POST"))
      throw Error{ErrorCode::invalid_range};
    std::string input =
        "url = " + quote(r.url) + "\nrequest = " + quote(r.method) + "\n";
    for (const auto &[k, v] : r.headers) {
      identifier(k);
      secret(v);
      input += "header = " + quote(k + ": " + v) + "\n";
    }
    if (r.method == "POST")
      input += "data-binary = " + quote(r.body) + "\n";
    if (trace)
      trace->body(r.method, r.url,
                  r.method == "POST" ? std::string_view{r.body} : std::string_view{});
    detail::Child child;
    child.cancelled = c.cancelled;
    child.deadline_error = {ErrorCode::provider_transport, 28};
    if (trace)
      child.raw_capture(trace->reference());
    child.output_observer = observer;
    child.start({c.curl_executable, "--disable", "--silent", "--no-buffer", "--proto",
                 "=https", "--max-time", std::to_string(r.timeout_seconds),
                 "--write-out", "\n%{http_code}", "--config", "-"});
    const auto deadline =
        initial_deadline
            ? *initial_deadline
            : detail::Clock::now() + std::chrono::seconds{r.timeout_seconds};
    child.send(input, deadline);
    child.close_input();
    int code = 0;
    auto output = child.collect(deadline, 16 * 1024 * 1024, &code);
    if (code != 0)
      throw Error{ErrorCode::provider_transport, code};
    const auto split = output.rfind('\n');
    if (split == std::string::npos || output.size() - split != 4)
      throw Error{ErrorCode::corrupt};
    int status = 0;
    const auto n = std::from_chars(output.data() + split + 1,
                                   output.data() + output.size(), status);
    if (n.ec != std::errc{} || n.ptr != output.data() + output.size() || status < 100 ||
        status > 599)
      throw Error{ErrorCode::corrupt};
    if (trace)
      trace->http(child.raw_reference(), status);
    output.resize(split);
    if (trace)
      trace->finish(1);
    return {status, std::move(output)};
  } catch (const Error &error) {
    const Error returned =
        error.code == ErrorCode::interrupted && error.detail == ETIMEDOUT
            ? Error{ErrorCode::provider_transport, 28}
            : error;
    if (trace)
      trace->finish(2, 1, returned);
    throw returned;
  } catch (const std::bad_alloc &) {
    if (trace)
      trace->finish(3, 1, {ErrorCode::allocation});
    throw;
  } catch (const std::length_error &) {
    if (trace)
      trace->finish(4, 1, {ErrorCode::capacity});
    throw;
  } catch (...) {
    if (trace)
      trace->finish(5, 1);
    throw;
  }
}
ProviderAuth::ProviderAuth(ProviderAuthConfig config) : config_(std::move(config)) {
  if (config_.directory.empty()) {
    if (const auto *p = std::getenv("BLACKBIRD_AUTH_HOME"); p && *p)
      config_.directory = p;
    else {
      const auto *h = std::getenv("HOME");
      if (!h)
        throw Error{ErrorCode::invalid_range};
      config_.directory = std::filesystem::path{h} / ".local/share/blackbird/auth";
    }
  }
}
std::filesystem::path ProviderAuth::directory() const { return config_.directory; }
Value ProviderAuth::descriptor(std::string_view p) const {
  Store s{config_, false};
  return blackbird::descriptor(s, p);
}
Value ProviderAuth::catalog() const {
  Store s{config_, false};
  Value::Array out;
  for (const auto id :
       {"kimi", "mimo", "openai", "anthropic", "grok", "moonshot", "openrouter"})
    out.push_back(builtin(id));
  for (const auto &[k, v] : s.field("providers").object()) {
    (void)k;
    out.push_back(v);
  }
  return Value::object({{"providers", Value{std::move(out)}}});
}
Value ProviderAuth::status() const {
  Store s{config_, false};
  Value::Array out;
  for (const auto &a : s.field("accounts").array()) {
    auto state = str(a, "state", "ready");
    if (state == "ready" && str(a, "kind") == "oauth" &&
        number(a, "expires_at") <= now(config_))
      state = "expired";
    out.push_back(Value::object(
        {{"provider", Value{str(a, "provider")}},
         {"account", Value{str(a, "account")}},
         {"kind", Value{str(a, "kind")}},
         {"state", Value{state}},
         {"expires_at", a.find("expires_at") ? *a.find("expires_at") : Value{}},
         {"active",
          Value{str(s.field("active"), str(a, "provider")) == str(a, "account")}}}));
  }
  return Value::object(
      {{"accounts", Value{std::move(out)}},
       {"environment",
        Value{"available only without an owned selection; not probed"}}});
}
Value ProviderAuth::registration(std::string_view p, std::string_view a) const {
  identifier(p);
  identifier(a);
  Store s{config_, false};
  auto *v = s.account(p, a);
  Value out = Value::object(
      {{"revision", integer(v ? number(*v, "revision") : -1)},
       {"selection_revision", integer(number(s.field("selection_revision"), p))}});
  if (v) {
    for (const auto k : {"client_id", "metadata"})
      if (const auto *x = v->find(k))
        put(out, k, *x);
    if (auto *x = out.find("metadata")) {
      auto m = *x;
      std::erase_if(m.object(), [](const auto &kv) { return kv.first == "id_token"; });
      put(out, "metadata", std::move(m));
    }
  }
  return out;
}
Value ProviderAuth::binding(std::string_view p) const {
  Store s{config_, false};
  const auto d = blackbird::descriptor(s, p);
  const auto active = str(s.field("active"), p);
  const auto *a = s.account(p, active);
  return Value::object({{"provider", Value{std::string{p}}},
                        {"account", Value{active}},
                        {"revision", integer(a ? number(*a, "revision") : -1)},
                        {"kind", Value{a                ? str(*a, "kind")
                                       : active.empty() ? "environment"
                                                        : "logged_out"}},
                        {"base_url", Value{str(d, "base_url")}},
                        {"protocol", Value{str(d, "protocol")}}});
}
bool ProviderAuth::selected(std::string_view p) const {
  Store s{config_, false};
  return !str(s.field("active"), p).empty();
}
void ProviderAuth::set_key(std::string_view p, std::string_view a, std::string key) {
  secret(key);
  Store s{config_, true};
  (void)blackbird::descriptor(s, p);
  save_account(s, p, a,
               Value::object({{"kind", Value{"api_key"}},
                              {"access", Value{std::move(key)}},
                              {"state", Value{"ready"}}}));
  s.save();
}
void ProviderAuth::use(std::string_view p, std::string_view a) {
  identifier(p);
  identifier(a);
  Store s{config_, true};
  if (!s.account(p, a))
    throw Error{ErrorCode::invalid_range};
  put(s.field("active"), p, Value{std::string{a}});
  put(s.field("selection_revision"), p,
      integer(number(s.field("selection_revision"), p) + 1));
  s.save();
}
void ProviderAuth::logout(std::string_view p, std::string_view a) {
  identifier(p);
  identifier(a);
  Store s{config_, true};
  if (!s.account(p, a)) {
    if (s.field("accounts").array().size() >= 128)
      throw Error{ErrorCode::capacity};
    s.field("accounts")
        .array()
        .push_back(Value::object({{"provider", Value{std::string{p}}},
                                  {"account", Value{std::string{a}}},
                                  {"kind", Value{"none"}},
                                  {"state", Value{"logged_out"}},
                                  {"revision", integer(0)}}));
  }
  if (auto *record = s.account(p, a)) {
    put(*record, "access", Value{});
    put(*record, "refresh", Value{});
    put(*record, "state", Value{"logged_out"});
    put(*record, "revision", integer(number(*record, "revision") + 1));
    if (auto *metadata = record->find("metadata")) {
      auto cleaned = *metadata;
      std::erase_if(cleaned.object(),
                    [](const auto &v) { return v.first == "id_token"; });
      put(*record, "metadata", std::move(cleaned));
    }
  }
  auto &pending = s.field("pending").array();
  std::erase_if(pending, [&](const Value &v) {
    return str(v, "provider") == p && str(v, "account") == a;
  });
  put(s.field("selection_revision"), p,
      integer(number(s.field("selection_revision"), p) + 1));
  // Keep the selected account label. Its logged_out state prevents fallback;
  // a magic account label could accidentally select another stored account.
  s.save();
}
void ProviderAuth::add_provider(const std::string &id, std::string base,
                                const std::string &protocol, const std::string &env) {
  identifier(id);
  identifier(env);
  https_url(base);
  if (protocol != "responses" && protocol != "chat" && protocol != "messages")
    throw Error{ErrorCode::invalid_range};
  try {
    (void)builtin(id);
    throw Error{ErrorCode::conflict};
  } catch (const Error &e) {
    if (e.code != ErrorCode::unsupported)
      throw;
  }
  Store s{config_, true};
  if (s.field("providers").find(id))
    throw Error{ErrorCode::conflict};
  if (s.field("providers").object().size() >= 32)
    throw Error{ErrorCode::capacity};
  while (base.ends_with('/'))
    base.pop_back();
  put(s.field("providers"), id,
      Value::object({{"id", Value{id}},
                     {"base_url", Value{base}},
                     {"protocol", Value{protocol}},
                     {"environment", Value{env}},
                     {"login", Value{"none"}}}));
  s.save();
}
void ProviderAuth::save_oauth(std::string_view p, std::string_view a,
                              const Value &tokens, std::string client,
                              std::string token_url, Value metadata,
                              OAuthExpected expected) {
  identifier(p);
  identifier(a);
  secret(client);
  https_url(token_url);
  Store s{config_, true};
  const auto d = blackbird::descriptor(s, p);
  if (token_url != str(d, "token_url"))
    throw Error{ErrorCode::conflict};
  if (expected.selection_revision >= 0 &&
      number(s.field("selection_revision"), p) != expected.selection_revision)
    throw Error{ErrorCode::conflict};
  const auto *prior = s.account(p, a);
  if (expected.account_revision != -2 &&
      (prior ? number(*prior, "revision") : -1) != expected.account_revision)
    throw Error{ErrorCode::conflict};
  auto record = token_record(tokens, now(config_));
  put(record, "client_id", Value{std::move(client)});
  put(record, "token_url", Value{std::move(token_url)});
  put(record, "metadata", std::move(metadata));
  save_account(s, p, a, std::move(record));
  std::erase_if(s.field("pending").array(), [&](const Value &v) {
    return str(v, "provider") == p && str(v, "account") == a;
  });
  s.save();
}
Value ProviderAuth::device_begin(std::string_view p, std::string_view a) {
  identifier(p);
  identifier(a);
  Store s{config_, true};
  const auto d = blackbird::descriptor(s, p);
  if (str(d, "login") != "device")
    throw Error{ErrorCode::unsupported};
  ProviderHttpRequest r{str(d, "device_url"), "POST",
                        "client_id=" + form(str(d, "client_id")), oauth_headers(p), 30};
  if (!str(d, "scope").empty())
    r.body += "&scope=" + form(str(d, "scope"));
  const auto reply = http(config_, r);
  require_provider_success(reply.status);
  auto v = unwrap(parse_json(reply.body));
  secret(str(v, "device_code"));
  secret(str(v, "user_code"));
  https_url(str(v, "verification_uri"));
  auto expiry = number(v, "expires_in", 900), interval = number(v, "interval", 5);
  if (expiry < 1 || expiry > 1800 || interval < 1 || interval > 60)
    throw Error{ErrorCode::corrupt};
  const auto complete = str(v, "verification_uri_complete");
  if (!complete.empty() && !complete.starts_with(str(v, "verification_uri")))
    throw Error{ErrorCode::corrupt};
  auto record = Value::object(
      {{"provider", Value{std::string{p}}},
       {"account", Value{std::string{a}}},
       {"device_code", Value{str(v, "device_code")}},
       {"selection_revision", integer(number(s.field("selection_revision"), p))},
       {"expires_at", integer(now(config_) + expiry)},
       {"interval", integer(interval)},
       {"next_poll", integer(now(config_) + interval)}});
  auto &pending = s.field("pending").array();
  std::erase_if(pending, [&](const Value &x) {
    return str(x, "provider") == p && str(x, "account") == a;
  });
  if (pending.size() >= 16)
    throw Error{ErrorCode::capacity};
  pending.push_back(std::move(record));
  s.save();
  return Value::object({{"verification_uri", Value{str(v, "verification_uri")}},
                        {"verification_uri_complete", Value{complete}},
                        {"user_code", Value{str(v, "user_code")}},
                        {"expires_in", integer(expiry)},
                        {"interval", integer(interval)}});
}
Value ProviderAuth::device_poll(std::string_view p, std::string_view a) {
  identifier(p);
  identifier(a);
  Store s{config_, true};
  const auto d = blackbird::descriptor(s, p);
  auto &pending = s.field("pending").array();
  auto it = std::find_if(pending.begin(), pending.end(), [&](const Value &v) {
    return str(v, "provider") == p && str(v, "account") == a;
  });
  if (it == pending.end())
    throw Error{ErrorCode::invalid_range};
  const auto clock = now(config_);
  if (number(*it, "selection_revision") != number(s.field("selection_revision"), p)) {
    pending.erase(it);
    s.save();
    throw Error{ErrorCode::conflict};
  }
  if (number(*it, "expires_at") <= clock) {
    pending.erase(it);
    s.save();
    return Value::object({{"state", Value{"expired"}}});
  }
  if (number(*it, "next_poll") > clock)
    return Value::object({{"state", Value{"pending"}},
                          {"wait_seconds", integer(number(*it, "next_poll") - clock)}});
  ProviderHttpRequest r{
      str(d, "token_url"), "POST",
      "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code&client_id=" +
          form(str(d, "client_id")) + "&device_code=" + form(str(*it, "device_code")),
      oauth_headers(p), 30};
  put(*it, "next_poll", integer(clock + number(*it, "interval")));
  s.save();
  const auto reply = http(config_, r);
  if (reply.status == 429 || reply.status >= 500) {
    put(*it, "next_poll", integer(now(config_) + 30));
    s.save();
    return Value::object({{"state", Value{"pending"}}, {"wait_seconds", integer(30)}});
  }
  auto tokens = unwrap(parse_json(reply.body));
  const auto error = str(tokens, "error");
  if (error == "authorization_pending" || error == "slow_down") {
    auto interval = number(*it, "interval");
    if (error == "slow_down")
      interval += 5;
    interval = std::min<std::int64_t>(interval, 300);
    put(*it, "interval", integer(interval));
    put(*it, "next_poll", integer(now(config_) + interval));
    s.save();
    return Value::object(
        {{"state", Value{"pending"}}, {"wait_seconds", integer(interval)}});
  }
  if (!error.empty()) {
    pending.erase(it);
    s.save();
    return Value::object({{"state", Value{"denied"}}});
  }
  require_provider_success(reply.status);
  auto record = token_record(tokens, now(config_));
  put(record, "client_id", Value{str(d, "client_id")});
  put(record, "token_url", Value{str(d, "token_url")});
  save_account(s, p, a, std::move(record));
  pending.erase(it);
  s.save();
  return Value::object({{"state", Value{"ready"}}});
}
ProviderHttpResponse ProviderAuth::request(
    std::string_view p, std::string_view route, const Value &body, int timeout_seconds,
    const std::function<void(std::string_view)> &observer,
    const Value *expected_binding, const RawProviderOwner *raw_owner) {
  if (route != "responses" && route != "chat/completions" && route != "messages" &&
      route != "models")
    throw Error{ErrorCode::invalid_range};
  std::string token, kind, base;
  Value metadata;
  {
    Store s{config_, false};
    const auto d = blackbird::descriptor(s, p);
    base = str(d, "base_url");
    const auto active = str(s.field("active"), p);
    if (expected_binding) {
      const auto *record = s.account(p, active);
      if (str(*expected_binding, "provider") != p ||
          str(*expected_binding, "account") != active ||
          number(*expected_binding, "revision", -1) !=
              (record ? number(*record, "revision") : -1) ||
          str(*expected_binding, "base_url") != base ||
          str(*expected_binding, "protocol") != str(d, "protocol"))
        throw Error{ErrorCode::conflict};
    }
    if (!active.empty()) {
      auto *a = s.account(p, active);
      if (!a)
        throw Error{ErrorCode::provider_auth};
      if (str(*a, "state", "ready") != "ready")
        throw Error{ErrorCode::provider_auth};
      kind = str(*a, "kind");
      if (kind == "oauth" && number(*a, "expires_at") <= now(config_) + 60) {
        if (str(*a, "token_url") != str(d, "token_url"))
          throw Error{ErrorCode::conflict};
        const auto retry = number(*a, "refresh_after");
        if (retry > now(config_))
          throw Error{ErrorCode::provider_rate_limit, -429};
        ProviderHttpRequest r{
            str(*a, "token_url"), "POST",
            "grant_type=refresh_token&client_id=" + form(str(*a, "client_id")) +
                "&refresh_token=" + form(str(*a, "refresh")),
            oauth_headers(p), 30};
        if (p == "openai")
          r.body += "&resource=https%3A%2F%2Fapi.openai.com%2Fv1";
        if (p == "anthropic") {
          r.headers = {{"Content-Type", "application/json"}};
          r.body = unwrap(
              dump_json(Value::object({{"grant_type", Value{"refresh_token"}},
                                       {"client_id", Value{str(*a, "client_id")}},
                                       {"refresh_token", Value{str(*a, "refresh")}}})));
        }
        ProviderHttpResponse reply;
        try {
          reply = http(config_, r);
        } catch (...) {
          put(*a, "state", Value{"refresh_unknown"});
          s.save();
          throw;
        }
        if (reply.status == 429 || reply.status >= 500) {
          put(*a, "refresh_after", integer(now(config_) + 60));
          s.save();
          require_provider_success(reply.status);
        }
        if (reply.status < 200 || reply.status >= 300) {
          put(*a, "state", Value{"reauthorize"});
          s.save();
          throw Error{ErrorCode::provider_auth, -reply.status};
        }
        try {
          const auto tokens = unwrap(parse_json(reply.body));
          auto updated = token_record(tokens, now(config_), str(*a, "refresh"));
          if (!tokens.find("scope"))
            put(updated, "scope", Value{str(*a, "scope")});
          for (const auto k : {"provider", "account", "revision", "client_id",
                               "token_url", "metadata"})
            if (const auto *v = a->find(k))
              put(updated, k, *v);
          *a = std::move(updated);
          s.save();
        } catch (...) {
          put(*a, "state", Value{"refresh_unknown"});
          s.save();
          throw;
        }
      }
      token = str(*a, "access");
      if (const auto *v = a->find("metadata"))
        metadata = *v;
    } else {
      kind = "api_key";
      const auto *env = std::getenv(str(d, "environment").c_str());
      if (!env || !*env)
        throw Error{ErrorCode::provider_auth};
      token = env;
    }
  }
  secret(token);
  ProviderHttpRequest r{base + "/" + std::string{route},
                        route == "models" ? "GET" : "POST",
                        unwrap(dump_json(body)),
                        {{"Content-Type", "application/json"}},
                        timeout_seconds};
  if ((p == "anthropic" || p == "kimi") && route == "messages") {
    r.headers.emplace_back("anthropic-version", "2023-06-01");
    if (kind == "api_key")
      r.headers.emplace_back("x-api-key", token);
    else
      r.headers.emplace_back("Authorization", "Bearer " + token);
    if (p == "anthropic" && kind == "oauth") {
      r.headers.emplace_back("anthropic-beta", "oauth-2025-04-20,claude-code-20250219");
      r.headers.emplace_back("User-Agent", "claude-cli/2.1.163 (external, cli)");
      r.headers.emplace_back("x-app", "cli");
    }
    if (p == "kimi")
      r.headers.emplace_back("User-Agent", "KimiCLI/blackbird-0.1");
  } else
    r.headers.emplace_back("Authorization", "Bearer " + token);
  (void)metadata;
  auto result = config_.http ? config_.http(r)
                             : provider_http(r, config_, observer, raw_owner, true);
  if (config_.http && observer)
    observer(result.body);
  require_provider_success(result.status);
  return result;
}
} // namespace blackbird
