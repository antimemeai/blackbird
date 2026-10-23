#include "blackbird/application.hpp"
#include "blackbird/json.hpp"
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace blackbird;
namespace {
void check(bool good, const char *message) {
  if (!good) {
    std::cerr << message << '\n';
    throw Error{ErrorCode::corrupt};
  }
}
void transfer(int descriptor, std::span<std::byte> bytes, bool writing) {
  while (!bytes.empty()) {
    pollfd fd{descriptor, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
    check(::poll(&fd, 1, 5000) > 0, "wire deadline");
    const auto count = writing ? ::write(descriptor, bytes.data(), bytes.size())
                               : ::read(descriptor, bytes.data(), bytes.size());
    check(count > 0, "wire closed");
    bytes = bytes.subspan(static_cast<std::size_t>(count));
  }
}
void send(int descriptor, const Value &packet) {
  auto bytes = unwrap(encode_packet(packet));
  std::array<std::byte, 4> header{};
  const auto size = static_cast<std::uint32_t>(bytes.size());
  for (unsigned n = 0; n < 4; ++n)
    header[n] = std::byte{static_cast<unsigned char>(size >> ((3 - n) * 8))};
  transfer(descriptor, header, true);
  transfer(descriptor, bytes, true);
}
Value receive(int descriptor) {
  std::array<std::byte, 4> header{};
  transfer(descriptor, header, false);
  std::uint32_t size = 0;
  for (const auto byte : header)
    size = (size << 8) | std::to_integer<unsigned char>(byte);
  check(size <= ApplicationConnection::max_packet_bytes, "oversized event");
  std::vector<std::byte> bytes(size);
  transfer(descriptor, bytes, false);
  return unwrap(decode_packet(bytes));
}
Value packet(const char *type, const char *id) {
  return Value::object({{"type", Value{type}}, {"id", Value{id}}});
}
Value until(int descriptor, const char *type, const char *id) {
  for (unsigned n = 0; n < 100; ++n) {
    auto event = receive(descriptor);
    if (string_field(event, "type") == type && string_field(event, "id") == id)
      return event;
    check(string_field(event, "type") != "error", "unexpected error");
  }
  throw Error{ErrorCode::corrupt};
}
Value scalar(const char *kind) { return Value::object({{"kind", Value{kind}}}); }
Value object_schema(const char *name) {
  return Value::object(
      {{"kind", Value{"object"}},
       {"fields", Value{Value::Array{Value::object({{"name", Value{name}},
                                                    {"required", Value{true}},
                                                    {"schema", scalar("string")}})}}}});
}
Value catalog() {
  return Value::object(
      {{"version", Value{Number{1}}},
       {"catalog", Value{Value::Array{Value::object(
                       {{"name", Value{"oxcart_state"}},
                        {"description", Value{"Read the identified Oxcart document"}},
                        {"arguments", object_schema("document")},
                        {"result", object_schema("reading_path")}})}}}});
}
void open(int descriptor, const std::filesystem::path &directory) {
  auto opening = packet("open", "open");
  opening.object().emplace_back("session", Value{directory.string()});
  opening.object().emplace_back("host_tools", catalog());
  send(descriptor, opening);
  const auto ready = until(descriptor, "ready", "open");
  check(ready.find("host_tools") != nullptr, "RED: ready lacks host-tool negotiation");
  const auto &offer = field(ready, "host_tools");
  check(field(offer, "version") == Value{Number{1}} &&
            field(offer, "names") == Value{Value::Array{Value{"oxcart_state"}}},
        "negotiated names/version");
}
void turn(int descriptor, const char *id) {
  auto next = packet("turn", id);
  next.object().emplace_back("objective", Value{"Inspect the original reading"});
  send(descriptor, next);
}
struct ReplyCommand {
  const char *id;
  const char *outcome;
};
Value reply(const Value &call, ReplyCommand command, Value result) {
  auto value = packet("host_result", command.id);
  value.object().emplace_back("turn_id", field(call, "id"));
  value.object().emplace_back("call_id", field(call, "call_id"));
  value.object().emplace_back("outcome", Value{command.outcome});
  value.object().emplace_back("result", std::move(result));
  return value;
}
Value function(const char *name, const char *id, Value arguments) {
  return Value::object(
      {{"output",
        Value{Value::Array{Value::object({{"type", Value{"function_call"}},
                                          {"name", Value{name}},
                                          {"call_id", Value{id}},
                                          {"arguments", std::move(arguments)}})}}}});
}
Value answer() {
  return Value::object(
      {{"output", Value{Value::Array{Value::object(
                      {{"type", Value{"message"}},
                       {"role", Value{"assistant"}},
                       {"content", Value{Value::Array{Value::object(
                                       {{"type", Value{"output_text"}},
                                        {"text", Value{"Source backed"}}})}}}})}}}});
}
class Provider final : public CodingProvider {
public:
  std::function<Value(const Value &, unsigned)> script;
  unsigned calls = 0;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    auto response = script(request, ++calls);
    capture(unwrap(dump_json(response)));
    return response;
  }
};
using Host = std::function<void(int, const std::filesystem::path &)>;
using Inspect = std::function<void(RetainedState &, ContextStore &, unsigned)>;
void session(const std::filesystem::path &directory, const Host &host_script,
             std::function<Value(const Value &, unsigned)> provider_script,
             const Inspect &inspect = {}, bool transport_failure = false) {
  std::filesystem::create_directory(directory);
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
  int descriptors[2]{};
  check(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0, "socketpair");
  for (const auto fd : descriptors)
    check(::fcntl(fd, F_SETFD, FD_CLOEXEC) == 0, "cloexec");
  std::exception_ptr host_failure;
  std::jthread host{[&] {
    try {
      host_script(descriptors[1], directory);
    } catch (...) {
      host_failure = std::current_exception();
    }
    (void)::shutdown(descriptors[1], SHUT_RDWR);
  }};
  try {
    ApplicationConnection app{{.input = descriptors[0], .output = descriptors[0]}};
    (void)app.open();
    IdentityBytes environment{}, stream{};
    environment[0] = std::byte{1};
    stream[0] = std::byte{2};
    const JournalHeader header{unwrap(EnvironmentId::from_bytes(environment)),
                               unwrap(AuditStreamId::from_bytes(stream)),
                               1,
                               {8 * 1024 * 1024, 24 * 1024 * 1024},
                               std::nullopt};
    auto root = unwrap(
        RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                  NativeJournalDirectory::open(directory.string()))),
                              "audit", header, {128 * 1024 * 1024, 10000}));
    AuditLog log{*root};
    ContextStore context{log};
    Provider provider;
    provider.script = std::move(provider_script);
    CodingEngine engine{log, context, provider, "test-model"};
    SessionSettings settings;
    settings.workflow = std::string{BLACKBIRD_SOURCE_DIR} + "/programs/turn.lua";
    settings.model = "test-model";
    bool failed = false;
    try {
      (void)app.run(engine, log, settings, directory);
    } catch (const Error &error) {
      if (!transport_failure ||
          (error.code != ErrorCode::capacity && error.code != ErrorCode::io &&
           error.code != ErrorCode::incomplete))
        throw;
      failed = true;
    }
    check(failed == transport_failure, "expected transport failure");
    host.join();
    if (host_failure)
      std::rethrow_exception(host_failure);
    if (inspect)
      inspect(*root, context, provider.calls);
  } catch (...) {
    (void)::shutdown(descriptors[0], SHUT_RDWR);
    if (host.joinable())
      host.join();
    (void)::close(descriptors[0]);
    (void)::close(descriptors[1]);
    if (host_failure)
      std::rethrow_exception(host_failure);
    throw;
  }
  (void)::close(descriptors[0]);
  (void)::close(descriptors[1]);
}
void circuit(const std::filesystem::path &directory) {
  const auto reading = directory / "reading.txt";
  session(
      directory,
      [&](int fd, const auto &path) {
        write_file(reading,
                   "Original native reading: document seven, source marker 731\n");
        open(fd, path);
        turn(fd, "turn");
        const auto call = until(fd, "host_call", "turn");
        check(string_field(call, "tool") == "oxcart_state" &&
                  string_field(field(call, "arguments"), "document") == "doc-7" &&
                  string_field(call, "call_id").size() == 32 &&
                  string_field(call, "invocation_id").size() == 32 &&
                  string_field(call, "generation").size() == 32,
              "call identity/arguments");
        auto progress = packet("host_progress", "progress");
        progress.object().emplace_back("turn_id", Value{"turn"});
        progress.object().emplace_back("call_id", field(call, "call_id"));
        progress.object().emplace_back("text", Value{"Reading retained"});
        send(fd, progress);
        const auto status = until(fd, "status", "turn");
        check(field(status, "call_id") == field(call, "call_id") &&
                  string_field(status, "text") == "Reading retained",
              "progress");
        auto stale = reply(call, {.id = "stale", .outcome = "success"}, Value{});
        for (auto &[key, value] : stale.object())
          if (key == "turn_id")
            value = Value{"old-turn"};
        send(fd, stale);
        check(field(until(fd, "error", "stale"), "final_follows") == Value{false},
              "stale rejected");
        send(fd,
             reply(call, {.id = "malformed", .outcome = "success"}, Value{Number{7}}));
        (void)until(fd, "error", "malformed");
        const auto result = Value::object({{"reading_path", Value{reading.string()}}});
        send(fd, reply(call, {.id = "result", .outcome = "success"}, result));
        send(fd, reply(call, {.id = "duplicate", .outcome = "success"}, result));
        (void)until(fd, "error", "duplicate");
        check(string_field(until(fd, "final", "turn"), "outcome") == "completed",
              "completed circuit");
        send(fd, packet("close", "close"));
        (void)until(fd, "closed", "close");
      },
      [&](const Value &request, unsigned calls) {
        const auto &tools = field(request, "tools").array();
        const auto tool =
            std::find_if(tools.begin(), tools.end(), [](const Value &entry) {
              return string_field(entry, "name") == "oxcart_state";
            });
        check(tool != tools.end() &&
                  string_field(field(*tool, "parameters"), "type") == "object",
              "provider catalog");
        if (calls == 1)
          return function("oxcart_state", "model-call",
                          Value::object({{"document", Value{"doc-7"}}}));
        const auto &input = field(request, "input").array();
        const auto &result = field(input.back(), "output");
        if (calls == 2) {
          check(string_field(result, "outcome") == "success" &&
                    string_field(field(result, "result"), "reading_path") ==
                        reading.string() &&
                    !string_field(result, "source").empty(),
                "host result in next request");
          return function("read_file", "read-original",
                          Value::object({{"path", Value{reading.string()}}}));
        }
        check(calls == 3 && unwrap(format_value(result)).find("source marker 731") !=
                                std::string::npos,
              "ordinary file read reaches continuation");
        return answer();
      },
      [](RetainedState &root, ContextStore &, unsigned calls) {
        check(calls == 3, "three provider requests");
        unsigned originals = 0;
        for (const auto &fact : root.committed_facts()) {
          const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
          if (!record || record->channel != ApplicationChannel::log)
            continue;
          const auto value = unwrap(read_packet(record->payload));
          const auto *label = value.find("label");
          if (!label || *label != Value{"application.host.packet"})
            continue;
          check(fact.event.dependencies.size() == 1, "original source dependency");
          const auto bytes = unwrap(root.source(fact.event.dependencies.front()));
          const auto packet = unwrap(decode_packet(bytes));
          if (string_field(packet, "id") == "stale" ||
              string_field(packet, "id") == "malformed" ||
              string_field(packet, "id") == "result" ||
              string_field(packet, "id") == "duplicate" ||
              string_field(packet, "id") == "progress") {
            check(bytes == unwrap(encode_packet(packet)),
                  "retained original packet bytes");
            ++originals;
          }
        }
        check(originals == 5, "all accepted/rejected/progress originals retained");
      });
}
void control_case(const std::filesystem::path &directory, const char *control) {
  std::string first_call;
  const std::string mode{control};
  session(
      directory,
      [&](int fd, const auto &path) {
        open(fd, path);
        turn(fd, "old");
        const auto old = until(fd, "host_call", "old");
        first_call = string_field(old, "call_id");
        if (mode == "eof")
          (void)::shutdown(fd, SHUT_WR);
        else if (mode == "steer") {
          auto replacement = packet("steer", "new");
          replacement.object().emplace_back("objective", Value{"Inspect replacement"});
          send(fd, replacement);
        } else
          send(fd, packet(control, mode == "close" ? "close" : "stop"));
        if (mode != "eof")
          check(field(until(fd, "host_cancel", "old"), "call_id") ==
                    field(old, "call_id"),
                "cancel names old call before final");
        check(string_field(until(fd, "final", "old"), "outcome") == "cancelled",
              "old turn cancelled");
        if (mode == "eof")
          return;
        if (mode == "close") {
          (void)until(fd, "closed", "close");
          return;
        }
        if (mode == "cancel")
          turn(fd, "new");
        const auto fresh = until(fd, "host_call", "new");
        check(field(fresh, "call_id") != field(old, "call_id"),
              "fresh operation identity");
        const auto payload =
            Value::object({{"reading_path", Value{"retained-source"}}});
        send(fd, reply(old, {.id = "late", .outcome = "success"}, payload));
        (void)until(fd, "error", "late");
        send(fd, reply(fresh, {.id = "fresh", .outcome = "success"}, payload));
        check(string_field(until(fd, "final", "new"), "outcome") == "completed",
              "replacement completes on its own result");
        send(fd, packet("close", "close"));
        (void)until(fd, "closed", "close");
      },
      [](const Value &request, unsigned calls) {
        const auto &input = field(request, "input").array();
        if (input.back().find("output"))
          return answer();
        return function("oxcart_state", calls == 1 ? "old-model" : "new-model",
                        Value::object({{"document", Value{"doc-7"}}}));
      },
      [&](RetainedState &root, ContextStore &, unsigned calls) {
        check(calls == (mode == "cancel" || mode == "steer" ? 3U : 1U),
              "no provider replay after interruption");
        bool unknown = false;
        for (const auto &fact : root.committed_facts())
          if (const auto *event =
                  std::get_if<AttemptObservationEvent>(&fact.event.body);
              event && hex_identity(event->attempt.bytes()) == first_call &&
              event->phase == AttemptPhase::terminal)
            unknown = event->disposition == AttemptDisposition::unknown;
        check(unknown, "unanswered host effect remains unknown");
      });
}
void outcomes(const std::filesystem::path &directory, const char *outcome) {
  std::string call_id;
  session(
      directory,
      [&](int fd, const auto &path) {
        open(fd, path);
        turn(fd, "turn");
        const auto call = until(fd, "host_call", "turn");
        call_id = string_field(call, "call_id");
        send(fd, reply(call, {.id = "outcome", .outcome = outcome},
                       Value::object({{"observed", Value{"source closed"}}})));
        (void)until(fd, "final", "turn");
        send(fd, packet("close", "close"));
        (void)until(fd, "closed", "close");
      },
      [&](const Value &request, unsigned calls) {
        if (calls == 1)
          return function("oxcart_state", "model",
                          Value::object({{"document", Value{"doc-7"}}}));
        const auto &input = field(request, "input").array();
        check(string_field(field(input.back(), "output"), "outcome") == outcome,
              "observed non-success reaches model");
        return answer();
      },
      [&](RetainedState &root, ContextStore &, unsigned calls) {
        check(calls == 2, "no automatic effect retry");
        bool observed = false;
        for (const auto &fact : root.committed_facts())
          if (const auto *event =
                  std::get_if<AttemptObservationEvent>(&fact.event.body);
              event && hex_identity(event->attempt.bytes()) == call_id &&
              event->phase == AttemptPhase::terminal)
            observed = event->disposition == (std::string_view{outcome} == "unknown"
                                                  ? AttemptDisposition::unknown
                                                  : AttemptDisposition::failure);
        check(observed, "host outcome settles correctly");
      });
}
template <class Action> void refused(Action action, const char *message) {
  bool rejected = false;
  try {
    action();
  } catch (const Error &) {
    rejected = true;
  }
  check(rejected, message);
}
void assign(Value &object, std::string_view key, Value value) {
  for (auto &[name, entry] : object.object())
    if (name == key) {
      entry = std::move(value);
      return;
    }
  throw Error{ErrorCode::corrupt};
}
Value typed_field(const char *name, Value type, bool required = true) {
  return Value::object({{"name", Value{name}},
                        {"required", Value{required}},
                        {"schema", std::move(type)}});
}
void catalog_checks() {
  const auto row = Value::object(
      {{"kind", Value{"object"}},
       {"fields", Value{Value::Array{typed_field("id", scalar("string")),
                                     typed_field("revision", scalar("integer")),
                                     typed_field("ready", scalar("boolean"), false),
                                     typed_field("empty", scalar("null"), false),
                                     typed_field("score", scalar("number"), false)}}}});
  const auto rows = Value::object({{"kind", Value{"array"}}, {"element", row}});
  const auto args =
      Value::object({{"kind", Value{"object"}},
                     {"fields", Value{Value::Array{typed_field("rows", rows)}}}});
  auto definition = field(catalog(), "catalog").array().front();
  assign(definition, "arguments", args);
  const HostToolCatalog tools{Value{Value::Array{definition}}};
  const auto make_args = [](Value row) {
    return Value::object({{"rows", Value{Value::Array{std::move(row)}}}});
  };
  const auto valid = Value::object({{"id", Value{"original"}},
                                    {"revision", Value{Number{UINT64_MAX}}},
                                    {"ready", Value{true}},
                                    {"empty", Value{}},
                                    {"score", Value{Number{"1.25"}}}});
  tools.validate_arguments("oxcart_state", make_args(valid));
  auto wrong = valid;
  assign(wrong, "revision", Value{Number{"1.2"}});
  refused([&] { tools.validate_arguments("oxcart_state", make_args(wrong)); },
          "fractional integer rejected");
  wrong = valid;
  assign(wrong, "ready", Value{Number{1}});
  refused([&] { tools.validate_arguments("oxcart_state", make_args(wrong)); },
          "boolean coercion rejected");
  wrong = valid;
  wrong.object().emplace_back("unexpected", Value{});
  refused([&] { tools.validate_arguments("oxcart_state", make_args(wrong)); },
          "extra nested field rejected");
  refused([&] { tools.validate_arguments("oxcart_state", Value::object({})); },
          "required array rejected");
  refused([&] { tools.validate_arguments("unknown", make_args(valid)); },
          "unadvertised capability rejected");
  auto broken = definition;
  assign(broken, "arguments",
         Value::object({{"kind", Value{"object"}}, {"fields", Value{false}}}));
  refused([&] { (void)HostToolCatalog{Value{Value::Array{broken}}}; },
          "malformed fields rejected");
  broken = definition;
  assign(broken, "result", scalar("script"));
  refused([&] { (void)HostToolCatalog{Value{Value::Array{broken}}}; },
          "unknown schema rejected");
  refused([&] { (void)HostToolCatalog{Value{Value::Array{definition, definition}}}; },
          "duplicate tool rejected");
  refused([&] { (void)HostToolCatalog{Value{false}}; }, "non-array catalog rejected");
  auto nested = scalar("string");
  for (unsigned n = 0; n < 18; ++n)
    nested = Value::object({{"kind", Value{"array"}}, {"element", nested}});
  broken = definition;
  assign(broken, "result", nested);
  refused([&] { (void)HostToolCatalog{Value{Value::Array{broken}}}; },
          "deep schema rejected");
  const auto advertised = tools.provider_tools();
  const auto &properties =
      field(field(advertised.array().front(), "parameters"), "properties");
  const auto &items = field(field(properties, "rows"), "items");
  check(string_field(items, "type") == "object" &&
            field(items, "additionalProperties") == Value{false} &&
            field(items, "required") ==
                Value{Value::Array{Value{"id"}, Value{"revision"}}},
        "native nested schema converted at provider boundary");
}
void received_before_bad_frame(const std::filesystem::path &directory) {
  std::string completed_call;
  session(
      directory,
      [&](int fd, const auto &path) {
        open(fd, path);
        turn(fd, "turn");
        const auto call = until(fd, "host_call", "turn");
        completed_call = string_field(call, "call_id");
        const auto result =
            reply(call, {.id = "received", .outcome = "success"},
                  Value::object({{"reading_path", Value{"preserved-original"}}}));
        auto body = unwrap(encode_packet(result));
        const auto size = static_cast<std::uint32_t>(body.size());
        std::vector<std::byte> bytes;
        for (unsigned n = 0; n < 4; ++n)
          bytes.push_back(std::byte{static_cast<unsigned char>(size >> ((3 - n) * 8))});
        bytes.insert(bytes.end(), body.begin(), body.end());
        bytes.insert(bytes.end(),
                     {std::byte{0}, std::byte{0x40}, std::byte{0}, std::byte{1}});
        transfer(fd, bytes, true);
      },
      [](const Value &, unsigned calls) {
        check(calls == 1, "no request after broken transport");
        return function("oxcart_state", "model",
                        Value::object({{"document", Value{"doc-7"}}}));
      },
      [&](RetainedState &root, ContextStore &, unsigned calls) {
        check(calls == 1, "one request before broken transport");
        bool succeeded = false;
        for (const auto &fact : root.committed_facts())
          if (const auto *event =
                  std::get_if<AttemptObservationEvent>(&fact.event.body);
              event && hex_identity(event->attempt.bytes()) == completed_call &&
              event->phase == AttemptPhase::terminal)
            succeeded = event->disposition == AttemptDisposition::success;
        check(succeeded, "received success must settle before later malformed frame");
      },
      true);
}
void exact_integers() {
  auto definition = field(catalog(), "catalog").array().front();
  assign(definition, "arguments",
         Value::object({{"kind", Value{"object"}},
                        {"fields", Value{Value::Array{
                                       typed_field("revision", scalar("integer"))}}}}));
  const HostToolCatalog tools{Value{Value::Array{definition}}};
  for (const auto &number :
       {Number{"1e17"}, Number{"-1e40"}, Number{"18446744073709551616"}, Number{1e20}})
    tools.validate_arguments("oxcart_state",
                             Value::object({{"revision", Value{number}}}));
}
void oversized_call(const std::filesystem::path &directory) {
  session(
      directory,
      [&](int fd, const auto &path) {
        open(fd, path);
        turn(fd, "turn");
        const auto call = until(fd, "host_call", "turn");
        check(string_field(field(call, "arguments"), "document") == "doc-7",
              "oversized call never dispatched; corrected call arrives");
        send(fd, reply(call, {.id = "corrected", .outcome = "success"},
                       Value::object({{"reading_path", Value{"preserved-reading"}}})));
        check(string_field(until(fd, "final", "turn"), "outcome") == "completed",
              "local frame refusal permits corrected work");
        send(fd, packet("close", "close"));
        (void)until(fd, "closed", "close");
      },
      [](const Value &request, unsigned calls) {
        if (calls == 1)
          return function(
              "oxcart_state", "oversized-model",
              Value::object(
                  {{"document", Value{std::string(
                                    ApplicationConnection::max_packet_bytes, 'x')}}}));
        if (calls == 2) {
          const auto &input = field(request, "input").array();
          const auto &result = field(input.back(), "output");
          check(string_field(result, "outcome") == "error" &&
                    string_field(field(result, "result"), "effect_outcome") ==
                        "not_started",
                "oversized control frame is a definite local refusal");
          return function("oxcart_state", "corrected-model",
                          Value::object({{"document", Value{"doc-7"}}}));
        }
        check(calls == 3, "only corrected host effect");
        return answer();
      },
      [](RetainedState &, ContextStore &, unsigned calls) {
        check(calls == 3, "corrected call continued");
      });
}
void invalid_open(Value offer) {
  int descriptors[2]{};
  check(::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) == 0, "open socketpair");
  auto opening = packet("open", "open");
  opening.object().emplace_back("session", Value{"unused"});
  opening.object().emplace_back("host_tools", std::move(offer));
  send(descriptors[1], opening);
  ApplicationConnection app{{.input = descriptors[0], .output = descriptors[0]}};
  refused([&] { (void)app.open(); }, "invalid/unimplemented negotiation refused");
  (void)::close(descriptors[0]);
  (void)::close(descriptors[1]);
}
void installation(const std::filesystem::path &directory) {
  invalid_open(Value{false});
  auto unsupported = catalog();
  assign(unsupported, "version", Value{Number{2}});
  invalid_open(unsupported);
  std::filesystem::create_directory(directory);
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
  IdentityBytes environment{}, stream{};
  environment[0] = std::byte{1};
  stream[0] = std::byte{2};
  auto root = unwrap(
      RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(directory.string()))),
                            "audit",
                            {unwrap(EnvironmentId::from_bytes(environment)),
                             unwrap(AuditStreamId::from_bytes(stream)),
                             1,
                             {8 * 1024 * 1024, 24 * 1024 * 1024},
                             std::nullopt},
                            {128 * 1024 * 1024, 10000}));
  AuditLog log{*root};
  ContextStore context{log};
  Provider provider;
  CodingEngine engine{log, context, provider, "test-model"};
  unsigned callbacks = 0;
  const HostToolCallback callback = [&](const HostToolCall &call) {
    ++callbacks;
    check(call.name == "oxcart_state", "only negotiated operation callback");
    return Value::object(
        {{"outcome", Value{"success"}},
         {"result", Value::object({{"reading_path", Value{"original"}}})}});
  };
  auto definition = field(catalog(), "catalog").array().front();
  assign(definition, "name", Value{"exec"});
  refused(
      [&] {
        engine.host_tools(HostToolCatalog{Value{Value::Array{definition}}}, callback);
      },
      "native tool collision refused");
  engine.host_tools(HostToolCatalog{field(catalog(), "catalog")}, callback);
  const auto lua = Value::object(
      {{"definition",
        Value::object(
            {{"name", Value{"oxcart_state"}},
             {"description", Value{"collision"}},
             {"parameters", Value::object({{"type", Value{"object"}},
                                           {"properties", Value::object({})},
                                           {"required", Value{Value::Array{}}},
                                           {"additionalProperties", Value{false}}})},
             {"source", Value{"return args"}}})}});
  check(string_field(engine.operator_call("tool_define", lua), "error") == "conflict",
        "Lua cannot replace connected host tool");
  check(string_field(engine.operator_call("oxcart_state", Value::object({})),
                     "error") == "invalid_range" &&
            callbacks == 0,
        "invalid arguments dispatch nothing");
  const auto arguments = Value::object({{"document", Value{"doc-7"}}});
  check(string_field(engine.operator_call("oxcart_state", arguments), "outcome") ==
                "success" &&
            callbacks == 1,
        "ordinary native tool callback");
  engine.detach_host_tools();
  check(string_field(engine.operator_call("oxcart_state", arguments), "error") ==
                "unsupported" &&
            callbacks == 1,
        "detached callback never revives");
  check(!engine.operator_call("tool_define", lua).find("error"),
        "Lua name available after detach");
  refused(
      [&] {
        engine.host_tools(HostToolCatalog{field(catalog(), "catalog")}, callback);
      },
      "restored/effective Lua collision refused");
  bool bound = false;
  for (const auto &fact : root->committed_facts())
    if (const auto *event = std::get_if<DecisionEvent>(&fact.event.body)) {
      const auto meta = unwrap(decode_packet(event->continuation));
      if (const auto *source = meta.find("host_catalog"))
        bound = !source->string().empty();
    }
  check(bound, "native admission retains catalog source linkage");
}
} // namespace
int main(int argc, char **argv) {
  char temporary[] = "/tmp/blackbird-host-tools-XXXXXX";
  const auto path = ::mkdtemp(temporary);
  if (!path)
    return 2;
  try {
    if (argc == 2) {
      if (std::string_view{argv[1]} == "integer")
        exact_integers();
      else if (std::string_view{argv[1]} == "oversize")
        oversized_call(std::filesystem::path{path} / "oversize");
      else
        received_before_bad_frame(std::filesystem::path{path} / "received");
      std::filesystem::remove_all(path);
      return 0;
    }
    installation(std::filesystem::path{path} / "installation");
    catalog_checks();
    exact_integers();
    oversized_call(std::filesystem::path{path} / "oversize");
    received_before_bad_frame(std::filesystem::path{path} / "received");
    circuit(std::filesystem::path{path} / "circuit");
    for (const auto *control : {"cancel", "steer", "close", "eof"})
      control_case(std::filesystem::path{path} / control, control);
    for (const auto *outcome : {"error", "cancelled", "unknown"})
      outcomes(std::filesystem::path{path} / outcome, outcome);
    std::filesystem::remove_all(path);
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << " in " << path << '\n';
    return 1;
  }
}
