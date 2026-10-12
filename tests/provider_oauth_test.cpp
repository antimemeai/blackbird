#include "../src/native_process.hpp"
#include "blackbird/json.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/tools.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <charconv>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
using namespace blackbird;
int reply(ProviderHttpResponseLease &response, int status, std::string body) {
  auto extent = response.prepare(body.size());
  std::copy(body.begin(), body.end(), extent.begin());
  response.commit(body.size());
  return status;
}

void check(bool b, const char *message) {
  if (!b)
    throw std::runtime_error{message};
}
template <class F> void rejects(F f) {
  try {
    f();
  } catch (const Error &) {
    return;
  }
  throw std::runtime_error{"accepted invalid identity"};
}
std::string run(std::vector<std::string> args, std::string_view bytes = {}) {
  detail::Child c;
  c.separate_error = true;
  c.start(std::move(args));
  auto until = detail::Clock::now() + std::chrono::seconds{10};
  c.send(bytes, until);
  c.close_input();
  int exit = 0;
  auto out = c.collect(until, 65536, &exit);
  check(exit == 0, "openssl fixture failed");
  return out;
}
std::string encode(std::string_view bytes) {
  constexpr char abc[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string s;
  for (std::size_t at = 0; at < bytes.size(); at += 3) {
    const auto a = static_cast<unsigned char>(bytes[at]);
    s += abc[a >> 2U];
    const auto b =
        at + 1 < bytes.size() ? static_cast<unsigned char>(bytes[at + 1]) : 0U;
    s += abc[((a & 3U) << 4U) | (b >> 4U)];
    if (at + 1 < bytes.size()) {
      const auto c =
          at + 2 < bytes.size() ? static_cast<unsigned char>(bytes[at + 2]) : 0U;
      s += abc[((b & 15U) << 2U) | (c >> 6U)];
      if (at + 2 < bytes.size())
        s += abc[c & 63U];
    }
  }
  return s;
}
std::string decode(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+')
      out += ' ';
    else if (s[i] == '%') {
      unsigned v = 0;
      auto r = std::from_chars(s.data() + i + 1, s.data() + i + 3, v, 16);
      check(r.ec == std::errc{}, "fixture decode");
      out += static_cast<char>(v);
      i += 2;
    } else
      out += s[i];
  }
  return out;
}
std::map<std::string, std::string> params(std::string_view s) {
  std::map<std::string, std::string> out;
  while (!s.empty()) {
    const auto end = s.find('&'), eq = s.find('=');
    out.emplace(
        std::string{s.substr(0, eq)},
        decode(s.substr(eq + 1, end == std::string_view::npos ? end : end - eq - 1)));
    if (end == std::string_view::npos)
      break;
    s.remove_prefix(end + 1);
  }
  return out;
}
int main() {
  char directory[] = "/tmp/blackbird-oauth-test-XXXXXX";
  const auto *tmp = ::mkdtemp(directory);
  if (!tmp)
    return 1;
  try {
    ProviderAuthConfig config;
    config.directory = std::filesystem::path{tmp} / "auth";
    config.now = [] { return 1000; };
    check(auth_pkce_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", config) ==
              "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
          "RFC7636 challenge mismatch");
    const auto private_file = std::filesystem::path{tmp} / "key.pem";
    write_file(private_file.string(), run({"/usr/bin/openssl", "genrsa", "2048"}));
    auto modulus = run({"/usr/bin/openssl", "rsa", "-in", private_file.string(),
                        "-modulus", "-noout"});
    check(modulus.starts_with("Modulus="), "RSA modulus absent");
    modulus = modulus.substr(8);
    while (modulus.ends_with('\n'))
      modulus.pop_back();
    std::string n;
    for (std::size_t i = 0; i < modulus.size(); i += 2) {
      unsigned v = 0;
      auto r = std::from_chars(modulus.data() + i, modulus.data() + i + 2, v, 16);
      check(r.ec == std::errc{}, "modulus hex");
      n += static_cast<char>(v);
    }
    const auto jwks = Value::object(
        {{"keys", Value{Value::Array{Value::object({{"kid", Value{"test"}},
                                                    {"kty", Value{"RSA"}},
                                                    {"n", Value{encode(n)}},
                                                    {"e", Value{"AQAB"}}})}}}});
    auto sign = [&](std::string nonce = "nonce", std::string client = "issued",
                    int expires = 2000) {
      const auto header = encode(R"({"alg":"RS256","kid":"test"})");
      const auto claims = unwrap(
          dump_json(Value::object({{"iss", Value{"https://auth.openai.com"}},
                                   {"aud", Value{std::move(client)}},
                                   {"nonce", Value{std::move(nonce)}},
                                   {"sub", Value{"subject"}},
                                   {"exp", Value{Number{std::to_string(expires)}}}})));
      const auto body = header + "." + encode(claims);
      return body + "." +
             encode(run({"/usr/bin/openssl", "dgst", "-sha256", "-sign",
                         private_file.string()},
                        body));
    };
    auto token = sign();
    check(
        auth_validate_openai_identity(
            {.token = token, .client = "issued", .nonce = "nonce"}, jwks, 1000, config)
                .find("sub")
                ->string() == "subject",
        "valid JWT failed");
    rejects([&] {
      (void)auth_validate_openai_identity(
          {.token = token, .client = "wrong", .nonce = "nonce"}, jwks, 1000, config);
    });
    rejects([&] {
      (void)auth_validate_openai_identity(
          {.token = token, .client = "issued", .nonce = "wrong"}, jwks, 1000, config);
    });
    rejects([&] {
      (void)auth_validate_openai_identity(
          {.token = sign("nonce", "issued", 900), .client = "issued", .nonce = "nonce"},
          jwks, 1000, config);
    });
    token[token.size() - 10] = token[token.size() - 10] == 'A' ? 'B' : 'A';
    rejects([&] {
      (void)auth_validate_openai_identity(
          {.token = token, .client = "issued", .nonce = "nonce"}, jwks, 1000, config);
    });
    rejects([&] {
      (void)auth_validate_openai_identity(
          {.token = "eyJhbGciOiJub25lIn0.e30.", .client = "issued", .nonce = "nonce"},
          jwks, 1000, config);
    });
    std::map<std::string, std::string> authorization;
    config.http = [&](const ProviderHttpRequest &r,
                      ProviderHttpResponseLease &response) {
      if (r.url.ends_with("jwks.json"))
        return reply(response, 200, unwrap(dump_json(jwks)));
      auto fields = params(r.body);
      check(fields["client_id"] == "issued", "dynamic client used for exchange");
      check(fields["redirect_uri"] == authorization["redirect_uri"],
            "redirect changed");
      check(auth_pkce_challenge(fields["code_verifier"], config) ==
                authorization["code_challenge"],
            "PKCE linkage");
      return reply(
          response, 200,
          unwrap(dump_json(Value::object(
              {{"access_token", Value{"fixture-access"}},
               {"refresh_token", Value{"fixture-refresh"}},
               {"expires_in", Value{Number{"3600"}}},
               {"id_token", Value{sign(authorization["nonce"])}},
               {"scope",
                Value{"openid resource.invoke chatgpt.tokens.use.direct"}}}))));
    };
    ProviderAuth auth{config};
    std::thread browser;
    try {
      auth.browser_login(
          "openai", "personal",
          [&](std::string_view notice) {
            const auto start = notice.find("https://auth.openai.com/");
            if (start == std::string_view::npos)
              return;
            const auto line = notice.substr(start, notice.find('\n', start) - start);
            authorization = params(line.substr(line.find('?') + 1));
            check(authorization["client_id"] == "dynamic_agent_client",
                  "initial registration wrong");
            browser = std::thread{[&] {
              const auto &uri = authorization["redirect_uri"];
              const auto colon = uri.find(':', 7), slash = uri.find('/', colon);
              const auto port = std::stoi(uri.substr(colon + 1, slash - colon - 1));
              for (const bool correct : {false, true}) {
                const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
                if (fd < 0)
                  throw Error{ErrorCode::io};
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                addr.sin_port = htons(static_cast<std::uint16_t>(port));
                check(::connect(fd, reinterpret_cast<sockaddr *>(&addr),
                                sizeof(addr)) == 0,
                      "callback connect");
                const auto req =
                    "GET /auth/callback?code=fixture&client_id=issued&state=" +
                    (correct ? authorization["state"] : "wrong") +
                    " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
                (void)::write(fd, req.data(), req.size());
                char bytes[512];
                const auto got = ::read(fd, bytes, sizeof(bytes));
                check(got > 0, "callback reply missing");
                const std::string_view response{bytes, static_cast<std::size_t>(got)};
                check(response.starts_with(correct ? "HTTP/1.1 200" : "HTTP/1.1 400"),
                      "callback state not checked");
                (void)::close(fd);
              }
            }};
          },
          [] { return std::string{}; });
    } catch (...) {
      if (browser.joinable())
        browser.join();
      throw;
    }
    browser.join();
    check(auth.selected("openai"), "browser account not selected");
    check(unwrap(dump_json(auth.status())).find("fixture-access") == std::string::npos,
          "status token leak");
    auto saved = auth.registration("openai", "personal");
    auth.logout("openai", "personal");
    const auto retained = auth.registration("openai", "personal");
    check(retained.find("client_id")->string() == "issued",
          "logout discarded registration");
    rejects([&] {
      auth.save_oauth(
          "openai", "personal",
          unwrap(parse_json(
              R"({"access_token":"a","refresh_token":"r","expires_in":3600})")),
          "issued", auth.descriptor("openai").find("token_url")->string(),
          Value::object({}),
          {.account_revision = std::stoll(saved.find("revision")->number().text())});
    });
    config.http = [&](const ProviderHttpRequest &r,
                      ProviderHttpResponseLease &response) {
      const auto fields = unwrap(parse_json(r.body));
      check(fields.find("state")->string() == authorization["state"],
            "anthropic state missing");
      check(auth_pkce_challenge(fields.find("code_verifier")->string(), config) ==
                authorization["code_challenge"],
            "anthropic PKCE mismatch");
      return reply(
          response, 200,
          R"({"access_token":"anthropic-a","refresh_token":"anthropic-r","expires_in":3600})");
    };
    ProviderAuth anthropic{config};
    anthropic.browser_login(
        "anthropic", "max",
        [&](std::string_view notice) {
          const auto at = notice.find("https://claude.ai/");
          if (at != std::string_view::npos) {
            const auto line = notice.substr(at, notice.find('\n', at) - at);
            authorization = params(line.substr(line.find('?') + 1));
          }
        },
        [&] { return "code#" + authorization["state"]; });
    check(anthropic.selected("anthropic"), "anthropic login not saved");
    std::filesystem::remove_all(tmp);
    std::cout << "provider OAuth checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << ':' << e.detail << '\n';
  }
  std::filesystem::remove_all(tmp);
  return 1;
}
