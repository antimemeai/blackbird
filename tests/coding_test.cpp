#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_runtime.hpp"
#include <cerrno>
#include <chrono>
#include <iostream>
#include <set>
#include <unistd.h>
using namespace blackbird;
template <class T> T present(std::optional<T> value) {
  if (!value)
    throw std::runtime_error("expected present optional in fixture");
  return std::move(*value);
}

template <class T> T id(unsigned char n) {
  IdentityBytes b{};
  b[0] = std::byte{n};
  return unwrap(T::from_bytes(b));
}
std::vector<std::byte> diagnostic_bytes(RetainedState &root, const Value &packet) {
  const auto now = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  return unwrap(root.read_diagnostic(
      string_field(field(packet, "metadata"), "diagnostic_file"), now));
}
class ManagedToolProvider final : public CodingProvider {
public:
  Value proposal;
  Value::Array last_input;
  int calls = 0;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    last_input = field(request, "input").array();
    ++calls;
    Value::Array output;
    if (calls == 1)
      output.push_back(Value::object(
          {{"type", Value{"function_call"}},
           {"call_id", Value{"managed-real"}},
           {"name", Value{"context_manage"}},
           {"arguments",
            Value{unwrap(dump_json(Value::object({{"proposal", proposal}})))}}}));
    if (calls == 2)
      output.push_back(Value::object({{"type", Value{"function_call"}},
                                      {"call_id", Value{"managed-second"}},
                                      {"name", Value{"context_stats"}},
                                      {"arguments", Value{"{}"}}}));
    auto response = Value::object({{"output", Value{std::move(output)}}});
    capture(unwrap(dump_json(response)));
    return response;
  }
};
class Scripted final : public CodingProvider {
public:
  std::string path;
  int calls = 0;
  bool interrupt = false;
  std::function<void()> check_preview;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    if (interrupt) {
      capture("data: "
              "{\"type\":\"response.output_text.delta\",\"item_id\":\"partial\","
              "\"delta\":\"PARTIAL\"}\n\n");
      capture("data: incomplete\n");
      throw Error{ErrorCode::interrupted};
    }
    if (string_field(field(request, "reasoning"), "effort") != "medium")
      throw Error{ErrorCode::corrupt};
    const auto &input = field(request, "input").array();
    Value response;
    if (calls == 1) {
      response = Value::object(
          {{"output",
            Value{Value::Array{
                Value::object({{"type", Value{"reasoning"}},
                               {"encrypted_content", Value{"opaque=="}}}),
                Value::object(
                    {{"type", Value{"function_call"}},
                     {"call_id", Value{"write-1"}},
                     {"name", Value{"write_file"}},
                     {"arguments",
                      Value{unwrap(dump_json(Value::object(
                          {{"path", Value{path}},
                           {"content", Value{"int answer = 42;\n"}}})))}}})}}}});
    } else {
      if (input.size() != 4 ||
          string_field(input[1], "encrypted_content") != "opaque==" ||
          string_field(input[3], "call_id") != "write-1" ||
          string_field(input[3], "type") != "function_call_output")
        throw Error{ErrorCode::corrupt};
      response = Value::object(
          {{"output", Value{Value::Array{Value::object(
                          {{"type", Value{"message"}},
                           {"id", Value{"m1"}},
                           {"role", Value{"assistant"}},
                           {"content", Value{Value::Array{Value::object(
                                           {{"type", Value{"output_text"}},
                                            {"text", Value{"done"}}})}}}})}}}});
    }
    if (calls == 2) {
      capture("data: "
              "{\"type\":\"response.output_text.delta\",\"item_id\":\"m1\",\"delta\":"
              "\"do\"}\n\n");
      if (check_preview)
        check_preview();
    }
    capture(unwrap(dump_json(response)));
    return response;
  }
};
class StoppedTool final : public CodingProvider {
public:
  int calls = 0;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    Value item;
    if (calls == 1)
      item = Value::object(
          {{"type", Value{"function_call"}},
           {"call_id", Value{"stop-tool"}},
           {"name", Value{"exec"}},
           {"arguments",
            Value{"{\"command\":\"printf BEFORE_; printf STOP; sleep 30\"}"}}});
    else {
      const auto &input = field(request, "input").array();
      std::size_t matched = 0;
      for (const auto &old : input) {
        const auto *type = old.find("type");
        if (type && type->string() == "function_call_output" &&
            string_field(old, "call_id") == "stop-tool") {
          if (field(old, "output").find("error") == nullptr ||
              string_field(field(old, "output"), "error") != "turn_interrupted")
            throw Error{ErrorCode::corrupt};
          ++matched;
        }
      }
      if (matched != 1)
        throw Error{ErrorCode::corrupt};
      item = Value::object(
          {{"type", Value{"message"}},
           {"content",
            Value{Value::Array{Value::object(
                {{"type", Value{"output_text"}}, {"text", Value{"CONTINUED"}}})}}}});
    }
    const auto result =
        Value::object({{"output", Value{Value::Array{std::move(item)}}}});
    capture(unwrap(dump_json(result)));
    return result;
  }
};
class TimeoutRecoveryProvider final : public CodingProvider {
public:
  std::string marker;
  int calls = 0;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    Value::Array output;
    if (calls == 1) {
      const auto args =
          Value::object({{"command", Value{"printf X >> '" + marker +
                                           "'; printf PARTIAL; exec sleep 10"}},
                         {"timeout_seconds", Value{Number{"1"}}}});
      output.push_back(Value::object({{"type", Value{"function_call"}},
                                      {"call_id", Value{"deadline-first"}},
                                      {"name", Value{"exec"}},
                                      {"arguments", Value{unwrap(dump_json(args))}}}));
    } else if (calls == 2) {
      const auto &last = field(request, "input").array().back();
      const auto result = field(last, "output");
      if (string_field(last, "call_id") != "deadline-first" ||
          !std::get<bool>(field(result, "timed_out").value()) ||
          string_field(result, "output_ref").empty())
        throw Error{ErrorCode::corrupt};
      // A new approach, rather than automatically replaying the prior effect.
      output.push_back(
          Value::object({{"type", Value{"function_call"}},
                         {"call_id", Value{"deadline-next"}},
                         {"name", Value{"exec"}},
                         {"arguments", Value{R"({"command":"printf RECOVERED"})"}}}));
    } else if (calls == 3) {
      const auto &last = field(request, "input").array().back();
      const auto result = field(last, "output");
      if (string_field(last, "call_id") != "deadline-next" ||
          string_field(result, "output") != "RECOVERED")
        throw Error{ErrorCode::corrupt};
    } else {
      throw Error{ErrorCode::corrupt};
    }
    auto result = Value::object({{"output", Value{std::move(output)}}});
    capture(unwrap(dump_json(result)));
    return result;
  }
};
class UsageProvider final : public CodingProvider {
public:
  std::size_t bytes = 0;
  bool usage = true;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &) override {
    bytes = unwrap(encode_packet_string(request)).size();
    auto result = Value::object({{"output", Value{Value::Array{}}}});
    if (usage)
      result.object().emplace_back(
          "usage",
          unwrap(parse_json(
              R"({"input_tokens":123,"output_tokens":4,"total_tokens":127,"secret":"DO_NOT_SHOW","output_tokens_details":{"reasoning_tokens":2,"secret":"HIDDEN"}})")));
    return result;
  }
};
Value invoke(CodingEngine &engine, std::string_view name, const Value &args) {
  std::string shown;
  engine.display = [&](std::string_view text) { shown += text; };
  const auto code = "local r = arco.call('" + std::string{name} +
                    "', arco.json.decode([==[" + unwrap(dump_json(args)) +
                    "]==])); arco.display(arco.json.encode(r))";
  engine.turn({"", code});
  engine.display = {};
  return unwrap(parse_json(shown));
}
void atomic_admission_test(const std::string &path, int mode) {
  std::filesystem::create_directory(path);
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  const JournalHeader h{id<EnvironmentId>(31),
                        id<AuditStreamId>(32),
                        3,
                        {mode == 2 ? 7300U : 8192U, mode == 2 ? 7400U : 65536U},
                        std::nullopt};
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                       unwrap(NativeJournalDirectory::open(path))),
                                   "audit", h, {64 * 1024 * 1024, 10000}));
  AuditLog log{*root};
  ContextStore context{log};
  UsageProvider provider;
  CodingEngine engine{log, context, provider, "test"};
  engine.turn({"", "assert(rawequal(blackbird, arco)); assert(blackbird.module == "
                   "arco.module); arco.call('context_stats', {})"});
  const auto counts = [&] {
    std::array<std::size_t, 3> result{};
    for (const auto &fact : root->committed_facts()) {
      result[0] += std::holds_alternative<DecisionEvent>(fact.event.body);
      result[1] += std::holds_alternative<InvocationEvent>(fact.event.body);
      result[2] += std::holds_alternative<AttemptAdmissionEvent>(fact.event.body);
    }
    return result;
  };
  std::array<std::size_t, 3> before{};
  const auto marker = path + "/effect";
  const auto args =
      Value::object({{"path", Value{marker}},
                     {"content", Value{std::string(mode == 2 ? 7000 : 2048, 'x')}}});
  engine.status = [&](std::string_view description) {
    if (!description.starts_with("write_file"))
      return;
    before = counts();
    if (mode != 2) {
      const auto usage = root->journal_usage();
      JournalCapacity credit{1, usage.remaining_records() - 2};
      if (mode == 0) {
        std::optional<DecisionEvent> prior;
        for (const auto &fact : root->committed_facts())
          if (const auto *d = std::get_if<DecisionEvent>(&fact.event.body))
            prior = *d;
        if (!prior)
          throw Error{ErrorCode::corrupt};
        const auto input = unwrap(dump_json(args));
        const auto metadata = unwrap(encode_packet(Value::object(
            {{"operation", Value{"write_file"}},
             {"input_binding", Value{"invocation-v1"}},
             {"invocation", Value{hex_identity(id<InvocationId>(52).bytes())}},
             {"revision", Value{context.head()}},
             {"generation", Value{hex_identity(prior->definition.bytes())}}})));
        const auto raw = std::as_bytes(std::span{metadata.data(), metadata.size()});
        const auto in = std::as_bytes(std::span{input.data(), input.size()});
        const auto encoded_size = [&](const RetainedEvent &event) {
          return unwrap(encode_retained_event(event, h.limits.max_payload)).size();
        };
        const auto decision = encoded_size({{},
                                            DecisionEvent{id<DecisionId>(51),
                                                          prior->actor,
                                                          prior->conversation,
                                                          prior->workflow,
                                                          prior->definition,
                                                          prior->context,
                                                          {id<InvocationId>(52)},
                                                          {raw.begin(), raw.end()}}});
        const auto invocation = encoded_size({{},
                                              InvocationEvent{id<InvocationId>(52),
                                                              id<DecisionId>(51),
                                                              prior->definition,
                                                              {in.begin(), in.end()}}});
        // Leave only the untimed decision/invocation footprint, less than the
        // complete timed three-record batch. No effect may start with a partial batch.
        const auto room = 2 * (56 + journal_frame_header_size) + decision + invocation;
        if (!usage.remaining_bytes() || present(usage.remaining_bytes()) <= room)
          throw Error{ErrorCode::corrupt};
        credit = {present(usage.remaining_bytes()) - room, 1};
      }
      unwrap(root->refresh_settlement(credit));
    }
  };

  bool refused = false;
  try {
    const auto program =
        mode == 2
            ? "local a=arco.json.decode([==[" +
                  unwrap(dump_json(Value::object({{"path", Value{marker}}}))) +
                  "]==]); a.content=string.rep('x',7000); arco.call('write_file',a)"
            : "arco.call('write_file', arco.json.decode([==[" +
                  unwrap(dump_json(args)) + "]==]))";
    engine.turn({"", program});
  } catch (const Error &error) {
    refused = error.code == ErrorCode::capacity;
  }
  const auto after_credit = root->protected_settlement();
  if (!refused || counts() != before || std::filesystem::exists(marker) ||
      provider.bytes != 0 || root->state() != JournalWriterState::live ||
      after_credit) {
    const auto actual = counts();
    std::cerr << "admission refusal mode=" << mode << " refused=" << refused
              << " committed deltas=" << actual[0] - before[0] << ','
              << actual[1] - before[1] << ',' << actual[2] - before[2] << '\n';
    throw Error{ErrorCode::corrupt};
  }
  engine.status = {};
  auto expected = counts();
  std::set<IdentityBytes> prior_ids;
  for (const auto &fact : root->committed_facts()) {
    if (const auto *d = std::get_if<DecisionEvent>(&fact.event.body))
      prior_ids.insert(d->decision.bytes());
    if (const auto *v = std::get_if<InvocationEvent>(&fact.event.body))
      prior_ids.insert(v->invocation.bytes());
    if (const auto *a = std::get_if<AttemptAdmissionEvent>(&fact.event.body))
      prior_ids.insert(a->attempt.bytes());
  }
  (void)invoke(
      engine, "write_file",
      Value::object({{"path", Value{marker}}, {"content", Value{"accepted"}}}));
  for (auto &value : expected)
    ++value;
  if (counts() != expected || read_file(marker) != "accepted")
    throw Error{ErrorCode::corrupt};
  const DecisionEvent *decision = nullptr;
  const InvocationEvent *invocation = nullptr;
  const AttemptAdmissionEvent *admission = nullptr;
  std::array<std::uint64_t, 3> sequence{};
  for (const auto &fact : root->committed_facts()) {
    if (const auto *d = std::get_if<DecisionEvent>(&fact.event.body)) {
      decision = d;
      sequence[0] = fact.record.sequence;
    }
    if (const auto *v = std::get_if<InvocationEvent>(&fact.event.body)) {
      invocation = v;
      sequence[1] = fact.record.sequence;
    }
    if (const auto *a = std::get_if<AttemptAdmissionEvent>(&fact.event.body)) {
      admission = a;
      sequence[2] = fact.record.sequence;
    }
  }
  if (!decision || !invocation || !admission ||
      invocation->decision != decision->decision ||
      admission->decision != decision->decision ||
      admission->invocation != invocation->invocation ||
      invocation->definition != decision->definition ||
      decision->planned_invocations != std::vector{invocation->invocation} ||
      prior_ids.contains(decision->decision.bytes()) ||
      prior_ids.contains(invocation->invocation.bytes()) ||
      prior_ids.contains(admission->attempt.bytes()) ||
      sequence[1] != sequence[0] + 1 || sequence[2] != sequence[1] + 1)
    throw Error{ErrorCode::corrupt};
  // Read only this small fixture; do not contend with the live journal lease.
  const auto retained = read_file(path + "/audit");
  const auto raw = std::as_bytes(std::span{retained.data(), retained.size()});
  std::array<bool, 3> found{};
  for (std::size_t offset = journal_header_size; offset != raw.size();) {
    const auto frame = unwrap(decode_journal_frame(raw.subspan(offset), h.limits));
    for (std::size_t i = 0; i != sequence.size(); ++i)
      if (frame.kind == FrameKind::semantic && frame.sequence == sequence[i]) {
        found[i] = true;
        if (frame.batch_first != sequence[0])
          throw Error{ErrorCode::corrupt};
      }
    offset += frame.encoded_size;
  }
  for (const auto present : found)
    if (!present)
      throw Error{ErrorCode::corrupt};
}

void retained_output_test(const std::string &path) {
  std::filesystem::create_directory(path);
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  const JournalHeader h{id<EnvironmentId>(11),
                        id<AuditStreamId>(12),
                        3,
                        {4 * 1024 * 1024, 16 * 1024 * 1024},
                        std::nullopt};
  const JournalCapacity capacity{64 * 1024 * 1024, 10000};
  std::string reference, timed_reference;
  const auto marker = path + "/count";
  {
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                         unwrap(NativeJournalDirectory::open(path))),
                                     "audit", h, capacity));
    AuditLog log{*root};
    ContextStore context{log};
    Scripted provider;
    CodingEngine engine{log, context, provider, "test"};
    auto result = invoke(
        engine, "exec",
        Value::object({{"command", Value{"printf X >> '" + marker +
                                         "'; printf abc; printf def >&2; exit 7"}},
                       {"output_max_bytes", Value{Number{"2"}}}}));
    if (string_field(result, "output") != "ab" ||
        field(result, "exit_code").number().text() != "7")
      throw Error{ErrorCode::corrupt};
    reference = string_field(result, "output_ref");
    const auto timed_result =
        invoke(engine, "exec",
               Value::object({{"command", Value{"printf PARTIAL; sleep 10"}},
                              {"timeout_seconds", Value{Number{"1"}}},
                              {"output_max_bytes", Value{Number{"0"}}}}));
    if (!std::get<bool>(field(timed_result, "timed_out").value()) ||
        string_field(timed_result, "error") != "io" ||
        string_field(timed_result, "effect_outcome") != "unknown")
      throw Error{ErrorCode::corrupt};
    for (const auto &fact : root->committed_facts()) {
      const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
      if (!record || record->channel != ApplicationChannel::log)
        continue;
      const auto packet = unwrap(read_packet(record->payload));
      if (string_field(packet, "label") == "process.output")
        timed_reference = string_field(field(packet, "metadata"), "attempt");
    }
  }
  {
    auto root =
        unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(
                                       unwrap(NativeJournalDirectory::open(path))),
                                   "audit", h, capacity));
    unwrap(root->confirm_recovery());
    recover_coding_session(*root);
    AuditLog log{*root};
    ContextStore context{log};
    Scripted provider;
    CodingEngine engine{log, context, provider, "test"};
    auto result = invoke(engine, "read_process_output",
                         Value::object({{"output_ref", Value{reference}},
                                        {"byte_start", Value{Number{"2"}}}}));
    if (string_field(result, "output") != "cdef" || read_file(marker) != "X" ||
        field(result, "output_bytes").number().text() != "6" || provider.calls != 0)
      throw Error{ErrorCode::corrupt};
    auto partial = invoke(engine, "read_process_output",
                          Value::object({{"output_ref", Value{timed_reference}}}));
    if (string_field(partial, "output") != "PARTIAL")
      throw Error{ErrorCode::corrupt};
    // The explorer must expose the actual unknown disposition and causal links,
    // rather than interpreting a terminal record as successful completion.
    bool unknown = false, linked = false;
    const auto end = log.root().fact_count();
    for (std::size_t cursor = 0; cursor < end; cursor += 64) {
      auto page =
          log.inspect(Value::object({{"cursor", Value{Number{std::to_string(cursor)}}},
                                     {"end", Value{Number{std::to_string(end)}}},
                                     {"count", Value{Number{"64"}}}}));
      for (const auto &row : field(page, "records").array()) {
        const auto *a = row.find("attempt");
        if (a && a->string() == timed_reference) {
          if (const auto *o = row.find("outcome"); o && o->string() == "unknown")
            unknown = true;
          if (row.find("invocation") && row.find("decision"))
            linked = true;
        }
      }
    }
    if (!unknown || !linked)
      throw Error{ErrorCode::corrupt};
    const std::string old_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    const auto fake_bin = path + "/fake-bin";
    std::filesystem::create_directory(fake_bin);
    const auto argv_file = path + "/bead-argv";
    write_file(fake_bin + "/bd", "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + argv_file +
                                     "'\nprintf '{\"id\":\"fake-bead\"}'\n");
    std::filesystem::permissions(fake_bin + "/bd", std::filesystem::perms::owner_all);
    if (setenv("PATH", fake_bin.c_str(), 1) != 0)
      throw Error{ErrorCode::io};
    auto delivered =
        invoke(engine, "rageshake",
               Value::object({{"observation", Value{"PRIVATE_SUCCESS_OBSERVATION"}}}));
    if (setenv("PATH", old_path.c_str(), 1) != 0)
      throw Error{ErrorCode::io};
    if (!std::get<bool>(field(delivered, "bead_created").value()) ||
        read_file(argv_file).find("PRIVATE_SUCCESS_OBSERVATION") != std::string::npos ||
        read_file(argv_file).find(string_field(delivered, "complaint")) ==
            std::string::npos)
      throw Error{ErrorCode::corrupt};
    auto oversized = invoke(
        engine, "rageshake",
        Value::object({{"observation", Value{"bounded"}},
                       {"references",
                        Value::object({{"blob", Value{std::string(65537, 'x')}}})}}));
    if (string_field(oversized, "error") != "invalid_range")
      throw Error{ErrorCode::corrupt};
    if (setenv("PATH", "/arco-test-no-bd", 1) != 0)
      throw Error{ErrorCode::io};
    auto complaint =
        invoke(engine, "rageshake",
               Value::object({{"observation", Value{"PRIVATE_TEST_OBSERVATION"}},
                              {"references",
                               Value::object({{"attempt", Value{timed_reference}}})}}));
    if (setenv("PATH", old_path.c_str(), 1) != 0)
      throw Error{ErrorCode::io};
    if (!std::get<bool>(field(complaint, "retained_locally").value()) ||
        std::get<bool>(field(complaint, "bead_created").value()))
      throw Error{ErrorCode::corrupt};
    const auto complaint_id = string_field(complaint, "complaint");
    bool captured = false;
    for (std::size_t ordinal = 0; ordinal < root->fact_count(); ++ordinal) {
      const auto fact = unwrap(root->fact(ordinal));
      if (const auto *e = std::get_if<ApplicationRecordEvent>(&fact.event.body);
          e && hex_identity(e->identity.bytes()) == complaint_id) {
        for (const auto dep : fact.event.dependencies) {
          const auto raw = unwrap(root->source(dep));
          const std::string value = unwrap(format_value(unwrap(decode_packet(raw))));
          if (value.find("PRIVATE_TEST_OBSERVATION") != std::string::npos &&
              value.find(timed_reference) != std::string::npos)
            captured = true;
        }
      }
    }
    if (!captured || provider.calls != 0)
      throw Error{ErrorCode::corrupt};
    TimeoutRecoveryProvider recovery;
    recovery.marker = marker + "-timeout";
    CodingEngine recovery_engine{log, context, recovery, "test"};
    recovery_engine.turn({"recover timeout", read_file("programs/turn.lua")});
    if (recovery.calls != 3 || read_file(recovery.marker) != "X")
      throw Error{ErrorCode::corrupt};
    recovery_engine.validate_restart();
    auto bad = invoke(engine, "read_process_output",
                      Value::object({{"output_ref", Value{"missing"}}}));
    if (!bad.find("error"))
      throw Error{ErrorCode::corrupt};
  }
}

class FlakyProvider final : public CodingProvider {
public:
  unsigned calls = 0, failures = 1;
  unsigned begins = 0, ends = 0;
  bool active = false, refuse_preparation = false;
  std::function<void()> prepared;
  Error fault{ErrorCode::provider_transport, 92};
  Value first_request;
  std::string accepted_id = "accepted";
  void begin_request_group(const Value &, const RawAdapterContext *) override {
    if (active)
      throw Error{ErrorCode::busy};
    active = true;
    ++begins;
    if (prepared)
      prepared();
    if (refuse_preparation)
      throw Error{ErrorCode::unsupported};
  }
  void end_request_group() noexcept override {
    active = false;
    ++ends;
  }
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    if (!active)
      throw Error{ErrorCode::corrupt};
    if (calls == 1)
      first_request = request;
    if (request != first_request || request.find("retry_policy"))
      throw Error{ErrorCode::corrupt};
    if (calls <= failures) {
      capture("data: "
              "{\"type\":\"response.output_text.delta\",\"item_id\":\"partial\","
              "\"delta\":\"UNACCEPTED\"}\n\n");
      capture("data: "
              "{\"type\":\"response.output_item.done\",\"item\":{\"type\":\"function_"
              "call\",\"name\":\"write_file\",\"arguments\":\"{}\"}}\n\n");
      throw fault;
    }
    return Value::object(
        {{"output", Value{Value::Array{Value::object({{"type", Value{"function_call"}},
                                                      {"call_id", Value{accepted_id}},
                                                      {"name", Value{"context_stats"}},
                                                      {"arguments", Value{"{}"}}})}}}});
  }
};
void capacity_warning_test(const std::string &path) {
  std::filesystem::create_directory(path);
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  JournalHeader h{id<EnvironmentId>(41),
                  id<AuditStreamId>(42),
                  4,
                  {1024 * 1024, 2 * 1024 * 1024},
                  std::nullopt};
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                       unwrap(NativeJournalDirectory::open(path))),
                                   "audit", h, {256 * 1024, 1000}));
  AuditLog log{*root};
  ContextStore context{log};
  FlakyProvider provider;
  provider.failures = 0;
  CodingEngine engine{log, context, provider, "test"};
  const std::string filler(present(root->journal_usage().remaining_bytes()) - 24576,
                           'x');
  log.original({"capacity diagnostic filler", filler, Value::object({})});
  unsigned warnings = 0;
  engine.status = [&](std::string_view s) {
    if (s.starts_with("audit headroom:") &&
        s.find("no handoff reserve") != std::string_view::npos)
      ++warnings;
  };
  bool refused = false;
  try {
    engine.turn({"warning", R"(arco.request())"});
  } catch (const Error &e) {
    if (e.code != ErrorCode::capacity)
      throw;
    refused = true;
  }
  if (!refused || warnings != 1 || provider.calls != 0 ||
      !std::get<bool>(field(field(engine.stats(), "audit"), "approaching").value())) {
    std::cerr << "capacity warning refused=" << refused << " warnings=" << warnings
              << " provider=" << provider.calls
              << " remaining=" << present(root->journal_usage().remaining_bytes())
              << '\n';
    throw Error{ErrorCode::corrupt};
  }
}
void retry_tests(const std::string &path) {
  std::filesystem::create_directory(path);
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  JournalHeader h{id<EnvironmentId>(31),
                  id<AuditStreamId>(32),
                  3,
                  {4 * 1024 * 1024, 16 * 1024 * 1024},
                  std::nullopt};
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                       unwrap(NativeJournalDirectory::open(path))),
                                   "audit", h, {64 * 1024 * 1024, 10000}));
  AuditLog log{*root};
  const char *program =
      R"(local r=arco.request({retry_policy={max_attempts=3,base_ms=0,cap_ms=0}}); for _,v in ipairs(r.output) do local out=arco.call(v.name, {}); arco.append({{type="function_call_output",call_id=v.call_id,output=arco.json.encode(out)}}) end)";
  {
    ContextStore context{log};
    FlakyProvider provider;
    CodingEngine engine{log, context, provider, "test"};
    const auto stats = engine.stats();
    const auto &audit = field(stats, "audit");
    if (field(audit, "max_file_bytes").number().text() != "67108864" ||
        field(audit, "max_records").number().text() != "10000" ||
        field(audit, "remaining_bytes").number().text().empty())
      throw Error{ErrorCode::corrupt};
  }
  {
    ContextStore context{log};
    FlakyProvider provider;
    CodingEngine engine{log, context, provider, "test"};
    unsigned tool_count = 0;
    engine.operation_completed = [&](std::string_view event) {
      if (event.starts_with("context_stats completed"))
        ++tool_count;
    };
    engine.turn({"retry test", program});
    if (provider.calls != 2 || tool_count != 1 || provider.begins != 1 ||
        provider.ends != 1 || provider.active ||
        unwrap(dump_json(Value{context.items()})).find("UNACCEPTED") !=
            std::string::npos)
      throw Error{ErrorCode::corrupt};
    std::set<std::string> attempts;
    std::string group;
    unsigned streams = 0, requests = 0;
    for (const auto &fact : root->committed_facts()) {
      const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
      if (!record || record->channel != ApplicationChannel::log)
        continue;
      const auto original = unwrap(read_packet(record->payload));
      if (!original.find("label"))
        continue;
      if (string_field(original, "label") == "provider.request") {
        const auto &metadata = field(original, "metadata");
        ++requests;
        attempts.insert(string_field(metadata, "attempt"));
        const auto current = string_field(metadata, "retry_group");
        if (!group.empty() && group != current)
          throw Error{ErrorCode::corrupt};
        group = current;
        if (field(metadata, "ordinal").number().text() != std::to_string(requests))
          throw Error{ErrorCode::corrupt};
      }
      if (string_field(original, "label") == "provider.stream")
        ++streams;
    }
    if (attempts.size() != 2 || requests != 2 || streams != 1)
      throw Error{ErrorCode::corrupt};
  }
  for (const auto fault :
       {Error{ErrorCode::provider_transport, 92},
        Error{ErrorCode::external_unknown, 429},
        Error{ErrorCode::provider_rate_limit, 429},
        Error{ErrorCode::provider_rate_limit, -429},
        Error{ErrorCode::external_unknown, 401},
        Error{ErrorCode::provider_transport, 60}, Error{ErrorCode::interrupted},
        Error{ErrorCode::corrupt}, Error{ErrorCode::capacity}, Error{ErrorCode::io, 92},
        Error{ErrorCode::io, ETIMEDOUT}}) {
    ContextStore context{log};
    FlakyProvider provider;
    provider.failures = 10;
    provider.fault = fault;
    CodingEngine engine{log, context, provider, "test"};
    bool failed = false;
    try {
      engine.turn({"outage", program});
    } catch (const Error &e) {
      failed = e.code == fault.code && e.detail == fault.detail;
    }
    const unsigned expected =
        (fault.code == ErrorCode::provider_transport && fault.detail == 92) ||
                (fault.code == ErrorCode::external_unknown && fault.detail == 429) ||
                (fault.code == ErrorCode::provider_rate_limit && fault.detail == 429)
            ? 3U
            : 1U;
    if (!failed || provider.calls != expected || provider.begins != 1 ||
        provider.ends != 1 || provider.active)
      throw Error{ErrorCode::corrupt};
  }
  {
    ContextStore context{log};
    FlakyProvider provider;
    provider.failures = 10;
    CodingEngine engine{log, context, provider, "test"};
    std::vector<std::string> waits;
    engine.status = [&](std::string_view event) {
      if (event.find("partial output not accepted") != std::string_view::npos)
        waits.emplace_back(event);
    };
    bool failed = false;
    try {
      engine.turn(
          {"bounded backoff",
           R"(arco.request({retry_policy={max_attempts=3,base_ms=2,cap_ms=3}}))"});
    } catch (const Error &e) {
      failed = e.code == ErrorCode::provider_transport;
    }
    if (!failed || provider.calls != 3 || waits.size() != 2 ||
        !waits[0].ends_with("in 2ms") || !waits[1].ends_with("in 3ms"))
      throw Error{ErrorCode::corrupt};
  }
  {
    ContextStore context{log};
    FlakyProvider provider;
    CodingEngine engine{log, context, provider, "test"};
    bool failed = false;
    try {
      engine.turn(
          {"invalid policy", R"(arco.request({retry_policy={max_attempts=0}}))"});
    } catch (const Error &e) {
      failed = e.code == ErrorCode::invalid_range;
    }
    if (!failed || provider.calls != 0)
      throw Error{ErrorCode::corrupt};
    engine.cancelled = [] { return true; };
    failed = false;
    try {
      engine.turn({"cancel before dispatch", R"(arco.request())"});
    } catch (const Error &e) {
      failed = e.code == ErrorCode::interrupted;
    }
    if (!failed || provider.calls != 0)
      throw Error{ErrorCode::corrupt};
  }
  {
    ContextStore context{log};
    FlakyProvider provider;
    CodingEngine engine{log, context, provider, "test"};
    bool cancel = false, failed = false;
    engine.status = [&](std::string_view event) {
      if (event.find("retry 2/") != std::string_view::npos)
        cancel = true;
    };
    engine.cancelled = [&] { return cancel; };
    const auto began = std::chrono::steady_clock::now();
    try {
      engine.turn({"cancel wait", R"(arco.request({retry_policy={base_ms=5000}}))"});
    } catch (const Error &e) {
      failed = e.code == ErrorCode::interrupted;
    }
    if (!failed || provider.calls != 1 ||
        std::chrono::steady_clock::now() - began > std::chrono::seconds{1})
      throw Error{ErrorCode::corrupt};
  }
  {
    ContextStore context{log};
    FlakyProvider provider;
    provider.refuse_preparation = true;
    provider.accepted_id = "preparation-recovery";
    CodingEngine engine{log, context, provider, "test"};
    bool failed = false;
    try {
      engine.turn({"", "arco.request()"});
    } catch (const Error &e) {
      failed = e.code == ErrorCode::unsupported;
    }
    if (!failed || provider.calls != 0 || provider.begins != 1 || provider.ends != 1 ||
        provider.active)
      throw Error{ErrorCode::corrupt};
    provider.refuse_preparation = false;
    engine.turn({"", program});
    if (provider.calls != 2 || provider.begins != 2 || provider.ends != 2 ||
        provider.active)
      throw Error{ErrorCode::corrupt};
  }
  {
    ContextStore context{log}, other_context{log};
    FlakyProvider provider;
    provider.accepted_id = "nested-group";
    CodingEngine engine{log, context, provider, "test"};
    CodingEngine other{log, other_context, provider, "test"};
    bool nested_refused = false;
    provider.prepared = [&] {
      try {
        other.turn({"", "arco.request()"});
      } catch (const Error &e) {
        nested_refused = e.code == ErrorCode::busy;
      }
    };
    engine.turn({"", program});
    if (!nested_refused || provider.begins != 1 || provider.ends != 1 ||
        provider.active || provider.calls != 2)
      throw Error{ErrorCode::corrupt};
  }
  {
    ContextStore context{log};
    FlakyProvider provider;
    CodingEngine engine{log, context, provider, "test"};
    provider.prepared = [&] {
      context.append(
          {Value::object({{"role", Value{"assistant"}},
                          {"content", Value{"changed during preparation"}}})},
          "preparation-oracle");
    };
    provider.accepted_id = "preparation-conflict";
    bool conflicted = false;
    try {
      engine.turn({"", "arco.request()"});
    } catch (const Error &e) {
      conflicted = e.code == ErrorCode::conflict;
    }
    if (!conflicted || provider.calls != 0 || provider.begins != 1 ||
        provider.ends != 1 || provider.active)
      throw Error{ErrorCode::corrupt};
    provider.prepared = {};
    engine.turn({"", program});
    if (provider.calls != 2 || provider.begins != 2 || provider.ends != 2)
      throw Error{ErrorCode::corrupt};
  }
}
void owned_route_tests(const std::string &path) {
  for (const std::string mode :
       {"retry", "switch", "replace", "refresh", "refresh429"}) {
    const auto selected = std::filesystem::path{path} / mode;
    std::filesystem::create_directories(selected);
    std::filesystem::permissions(selected, std::filesystem::perms::owner_all);
    auto root = unwrap(
        RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                  NativeJournalDirectory::open(selected.string()))),
                              "audit",
                              {id<EnvironmentId>(71),
                               id<AuditStreamId>(72),
                               3,
                               {4 * 1024 * 1024, 16 * 1024 * 1024},
                               std::nullopt},
                              {64 * 1024 * 1024, 10000}));
    AuditLog log{*root};
    ContextStore context{log};
    const auto native_input = unwrap(parse_json(
        R"([{"type":"function_call","call_id":"route","name":"read_file","arguments":{"path":"README.md"}},{"type":"function_call_output","call_id":"route","output":{"content":"hello"}}])"));
    context.append(native_input.array(), "route-oracle");
    std::int64_t clock = 1000;
    unsigned calls = 0, refreshes = 0;
    std::string first_body;
    OpenAiConfig config;
    config.owned_auth.directory = selected / "auth";
    config.owned_auth.now = [&] { return clock; };
    const auto tokens = unwrap(parse_json(
        R"({"access_token":"fixture-access","refresh_token":"fixture-refresh","expires_in":3600})"));
    ProviderAuth auth{config.owned_auth};
    auth.set_key("openai", "other", "fixture-other");
    if (mode == "refresh" || mode == "refresh429")
      auth.save_oauth("openai", "bound", tokens, "fixture-client",
                      string_field(auth.descriptor("openai"), "token_url"));
    else
      auth.set_key("openai", "bound", "fixture-key");
    config.owned_auth.http = [&](const ProviderHttpRequest &request,
                                 ProviderHttpResponseLease &response) {
      const auto respond = [&](int status, std::string body) {
        auto destination = response.prepare(body.size());
        std::copy(body.begin(), body.end(), destination.begin());
        response.commit(body.size());
        return status;
      };
      if (request.url.ends_with("oauth/token")) {
        ++refreshes;
        if (mode == "refresh429")
          return respond(429, "{}");
        return respond(
            200,
            R"({"access_token":"fixture-renewed","refresh_token":"fixture-rotated","expires_in":3600})");
      }
      ++calls;
      if (mode == "refresh") {
        const auto expected =
            calls == 1 ? "Bearer fixture-access" : "Bearer fixture-renewed";
        const auto header = std::find_if(
            request.headers.begin(), request.headers.end(),
            [](const auto &value) { return value.first == "Authorization"; });
        if (header == request.headers.end() || header->second != expected)
          throw Error{ErrorCode::corrupt};
      }
      if (request.url != "https://api.openai.com/v1/responses")
        throw Error{ErrorCode::corrupt};
      const auto submitted = unwrap(parse_json(request.body));
      const auto &items = field(submitted, "input").array();
      if (field(items[0], "arguments") != Value{"{\"path\":\"README.md\"}"} ||
          field(items[1], "output") != Value{"{\"content\":\"hello\"}"})
        throw Error{ErrorCode::corrupt};
      if (calls == 1) {
        first_body = request.body;
        if (mode == "switch")
          auth.use("openai", "other");
        if (mode == "replace")
          auth.set_key("openai", "bound", "fixture-replaced");
        if (mode == "refresh" || mode == "refresh429")
          clock = 4550;
        return respond(429, "{}");
      }
      if (request.body != first_body)
        throw Error{ErrorCode::corrupt};
      return respond(200,
                     "data: {\"type\":\"response.completed\",\"response\":{\"status\":"
                     "\"completed\",\"output\":[]}}\n\n");
    };
    OpenAiCodingProvider provider{config};
    auto runtime = unwrap(RawRuntime::open(selected.string(), id<EnvironmentId>(71)));
    auto raw_handle = unwrap(runtime->prepare_thread());
    RawThreadBinding raw_binding{raw_handle};
    provider.begin_request_group(Value::object({}), nullptr);
    if (provider.supports_prompt_cache_policy() !=
        (mode != "refresh" && mode != "refresh429"))
      throw Error{ErrorCode::corrupt};
    bool nested_refused = false;
    try {
      provider.begin_request_group(Value::object({}), nullptr);
    } catch (const Error &e) {
      nested_refused = e.code == ErrorCode::busy;
    }
    if (!nested_refused)
      throw Error{ErrorCode::corrupt};
    provider.end_request_group();
    CodingEngine engine{log, context, provider, "gpt-6.1-sol"};
    bool refused = false;
    try {
      engine.turn(
          {"", "arco.request({retry_policy={max_attempts=3,base_ms=0,cap_ms=0}})"});
    } catch (const Error &e) {
      refused = (mode == "switch" || mode == "replace")
                    ? e.code == ErrorCode::conflict
                    : mode == "refresh429" &&
                          e.code == ErrorCode::provider_rate_limit && e.detail == -429;
      if (!refused)
        throw;
    }
    const bool success = mode == "retry" || mode == "refresh";
    if (refused == success || calls != (success ? 2U : 1U) ||
        refreshes != ((mode == "refresh" || mode == "refresh429") ? 1U : 0U) ||
        Value{context.items()} != native_input ||
        provider.supports_prompt_cache_policy())
      throw Error{ErrorCode::corrupt};
    std::set<std::string> attempts;
    std::string group;
    for (const auto &fact : root->committed_facts()) {
      const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
      if (!record || record->channel != ApplicationChannel::log)
        continue;
      const auto packet = unwrap(read_packet(record->payload));
      if (packet.find("label") && string_field(packet, "label") == "provider.request") {
        const auto &metadata = field(packet, "metadata");
        attempts.insert(string_field(metadata, "attempt"));
        const auto next = string_field(metadata, "retry_group");
        if (!group.empty() && group != next)
          throw Error{ErrorCode::corrupt};
        group = next;
      }
    }
    if (attempts.size() != 2)
      throw Error{ErrorCode::corrupt};
  }
}
class ExhaustingProvider final : public CodingProvider {
public:
  int calls = 0;
  bool returned = false;
  bool records = false;
  Value respond(const Value &,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    capture("retained-first-fragment");
    for (int i = 0; i < 500; ++i)
      capture(std::string(records ? 65536 : 120000, 'x'));
    returned = true;
    return Value::object({{"output", Value{Value::Array{}}}});
  }
};
void workflow_capacity_test(const std::string &path, bool records) {
  std::filesystem::create_directory(path);
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  const JournalHeader h{id<EnvironmentId>(91),
                        id<AuditStreamId>(92),
                        9,
                        {1024 * 1024, 4 * 1024 * 1024},
                        std::nullopt};
  auto root = unwrap(RetainedState::create(
      std::make_unique<NativeJournalDirectory>(
          unwrap(NativeJournalDirectory::open(path))),
      "audit", h, {records ? 8 * 1024 * 1024U : 384 * 1024U, 180}));
  AuditLog log{*root};
  ContextStore context{log};
  ExhaustingProvider provider;
  provider.records = records;
  CodingEngine engine{log, context, provider, "test"};
  context.append({Value::object({{"role", Value{"assistant"}},
                                 {"content", Value{std::string(12000, 'a')}}})},
                 "fixture");
  // Actual staged proposal + nested outstanding Lua attempt + streamed refusal.
  bool capacity = false;
  try {
    engine.turn(
        {"", "local v=arco.context(); arco.manage({base=v.base,mode='archive',"
             "ids={v.entries[#v.entries].id},reason='capacity',source='test'}); "
             "pcall(function() arco.call('lua',{code='return arco.request()'}) end); "
             "pcall(function() arco.request() end)"});
  } catch (const Error &e) {
    if (e.code != ErrorCode::capacity)
      throw;
    capacity = true;
  }
  if (!capacity || provider.calls != 1 || provider.returned ||
      root->state() != JournalWriterState::live || root->protected_settlement() ||
      context.pending_proposal() != Value{})
    throw Error{ErrorCode::corrupt};
  std::size_t unknown = 0;
  bool first = false, lost = false, cancelled = false;
  for (const auto &fact : root->committed_facts()) {
    if (const auto *o = std::get_if<AttemptObservationEvent>(&fact.event.body);
        o && o->phase == AttemptPhase::terminal &&
        o->disposition == AttemptDisposition::unknown)
      ++unknown;
    if (const auto *a = std::get_if<ApplicationRecordEvent>(&fact.event.body)) {
      const auto packet = unwrap(read_packet(a->payload));
      if (const auto *label = packet.find("label")) {
        if (*label == Value{"provider.stream"}) {
          const auto bytes = diagnostic_bytes(*root, packet);
          if (std::string_view{reinterpret_cast<const char *>(bytes.data()),
                               bytes.size()}
                  .starts_with("retained-first-fragment"))
            first = true;
        }
        if (*label == Value{"capacity.stop"} &&
            field(field(packet, "metadata"), "unretained_bytes").number().text() != "0")
          lost = true;
      }
      if (const auto *outcome = packet.find("outcome"))
        if (const auto *reason = outcome->find("reason");
            reason && *reason == Value{"workflow-cancelled"})
          cancelled = true;
    }
  }
  if (unknown < 1 || !first || !lost || !cancelled)
    throw Error{ErrorCode::corrupt};
}

void workflow_process_capacity_test(const std::string &path) {
  std::filesystem::create_directory(path);
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  const JournalHeader h{id<EnvironmentId>(93),
                        id<AuditStreamId>(94),
                        10,
                        {1024 * 1024, 4 * 1024 * 1024},
                        std::nullopt};
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                       unwrap(NativeJournalDirectory::open(path))),
                                   "audit", h, {384 * 1024, 1000}));
  AuditLog log{*root};
  ContextStore context{log};
  UsageProvider provider;
  CodingEngine engine{log, context, provider, "test"};
  const auto marker = path + "/effect";
  const auto args = Value::object(
      {{"command", Value{"printf X > '" + marker +
                         "'; printf FIRST; head -c 2000000 /dev/zero; sleep 20"}}});
  const auto began = std::chrono::steady_clock::now();
  bool capacity = false;
  try {
    engine.turn({"", "arco.append({{type='function_call',call_id='capacity-exec',"
                     "name='exec',arguments='{}'}}); "
                     "arco.call('exec',arco.json.decode([==[" +
                         unwrap(dump_json(args)) + "]==]))"});
  } catch (const Error &e) {
    if (e.code != ErrorCode::capacity)
      throw;
    capacity = true;
  }
  bool linked = false, partial = false, unknown = false;
  for (const auto &item : context.items())
    if (const auto *type = item.find("type");
        type && *type == Value{"function_call_output"})
      linked = string_field(item, "call_id") == "capacity-exec" &&
               field(item, "output").find("error") &&
               string_field(field(item, "output"), "error") == "turn_stopped";
  for (const auto &fact : root->committed_facts()) {
    if (const auto *o = std::get_if<AttemptObservationEvent>(&fact.event.body);
        o && o->phase == AttemptPhase::terminal &&
        o->disposition == AttemptDisposition::unknown)
      unknown = true;
    if (const auto *a = std::get_if<ApplicationRecordEvent>(&fact.event.body);
        a && a->channel == ApplicationChannel::log) {
      const auto p = unwrap(read_packet(a->payload));
      if (field(p, "label") == Value{"process.output"})
        for (const auto ref : fact.event.dependencies) {
          const auto raw = unwrap(root->source(ref));
          partial |=
              std::string_view{reinterpret_cast<const char *>(raw.data()), raw.size()}
                  .starts_with("FIRST");
        }
    }
  }
  if (!capacity || !linked || !partial || !unknown || read_file(marker) != "X" ||
      root->state() != JournalWriterState::live ||
      std::chrono::steady_clock::now() - began > std::chrono::seconds{5})
    throw Error{ErrorCode::corrupt};
}

void workflow_mutation_capacity_test(const std::string &path, bool nesting) {
  std::filesystem::create_directory(path);
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  const JournalHeader h{id<EnvironmentId>(95),
                        id<AuditStreamId>(96),
                        11,
                        {1024 * 1024, 4 * 1024 * 1024},
                        std::nullopt};
  auto root = unwrap(RetainedState::create(
      std::make_unique<NativeJournalDirectory>(
          unwrap(NativeJournalDirectory::open(path))),
      "audit", h, {nesting ? 8 * 1024 * 1024U : 384 * 1024U, 2000}));
  AuditLog log{*root};
  ContextStore context{log};
  UsageProvider provider;
  CodingEngine engine{log, context, provider, "test"};
  const auto marker = path + "/forbidden";
  const auto old = context.items();
  std::string code =
      nesting
          ? "function recurse(n) if n>0 then return arco.call('lua',{code='return "
            "recurse('..(n-1)..')'}) end end; pcall(function() recurse(18) end); "
          : "pcall(function() "
            "arco.append({{role='assistant',content=string.rep('x',500000)}}) end); ";
  code += "pcall(function() arco.call('write_file',{path='" + marker +
          "',content='BAD'}) end)";
  bool capacity = false;
  try {
    engine.turn({"", code});
  } catch (const Error &e) {
    if (e.code != ErrorCode::capacity)
      throw;
    capacity = true;
  }
  std::size_t admissions = 0, terminals = 0;
  for (const auto &fact : root->committed_facts()) {
    admissions += std::holds_alternative<AttemptAdmissionEvent>(fact.event.body);
    if (const auto *o = std::get_if<AttemptObservationEvent>(&fact.event.body);
        o && o->phase == AttemptPhase::terminal)
      ++terminals;
  }
  if (!capacity || std::filesystem::exists(marker) || context.items() != old ||
      root->state() != JournalWriterState::live || admissions != (nesting ? 16U : 0U) ||
      terminals != admissions)
    throw Error{ErrorCode::corrupt};
}

void workflow_settlement_limits_test(const std::string &path, bool batch) {
  std::filesystem::create_directory(path);
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  const JournalHeader h{id<EnvironmentId>(97),
                        id<AuditStreamId>(98),
                        12,
                        {batch ? 3520U : 3550U, batch ? 3608U : 16384U},
                        std::nullopt};
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                       unwrap(NativeJournalDirectory::open(path))),
                                   "audit", h, {2 * 1024 * 1024, 2000}));
  AuditLog log{*root};
  ContextStore context{log};
  UsageProvider provider;
  CodingEngine engine{log, context, provider, "test"};
  for (int i = 0; i < 5; ++i)
    context.append({Value::object({{"role", Value{"assistant"}},
                                   {"content", Value{std::string(500, 'x')}}})},
                   "fixture");
  context.append({Value::object({{"role", Value{"assistant"}},
                                 {"content", Value{std::string(100, 'y')}}})},
                 "fixture");
  const auto before = context.items();
  const auto marker = path + "/forbidden";
  bool refused = false;
  try {
    engine.turn(
        {"",
         "pcall(function() arco.append({{type='function_call',call_id='c',name='exec',"
         "arguments='{}'}}) end); "
         "pcall(function() arco.call('write_file',{path='" +
             marker + "',content='BAD'}) end)"});
  } catch (const Error &e) {
    if (e.code != ErrorCode::capacity)
      throw;
    refused = true;
  }
  if (!refused || context.items() != before || std::filesystem::exists(marker) ||
      root->state() != JournalWriterState::live) {
    std::cerr << "settlement limits batch=" << batch << " refused=" << refused
              << " changed=" << (context.items() != before)
              << " effect=" << std::filesystem::exists(marker) << '\n';
    throw Error{ErrorCode::corrupt};
  }
}

void workflow_interruption_budget_test(const std::string &path) {
  std::filesystem::create_directory(path);
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  const JournalHeader h{id<EnvironmentId>(99),
                        id<AuditStreamId>(100),
                        13,
                        {4 * 1024 * 1024, 8 * 1024 * 1024},
                        std::nullopt};
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                       unwrap(NativeJournalDirectory::open(path))),
                                   "audit", h, {8 * 1024 * 1024, 2000}));
  AuditLog log{*root};
  ContextStore context{log};
  UsageProvider provider;
  CodingEngine engine{log, context, provider, "test"};
  bool filled = false;
  engine.cancelled = [&] {
    if (context.items().size() < 2000)
      return false;
    if (!filled) {
      const auto floor = present(root->protected_settlement());
      auto remove =
          present(root->journal_usage().remaining_bytes()) - floor.max_file_bytes;
      const auto overhead = 56 + journal_frame_header_size;
      // Leave less than one further frame's room, never raise the physical cap.
      while (remove >= overhead) {
        const auto count = std::min<std::uint64_t>(65536, remove - overhead);
        std::vector<std::byte> filler(count);
        const ByteView source{filler};
        (void)unwrap(root->append(root->cursor(), std::span{&source, 1}, {}));
        remove -= count + overhead;
      }
      filled = true;
    }
    return true;
  };
  bool interrupted = false;
  try {
    engine.turn({"", "local items={} for i=1,2000 do items[i]={type='function_call',"
                     "call_id='large-interrupt-'..i,name='exec',arguments='{}'} end "
                     "arco.append(items)"});
  } catch (const Error &e) {
    if (e.code != ErrorCode::interrupted)
      throw;
    interrupted = true;
  }
  std::size_t outputs = 0;
  for (const auto &item : context.items())
    if (const auto *type = item.find("type");
        type && *type == Value{"function_call_output"}) {
      if (field(item, "output").find("error") == nullptr ||
          string_field(field(item, "output"), "error") != "turn_interrupted")
        throw Error{ErrorCode::corrupt};
      ++outputs;
    }
  engine.validate_restart(); // direct protocol validation, not a native restart
  if (!interrupted || !filled || outputs != 2000 ||
      root->state() != JournalWriterState::live)
    throw Error{ErrorCode::corrupt};
}

void file_selector_test(const std::string &path) {
  std::filesystem::create_directory(path);
  std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  const JournalHeader h{
      id<EnvironmentId>(91), id<AuditStreamId>(92), 3, {8192, 65536}, std::nullopt};
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                       unwrap(NativeJournalDirectory::open(path))),
                                   "audit", h, {1024 * 1024, 1000}));
  AuditLog log{*root};
  ContextStore context{log};
  Scripted provider;
  CodingEngine engine{log, context, provider, "test"};
  const auto file = path + "/source";
  write_file(file, "first\nsecond\n");
  const auto bad =
      invoke(engine, "read_file",
             unwrap(parse_json("{\"path\":\"" + file +
                               "\",\"range\":{\"mode\":\"lines\"},\"byte_start\":0}")));
  if (string_field(bad, "error") != "invalid_range" ||
      string_field(bad, "message").find("one range object") == std::string::npos)
    throw Error{ErrorCode::corrupt};
  const auto corrected = invoke(engine, "read_file", field(bad, "example"));
  if (string_field(corrected, "content") != "first\nsecond\n")
    throw Error{ErrorCode::corrupt};
  std::size_t failed = 0;
  for (const auto &fact : root->committed_facts())
    if (const auto *o = std::get_if<AttemptObservationEvent>(&fact.event.body))
      if (o->disposition == AttemptDisposition::failure)
        ++failed;
  if (failed != 1)
    throw Error{ErrorCode::corrupt};
}
int main(int argc, char **argv) {
  const std::string_view selected_mode = argc == 1 ? "ordinary" : argv[1];
  if (argc > 2 || (selected_mode != "ordinary" && selected_mode != "capacity" &&
                   selected_mode != "routing" && selected_mode != "stream-capture")) {
    std::cerr << "expected ordinary, capacity, routing or stream-capture mode\n";
    return 2;
  }
  char name[] = "/tmp/arco-coding-XXXXXX";
  auto path = mkdtemp(name);
  if (!path)
    return 2;
  std::string stage = "initial";
  const auto began = std::chrono::steady_clock::now();
  auto enter_stage = [&](std::string_view next) {
    stage = next;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - began)
                             .count();
    std::cerr << "coding stage " << elapsed << "ms: " << stage << '\n';
  };
  try {
    if (selected_mode == "capacity") {
      enter_stage("workflow_interruption_budget_test");
      workflow_interruption_budget_test(std::string{path} + "/interruption-floor");
      enter_stage("workflow settlement payload");
      workflow_settlement_limits_test(std::string{path} + "/settlement-payload", false);
      enter_stage("workflow settlement batch");
      workflow_settlement_limits_test(std::string{path} + "/settlement-batch", true);
      enter_stage("workflow mutation capacity");
      workflow_mutation_capacity_test(std::string{path} + "/workflow-mutation", false);
      enter_stage("workflow nesting capacity");
      workflow_mutation_capacity_test(std::string{path} + "/workflow-nesting", true);
      enter_stage("workflow_process_capacity_test");
      workflow_process_capacity_test(std::string{path} + "/workflow-process");
      enter_stage("workflow capacity bytes");
      workflow_capacity_test(std::string{path} + "/workflow-bytes", false);
      enter_stage("workflow capacity records");
      workflow_capacity_test(std::string{path} + "/workflow-records", true);
      enter_stage("atomic_admission_test");
      for (int mode = 0; mode != 3; ++mode)
        atomic_admission_test(std::string{path} + "/admission-" + std::to_string(mode),
                              mode);
      enter_stage("capacity_warning_test");
      capacity_warning_test(std::string{path} + "/capacity");
      enter_stage("fixture removal");
      std::filesystem::remove_all(path);
      enter_stage("complete");
      return 0;
    }
    if (selected_mode == "routing") {
      enter_stage("retry_tests");
      retry_tests(std::string{path} + "/retries");
      enter_stage("owned_route_tests");
      owned_route_tests(std::string{path} + "/owned-routes");
      enter_stage("fixture removal");
      std::filesystem::remove_all(path);
      enter_stage("complete");
      return 0;
    }
    if (selected_mode == "stream-capture") {
      enter_stage("stream setup and interruption");
      JournalHeader h{id<EnvironmentId>(1),
                      id<AuditStreamId>(2),
                      3,
                      {4 * 1024 * 1024, 16 * 1024 * 1024},
                      std::nullopt};
      auto root =
          unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                           unwrap(NativeJournalDirectory::open(path))),
                                       "audit", h, {64 * 1024 * 1024, 10000}));
      AuditLog log{*root};
      ContextStore context{log};
      Scripted provider;
      provider.path = std::string{path} + "/source.cpp";
      CodingEngine engine{log, context, provider, "test"};
      std::string displayed;
      engine.display = [&](std::string_view bytes) { displayed += bytes; };
      provider.check_preview = [&] {
        if (!displayed.ends_with("do"))
          throw Error{ErrorCode::corrupt};
      };
      engine.turn({"write a source", read_file("programs/turn.lua")});
      if (provider.calls != 2 || read_file(provider.path) != "int answer = 42;\n")
        throw Error{ErrorCode::corrupt};
      provider.interrupt = true;
      bool stopped = false;
      try {
        engine.turn({"cancel", read_file("programs/turn.lua")});
      } catch (const Error &e) {
        stopped = e.code == ErrorCode::interrupted;
      }
      if (!stopped || !displayed.ends_with("PARTIAL"))
        throw Error{ErrorCode::corrupt};
      engine.validate_restart();
      enter_stage("stream 100000 fragments");
      class Fragmented final : public CodingProvider {
        Value respond(const Value &,
                      const std::function<void(std::string_view)> &capture) override {
          for (unsigned i = 0; i < 100000; ++i)
            capture("x");
          return Value::object({{"output", Value{Value::Array{}}}});
        }
      } fragmented;
      const auto before = root->committed_facts().size();
      CodingEngine fragmented_engine{log, context, fragmented, "test"};
      fragmented_engine.turn({"", "arco.request()"});
      std::size_t blocks = 0, captured_bytes = 0;
      const auto facts = root->committed_facts();
      for (std::size_t i = before; i < facts.size(); ++i) {
        const auto *record = std::get_if<ApplicationRecordEvent>(&facts[i].event.body);
        if (!record || record->channel != ApplicationChannel::log)
          continue;
        const auto packet = unwrap(read_packet(record->payload));
        if (string_field(packet, "label") != "provider.stream")
          continue;
        ++blocks;
        if (!facts[i].event.dependencies.empty())
          throw Error{ErrorCode::corrupt};
        captured_bytes += diagnostic_bytes(*root, packet).size();
      }
      if (blocks != 2 || captured_bytes != 100000)
        throw Error{ErrorCode::corrupt};
      std::cout << "engine: 100000 callbacks -> " << blocks << " diagnostic blocks\n";
      enter_stage("fixture removal");
      std::filesystem::remove_all(path);
      enter_stage("complete");
      return 0;
    }
    enter_stage("file_selector_test");
    file_selector_test(std::string{path} + "/file-selector");
    enter_stage("retained_output_test");
    retained_output_test(std::string{path} + "/output");
    enter_stage("ordinary coding");
    JournalHeader h{id<EnvironmentId>(1),
                    id<AuditStreamId>(2),
                    3,
                    {4 * 1024 * 1024, 16 * 1024 * 1024},
                    std::nullopt};
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                         unwrap(NativeJournalDirectory::open(path))),
                                     "audit", h, {64 * 1024 * 1024, 10000}));
    AuditLog log{*root};
    ContextStore context{log};
    Scripted provider;
    provider.path = std::string{path} + "/source.cpp";
    CodingEngine engine{log, context, provider, "test"};
    std::string displayed;
    engine.display = [&](std::string_view s) { displayed += s; };
    provider.check_preview = [&] {
      if (!displayed.ends_with("do"))
        throw Error{ErrorCode::corrupt};
    };
    engine.turn({"write a source", read_file("programs/turn.lua")});
    if (provider.calls != 2 || read_file(provider.path) != "int answer = 42;\n" ||
        !displayed.ends_with("done"))
      throw Error{ErrorCode::corrupt};
    std::size_t terminal = 0;
    for (const auto &fact : root->committed_facts())
      if (const auto *o = std::get_if<AttemptObservationEvent>(&fact.event.body);
          o && o->phase == AttemptPhase::terminal)
        ++terminal;
    if (terminal != 3)
      throw Error{ErrorCode::corrupt};
    enter_stage("ordinary context edit and sandbox");
    const auto before = context.items();
    engine.turn(
        {"",
         R"(local v=arco.context(); local id=v.entries[1].id; v.entries=arco.array({}); assert(arco.edit(v).accepted); arco.restore(id); assert(arco.context().entries[1].id==id); local original=arco.originals(); assert(#original>=1); local j=arco.json.decode('{"n":9007199254740993,"z":null,"a":[]}'); assert(arco.json.encode(j):find('9007199254740993',1,true)); assert(arco.json.encode(j):find('null',1,true)); assert(arco.json.encode(j):find('[]',1,true)))"});
    if (context.items().size() != 1 || context.items()[0] != before[0])
      throw Error{ErrorCode::corrupt};
    engine.turn(
        {"",
         R"(assert(not pcall(arco.call, "provider", {})); assert(io == nil and os == nil and package == nil and debug == nil); assert(dofile == nil and loadfile == nil); print("audited", 42))"});
    if (!displayed.ends_with("doneaudited\t42\n"))
      throw Error{ErrorCode::corrupt};
    bool print_retained = false;
    for (const auto &fact : root->committed_facts()) {
      if (const auto *a = std::get_if<ApplicationRecordEvent>(&fact.event.body)) {
        const auto packet = unwrap(read_packet(a->payload));
        if (const auto *label = packet.find("label");
            label && std::holds_alternative<std::string>(label->value()) &&
            label->string() == "display") {
          if (fact.event.dependencies.size() != 1)
            throw Error{ErrorCode::corrupt};
          const auto raw = unwrap(root->source(fact.event.dependencies[0]));
          if (std::string_view{reinterpret_cast<const char *>(raw.data()),
                               raw.size()} == "audited\t42\n")
            print_retained = true;
        }
      }
    }
    if (!print_retained)
      throw Error{ErrorCode::corrupt};
    enter_stage("ordinary provider interruption");
    provider.interrupt = true;
    bool stopped = false;
    try {
      engine.turn({"interruption fixture", read_file("programs/turn.lua")});
    } catch (const Error &e) {
      stopped = e.code == ErrorCode::interrupted;
    }
    if (!stopped || !displayed.ends_with("PARTIAL"))
      throw Error{ErrorCode::corrupt};
    bool unknown = false;
    for (const auto &fact : root->committed_facts())
      if (const auto *o = std::get_if<AttemptObservationEvent>(&fact.event.body);
          o && o->disposition == AttemptDisposition::unknown)
        unknown = true;
    if (!unknown || read_file(provider.path) != "int answer = 42;\n")
      throw Error{ErrorCode::corrupt};
    enter_stage("ordinary command cancellation");
    StoppedTool stopped_provider;
    CodingEngine stopped_engine{log, context, stopped_provider, "test"};
    bool stop_requested = false;
    stopped_engine.cancelled = [&] { return stop_requested; };
    std::string process;
    stopped_engine.process_output = [&](std::string_view s) {
      process += s;
      if (process.starts_with("BEFORE_STOP"))
        stop_requested = true;
    };
    bool tool_stopped = false;
    try {
      stopped_engine.turn({"test stopping", read_file("programs/turn.lua")});
    } catch (const Error &e) {
      tool_stopped = e.code == ErrorCode::interrupted;
    }
    if (!tool_stopped || !process.starts_with("BEFORE_STOP"))
      throw Error{ErrorCode::corrupt};
    auto last_disposition = AttemptDisposition::failure;
    for (const auto &fact : root->committed_facts())
      if (const auto *observation =
              std::get_if<AttemptObservationEvent>(&fact.event.body);
          observation && observation->phase == AttemptPhase::terminal)
        last_disposition = observation->disposition;
    if (last_disposition != AttemptDisposition::unknown)
      throw Error{ErrorCode::corrupt};
    enter_stage("ordinary continuation after command cancellation");
    stopped_engine.cancelled = {};
    stopped_engine.display = [&](std::string_view s) { displayed += s; };
    stopped_engine.turn({"continue", read_file("programs/turn.lua")});
    if (stopped_provider.calls != 2 || !displayed.ends_with("CONTINUED"))
      throw Error{ErrorCode::corrupt};

    enter_stage("ordinary usage");
    UsageProvider usage_provider;
    CodingEngine usage_engine{log, context, usage_provider, "test"};
    std::string summary;
    usage_engine.display = [&](std::string_view text) { summary += text; };
    usage_engine.turn({"", "arco.request({model='override'}); "
                           "arco.display(arco.json.encode(arco.stats()))"});
    if (summary.find("DO_NOT_SHOW") != std::string::npos ||
        summary.find("HIDDEN") != std::string::npos ||
        summary.find("123") == std::string::npos ||
        summary.find(std::to_string(usage_provider.bytes)) == std::string::npos)
      throw Error{ErrorCode::corrupt};
    auto stats = invoke(usage_engine, "context_stats", Value::object({}));
    if (field(stats, "last_request_bytes").number().text() !=
            std::to_string(usage_provider.bytes) ||
        field(field(stats, "usage"), "input_tokens").number().text() != "123")
      throw Error{ErrorCode::corrupt};
    usage_provider.usage = false;
    usage_engine.turn({"", "arco.request()"});
    stats = invoke(usage_engine, "context_stats", Value::object({}));
    if (!std::holds_alternative<std::nullptr_t>(field(stats, "usage").value()))
      throw Error{ErrorCode::corrupt};

    enter_stage("ordinary managed publication");
    // Managed publication waits for workflow success, including a proposing tool
    // result.
    context.append({Value::object({{"role", Value{"assistant"}},
                                   {"content", Value{"old expendable fact"}}})},
                   "managed-engine");
    const auto old_entry =
        string_field(field(context.view(), "entries").array().back(), "id");
    const auto args = Value::object(
        {{"proposal", Value::object({{"base", Value{context.head()}},
                                     {"mode", Value{"archive"}},
                                     {"ids", Value{Value::Array{Value{old_entry}}}},
                                     {"reason", Value{"engine direct test"}},
                                     {"source", Value{"explicit"}}})}});
    const auto source = "local r=arco.call('context_manage',arco.json.decode(" +
                        unwrap(dump_json(Value{unwrap(dump_json(args))})) +
                        ")); assert(r.staged); "
                        "local v=arco.context(); assert(v.entries[#v.entries].id=='" +
                        old_entry + "')";
    usage_engine.turn({"", source});
    for (const auto &entry : [&] { return field(context.view(), "entries").array(); }())
      if (string_field(entry, "id") == old_entry)
        throw Error{ErrorCode::corrupt};
    context.append({Value::object({{"role", Value{"assistant"}},
                                   {"content", Value{"cancel fact"}}})},
                   "managed-engine");
    const auto cancel_entry =
        string_field(field(context.view(), "entries").array().back(), "id");
    const auto failure_source =
        "local v=arco.context(); arco.manage({base=v.base,mode='archive',"
        "ids={v.entries[#v.entries].id},reason='cancel',source='explicit'}); "
        "error('deliberate failure')";
    bool failed_workflow = false;
    try {
      usage_engine.turn({"", failure_source});
    } catch (const Error &) {
      failed_workflow = true;
    }
    if (!failed_workflow ||
        string_field(field(context.view(), "entries").array().back(), "id") !=
            cancel_entry)
      throw Error{ErrorCode::corrupt};
    usage_engine.turn(
        {"", "local v=arco.context(); arco.manage({base=v.base,mode='archive',"
             "ids={v.entries[#v.entries].id},reason='RRC',source='explicit'}); "
             "arco.call('restart',{note='managed settled'})"});
    if (!usage_engine.restart_note())
      throw Error{ErrorCode::corrupt};
    usage_engine.validate_restart();
    for (const auto &entry : [&] { return field(context.view(), "entries").array(); }())
      if (string_field(entry, "id") == cancel_entry)
        throw Error{ErrorCode::corrupt};

    enter_stage("ordinary managed provider settlement");
    // Drive actual provider response append -> tool call -> result append ->
    // settlement.
    context.append({Value::object({{"role", Value{"assistant"}},
                                   {"content", Value{"provider-selected-away"}}})},
                   "managed-provider");
    const auto provider_discard =
        string_field(field(context.view(), "entries").array().back(), "id");
    Value::Array selected_ids;
    const auto provider_view = context.view();
    for (const auto &candidate : field(provider_view, "entries").array())
      if (string_field(candidate, "id") != provider_discard)
        selected_ids.push_back(field(candidate, "id"));
    ManagedToolProvider managed_provider;
    managed_provider.proposal =
        Value::object({{"mode", Value{"select"}},
                       {"ids", Value{selected_ids}},
                       {"reason", Value{"provider-order direct oracle"}},
                       {"source", Value{"explicit selection"}}});
    CodingEngine managed_engine{log, context, managed_provider, "test"};
    managed_engine.turn({"", read_file("programs/turn.lua")});
    if (managed_provider.calls != 3)
      throw Error{ErrorCode::corrupt};
    const auto managed_view = context.view();
    for (const auto &candidate : field(managed_view, "entries").array())
      if (string_field(candidate, "id") == provider_discard)
        throw Error{ErrorCode::corrupt};
    if (string_field(context.items().back(), "call_id") != "managed-second" ||
        !field(context.items()[context.items().size() - 3], "output").find("staged"))
      throw Error{ErrorCode::corrupt};
    managed_engine.validate_restart();
    if (string_field(context.items()[context.items().size() - 3], "call_id") !=
        "managed-real")
      throw Error{ErrorCode::corrupt};
    // Request2 was still in the same workflow: old bytes + proposing exchange remain.
    if (unwrap(dump_json(Value{managed_provider.last_input}))
                .find("provider-selected-away") == std::string::npos ||
        string_field(managed_provider.last_input.back(), "call_id") != "managed-second")
      throw Error{ErrorCode::corrupt};
    const auto expected_compacted_input = context.items();
    managed_engine.turn({"", "arco.request()"});
    if (managed_provider.last_input != expected_compacted_input ||
        unwrap(dump_json(Value{managed_provider.last_input}))
                .find("provider-selected-away") != std::string::npos)
      throw Error{ErrorCode::corrupt};

    enter_stage("ordinary restart cancellation");
    bool cancel_restart = false;

    stopped_engine.cancelled = [&] { return cancel_restart; };
    stopped_engine.display = [&](std::string_view) { cancel_restart = true; };
    bool interrupted_restart = false;
    try {
      stopped_engine.turn(
          {"",
           R"(arco.call("restart", {note="must cancel"}); print("stop"); while true do end)"});
    } catch (const Error &e) {
      interrupted_restart = e.code == ErrorCode::interrupted;
    }
    if (!interrupted_restart || stopped_engine.restart_note())
      throw Error{ErrorCode::corrupt};

  } catch (const Error &e) {
    std::cerr << stage << " stage: " << error_name(e.code) << ':' << e.detail << '\n';
    std::filesystem::remove_all(path);
    return 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
  enter_stage("fixture removal");
  std::filesystem::remove_all(path);
  enter_stage("complete");
}
