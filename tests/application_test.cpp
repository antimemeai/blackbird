#include "blackbird/application.hpp"
#include "blackbird/json.hpp"
#include <chrono>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace blackbird;
namespace {
void check(bool good) {
  if (!good)
    throw Error{ErrorCode::corrupt};
}
template <class T> T id(unsigned char number) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{number};
  return unwrap(T::from_bytes(bytes));
}
void transfer(int descriptor, std::span<std::byte> bytes, bool writing) {
  while (!bytes.empty()) {
    const auto count = writing ? ::write(descriptor, bytes.data(), bytes.size())
                               : ::read(descriptor, bytes.data(), bytes.size());
    check(count > 0);
    bytes = bytes.subspan(static_cast<std::size_t>(count));
  }
}
void send(int descriptor, const Value &value, bool fragmented = false) {
  auto packet = unwrap(encode_packet(value));
  std::array<std::byte, 4> header{};
  const auto size = static_cast<std::uint32_t>(packet.size());
  for (unsigned i = 0; i < 4; ++i)
    header[i] = std::byte{static_cast<unsigned char>(size >> ((3 - i) * 8))};
  if (fragmented) {
    for (auto &byte : header)
      transfer(descriptor, {&byte, 1}, true);
    for (auto &byte : packet)
      transfer(descriptor, {&byte, 1}, true);
  } else {
    transfer(descriptor, header, true);
    transfer(descriptor, packet, true);
  }
}
Value receive(int descriptor) {
  std::array<std::byte, 4> header{};
  transfer(descriptor, header, false);
  std::uint32_t size = 0;
  for (auto byte : header)
    size = (size << 8) | std::to_integer<unsigned char>(byte);
  check(size <= ApplicationConnection::max_packet_bytes);
  std::vector<std::byte> packet(size);
  transfer(descriptor, packet, false);
  return unwrap(decode_packet(packet));
}
Value command(const char *type, const char *identity, const char *objective = "") {
  return Value::object({{"type", Value{type}},
                        {"id", Value{identity}},
                        {"objective", Value{objective}}});
}
class Provider final : public CodingProvider {
public:
  unsigned calls = 0;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    const auto &input = field(request, "input").array();
    const auto expected = calls == 1   ? "first objective"
                          : calls == 2 ? "replace me"
                          : calls == 3 ? "replacement objective"
                          : calls == 4 ? "long response"
                                       : "close me";
    check(string_field(input.back(), "content") == expected);
    if (calls == 1) {
      check(input.size() >= 2);
      const auto &observation = string_field(input[input.size() - 2], "content");
      check(observation.find("Retained source:") != std::string::npos &&
            observation.find("https://www.wikipedia.org/") != std::string::npos &&
            observation.find("live page text") != std::string::npos &&
            observation.find("18446744073709551615") != std::string::npos);
    }
    capture("data: {\"type\":\"response.output_text.delta\",\"item_id\":\"m1\","
            "\"delta\":\"Answer \"}\n\n");
    if (calls == 2 || calls >= 5) {
      while (!cancelled())
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      throw Error{ErrorCode::interrupted};
    }
    const auto answer =
        calls == 4 ? "Answer " + std::string(3 * 1024 * 1024, 'x') : "Answer complete";
    auto response = Value::object(
        {{"output", Value{Value::Array{Value::object(
                        {{"type", Value{"message"}},
                         {"id", Value{"m1"}},
                         {"role", Value{"assistant"}},
                         {"content", Value{Value::Array{Value::object(
                                         {{"type", Value{"output_text"}},
                                          {"text", Value{answer}}})}}}})}}}});
    capture(unwrap(dump_json(response)));
    return response;
  }
};
Value until(int descriptor, const char *type, const char *identity) {
  for (unsigned count = 0; count < 50; ++count) {
    auto packet = receive(descriptor);
    check(string_field(packet, "id") == identity);
    if (string_field(packet, "type") == type)
      return packet;
    check(string_field(packet, "type") != "error");
  }
  throw Error{ErrorCode::corrupt};
}
void session(const std::filesystem::path &directory) {
  int descriptors[2]{};
  check(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);
  for (const auto descriptor : descriptors)
    check(::fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 0);
  std::exception_ptr host_failure;
  std::jthread host{[&] {
    try {
      auto opening = command("open", "1");
      opening.object().emplace_back("session", Value{directory.string()});
      send(descriptors[1], opening, true);
      const auto ready = until(descriptors[1], "ready", "1");
      check(field(ready, "protocol") == Value{Number{1}});
      send(descriptors[1], command("invalid", "bad"));
      check(string_field(receive(descriptors[1]), "type") == "error");
      auto first = command("turn", "2", "first objective");
      first.object().emplace_back("observations",
                                  Value{Value::Array{Value::object(
                                      {{"url", Value{"https://www.wikipedia.org/"}},
                                       {"title", Value{"Wikipedia"}},
                                       {"text", Value{"live page text"}},
                                       {"capture_start_ns", Value{Number{UINT64_MAX}}},
                                       {"truncated", Value{false}}})}});
      send(descriptors[1], first, true);
      const auto final = until(descriptors[1], "final", "2");
      check(string_field(final, "text") == "Answer complete" &&
            string_field(final, "outcome") == "completed");
      send(descriptors[1], command("turn", "3", "replace me"));
      (void)until(descriptors[1], "text", "3");
      send(descriptors[1], command("steer", "4", "replacement objective"));
      check(string_field(until(descriptors[1], "final", "3"), "outcome") ==
            "cancelled");
      check(string_field(until(descriptors[1], "final", "4"), "outcome") ==
            "completed");
      send(descriptors[1], command("turn", "long", "long response"));
      std::size_t streamed = 0;
      for (;;) {
        const auto event = receive(descriptors[1]);
        check(string_field(event, "id") == "long");
        const auto &type = string_field(event, "type");
        if (type == "text")
          streamed += string_field(event, "text").size();
        else if (type == "final") {
          check(string_field(event, "outcome") == "completed" &&
                field(event, "text_truncated") == Value{true} &&
                string_field(event, "text").size() ==
                    ApplicationConnection::max_packet_bytes / 2 &&
                streamed == 3 * 1024 * 1024 + 7);
          break;
        } else
          check(type == "status");
      }
      send(descriptors[1], command("turn", "5", "close me"));
      (void)until(descriptors[1], "text", "5");
      send(descriptors[1], command("steer", "discarded", "do not execute"));
      send(descriptors[1], command("cancel", "stop"));
      (void)until(descriptors[1], "status", "stop");
      check(string_field(until(descriptors[1], "final", "5"), "outcome") ==
            "cancelled");
      check(string_field(until(descriptors[1], "final", "discarded"), "outcome") ==
            "cancelled");
      send(descriptors[1], command("turn", "8", "close me"));
      (void)until(descriptors[1], "text", "8");
      send(descriptors[1], command("close", "6"));
      check(string_field(until(descriptors[1], "final", "8"), "outcome") ==
            "cancelled");
      (void)until(descriptors[1], "closed", "6");
    } catch (...) {
      host_failure = std::current_exception();
    }
    (void)::shutdown(descriptors[1], SHUT_RDWR);
  }};
  try {
    ApplicationConnection application{
        {.input = descriptors[0], .output = descriptors[0]}};
    (void)application.open();
    const JournalHeader header{id<EnvironmentId>(1),
                               id<AuditStreamId>(2),
                               3,
                               {8 * 1024 * 1024, 24 * 1024 * 1024},
                               std::nullopt};
    auto root = unwrap(
        RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                  NativeJournalDirectory::open(directory.string()))),
                              "audit", header, {128 * 1024 * 1024, 10000}));
    AuditLog log{*root};
    ContextStore context{log};
    Provider provider;
    CodingEngine engine{log, context, provider, "test-model"};
    const auto program = directory / "turn.lua";
    write_file(program.string(),
               "local r=blackbird.request(); for _,i in ipairs(r.output) do "
               "if i.type=='message' then blackbird.present(i) end end");
    SessionSettings settings;
    settings.workflow = program.string();
    settings.model = "test-model";
    check(application.run(engine, log, settings, directory) == 0);
    check(provider.calls == 6);
  } catch (...) {
    (void)::shutdown(descriptors[0], SHUT_RDWR);
    host.join();
    if (host_failure)
      std::rethrow_exception(host_failure);
    throw;
  }
  host.join();
  (void)::close(descriptors[0]);
  (void)::close(descriptors[1]);
  if (host_failure)
    std::rethrow_exception(host_failure);
}
void bad_frame(bool oversized) {
  int descriptors[2]{};
  check(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);
  for (const auto descriptor : descriptors)
    check(::fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 0);
  std::array<std::byte, 4> bytes{std::byte{0}, std::byte{0x40}, std::byte{0},
                                 std::byte{1}};
  if (!oversized)
    bytes[1] = std::byte{0};
  transfer(descriptors[1], oversized ? std::span{bytes} : std::span{bytes}.first(2),
           true);
  (void)::shutdown(descriptors[1], SHUT_WR);
  bool refused = false;
  try {
    ApplicationConnection application{
        {.input = descriptors[0], .output = descriptors[0]}};
    (void)application.open();
  } catch (const Error &failure) {
    refused = failure.code == (oversized ? ErrorCode::capacity : ErrorCode::incomplete);
  }
  check(refused);
  (void)::close(descriptors[0]);
  (void)::close(descriptors[1]);
}
void disconnected(const std::filesystem::path &directory, bool broken_output) {
  std::filesystem::create_directory(directory);
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
  int descriptors[2]{};
  check(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0);
  for (const auto descriptor : descriptors)
    check(::fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 0);
  auto opening = command("open", "open");
  opening.object().emplace_back("session", Value{directory.string()});
  send(descriptors[1], opening);
  ApplicationConnection application{
      {.input = descriptors[0], .output = descriptors[0]}};
  (void)application.open();
  const JournalHeader header{id<EnvironmentId>(1),
                             id<AuditStreamId>(2),
                             3,
                             {1024 * 1024, 4 * 1024 * 1024},
                             std::nullopt};
  auto root = unwrap(
      RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(directory.string()))),
                            "audit", header, {32 * 1024 * 1024, 10000}));
  AuditLog log{*root};
  ContextStore context{log};
  Provider provider;
  CodingEngine engine{log, context, provider, "test-model"};
  std::cerr << "Admit owned child\n";
  (void)engine.operator_call("exec", Value::object({{"command", Value{"sleep 30"}},
                                                    {"yield_ms", Value{Number{0}}}}));
  check(engine.command_jobs()->active());
  std::cerr << "Owned child admitted\n";
  if (broken_output) {
    (void)::close(descriptors[1]);
    descriptors[1] = -1;
  } else {
    send(descriptors[1], command("turn", "never-start", "do not execute"));
    (void)::shutdown(descriptors[1], SHUT_WR);
  }
  bool failed = false;
  std::cerr << "Run disconnected application\n";
  try {
    (void)application.run(engine, log, SessionSettings{}, directory);
  } catch (const Error &failure) {
    failed = failure.code == ErrorCode::io;
  }
  check(failed == broken_output && !engine.command_jobs()->active() &&
        provider.calls == 0);
  std::cerr << "Disconnected application stopped\n";
  if (broken_output)
    application.fatal("open", "disconnected");
  else {
    (void)until(descriptors[1], "ready", "open");
    check(string_field(until(descriptors[1], "final", "never-start"), "outcome") ==
          "cancelled");
  }
  (void)::close(descriptors[0]);
  if (descriptors[1] >= 0)
    (void)::close(descriptors[1]);
}
} // namespace
int main() {
  const char *stage = "session";
  char name[] = "/tmp/blackbird-application-XXXXXX";
  const auto directory = ::mkdtemp(name);
  if (!directory)
    return 2;
  try {
    std::cerr << "Application session\n";
    session(directory);
    std::cerr << "Application invalid frames\n";
    stage = "oversized";
    bad_frame(true);
    stage = "truncated";
    bad_frame(false);
    stage = "disconnected child";
    std::cerr << "Application disconnected child\n";
    disconnected(std::filesystem::path{directory} / "disconnected", true);
    stage = "EOF child";
    std::cerr << "Application EOF child\n";
    disconnected(std::filesystem::path{directory} / "eof", false);
    std::filesystem::remove_all(directory);
    std::cout
        << "Application framing, observations, streaming, steer and close passed\n";
    return 0;
  } catch (const Error &failure) {
    std::cerr << stage << ": " << error_name(failure.code) << " (" << failure.detail
              << ")\n";
    return 1;
  }
}
