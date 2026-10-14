#include "blackbird/raw_file.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_session.hpp"
#include "blackbird/session.hpp"
#include "blackbird/tools.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <unistd.h>
using namespace blackbird;
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
std::string disk(const std::filesystem::path &path) {
  std::ifstream stream{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, {}};
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
void owners() {
  Directory directory;
  const auto named = directory.path / "session'''";
  std::filesystem::create_directory(named);
  std::filesystem::permissions(named, std::filesystem::perms::owner_all);
  const SessionSettings settings{"literal-model", "high", "/literal/workflow.lua",
                                 "literal session"};
  SessionIdentity identity{id<ParticipantId>(1), id<ConversationId>(2),
                           id<WorkflowId>(3)};
  std::string serialized;
  std::vector<std::string> locators;
  struct stat expected_info{}, expected_audit{};
  {
    auto root = state(named);
    AuditLog log{*root};
    SessionStore store{log};
    auto runtime = unwrap(
        RawRuntime::open(directory.path.string(), header().environment,
                         {{64 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto context = source();
    store.save(settings, &context);
    const auto count = root->fact_count();
    store.save(settings, &context);
    check(root->fact_count() == count, "duplicate setting appended");
    bool rejected = false;
    try {
      store.save({"invalid", "wrong", "/literal/workflow.lua", "rejected"}, &context);
    } catch (const Error &error) {
      rejected = error == Error{ErrorCode::invalid_range};
    }
    check(rejected && store.settings() == settings && root->fact_count() == count,
          "invalid setting changed state");
    identity = session_identity(log, &context);
    check(session_identity(log, &context).conversation == identity.conversation,
          "native identity changed");
    save_session_info(named, settings, identity, &context);
    check(::lstat((named / "session-info.bbm").c_str(), &expected_info) == 0 &&
              ::stat((named / "audit").c_str(), &expected_audit) == 0,
          "independent native metadata observations");
    serialized = disk(named / "session-info.bbm");
    const auto timestamp = std::filesystem::last_write_time(named / "session-info.bbm");
    save_session_info(named, settings, identity, &context);
    check(std::filesystem::last_write_time(named / "session-info.bbm") == timestamp,
          "unchanged discovery snapshot rewritten");
    RawEventRef listing_source{};
    const auto listing = list_sessions(directory.path, &context, &listing_source);
    check(listing_source.ordinal != 0 && listing_source.lane == raw_lane_epoch(),
          "actual listing completion source absent");
    check(field(listing, "sessions").array().size() == 1 &&
              string_field(field(listing, "sessions").array()[0], "metadata_status") ==
                  "snapshot",
          "selected discovery changed business rows");
    const auto stable = std::filesystem::canonical(named).string();
    const auto quoted = "'" + std::filesystem::canonical(directory.path).string() +
                        "/session'\"'\"''\"'\"''\"'\"''";
    check(string_field(field(listing, "sessions").array()[0], "resume_command") ==
              "./scripts/blackbird --session " + quoted,
          "literal apostrophe quote changed");
    const auto missing = list_sessions(named / "does-not-exist", &context);
    check(string_field(missing, "root") == stable + "/does-not-exist" &&
              field(missing, "sessions").array().empty(),
          "weak canonical missing suffix changed");
    std::filesystem::create_symlink(named, directory.path / "alias");
    const auto alias_missing =
        list_sessions(directory.path / "alias" / "missing", &context);
    check(string_field(alias_missing, "root") == stable + "/missing",
          "weak canonical existing symlink prefix changed");
    write_file(named / "session-info.bbm", "broken");
    const auto damaged = list_sessions(directory.path, &context);
    check(string_field(field(damaged, "sessions").array()[0], "metadata_status") ==
              "damaged",
          "ordinary malformed metadata not damaged");
    save_session_info(named, settings, identity, &context);
    check(disk(named / "session-info.bbm") == serialized,
          "ordinary damaged metadata replacement changed");
    SessionStore restored{log, &context};
    check(restored.settings() == settings, "actual retained settings restore changed");
    constexpr char digits[] = "0123456789abcdef";
    for (const auto &fact : root->committed_facts())
      if (const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body)) {
        std::string literal;
        for (const auto byte : record->identity.bytes()) {
          const auto value = std::to_integer<unsigned char>(byte);
          literal += digits[value >> 4U];
          literal += digits[value & 15U];
        }
        locators.push_back(std::move(literal));
      }
  }
  const auto rows = records(directory.path);
  std::map<Ref, const Row *> index;
  for (const auto &row : rows)
    check(index.emplace(row.own, &row).second, "duplicate session raw identity");
  std::map<Ref, unsigned> ends;
  std::map<Ref, std::string> requested_paths;
  std::map<Ref, std::uint8_t> actions;
  std::map<Ref, bool> invalid_saves;
  for (const auto &row : rows) {
    ByteView bytes{row.bytes};
    if (row.site == 4210) {
      (void)ref(bytes);
      actions[row.own] = native<std::uint8_t>(bytes);
      (void)native<IdentityBytes>(bytes);
      (void)native<std::uint64_t>(bytes);
      (void)native<std::uint64_t>(bytes);
      check(bytes.empty(), "actual native Session cursor fields");
    }
    if (row.site == 4212 || row.site == 4222) {
      const auto owner = ref(bytes);
      const auto kind = native<std::uint8_t>(bytes);
      const auto length = native<std::size_t>(bytes);
      check(length == bytes.size(), "independent owner leaf length");
      const std::string text{reinterpret_cast<const char *>(bytes.data()),
                             bytes.size()};
      if (index.at(owner)->site == 4220 && kind == 5 &&
          !requested_paths.contains(owner))
        requested_paths[owner] = text;
      if (kind == 3 && text == "wrong")
        invalid_saves[owner] = true;
    }
  }
  bool model = false, invalid = false, body = false, quote = false,
       identity_seen = false, mode = false, time = false;
  unsigned complete = 0;
  unsigned saved = 0, restored_base = 0, restored_name = 0, equal = 0, different = 0,
           linked_rows = 0, absolute_links = 0, observed_locators = 0,
           damaged_failures = 0;
  const auto quoted = "'" + std::filesystem::canonical(directory.path).string() +
                      "/session'\"'\"''\"'\"''\"'\"''";
  for (const auto &row : rows) {
    ByteView bytes{row.bytes};
    if (row.site == 4212 || row.site == 4222) {
      const auto parent = ref(bytes);
      check(index.contains(parent), "session leaf owner absent");
      const auto kind = native<std::uint8_t>(bytes);
      const auto size = native<std::size_t>(bytes);
      check(size == bytes.size(), "session native leaf extent");
      const std::string_view text{reinterpret_cast<const char *>(bytes.data()), size};
      model = model || (kind == 2 && text == "literal-model");
      invalid = invalid || (kind == 3 && text == "wrong");
      body = body || (kind == 6 && text == serialized);
      quote = quote || (kind == 9 && text == quoted);
    }
    if (row.site == 4211 || row.site == 4219 || row.site == 4221 || row.site == 4243) {
      const auto parent = ref(bytes);
      check(index.contains(parent), "actual session terminal request absent");
      const auto branch = native<std::uint8_t>(bytes);
      const auto stage = native<std::uint8_t>(bytes);
      const bool error_present = native<bool>(bytes);
      std::optional<Error> error;
      if (error_present)
        error = Error{native<ErrorCode>(bytes), native<std::int64_t>(bytes)};
      if (invalid_saves[parent])
        check(branch == 3 && stage == 1 && error == Error{ErrorCode::invalid_range},
              "invalid save exact native Error/stage");
      else if (row.site == 4219 && branch == 3) {
        check(stage == 2 && error == Error{ErrorCode::corrupt},
              "damaged metadata exact actual parser failure");
        ++damaged_failures;
      } else {
        check(branch == 1 && !error_present,
              "actual returned session outcome/absent Error");
        if (row.site == 4221)
          check(stage == 4, "listing reached completion stage");
        if (row.site == 4219)
          check(stage == 3, "successful metadata reached validation stage");
        if (row.site == 4211 && actions[parent] == 1)
          check(stage == 1, "restore exact reached stage");
        if (row.site == 4211 && (actions[parent] == 2 || actions[parent] == 3)) {
          const bool published =
              std::any_of(rows.begin(), rows.end(), [&](const Row &candidate) {
                if (candidate.site != 4213)
                  return false;
                ByteView locator{candidate.bytes};
                return ref(locator) == parent;
              });
          check(stage == (published ? 3 : 1), "save/identity exact reached stage");
        }
      }
      if (row.site == 4243) {
        ByteView request{index.at(parent)->bytes};
        (void)ref(request);
        const auto method = native<std::uint8_t>(request);
        const auto code = native<int>(bytes);
        check(stage == 0 && request.empty(), "native filesystem stage/request width");
        if (method == 8) {
          const auto acquired = ref(bytes);
          check(code == 0 && index.contains(acquired) &&
                    index.at(acquired)->site == 3407,
                "actual weak canonical successful error_code/body ref");
        } else {
          const bool returned = native<bool>(bytes);
          check(method >= 1 && method <= 5, "native filesystem method");
          if (method == 1 || method == 3 || method == 4 || method == 5)
            check(code == 0, "native existing filesystem operation error_code");
          if (method == 5)
            check(returned, "actual directory-entry predicate");
          if (method == 2) {
            ByteView request_body{index.at(parent)->bytes};
            const auto owner = ref(request_body);
            std::error_code expected;
            const bool exists =
                std::filesystem::is_directory(requested_paths.at(owner), expected);
            check(code == expected.value() && returned == exists,
                  "actual requested directory native result/error_code");
          }
        }
      }
      check(bytes.empty(), "native terminal trailing/omitted field variant");
      ++ends[parent];
    }
    if (row.site == 4213) {
      const auto parent = ref(bytes);
      const auto length = native<std::size_t>(bytes);
      const std::string text{reinterpret_cast<const char *>(bytes.data()),
                             bytes.size()};
      check(length == 32 && bytes.size() == 32 && index.contains(parent) &&
                std::find(locators.begin(), locators.end(), text) != locators.end(),
            "actual locator equals independently read retained record identity");
      ++observed_locators;
    }
    if (row.site == 4215) {
      const auto parent = ref(bytes);
      const auto action = native<std::uint8_t>(bytes);
      check(bytes.empty() && actions.contains(parent),
            "native settings assignment owner");
      if (action == 2) {
        check(actions[parent] == 2, "save assignment");
        ++saved;
      } else if (action == 1) {
        check(actions[parent] == 1, "restore base");
        ++restored_base;
      } else {
        check(action == 3 && actions[parent] == 1, "restore name");
        ++restored_name;
      }
    }
    if (row.site == 4216) {
      const auto parent = ref(bytes);
      check(actions[parent] == 2 && native<std::uint8_t>(bytes) == 1,
            "actual duplicate predicate owner/tag");
      if (native<bool>(bytes))
        ++equal;
      else
        ++different;
      check(bytes.empty(), "actual equality width");
    }
    if (row.site == 4214) {
      const auto parent = ref(bytes);
      check(index.contains(parent) &&
                native<IdentityBytes>(bytes) == identity.actor.bytes() &&
                native<IdentityBytes>(bytes) == identity.conversation.bytes() &&
                native<IdentityBytes>(bytes) == identity.workflow.bytes() &&
                bytes.empty(),
            "actual native identity triple changed");
      identity_seen = true;
    }
    if (row.site == 4241) {
      const auto parent = ref(bytes);
      check(index.contains(parent) && index.at(parent)->site == 4240,
            "stat result causal owner");
      ByteView request{index.at(parent)->bytes};
      (void)ref(request);
      const auto method = native<RawSessionStat>(request);
      const auto result = native<int>(bytes);
      if (result < 0) {
        check(native<int>(bytes) == ENOENT, "actual failed stat errno");
        check(bytes.empty(), "failed stat fabricated fields");
      } else if (method == RawSessionStat::lstat) {
        const auto returned = native<mode_t>(bytes);
        check(returned == expected_info.st_mode && bytes.empty(), "actual lstat mode");
        mode = true;
      } else {
        check(native<time_t>(bytes) == expected_audit.st_mtime, "actual stat mtime");
        check(bytes.empty(), "stat native mtime only");
        time = true;
      }
    }
    if (row.site == 4225) {
      const auto parent = ref(bytes);
      check(index.contains(parent) && index.at(parent)->site == 4220,
            "listing completed unrelated request");
      const auto count = native<std::size_t>(bytes);
      check(count == (requested_paths.at(parent) == directory.path.native() ? 1U : 0U),
            "listing exact actual returned row count");
      check(bytes.empty(), "listing completion width");
      ++complete;
    }
    if (row.site == 4224) {
      const auto parent = ref(bytes);
      check(native<std::size_t>(bytes) == 0, "actual row index");
      const auto metadata = ref(bytes);
      check(index.at(parent)->site == 4220 && index.at(metadata)->site == 4218 &&
                bytes.empty(),
            "actual row metadata owner");
      ByteView request{index.at(metadata)->bytes};
      check(ref(request) == parent && request.empty(),
            "row actual metadata causal link");
      ++linked_rows;
    }
    if (row.site == 4226) {
      const auto parent = ref(bytes), original = ref(bytes);
      check(bytes.empty() && index.at(parent)->site == 4220 &&
                index.at(original)->site == 3407,
            "absolute actual body reference");
      ByteView acquired{index.at(original)->bytes};
      const auto helper = ref(acquired);
      check(native<std::uint8_t>(acquired) == 7, "absolute joined path role");
      const auto size = native<std::size_t>(acquired);
      check(size == acquired.size() &&
                std::string_view{reinterpret_cast<const char *>(acquired.data()),
                                 size} == requested_paths.at(parent),
            "actual absolute original path bytes");
      ByteView request{index.at(helper)->bytes};
      check(index.at(helper)->site == 3410 && ref(request) == parent &&
                native<RawFileMethod>(request) == RawFileMethod::absolute,
            "absolute actual selected native helper");
      ++absolute_links;
    }
  }
  for (const auto &row : rows)
    if (row.site == 4210 || row.site == 4217 || row.site == 4218 || row.site == 4220 ||
        row.site == 4242)
      check(ends[row.own] == 1, "missing/duplicate actual session outcome");
  check(model && invalid && body && quote && identity_seen && mode && time &&
            complete == 4,
        "native session original/result surfaces absent");
  check(saved == 1 && restored_base == 1 && restored_name == 1 && equal == 1 &&
            different == 1 && observed_locators == 2 && linked_rows == 2 &&
            absolute_links == 4 && damaged_failures == 1,
        "actual Session mutation/predicate/locator/source/error graph");
}
using NativeOpen = int (*)(const char *, int, ...);
using NativeClose = int (*)(int);
using NativeWrite = ssize_t (*)(int, const void *, std::size_t);
using NativeSend = ssize_t (*)(int, const void *, std::size_t, int);
NativeOpen original_open = nullptr;
NativeClose original_close = nullptr;
NativeWrite original_write = nullptr;
NativeSend original_send = nullptr;
RawChannel *notification_channel = nullptr;
std::uint32_t notification_cut = 0;
bool terminal_cut_hit = false;
std::string selected_path;
int selected_fd = -1;
unsigned writes = 0;
bool cut_read = false, cut_fields = false;
RawProducer *selected_producer = nullptr;
std::optional<RawCapacityClaim> fill;
void fill_data() {
  check(selected_producer, "real native producer selected");
  fill.emplace(unwrap(selected_producer->hold_capacity(
      RawPartition::data, unwrap(selected_producer->available(RawPartition::data)))));
}
void refusal() {
  for (const bool metadata_read : {true, false}) {
    Directory directory;
    auto root = state(directory.path);
    AuditLog log{*root};
    SessionStore store{log};
    const SessionSettings settings{"literal-model", "high", "/literal/workflow.lua",
                                   "literal"};
    store.save(settings);
    const auto identity = session_identity(log);
    save_session_info(directory.path, settings, identity);
    const auto before = disk(directory.path / "session-info.bbm");
    const auto stamp =
        std::filesystem::last_write_time(directory.path / "session-info.bbm");
    const auto name = "/bb-scut-" + std::to_string(::getpid());
    const auto lane = id<EnvironmentId>(91).bytes();
    auto channel =
        unwrap(RawChannel::create(name, lane, {2 * 1024 * 1024, 1024 * 1024, 4096}));
    RawProducer producer{channel};
    bool rejected = false;
    RawEventRef completion{};
    {
      RawThreadBinding binding{producer, lane};
      const auto context = source();
      selected_path =
          ((metadata_read ? directory.path
                          : std::filesystem::weakly_canonical(directory.path)) /
           "session-info.bbm")
              .string();
      selected_producer = &producer;
      cut_read = metadata_read;
      cut_fields = !metadata_read;
      writes = 0;
      try {
        if (metadata_read)
          save_session_info(directory.path, settings, identity, &context);
        else
          (void)list_sessions(directory.path, &context, &completion);
      } catch (const Error &error) {
        rejected = error == Error{ErrorCode::capacity};
      }
      cut_read = cut_fields = false;
      selected_path.clear();
      selected_producer = nullptr;
      fill.reset();
    }
    if (!rejected || writes || completion.ordinal ||
        disk(directory.path / "session-info.bbm") != before ||
        std::filesystem::last_write_time(directory.path / "session-info.bbm") != stamp)
      std::cerr << "refusal diagnostic read=" << metadata_read
                << " rejected=" << rejected << " writes=" << writes
                << " completion=" << completion.ordinal << '\n';
    check(rejected && writes == 0 && completion.ordinal == 0 &&
              disk(directory.path / "session-info.bbm") == before &&
              std::filesystem::last_write_time(directory.path / "session-info.bbm") ==
                  stamp,
          "real capture refusal became business fallback/metadata write/completion");
    unsigned complete = 0, failed = 0, write_requests = 0;
    for (auto partition : {RawPartition::data, RawPartition::control}) {
      const auto output = unwrap(channel.read(partition, 0, 2 * 1024 * 1024));
      ByteView bytes{output.bytes};
      while (!bytes.empty()) {
        const auto size = native<std::uint32_t>(bytes),
                   site = native<std::uint32_t>(bytes);
        (void)native<std::uint64_t>(bytes);
        check(size >= 16 && size - 16 <= bytes.size(), "native refusal frame");
        ByteView body = bytes.first(size - 16);
        bytes = bytes.subspan(size - 16);
        if (site == 3410) {
          (void)ref(body);
          if (native<RawFileMethod>(body) == RawFileMethod::write)
            ++write_requests;
        }
        if (site == 4225)
          ++complete;
        if (site == 4211 || site == 4219 || site == 4221) {
          (void)ref(body);
          if (native<std::uint8_t>(body) == 3)
            ++failed;
        }
      }
    }
    check(complete == 0 && failed > 0 && write_requests == 0,
          "actual selected capture refusal outcome absent");
    unwrap(RawChannel::unlink(name));
  }
}
void settings_refusal() {
  Directory directory;
  auto root = state(directory.path);
  AuditLog log{*root};
  SessionStore store{log};
  const auto prior = store.settings();
  const auto count = root->fact_count();
  const SessionSettings settings{"literal-model", "high", "/literal/workflow.lua",
                                 "literal"};
  const auto name = "/bb-spay-" + std::to_string(::getpid());
  const auto lane = id<EnvironmentId>(92).bytes();
  auto channel = unwrap(RawChannel::create(
      name, lane, {49 + settings.name.size() - 1, 1024 * 1024, 4096}));
  RawProducer producer{channel};
  bool rejected = false;
  {
    RawThreadBinding binding{producer, lane};
    const auto parent = unwrap(raw_emit(9200, {}, RawPartition::control));
    const RawAdapterContext context{nullptr, RawEventRef{lane, parent}, nullptr,
                                    nullptr};
    try {
      store.save(settings, &context);
    } catch (const Error &error) {
      rejected = error == Error{ErrorCode::capacity};
    }
  }
  check(rejected && store.settings() == prior && root->fact_count() == count &&
            !std::filesystem::exists(directory.path / "session-info.bbm"),
        "one-byte-short settings admission performed semantic work");
  const auto output = unwrap(channel.read(RawPartition::control, 0, 1024 * 1024));
  ByteView bytes{output.bytes};
  unsigned failed = 0;
  while (!bytes.empty()) {
    const auto length = native<std::uint32_t>(bytes),
               site = native<std::uint32_t>(bytes);
    (void)native<std::uint64_t>(bytes);
    check(length >= 16 && length - 16 <= bytes.size(), "settings refusal native frame");
    ByteView body = bytes.first(length - 16);
    bytes = bytes.subspan(length - 16);
    check(site != 4213 && site != 4215, "refused settings emitted locator/assignment");
    if (site == 4211) {
      (void)ref(body);
      check(native<std::uint8_t>(body) == 3 && native<std::uint8_t>(body) == 0 &&
                native<bool>(body) && native<ErrorCode>(body) == ErrorCode::capacity &&
                native<std::int64_t>(body) == 0 && body.empty(),
            "settings admission exact owning capacity Error");
      ++failed;
    }
  }
  check(failed == 1, "settings refusal missing owning terminal");
  unwrap(RawChannel::unlink(name));
}
void terminal_refusal() {
  for (const bool listing : {true, false}) {
    Directory directory;
    auto root = state(directory.path);
    AuditLog log{*root};
    SessionStore store{log};
    const SessionSettings settings{"literal-model", "high", "/literal/workflow.lua",
                                   "literal"};
    const auto count = root->fact_count();
    const auto name = "/bb-send-" + std::to_string(::getpid());
    const auto lane = id<EnvironmentId>(93).bytes();
    auto channel =
        unwrap(RawChannel::create(name, lane, {2 * 1024 * 1024, 1024 * 1024, 4096}));
    const auto sockets = unwrap(raw_socket_pair());
    RawProducer producer{channel, sockets[0]};
    RawEventRef completion{};
    bool rejected = false;
    {
      RawThreadBinding binding{producer, lane};
      const auto context = source();
      notification_channel = &channel;
      notification_cut = listing ? 4221 : 4211;
      terminal_cut_hit = false;
      unwrap(channel.arm_data());
      try {
        if (listing)
          (void)list_sessions(directory.path, &context, &completion);
        else
          store.save(settings, &context);
      } catch (const Error &error) {
        rejected = error == Error{ErrorCode::io, EBADF};
      }
      notification_channel = nullptr;
      notification_cut = 0;
    }
    check(terminal_cut_hit && rejected && completion.ordinal == 0,
          "actual final terminal notification failure was accepted");
    if (!listing)
      check(store.settings() == settings && root->current_programs().size() == 1 &&
                root->fact_count() > count,
            "final notification failure erased earlier semantic append");
    ::close(sockets[1]);
    unwrap(RawChannel::unlink(name));
  }
}
} // namespace
extern "C" ssize_t send(int fd, const void *bytes, std::size_t size, int flags) {
  if (notification_channel) {
    const auto output =
        unwrap(notification_channel->read(RawPartition::control, 0, 1024 * 1024));
    ByteView records{output.bytes};
    std::uint32_t last = 0;
    while (!records.empty()) {
      const auto length = native<std::uint32_t>(records);
      last = native<std::uint32_t>(records);
      (void)native<std::uint64_t>(records);
      records = records.subspan(length - 16);
    }
    if (!terminal_cut_hit && last == notification_cut) {
      terminal_cut_hit = true;
      original_close(fd);
    } else if (!terminal_cut_hit)
      unwrap(notification_channel->arm_data());
  }
  return original_send(fd, bytes, size, flags);
}
extern "C" int open(const char *path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = static_cast<mode_t>(va_arg(args, int));
    va_end(args);
  }
  const auto returned =
      flags & O_CREAT ? original_open(path, flags, mode) : original_open(path, flags);
  const int error = errno;
  if (!selected_path.empty() && selected_path == path && returned >= 0) {
    selected_fd = returned;
    if (cut_read) {
      cut_read = false;
      fill_data();
    }
  }
  if (!selected_path.empty() && (flags & O_WRONLY))
    ++writes;
  errno = error;
  return returned;
}
extern "C" ssize_t write(int fd, const void *bytes, std::size_t size) {
  if (selected_producer)
    ++writes;
  return original_write(fd, bytes, size);
}
extern "C" int close(int fd) {
  const auto returned = original_close(fd);
  const int error = errno;
  if (fd == selected_fd) {
    selected_fd = -1;
    if (cut_fields) {
      cut_fields = false;
      fill_data();
    }
  }
  errno = error;
  return returned;
}
int main() {
  original_open = reinterpret_cast<NativeOpen>(::dlsym(RTLD_NEXT, "open"));
  original_close = reinterpret_cast<NativeClose>(::dlsym(RTLD_NEXT, "close"));
  original_write = reinterpret_cast<NativeWrite>(::dlsym(RTLD_NEXT, "write"));
  original_send = reinterpret_cast<NativeSend>(::dlsym(RTLD_NEXT, "send"));
  try {
    check(original_open && original_close && original_write && original_send,
          "actual forwarding native functions");
    owners();
    refusal();
    settings_refusal();
    terminal_refusal();
    std::cout << "session native owner originals/outcomes passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
