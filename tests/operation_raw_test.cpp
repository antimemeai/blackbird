#include "blackbird/local_timing.hpp"
#include "blackbird/raw_operation.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/saved_state.hpp"
#include "blackbird/session.hpp"
#include "operation_raw_native.hpp"
#include "operation_raw_oracles.hpp"
#include <array>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

// Native operation oracles use actual owner calls, real files and recorder.
// The independently executed published baseline remains in ignored context.
using namespace operation_oracle;
namespace {
template <class T> T identity(unsigned char value) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(value);
  return unwrap(T::from_bytes(bytes));
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char name[] = "/tmp/blackbird-operation-baseline-XXXXXX";
    require(::mkdtemp(name) != nullptr, "native fixture directory");
    path = name;
  }
  ~Directory() {
    if (std::uncaught_exceptions())
      std::cerr << "native baseline retained: " << path << '\n';
    else {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  }
};
struct Unused final : CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw Error{ErrorCode::conflict};
  }
};
struct Fixture {
  Directory directory;
  JournalHeader header;
  std::unique_ptr<RetainedState> root;
  explicit Fixture(JournalLimits limits)
      : header{identity<EnvironmentId>(91), identity<AuditStreamId>(92), 3, limits, {}},
        root(unwrap(RetainedState::create(
            std::make_unique<NativeJournalDirectory>(
                unwrap(NativeJournalDirectory::open(directory.path.string()))),
            "audit", header, {64 * 1024 * 1024, 8192}))) {}
  template <class F>
  std::vector<Record>
  capture(F &&action,
          RawRuntimeOptions options = {{64 * 1024 * 1024, 1024 * 1024, 4096}, 4096}) {
    {
      auto runtime = unwrap(
          RawRuntime::open(directory.path.string(), header.environment, options));
      operation_cut::register_watch = true;
      auto lane = unwrap(runtime->prepare_thread());
      operation_cut::register_watch = false;
      operation_cut::probe_open();
      struct CloseProbe {
        ~CloseProbe() { operation_cut::probe_close(); }
      } close_probe;
      {
        RawThreadBinding binding{lane};
        if constexpr (std::is_invocable_v<F, RawRuntime &, const RawThreadHandle &>)
          action(*runtime, lane);
        else
          action();
      }
      lane.reset();
    }
    const auto runs = unwrap(discover_raw_runs(directory.path.string()));
    require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
            "actual fixture raw run closed");
    auto reader = unwrap(
        RawRunReader::open((directory.path / "raw" / runs.front().name).string()));
    std::vector<std::byte> storage(32 * 1024 * 1024);
    std::vector<Record> records;
    while (const auto record = unwrap(reader->read(storage))) {
      ByteView bytes{record->raw};
      const auto length = read<std::uint32_t>(bytes);
      const auto site = read<std::uint32_t>(bytes);
      const auto ordinal = read<std::uint64_t>(bytes);
      require(length == record->raw.size(), "native envelope length");
      records.push_back({{record->origin.lane, ordinal},
                         record->origin.partition,
                         site,
                         {bytes.begin(), bytes.end()}});
    }
    return records;
  }
};
void normal_fields(const std::vector<Record> &records, RetainedState &root,
                   ByteView input, ByteView result);
void post_effect_fields(const std::vector<Record> &records, bool allocation);
void whole_request(bool vector_cut, bool receipt_cut) {
  Fixture fixture{{65536, 4 * 1024 * 1024}};
  const auto target = (fixture.directory.path / "actual-result.bin").string();
  const std::string content{"literal\0\xFE bytes", 15};
  const auto input = write_input({target, content});
  const auto primary = write_result(target);
  const auto records = fixture.capture([&] {
    AuditLog log{*fixture.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    const auto arguments =
        Value::object({{"path", Value{target}}, {"content", Value{content}}});
    const auto journal = (fixture.directory.path / "audit").string();
    if (vector_cut || receipt_cut)
      operation_cut::prepare(
          target.c_str(),
          {reinterpret_cast<const char *>(primary.data()), primary.size()},
          receipt_cut ? journal.c_str() : nullptr);
    bool allocation = false;
    std::optional<Error> native_error;
    try {
      const auto result = engine.operator_call("write_file", arguments);
      require(!vector_cut && !receipt_cut && field(result, "written") == Value{true} &&
                  string_field(result, "path") == target,
              "actual native write returned its business result");
    } catch (const std::bad_alloc &) {
      allocation = true;
    } catch (const Error &error) {
      native_error = error;
      std::cerr << "actual caller Error " << error_name(error.code) << ":"
                << error.detail << '\n';
      require((vector_cut && error.code == ErrorCode::allocation) ||
                  (receipt_cut && error == Error{ErrorCode::audit_unavailable}),
              "actual selected native allocation or receipt failure");
      allocation = vector_cut;
    }
    operation_cut::stop();
    std::cerr << "actual native cut renamed=" << operation_cut::renamed
              << " vector_refused=" << operation_cut::refused
              << " acquired_extent=" << operation_cut::acquired_extent
              << " refused_size=" << operation_cut::refused_size
              << " receipt_short=" << operation_cut::receipt_short
              << " receipt_EIO=" << operation_cut::receipt_error << '\n';
    if (vector_cut)
      require(allocation && operation_cut::renamed && operation_cut::refused &&
                  operation_cut::acquired_extent >= primary.size() &&
                  operation_cut::refused_size != 0,
              "genuine next-allocation refusal after complete encoded vector");
    if (receipt_cut)
      require(native_error && *native_error == Error{ErrorCode::audit_unavailable} &&
                  operation_cut::renamed && operation_cut::receipt_short > 0 &&
                  operation_cut::receipt_error,
              "real target rename then actual positive receipt pwrite and EIO");
    std::ifstream file{target, std::ios::binary};
    const std::string acquired{std::istreambuf_iterator<char>{file},
                               std::istreambuf_iterator<char>{}};
    require(acquired == content, "real native renamed file contains exact bytes");
    std::cerr << "actual literal file bytes=" << acquired.size() << " exact=true\n";
  });
  if (vector_cut || receipt_cut)
    require(std::any_of(records.begin(), records.end(),
                        [&](const auto &record) {
                          if (record.site != 2402)
                            return false;
                          ByteView bytes{record.bytes};
                          (void)reference(bytes);
                          if (read<std::uint8_t>(bytes) != 1)
                            return false;
                          const auto length = read<std::size_t>(bytes);
                          return length == primary.size() &&
                                 bytes.size() == primary.size() &&
                                 std::equal(bytes.begin(), bytes.end(),
                                            primary.begin());
                        }),
            "actual primary result absent before selected post-effect failure");
  const auto request =
      std::find_if(records.begin(), records.end(),
                   [](const auto &record) { return record.site == 2400; });
  require(request != records.end(), "missing whole operation2400");
  for (const auto &[role, expected] :
       std::array{std::pair{std::uint8_t{0}, ByteView{input}},
                  std::pair{std::uint8_t{1}, ByteView{primary}}})
    require(std::count_if(records.begin(), records.end(),
                          [&](const auto &record) {
                            return record.site == 2402 &&
                                   record.bytes ==
                                       original(request->reference, role, expected);
                          }) == 1,
            "exact once-only independent operation original");
  if (!vector_cut && !receipt_cut)
    normal_fields(records, *fixture.root, input, primary);
  else
    post_effect_fields(records, vector_cut);
}
void rejection(bool split, bool native_failure = false) {
  Fixture fixture{split ? JournalLimits{1024, 1112} : JournalLimits{4096, 32768}};
  const auto stale = fixture.root->cursor();
  const std::array advance{RetainedEvent{{}, IssuerReservationEvent{1000}}};
  (void)unwrap(fixture.root->append(stale, {}, advance));
  const auto before = fixture.root->cursor();
  const std::vector<std::byte> source(split ? 28000 : 9000, std::byte{'Z'});
  const auto expected = rejected_proposal(stale, source, 1001);
  require(expected.size() == (split ? 28072 : 9072), "independent exact proposal size");
  const std::array sources{ByteView{source}};
  const std::array events{RetainedEvent{{}, IssuerReservationEvent{1001}}};
  std::optional<Error> returned;
  std::uint64_t spent = 0;
  const auto records = fixture.capture([&] {
    auto bank = unwrap(raw_claim_control(unwrap(raw_operation_append_credit(
        fixture.root->journal_limits(), fixture.root->journal_capacity()))));
    const auto initial = bank.remaining();
    RawRegionPrepaidScope selection{bank};
    if (native_failure) {
      operation_cut::diagnostic_writes = 0;
      operation_cut::diagnostic_failed = false;
      operation_cut::diagnostic_cut = 4;
    }
    const auto outcome = fixture.root->append(stale, sources, events);
    require(!outcome.has_value(), "actual stale submission refuses");
    operation_cut::diagnostic_cut = 0;
    spent = initial - bank.remaining();
    returned = outcome.error();
    std::cerr << "actual stale native error " << error_name(returned->code) << ':'
              << returned->detail << '\n';
  });
  std::cerr << "actual diagnostic begin="
            << std::count_if(records.begin(), records.end(),
                             [](const auto &record) { return record.site == 1090; })
            << " terminal="
            << std::count_if(records.begin(), records.end(),
                             [](const auto &record) { return record.site == 1094; })
            << " failure="
            << std::count_if(records.begin(), records.end(),
                             [](const auto &record) {
                               return record.site == 8000 || record.site == 8001;
                             })
            << " cursor_before=" << before.sequence
            << " cursor_after=" << fixture.root->cursor().sequence << '\n';
  for (const auto &record : records)
    if (record.site == 8001) {
      ByteView bytes{record.bytes};
      const auto code = read<ErrorCode>(bytes);
      const auto detail = read<std::int64_t>(bytes);
      require(bytes.empty(), "actual ingest failure native fields");
      std::cerr << "actual ingest failure " << error_name(code) << ':' << detail
                << '\n';
    }
  if (split && returned && returned->code != ErrorCode::conflict) {
    const auto delta = fixture.root->cursor().sequence - before.sequence;
    require(delta % 2 == 0 && delta / 2 < 28 && delta > 0,
            "actual accepted source groups before diagnostic refusal");
    std::vector<std::byte> prefix;
    for (std::uint64_t index = 0; index < delta / 2; ++index) {
      const auto part = unwrap(
          fixture.root->source({before.journal, before.sequence + 1 + index * 2}));
      prefix.insert(prefix.end(), part.begin(), part.end());
    }
    require(prefix.size() == (delta / 2) * 1024 &&
                std::equal(prefix.begin(), prefix.end(), expected.begin()),
            "actual preserved rejection prefix before refusal");
    std::cerr << "actual accepted groups=" << delta / 2
              << " exact prefix_bytes=" << prefix.size() << '\n';
  }
  if (native_failure) {
    require(returned && *returned == Error{ErrorCode::io, EIO} &&
                operation_cut::diagnostic_failed && operation_cut::append_loan_paused,
            "actual fourth diagnostic source write failed with loan suspended");
    require(fixture.root->cursor().sequence == before.sequence + 6 &&
                spent == 380 + 4 * 328,
            "three accepted groups and one entered failing child use exact credit");
    std::vector<std::byte> prefix;
    for (std::uint64_t index = 0; index != 3; ++index) {
      const auto bytes = unwrap(
          fixture.root->source({before.journal, before.sequence + 1 + 2 * index}));
      prefix.insert(prefix.end(), bytes.begin(), bytes.end());
    }
    require(prefix.size() == 3072 &&
                std::equal(prefix.begin(), prefix.end(), expected.begin()),
            "native EIO retains exact accepted proposal prefix");
    return;
  }
  require(spent == static_cast<std::uint64_t>(split ? 9892 : 864),
          "actual source-count scoped credits equal independent topology");
  require(returned && returned->code == ErrorCode::conflict,
          "complete diagnostic still reports actual stale conflict");
  const RetainedFact *marker = nullptr;
  std::optional<RetainedFact> owned;
  for (std::size_t index = 0; index < fixture.root->fact_count(); ++index) {
    auto fact = unwrap(fixture.root->fact(index));
    if (const auto *body = std::get_if<RejectedSubmissionEvent>(&fact.event.body)) {
      require(!owned && body->reason == Error{ErrorCode::conflict},
              "one actual stale rejection marker");
      owned = std::move(fact);
    }
  }
  if (!owned)
    throw std::runtime_error{"actual retained rejection marker missing"};
  marker = &owned.value();
  require(marker->event.dependencies.size() == (split ? 28 : 3),
          "actual dependency count equals literal chunk count");
  std::vector<std::byte> captured;
  for (std::size_t index = 0; index < marker->event.dependencies.size(); ++index) {
    const auto dependency = marker->event.dependencies[index];
    require(dependency.journal == before.journal &&
                dependency.sequence == before.sequence + 1 + index * (split ? 2 : 1),
            "actual source group native dependency sequence");
    const auto bytes = unwrap(fixture.root->source(dependency));
    captured.insert(captured.end(), bytes.begin(), bytes.end());
  }
  require(captured == expected, "exact independently built ARPROP original");
  std::cerr << "actual rejection marker dependencies="
            << marker->event.dependencies.size()
            << " exact proposal_bytes=" << captured.size() << '\n';
  const auto count = split ? 30 : 2;
  for (const auto site :
       {std::uint32_t{1090}, std::uint32_t{1091}, std::uint32_t{1094}})
    require(std::count_if(records.begin(), records.end(),
                          [&](const auto &record) { return record.site == site; }) ==
                count,
            "every actual diagnostic retained region begins and ends once");
  require(std::none_of(records.begin(), records.end(),
                       [](const auto &record) {
                         return record.site == 8000 || record.site == 8001;
                       }),
          "bounded actual diagnostic path must not fence raw capture");
}
} // namespace
namespace {
const Record &only(const std::vector<Record> &records, std::uint32_t site,
                   std::optional<RawEventRef> owner = {}) {
  const Record *selected = nullptr;
  for (const auto &record : records) {
    if (record.site != site)
      continue;
    ByteView bytes{record.bytes};
    if (owner && !same(reference(bytes), *owner))
      continue;
    require(selected == nullptr, "duplicate selected native record");
    selected = &record;
  }
  require(selected != nullptr, "missing selected native record");
  return *selected;
}
ByteView body(const Record &record, RawEventRef owner) {
  ByteView bytes{record.bytes};
  require(same(reference(bytes), owner), "wrong actual operation owner");
  return bytes;
}
void effect_branch(const std::vector<Record> &records, RawEventRef owner,
                   RawOperationBranch expected, std::optional<Error> error = {}) {
  auto bytes = body(only(records, 2407, owner), owner);
  require(read<RawOperationBranch>(bytes) == expected &&
              read<bool>(bytes) == error.has_value(),
          "actual effect native branch/error qualification");
  if (error)
    require(read<ErrorCode>(bytes) == error->code &&
                read<std::int64_t>(bytes) == error->detail,
            "actual effect error unchanged");
  require(bytes.empty(), "effect return has no fabricated fields");
}
void terminal_branch(const std::vector<Record> &records, RawEventRef owner,
                     RawOperationBranch expected, std::optional<Error> error = {}) {
  auto bytes = body(only(records, 2417, owner), owner);
  require(read<RawOperationBranch>(bytes) == expected, "actual whole operation branch");
  (void)read<std::uint8_t>(bytes);
  require(read<bool>(bytes) == error.has_value(),
          "terminal native Error qualification");
  if (error)
    require(read<ErrorCode>(bytes) == error->code &&
                read<std::int64_t>(bytes) == error->detail,
            "terminal copies actual Error unchanged");
  if (read<bool>(bytes))
    (void)read<AttemptDisposition>(bytes);
  require(bytes.size() == 57, "terminal has exact native clocks, no duration");
}
void original_links(const std::vector<Record> &records, RawEventRef owner) {
  for (const auto &record : records) {
    if (record.site != 2409)
      continue;
    auto bytes = body(record, owner);
    const auto role = read<std::uint8_t>(bytes);
    const bool present = read<bool>(bytes);
    require(present, "enabled capture link unexpectedly absent");
    const auto source = reference(bytes);
    const auto found =
        std::find_if(records.begin(), records.end(), [&](const auto &candidate) {
          return same(candidate.reference, source);
        });
    require(found != records.end() && source.ordinal < record.reference.ordinal,
            "link names actual prior source event");
    if (role == 4) {
      require(found->site == 1060 && bytes.empty(), "actual command admission link");
      continue;
    }
    const auto partition = read<RawPartition>(bytes);
    const auto offset = read<std::size_t>(bytes);
    const auto count = read<std::size_t>(bytes);
    require(bytes.empty() && partition == found->partition && offset == 49 &&
                found->bytes.size() + 16 == offset + count,
            "original reference copies actual partition/range");
  }
}
void metadata_has_no_writer_time(const RetainedState &root) {
  for (std::size_t index = 0; index != root.fact_count(); ++index) {
    const auto fact = unwrap(root.fact(index));
    if (const auto *decision = std::get_if<DecisionEvent>(&fact.event.body)) {
      const auto metadata = unwrap(read_packet(decision->continuation));
      require(!metadata.find("time") && !metadata.find("duration_ns"),
              "operation decision emits no writer timing projection");
    }
    if (const auto *application =
            std::get_if<ApplicationRecordEvent>(&fact.event.body)) {
      const auto payload = unwrap(application->payload.read());
      const auto packet = read_packet(payload);
      if (!packet.has_value())
        continue;
      if (const auto *label = packet.value().find("label");
          label && *label == Value{"operation.result"}) {
        const auto &metadata = field(packet.value(), "metadata");
        require(!metadata.find("time") && !metadata.find("duration_ns"),
                "result metadata emits no writer timing projection");
      }
    }
  }
}
void dispatch_result(const std::vector<Record> &records, RawEventRef owner) {
  auto request = body(only(records, 2420, owner), owner);
  const auto attempt = read<IdentityBytes>(request);
  require(request.empty(), "dispatch request has only actual attempt identity");
  auto effect = body(only(records, 2406, owner), owner);
  (void)read<IdentityBytes>(effect);
  (void)read<IdentityBytes>(effect);
  (void)read<IdentityBytes>(effect);
  require(read<IdentityBytes>(effect) == attempt,
          "dispatch request binds actual effect attempt");
  auto returned = body(only(records, 2421, owner), owner);
  require(read<std::uint8_t>(returned) == 1 && !read<bool>(returned) &&
              returned.empty(),
          "actual outer Result returned report, no fabricated native Error");
}
void normal_fields(const std::vector<Record> &records, RetainedState &root,
                   ByteView input, ByteView result) {
  const auto &request_record = only(records, 2400);
  const auto owner = request_record.reference;
  std::array<IdentityBytes, 5> ids{};
  std::array<bool, 5> present{};
  for (const auto &record : records) {
    if (record.site != 2403)
      continue;
    auto bytes = body(record, owner);
    const auto kind = read<std::uint8_t>(bytes);
    require(kind < ids.size() && !present[kind],
            "one acquired identity per native kind");
    ids[kind] = read<IdentityBytes>(bytes);
    present[kind] = true;
    require(bytes.empty(), "identity has exact unformatted native bytes");
  }
  require(std::all_of(present.begin(), present.end(), [](bool value) { return value; }),
          "all five actual identities recorded");
  const auto decision = unwrap(root.decision(unwrap(DecisionId::from_bytes(ids[0]))));
  const auto invocation =
      unwrap(root.invocation(unwrap(InvocationId::from_bytes(ids[1]))));
  const auto attempt =
      unwrap(root.attempt(unwrap(OperationAttemptId::from_bytes(ids[2]))));
  require(ids[3] == decision.definition.bytes() && ids[4] == decision.context.bytes() &&
              decision.planned_invocations.size() == 1 &&
              decision.planned_invocations.front() == invocation.invocation &&
              invocation.decision == decision.decision &&
              attempt.admission.invocation == invocation.invocation &&
              std::equal(invocation.input.begin(), invocation.input.end(),
                         input.begin(), input.end()),
          "native operation IDs match actual retained decision/invocation/attempt");
  ByteView request{request_record.bytes};
  require(read<bool>(request) &&
              same(reference(request), only(records, 1900).reference),
          "whole operation parent is actual turn request");
  require(read<IdentityBytes>(request) == ids[3] && read<std::size_t>(request) == 0 &&
              !read<bool>(request) && !read<bool>(request) && request.size() == 57,
          "whole request copies exact preflight native state/clocks");
  auto name = body(only(records, 2401, owner), owner);
  require(read<std::size_t>(name) == 10 && name.size() == 10 &&
              std::memcmp(name.data(), "write_file", 10) == 0,
          "operation name original exact");
  auto admission = body(only(records, 2404, owner), owner);
  require(read<std::uint8_t>(admission) == 0, "admission native kind");
  const auto journal = read<IdentityBytes>(admission);
  const auto sequence = read<std::uint64_t>(admission);
  const auto offset = read<std::uint64_t>(admission);
  require(read<std::size_t>(admission) == 3 && admission.empty(),
          "actual admitted batch size");
  auto accepted = body(only(records, 2405, owner), owner);
  require(read<std::uint8_t>(accepted) == 0 && read<std::uint8_t>(accepted) == 1 &&
              read<IdentityBytes>(accepted) == journal &&
              read<std::uint64_t>(accepted) == sequence + 4 &&
              read<std::uint64_t>(accepted) > offset && accepted.empty(),
          "actual returned admission cursor advances three events plus commit");
  auto intent = body(only(records, 2406, owner), owner);
  require(read<IdentityBytes>(intent) == identity<EnvironmentId>(91).bytes() &&
              read<IdentityBytes>(intent) == decision.actor.bytes() &&
              read<IdentityBytes>(intent) == ids[1] &&
              read<IdentityBytes>(intent) == ids[2] &&
              read<std::size_t>(intent) == input.size() && intent.empty(),
          "effect intent copies existing native IDs and actual admitted length");
  dispatch_result(records, owner);
  effect_branch(records, owner, RawOperationBranch::returned);
  auto report = body(only(records, 2408, owner), owner);
  require(read<bool>(report) && read<bool>(report) && read<bool>(report) &&
              report.empty(),
          "actual acquired dispatch report success fields");
  original_links(records, owner);
  const auto &retained = only(records, 2411, owner);
  auto source = body(retained, owner);
  require(read<std::uint8_t>(source) == 0 && read<std::uint8_t>(source) == 1,
          "actual primary semantic retention return");
  const auto source_journal =
      unwrap(AuditStreamId::from_bytes(read<IdentityBytes>(source)));
  const auto source_sequence = read<std::uint64_t>(source);
  const auto bytes = unwrap(root.source({source_journal, source_sequence}));
  require(source.empty() &&
              std::equal(bytes.begin(), bytes.end(), result.begin(), result.end()),
          "retention return names exact native source bytes");
  terminal_branch(records, owner, RawOperationBranch::returned);
  metadata_has_no_writer_time(root);
}
void post_effect_fields(const std::vector<Record> &records, bool allocation) {
  const auto owner = only(records, 2400).reference;
  effect_branch(records, owner, RawOperationBranch::returned);
  dispatch_result(records, owner);
  auto report = body(only(records, 2408, owner), owner);
  require(read<bool>(report) && read<bool>(report) && read<bool>(report) == allocation,
          "receipt recording failure is distinct from successful effect");
  if (!allocation)
    require(read<ErrorCode>(report) == ErrorCode::io &&
                read<std::int64_t>(report) == EIO,
            "actual native short-write receipt EIO unchanged");
  require(report.empty(), "report has no fabricated status fields");
  terminal_branch(records, owner,
                  allocation ? RawOperationBranch::allocation
                             : RawOperationBranch::error,
                  allocation ? std::nullopt : std::optional{Error{ErrorCode::io, EIO}});
  original_links(records, owner);
  require(std::none_of(records.begin(), records.end(),
                       [](const auto &record) {
                         return record.site == 2410 || record.site == 2413;
                       }),
          "post-effect failure invents no accepted result retention/settlement");
}
void credits() {
  require(unwrap(raw_operation_append_credit({256, 344}, {100000, 10000})) == 13848,
          "independent small-payload split topology bound");
  require(unwrap(raw_operation_append_credit({1048576, 1048664},
                                             {1000000000, 10000})) == 16196,
          "independent large-payload partial-last topology bound");
  require(unwrap(raw_operation_append_credit({8, 96}, {100000, 10000})) == 4472,
          "no marker-reference capacity has only thirteen direct regions");
  const auto invalid = raw_operation_append_credit({0, 88}, {1, 1});
  require(!invalid.has_value() && invalid.error().code == ErrorCode::invalid_range,
          "invalid native journal dimensions refuse before admission");
  const auto overflow = raw_retained_region_credit(SIZE_MAX, 1);
  require(!overflow.has_value() && overflow.error().code == ErrorCode::overflow,
          "aggregate native observation accounting never saturates");
}
void check_clocks(ByteView bytes) {
  std::size_t matched = 0;
  for (unsigned index = 0; index != 2; ++index) {
    if (index == 1)
      require(read<bool>(bytes), "thread CPU was actually acquired");
    const auto method = read<clockid_t>(bytes);
    const auto result = read<int>(bytes);
    const auto error = read<int>(bytes);
    const auto seconds = read<time_t>(bytes);
    const auto nanos = read<long>(bytes);
    bool found = false;
    for (; matched != operation_cut::clock_count; ++matched) {
      const auto &actual = operation_cut::clocks[matched];
      if (actual.method == method && actual.result == result && actual.error == error &&
          actual.value.tv_sec == seconds && actual.value.tv_nsec == nanos) {
        ++matched;
        found = true;
        break;
      }
    }
    require(found, "raw clock fields differ from actual forwarded native acquisition");
  }
  require(bytes.empty(), "raw clocks have no extra duration/normalization");
}
void native_clocks() {
  Fixture fixture{{65536, 4 * 1024 * 1024}};
  const auto records = fixture.capture([&] {
    RawOperationTrace trace{*fixture.root};
    operation_cut::clock_count = 0;
    operation_cut::clock_failure = true;
    operation_cut::clock_watch = true;
    trace.start({}, identity<DefinitionGenerationId>(93), 0, false, false);
    trace.run([] {});
    operation_cut::clock_watch = false;
  });
  ByteView request{only(records, 2400).bytes};
  require(!read<bool>(request), "direct owner parent is absent");
  require(read<IdentityBytes>(request) == identity<DefinitionGenerationId>(93).bytes(),
          "actual generation identity unchanged");
  require(read<std::size_t>(request) == 0 && !read<bool>(request) &&
              !read<bool>(request),
          "native owner state fields unchanged");
  check_clocks(request);
  bool failed = false;
  for (std::size_t index = 0; index != operation_cut::clock_count; ++index) {
    const auto &actual = operation_cut::clocks[index];
    failed = failed || (actual.method == CLOCK_THREAD_CPUTIME_ID &&
                        actual.result == -1 && actual.error == EIO);
  }
  require(failed, "genuine failed native thread clock was selected");
  const auto owner = only(records, 2400).reference;
  auto terminal = body(only(records, 2417, owner), owner);
  require(read<std::uint8_t>(terminal) == 1 && read<std::uint8_t>(terminal) == 0 &&
              !read<bool>(terminal) && !read<bool>(terminal),
          "direct returned terminal has absent Error/disposition");
  check_clocks(terminal);
}
struct ReturnedProvider final : CodingProvider {
  RawOperationBranch branch = RawOperationBranch::returned;
  bool conversion_cut = false, oversize = false;
  unsigned calls = 0;
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    ++calls;
    if (branch == RawOperationBranch::error)
      throw Error{ErrorCode::io, EIO};
    if (branch == RawOperationBranch::allocation)
      throw std::bad_alloc{};
    if (branch == RawOperationBranch::length)
      throw std::length_error{"actual native adapter length"};
    if (branch == RawOperationBranch::standard)
      throw std::runtime_error{"actual native adapter standard"};
    if (branch == RawOperationBranch::nonstandard)
      throw 93;
    if (conversion_cut) {
      operation_cut::live = {};
      operation_cut::require_result_phase = true;
      operation_cut::result_phase_seen = false;
      operation_cut::watching = true;
    }
    Value result =
        Value::object({{"output", Value{Value::Array{}}},
                       {"sentinel", Value{std::string{"literal\0\xFE response", 19}}}});
    if (oversize)
      result.object().emplace_back("oversized",
                                   Value{std::string(16 * 1024 * 1024 + 1, 'X')});
    return result;
  }
};
std::vector<std::byte> provider_packet() {
  std::vector<std::byte> expected{std::byte{'B'}, std::byte{'B'}, std::byte{'M'},
                                  std::byte{2},   std::byte{10},  std::byte{2}};
  text(expected, "output");
  expected.push_back(std::byte{9});
  expected.push_back(std::byte{0});
  text(expected, "sentinel");
  text(expected, std::string_view{"literal\0\xFE response", 19});
  return expected;
}
std::vector<std::byte> provider_error_packet(std::string_view name,
                                             std::size_t detail) {
  std::vector<std::byte> expected{std::byte{'B'}, std::byte{'B'}, std::byte{'M'},
                                  std::byte{2},   std::byte{10},  std::byte{2}};
  text(expected, "error");
  text(expected, name);
  text(expected, "detail");
  expected.push_back(std::byte{3});
  variable(expected, detail);
  return expected;
}
void provider_case(RawOperationBranch branch, bool conversion_cut = false,
                   bool oversize = false) {
  Fixture fixture{{4 * 1024 * 1024, 16 * 1024 * 1024}};
  const auto business_error = branch == RawOperationBranch::error
                                  ? std::optional{Error{ErrorCode::io, EIO}}
                              : branch == RawOperationBranch::allocation
                                  ? std::optional{Error{ErrorCode::allocation}}
                              : branch == RawOperationBranch::returned
                                  ? std::nullopt
                                  : std::optional{Error{ErrorCode::external_unknown}};
  const auto expected = branch == RawOperationBranch::error
                            ? provider_error_packet("io", EIO)
                        : branch == RawOperationBranch::allocation
                            ? provider_error_packet("allocation", 0)
                        : branch == RawOperationBranch::returned
                            ? provider_packet()
                            : provider_error_packet("external_unknown", 0);
  ReturnedProvider provider;
  provider.branch = branch;
  provider.conversion_cut = conversion_cut;
  provider.oversize = oversize;
  const auto records = fixture.capture([&] {
    AuditLog log{*fixture.root};
    ContextStore context{log};
    CodingEngine engine{log, context, provider, "native.fixture"};
    operation_cut::literal = {reinterpret_cast<const char *>(expected.data()),
                              expected.size()};
    operation_cut::refused = false;
    std::optional<Error> caller;
    std::optional<int> nonstandard_caller;
    try {
      engine.turn(
          {"", "arco.request({retry_policy={max_attempts=1,base_ms=0,cap_ms=0}})"});
    } catch (const Error &error) {
      caller = error;
    } catch (int value) {
      nonstandard_caller = value;
    }
    operation_cut::stop();
    operation_cut::require_result_phase = false;
    require(provider.calls == 1, "one genuine native adapter invocation");
    require(nonstandard_caller.has_value() ==
                (branch == RawOperationBranch::nonstandard),
            "nonstandard provider throw reaches actual caller unchanged");
    if (nonstandard_caller)
      require(*nonstandard_caller == 93, "actual provider nonstandard value");
    if (conversion_cut)
      require(caller && caller->code == ErrorCode::allocation &&
                  operation_cut::refused && operation_cut::result_phase_seen &&
                  operation_cut::acquired_extent >= expected.size(),
              "actual provider result vector acquired before next conversion "
              "allocation refuses");
    else if (oversize)
      require(caller && caller->code == ErrorCode::capacity && caller->detail == 0,
              "actual result encoder refusal reaches caller unchanged");
    else if (branch == RawOperationBranch::returned)
      require(!caller, "actual returned provider business response succeeds");
    else if (branch != RawOperationBranch::nonstandard)
      require(caller && caller->code == business_error->code &&
                  caller->detail == business_error->detail,
              "actual normalized provider Error reaches caller unchanged");
  });
  const auto owner = only(records, 2400).reference;
  effect_branch(records, owner, branch,
                branch == RawOperationBranch::error
                    ? std::optional{Error{ErrorCode::io, EIO}}
                    : std::nullopt);
  original_links(records, owner);
  dispatch_result(records, owner);
  auto report = body(only(records, 2408, owner), owner);
  require(read<bool>(report) && read<bool>(report) == !business_error.has_value() &&
              read<bool>(report),
          "actual report separates effect failure from successful receipt recording");
  if (business_error)
    require(read<ErrorCode>(report) == business_error->code &&
                read<std::int64_t>(report) == business_error->detail,
            "dispatch report preserves actual normalized business Error");
  require(report.empty(), "dispatch report contains no invented fields");
  std::optional<OperationAttemptId> acquired_attempt;
  for (const auto &record : records) {
    if (record.site != 2403)
      continue;
    auto bytes = body(record, owner);
    if (read<std::uint8_t>(bytes) == 2)
      acquired_attempt =
          unwrap(OperationAttemptId::from_bytes(read<IdentityBytes>(bytes)));
  }
  if (!acquired_attempt)
    throw std::runtime_error{"actual provider attempt identity missing"};
  const auto attempt = acquired_attempt.value();
  const auto state = unwrap(fixture.root->attempt(attempt));
  if (!state.receipt)
    throw std::runtime_error{"actual provider adapter receipt missing"};
  require(state.receipt->attempt == attempt &&
              state.receipt->failure.has_value() == business_error.has_value(),
          "actual semantic adapter receipt retains effect outcome");
  if (business_error) {
    if (!state.receipt->failure)
      throw std::runtime_error{"actual failed provider receipt lacks its Error"};
    require(state.receipt->failure->code == business_error->code &&
                state.receipt->failure->detail == business_error->detail,
            "semantic receipt preserves actual normalized business Error");
  }
  if (conversion_cut || oversize || branch == RawOperationBranch::nonstandard) {
    terminal_branch(records, owner,
                    conversion_cut ? RawOperationBranch::allocation
                    : oversize     ? RawOperationBranch::error
                                   : RawOperationBranch::nonstandard,
                    oversize ? std::optional{Error{ErrorCode::capacity}}
                             : std::nullopt);
    require(!state.observation && std::none_of(records.begin(), records.end(),
                                               [](const auto &record) {
                                                 return record.site == 2410 ||
                                                        record.site == 2412 ||
                                                        record.site == 2413 ||
                                                        record.site == 2414;
                                               }),
            "pre-retention failure invents no final disposition or settlement");
  } else {
    terminal_branch(records, owner,
                    business_error ? RawOperationBranch::error
                                   : RawOperationBranch::returned,
                    business_error);
    const auto expected_disposition =
        business_error ? AttemptDisposition::unknown : AttemptDisposition::success;
    auto disposition = body(only(records, 2412, owner), owner);
    require(
        read<AttemptDisposition>(disposition) == expected_disposition &&
            !read<bool>(disposition) && !read<bool>(disposition) &&
            !read<bool>(disposition) &&
            read<bool>(disposition) == business_error.has_value(),
        "actual provider disposition reflects known receipt and effect uncertainty");
    if (business_error)
      require(read<ErrorCode>(disposition) == business_error->code &&
                  read<std::int64_t>(disposition) == business_error->detail,
              "disposition preserves actual business Error");
    require(disposition.empty(), "disposition has exact native fields");
    auto settlement = body(only(records, 2413, owner), owner);
    require(read<bool>(settlement) == business_error.has_value() &&
                !read<bool>(settlement) &&
                read<IdentityBytes>(settlement) == attempt.bytes() &&
                read<AttemptPhase>(settlement) == AttemptPhase::terminal &&
                read<AttemptDisposition>(settlement) == expected_disposition &&
                read<std::size_t>(settlement) > 0 && settlement.empty(),
            "actual settlement request records terminal observation for same attempt");
    auto settled = body(only(records, 2414, owner), owner);
    require(read<RawOperationBranch>(settled) == RawOperationBranch::returned &&
                !read<bool>(settled),
            "actual settlement accepted new terminal record");
    const auto journal = read<IdentityBytes>(settled);
    const auto sequence = read<std::uint64_t>(settled);
    require(
        read<RetainedEvidence>(settled) == RetainedEvidence::live && settled.empty() &&
            journal == fixture.root->cursor().journal.bytes() && sequence > 0 &&
            state.observation && state.observation->phase == AttemptPhase::terminal &&
            state.observation->disposition == expected_disposition,
        "accepted settlement and semantic terminal agree on native attempt outcome");
  }
  std::size_t primary = 0;
  for (const auto &record : records) {
    if (record.site != 1322)
      continue;
    ByteView bytes{record.bytes};
    (void)reference(bytes);
    if (read<std::uint8_t>(bytes) != 3)
      continue;
    const auto count = read<std::size_t>(bytes);
    require(count == bytes.size(), "provider source exact actual length");
    require(std::equal(bytes.begin(), bytes.end(), expected.begin(), expected.end()),
            "provider result/error matches independent literal BBM2");
    ++primary;
  }
  require(primary == static_cast<std::size_t>(
                         branch != RawOperationBranch::nonstandard && !oversize),
          "one original only when an actual successful result encoder returned");
  require(std::none_of(records.begin(), records.end(),
                       [&](const auto &record) {
                         if (record.site != 2402)
                           return false;
                         auto bytes = body(record, owner);
                         return read<std::uint8_t>(bytes) == 1;
                       }),
          "provider selected result is referenced without duplicate full original");
  if (oversize) {
    bool refused = false;
    for (const auto &record : records) {
      if (record.site != 2418)
        continue;
      auto bytes = body(record, owner);
      if (read<std::uint8_t>(bytes) != 1)
        continue;
      require(read<std::uint8_t>(bytes) == 3 &&
                  read<ErrorCode>(bytes) == ErrorCode::capacity,
              "actual oversized result encoder native Error");
      refused = true;
    }
    require(refused, "missing oversize encoder refusal");
  }
  metadata_has_no_writer_time(*fixture.root);
}
void presentation_throw(RawOperationBranch branch) {
  Fixture fixture{{65536, 4 * 1024 * 1024}};
  const auto target = (fixture.directory.path / "presented.bin").string();
  const auto expected = write_result(target);
  const auto records = fixture.capture([&] {
    AuditLog log{*fixture.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    engine.operation_completed = [branch](std::string_view) {
      if (branch == RawOperationBranch::error)
        throw Error{ErrorCode::io, EPIPE};
      if (branch == RawOperationBranch::allocation)
        throw std::bad_alloc{};
      if (branch == RawOperationBranch::length)
        throw std::length_error{"native presentation"};
      if (branch == RawOperationBranch::standard)
        throw std::runtime_error{"native presentation"};
      throw 91;
    };
    bool caught = false;
    try {
      (void)engine.operator_call(
          "write_file",
          Value::object({{"path", Value{target}}, {"content", Value{"exact"}}}));
    } catch (...) {
      caught = true;
    }
    require(caught, "actual presentation exception reaches caller");
  });
  const auto owner = only(records, 2400).reference;
  require(std::count_if(records.begin(), records.end(),
                        [&](const auto &record) {
                          return record.site == 2402 &&
                                 record.bytes == original(owner, 1, expected);
                        }) == 1,
          "exact earlier result survives presentation throw");
  effect_branch(records, owner, RawOperationBranch::returned);
  bool seen = false;
  for (const auto &record : records) {
    if (record.site != 2416)
      continue;
    auto bytes = body(record, owner);
    if (read<std::uint8_t>(bytes) != 5)
      continue;
    require(read<RawOperationBranch>(bytes) == branch,
            "callback native throw classification");
    if (branch == RawOperationBranch::error)
      require(read<ErrorCode>(bytes) == ErrorCode::io &&
                  read<std::int64_t>(bytes) == EPIPE,
              "callback copies actual Error");
    require(bytes.empty(), "callback return invents no fields");
    seen = true;
  }
  require(seen, "actual completion callback return recorded");
  terminal_branch(records, owner, branch,
                  branch == RawOperationBranch::error
                      ? std::optional{Error{ErrorCode::io, EPIPE}}
                      : std::nullopt);
}
void capacity_original() {
  Fixture fixture{{2048, 8192}};
  const std::string content(4096, 'Q');
  std::vector<std::byte> expected{std::byte{'B'}, std::byte{'B'}, std::byte{'M'},
                                  std::byte{2}};
  text(expected, content);
  const auto fallback = capacity_fallback();
  const auto records = fixture.capture([&] {
    AuditLog log{*fixture.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    bool refused = false;
    try {
      (void)engine.operator_call(
          "lua", Value::object({{"code", Value{"return string.rep(\"Q\", 4096)"}}}));
    } catch (const Error &error) {
      refused = error == Error{ErrorCode::capacity};
    }
    require(refused, "actual post-effect retention refusal reaches caller as capacity");
  });
  const auto owner = only(records, 2400).reference;
  for (const auto &[role, bytes] :
       std::array{std::pair{std::uint8_t{1}, ByteView{expected}},
                  std::pair{std::uint8_t{2}, ByteView{fallback}}})
    require(
        std::count_if(records.begin(), records.end(),
                      [&](const auto &record) {
                        return record.site == 2402 &&
                               record.bytes == original(owner, role, bytes);
                      }) == 1,
        "complete original and literal capacity fallback survive retention refusal");
  effect_branch(records, owner, RawOperationBranch::returned);
  terminal_branch(records, owner, RawOperationBranch::error,
                  Error{ErrorCode::capacity});
  bool message = false;
  constexpr std::string_view expected_message =
      "effect outcome unknown; no replay; earlier originals retained, rejected bytes "
      "not retained";
  for (const auto &record : records)
    message =
        message ||
        (record.site == 2402 &&
         record.bytes == original(owner, 3,
                                  std::as_bytes(std::span{expected_message.data(),
                                                          expected_message.size()})));
  require(message, "actual bounded capacity message original preserved");
}
void raw_inactive() {
  Fixture fixture{{65536, 4 * 1024 * 1024}};
  AuditLog log{*fixture.root};
  ContextStore context{log};
  Unused provider;
  CodingEngine engine{log, context, provider, "native.fixture"};
  const auto target = (fixture.directory.path / "inactive.bin").string();
  require(!raw_enabled() &&
              field(engine.operator_call(
                        "write_file", Value::object({{"path", Value{target}},
                                                     {"content", Value{"literal"}}})),
                    "written") == Value{true},
          "raw-inactive native business return remains intact");
  require(unwrap(discover_raw_runs(fixture.directory.path.string())).empty(),
          "raw-inactive operation creates no invented run/reference");
}
void refused_dispatch() {
  Fixture fixture{{65536, 4 * 1024 * 1024}};
  const auto target = (fixture.directory.path / "refused-open.bin").string();
  const auto records = fixture.capture([&] {
    AuditLog log{*fixture.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    const auto journal = (fixture.directory.path / "audit").string();
    operation_cut::prepare(target.c_str(), {}, journal.c_str());
    operation_cut::ready = false;
    operation_cut::open_cut = true;
    operation_cut::open_refused = false;
    bool caught = false;
    try {
      (void)engine.operator_call(
          "write_file",
          Value::object({{"path", Value{target}}, {"content", Value{"no effect"}}}));
    } catch (const Error &error) {
      caught = error == Error{ErrorCode::audit_unavailable};
    }
    operation_cut::stop();
    operation_cut::open_cut = false;
    require(caught && operation_cut::open_refused && !std::filesystem::exists(target),
            "actual attempt-open native EIO refuses before effect; outer turn cannot "
            "record rollback");
  });
  const auto owner = only(records, 2400).reference;
  auto result = body(only(records, 2421, owner), owner);
  require(read<std::uint8_t>(result) == 1 && read<bool>(result) &&
              read<ErrorCode>(result) == ErrorCode::io &&
              read<std::int64_t>(result) == EIO && result.empty(),
          "actual failed outer dispatch Result preserves native EIO");
  require(only(records, 2420, owner).reference.ordinal <
              only(records, 2421, owner).reference.ordinal,
          "dispatch request and actual Result are distinct ordered events");
  require(std::none_of(records.begin(), records.end(),
                       [](const auto &record) {
                         return record.site == 2406 || record.site == 2407 ||
                                record.site == 2408;
                       }),
          "refused dispatch creates no acquired effect/report fields");
  terminal_branch(records, owner, RawOperationBranch::error, Error{ErrorCode::io, EIO});
}
struct Pipe {
  std::array<int, 2> descriptors{};
  Pipe() {
    require(::pipe(descriptors.data()) == 0, "native fixture pipe");
    for (const int descriptor : descriptors)
      require(::fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 0, "fixture pipe CLOEXEC");
  }
  ~Pipe() {
    for (const int descriptor : descriptors)
      if (descriptor >= 0)
        ::close(descriptor);
  }
};
struct Resume {
  pid_t recorder;
  ~Resume() {
    if (recorder > 0)
      ::kill(recorder, SIGCONT);
  }
};
void fill_data(std::size_t leave) {
  std::vector<std::byte> filler(1024 * 1024, std::byte{'F'});
  auto &header = *operation_cut::probe;
  for (;;) {
    const auto acknowledged =
        header.partitions[0].acknowledged.value.load(std::memory_order_acquire);
    const auto published =
        header.partitions[0].published.value.load(std::memory_order_acquire);
    const auto free = header.capacities[0] - (published - acknowledged);
    if (free <= leave + 16)
      return;
    const auto total = std::min<std::uint64_t>(filler.size(), free - leave);
    const std::array fields{
        ByteView{filler}.first(static_cast<std::size_t>(total - 16))};
    (void)unwrap(raw_emit(2499, fields));
  }
}
void blocked_admission(bool deadline) {
  Fixture fixture{{65536, 4 * 1024 * 1024}};
  Pipe cancellation;
  const auto target = (fixture.directory.path / "never-written.bin").string();
  const auto records = fixture.capture([&](RawRuntime &runtime,
                                           const RawThreadHandle &) {
    AuditLog log{*fixture.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    const auto status = unwrap(runtime.status());
    Resume resume{status.recorder};
    std::optional<RawWaitScope> scope;
    operation_cut::space_wait = false;
    operation_cut::cancellation_watch = cancellation.descriptors[0];
    engine.operation_started = [&](std::string_view) {
      require(::kill(status.recorder, SIGSTOP) == 0, "stop actual recorder");
      fill_data(4096);
      RawWaitControl control;
      control.cancellation_fds[0] = cancellation.descriptors[0];
      if (deadline)
        control.deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds{20};
      else
        operation_cut::space_cancel = cancellation.descriptors[1];
      scope.emplace(control);
    };
    bool refused = false;
    try {
      (void)engine.operator_call(
          "write_file",
          Value::object({{"path", Value{target}}, {"content", Value{"no effect"}}}));
    } catch (const Error &) {
      refused = true;
    }
    scope.reset();
    operation_cut::cancellation_watch = -1;
    require(refused && operation_cut::space_wait && !std::filesystem::exists(target),
            "actual blocked lane-space admission interrupts before external write");
  });
  const auto owner = only(records, 2400).reference;
  terminal_branch(records, owner, RawOperationBranch::error,
                  Error{ErrorCode::interrupted, deadline ? ETIMEDOUT : ECANCELED});
  require(std::none_of(records.begin(), records.end(),
                       [](const auto &record) {
                         return record.site == 2406 || record.site == 8000 ||
                                record.site == 8001;
                       }),
          "interrupted admission has no acquired effect or emitter fence");
}
void cancellation_after_effect() {
  Fixture fixture{{65536, 4 * 1024 * 1024}};
  Pipe cancellation;
  const auto target = (fixture.directory.path / "canceled-after-write.bin").string();
  const auto expected = write_result(target);
  const auto records = fixture.capture([&] {
    AuditLog log{*fixture.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    RawWaitControl control;
    control.cancellation_fds[0] = cancellation.descriptors[0];
    RawWaitScope scope{control};
    operation_cut::target = target.c_str();
    operation_cut::rename_cancel = cancellation.descriptors[1];
    try {
      (void)engine.operator_call(
          "write_file",
          Value::object({{"path", Value{target}}, {"content", Value{"preserved"}}}));
    } catch (const Error &) {
      // Later turn work can truthfully refuse; inspect the operation's actual
      // effect/receipt/original, independently of that different caller boundary.
    }
    operation_cut::target = nullptr;
    require(operation_cut::rename_cancel == -1,
            "actual target rename wrote sticky cancellation");
  });
  const auto owner = only(records, 2400).reference;
  effect_branch(records, owner, RawOperationBranch::returned);
  require(std::count_if(records.begin(), records.end(),
                        [&](const auto &record) {
                          return record.site == 2402 &&
                                 record.bytes == original(owner, 1, expected);
                        }) == 1,
          "post-effect sticky cancellation preserves exact already paid result");
  auto report = body(only(records, 2408, owner), owner);
  require(
      read<bool>(report) && read<bool>(report) && read<bool>(report) && report.empty(),
      "prepaid receipt records actual success despite post-effect sticky cancellation");
  terminal_branch(records, owner, RawOperationBranch::returned);
  require(std::none_of(records.begin(), records.end(),
                       [](const auto &record) {
                         return record.site == 8000 || record.site == 8001;
                       }),
          "paid cancellation cleanup does not fence emitter");
}
void persistent_initial() {
  Fixture fixture{{65536, 4 * 1024 * 1024}};
  const auto fifo = (fixture.directory.path / "child-gate").string();
  require(::mkfifo(fifo.c_str(), 0600) == 0, "native persistent child gate");
  const auto command = "read line < '" + fifo + "'; printf literal-final; exit 7";
  const auto records = fixture.capture([&] {
    AuditLog log{*fixture.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    const auto result = engine.operator_call(
        "exec",
        Value::object({{"command", Value{command}}, {"yield_ms", Value{Number{0}}}}));
    require(!result.find("exit_code") && string_field(result, "state") != "completed",
            "actual initial persistent operation has no fabricated exit");
    const auto id = string_field(result, "job_id");
    const int release = ::open(fifo.c_str(), O_WRONLY | O_CLOEXEC);
    require(release >= 0, "open actual blocked child gate");
    operation_cut::notify(release);
    ::close(release);
    const auto final = engine.command_jobs()->wait(
        id, {std::nullopt, 16 * 1024 * 1024, false}, [&] { engine.poll_commands(); },
        [] { return false; });
    require(field(final, "exit_code").number().text() == "7",
            "genuine later child exit acquired");
    engine.poll_commands();
  });
  const auto owner = only(records, 2400).reference;
  terminal_branch(records, owner, RawOperationBranch::returned);
  auto disposition = body(only(records, 2412, owner), owner);
  require(read<AttemptDisposition>(disposition) == AttemptDisposition::success &&
              !read<bool>(disposition) && read<bool>(disposition),
          "initial success/persistent fact differs from later nonzero job exit");
  const Record *terminal = &only(records, 2417, owner);
  bool later_job_return = false;
  for (const auto &record : records) {
    if (record.site == 2402) {
      auto bytes = body(record, owner);
      if (read<std::uint8_t>(bytes) == 1) {
        const auto count = read<std::size_t>(bytes);
        require(count == bytes.size(), "persistent source range exact");
        const auto decoded = unwrap(decode_packet(bytes));
        require(!decoded.find("exit_code") &&
                    string_field(decoded, "state") != "completed",
                "immutable initial original has no later synthesized exit");
      }
    }
    // Different worker lanes are ordered by their own Ref; do not compare ordinals
    // across lanes.
    later_job_return = later_job_return || record.site == 2034;
  }
  require(terminal != nullptr && later_job_return,
          "later actual job lifecycle remains separately emitted");
}
struct ChildLane {
  std::array<char, 32> name;
  IdentityBytes epoch;
  RawCapacities capacities;
};
void native_read_exact(int descriptor, void *data, std::size_t count) {
  auto *bytes = static_cast<unsigned char *>(data);
  while (count) {
    const auto result = ::read(descriptor, bytes, count);
    if (result < 0 && errno == EINTR)
      continue;
    require(result > 0, "actual child notice/read reached EOF");
    bytes += result;
    count -= static_cast<std::size_t>(result);
  }
}
void native_write_exact(int descriptor, const void *data, std::size_t count) {
  const auto *bytes = static_cast<const unsigned char *>(data);
  while (count) {
    const auto result = ::write(descriptor, bytes, count);
    if (result < 0 && errno == EINTR)
      continue;
    require(result > 0, "actual child notice/write failed");
    bytes += result;
    count -= static_cast<std::size_t>(result);
  }
}
void dispatch_allocation() {
  Fixture fixture{{65536, 4 * 1024 * 1024}};
  const auto target = (fixture.directory.path / "dispatch-no-effect.bin").string();
  const auto records = fixture.capture([&] {
    AuditLog log{*fixture.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    operation_cut::dispatch_copy_refused = false;
    operation_cut::dispatch_copy_cut = true;
    std::optional<Error> caller;
    try {
      (void)engine.operator_call(
          "write_file", Value::object({{"path", Value{target}},
                                       {"content", Value{"must not be written"}}}));
    } catch (const Error &error) {
      caller = error;
    }
    operation_cut::dispatch_copy_cut = false;
    require(operation_cut::dispatch_copy_refused && caller &&
                caller->code == ErrorCode::allocation && caller->detail == 0,
            "actual dispatch checkpoint copy allocation reaches native caller");
    require(!std::filesystem::exists(target), "dispatch exception performs no effect");
  });
  const auto owner = only(records, 2400).reference;
  auto request = body(only(records, 2420, owner), owner);
  (void)read<IdentityBytes>(request);
  require(request.empty(), "actual dispatch request before allocation refusal");
  auto returned = body(only(records, 2421, owner), owner);
  require(read<RawOperationBranch>(returned) == RawOperationBranch::allocation &&
              !read<bool>(returned) && returned.empty(),
          "dispatch thrown allocation has no invented Result or Error members");
  require(std::none_of(records.begin(), records.end(),
                       [](const auto &record) {
                         return record.site == 2406 || record.site == 2408;
                       }),
          "no effect or acquired report after actual dispatch throw");
  terminal_branch(records, owner, RawOperationBranch::allocation);
}
void producer_death() {
  Fixture fixture{{65536, 4 * 1024 * 1024}};
  Pipe ready, published, release, begin;
  const auto target = (fixture.directory.path / "death-original.bin").string();
  const auto expected = write_result(target);
  RawEventRef original_ref;
  {
    auto runtime = unwrap(
        RawRuntime::open(fixture.directory.path.string(), fixture.header.environment,
                         {{64 * 1024 * 1024, 1024 * 1024, 4096}, 4096}));
    const auto status = unwrap(runtime->status());
    Resume resume{status.recorder};
    const pid_t child = ::fork();
    require(child >= 0, "native owner fork");
    if (child == 0) {
      ::close(ready.descriptors[0]);
      ::close(published.descriptors[0]);
      ::close(release.descriptors[1]);
      ::close(begin.descriptors[1]);
      try {
        operation_cut::register_watch = true;
        auto handle = unwrap(runtime->prepare_thread());
        operation_cut::register_watch = false;
        const ChildLane lane{operation_cut::lane_name, operation_cut::lane_epoch,
                             operation_cut::lane_capacity};
        native_write_exact(ready.descriptors[1], &lane, sizeof(lane));
        char permission = 0;
        native_read_exact(begin.descriptors[0], &permission, 1);
        RawThreadBinding binding{handle};
        AuditLog log{*fixture.root};
        ContextStore context{log};
        Unused provider;
        CodingEngine engine{log, context, provider, "native.fixture"};
        const auto journal = (fixture.directory.path / "audit").string();
        operation_cut::prepare(
            target.c_str(),
            {reinterpret_cast<const char *>(expected.data()), expected.size()},
            journal.c_str());
        operation_cut::publication_notice = published.descriptors[1];
        operation_cut::publication_release = release.descriptors[0];
        (void)engine.operator_call(
            "write_file",
            Value::object({{"path", Value{target}},
                           {"content", Value{"death preserves actual original"}}}));
      } catch (...) {
      }
      ::_exit(3);
    }
    struct KillChild {
      pid_t pid;
      ~KillChild() {
        if (pid > 0) {
          ::kill(pid, SIGKILL);
          ::waitpid(pid, nullptr, 0);
        }
      }
    } owner{child};
    ::close(ready.descriptors[1]);
    ready.descriptors[1] = -1;
    ::close(published.descriptors[1]);
    published.descriptors[1] = -1;
    ::close(release.descriptors[0]);
    release.descriptors[0] = -1;
    ::close(begin.descriptors[0]);
    begin.descriptors[0] = -1;
    ChildLane lane{};
    native_read_exact(ready.descriptors[0], &lane, sizeof(lane));
    require(::kill(status.recorder, SIGSTOP) == 0,
            "stop real recorder after registration and before child acquisition");
    const char permission = 1;
    native_write_exact(begin.descriptors[1], &permission, 1);
    char publication = 0;
    native_read_exact(published.descriptors[0], &publication, 1);
    auto channel =
        unwrap(RawChannel::open(lane.name.data(), lane.epoch, lane.capacities));
    const auto cursors = unwrap(channel.cursors(RawPartition::data));
    require(cursors.published > cursors.acknowledged && cursors.acknowledged == 0,
            "actual raw original cut has real unacknowledged P>A");
    const auto data = unwrap(channel.read(RawPartition::data, 0, 64 * 1024 * 1024));
    ByteView bytes{data.bytes};
    bool found = false;
    while (!bytes.empty()) {
      const auto length = read<std::uint32_t>(bytes);
      const auto site = read<std::uint32_t>(bytes);
      const auto ordinal = read<std::uint64_t>(bytes);
      require(length >= 16 && bytes.size() >= length - 16,
              "actual publication envelope");
      if (site == 2402) {
        auto body_bytes = bytes.first(length - 16);
        (void)reference(body_bytes);
        if (read<std::uint8_t>(body_bytes) == 1) {
          require(read<std::size_t>(body_bytes) == expected.size() &&
                      std::equal(body_bytes.begin(), body_bytes.end(), expected.begin(),
                                 expected.end()),
                  "unacknowledged original is exact native successful encoder output");
          original_ref = {lane.epoch, ordinal};
          found = true;
        }
      }
      bytes = bytes.subspan(length - 16);
    }
    require(found, "no primary at real pre-receipt death cut");
    require(::kill(child, SIGKILL) == 0, "kill only owned producer holder");
    int child_status = 0;
    require(::waitpid(child, &child_status, 0) == child && WIFSIGNALED(child_status) &&
                WTERMSIG(child_status) == SIGKILL,
            "actual producer died at selected acquired-original cut");
    owner.pid = -1;
    // The pause establishes P>A at the acquisition cut. Resume the owned
    // recorder before recovery asks it to exit through its signal handler.
    require(::kill(status.recorder, SIGCONT) == 0,
            "resume real recorder after producer exit and preserved-prefix check");
    resume.recorder = -1;
    unwrap(runtime->recover());
  }
  const auto runs = unwrap(discover_raw_runs(fixture.directory.path.string()));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "real producer death recovery closes raw run");
  auto reader = unwrap(RawRunReader::open(
      (fixture.directory.path / "raw" / runs.front().name).string()));
  std::vector<std::byte> output(32 * 1024 * 1024);
  bool found = false, fabricated = false;
  while (const auto acquired = unwrap(reader->read(output))) {
    ByteView bytes{acquired->raw};
    (void)read<std::uint32_t>(bytes);
    const auto site = read<std::uint32_t>(bytes);
    const auto ordinal = read<std::uint64_t>(bytes);
    if (same({acquired->origin.lane, ordinal}, original_ref)) {
      require(site == 2402, "recovery preserved original site and Ref");
      (void)reference(bytes);
      require(
          read<std::uint8_t>(bytes) == 1 &&
              read<std::size_t>(bytes) == expected.size() &&
              std::equal(bytes.begin(), bytes.end(), expected.begin(), expected.end()),
          "recovered acquired original bytes unchanged");
      found = true;
    }
    fabricated = fabricated || site == 2417;
  }
  require(found && !fabricated,
          "death preserves acquired original without fabricated owner completion");
}
void maintenance_loan() {
  Fixture fixture{{65536, 4 * 1024 * 1024}};
  std::cerr << "maintenance initial snapshot\n";
  {
    AuditLog log{*fixture.root};
    ContextStore context{log};
    (void)session_identity(log);
    require(field(context.edit(Value::object({{"base", Value{context.head()}},
                                              {"entries", Value{Value::Array{}}}})),
                  "accepted") == Value{true},
            "actual Context edit establishes a valid saved revision");
    std::cerr << "maintenance initial state="
              << static_cast<unsigned>(fixture.root->state())
              << " seq=" << fixture.root->cursor().sequence << "\n";
    unwrap(context.checkpoint());
    require(fixture.root->saved_state() &&
                fixture.root->saved_state()->context.find("semantic_coverage"),
            "actual initial checkpoint supports compact semantic coverage");
  }
  std::cerr << "maintenance reopen compact\n";
  fixture.root.reset();
  fixture.root = unwrap(RetainedState::open(
      std::make_unique<NativeJournalDirectory>(
          unwrap(NativeJournalDirectory::open(fixture.directory.path.string()))),
      "audit", fixture.header, {64 * 1024 * 1024, 8192}, false, false));
  unwrap(fixture.root->confirm_recovery());
  struct EmptyCustody final : CustodyVerifier {
    Result<void> verify(std::span<const AttemptState> attempts) override {
      return attempts.empty() ? Result<void>::success()
                              : Result<void>::failure({ErrorCode::conflict});
    }
  } custody;
  unwrap(fixture.root->reconcile(custody));
  require(fixture.root->compact_recovery(),
          "actual maintenance fixture reopens compact");
  AuditLog log{*fixture.root};
  ContextStore context{log};
  std::vector<RetainedEvent> prefix;
  for (std::uint64_t index = 0; index != 128; ++index)
    prefix.push_back({{}, IssuerReservationEvent{10000 + index}});
  (void)unwrap(fixture.root->append(fixture.root->cursor(), {}, prefix));
  bool called = false;
  const auto records = fixture.capture([&] {
    std::cerr << "maintenance owner claim\n";
    auto bank = unwrap(raw_claim_control(1024));
    const auto before = bank.remaining();
    fixture.root->set_maintenance_checkpoint([&](const RawEventRef &parent) {
      std::cerr << "maintenance actual callback loan=" << bank.remaining()
                << " before=" << before << "\n";
      require(!called && raw_region_prepaid_bank() == nullptr &&
                  bank.remaining() == before - 328,
              "actual maintenance begins with caller loan suspended");
      called = true;
      const auto outcome = context.checkpoint({parent});
      require(
          raw_region_prepaid_bank() == nullptr && bank.remaining() == before - 328,
          "actual Context checkpoint uses its own bank and restores absent selection");
      return outcome;
    });
    RawRegionPrepaidScope selection{bank};
    const std::array event{RetainedEvent{{}, IssuerReservationEvent{20000}}};
    std::cerr << "maintenance actual append\n";
    (void)unwrap(fixture.root->append(fixture.root->cursor(), {}, event));
    require(raw_region_prepaid_bank() == &bank && bank.remaining() == before - 328,
            "only actual direct append region consumes caller bank");
  });
  require(called, "actual compact maintenance checkpoint did not run");
  require(std::count_if(records.begin(), records.end(),
                        [](const auto &record) { return record.site == 1090; }) == 1,
          "checkpoint did not invent a nested retained append on caller credit");
}
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view{argv[1]} == "dispatch_allocation") {
      dispatch_allocation();
      return 0;
    }
    credits();
    whole_request(false, false);
    whole_request(true, false);
    whole_request(false, true);
    rejection(false);
    rejection(true);
    rejection(true, true);
    native_clocks();
    std::cerr << "provider normal\n";
    provider_case(RawOperationBranch::returned);
    std::cerr << "provider conversion\n";
    provider_case(RawOperationBranch::returned, true);
    std::cerr << "provider oversize\n";
    provider_case(RawOperationBranch::returned, false, true);
    for (const auto branch : {RawOperationBranch::error, RawOperationBranch::allocation,
                              RawOperationBranch::length, RawOperationBranch::standard,
                              RawOperationBranch::nonstandard}) {
      std::cerr << "provider variant " << static_cast<unsigned>(branch) << "\n";
      provider_case(branch);
      presentation_throw(branch);
    }
    std::cerr << "retention capacity\n";
    capacity_original();
    std::cerr << "outer dispatch refusal\n";
    refused_dispatch();
    std::cerr << "full DATA cancellation\n";
    blocked_admission(false);
    std::cerr << "full DATA deadline\n";
    blocked_admission(true);
    std::cerr << "post-effect cancellation\n";
    cancellation_after_effect();
    std::cerr << "maintenance loan\n";
    maintenance_loan();
    std::cerr << "persistent initial/later\n";
    persistent_initial();
    std::cerr << "producer death P>A\n";
    producer_death();
    std::cerr << "dispatch allocation\n";
    dispatch_allocation();
    raw_inactive();
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
