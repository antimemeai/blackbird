#include "../src/native_process.hpp"
#include "blackbird/backstop.hpp"
#include "blackbird/json.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/raw_file.hpp"
#include "blackbird/raw_reader.hpp"
#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <iostream>
#include <new>
#include <source_location>
#include <sys/socket.h>
#include <unistd.h>
using namespace blackbird;
namespace {
thread_local bool allocation_armed = false, allocation_hit = false,
                  allocation_busy = false, entropy_failure = false;
bool selected_allocation() noexcept {
  if (!allocation_armed || allocation_busy)
    return false;
  allocation_busy = true;
  std::array<void *, 48> frames{};
  const int count = ::backtrace(frames.data(), static_cast<int>(frames.size()));
  bool original = false, backstop = false, turn = false;
  for (int index = 0; index < count; ++index) {
    Dl_info info{};
    if (::dladdr(frames[static_cast<std::size_t>(index)], &info) && info.dli_sname) {
      original |= std::strstr(info.dli_sname, "AuditLog") &&
                  std::strstr(info.dli_sname, "original");
      backstop |= std::strstr(info.dli_sname, "run_backstop") != nullptr;
      turn |= std::strstr(info.dli_sname, "CodingEngine") &&
              std::strstr(info.dli_sname, "turn");
    }
  }
  allocation_busy = false;
  if (!original || !backstop || turn)
    return false;
  allocation_armed = false;
  allocation_hit = true;
  return true;
}
} // namespace
extern "C" int getentropy(void *output, std::size_t size) {
  using Function = int (*)(void *, std::size_t);
  static const auto native_function = [] {
    const auto symbol = ::dlsym(RTLD_NEXT, "getentropy");
    Function result{};
    static_assert(sizeof(result) == sizeof(symbol));
    std::memcpy(&result, &symbol, sizeof(result));
    if (!result)
      ::_exit(72);
    return result;
  }();
  if (entropy_failure) {
    entropy_failure = false;
    errno = EIO;
    return -1;
  }
  return native_function(output, size);
}
void *operator new(std::size_t size) {
  if (selected_allocation())
    throw std::bad_alloc{};
  if (void *value = std::malloc(size ? size : 1))
    return value;
  throw std::bad_alloc{};
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *value) noexcept { std::free(value); }
void operator delete[](void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }
void operator delete[](void *value, std::size_t) noexcept { std::free(value); }

void require(bool value, std::source_location where = std::source_location::current()) {
  if (!value) {
    std::cerr << "require line " << where.line() << "\n";
    throw Error{ErrorCode::conflict};
  }
}
template <class T> T id(unsigned char n) {
  IdentityBytes b{};
  b[0] = std::byte{n};
  return unwrap(T::from_bytes(b));
}
class Failed final : public CodingProvider {
public:
  Value respond(const Value &,
                const std::function<void(std::string_view)> &capture) override {
    capture("incomplete remote response: outcome unknown");
    throw Error{ErrorCode::external_unknown};
  }
};
class Assessor final : public CodingProvider {
public:
  std::string artifact, dirty;
  bool no_progress = false, cancel = false;
  std::string action_override, oracle_override;
  std::function<void()> on_request;
  int calls = 0;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    require(field(request, "tools").array().empty());
    const auto encoded = unwrap(dump_json(field(request, "input")));
    require(encoded.find("outcome_unknown") != std::string::npos);
    require(encoded.find("replay_authority") != std::string::npos);
    require(encoded.find("incomplete remote response") == std::string::npos);
    if (on_request)
      on_request();
    if (cancel)
      throw Error{ErrorCode::interrupted};
    std::string action =
        no_progress
            ? "error('same failure, no progress')"
            : "local r=arco.call('read_file',{path='" + dirty +
                  "'}); "
                  "assert(r.content=='dirty work preserved'); "
                  "local w=arco.call('write_file',{path='" +
                  artifact + "',content='reconciled useful work'}); assert(w.written)";
    if (!action_override.empty())
      action = action_override;
    auto choice = Value::object(
        {{"retained", Value{"original audit and dirty source"}},
         {"uncertain", Value{"provider remote completion unknown"}},
         {"change", Value{"inspect file, do not repeat provider"}},
         {"next", Value{"deliver reconciled artifact"}},
         {"action", Value{action}},
         {"oracle", Value::object({{"path", Value{artifact}},
                                   {"content", Value{"reconciled useful work"}}})}});
    Value response = Value::object(
        {{"output",
          Value{Value::Array{Value::object(
              {{"type", Value{"message"}},
               {"role", Value{"assistant"}},
               {"id", Value{"assessment"}},
               {"content", Value{Value::Array{Value::object(
                               {{"type", Value{"output_text"}},
                                {"text", Value{unwrap(dump_json(choice))}}})}}}})}}}});
    capture(unwrap(dump_json(response)));
    return response;
  }
};
struct Fixture {
  std::filesystem::path path, work;
  std::unique_ptr<RetainedState> root;
  std::unique_ptr<AuditLog> log;
  std::unique_ptr<ContextStore> context;
  Failed provider;
  Error cause{ErrorCode::external_unknown};
  std::unique_ptr<CodingEngine> engine;
  Fixture() {
    char name[] = "/tmp/arco-backstop-XXXXXX";
    const auto *made = mkdtemp(name);
    if (!made)
      throw Error{ErrorCode::io};
    path = made;
    char work_name[] = "/tmp/arco-backstop-work-XXXXXX";
    const auto *workspace = mkdtemp(work_name);
    if (!workspace)
      throw Error{ErrorCode::io};
    work = workspace;
    root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                         NativeJournalDirectory::open(path.string()))),
                                     "audit",
                                     {id<EnvironmentId>(1),
                                      id<AuditStreamId>(2),
                                      7,
                                      {1024 * 1024, 4 * 1024 * 1024},
                                      std::nullopt},
                                     {64 * 1024 * 1024, 20000}));
    log = std::make_unique<AuditLog>(*root);
    context = std::make_unique<ContextStore>(*log);
    engine = std::make_unique<CodingEngine>(*log, *context, provider, "test");
  }
  void fail() {
    const auto program = "local w=arco.call('write_file',{path='" +
                         (path / "dirty").string() +
                         "',content='dirty work preserved'}); assert(w.written); "
                         "arco.request({retry_policy={max_attempts=1}})";
    try {
      engine->turn({"Reconcile useful source work", program});
      throw std::runtime_error("expected failure");
    } catch (const Error &e) {
      require(e.code == ErrorCode::external_unknown);
      cause = e;
    }
  }
  ~Fixture() {
    engine.reset();
    context.reset();
    log.reset();
    root.reset();
    std::filesystem::remove_all(path);
    std::filesystem::remove_all(work);
  }
};
Value mission() {
  return Value::object(
      {{"mission", Value{"Reconcile file and produce useful artifact"}},
       {"steer", Value{"do not replay unknown remote request"}}});
}
Assessor assessor(const Fixture &f) {
  Assessor a;
  a.artifact = (f.work / "result").string();
  a.dirty = (f.path / "dirty").string();
  return a;
}
struct NativeRecord {
  std::uint32_t site;
  RawEventRef ref;
  std::vector<std::byte> fields;
};
template <class T> T native(ByteView &bytes) {
  require(bytes.size() >= sizeof(T));
  T result{};
  std::memcpy(&result, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return result;
}
RawEventRef reference(ByteView &bytes) {
  return {native<IdentityBytes>(bytes), native<std::uint64_t>(bytes)};
}
bool same(RawEventRef a, RawEventRef b) {
  return a.lane == b.lane && a.ordinal == b.ordinal;
}
std::vector<NativeRecord> records(const std::filesystem::path &base) {
  const auto runs = unwrap(discover_raw_runs(base.string()));
  require(runs.size() == 1);
  const auto &snapshot = runs.front().snapshot;
  require(snapshot.has_value());
  if (!snapshot)
    throw std::runtime_error{"actual source-run snapshot is absent"};
  const auto &preserved = *snapshot;
  std::cerr << "actual source-run closed=" << preserved.closed << "\n";
  require(preserved.closed);
  auto reader = unwrap(RawRunReader::open((base / "raw" / runs.front().name).string()));
  std::vector<std::byte> buffer(32 * 1024 * 1024);
  std::vector<NativeRecord> output;
  while (const auto item = unwrap(reader->read(buffer))) {
    auto body = item->raw;
    require(native<std::uint32_t>(body) == item->raw.size());
    const auto site = native<std::uint32_t>(body);
    const auto ordinal = native<std::uint64_t>(body);
    output.push_back({site, {item->origin.lane, ordinal}, {body.begin(), body.end()}});
  }
  return output;
}
std::string original(const NativeRecord &record, std::uint8_t expected) {
  ByteView fields = record.fields;
  (void)reference(fields);
  require(native<std::uint8_t>(fields) == expected);
  const auto count = native<std::size_t>(fields);
  require(count == fields.size());
  return {reinterpret_cast<const char *>(fields.data()), fields.size()};
}
void native_case(int cut) {
  Fixture fixture;
  const auto base = fixture.work / "raw-owner";
  std::filesystem::create_directories(base / "audit");
  std::filesystem::permissions(base, std::filesystem::perms::owner_all);
  std::filesystem::permissions(base / "audit", std::filesystem::perms::owner_all);
  struct Admission {
    OperationAttemptId id;
    std::size_t ordinal;
    std::string input;
  };
  std::vector<Admission> admissions;
  JournalCursor cursor = fixture.root->cursor();
  RawEventRef anchor{};
  Value result;
  std::string predecessor;
  const auto expected_request =
      cut == 5 ? Value::object({{"other", Value{"invalid mission"}}}) : mission();
  std::size_t expected_pivots = cut == 1 ? 2 : 1;
  int cancellation[2]{-1, -1};
  bool stopped = false, allocation_failed = false, failed = false;
  if (cut == 4)
    require(::socketpair(AF_UNIX, SOCK_STREAM, 0, cancellation) == 0);
  if (cut == 7)
    fixture.fail();
  {
    auto runtime = unwrap(
        RawRuntime::open(base.string(), id<EnvironmentId>(91),
                         {{cut == 7 ? 16 * 1024 * 1024 + 48U : 256 * 1024 * 1024U,
                           1024 * 1024, 128 * 1024},
                          128 * 1024}));
    auto issuer = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{issuer};
      if (cut != 7)
        fixture.fail();
      auto a = assessor(fixture);
      a.no_progress = cut == 1;
      if (cut == 3)
        a.on_request = [] {
          allocation_armed = true;
          allocation_hit = false;
        };
      if (cut == 4)
        a.action_override = "local w=arco.call(\"write_file\",{path=\"" + a.artifact +
                            "\",content=\"reconciled useful "
                            "work\"});assert(w.written);print(\"NATIVE_EFFECT_DONE\")";
      if (cut == 6)
        std::filesystem::create_directory(fixture.path / "backstop");
      if (cut == 8)
        entropy_failure = true;

      predecessor = read_file(fixture.path / "audit");
      cursor = fixture.root->cursor();
      for (std::size_t ordinal = 0; ordinal < fixture.root->fact_count(); ++ordinal) {
        const auto fact = unwrap(fixture.root->fact(ordinal));
        if (const auto *admission =
                std::get_if<AttemptAdmissionEvent>(&fact.event.body))
          admissions.push_back(
              {admission->attempt, ordinal,
               std::string{reinterpret_cast<const char *>(admission->input.data()),
                           admission->input.size()}});
      }
      if (cut == 9) {
        std::filesystem::resize_file(fixture.path / "audit", 31);
        predecessor = read_file(fixture.path / "audit");
      }
      RawRegion owner{raw_regions::command_perform};
      anchor = {owner.lane_epoch(), owner.begin_ordinal()};
      const RawAdapterContext context{nullptr, anchor, nullptr, nullptr};
      if (cut == 2) {
        try {
          (void)run_backstop(
              *fixture.engine, *fixture.log, fixture.path, mission(), a,
              {"test", "medium", ""}, [] { return true; }, {}, &context);
          require(false);
        } catch (const Error &error) {
          require(error.code == ErrorCode::interrupted);
        }
        require(a.calls == 0 && !std::filesystem::exists(fixture.path / "backstop"));
      } else {
        std::optional<RawWaitScope> wait;
        if (cut == 4)
          wait.emplace(RawWaitControl{{cancellation[0], -1}, std::nullopt});
        try {
          const auto &request = expected_request;
          result = run_backstop(
              *fixture.engine, *fixture.log, fixture.path, request, a,
              {"test", "medium", ""}, {},
              [&](std::string_view text) {
                if (cut == 4 &&
                    text.find("NATIVE_EFFECT_DONE") != std::string_view::npos) {
                  require(!stopped);
                  require(::shutdown(cancellation[1], SHUT_WR) == 0);
                  stopped = true;
                }
              },
              &context);
        } catch (const std::bad_alloc &) {
          require(cut == 3 && allocation_hit);
          allocation_failed = true;
          failed = true;
        } catch (const Error &error) {
          std::cerr << "native cut " << cut << " actual " << error_name(error.code)
                    << " " << error.detail << " stopped " << stopped << "\n";
          require((cut == 4 && stopped && error.code == ErrorCode::interrupted) ||
                  (cut == 5 && error.code == ErrorCode::corrupt) ||
                  (cut == 6 && error.code == ErrorCode::conflict &&
                   error.detail == EEXIST) ||
                  (cut == 7 && error.code == ErrorCode::capacity) ||
                  (cut == 8 && error.code == ErrorCode::io && error.detail == 0) ||
                  (cut == 9 && error.code == ErrorCode::incomplete));
          failed = true;
        }
        allocation_armed = false;
        if (cut <= 1)
          require(!failed && a.calls == static_cast<int>(expected_pivots));
        else
          require(failed && a.calls == ((cut == 3 || cut == 4) ? 1 : 0));
        require(!a.cancelled);
        if (cut == 0)
          require(string_field(result, "phase") == "useful-work-observed" &&
                  read_file(fixture.work / "result") == "reconciled useful work");
        else if (cut == 1)
          require(result.find("bound") &&
                  !std::filesystem::exists(fixture.work / "result"));
      }
      require(read_file(fixture.path / "audit") == predecessor);
      // The source VM retains its actual raw issuer. Retire it under that issuer
      // before Runtime drops its last state owner and the reader takes a snapshot.
      fixture.engine.reset();
      fixture.context.reset();
      fixture.log.reset();
      fixture.root.reset();
    }
    issuer.reset();
  }
  if (cancellation[0] >= 0) {
    (void)::close(cancellation[0]);
    (void)::close(cancellation[1]);
  }
  std::optional<JournalHeader> recovery_header;
  if (cut <= 4 && cut != 2) {
    const auto bytes =
        read_file_prefix(fixture.path / "backstop" / "audit", journal_header_size);
    recovery_header = unwrap(
        decode_journal_header(std::as_bytes(std::span{bytes.data(), bytes.size()})));
  }
  const auto raw = records(base);
  std::size_t pivots = 0, actions = 0, entropy_calls = 0;
  RawEventRef whole{};
  bool mission_bytes = false, choice_bytes = false, artifact = false, restore = false;
  std::size_t selected_admissions = 0, claim_checks = 0, entropy_returns = 0;
  auto find_record = [&](RawEventRef ref) -> const NativeRecord & {
    const auto found = std::find_if(
        raw.begin(), raw.end(), [&](const auto &item) { return same(item.ref, ref); });
    require(found != raw.end());
    return *found;
  };
  std::vector<std::pair<RawEventRef, std::uint32_t>> pending;
  for (const auto &record : raw) {
    ByteView body = record.fields;
    require(record.site != 8000 && (cut == 4 || record.site != 8001));
    if (record.site == 4700) {
      require(same(reference(body), anchor));
      require(native<IdentityBytes>(body) == cursor.journal.bytes());
      require(native<std::uint64_t>(body) == cursor.sequence &&
              native<std::uint64_t>(body) == cursor.end_offset && body.empty());
      whole = record.ref;
    }
    const auto site = record.site;
    if (site == 4700 || site == 4710 || site == 4720 || site == 4722 || site == 4725 ||
        site == 4730 || site == 4732 || site == 4740)
      pending.push_back({record.ref, site + 1});
    if (site == 4701 || site == 4711 || site == 4721 || site == 4723 || site == 4726 ||
        site == 4731 || site == 4733 || site == 4741) {
      auto fields = ByteView{record.fields};
      const auto request = reference(fields);
      const auto found =
          std::find_if(pending.begin(), pending.end(), [&](const auto &pair) {
            return same(pair.first, request) && pair.second == site;
          });
      require(found != pending.end());
      pending.erase(found);
      const auto branch = native<RawOperationBranch>(fields);
      require(native<std::uint8_t>(fields) == 0);
      const bool error = native<bool>(fields);
      if (error) {
        const auto code = native<ErrorCode>(fields);
        const auto detail = native<std::int64_t>(fields);
        if (site == 4701) {
          require(code == (cut == 2 || cut == 4 ? ErrorCode::interrupted
                           : cut == 5           ? ErrorCode::corrupt
                           : cut == 6           ? ErrorCode::conflict
                           : cut == 8           ? ErrorCode::io
                           : cut == 9           ? ErrorCode::incomplete
                                                : ErrorCode::capacity));
          if (cut == 2 || cut == 5 || cut == 7)
            require(detail == 0);
          if (cut == 4)
            require(detail == ECANCELED);
          if (cut == 6)
            require(detail == EEXIST);
        }
      }
      if (site == 4711 && branch == RawOperationBranch::returned) {
        require(!error && native<ErrorCode>(fields) == fixture.cause.code &&
                native<std::int64_t>(fields) == fixture.cause.detail);
      }
      if (site == 4741 && branch == RawOperationBranch::returned) {
        auto request_body = ByteView{find_record(request).fields};
        (void)reference(request_body);
        const auto method = native<std::uint8_t>(request_body);
        const auto operand = reference(request_body);
        require(find_record(operand).site == 4702 &&
                original(find_record(operand), 24).size() > 0);
        if (method == 3 || method == 6) {
          const auto other = reference(request_body);
          require(find_record(other).site == 4702);
          (void)original(find_record(other), 24);
        }
        require(request_body.empty());
        if (method <= 2) {
          require(native<int>(fields) == 0);
          require(native<const std::error_category *>(fields) != nullptr);
        } else if (method >= 4)
          (void)native<bool>(fields);
      }
      require(fields.empty());
      if (site == 4701)
        require(branch == (cut == 3   ? RawOperationBranch::allocation
                           : cut >= 2 ? RawOperationBranch::error
                                      : RawOperationBranch::returned));
    }
    if (site == 4720) {
      ++pivots;
      (void)reference(body);
      require(native<unsigned>(body) == pivots && body.empty());
    }
    if (site == 4725) {
      ++actions;
      (void)reference(body);
      const auto action = reference(body);
      const auto choice = reference(body);
      require(action.ordinal < record.ref.ordinal &&
              choice.ordinal < record.ref.ordinal && body.empty());
      const auto &action_source = find_record(action);
      const auto &choice_source = find_record(choice);
      require(action_source.site == 4702 && choice_source.site == 4724);
      require(original(action_source, 7) ==
              string_field(unwrap(decode_packet_string(original(choice_source, 6))),
                           "action"));
    }
    if (site == 4703) {
      const auto parent = reference(body);
      const auto kind = native<std::uint8_t>(body);
      const bool actual = native<bool>(body);
      require(body.empty());
      if (kind >= 21 && kind <= 29) {
        require(find_record(parent).site == 4710);
        require(kind == 21 + claim_checks && !actual);
        ++claim_checks;
      }
    }
    if (site == 4705) {
      (void)reference(body);
      require(native<ErrorCode>(body) == fixture.cause.code &&
              native<std::int64_t>(body) == fixture.cause.detail && body.empty());
    }
    if (site == 4704) {
      require(same(reference(body), whole));
      const auto attempt = native<IdentityBytes>(body);
      require(native<std::uint8_t>(body) == 1);
      const auto ordinal = native<std::size_t>(body);
      const auto extent = native<std::size_t>(body);
      const auto found =
          std::find_if(admissions.begin(), admissions.end(),
                       [&](const auto &item) { return item.id.bytes() == attempt; });
      require(found != admissions.end() && found->ordinal == ordinal &&
              found->input.size() == extent && body.empty());
      ++selected_admissions;
    }
    if (site == 4712) {
      require(same(reference(body), whole));
      require(native<mode_t>(body) == 0700);
      require(original(find_record(reference(body)), 2) ==
              (fixture.path / "backstop").native());
      require(body.empty());
    }
    if (site == 4713) {
      require(find_record(reference(body)).site == 4712);
      const int rc = native<int>(body);
      if (cut == 6)
        require(rc == -1 && native<int>(body) == EEXIST);
      else
        require(rc == 0);
      require(body.empty());
    }
    if (site == 4714) {
      ++entropy_calls;
      (void)reference(body);
      const auto count = native<std::size_t>(body);
      require((count == 8 || count == 16) && body.empty());
    }
    if (site == 4715) {
      const auto request = reference(body);
      require(find_record(request).site == 4714);
      const int rc = native<int>(body);
      if (cut == 8) {
        require(rc == -1 && native<int>(body) == EIO && body.empty());
      } else {
        require(rc == 0 && recovery_header.has_value());
        if (entropy_returns == 0)
          require(native<std::uint64_t>(body) == recovery_header->issuer_namespace);
        else
          require(native<IdentityBytes>(body) ==
                  (entropy_returns == 1 ? recovery_header->environment.bytes()
                                        : recovery_header->journal.bytes()));
        require(body.empty());
      }
      ++entropy_returns;
    }
    if (site == 4702 || site == 4724) {
      auto fields = ByteView{record.fields};
      (void)reference(fields);
      const auto role = native<std::uint8_t>(fields);
      const auto text = original(record, role);
      if (role == 3) {
        require(text == unwrap(encode_packet_string(expected_request)));
        mission_bytes = true;
      }
      if (role == 6) {
        require(string_field(unwrap(decode_packet_string(text)), "action").size() > 0);
        choice_bytes = true;
      }
      if (role == 12) {
        const auto outcome = unwrap(decode_packet_string(text));
        if (cut == 0)
          require(string_field(outcome, "content") == "reconciled useful work");
      }
    }
    if (site == 4727) {
      (void)reference(body);
      const auto file = reference(body);
      require(file.ordinal < record.ref.ordinal);
      const auto &file_owner = find_record(file);
      require(file_owner.site == 3410);
      auto file_request = ByteView{file_owner.fields};
      auto comparison_body = ByteView{record.fields};
      require(same(reference(file_request), reference(comparison_body)));
      require(native<RawFileMethod>(file_request) == RawFileMethod::read &&
              native<std::size_t>(file_request) == 1024 * 1024 && file_request.empty());
      require(native<bool>(body));
      require(body.empty());
      artifact = true;
    }
    if (site == 4706) {
      (void)reference(body);
      const auto kind = native<std::uint8_t>(body);
      if (kind == 8) {
        require(body.empty());
        restore = true;
      }
    }
  }
  if (!whole.ordinal || !pending.empty()) {
    std::cerr << "cut " << cut << " whole " << whole.ordinal << " pending";
    for (const auto &item : pending)
      std::cerr << " " << item.second;
    std::cerr << "\n";
  }
  require(whole.ordinal && pending.empty());
  if (cut == 2)
    require(pivots == 0 && entropy_calls == 0 && !mission_bytes);
  else if (cut <= 4)
    require(pivots == expected_pivots && entropy_calls == 3 && mission_bytes &&
            restore);
  if (cut <= 4 && cut != 2)
    require(claim_checks == 9 && selected_admissions == admissions.size());
  if (cut == 0)
    require(actions == 1 && choice_bytes && artifact);
  if (cut == 3)
    require(allocation_failed && choice_bytes && actions == 0 &&
            !std::filesystem::exists(fixture.work / "result"));
  if (cut == 4)
    require(stopped && actions == 1 &&
            read_file(fixture.work / "result") == "reconciled useful work");
  if (cut >= 5)
    require(actions == 0 && pivots == 0 && entropy_calls == (cut == 8 ? 1 : 0));
  if (cut == 8)
    require(entropy_returns == 1);
}
void weak_native() {
  Fixture fixture;
  const auto base = fixture.work / "raw-owner";
  std::filesystem::create_directories(base / "audit");
  std::filesystem::permissions(base, std::filesystem::perms::owner_all);
  std::filesystem::permissions(base / "audit", std::filesystem::perms::owner_all);
  std::filesystem::create_symlink(fixture.path, fixture.work / "linked");
  std::vector<std::pair<RawEventRef, std::string>> acquired;
  {
    auto runtime = unwrap(
        RawRuntime::open(base.string(), id<EnvironmentId>(92),
                         {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    auto issuer = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{issuer};
      RawRegion owner{raw_regions::command_perform};
      const RawAdapterContext context{
          nullptr, RawEventRef{owner.lane_epoch(), owner.begin_ordinal()}, nullptr,
          nullptr};
      for (const auto &path :
           std::array{std::filesystem::path{}, fixture.path,
                      fixture.work / "linked" / "missing" / ".." / "new",
                      std::filesystem::path{"uncreated/../relative"},
                      fixture.path / "missing" / "suffix"}) {
        std::error_code expected_error, actual_error;
        const auto expected = std::filesystem::weakly_canonical(path, expected_error);
        RawEventRef output{};
        const auto actual =
            raw_file_weak_canonical(path, actual_error, &context, &output);
        require(actual == expected && actual_error == expected_error);
        if (!actual_error) {
          require(output.ordinal != 0);
          acquired.push_back({output, actual.native()});
        }
      }
    }
    issuer.reset();
  }
  for (const auto &[ref, expected] : acquired) {
    bool found = false;
    for (const auto &record : records(base))
      if (same(record.ref, ref)) {
        require(record.site == 3407 && original(record, 7) == expected);
        found = true;
      }
    require(found);
  }
}
int main() {
  try {
    native_case(0);
    native_case(1);
    native_case(2);
    native_case(3);
    native_case(4);
    native_case(5);
    native_case(6);
    native_case(7);
    native_case(8);
    native_case(9);
    weak_native();
    std::cout
        << "Backstop native useful/no-progress/pause and weak-path originals passed\n";
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << " " << error.detail << "\n";
    return 1;
  }
}
