#include "blackbird/json.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/tools.hpp"
#include <algorithm>
#include <atomic>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
int reply(ProviderHttpResponseLease &response, int status, std::string body) {
  auto extent = response.prepare(body.size());
  std::copy(body.begin(), body.end(), extent.begin());
  response.commit(body.size());
  return status;
}

void check(bool b, const char *what) {
  if (!b)
    throw std::runtime_error{what};
}
template <class F> void rejects(F f, ErrorCode code) {
  try {
    f();
  } catch (const Error &e) {
    check(e.code == code, "wrong rejection");
    return;
  }
  throw std::runtime_error{"not rejected"};
}
Value tokens() {
  return unwrap(parse_json(
      R"({"access_token":"new-secret","refresh_token":"rotated-secret","expires_in":3600,"token_type":"Bearer"})"));
}
int main() {
  char path[] = "/tmp/blackbird-auth-XXXXXX";
  const auto *tmp = ::mkdtemp(path);
  if (!tmp)
    return 1;
  const auto root = std::filesystem::path{tmp} / "auth";
  try {
    std::atomic<std::int64_t> clock{1000};
    std::atomic<int> refreshes{0}, requests{0};
    ProviderAuthConfig config;
    config.directory = root;
    config.now = [&] { return clock.load(); };
    config.http = [&](const ProviderHttpRequest &r,
                      ProviderHttpResponseLease &response) {
      check(r.url.starts_with("https://"), "insecure URL");
      if (r.url.find("oauth/token") != std::string::npos) {
        ++refreshes;
        check(r.body.find("refresh_token=rotated-secret") != std::string::npos,
              "wrong refresh token");
        return reply(response, 200, unwrap(dump_json(tokens())));
      }
      ++requests;
      bool found = false;
      for (const auto &[k, v] : r.headers)
        if (k == "Authorization")
          found = v == "Bearer new-secret" || v == "Bearer api-secret";
      check(found, "incorrect credential header");
      return reply(response, 200, "{\"ok\":true}");
    };
    ProviderAuth auth{config};
    check(!auth.selected("openai"), "selected missing");
    check(!std::filesystem::exists(root), "metadata created store");
    auth.set_key("openai", "work", "api-secret");
    check(auth.selected("openai"), "key not selected");
    struct stat st{};
    check(::stat((root / "credentials.bbm").c_str(), &st) == 0 &&
              (st.st_mode & 0777) == 0600,
          "credential mode");
    check(::stat(root.c_str(), &st) == 0 && (st.st_mode & 0777) == 0700,
          "directory mode");
    const auto status = unwrap(dump_json(auth.status()));
    check(status.find("secret") == std::string::npos, "status secret leak");
    (void)auth.request("openai", "responses", Value::object({}), 10);
    check(requests == 1, "request absent");
    rejects([&] { auth.set_key("openai", "bad", "key\nInjected: yes"); },
            ErrorCode::invalid_range);
    rejects([&] { auth.set_key("openai", "../escape", "secret"); },
            ErrorCode::invalid_range);
    rejects([&] { auth.add_provider("bad", "http://example.com", "chat", "KEY"); },
            ErrorCode::invalid_range);
    auth.add_provider("other", "https://example.com/v1", "chat", "OTHER_KEY");
    check(auth.descriptor("other").find("protocol")->string() == "chat",
          "custom registry");
    rejects([&] { auth.add_provider("openai", "https://evil.example", "chat", "KEY"); },
            ErrorCode::conflict);
    const auto d = auth.descriptor("openai");
    auth.save_oauth("openai", "personal", tokens(), "issued-client",
                    d.find("token_url")->string());
    clock = 4550;
    std::thread a{
        [&] { (void)auth.request("openai", "responses", Value::object({}), 10); }};
    std::thread b{[&] {
      ProviderAuth other{config};
      (void)other.request("openai", "responses", Value::object({}), 10);
    }};
    a.join();
    b.join();
    check(refreshes == 1, "rotating refresh raced");
    auth.set_key("openai", "logged_out", "api-secret");
    auth.use("openai", "personal");
    auth.logout("openai", "personal");
    rejects([&] { (void)auth.request("openai", "responses", Value::object({}), 10); },
            ErrorCode::provider_auth);
    auth.use("openai", "work");
    (void)auth.request("openai", "responses", Value::object({}), 10);
    auto broken = tokens();
    for (auto &[k, v] : broken.object())
      if (k == "expires_in")
        v = Value{Number{"0"}};
    rejects(
        [&] {
          auth.save_oauth("openai", "broken", broken, "issued",
                          d.find("token_url")->string());
        },
        ErrorCode::corrupt);
    check(auth.status().find("accounts")->array().size() == 3,
          "partial token record committed");
    config.http = [&](const ProviderHttpRequest &r,
                      ProviderHttpResponseLease &response) {
      if (r.url.ends_with("device_authorization"))
        return reply(
            response, 200,
            R"({"device_code":"private-device","user_code":"CODE","verification_uri":"https://auth.kimi.com/device","verification_uri_complete":"https://auth.kimi.com/device?code=CODE","expires_in":30,"interval":2})");
      ++requests;
      return reply(response, 400, R"({"error":"slow_down"})");
    };
    ProviderAuth device{config};
    const auto public_device = unwrap(dump_json(device.device_begin("kimi", "code")));
    check(public_device.find("private-device") == std::string::npos,
          "device secret leaked");
    const auto count = requests.load();
    (void)device.device_poll("kimi", "code");
    check(requests == count, "poll ignored interval");
    clock += 2;
    auto poll = device.device_poll("kimi", "code");
    check(poll.find("wait_seconds")->number().text() == "7", "slow_down ignored");
    clock += 31;
    poll = device.device_poll("kimi", "code");
    check(poll.find("state")->string() == "expired", "device expiry ignored");
    rejects([&] { (void)device.device_poll("kimi", "code"); },
            ErrorCode::invalid_range);
    for (int status_code : {401, 403})
      rejects([&] { require_provider_success(status_code); }, ErrorCode::provider_auth);
    rejects([&] { require_provider_success(429); }, ErrorCode::provider_rate_limit);
    rejects([&] { require_provider_success(503); }, ErrorCode::external_unknown);
    auth.logout("openai", "never-signed-in");
    rejects(
        [&] {
          auth.save_oauth("openai", "never-signed-in", tokens(), "issued",
                          d.find("token_url")->string(), Value::object({}),
                          {.account_revision = -1});
        },
        ErrorCode::conflict);
    // Separate processes share the rotating token under the native file lock.
    auto process_config = config;
    process_config.directory = std::filesystem::path{tmp} / "process-auth";
    const auto count_file = std::filesystem::path{tmp} / "refresh-count";
    process_config.http = [&](const ProviderHttpRequest &r,
                              ProviderHttpResponseLease &response) {
      if (r.url.find("oauth/token") != std::string::npos) {
        const int fd = ::open(count_file.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        check(fd >= 0, "counter open");
        check(::write(fd, "1", 1) == 1, "counter write");
        (void)::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        return reply(response, 200, unwrap(dump_json(tokens())));
      }
      return reply(response, 200, "{}");
    };
    ProviderAuth process_auth{process_config};
    process_auth.save_oauth("openai", "work", tokens(), "issued",
                            d.find("token_url")->string());
    clock += 3600;
    int barrier[2];
    check(::pipe(barrier) == 0, "barrier pipe");
    std::vector<pid_t> children;
    for (int i = 0; i < 2; ++i) {
      const auto pid = ::fork();
      check(pid >= 0, "fork");
      if (pid == 0) {
        (void)::close(barrier[1]);
        char value;
        const auto got = ::read(barrier[0], &value, 1);
        if (got != 1)
          ::_exit(2);
        try {
          ProviderAuth child_auth{process_config};
          (void)child_auth.request("openai", "responses", Value::object({}), 10);
          ::_exit(0);
        } catch (...) {
          ::_exit(3);
        }
      }
      children.push_back(pid);
    }
    (void)::close(barrier[0]);
    check(::write(barrier[1], "xx", 2) == 2, "release barrier");
    (void)::close(barrier[1]);
    for (const auto pid : children) {
      int child_status = 0;
      check(::waitpid(pid, &child_status, 0) == pid && WIFEXITED(child_status) &&
                WEXITSTATUS(child_status) == 0,
            "refresh child failed");
    }
    check(std::filesystem::file_size(count_file) == 1,
          "cross-process token rotation raced");
    // Owner-only mode and symlink policy are enforced before reading secrets.
    check(::chmod((root / "credentials.bbm").c_str(), 0644) == 0, "chmod");
    rejects([&] { (void)auth.status(); }, ErrorCode::conflict);
    check(::chmod((root / "credentials.bbm").c_str(), 0600) == 0, "chmod restore");
    std::filesystem::rename(root / "credentials.bbm", root / "original");
    std::filesystem::create_symlink(root / "original", root / "credentials.bbm");
    rejects([&] { (void)auth.status(); }, ErrorCode::io);
    std::filesystem::remove_all(tmp);
    std::cout << "provider auth checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << ':' << e.detail << '\n';
  }
  std::filesystem::remove_all(tmp);
  return 1;
}
