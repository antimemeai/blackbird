#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <signal.h>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
struct CaptureFault {
  bool armed = false, fired = false;
};
class FaultFile final : public JournalFile {
  std::unique_ptr<JournalFile> file_;
  CaptureFault &fault_;

public:
  FaultFile(std::unique_ptr<JournalFile> file, CaptureFault &fault)
      : file_(std::move(file)), fault_(fault) {}
  Result<std::size_t> read_at(std::uint64_t offset, MutableByteView out) override {
    return file_->read_at(offset, out);
  }
  Result<std::size_t> write_at(std::uint64_t offset, ByteView input) override {
    const std::string_view bytes{reinterpret_cast<const char *>(input.data()),
                                 input.size()};
    if (fault_.armed && !fault_.fired &&
        bytes.find("not_started") != std::string_view::npos &&
        bytes.find("live_commands") != std::string_view::npos) {
      fault_.fired = true;
      return Result<std::size_t>::failure({ErrorCode::capacity});
    }
    return file_->write_at(offset, input);
  }
  Result<std::uint64_t> extent() override { return file_->extent(); }
  Result<void> synchronize(SyncStrength strength) override {
    return file_->synchronize(strength);
  }
  Result<void> lock_writer() override { return file_->lock_writer(); }
  void release_writer() noexcept override { file_->release_writer(); }
};
class FaultDirectory final : public JournalDirectory {
  std::unique_ptr<JournalDirectory> directory_;
  CaptureFault &fault_;
  Result<std::unique_ptr<JournalFile>>
  decorate(Result<std::unique_ptr<JournalFile>> result) {
    if (!result.has_value())
      return Result<std::unique_ptr<JournalFile>>::failure(result.error());
    return Result<std::unique_ptr<JournalFile>>::success(
        std::make_unique<FaultFile>(std::move(result).value(), fault_));
  }

public:
  FaultDirectory(std::unique_ptr<JournalDirectory> directory, CaptureFault &fault)
      : directory_(std::move(directory)), fault_(fault) {}
  Result<std::unique_ptr<JournalFile>>
  create_exclusive(std::string_view name) override {
    return decorate(directory_->create_exclusive(name));
  }
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view name,
                                                     FileAccess access) override {
    return decorate(directory_->open_existing(name, access));
  }
  Result<void> synchronize_directory(SyncStrength strength) override {
    return directory_->synchronize_directory(strength);
  }
  Result<void> replace_file(std::string_view from, std::string_view to) override {
    return directory_->replace_file(from, to);
  }
  Result<void> store_diagnostic(std::string_view name, ByteView bytes,
                                std::uint64_t now) override {
    return directory_->store_diagnostic(name, bytes, now);
  }
};
void check(bool actual, const char *message) {
  if (!actual) {
    std::cerr << message << '\n';
    throw std::runtime_error(message);
  }
}
template <class T> T identity(unsigned char value) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{value};
  return unwrap(T::from_bytes(bytes));
}
OperationAttemptId attempt_id(const std::string &text) {
  check(text.size() == 32, "missing native attempt identity");
  IdentityBytes bytes{};
  for (std::size_t i = 0; i < bytes.size(); ++i)
    bytes[i] = static_cast<std::byte>(std::stoul(text.substr(i * 2, 2), nullptr, 16));
  return unwrap(OperationAttemptId::from_bytes(bytes));
}
class Provider final : public CodingProvider {
public:
  std::string marker, resource;
  unsigned calls = 0;
  Value refusal;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    Value::Array output;
    if (calls == 1) {
      output.push_back(Value::object(
          {{"type", Value{"function_call"}},
           {"call_id", Value{"refused-launch"}},
           {"name", Value{"exec"}},
           {"arguments",
            Value{unwrap(dump_json(Value::object(
                {{"argv", Value{Value::Array{
                              Value{"/bin/sh"}, Value{"-c"},
                              Value{"printf WRONG > '" + marker + "'"}}}}})))}}}));
    } else {
      check(calls == 2, "refusal caused unexpected provider retry");
      for (const auto &item : field(request, "input").array()) {
        const auto *call = item.find("call_id");
        const auto *type = item.find("type");
        if (call && *call == Value{"refused-launch"} && type &&
            *type == Value{"function_call_output"})
          refusal = field(item, "output");
      }
      check(refusal.find("resource") && string_field(refusal, "resource") == resource &&
                string_field(refusal, "effect_outcome") == "not_started" &&
                string_field(refusal, "error") == "capacity" &&
                !refusal.find("output_ref") &&
                field(refusal, "limit") == Value{Number{1}} &&
                field(refusal, "count") == Value{Number{1}} &&
                !string_field(refusal, "message").empty(),
            "provider did not receive actionable known-not-started refusal");
    }
    auto response = Value::object({{"output", Value{std::move(output)}}});
    capture(unwrap(dump_json(response)));
    return response;
  }
};
void scenario(const std::filesystem::path &directory, bool live,
              bool capture_failure = false) {
  std::filesystem::create_directories(directory);
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
  CaptureFault fault;
  auto root = unwrap(
      RetainedState::create(std::make_unique<FaultDirectory>(
                                std::make_unique<NativeJournalDirectory>(unwrap(
                                    NativeJournalDirectory::open(directory.string()))),
                                fault),
                            "audit",
                            {identity<EnvironmentId>(1),
                             identity<AuditStreamId>(2),
                             3,
                             {1024 * 1024, 4 * 1024 * 1024},
                             std::nullopt},
                            {32 * 1024 * 1024, 20000}));
  AuditLog log{*root};
  ContextStore context{log};
  Provider provider;
  provider.marker = (directory / "forbidden").string();
  provider.resource = live ? "live_commands" : "command_records";
  CodingEngine engine{log, context, provider, "fixture"};
  engine.operator_call("process", Value::object({{"op", Value{"configure"}},
                                                 {"max_live", Value{Number{1}}},
                                                 {"max_records", Value{Number{1}}}}));
  auto args = Value::object(
      {{"argv", Value{Value::Array{Value{"/bin/sh"}, Value{"-c"},
                                   Value{live ? "sleep 10" : "printf ORIGINAL"}}}},
       {"timeout_seconds", Value{Number{15}}}});
  if (live)
    args.object().emplace_back("yield_ms", Value{Number{0}});
  const auto first = engine.operator_call("exec", args);
  const auto job = string_field(first, "job_id");
  if (live) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (!engine.command_jobs()
                ->read(Value::object({{"job_id", Value{job}}}))
                .find("pid")) {
      check(std::chrono::steady_clock::now() < deadline,
            "original command did not launch before refusal scenario");
      engine.poll_commands();
      std::this_thread::yield();
    }
  }
  const auto original =
      engine.command_jobs()->read(Value::object({{"job_id", Value{job}}}));
  if (!live)
    engine.command_jobs()->request(
        Value::object({{"op", Value{"stop"}}, {"job_id", Value{job}}}));
  // Keep the exact settled row pinned until dispatch reaches start, rather than
  // allowing the engine's ordinary mailbox drain to remove the protection.
  bool queued = !live;
  engine.effect_policy = [&](std::string_view name, const Value &) {
    if (queued && name == "exec") {
      engine.command_jobs()->request(
          Value::object({{"op", Value{"stop"}}, {"job_id", Value{job}}}));
      queued = false;
    }
  };
  std::string activity;
  engine.operation_outcome = [&](std::string_view text,
                                 AttemptDisposition disposition) {
    if (text.starts_with("exec ")) {
      activity = text;
      if (!capture_failure)
        check(disposition == AttemptDisposition::failure,
              "admission refusal reported an unknown external effect");
    }
  };
  engine.notice = [](std::string_view text) { std::cerr << text; };
  if (capture_failure) {
    fault.armed = true;
    bool stopped = false;
    try {
      engine.turn(
          {"refusal result cannot be retained", read_file("programs/turn.lua")});
    } catch (const Error &) {
      stopped = true;
    }
    check(fault.fired && stopped && provider.calls == 1 &&
              root->state() != JournalWriterState::live &&
              !std::filesystem::exists(provider.marker),
          "refused-result capture failure was returned as recoverable");
    bool later_blocked = false;
    try {
      engine.operator_call(
          "exec",
          Value::object({{"argv", Value{Value::Array{Value{"/bin/sh"}, Value{"-c"},
                                                     Value{"printf WRONG > '" +
                                                           provider.marker + "'"}}}}}));
    } catch (const Error &) {
      later_blocked = true;
    }
    check(later_blocked && !std::filesystem::exists(provider.marker),
          "capture failure admitted later effects");
    return;
  }
  engine.turn({"exercise refused command", read_file("programs/turn.lua")});
  check(provider.calls == 2 && !std::filesystem::exists(provider.marker),
        "refused command launched or aborted normal provider continuation");
  check(activity.find("not started") != std::string::npos,
        "operator activity did not identify a refused launch");
  if (live) {
    const auto retained =
        engine.command_jobs()->read(Value::object({{"job_id", Value{job}}}));
    const auto pid =
        static_cast<pid_t>(std::stol(field(retained, "pid").number().text()));
    check(field(retained, "pid") == field(original, "pid") &&
              field(retained, "native_custody_pending") == Value{true} &&
              field(retained, "leader_exited") == Value{false} && ::kill(pid, 0) == 0,
          "refusal stopped or replaced the original live command");
  }
  unsigned refused = 0;
  for (const auto &fact : root->committed_facts()) {
    if (const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
        record && record->channel == ApplicationChannel::log) {
      const auto packet = unwrap(read_packet(record->payload));
      check(string_field(packet, "label") != "capacity.stop",
            "ordinary command refusal raised a sticky capture-capacity stop");
      if (string_field(packet, "label") == "operation.result" &&
          string_field(field(packet, "metadata"), "operation") == "exec") {
        for (const auto &dependency : fact.event.dependencies) {
          const auto bytes = unwrap(root->source(dependency));
          const auto value = decode_packet(bytes);
          if (value.has_value() && value.value().find("resource")) {
            const auto id = string_field(field(packet, "metadata"), "attempt");
            const auto state = unwrap(root->attempt(attempt_id(id)));
            check(state.observation &&
                      state.observation->phase == AttemptPhase::terminal &&
                      state.observation->disposition == AttemptDisposition::failure,
                  "retained refusal terminal did not preserve known failure");
            ++refused;
          }
        }
      }
    }
  }
  check(refused == 1, "refused result was not retained exactly once");
  engine.operation_outcome = {};
  engine.command_jobs()->shutdown();
  engine.poll_commands();
  const auto read = engine.operator_call("read_process_output",
                                         Value::object({{"output_ref", Value{job}}}));
  if (!live)
    check(string_field(read, "output") == "ORIGINAL",
          "refusal destroyed earlier retained output");
  const auto done = engine.operator_call(
      "exec",
      Value::object({{"argv", Value{Value::Array{Value{"/bin/sh"}, Value{"-c"},
                                                 Value{"printf RECOVERED"}}}}}));
  check(string_field(done, "output") == "RECOVERED" &&
            field(done, "exit_code") == Value{Number{0}},
        "ordinary refusal left future command work fenced");
}
} // namespace
int main() {
  try {
    char pattern[] = "/tmp/bb-command-admission-XXXXXX";
    const auto *temporary = ::mkdtemp(pattern);
    check(temporary != nullptr, "could not create command admission fixture");
    struct Cleanup {
      std::filesystem::path path;
      ~Cleanup() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
      }
    } cleanup{temporary};
    scenario(cleanup.path / "live", true);
    scenario(cleanup.path / "resident", false);
    scenario(cleanup.path / "capture-failure", true, true);
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
