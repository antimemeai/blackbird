#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/raw_reader.hpp"
#include <array>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <iostream>
#include <map>
#include <new>
#include <set>
#include <sys/socket.h>
#include <unistd.h>
using namespace blackbird;
namespace {
thread_local bool allocation_armed = false, allocation_hit = false,
                  allocation_busy = false;
bool selected_retention_allocation() noexcept {
  if (!allocation_armed || allocation_busy)
    return false;
  allocation_busy = true;
  std::array<void *, 48> frames{};
  const int count = ::backtrace(frames.data(), static_cast<int>(frames.size()));
  bool original = false, actual_call = false;
  for (int index = 0; index < count; ++index) {
    Dl_info info{};
    if (::dladdr(frames[static_cast<std::size_t>(index)], &info) && info.dli_sname) {
      original |= std::strstr(info.dli_sname, "AuditLog") &&
                  std::strstr(info.dli_sname, "original");
      actual_call |= std::strstr(info.dli_sname, "CodingEngine") &&
                     std::strstr(info.dli_sname, "call");
    }
  }
  allocation_busy = false;
  if (!original || !actual_call)
    return false;
  allocation_armed = false;
  allocation_hit = true;
  return true;
}
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error{message};
}
template <class T> T identity(unsigned char value) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{value};
  return unwrap(T::from_bytes(bytes));
}
template <class T> T take(ByteView &body) {
  require(body.size() >= sizeof(T), "actual native field exists");
  T result{};
  std::memcpy(&result, body.data(), sizeof(T));
  body = body.subspan(sizeof(T));
  return result;
}
class NoProvider final : public CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw std::runtime_error{"private complaint fixture must not call a provider"};
  }
};
struct Fixture {
  std::filesystem::path base;
  std::string old_path;
  Fixture() {
    char path[] = "/tmp/bb-complaint-raw-XXXXXX";
    const auto *created = ::mkdtemp(path);
    require(created, "temporary native complaint owner");
    base = std::filesystem::canonical(created);
    std::filesystem::create_directory(base / "audit");
    std::filesystem::permissions(base / "audit", std::filesystem::perms::owner_all);
    std::filesystem::create_directory(base / "bin");
    const auto *path_value = std::getenv("PATH");
    old_path = path_value ? path_value : "";
    // Only this temporary executable can receive the advisory delivery.
    write_file(base / "bin/bd",
               "#!/bin/sh\nprintf '%s\\n' \"$@\" >> '" + (base / "argv").string() +
                   "'\nprintf 'once\\n' >> '" + (base / "effect").string() +
                   "'\nprintf '{\"id\":\"private-complaint\"}'\n"
                   "exit \"${BB_COMPLAINT_EXIT:-0}\"\n");
    std::filesystem::permissions(base / "bin/bd", std::filesystem::perms::owner_all);
    require(::setenv("PATH", (base / "bin").c_str(), 1) == 0,
            "private advisory executable is the sole PATH choice");
  }
  ~Fixture() {
    (void)::setenv("PATH", old_path.c_str(), 1);
    // Preserve original files on a failed oracle for source/disk inspection.
  }
};
struct RetainedOriginal {
  std::string label, locator, bytes;
};
RawEventRef take_reference(ByteView &body) {
  return {take<IdentityBytes>(body), take<std::uint64_t>(body)};
}
bool equal_reference(RawEventRef left, RawEventRef right) {
  return left.lane == right.lane && left.ordinal == right.ordinal;
}
struct NativeRecord {
  std::uint32_t site;
  RawEventRef reference;
  std::vector<std::byte> fields;
};
void verify_success(const std::vector<NativeRecord> &records,
                    const std::vector<RetainedOriginal> &retained,
                    std::string_view observation, const Value &references,
                    const IdentityBytes &invocation) {
  const auto detail = unwrap(decode_packet_string(retained[0].bytes));
  const auto delivery = unwrap(decode_packet_string(retained[1].bytes));
  const auto encoded_references = unwrap(encode_packet_string(references));
  auto find = [&](RawEventRef reference) -> const NativeRecord & {
    const auto found =
        std::find_if(records.begin(), records.end(), [&](const auto &row) {
          return equal_reference(row.reference, reference);
        });
    require(found != records.end(), "actual request/original reference resolves");
    return *found;
  };
  std::map<std::uint8_t, std::vector<std::string>> sources;
  std::map<std::uint8_t, std::vector<std::string>> operands;
  std::vector<std::string> selected;
  std::set<std::uint64_t> ended;
  std::vector<RawEventRef> owners;
  for (const auto &row : records) {
    auto body = ByteView{row.fields};
    if (row.site == 4500) {
      const auto parent = take_reference(body);
      require(find(parent).site == 2400 && parent.ordinal < row.reference.ordinal,
              "Complaint request links the actual earlier Operation17");
      require(
          hex_identity(take<IdentityBytes>(body)) ==
                  string_field(detail, "reporting_attempt") &&
              take<IdentityBytes>(body) == invocation &&
              hex_identity(take<IdentityBytes>(body)) ==
                  string_field(detail, "generation") &&
              body.empty(),
          "Complaint arguments equal actual retained attempt/invocation/generation");
      owners.push_back(row.reference);
    } else if (row.site == 4510 || row.site == 4520 || row.site == 4530) {
      const auto parent = take_reference(body);
      require(find(parent).site == 4500, "subcall parent is actual whole Complaint");
      const std::size_t count = row.site == 4520 ? 3 : 1;
      const std::array<std::uint8_t, 3> delivery_roles{11, 12, 13};
      for (std::size_t index = 0; index < count; ++index) {
        const auto original = take_reference(body);
        const auto &source = find(original);
        auto source_body = ByteView{source.fields};
        (void)take_reference(source_body);
        const auto role = take<std::uint8_t>(source_body);
        require(source.site == 4502 && original.ordinal < row.reference.ordinal &&
                    role == (row.site == 4520   ? delivery_roles[index]
                             : row.site == 4510 ? 10
                                                : 14),
                "subcall references its actual previously acquired originals");
      }
      require(body.empty(), "complete literal subcall request body");
      owners.push_back(row.reference);
    } else if (row.site == 4501 || row.site == 4511 || row.site == 4521 ||
               row.site == 4531) {
      const auto request = take_reference(body);
      const auto &origin = find(request);
      require(origin.site + 1 == row.site && request.ordinal < row.reference.ordinal &&
                  request.lane == row.reference.lane &&
                  ended.insert(request.ordinal).second,
              "one later terminal on the actual request producer");
      require(take<RawOperationBranch>(body) == RawOperationBranch::returned,
              "native successful return branch");
      const auto stage = take<std::uint8_t>(body);
      require(stage == (row.site == 4501 || row.site == 4531 ? 10
                        : row.site == 4511                   ? 5
                                                             : 8) &&
                  !take<bool>(body),
              "actual reached stage and no fabricated Error on success");
      if (row.site == 4511 || row.site == 4531) {
        const auto original = take_reference(body);
        const auto &source = find(original);
        auto source_body = ByteView{source.fields};
        require(equal_reference(take_reference(source_body), request) &&
                    source.site == 4502 &&
                    take<std::uint8_t>(source_body) == (row.site == 4511 ? 11 : 15),
                "retention return carries the actual acknowledged locator original");
      } else if (row.site == 4521)
        require(take<bool>(body), "actual private child delivered predicate");
      require(body.empty(), "complete literal terminal body");
    } else if (row.site == 4502) {
      const auto owner = take_reference(body);
      require(find(owner).site == 4500 || find(owner).site == 4510 ||
                  find(owner).site == 4520 || find(owner).site == 4530,
              "original links its actual live owner request");
      const auto role = take<std::uint8_t>(body);
      require(take<std::size_t>(body) == body.size(), "literal original extent");
      sources[role].emplace_back(reinterpret_cast<const char *>(body.data()),
                                 body.size());
    } else if (row.site == 4504) {
      require(find(take_reference(body)).site == 4500,
              "unresolved selection belongs to whole owner");
      selected.push_back(hex_identity(take<IdentityBytes>(body)));
      require(body.empty(), "complete selected native unresolved identity");
    } else if (row.site == 4505) {
      (void)find(take_reference(body));
      const auto role = take<std::uint8_t>(body);
      operands[role].push_back(hex_identity(take<IdentityBytes>(body)));
      require(body.empty(), "complete native formatter operand");
    } else if (row.site == 4506 || row.site == 4507) {
      require(find(take_reference(body)).site == 4500, "actual count parent");
      const auto count = take<std::size_t>(body);
      require(Number{count} == field(detail, row.site == 4506 ? "active_attempt_count"
                                                              : "audit_end")
                                   .number() &&
                  body.empty(),
              "literal native count equals actual retained business count");
    } else if (row.site == 4503) {
      (void)find(take_reference(body));
      const auto kind = take<std::uint8_t>(body);
      const bool actual = take<bool>(body);
      if (kind == 5) {
        const auto size = take<std::size_t>(body);
        require(actual == (size == 64), "actual first64 loop predicate");
      } else
        require(actual == (kind == 3 || kind == 6 || kind == 7),
                "reached success predicates equal actual arguments/delivery");
      require(body.empty(), "complete reached predicate body");
    }
  }
  require(owners.size() == 4 && ended.size() == 4,
          "all four native requests settled once");
  auto one = [&](std::uint8_t role, std::string_view bytes) {
    require(sources[role].size() == 1 && sources[role].front() == bytes,
            "exact source equals independent acquired/retained original");
  };
  one(1, observation);
  one(2, encoded_references);
  one(4, string_field(detail, "revision"));
  one(5, string_field(detail, "generation"));
  one(6, string_field(detail, "actor"));
  one(8, string_field(detail, "workflow"));
  one(10, retained[0].bytes);
  one(11, retained[0].locator);
  one(12, "Blackbird model complaint " + retained[0].locator);
  one(13, "Local audit complaint " + retained[0].locator + "; conversation " +
              string_field(detail, "conversation") +
              ". Inspect complaint.detail locally. Advisory; no immediate repair "
              "required.");
  one(14, retained[1].bytes);
  one(15, retained[1].locator);
  one(16, "0");
  one(17, string_field(detail, "model"));
  one(18, string_field(detail, "effort"));
  require(sources[7].size() == 2 && sources[9].size() == 2,
          "only the two reached same-business repeated formatters");
  require(selected == sources[3],
          "actual unresolved selections equal retained formatters");
  for (const auto role : {std::uint8_t{3}, std::uint8_t{5}, std::uint8_t{6},
                          std::uint8_t{7}, std::uint8_t{8}, std::uint8_t{9}})
    require(operands[role] == sources[role],
            "formatter originals equal native operands");
  require(std::get<bool>(field(delivery, "bead_created").value()),
          "actual retained delivery agrees with native return predicate");
}
std::vector<RetainedOriginal> originals(RetainedState &root) {
  std::vector<RetainedOriginal> output;
  for (const auto &fact : root.committed_facts()) {
    const auto *application = std::get_if<ApplicationRecordEvent>(&fact.event.body);
    if (!application || application->channel != ApplicationChannel::log)
      continue;
    const auto packet = unwrap(read_application_packet(*application));
    const auto *label = packet.find("label");
    if (!label || !std::holds_alternative<std::string>(label->value()) ||
        !label->string().starts_with("complaint."))
      continue;
    require(fact.event.dependencies.size() == 1,
            "actual complaint original has one retained source");
    output.push_back({label->string(), hex_identity(application->identity.bytes()),
                      read_text(unwrap(root.source(fact.event.dependencies.front())))});
  }
  return output;
}
void baseline() {
  Fixture fixture;
  constexpr char native_observation[] = "PRIVATE_COMPLAINT_OBSERVATION\0\xff";
  const std::string observation{native_observation, sizeof(native_observation) - 1};
  const auto references = Value::object({{"native", Value{"PRIVATE_REFERENCE"}}});
  std::vector<RetainedOriginal> retained;
  std::string complaint;
  IdentityBytes invocation{};
  {
    auto runtime = unwrap(
        RawRuntime::open(fixture.base.string(), identity<EnvironmentId>(121),
                         {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    auto issuer = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{issuer};
      JournalHeader header{identity<EnvironmentId>(122),
                           identity<AuditStreamId>(123),
                           3,
                           {32768, 131072},
                           std::nullopt};
      auto root = unwrap(RetainedState::create(
          std::make_unique<NativeJournalDirectory>(
              unwrap(NativeJournalDirectory::open((fixture.base / "audit").string()))),
          "audit", header, {64 * 1024 * 1024, 10000}));
      AuditLog log{*root};
      ContextStore context{log};
      NoProvider provider;
      CodingEngine engine{log, context, provider, "private-model"};
      const auto result = engine.operator_call(
          "rageshake", Value::object({{"observation", Value{observation}},
                                      {"references", references}}));
      require(std::get<bool>(field(result, "retained_locally").value()) &&
                  std::get<bool>(field(result, "bead_created").value()),
              "actual public owner retains and privately delivers complaint");
      complaint = string_field(result, "complaint");
      require(read_file(fixture.base / "effect") == "once\n",
              "one genuine private child effect, no replay");
      const auto argv = read_file(fixture.base / "argv");
      require(argv.find(complaint) != std::string::npos &&
                  argv.find("PRIVATE_COMPLAINT") == std::string::npos &&
                  argv.find("PRIVATE_REFERENCE") == std::string::npos,
              "advisory argv contains locator and excludes private original");
      retained = originals(*root);
      require(retained.size() == 2 && retained[0].label == "complaint.detail" &&
                  retained[0].locator == complaint &&
                  retained[1].label == "complaint.delivery",
              "independent actual retained original identities");
      const auto detail = unwrap(decode_packet(std::as_bytes(
          std::span{retained[0].bytes.data(), retained[0].bytes.size()})));
      require(string_field(detail, "observation") == observation &&
                  string_field(field(detail, "references"), "native") ==
                      "PRIVATE_REFERENCE",
              "local retained source preserves exact binary observation/references");
      bool found = false;
      for (const auto &fact : root->committed_facts())
        if (const auto *admission =
                std::get_if<AttemptAdmissionEvent>(&fact.event.body);
            admission && hex_identity(admission->attempt.bytes()) ==
                             string_field(detail, "reporting_attempt")) {
          invocation = admission->invocation.bytes();
          found = true;
        }
      require(found, "independent actual retained invocation for complaint attempt");
    }
    issuer.reset();
  }
  const auto runs = unwrap(discover_raw_runs(fixture.base.string()));
  require(runs.size() == 1, "one actual raw owner run");
  const auto &snapshot = runs.front().snapshot;
  if (!snapshot.has_value())
    throw std::runtime_error{"actual raw owner snapshot is absent"};
  require(snapshot.value().closed,
          "independent recorder closed the actual native owner run");
  auto reader =
      unwrap(RawRunReader::open((fixture.base / "raw" / runs[0].name).string()));
  std::vector<std::byte> storage(32 * 1024 * 1024);
  std::size_t requests = 0, detail_originals = 0, delivery_originals = 0;
  std::vector<NativeRecord> records;
  while (const auto record = unwrap(reader->read(storage))) {
    auto body = record->raw;
    require(take<std::uint32_t>(body) == record->raw.size(), "native envelope size");
    const auto site = take<std::uint32_t>(body);
    const auto ordinal = take<std::uint64_t>(body);
    records.push_back(
        {site, {record->origin.lane, ordinal}, {body.begin(), body.end()}});
    require(site != 8000 && site != 8001, "no capture failure disguised as success");
    if (site == 4500)
      ++requests;
    if (site != 4502)
      continue;
    (void)take<IdentityBytes>(body);
    (void)take<std::uint64_t>(body);
    const auto role = take<std::uint8_t>(body);
    const auto size = take<std::size_t>(body);
    require(size == body.size(), "actual unchanged original byte extent");
    const std::string_view acquired{reinterpret_cast<const char *>(body.data()),
                                    body.size()};
    if (role == 10) {
      require(acquired == retained[0].bytes,
              "raw detail equals actual retained source");
      ++detail_originals;
    }
    if (role == 14) {
      require(acquired == retained[1].bytes,
              "raw delivery equals actual retained source");
      ++delivery_originals;
    }
  }
  require(requests == 1 && detail_originals == 1 && delivery_originals == 1,
          "public complaint owner emits request and both preserved native originals");
  verify_success(records, retained, observation, references, invocation);
}
enum class Cut {
  empty,
  oversized,
  type,
  references,
  nonzero,
  absent,
  before,
  after,
  optout,
  original_allocation,
  source_refusal,
  active
};
void direct_case(Cut cut) {
  Fixture fixture;
  int cancellation[2]{};
  require(::socketpair(AF_UNIX, SOCK_STREAM, 0, cancellation) == 0,
          "real caller cancellation endpoints");
  const bool selected = cut != Cut::optout;
  const bool invalid = cut <= Cut::references;
  const bool canceled = cut == Cut::before || cut == Cut::after;
  const bool allocation = cut == Cut::original_allocation;
  const bool refusal = cut == Cut::source_refusal;
  const bool active = cut == Cut::active;
  if (cut == Cut::nonzero)
    require(::setenv("BB_COMPLAINT_EXIT", "17", 1) == 0,
            "private nonzero child status");
  if (cut == Cut::absent)
    require(::setenv("PATH", "/blackbird-private-no-bd", 1) == 0,
            "actual absent advisory executable");
  std::vector<RetainedOriginal> retained;
  bool cancellation_hit = false, returned = false;
  Error actual_native{ErrorCode::invalid_range};
  {
    std::unique_ptr<RawRuntime> runtime;
    RawThreadHandle issuer;
    if (selected) {
      runtime = unwrap(
          RawRuntime::open(fixture.base.string(), identity<EnvironmentId>(124),
                           {{refusal ? 16 * 1024 * 1024 + 48ULL : 256 * 1024 * 1024ULL,
                             1024 * 1024, 128 * 1024},
                            128 * 1024}));
      issuer = unwrap(runtime->prepare_thread());
    }
    {
      RawThreadBinding binding{issuer};
      JournalHeader header{identity<EnvironmentId>(125),
                           identity<AuditStreamId>(126),
                           3,
                           {131072, 524288},
                           std::nullopt};
      auto root = unwrap(RetainedState::create(
          std::make_unique<NativeJournalDirectory>(
              unwrap(NativeJournalDirectory::open((fixture.base / "audit").string()))),
          "audit", header, {64 * 1024 * 1024, 10000}));
      AuditLog log{*root};
      ContextStore context{log};
      NoProvider provider;
      CodingEngine engine{log, context, provider, "private-model"};
      if (active) {
        const auto decision = unwrap(root->issue<DecisionId>());
        std::vector<InvocationId> planned;
        std::vector<OperationAttemptId> attempts;
        for (std::size_t index = 0; index < 65; ++index) {
          planned.push_back(unwrap(root->issue<InvocationId>()));
          attempts.push_back(unwrap(root->issue<OperationAttemptId>()));
        }
        const auto generation = unwrap(root->issue<DefinitionGenerationId>());
        std::vector<RetainedEvent> events;
        events.push_back(
            {{},
             DecisionEvent{decision, identity<ParticipantId>(127),
                           identity<ConversationId>(128), identity<WorkflowId>(129),
                           generation, identity<ContextRevisionId>(130), planned,
                           ImmutableBytes{}}});
        for (std::size_t index = 0; index < planned.size(); ++index) {
          events.push_back({{},
                            InvocationEvent{planned[index], decision, generation,
                                            ImmutableBytes{}}});
          events.push_back({{},
                            AttemptAdmissionEvent{attempts[index], planned[index],
                                                  decision, ImmutableBytes{}}});
        }
        (void)unwrap(root->append(root->cursor(), {}, events));
      }
      if (allocation) {
        std::array<void *, 4> warm{};
        (void)::backtrace(warm.data(), static_cast<int>(warm.size()));
        engine.operation_started = [&](std::string_view name) {
          if (name == "rageshake")
            allocation_armed = true;
        };
      }
      auto cancel = [&] {
        require(!cancellation_hit && ::shutdown(cancellation[1], SHUT_WR) == 0,
                "actual one sticky native EOF at selected owner boundary");
        cancellation_hit = true;
      };
      if (cut == Cut::before)
        engine.operation_started = [&](std::string_view name) {
          if (name == "rageshake")
            cancel();
        };
      if (cut == Cut::after)
        engine.operation_completed = [&](std::string_view name) {
          if (name.starts_with("exec "))
            cancel();
        };
      const auto arguments = Value::object(
          {{"observation",
            Value{cut == Cut::empty       ? ""
                  : cut == Cut::oversized ? std::string(65537, 'o')
                                          : "PRIVATE_FINITE_OBSERVATION"}},
           {"references",
            cut == Cut::type ? Value{"wrong type"}
            : cut == Cut::references
                ? Value::object({{"blob", Value{std::string(65537, 'r')}}})
                : Value::object({})}});
      {
        RawWaitScope wait{{{cancellation[0], -1}, std::nullopt}};
        try {
          const auto result = engine.operator_call("rageshake", arguments);
          returned = true;
          if (invalid)
            require(string_field(result, "error") == "invalid_range",
                    "existing invalid complaint business return");
          else if (!canceled && !allocation && !refusal)
            require(
                std::get<bool>(field(result, "retained_locally").value()) &&
                    std::get<bool>(field(result, "bead_created").value()) ==
                        (cut == Cut::optout || active),
                "actual local retention versus failed/successful advisory delivery");
        } catch (const Error &error) {
          actual_native = error;
          std::cerr << "cut " << static_cast<unsigned>(cut) << " actual Error "
                    << static_cast<unsigned>(error.code) << ' ' << error.detail << '\n';
          require((canceled && error.code == ErrorCode::interrupted) ||
                      (refusal && error.code == ErrorCode::capacity) ||
                      (invalid && error.code == ErrorCode::invalid_range),
                  "actual invalid/native capture Error preserves its original class");
          returned = true;
        } catch (const std::bad_alloc &) {
          require(allocation && allocation_hit,
                  "actual post-encoding retention allocation throws");
        }
      }
      allocation_armed = false;
      if (allocation)
        require(allocation_hit, "actual AuditLog original allocation cut reached");
      retained = originals(*root);
      require(retained.size() == (invalid || cut == Cut::before || allocation || refusal
                                      ? 0U
                                  : cut == Cut::after ? 1U
                                                      : 2U),
              "only acknowledged originals, no fabricated delivery retention");
      const bool child =
          cut == Cut::nonzero || cut == Cut::after || cut == Cut::optout || active;
      require(std::filesystem::exists(fixture.base / "effect") == child,
              "native pre-effect refusal versus actual private child effect");
      if (child)
        require(read_file(fixture.base / "effect") == "once\n",
                "private child is never replayed after owner failure");
      if (active) {
        const auto detail = unwrap(decode_packet_string(retained.front().bytes));
        const auto &ids = field(detail, "active_attempts").array();
        require(ids.size() == 64 &&
                    field(detail, "active_attempt_count").number() == Number{66},
                "real65 unresolved plus reporting attempt, existing first64 bound");
        std::string previous;
        for (const auto &id : ids) {
          require(previous.empty() || previous < id.string(),
                  "actual sorted first64 IDs");
          previous = id.string();
        }
      }
      require(canceled     ? cancellation_hit
              : allocation ? allocation_hit
                           : refusal || returned,
              "finite native cut or genuine returned business operation reached");
    }
    issuer.reset();
  }
  require(::close(cancellation[0]) == 0 && ::close(cancellation[1]) == 0,
          "fixture endpoints close after actual borrower shutdown");
  ::unsetenv("BB_COMPLAINT_EXIT");
  const auto runs = unwrap(discover_raw_runs(fixture.base.string()));
  if (!selected) {
    require(runs.empty(), "ordinary unbound complaint remains raw opt-out");
    return;
  }
  require(runs.size() == 1, "one finite raw owner run");
  const auto &snapshot = runs.front().snapshot;
  if (!snapshot.has_value())
    throw std::runtime_error{"finite raw owner snapshot is absent"};
  require(snapshot.value().closed, "finite native owner run independently closes");
  auto reader =
      unwrap(RawRunReader::open((fixture.base / "raw" / runs[0].name).string()));
  std::vector<std::byte> storage(32 * 1024 * 1024);
  std::size_t requests = 0, ends = 0, delivery_ends = 0;
  bool references_original = false, detail_original = false;
  while (const auto record = unwrap(reader->read(storage))) {
    auto body = record->raw;
    (void)take<std::uint32_t>(body);
    const auto site = take<std::uint32_t>(body);
    (void)take<std::uint64_t>(body);
    require(site != 8000 && site != 8001, "no ingest failure hides native refusal");
    if (site == 4500)
      ++requests;
    if (site == 4502) {
      (void)take_reference(body);
      const auto role = take<std::uint8_t>(body);
      require(take<std::size_t>(body) == body.size(), "finite original exact length");
      if (role == 2)
        references_original = true;
      if (role == 10) {
        if (allocation) {
          const auto packet = unwrap(decode_packet(body));
          require(string_field(packet, "observation") == "PRIVATE_FINITE_OBSERVATION",
                  "completed detail survives actual later retention allocation");
        } else
          require(!retained.empty() &&
                      std::string_view{reinterpret_cast<const char *>(body.data()),
                                       body.size()} == retained.front().bytes,
                  "preserved detail equals retained source after native refusal");
        detail_original = true;
      }
    }
    if (site == 4501 || site == 4521 || (allocation && site == 4511)) {
      (void)take_reference(body);
      const auto branch = take<RawOperationBranch>(body);
      const auto stage = take<std::uint8_t>(body);
      const bool has_error = take<bool>(body);
      if (allocation) {
        require(branch == RawOperationBranch::allocation && !has_error && stage == 5,
                "actual allocation end without fabricated Error fields");
      } else if (invalid || cut == Cut::after || refusal) {
        const Error observed{take<ErrorCode>(body), take<std::int64_t>(body)};
        require(branch == RawOperationBranch::error && has_error &&
                    observed == actual_native &&
                    stage == (cut == Cut::after       ? 8
                              : cut <= Cut::oversized ? 1
                                                      : 2),
                "native Error end/stage, no unfinished substitute");
      } else {
        require(branch == RawOperationBranch::returned && !has_error,
                "failed advisory returns actual Complaint result");
        if (site == 4521)
          require(take<bool>(body) == active,
                  "actual absent/nonzero/active-selection delivered predicate");
      }
      require(body.empty(), "complete finite native terminal variant");
      if (site == 4501)
        ++ends;
      else if (site == 4521)
        ++delivery_ends;
    }
  }
  require(requests == (cut == Cut::before || refusal ? 0U : 1U) && ends == requests &&
              delivery_ends ==
                  (invalid || cut == Cut::before || allocation || refusal ? 0U : 1U),
          "only reached requests and one paid terminal each");
  require(references_original == ((!invalid && cut != Cut::before && !refusal) ||
                                  cut == Cut::references) &&
              detail_original == (!invalid && cut != Cut::before && !refusal),
          "only reached acquired originals survive native/validation failure");
}
} // namespace
void *operator new(std::size_t bytes) {
  if (selected_retention_allocation())
    throw std::bad_alloc{};
  if (void *result = std::malloc(bytes ? bytes : 1))
    return result;
  throw std::bad_alloc{};
}
void operator delete(void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void *value) noexcept { std::free(value); }
void operator delete[](void *value, std::size_t) noexcept { std::free(value); }
int main() {
  try {
    baseline();
    for (const auto cut :
         {Cut::empty, Cut::oversized, Cut::type, Cut::references, Cut::nonzero,
          Cut::absent, Cut::before, Cut::after, Cut::optout, Cut::original_allocation,
          Cut::source_refusal, Cut::active})
      direct_case(cut);
    return 0;
  } catch (const Error &error) {
    std::cerr << "native Error " << static_cast<unsigned>(error.code) << ' '
              << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
