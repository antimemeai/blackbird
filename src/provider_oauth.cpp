#include "blackbird/json.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/raw_auth.hpp"
#include "blackbird/tools.hpp"
#include "native_process.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <charconv>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

namespace blackbird {
namespace {
volatile std::sig_atomic_t auth_interrupted = 0;
volatile std::sig_atomic_t auth_signal_writer = -1;
extern "C" void auth_signal(int) {
  const int saved = errno;
  auth_interrupted = 1;
  const int fd = auth_signal_writer;
  if (fd >= 0) {
    const char byte = 1;
    ssize_t result;
    do {
      result = ::write(fd, &byte, 1);
    } while (result < 0 && errno == EINTR);
  }
  errno = saved;
}
struct AuthSignals {
  struct sigaction before_int{}, before_term{};
  std::array<int, 2> pipe{-1, -1};
  AuthSignals() {
    sigset_t selected{}, previous{};
    sigemptyset(&selected);
    sigaddset(&selected, SIGINT);
    sigaddset(&selected, SIGTERM);
    if (::sigprocmask(SIG_BLOCK, &selected, &previous) != 0)
      throw Error{ErrorCode::io, errno};
    bool installed = false;
    try {
      {
        const std::lock_guard lock{detail::process_spawn_mutex};
        if (::pipe(pipe.data()) != 0)
          throw Error{ErrorCode::io, errno};
        for (const int fd : pipe) {
          if (::fcntl(fd, F_SETFD, FD_CLOEXEC) != 0)
            throw Error{ErrorCode::io, errno};
          const int flags = ::fcntl(fd, F_GETFL);
          if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
            throw Error{ErrorCode::io, errno};
        }
      }
      auth_interrupted = 0;
      auth_signal_writer = pipe[1];
      struct sigaction action{};
      action.sa_handler = auth_signal;
      action.sa_mask = selected;
      if (::sigaction(SIGINT, &action, &before_int) != 0)
        throw Error{ErrorCode::io, errno};
      installed = true;
      if (::sigaction(SIGTERM, &action, &before_term) != 0)
        throw Error{ErrorCode::io, errno};
    } catch (...) {
      if (installed)
        (void)::sigaction(SIGINT, &before_int, nullptr);
      auth_signal_writer = -1;
      for (int fd : pipe)
        if (fd >= 0)
          (void)::close(fd);
      (void)::sigprocmask(SIG_SETMASK, &previous, nullptr);
      throw;
    }
    if (::sigprocmask(SIG_SETMASK, &previous, nullptr) != 0) {
      const int error = errno;
      (void)::sigaction(SIGINT, &before_int, nullptr);
      (void)::sigaction(SIGTERM, &before_term, nullptr);
      auth_signal_writer = -1;
      for (int fd : pipe)
        (void)::close(fd);
      throw Error{ErrorCode::io, error};
    }
  }
  ~AuthSignals() {
    const int saved = errno;
    sigset_t selected{}, previous{};
    sigemptyset(&selected);
    sigaddset(&selected, SIGINT);
    sigaddset(&selected, SIGTERM);
    (void)::sigprocmask(SIG_BLOCK, &selected, &previous);
    (void)::sigaction(SIGINT, &before_int, nullptr);
    (void)::sigaction(SIGTERM, &before_term, nullptr);
    auth_signal_writer = -1;
    for (int fd : pipe)
      (void)::close(fd);
    (void)::sigprocmask(SIG_SETMASK, &previous, nullptr);
    errno = saved;
  }
  RawWaitControl control() const noexcept { return {{pipe[0], -1}, {}}; }
};
std::string text(const Value &v, std::string_view k, std::string fallback = {}) {
  const auto *p = v.find(k);
  if (!p)
    return fallback;
  return p->string();
}
std::int64_t number(const Value &v, std::string_view k) {
  const auto *p = v.find(k);
  if (!p)
    throw Error{ErrorCode::corrupt};
  const auto &s = p->number().text();
  std::int64_t n = 0;
  auto r = std::from_chars(s.data(), s.data() + s.size(), n);
  if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
    throw Error{ErrorCode::corrupt};
  return n;
}
std::string b64(std::string_view bytes) {
  constexpr char abc[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  std::uint32_t bits = 0;
  unsigned count = 0;
  for (char raw : bytes) {
    bits = (bits << 8U) | static_cast<unsigned char>(raw);
    count += 8;
    while (count >= 6) {
      count -= 6;
      out += abc[(bits >> count) & 63U];
    }
  }
  if (count)
    out += abc[(bits << (6U - count)) & 63U];
  return out;
}
std::string unb64(std::string_view s) {
  if (s.size() > 65536)
    throw Error{ErrorCode::capacity};
  constexpr std::string_view abc =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  std::uint32_t bits = 0;
  unsigned count = 0;
  for (char c : s) {
    const auto at = abc.find(c);
    if (at == std::string_view::npos)
      throw Error{ErrorCode::corrupt};
    bits = (bits << 6U) | static_cast<std::uint32_t>(at);
    count += 6;
    if (count >= 8) {
      count -= 8;
      out += static_cast<char>((bits >> count) & 255U);
    }
  }
  if (count >= 6 || (count && ((bits & ((1U << count) - 1U)) != 0)))
    throw Error{ErrorCode::corrupt};
  return out;
}
std::string secure_nonce() {
  std::array<char, 32> bytes{};
  if (::getentropy(bytes.data(), bytes.size()) != 0)
    throw Error{ErrorCode::io, errno};
  return b64({bytes.data(), bytes.size()});
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
std::string decode(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+')
      out += ' ';
    else if (s[i] == '%') {
      if (i + 2 >= s.size())
        throw Error{ErrorCode::corrupt};
      unsigned value = 0;
      auto r = std::from_chars(s.data() + i + 1, s.data() + i + 3, value, 16);
      if (r.ec != std::errc{} || r.ptr != s.data() + i + 3 || value == 0 ||
          value < 32 || value == 127)
        throw Error{ErrorCode::corrupt};
      out += static_cast<char>(value);
      i += 2;
    } else
      out += s[i];
  }
  return out;
}
std::map<std::string, std::string> query(std::string_view s) {
  std::map<std::string, std::string> out;
  while (!s.empty()) {
    const auto end = s.find('&');
    const auto part = s.substr(0, end);
    const auto eq = part.find('=');
    if (eq == std::string_view::npos ||
        !out.emplace(decode(part.substr(0, eq)), decode(part.substr(eq + 1))).second)
      throw Error{ErrorCode::corrupt};
    if (end == std::string_view::npos)
      break;
    s.remove_prefix(end + 1);
  }
  return out;
}
ProviderHttpResponse http(const ProviderAuthConfig &c, const ProviderHttpRequest &r,
                          RawAuthTrace &trace, std::uint8_t operation) {
  RawAuthExchange exchange{trace, {1609, 1610}};
  try {
    exchange.start(operation);
    auto result = provider_http(r, c);
    exchange.returned(&result.status);
    return result;
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
template <class Operation>
auto auth_callback(RawAuthTrace &trace, std::uint8_t operation, Operation &&body) {
  trace.callback(operation, 1);
  try {
    if constexpr (std::is_void_v<std::invoke_result_t<Operation>>) {
      body();
      trace.callback(operation, 2);
    } else {
      auto result = body();
      trace.callback(operation, 2);
      return result;
    }
  } catch (...) {
    trace.callback(operation, 3);
    throw;
  }
}

std::string process(const ProviderAuthConfig &c, std::vector<std::string> args,
                    std::string_view input, int *exit = nullptr) {
  detail::Child child;
  child.cancelled = c.cancelled;
  child.separate_error = true;
  child.start(std::move(args));
  auto deadline = detail::Clock::now() + std::chrono::seconds{15};
  child.send(input, deadline);
  child.close_input();
  int code = 0;
  auto out = child.collect(deadline, 1024 * 1024, &code);
  if (exit)
    *exit = code;
  else if (code != 0)
    throw Error{ErrorCode::corrupt};
  return out;
}
std::string der(unsigned char tag, std::string_view data) {
  std::string out(1, static_cast<char>(tag));
  auto n = data.size();
  if (n < 128)
    out += static_cast<char>(n);
  else {
    std::string bytes;
    while (n) {
      bytes.insert(bytes.begin(), static_cast<char>(n & 255U));
      n >>= 8U;
    }
    out += static_cast<char>(128U + bytes.size());
    out += bytes;
  }
  out += data;
  return out;
}
std::string der_integer(std::string s) {
  while (s.size() > 1 && s.front() == 0)
    s.erase(s.begin());
  if (s.empty())
    throw Error{ErrorCode::corrupt};
  if ((static_cast<unsigned char>(s.front()) & 128U) != 0)
    s.insert(s.begin(), 0);
  return der(2, s);
}
struct Temp {
  std::filesystem::path path;
  Temp() {
    char p[] = "/tmp/blackbird-oauth-XXXXXX";
    auto *v = ::mkdtemp(p);
    if (!v)
      throw Error{ErrorCode::io, errno};
    path = v;
  }
  ~Temp() {
    std::error_code e;
    std::filesystem::remove_all(path, e);
  }
  void file(const char *name, std::string_view bytes) {
    const auto p = path / name;
    const int fd =
        ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
      throw Error{ErrorCode::io, errno};
    std::size_t done = 0;
    while (done < bytes.size()) {
      const auto n = ::write(fd, bytes.data() + done, bytes.size() - done);
      if (n < 0 && errno == EINTR)
        continue;
      if (n <= 0) {
        const auto e = errno;
        (void)::close(fd);
        throw Error{ErrorCode::io, e};
      }
      done += static_cast<std::size_t>(n);
    }
    if (::close(fd) != 0)
      throw Error{ErrorCode::io, errno};
  }
};
struct Socket {
  int fd = -1;
  explicit Socket(int n) : fd(n) {}
  ~Socket() {
    if (fd >= 0)
      (void)::close(fd);
  }
  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;
};
class Callback {
public:
  Socket listener{-1};
  std::string uri;
  Callback() {
    {
      const std::lock_guard spawn_lock{detail::process_spawn_mutex};
      listener.fd = ::socket(AF_INET, SOCK_STREAM, 0);
      if (listener.fd < 0 || ::fcntl(listener.fd, F_SETFD, FD_CLOEXEC) < 0 ||
          ::fcntl(listener.fd, F_SETFL, O_NONBLOCK) < 0)
        throw Error{ErrorCode::io, errno};
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(listener.fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) !=
            0 ||
        ::listen(listener.fd, 4) != 0)
      throw Error{ErrorCode::io, errno};
    socklen_t size = sizeof(addr);
    if (::getsockname(listener.fd, reinterpret_cast<sockaddr *>(&addr), &size) != 0)
      throw Error{ErrorCode::io, errno};
    uri = "http://127.0.0.1:" + std::to_string(ntohs(addr.sin_port)) + "/auth/callback";
  }
  std::map<std::string, std::string> wait(const ProviderAuthConfig &c,
                                          std::string_view state) {
    const auto until = detail::Clock::now() + std::chrono::seconds{360};
    while (detail::Clock::now() < until) {
      if (c.cancelled && c.cancelled())
        throw Error{ErrorCode::interrupted};
      pollfd p{listener.fd, POLLIN, 0};
      const int ready = ::poll(&p, 1, 100);
      if (ready < 0 && errno == EINTR)
        continue;
      if (ready < 0)
        throw Error{ErrorCode::io, errno};
      if (!ready)
        continue;
      Socket client{-1};
      {
        const std::lock_guard spawn_lock{detail::process_spawn_mutex};
        client.fd = ::accept(listener.fd, nullptr, nullptr);
        if (client.fd >= 0 && (::fcntl(client.fd, F_SETFD, FD_CLOEXEC) < 0 ||
                               ::fcntl(client.fd, F_SETFL, O_NONBLOCK) < 0))
          throw Error{ErrorCode::io, errno};
      }
      if (client.fd < 0)
        continue;
      std::string request;
      const auto read_until =
          std::min(until, detail::Clock::now() + std::chrono::seconds{3});
      while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192 &&
             detail::Clock::now() < read_until) {
        pollfd q{client.fd, POLLIN, 0};
        if (::poll(&q, 1, 100) <= 0)
          continue;
        char bytes[1024];
        const auto n = ::read(client.fd, bytes, sizeof(bytes));
        if (n <= 0)
          break;
        request.append(bytes, static_cast<std::size_t>(n));
      }
      bool accepted = false;
      std::map<std::string, std::string> values;
      try {
        const auto end = request.find(" HTTP/1.");
        if (!request.starts_with("GET /auth/callback?") || end == std::string::npos)
          throw Error{ErrorCode::corrupt};
        values = query(std::string_view{request}.substr(19, end - 19));
        accepted = values["state"] == state;
      } catch (const Error &) {
      }
      const std::string response =
          accepted ? "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: "
                     "close\r\n\r\nReturn to Blackbird to finish sign-in.\n"
                   : "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nInvalid "
                     "sign-in callback.\n";
#ifdef MSG_NOSIGNAL
      (void)::send(client.fd, response.data(), response.size(), MSG_NOSIGNAL);
#else
      const int one = 1;
      (void)::setsockopt(client.fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
      (void)::send(client.fd, response.data(), response.size(), 0);
#endif
      if (accepted)
        return values;
    }
    throw Error{ErrorCode::io, ETIMEDOUT};
  }
};
std::string input_secret(std::string_view prompt = "Secret input (hidden): ") {
  struct Echo {
    termios before{};
    bool changed = false;
    Echo() {
      if (::isatty(STDIN_FILENO)) {
        if (::tcgetattr(STDIN_FILENO, &before) != 0)
          throw Error{ErrorCode::io, errno};
        auto after = before;
        after.c_lflag &= static_cast<tcflag_t>(~ECHO);
        if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &after) != 0)
          throw Error{ErrorCode::io, errno};
        changed = true;
      }
    }
    ~Echo() {
      if (changed) {
        (void)::tcflush(STDIN_FILENO, TCIFLUSH);
        (void)::tcsetattr(STDIN_FILENO, TCSANOW, &before);
      }
    }
  } echo;
  std::cerr << prompt << std::flush;
  std::string s;
  bool finished = false;
  while (!finished) {
    if (auth_interrupted)
      throw Error{ErrorCode::interrupted};
    pollfd input{STDIN_FILENO, POLLIN, 0};
    const int ready = ::poll(&input, 1, 100);
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready < 0)
      throw Error{ErrorCode::io, errno};
    if (!ready)
      continue;
    char bytes[4096];
    const auto got = ::read(STDIN_FILENO, bytes, sizeof(bytes));
    if (got < 0 && errno == EINTR)
      continue;
    if (got < 0)
      throw Error{ErrorCode::io, errno};
    if (got == 0)
      break;
    for (ssize_t i = 0; i < got; ++i) {
      if (bytes[i] == '\n') {
        finished = true;
        break;
      }
      if (s.size() >= 32768)
        throw Error{ErrorCode::capacity};
      s += bytes[i];
    }
  }
  if (s.ends_with('\r'))
    s.pop_back();
  if (echo.changed)
    std::cerr << '\n';
  return s;
}
} // namespace
std::string auth_pkce_challenge(std::string_view verifier,
                                const ProviderAuthConfig &c) {
  auto digest =
      process(c, {c.openssl_executable, "dgst", "-sha256", "-binary"}, verifier);
  if (digest.size() != 32)
    throw Error{ErrorCode::corrupt};
  return b64(digest);
}
Value auth_validate_openai_identity(OpenAIIdentity identity, const Value &jwks,
                                    std::int64_t clock, const ProviderAuthConfig &c) {
  const auto token = identity.token;
  const auto client = identity.client;
  const auto nonce = identity.nonce;
  const auto first = token.find('.'),
             second = token.find('.', first == std::string_view::npos ? 0 : first + 1);
  if (first == std::string_view::npos || second == std::string_view::npos ||
      token.find('.', second + 1) != std::string_view::npos)
    throw Error{ErrorCode::corrupt};
  const auto header = unwrap(parse_json(unb64(token.substr(0, first))));
  if (text(header, "alg") != "RS256" || text(header, "kid").empty())
    throw Error{ErrorCode::provider_auth};
  const Value *key = nullptr;
  const auto *keys = jwks.find("keys");
  if (!keys || keys->array().size() > 32)
    throw Error{ErrorCode::corrupt};
  for (const auto &v : keys->array())
    if (text(v, "kid") == text(header, "kid")) {
      if (key)
        throw Error{ErrorCode::corrupt};
      key = &v;
    }
  if (!key || text(*key, "kty") != "RSA" || text(*key, "use", "sig") != "sig" ||
      text(*key, "alg", "RS256") != "RS256")
    throw Error{ErrorCode::provider_auth};
  const auto n = unb64(text(*key, "n")), e = unb64(text(*key, "e"));
  if (n.size() < 256 || n.size() > 512 || e.empty() || e.size() > 8)
    throw Error{ErrorCode::corrupt};
  const auto rsa = der(0x30, der_integer(n) + der_integer(e));
  const std::string algorithm{
      "\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01\x05\x00", 15};
  Temp temp;
  temp.file("key.der", der(0x30, algorithm + der(3, std::string(1, 0) + rsa)));
  temp.file("signature", unb64(token.substr(second + 1)));
  int exit = 0;
  (void)process(c,
                {c.openssl_executable, "dgst", "-sha256", "-keyform", "DER", "-verify",
                 (temp.path / "key.der").string(), "-signature",
                 (temp.path / "signature").string()},
                token.substr(0, second), &exit);
  if (exit != 0)
    throw Error{ErrorCode::provider_auth};
  auto claims = unwrap(parse_json(unb64(token.substr(first + 1, second - first - 1))));
  const auto *aud = claims.find("aud");
  bool audience = false;
  if (aud && std::holds_alternative<std::string>(aud->value()))
    audience = aud->string() == client;
  else if (aud && std::holds_alternative<Value::Array>(aud->value())) {
    for (const auto &a : aud->array())
      if (a == Value{std::string{client}})
        audience = true;
    if (aud->array().size() > 1 && text(claims, "azp") != client)
      audience = false;
  }
  if (text(claims, "iss") != "https://auth.openai.com" || !audience ||
      text(claims, "nonce") != nonce || text(claims, "sub").empty() ||
      number(claims, "exp") <= clock)
    throw Error{ErrorCode::provider_auth};
  if (claims.find("nbf") && number(claims, "nbf") > clock + 60)
    throw Error{ErrorCode::provider_auth};
  return claims;
}
void ProviderAuth::browser_login(std::string_view p, std::string_view account,
                                 const std::function<void(std::string_view)> &notice,
                                 const std::function<std::string()> &read_secret,
                                 const RawAuthParent *parent) {
  RawAuthTrace trace{RawAuthEntry{RawAuthAction::browser_login, {p, account}, parent}};
  try {
    trace.start();
    const auto child = trace.child();
    const auto d = descriptor(p, &child);
    if (text(d, "login") != "browser" || !notice || !read_secret)
      throw Error{ErrorCode::unsupported};
    const auto registration = this->registration(p, account, &child);
    trace.stage(RawAuthField::browser_revision);
    const auto expected = number(registration, "revision");
    trace.scalar(RawAuthField::browser_revision, expected);
    const auto verifier = secure_nonce(), state = secure_nonce(),
               nonce = secure_nonce();
    auto client = p == "openai" ? text(registration, "client_id", text(d, "client_id"))
                                : text(d, "client_id");
    const bool new_registration = client == "dynamic_agent_client";
    std::string callback, authorize;
    std::unique_ptr<Callback> server;
    if (p == "openai") {
      server = std::make_unique<Callback>();
      callback = server->uri;
      authorize = "https://auth.openai.com/api/accounts/authorize";
    } else {
      callback = "https://platform.claude.com/oauth/code/callback";
      authorize = "https://claude.ai/oauth/authorize";
    }
    std::string challenge;
    RawAuthExchange pkce{trace, {1615, 1616}};
    try {
      pkce.start(4);
      challenge = auth_pkce_challenge(verifier, config_);
      pkce.returned();
    } catch (const Error &error) {
      pkce.finish(2, &error);
      throw;
    } catch (const std::bad_alloc &) {
      pkce.finish(3);
      throw;
    } catch (const std::length_error &) {
      pkce.finish(4);
      throw;
    } catch (...) {
      pkce.finish(5);
      throw;
    }
    std::string url = authorize + "?response_type=code&client_id=" + form(client) +
                      "&redirect_uri=" + form(callback) +
                      "&scope=" + form(text(d, "scope")) + "&state=" + form(state) +
                      "&code_challenge_method=S256&code_challenge=" + challenge;
    Value metadata = Value::object({});
    if (p == "openai") {
      // Stable host identity is separate from account credentials; no provider cache.
      std::filesystem::create_directories(directory().parent_path());
      const auto hostfile = directory().parent_path() / "auth-host-id";
      int fd =
          ::open(hostfile.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
      if (fd < 0)
        throw Error{ErrorCode::io, errno};
      struct Close {
        int fd;
        ~Close() { (void)::close(fd); }
      } close{fd};
      struct stat st{};
      if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != ::geteuid() ||
          (st.st_mode & 0077) != 0 || st.st_nlink != 1)
        throw Error{ErrorCode::conflict};
      if (::flock(fd, LOCK_EX) != 0)
        throw Error{ErrorCode::io, errno};
      std::string host;
      char bytes[128];
      const auto got = ::read(fd, bytes, sizeof(bytes));
      if (got < 0)
        throw Error{ErrorCode::io, errno};
      if (got > 0)
        host.assign(bytes, static_cast<std::size_t>(got));
      else {
        host = secure_nonce();
        if (::write(fd, host.data(), host.size()) !=
                static_cast<ssize_t>(host.size()) ||
            ::fsync(fd) != 0)
          throw Error{ErrorCode::io, errno};
      }
      if (host.size() != 43 ||
          host.find_first_not_of(
              "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") !=
              std::string::npos)
        throw Error{ErrorCode::corrupt};
      if (::flock(fd, LOCK_UN) != 0)
        throw Error{ErrorCode::io, errno};
      url += (new_registration ? "&agent_name_hint=Blackbird&ext_agent_host_id="
                               : "&ext_agent_host_id=") +
             form(host) + "&nonce=" + form(nonce) +
             "&resource=https%3A%2F%2Fapi.openai.com%2Fv1";
      metadata.object().emplace_back("host_id", Value{host});
    }
    auth_callback(trace, 1, [&] {
      notice("Open this sign-in URL in your browser:\n" + url + "\n");
    });
    std::map<std::string, std::string> values;
    if (server)
      values = auth_callback(trace, 2, [&] { return server->wait(config_, state); });
    else {
      auth_callback(trace, 3,
                    [&] { notice("Paste the returned code#state (input hidden): "); });
      const auto input = auth_callback(trace, 4, [&] { return read_secret(); });
      const auto at = input.find('#');
      if (at != std::string::npos) {
        values["code"] = input.substr(0, at);
        values["state"] = input.substr(at + 1);
      } else {
        const auto q = input.find('?');
        values = query(q == std::string::npos ? std::string_view{input}
                                              : std::string_view{input}.substr(q + 1));
      }
    }
    if (values["state"] != state || !values["error"].empty() || values["code"].empty())
      throw Error{ErrorCode::provider_auth};
    if (p == "openai") {
      const auto returned = values["client_id"];
      if (new_registration) {
        client = returned;
        if (client.empty() || client == "dynamic_agent_client" || client.size() > 256 ||
            client.find_first_not_of(
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") !=
                std::string::npos)
          throw Error{ErrorCode::provider_auth};
      } else if (!returned.empty() && returned != client)
        throw Error{ErrorCode::provider_auth};
    }
    ProviderHttpRequest r{text(d, "token_url"),
                          "POST",
                          "grant_type=authorization_code&client_id=" + form(client) +
                              "&code=" + form(values["code"]) + "&code_verifier=" +
                              form(verifier) + "&redirect_uri=" + form(callback),
                          {{"Content-Type", "application/x-www-form-urlencoded"}},
                          30};
    if (p == "openai")
      r.body += "&resource=https%3A%2F%2Fapi.openai.com%2Fv1";
    if (p == "anthropic") {
      r.headers = {{"Content-Type", "application/json"}};
      r.body =
          unwrap(dump_json(Value::object({{"grant_type", Value{"authorization_code"}},
                                          {"client_id", Value{client}},
                                          {"code", Value{values["code"]}},
                                          {"state", Value{state}},
                                          {"redirect_uri", Value{callback}},
                                          {"code_verifier", Value{verifier}}})));
    }
    auto reply = http(config_, r, trace, 4);
    require_provider_success(reply.status);
    auto tokens = unwrap(parse_json(reply.body));
    if (p == "openai") {
      const auto keys =
          http(config_,
               ProviderHttpRequest{
                   "https://auth.openai.com/.well-known/jwks.json", "GET", "", {}, 30},
               trace, 5);
      require_provider_success(keys.status);
      trace.stage(RawAuthField::browser_clock);
      const auto clock = config_.now
                             ? config_.now()
                             : std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
      trace.scalar(RawAuthField::browser_clock, clock);
      RawAuthExchange jwt{trace, {1615, 1616}};
      Value claims;
      try {
        jwt.start(5);
        claims = auth_validate_openai_identity(
            {.token = text(tokens, "id_token"), .client = client, .nonce = nonce},
            unwrap(parse_json(keys.body)), clock, config_);
        jwt.returned();
      } catch (const Error &error) {
        jwt.finish(2, &error);
        throw;
      } catch (const std::bad_alloc &) {
        jwt.finish(3);
        throw;
      } catch (const std::length_error &) {
        jwt.finish(4);
        throw;
      } catch (...) {
        jwt.finish(5);
        throw;
      }
      if (const auto *old = registration.find("metadata");
          old && !text(*old, "sub").empty() && text(*old, "sub") != text(claims, "sub"))
        throw Error{ErrorCode::provider_auth};
      const auto scope = " " + text(tokens, "scope") + " ";
      if (scope.find(" chatgpt.tokens.use.direct ") == std::string::npos ||
          scope.find(" resource.invoke ") == std::string::npos)
        throw Error{ErrorCode::provider_auth};
      for (const auto k : {"sub", "email"})
        if (const auto *v = claims.find(k))
          metadata.object().emplace_back(k, *v);
      metadata.object().emplace_back("id_token", Value{text(tokens, "id_token")});
    }
    save_oauth(p, account, tokens, client, text(d, "token_url"), std::move(metadata),
               {.account_revision = expected}, &child);
    auth_callback(trace, 5, [&] {
      notice("Sign-in saved. Blackbird owns this account's credentials.\n");
    });
    trace.stage(109);
    trace.finish(1);
  } catch (const Error &error) {
    trace.finish(2, &error);
    throw;
  } catch (const std::bad_alloc &) {
    trace.finish(3);
    throw;
  } catch (const std::length_error &) {
    trace.finish(4);
    throw;
  } catch (...) {
    trace.finish(5);
    throw;
  }
}

int provider_auth_cli(int argc, char **argv) {
  try {
    ProviderAuthConfig config;
    config.cancelled = [] { return auth_interrupted != 0; };
    ProviderAuth auth{config};
    const std::string_view action = argc > 1 ? argv[1] : "status";
    if (action == "status" || action == "providers") {
      if (argc > 2)
        throw Error{ErrorCode::invalid_range};
      std::cout << unwrap(
                       dump_json(action == "status" ? auth.status() : auth.catalog()))
                << '\n';
      return 0;
    }
    AuthSignals signals;
    auto environment = unwrap(RawAuthEnvironment::open(auth.config()));
    std::unique_ptr<RawRuntime> runtime;
    RawThreadHandle handle;
    {
      RawWaitScope setup{signals.control()};
      const auto runtime_directory = environment->runtime_directory();
      environment->verify_runtime_directory(runtime_directory);
      runtime = unwrap(RawRuntime::open(runtime_directory, environment->identity()));
      environment->verify_runtime_directory(runtime_directory);
      handle = unwrap(runtime->prepare_thread());
    }
    RawThreadBinding binding{handle};
    RawWaitScope business{signals.control()};
    constexpr std::string_view purpose = "provider-auth";
    auto purpose_credit = unwrap(
        raw_claim_control(raw_envelope_size + sizeof(std::size_t) + purpose.size()));
    const auto purpose_size = purpose.size();
    const std::array purpose_fields{detail::raw_native_field(purpose_size),
                                    detail::raw_string_field(purpose)};
    (void)unwrap(
        raw_emit(1619, purpose_fields, RawPartition::control, &purpose_credit));
    const RawAuthLabels labels{
        argc > 2 ? std::string_view{argv[2]} : std::string_view{},
        argc > 3 && action != "add-provider" ? std::string_view{argv[3]}
                                             : std::string_view{}};
    RawAuthTrace command{RawAuthEntry{RawAuthAction::command, labels, nullptr, nullptr,
                                      false, false, action == "key"}};
    try {
      command.start();
      // A CLI command links child actions but does not hold their unbounded bank.
      const RawAuthParent parent{command.reference(), nullptr, nullptr};
      if (action == "add-provider" && argc == 6) {
        auth.add_provider(argv[2], argv[3], argv[4], argv[5], &parent);
        command.stage(109);
        command.finish(1);
        return 0;
      }
      if (argc < 3 || argc > 4)
        throw Error{ErrorCode::invalid_range};
      const std::string_view provider = argv[2],
                             account = argc == 4 ? argv[3] : "default";
      if (action == "key") {
        std::string prompted_key;
        command.callback(1, 1);
        try {
          auto key = input_secret("API key (input hidden; never use a chat command): ");
          command.callback(1, 2);
          prompted_key = std::move(key);
        } catch (...) {
          command.callback(1, 3);
          throw;
        }
        auth.set_key(provider, account, std::move(prompted_key), &parent);
      } else if (action == "use")
        auth.use(provider, account, &parent);
      else if (action == "logout") {
        auth.logout(provider, account, &parent);
        std::cerr
            << "Local credentials removed; remote revocation is not implemented.\n";
      } else if (action == "login") {
        if (text(auth.descriptor(provider, &parent), "login") == "device") {
          const auto begin = auth.device_begin(provider, account, &parent);
          std::cerr << "Open "
                    << text(begin, "verification_uri_complete",
                            text(begin, "verification_uri"))
                    << "\nCode: " << text(begin, "user_code") << '\n';
          const auto until = detail::Clock::now() + std::chrono::minutes{30};
          for (;;) {
            const auto state = auth.device_poll(provider, account, &parent);
            const auto phase = text(state, "state");
            if (phase == "ready")
              break;
            if (phase != "pending")
              throw Error{ErrorCode::provider_auth};
            const auto seconds = number(state, "wait_seconds");
            if (detail::Clock::now() + std::chrono::seconds{seconds} > until)
              throw Error{ErrorCode::io, ETIMEDOUT};
            const auto next = detail::Clock::now() + std::chrono::seconds{seconds};
            while (detail::Clock::now() < next) {
              if (auth_interrupted)
                throw Error{ErrorCode::interrupted};
              std::this_thread::sleep_for(std::chrono::milliseconds{50});
            }
          }
          std::cerr << "Sign-in saved.\n";
        } else
          auth.browser_login(
              provider, account,
              [](std::string_view s) { std::cerr << s << std::flush; },
              [] { return input_secret(); }, &parent);
      } else
        throw Error{ErrorCode::invalid_range};
      command.stage(109);
      command.finish(1);
    } catch (const Error &error) {
      command.finish(2, &error);
      throw;
    } catch (const std::bad_alloc &) {
      command.finish(3);
      throw;
    } catch (const std::length_error &) {
      command.finish(4);
      throw;
    } catch (...) {
      command.finish(5);
      throw;
    }
    return 0;
  } catch (const Error &e) {
    std::cerr
        << "Provider auth: " << error_name(e.code) << " (" << e.detail
        << ").\nUsage: blackbird auth providers|status|key PROVIDER [ACCOUNT]|login "
           "PROVIDER [ACCOUNT]|use PROVIDER ACCOUNT|logout PROVIDER "
           "[ACCOUNT]|add-provider ID HTTPS_BASE responses|chat|messages ENV\n";
  } catch (const std::exception &) {
    std::cerr
        << "Provider auth: malformed local or provider data. No credentials printed.\n";
  }
  return 1;
}
} // namespace blackbird
