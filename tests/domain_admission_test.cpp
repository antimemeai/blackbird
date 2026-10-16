#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
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
        bytes.find("program_modules") != std::string_view::npos) {
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

void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error{why};
}
template <class T> T identity(unsigned char v) {
  IdentityBytes b{};
  b[0] = std::byte{v};
  return unwrap(T::from_bytes(b));
}
Value parse(std::string_view s) { return unwrap(parse_json(s)); }
class Provider final : public CodingProvider {
public:
  std::string operation, resource;
  Value args, refusal;
  unsigned calls = 0;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    Value::Array output;
    if (calls == 1)
      output.push_back(Value::object({{"type", Value{"function_call"}},
                                      {"call_id", Value{"domain-refusal"}},
                                      {"name", Value{operation}},
                                      {"arguments", Value{unwrap(dump_json(args))}}}));
    else if (calls == 2) {
      for (const auto &item : field(request, "input").array())
        if (item.find("call_id") && field(item, "call_id") == Value{"domain-refusal"} &&
            field(item, "type") == Value{"function_call_output"})
          refusal = field(item, "output");
      check(refusal.find("resource") && string_field(refusal, "resource") == resource &&
                string_field(refusal, "error") == "capacity" &&
                string_field(refusal, "effect_outcome") == "not_started" &&
                !string_field(refusal, "attempt").empty(),
            "provider missed exact ordinary domain refusal");
      output.push_back(Value::object({{"type", Value{"function_call"}},
                                      {"call_id", Value{"later-read"}},
                                      {"name", Value{"context_stats"}},
                                      {"arguments", Value{"{}"}}}));
    } else {
      check(calls == 3, "unexpected provider repeat");
      bool read = false;
      for (const auto &item : field(request, "input").array())
        if (item.find("call_id") && field(item, "call_id") == Value{"later-read"} &&
            field(item, "type") == Value{"function_call_output"}) {
          read = true;
          check(!field(item, "output").find("error"),
                "ordinary refusal fenced later read");
        }
      check(read, "later function output absent");
    }
    auto response = Value::object({{"output", Value{std::move(output)}}});
    capture(unwrap(dump_json(response)));
    return response;
  }
};
void exercise(const std::string &mode) {
  char temporary[] = "/tmp/bb-domain-refusal-XXXXXX";
  const auto path = ::mkdtemp(temporary);
  check(path, "fixture directory");
  struct Remove {
    std::filesystem::path p;
    ~Remove() { std::filesystem::remove_all(p); }
  } remove{path};
  CaptureFault fault;
  auto root = unwrap(RetainedState::create(
      std::make_unique<FaultDirectory>(std::make_unique<NativeJournalDirectory>(
                                           unwrap(NativeJournalDirectory::open(path))),
                                       fault),
      "audit",
      {identity<EnvironmentId>(61),
       identity<AuditStreamId>(62),
       3,
       {1024 * 1024, 4 * 1024 * 1024},
       std::nullopt},
      {256 * 1024 * 1024, 50000}));
  AuditLog log{*root};
  ContextStore context{log};
  Provider provider;
  CodingEngine engine{log, context, provider, "fixture"};
  std::atomic<unsigned> launches = 0;
  engine.participant_transport = [&](const Value &, const ColleagueCapture &,
                                     const std::function<bool()> &stop,
                                     const RawAdapterContext *) -> Value {
    ++launches;
    while (!stop())
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    throw Error{ErrorCode::interrupted};
  };
  std::string run;
  Value before;
  if (mode == "participant" || mode == "inbox") {
    engine.operator_call("participant_configure",
                         Value::object({{"concurrency", Value{Number{1}}}}));
    auto req = parse(
        R"({"request_id":"initial","from":"main","to":"alpha","provider":"openai","model":"fake","task":"audit","context":[],"profile":{"name":"context-only","provenance":"test","timeout_seconds":10,"tools":"none","requests":1}})");
    auto args = Value::object({{"request", req}});
    run = string_field(engine.operator_call("participant_start", args), "run_id");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (launches.load() != 1) {
      check(std::chrono::steady_clock::now() < deadline, "first participant not live");
      std::this_thread::yield();
    }
    provider.operation = "participant_start";
    provider.resource = "participant_concurrency";
    provider.args = args;
    if (mode == "inbox") {
      for (unsigned i = 0; i < 16; ++i) {
        auto accepted = engine.operator_call(
            "participant_send",
            Value::object({{"run_id", Value{run}},
                           {"message_id", Value{"direction-" + std::to_string(i)}},
                           {"from", Value{"operator"}},
                           {"text", Value{"direction"}}}));
        check(!accepted.find("error"), "seed direction refused");
      }
      provider.operation = "participant_send";
      provider.resource = "participant_pending_directions";
      provider.args = Value::object({{"run_id", Value{run}},
                                     {"message_id", Value{"overflow"}},
                                     {"from", Value{"operator"}},
                                     {"text", Value{"not accepted"}}});
    }
    before = engine.operator_call("participant_read",
                                  Value::object({{"run_id", Value{run}}}));
  } else if (mode == "tasks") {
    for (unsigned batch = 0; batch < 32; ++batch) {
      Value::Array ops;
      for (unsigned i = 0; i < 64; ++i)
        ops.push_back(Value::object({{"op", Value{"add"}}, {"title", Value{"x"}}}));
      engine.tasks().edit(
          Value::object({{"op_id", Value{"seed-" + std::to_string(batch)}},
                         {"base", Value{Number{engine.tasks().state().revision()}}},
                         {"ops", Value{std::move(ops)}}}));
    }
    check(engine.tasks().state().size() == 2048, "full task seed");
    before = engine.tasks().snapshot();
    provider.operation = "tasks_edit";
    provider.resource = "task_items";
    provider.args = Value::object(
        {{"op_id", Value{"offered"}},
         {"base", Value{Number{engine.tasks().state().revision()}}},
         {"ops", Value{Value::Array{Value::object(
                     {{"op", Value{"add"}}, {"title", Value{"overflow"}}})}}}});
  } else {
    provider.operation = "program_config";
    provider.resource = mode == "encoding" ? "program_encoding" : "program_modules";
    Value::Array modules;
    const unsigned count = mode == "encoding" ? 5 : 33;
    for (unsigned i = 0; i < count; ++i)
      modules.push_back(Value::object(
          {{"name", Value{"m" + std::to_string(i)}},
           {"source",
            Value{mode == "encoding" ? "--" + std::string(15000, 'x') + "\nreturn {}"
                                     : "return {}"}}}));
    provider.args = Value::object(
        {{"proposal", Value::object({{"model", Value{"fixture"}},
                                     {"effort", Value{"medium"}},
                                     {"modules", Value{std::move(modules)}}})}});
    before = engine.operator_call("program_config", Value::object({}));
  }
  const bool capture = mode == "capture";
  fault.armed = capture;
  std::optional<Error> stopped;
  try {
    engine.turn({"exercise actual domain refusal", read_file("programs/turn.lua")});
  } catch (const Error &e) {
    stopped = e;
  }
  if (capture) {
    check(stopped && fault.fired && provider.calls == 1 &&
              root->state() != JournalWriterState::live,
          "refusal-result write failure was treated as recoverable");
    bool blocked = false;
    try {
      engine.operator_call("context_stats", Value::object({}));
    } catch (const Error &) {
      blocked = true;
    }
    check(blocked, "blocked writer admitted later operation");
    engine.shutdown_participants();
    return;
  }
  check(!stopped, "ordinary owner refusal aborted turn");
  check(provider.calls == 3, "refusal prevented exact provider continuation");
  unsigned refusals = 0;
  for (const auto &fact : root->committed_facts()) {
    if (const auto *r = std::get_if<ApplicationRecordEvent>(&fact.event.body);
        r && r->channel == ApplicationChannel::log) {
      auto packet = unwrap(read_packet(r->payload));
      check(string_field(packet, "label") != "capacity.stop",
            "domain refusal falsely latched capture stop");
      if (string_field(packet, "label") == "operation.result" &&
          string_field(field(packet, "metadata"), "operation") == provider.operation)
        for (const auto &dependency : fact.event.dependencies) {
          auto value = decode_packet(unwrap(root->source(dependency)));
          if (value.has_value() && value.value().find("resource") &&
              string_field(value.value(), "resource") == provider.resource)
            ++refusals;
        }
    }
  }
  check(refusals == 1, "domain refusal original not retained exactly once");
  if (mode == "participant" || mode == "inbox") {
    auto after = engine.operator_call("participant_read",
                                      Value::object({{"run_id", Value{run}}}));
    check(field(after, "state") == field(before, "state") && launches.load() == 1,
          "refusal stopped or relaunched existing participant");
    if (mode == "inbox")
      check(field(after, "queued_directions") == field(before, "queued_directions"),
            "refusal changed accepted directions");
  } else if (mode == "tasks")
    check(engine.tasks().snapshot() == before, "refused edit changed task state");
  else {
    const auto after = engine.operator_call("program_config", Value::object({}));
    for (const auto key : {"revision", "effective", "pending"})
      check(field(after, key) == field(before, key),
            "refused config changed pending/effective source");
  }
  engine.shutdown_participants();
  std::cout << mode << ": exact refusal, continued reads and retained state pass\n";
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2, "select case");
    exercise(argv[1]);
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << ":" << e.detail << '\n';
    return 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
