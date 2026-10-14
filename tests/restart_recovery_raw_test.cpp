#include "blackbird/coding.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_restart.hpp"
#include "blackbird/session.hpp"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace native_cut {
thread_local bool note_copy = false;
thread_local bool hit = false;
} // namespace native_cut
void *operator new(std::size_t size) {
  if (native_cut::note_copy && size >= 8193 && size <= 8448) {
    native_cut::note_copy = false;
    native_cut::hit = true;
    throw std::bad_alloc{};
  }
  if (void *memory = std::malloc(size ? size : 1))
    return memory;
  throw std::bad_alloc{};
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { ::operator delete(memory); }
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *memory) noexcept { ::operator delete(memory); }
void operator delete[](void *memory, std::size_t) noexcept {
  ::operator delete(memory);
}
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error{message};
}
template <class T> T id(unsigned char value) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{value};
  return unwrap(T::from_bytes(bytes));
}
template <class T> T native(ByteView &bytes) {
  check(bytes.size() >= sizeof(T), "short native session field");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return value;
}
using Ref = std::pair<IdentityBytes, std::uint64_t>;
Ref ref(ByteView &bytes) {
  return {native<IdentityBytes>(bytes), native<std::uint64_t>(bytes)};
}
struct Row {
  Ref own;
  std::uint32_t site;
  std::vector<std::byte> bytes;
};
std::vector<Row> records(const std::filesystem::path &directory) {
  const auto runs = unwrap(discover_raw_runs(directory.string()));
  check(runs.size() == 1, "one actual session raw run");
  const auto &snapshot = runs.front().snapshot;
  check(snapshot.has_value() && snapshot->closed, "closed actual session raw run");
  auto reader = unwrap(RawRunReader::open((directory / "raw" / runs[0].name).string()));
  std::vector<std::byte> scratch(2 * 1024 * 1024);
  std::vector<Row> result;
  while (const auto row = unwrap(reader->read(scratch))) {
    ByteView bytes{row->raw};
    check(native<std::uint32_t>(bytes) == row->raw.size(), "session envelope");
    const auto site = native<std::uint32_t>(bytes);
    const auto ordinal = native<std::uint64_t>(bytes);
    check(site != 8000 && site != 8001, "unexpected session ingestion failure");
    result.push_back({{row->origin.lane, ordinal}, site, {bytes.begin(), bytes.end()}});
  }
  return result;
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char name[] = "/tmp/bb-session-native-XXXXXX";
    const auto created = ::mkdtemp(name);
    check(created, "private native session fixture");
    path = created;
  }
  ~Directory() {
    if (std::uncaught_exceptions())
      std::cerr << "preserved session fixture " << path << '\n';
    else
      std::filesystem::remove_all(path);
  }
};
JournalHeader header() {
  return {id<EnvironmentId>(71), id<AuditStreamId>(72), 3, {32768, 131072}, {}};
}
std::unique_ptr<RetainedState> state(const std::filesystem::path &path) {
  return unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                          NativeJournalDirectory::open(path.string()))),
                                      "audit", header(), {64 * 1024 * 1024, 10000}));
}
RawAdapterContext source() {
  const std::uint8_t fixture = 1;
  return {nullptr,
          RawEventRef{raw_lane_epoch(),
                      unwrap(raw_emit(9200, std::array{std::as_bytes(std::span{
                                                &fixture, std::size_t{1}})}))},
          nullptr, nullptr};
}
struct NoProvider final : CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw Error{ErrorCode::conflict};
  }
};
void semantics() {
  Directory directory;
  std::string note{"native note\0\xff", 13};
  std::size_t resumed_items = 0;
  std::vector<std::pair<std::uint8_t, JournalCursor>> expected_sessions;
  std::vector<std::string> expected_locators;
  std::string token;
  IdentityBytes generation{};
  {
    auto root = state(directory.path);
    AuditLog log{*root};
    ContextStore context{log};
    auto runtime =
        unwrap(RawRuntime::open(directory.path.string(), header().environment));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto owner = source();
    SessionStore store{log, &owner};
    auto expect = [&](std::uint8_t method) {
      expected_sessions.emplace_back(method, root->cursor());
    };
    expect(2);
    check(!store.resume(context, &owner), "absent restart resumed");
    bool invalid = false;
    expect(1);
    try {
      store.restart("", &owner);
    } catch (const Error &error) {
      invalid = error == Error{ErrorCode::invalid_range};
    }
    check(invalid, "empty restart note accepted");
    expect(1);
    store.restart(note, &owner);
    for (const auto &program : root->current_programs())
      if (program.find("label") && string_field(program, "label") == "session.restart")
        token = string_field(program, "token");
    const auto before = root->fact_count();
    bool busy = false;
    expect(1);
    try {
      store.restart("later", &owner);
    } catch (const Error &error) {
      busy = error == Error{ErrorCode::busy};
    }
    check(busy && root->fact_count() == before, "pending note replaced");
    expect(3);
    SessionStore restored{log, &owner};
    expect(2);
    check(restored.resume(context, &owner), "actual restart was not consumed");
    resumed_items = context.items().size();
    check(resumed_items == 1, "resume did not inject exactly one item");
    expect(2);
    check(!restored.resume(context, &owner) && context.items().size() == resumed_items,
          "consumed restart injected twice");
    NoProvider provider;
    CodingEngine engine{log, context, provider, "private-native"};
    const auto scheduled =
        engine.operator_call("restart", Value::object({{"note", Value{note}}}));
    check(field(scheduled, "scheduled") == Value{true}, "public local schedule failed");
    const auto taken = engine.take_restart_note(&owner);
    check(taken && *taken == note && !engine.take_restart_note(&owner),
          "actual model note exchange changed");
    engine.validate_session_switch(&owner);
    engine.validate_restart(&owner);
    for (const auto &fact : root->committed_facts()) {
      if (const auto *decision = std::get_if<DecisionEvent>(&fact.event.body))
        generation = decision->definition.bytes();
      const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
      if (record && record->channel == ApplicationChannel::program) {
        const auto value = unwrap(read_packet(record->payload));
        if (value.find("label") && string_field(value, "label") == "session.restart")
          expected_locators.push_back(hex_identity(record->identity.bytes()));
      }
    }
  }
  const auto rows = records(directory.path);
  std::map<Ref, const Row *> index;
  std::map<Ref, unsigned> ended;
  unsigned note_originals = 0, returned_true = 0, returned_false = 0;
  bool invalid_end = false, busy_end = false, scheduled_owner = false;
  std::size_t session_index = 0, locator_index = 0;
  unsigned issued = 0, pending = 0, consumed = 0, restored_count = 0;
  for (const auto &row : rows)
    index.emplace(row.own, &row);
  for (const auto &row : rows) {
    ByteView bytes{row.bytes};
    if (row.site == 4400 || row.site == 4420) {
      const auto parent = ref(bytes);
      check(index.contains(parent), "request parent absent");
      const auto method = native<std::uint8_t>(bytes);
      if (row.site == 4400) {
        check(method >= 1 && method <= 3 &&
                  native<IdentityBytes>(bytes) == header().journal.bytes(),
              "literal Session method/cursor journal");
        const auto sequence = native<std::uint64_t>(bytes);
        const auto end = native<std::uint64_t>(bytes);
        check(session_index < expected_sessions.size(), "unexpected Session request");
        const auto &expected = expected_sessions[session_index++];
        check(method == expected.first && sequence == expected.second.sequence &&
                  end == expected.second.end_offset,
              "actual pre-call Session cursor/method");
      } else {
        check(method >= 1 && method <= 4, "literal Coding method");
        check(native<IdentityBytes>(bytes) == generation,
              "Coding generation differs from actual retained decision");
        if (method == 1) {
          scheduled_owner = true;
          check(index.at(parent)->site == 2400,
                "schedule must link actual Operation17");
        }
      }
      check(bytes.empty(), "request trailing fields");
    }
    if (row.site == 4402) {
      const auto parent = ref(bytes);
      const auto role = native<std::uint8_t>(bytes);
      const auto length = native<std::size_t>(bytes);
      check(index.contains(parent) && bytes.size() == length, "original parent/extent");
      const std::string_view text{reinterpret_cast<const char *>(bytes.data()),
                                  bytes.size()};
      if (role == 1 && text == note)
        ++note_originals;
      if (role == 2)
        check(text == token, "retained token original");
      if (role == 3)
        check(text == "rrc:" + token, "actual injected origin original");
      if (role == 4)
        check(text == "continue\n\nRestart/resume note:\n" + note,
              "actual injected message original");
      if (role == 5)
        check(text == "pending" || text == "consumed", "actual state original");
    }
    if (row.site == 4404) {
      check(index.at(ref(bytes))->site == 4400 &&
                hex_identity(native<IdentityBytes>(bytes)) == token && bytes.empty(),
            "actual issued token ID equals retained restart token");
      ++issued;
    }
    if (row.site == 4405) {
      check(index.at(ref(bytes))->site == 4400, "locator actual Session parent");
      const auto extent = native<std::size_t>(bytes);
      const std::string_view value{reinterpret_cast<const char *>(bytes.data()),
                                   bytes.size()};
      check(extent == 32 && bytes.size() == 32 &&
                locator_index < expected_locators.size() &&
                value == expected_locators[locator_index++],
            "actual retained record locator");
    }
    if (row.site == 4406) {
      check(index.at(ref(bytes))->site == 4400, "state mutation actual parent");
      const auto kind = native<std::uint8_t>(bytes);
      check(kind >= 1 && kind <= 3 && bytes.empty(), "literal state mutation");
      pending += kind == 1;
      consumed += kind == 2;
      restored_count += kind == 3;
    }
    if (row.site == 4403 || row.site == 4422) {
      const auto parent = ref(bytes);
      const auto kind = native<std::uint8_t>(bytes);
      const bool actual = native<bool>(bytes);
      check(index.contains(parent) && bytes.empty(),
            "literal reached predicate parent/fields");
      if (row.site == 4403) {
        check(kind >= 1 && kind <= 5, "Session predicate kind");
        if (kind == 2 || kind == 5)
          check(!actual, "unexpected unresolved/already-injected predicate");
      } else {
        check(kind >= 1 && kind <= 4, "Coding predicate kind");
        if (kind != 1)
          check(!actual, "unexpected active/uncontained Coding predicate");
      }
    }
    if (row.site == 4401 || row.site == 4421) {
      const auto parent = ref(bytes);
      check(index.contains(parent), "terminal has no real request");
      const auto branch = native<RawOperationBranch>(bytes);
      const auto stage = native<std::uint8_t>(bytes);
      const bool error = native<bool>(bytes);
      if (error) {
        const Error value{native<ErrorCode>(bytes), native<std::int64_t>(bytes)};
        check(branch == RawOperationBranch::error && value.detail == 0,
              "literal Error outcome");
        invalid_end |= value.code == ErrorCode::invalid_range && stage == 1;
        busy_end |= value.code == ErrorCode::busy && stage == 1;
      } else
        check(branch == RawOperationBranch::returned,
              "unexpected native owner failure");
      check(bytes.empty(), "terminal fields");
      ++ended[parent];
    }
    if (row.site == 4407) {
      check(index.at(ref(bytes))->site == 4400, "resume result parent");
      if (native<bool>(bytes))
        ++returned_true;
      else
        ++returned_false;
      check(bytes.empty(), "resume result width");
    }
  }
  for (const auto &row : rows)
    if (row.site == 4400 || row.site == 4420)
      check(ended[row.own] == 1, "request must have one actual terminal");
  check(note_originals >= 5 && returned_true == 1 && returned_false == 2 &&
            invalid_end && busy_end && scheduled_owner &&
            session_index == expected_sessions.size() && locator_index == 2 &&
            issued == 1 && pending == 1 && consumed == 1 && restored_count == 1,
        "native semantic outcome/source census");
}
std::vector<std::byte> admit(RetainedState &root, unsigned char base,
                             std::string_view operation, const Value &input,
                             bool opened = true, std::string_view fault = {}) {
  const auto decision = id<DecisionId>(base);
  const auto invocation = id<InvocationId>(base + 1);
  const auto attempt = id<OperationAttemptId>(base + 2);
  const auto bytes = unwrap(encode_packet(input));
  auto metadata = Value::object(
      {{"operation", Value{std::string{operation}}},
       {"input_binding", Value{"invocation-v1"}},
       {"invocation", Value{hex_identity(invocation.bytes())}},
       {"revision", Value{hex_identity(id<ContextRevisionId>(10).bytes())}},
       {"generation", Value{hex_identity(id<DefinitionGenerationId>(7).bytes())}}});
  for (auto &[key, value] : metadata.object()) {
    if (key == "generation" && fault == "generation")
      value = Value{hex_identity(id<DefinitionGenerationId>(19).bytes())};
    if (key == "revision" && fault == "revision")
      value = Value{hex_identity(id<ContextRevisionId>(19).bytes())};
  }
  if (fault == "legacy-plan") {
    auto &fields = metadata.object();
    fields.erase(std::remove_if(fields.begin(), fields.end(),
                                [](const auto &entry) {
                                  return entry.first == "input_binding" ||
                                         entry.first == "invocation";
                                }),
                 fields.end());
    fields.emplace_back("input", input);
  }
  const auto packet = unwrap(encode_packet(metadata));
  const auto invoked_input =
      fault == "input"
          ? unwrap(encode_packet(Value::object({{"different", Value{true}}})))
          : bytes;
  const std::array<RetainedEvent, 3> events{
      RetainedEvent{{},
                    DecisionEvent{decision, id<ParticipantId>(4), id<ConversationId>(5),
                                  id<WorkflowId>(6), id<DefinitionGenerationId>(7),
                                  id<ContextRevisionId>(10),
                                  fault == "plan" || fault == "legacy-plan"
                                      ? std::vector<InvocationId>{invocation,
                                                                  id<InvocationId>(90)}
                                      : std::vector<InvocationId>{invocation},
                                  packet}},
      RetainedEvent{{},
                    InvocationEvent{invocation, decision, id<DefinitionGenerationId>(7),
                                    invoked_input}},
      RetainedEvent{{}, AttemptAdmissionEvent{attempt, invocation, decision, bytes}}};
  (void)unwrap(root.append(root.cursor(), {}, events));
  if (opened)
    (void)unwrap(root.submit({{}, AttemptOpenEvent{attempt}}));
  return packet;
}
void recovery() {
  for (const std::string scenario :
       {"provider", "write_file", "exec", "revision", "legacy-plan"}) {
    Directory directory;
    auto root = state(directory.path);
    const auto metadata = admit(
        *root, 30,
        scenario == "revision" || scenario == "legacy-plan" ? "provider" : scenario,
        Value::object({{"fixture", Value{"unaltered input"}}}), true,
        scenario == "revision" || scenario == "legacy-plan" ? std::string_view{scenario}
                                                            : "");
    root.reset();
    root = unwrap(
        RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(directory.path.string()))),
                            "audit", header(), {64 * 1024 * 1024, 10000}));
    unwrap(root->confirm_recovery());
    const auto before = root->fact_count();
    const auto before_cursor = root->cursor();
    const auto admitted_input =
        unwrap(encode_packet(Value::object({{"fixture", Value{"unaltered input"}}})));
    const bool accepted = scenario == "provider" || scenario == "exec";
    {
      auto runtime =
          unwrap(RawRuntime::open(directory.path.string(), header().environment));
      auto handle = unwrap(runtime->prepare_thread());
      RawThreadBinding binding{handle};
      const auto owner = source();
      bool failed = false;
      try {
        recover_coding_session(*root,
                               scenario == "exec"
                                   ? RecoveryMode::acknowledge_local_unknowns
                                   : RecoveryMode::provider_only,
                               &owner);
      } catch (const Error &error) {
        failed = error == Error{ErrorCode::external_unknown};
      }
      check(failed != accepted, "native recovery mode/link classification changed");
      const auto attempt = unwrap(root->attempt(id<OperationAttemptId>(32)));
      check(accepted
                ? (root->state() == JournalWriterState::live && attempt.observation &&
                   attempt.observation->disposition == AttemptDisposition::unknown)
                : (root->state() == JournalWriterState::recovered &&
                   !attempt.observation && root->fact_count() == before),
            "native no-replay/reconciliation behavior");
    }
    const auto after_cursor = root->cursor();
    const auto observed = unwrap(root->attempt(id<OperationAttemptId>(32)));
    auto append_cursor = before_cursor;
    if (accepted) {
      std::uint64_t observation_sequence = 0;
      for (const auto &fact : root->committed_facts())
        if (std::holds_alternative<AttemptObservationEvent>(fact.event.body))
          observation_sequence = fact.record.sequence;
      std::ifstream file{directory.path / "audit", std::ios::binary};
      const std::string disk{std::istreambuf_iterator<char>{file},
                             std::istreambuf_iterator<char>{}};
      ByteView physical{reinterpret_cast<const std::byte *>(disk.data()), disk.size()};
      std::size_t offset = journal_header_size;
      bool found = false;
      while (offset < physical.size()) {
        const auto frame =
            unwrap(decode_journal_frame(physical.subspan(offset), header().limits));
        if (frame.sequence == observation_sequence) {
          check(frame.batch_first == frame.sequence,
                "observation append starts its actual batch");
          append_cursor = {header().journal, frame.sequence - 1, offset};
          found = true;
          break;
        }
        offset += frame.encoded_size;
      }
      check(found, "actual persisted observation append boundary missing");
    }
    const auto rows = records(directory.path);
    std::map<Ref, const Row *> index;
    std::map<Ref, unsigned> ended;
    bool metadata_seen = false, mode_seen = false, result_seen = false;
    unsigned verifier_count = 0, attempts = 0, decision_count = 0, invocation_count = 0,
             collected = 0, reconciled = 0, appended = 0, append_requests = 0;
    std::vector<std::pair<std::uint8_t, bool>> predicates;
    std::vector<std::pair<std::uint8_t, bool>> expected_predicates{
        {1, true},
        {2, true},
        {3, scenario != "write_file" && scenario != "exec"},
        {4, scenario == "write_file" || scenario == "exec"}};
    if (scenario == "write_file" || scenario == "exec")
      expected_predicates.emplace_back(5, scenario == "exec");
    if (scenario != "write_file") {
      expected_predicates.emplace_back(6, true);
      expected_predicates.emplace_back(7, scenario != "legacy-plan");
      if (scenario != "legacy-plan")
        for (std::uint8_t kind = 8; kind <= 14; ++kind)
          expected_predicates.emplace_back(kind, kind != 14 || scenario != "revision");
    }
    for (const auto &row : rows)
      index.emplace(row.own, &row);
    for (const auto &row : rows) {
      ByteView bytes{row.bytes};
      if (row.site == 4440) {
        check(index.contains(ref(bytes)), "recovery actual startup parent");
        check(native<RecoveryMode>(bytes) ==
                  (scenario == "exec" ? RecoveryMode::acknowledge_local_unknowns
                                      : RecoveryMode::provider_only),
              "native enum mode");
        check(native<IdentityBytes>(bytes) == header().journal.bytes(),
              "recovery native cursor");
        check(native<std::uint64_t>(bytes) == before_cursor.sequence &&
                  native<std::uint64_t>(bytes) == before_cursor.end_offset,
              "actual pre-recovery cursor");
        check(bytes.empty(), "recovery request exact fields");
        mode_seen = true;
      }
      if (row.site == 4402) {
        const auto parent = ref(bytes);
        const auto role = native<std::uint8_t>(bytes);
        const auto extent = native<std::size_t>(bytes);
        check(index.contains(parent) && extent == bytes.size(),
              "recovery original extent/ref");
        if (role == 9) {
          check(
              std::equal(bytes.begin(), bytes.end(), metadata.begin(), metadata.end()),
              "actual metadata original changed");
          metadata_seen = true;
        }
        if (role == 14)
          check(std::equal(bytes.begin(), bytes.end(), admitted_input.begin(),
                           admitted_input.end()),
                "actual admitted input original");
        if (role == 11) {
          check(observed.observation.has_value(),
                "observation original without retained observation");
          const auto retained = unwrap(observed.observation->observation.read());
          check(
              std::equal(bytes.begin(), bytes.end(), retained.begin(), retained.end()),
              "original differs from actual retained observation");
        }
      }
      if (row.site == 4442) {
        check(index.at(ref(bytes))->site == 4440 && native<std::size_t>(bytes) == 1 &&
                  bytes.empty(),
              "actual verifier span count/parent");
        ++verifier_count;
      }
      if (row.site == 4444) {
        check(index.at(ref(bytes))->site == 4442 &&
                  native<IdentityBytes>(bytes) == id<OperationAttemptId>(32).bytes() &&
                  native<IdentityBytes>(bytes) == id<DecisionId>(30).bytes() &&
                  native<IdentityBytes>(bytes) == id<InvocationId>(31).bytes() &&
                  native<bool>(bytes) && bytes.empty(),
              "actual admitted attempt identities/opened");
        ++attempts;
      }
      if (row.site == 4446) {
        check(index.at(ref(bytes))->site == 4444, "actual recovered native parent");
        const auto kind = native<std::uint8_t>(bytes);
        check(kind == 1 || kind == 2, "unexpected actual recovery native alternative");
        if (kind == 1) {
          check(native<IdentityBytes>(bytes) == id<DecisionId>(30).bytes() &&
                    native<IdentityBytes>(bytes) ==
                        id<DefinitionGenerationId>(7).bytes() &&
                    native<IdentityBytes>(bytes) == id<ContextRevisionId>(10).bytes(),
                "actual recovered decision fields");
          ++decision_count;
        } else {
          check(native<IdentityBytes>(bytes) == id<InvocationId>(31).bytes() &&
                    native<IdentityBytes>(bytes) == id<DecisionId>(30).bytes() &&
                    native<IdentityBytes>(bytes) ==
                        id<DefinitionGenerationId>(7).bytes() &&
                    native<std::size_t>(bytes) == admitted_input.size(),
                "actual recovered invocation fields/input extent");
          ++invocation_count;
        }
        check(bytes.empty(), "native recovered variant trailing fields");
      }
      if (row.site == 4447) {
        check(index.at(ref(bytes))->site == 4444, "actual reached predicate parent");
        const auto kind = native<std::uint8_t>(bytes);
        const bool returned = native<bool>(bytes);
        predicates.emplace_back(kind, returned);
        check(bytes.empty(), "reached predicate fields");
      }
      if (row.site == 4448) {
        const auto parent = index.at(ref(bytes))->site;
        const auto kind = native<std::uint8_t>(bytes);
        check(bytes.empty() && ((kind == 1 && parent == 4444) ||
                                ((kind == 2 || kind == 3) && parent == 4440)),
              "actual recovery mutation parent/kind");
        collected += kind == 1;
        reconciled += kind == 2;
        appended += kind == 3;
      }
      if (row.site == 4449) {
        check(index.at(ref(bytes))->site == 4444 &&
                  native<IdentityBytes>(bytes) == id<OperationAttemptId>(32).bytes() &&
                  bytes.empty(),
              "actual collected attempt identity");
      }
      if (row.site == 4450) {
        check(index.at(ref(bytes))->site == 4440 &&
                  native<IdentityBytes>(bytes) == append_cursor.journal.bytes() &&
                  native<std::uint64_t>(bytes) == append_cursor.sequence &&
                  native<std::uint64_t>(bytes) == append_cursor.end_offset &&
                  native<std::size_t>(bytes) == (scenario == "exec" ? 2U : 1U) &&
                  bytes.empty(),
              "actual append cursor/count");
        ++append_requests;
      }
      if (row.site == 4441 || row.site == 4443 || row.site == 4445 ||
          row.site == 4451) {
        const auto parent = ref(bytes);
        check(index.contains(parent), "recovery terminal actual request");
        const auto branch = native<RawOperationBranch>(bytes);
        (void)native<std::uint8_t>(bytes);
        const bool error = native<bool>(bytes);
        if (error) {
          check(branch == RawOperationBranch::error &&
                    native<ErrorCode>(bytes) == ErrorCode::external_unknown &&
                    native<std::int64_t>(bytes) == 0,
                "literal thrown recovery Error");
        } else
          check(branch == RawOperationBranch::returned,
                "unexpected recovery terminal class");
        if (row.site == 4443 || row.site == 4445) {
          check(native<bool>(bytes) == accepted,
                "actual verifier/attempt Result alternative");
          if (!accepted)
            check(native<ErrorCode>(bytes) == ErrorCode::external_unknown &&
                      native<std::int64_t>(bytes) == 0,
                  "actual failed native Result fields");
          result_seen = true;
        }
        if (row.site == 4451) {
          check(native<IdentityBytes>(bytes) == after_cursor.journal.bytes() &&
                    native<std::uint64_t>(bytes) == after_cursor.sequence &&
                    native<std::uint64_t>(bytes) == after_cursor.end_offset,
                "actual returned append cursor");
        }
        check(bytes.empty(), "recovery terminal exact variant");
        ++ended[parent];
      }
    }
    for (const auto &row : rows)
      if (row.site == 4440 || row.site == 4442 || row.site == 4444 || row.site == 4450)
        check(ended[row.own] == 1, "recovery request has exactly one terminal");
    check(metadata_seen && mode_seen && result_seen && verifier_count == 1 &&
              attempts == 1 && decision_count == 1 &&
              invocation_count == (scenario == "write_file" ? 0U : 1U) &&
              predicates == expected_predicates &&
              collected == static_cast<unsigned>(accepted) &&
              reconciled == static_cast<unsigned>(accepted) &&
              appended == static_cast<unsigned>(accepted) &&
              append_requests == static_cast<unsigned>(accepted),
          "actual recovery native sources/results/predicates/mutations missing");
  }
}

std::vector<Row> channel_records(RawChannel &channel) {
  std::vector<Row> result;
  for (const auto partition : {RawPartition::data, RawPartition::control}) {
    auto output = unwrap(channel.read(partition, 0, 1024 * 1024));
    ByteView bytes{output.bytes};
    while (!bytes.empty()) {
      const auto size = native<std::uint32_t>(bytes),
                 site = native<std::uint32_t>(bytes);
      const auto ordinal = native<std::uint64_t>(bytes);
      check(size >= 16 && size - 16 <= bytes.size(), "actual borrowed record extent");
      result.push_back(
          {{{}, ordinal}, site, {bytes.begin(), bytes.begin() + size - 16}});
      bytes = bytes.subspan(size - 16);
    }
  }
  return result;
}
void source_refusal() {
  const std::string note(256, 'n');
  for (const bool exact : {false, true}) {
    Directory directory;
    auto root = state(directory.path);
    AuditLog log{*root};
    SessionStore store{log};
    const auto before = root->fact_count();
    const auto lane = id<EnvironmentId>(91).bytes();
    const auto name = "/bb-rrc-source-" + std::to_string(::getpid());
    auto channel = unwrap(RawChannel::create(
        name, lane, {49 + note.size() - (exact ? 0U : 1U), 1024 * 1024, 4096}));
    RawProducer producer{channel};
    bool refused = false;
    {
      RawThreadBinding binding{producer, lane};
      const auto ordinal = unwrap(raw_emit(9200, {}, RawPartition::control));
      const RawAdapterContext owner{nullptr, RawEventRef{lane, ordinal}, nullptr,
                                    nullptr};
      try {
        store.restart(note, &owner);
      } catch (const Error &error) {
        refused = error == Error{ErrorCode::capacity};
      }
    }
    check(refused && root->fact_count() == before,
          "source refusal changed retained restart state");
    unsigned source_count = 0, terminal = 0;
    for (const auto &row : channel_records(channel)) {
      check(row.site != 4404 && row.site != 4405 && row.site != 4406,
            "source refusal issued identity/record/state");
      ByteView bytes{row.bytes};
      if (row.site == 4402) {
        (void)ref(bytes);
        check(native<std::uint8_t>(bytes) == 1 &&
                  native<std::size_t>(bytes) == note.size() &&
                  std::string_view{reinterpret_cast<const char *>(bytes.data()),
                                   bytes.size()} == note,
              "exact funded note original");
        ++source_count;
      }
      if (row.site == 4401) {
        (void)ref(bytes);
        check(native<RawOperationBranch>(bytes) == RawOperationBranch::error &&
                  native<std::uint8_t>(bytes) == (exact ? 2 : 0) &&
                  native<bool>(bytes) &&
                  native<ErrorCode>(bytes) == ErrorCode::capacity &&
                  native<std::int64_t>(bytes) == 0 && bytes.empty(),
              "already-paid source refusal terminal");
        ++terminal;
      }
    }
    check(source_count == static_cast<unsigned>(exact) && terminal == 1,
          "source exact/-1 literal census");
    unwrap(RawChannel::unlink(name));
  }
}
void allocation_refusal() {
  Directory directory;
  const std::string note(8192, 'a');
  {
    auto root = state(directory.path);
    AuditLog log{*root};
    SessionStore store{log};
    const auto before = root->current_programs();
    auto runtime =
        unwrap(RawRuntime::open(directory.path.string(), header().environment));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto owner = source();
    bool refused = false;
    native_cut::hit = false;
    native_cut::note_copy = true;
    try {
      store.restart(note, &owner);
    } catch (const std::bad_alloc &) {
      refused = true;
    }
    native_cut::note_copy = false;
    check(refused && native_cut::hit && root->current_programs() == before,
          "actual note-copy allocation refusal changed state");
  }
  unsigned original = 0, terminal = 0;
  for (const auto &row : records(directory.path)) {
    check(row.site != 4405 && row.site != 4406,
          "failed allocation fabricated record/state");
    ByteView bytes{row.bytes};
    if (row.site == 4402) {
      (void)ref(bytes);
      const auto role = native<std::uint8_t>(bytes);
      const auto size = native<std::size_t>(bytes);
      if (role == 1) {
        check(size == note.size() &&
                  std::string_view{reinterpret_cast<const char *>(bytes.data()),
                                   bytes.size()} == note,
              "note original lost before actual copy failure");
        ++original;
      }
    }
    if (row.site == 4401) {
      (void)ref(bytes);
      check(native<RawOperationBranch>(bytes) == RawOperationBranch::allocation &&
                native<std::uint8_t>(bytes) == 3 && !native<bool>(bytes) &&
                bytes.empty(),
            "actual allocation terminal/stage");
      ++terminal;
    }
  }
  check(original == 1 && terminal == 1, "post-acquisition allocation outcome missing");
}
void handoff_custody() {
  for (const bool worker : {false, true}) {
    Directory directory;
    RawEventRef selected;
    {
      RawSuccessorHandoff handoff;
      auto runtime =
          unwrap(RawRuntime::open(directory.path.string(), header().environment));
      auto handle = unwrap(runtime->prepare_thread());
      auto select = [&] {
        RawThreadBinding binding{handle};
        const auto parent = source();
        const std::uint8_t method = 3;
        RawSessionCall call{4460, 4461, &parent,
                            std::array{detail::raw_native_field(method)}};
        call.run([&] {
          selected = call.reference();
          handoff.select(call);
        });
      };
      if (worker) {
        std::thread creator{select};
        creator.join();
        unwrap(handoff.joined());
      } else
        select();
      bool rejected = false, joined_rejected = false;
      std::thread foreign{[&] {
        const auto joined = handoff.joined();
        joined_rejected = !joined.has_value() &&
                          joined.error() == Error{ErrorCode::wrong_incarnation};
        try {
          handoff.consume();
        } catch (const Error &error) {
          rejected = error == Error{ErrorCode::wrong_incarnation};
        }
      }};
      foreign.join();
      check(rejected && joined_rejected,
            "foreign thread accepted intended receiver custody");
      const auto child = ::fork();
      check(child >= 0, "actual custody fork");
      if (child == 0) {
        std::set_terminate([] { ::_exit(77); });
        try {
          handoff.consume();
        } catch (const Error &error) {
          if (error != Error{ErrorCode::wrong_incarnation})
            ::_exit(78);
          handoff.~RawSuccessorHandoff();
          ::_exit(79);
        }
        ::_exit(80);
      }
      int status = 0;
      check(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                WEXITSTATUS(status) == 77,
            "illegal fork destructor failed to take fatal custody path");
      handoff.consume();
    }
    unsigned retired = 0;
    for (const auto &row : records(directory.path)) {
      check(row.site != 4480 && row.site != 4481, "consume fabricated exec");
      if (row.site == 4482) {
        ByteView bytes{row.bytes};
        check(ref(bytes) == Ref{selected.lane, selected.ordinal} &&
                  native<std::uint8_t>(bytes) == 1 && bytes.empty(),
              "rejected custody discarded original paid retirement");
        ++retired;
      }
    }
    check(retired == 1, "same issuer retirement missing/duplicated");
  }
  Directory directory;
  {
    auto runtime =
        unwrap(RawRuntime::open(directory.path.string(), header().environment));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto owner = source();
    const std::uint8_t method = 3;
    RawSessionCall call{4460, 4461, &owner,
                        std::array{detail::raw_native_field(method)}};
    RawSuccessorHandoff handoff;
    call.run([&] { handoff.select(call); });
    handoff.prepare_exec(3);
    // An actual std::vector::reserve cannot represent this pointer count. The
    // holder must still retire with its original loan during this unwind.
    bool failed = false;
    try {
      std::vector<char *> pointers;
      pointers.reserve(pointers.max_size() + 1);
    } catch (const std::length_error &) {
      failed = true;
    }
    check(failed, "actual pointer preparation did not fail");
  }
  unsigned abandoned = 0;
  for (const auto &row : records(directory.path)) {
    check(row.site != 4481, "pointer preparation fabricated exec return");
    if (row.site == 4482) {
      ByteView bytes{row.bytes};
      (void)ref(bytes);
      check(native<std::uint8_t>(bytes) == 3 && bytes.empty(),
            "actual abandoned handoff reason");
      ++abandoned;
    }
  }
  check(abandoned == 1, "paid abandoned pointer preparation missing");
}
void regular_predicates() {
  Directory directory;
  std::ofstream{directory.path / "file"} << "literal";
  std::filesystem::create_symlink("loop", directory.path / "loop");
  {
    auto runtime =
        unwrap(RawRuntime::open(directory.path.string(), header().environment));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto owner = source();
    const std::uint8_t method = 1;
    RawSessionCall call{4460, 4461, &owner,
                        std::array{detail::raw_native_field(method)}};
    call.run([&] {
      for (const auto &path :
           {directory.path / "missing", directory.path / "file" / "child"}) {
        check(!std::filesystem::is_regular_file(path) &&
                  !raw_restart_regular(call, path),
              "missing/not-directory regular predicate changed");
        RawSessionCall optout{4460, 4461, nullptr};
        check(!raw_restart_regular(optout, path), "opt-out regular predicate changed");
      }
      bool native_failed = false, selected_failed = false;
      try {
        (void)std::filesystem::is_regular_file(directory.path / "loop");
      } catch (const std::filesystem::filesystem_error &error) {
        native_failed = error.code() == std::errc::too_many_symbolic_link_levels;
      }
      try {
        (void)raw_restart_regular(call, directory.path / "loop");
      } catch (const std::filesystem::filesystem_error &error) {
        selected_failed = error.code() == std::errc::too_many_symbolic_link_levels;
      }
      check(native_failed && selected_failed,
            "actual status failure exception changed");
    });
  }
  unsigned returned = 0, failed = 0;
  for (const auto &row : records(directory.path))
    if (row.site == 4463) {
      ByteView bytes{row.bytes};
      (void)ref(bytes);
      const auto branch = native<RawOperationBranch>(bytes);
      (void)native<std::uint8_t>(bytes);
      check(!native<bool>(bytes),
            "unexpected owned Error in native filesystem outcome");
      const auto error = native<int>(bytes);
      check(native<const void *>(bytes) != nullptr,
            "actual error category pointer missing");
      if (branch == RawOperationBranch::returned) {
        check((error == ENOENT || error == ENOTDIR) && !native<bool>(bytes) &&
                  bytes.empty(),
              "literal missing regular result/ec");
        ++returned;
      } else {
        check(branch == RawOperationBranch::standard && error == ELOOP && bytes.empty(),
              "literal actual filesystem failure");
        ++failed;
      }
    }
  check(returned == 2 && failed == 1, "regular predicate literal census");
}

void failed_exec() {
  Directory directory;
  RawSuccessorHandoff handoff;
  RawEventRef selected;
  {
    auto runtime =
        unwrap(RawRuntime::open(directory.path.string(), header().environment));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto parent = source();
    const std::uint8_t method = 3;
    RawSessionCall selection{4460, 4461, &parent,
                             std::array{detail::raw_native_field(method)}};
    selection.run([&] {
      selected = selection.reference();
      handoff.select(selection);
    });
  }
  char unavailable[] = "/no/such/blackbird-native-successor";
  std::array<char *, 2> argv{unavailable, nullptr};
  check(handoff.execute(argv) == -1 && errno == ENOENT,
        "actual exec failure after Runtime destruction");
  const auto rows = records(directory.path);
  Ref exec{};
  bool failed = false, retired = false;
  for (const auto &row : rows) {
    ByteView bytes{row.bytes};
    if (row.site == 4480) {
      check(ref(bytes) == Ref{selected.lane, selected.ordinal} &&
                native<std::size_t>(bytes) == 1 && bytes.empty(),
            "paid cold exec request/argc");
      exec = row.own;
    }
    if (row.site == 4481) {
      check(ref(bytes) == exec && native<int>(bytes) == -1 &&
                native<int>(bytes) == ENOENT && bytes.empty(),
            "actual failed exec rc/errno");
      failed = true;
    }
    if (row.site == 4482) {
      check(ref(bytes) == Ref{selected.lane, selected.ordinal} &&
                native<std::uint8_t>(bytes) == 2 && bytes.empty(),
            "actual cold failed retirement");
      retired = true;
    }
  }
  check(exec.second && failed && retired, "cold actual exec owner missing");
}
void successful_exec(const char *self) {
  Directory directory;
  const auto child = ::fork();
  check(child >= 0, "private native exec fixture fork");
  if (child == 0) {
    try {
      RawSuccessorHandoff handoff;
      std::string supervisor;
      {
        auto runtime =
            unwrap(RawRuntime::open(directory.path.string(), header().environment));
        const auto status = unwrap(runtime->status());
        supervisor = std::to_string(status.supervisor);
        auto handle = unwrap(runtime->prepare_thread());
        RawThreadBinding binding{handle};
        const auto parent = source();
        const std::uint8_t method = 3;
        RawSessionCall selection{4460, 4461, &parent,
                                 std::array{detail::raw_native_field(method)}};
        selection.run([&] { handoff.select(selection); });
      }
      std::string executable{self}, argument{"--wait-old-supervisor"};
      std::array<char *, 4> args{executable.data(), argument.data(), supervisor.data(),
                                 nullptr};
      (void)handoff.execute(args);
    } catch (...) {
      ::_exit(91);
    }
    ::_exit(92);
  }
  int status = 0;
  check(::waitpid(child, &status, 0) == child && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0,
        "real next image failed to wait for its old CLOEXEC supervisor drain");
  const auto rows = records(directory.path);
  unsigned launches = 0;
  for (const auto &row : rows) {
    check(row.site != 4481 && row.site != 4482,
          "successful exec fabricated returned/retired outcome");
    if (row.site == 4480) {
      ByteView bytes{row.bytes};
      (void)ref(bytes);
      check(native<std::size_t>(bytes) == 3 && bytes.empty(),
            "real successful native argc");
      ++launches;
    }
  }
  check(launches == 1, "actual successful exec request missing");
}
} // namespace
int main(int argc, char **argv) {
  if (argc == 3 && std::string_view{argv[1]} == "--wait-old-supervisor") {
    pid_t process = 0;
    const std::string_view text{argv[2]};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), process);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        process <= 0)
      return 93;
    int status = 0;
    return ::waitpid(process, &status, 0) == process && WIFEXITED(status) &&
                   WEXITSTATUS(status) == 0
               ? 0
               : 94;
  }
  try {
    semantics();
    recovery();
    source_refusal();
    allocation_refusal();
    handoff_custody();
    regular_predicates();
    failed_exec();
    successful_exec(argv[0]);
    std::cout << "restart/recovery native owners passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
