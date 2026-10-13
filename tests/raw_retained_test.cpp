#include "blackbird/coding.hpp"
#include "blackbird/context.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/raw_operation.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_retained.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/retained_environment.hpp"
#include "blackbird/saved_state.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace blackbird;
namespace {
// The callback reenters the same normal call used by Lua under the already
// admitted turn. Explicit instantiation exposes that existing member only to
// this fixture; a second operator_call would start another top-level turn.
struct SelectedCall {
  using Function = Value (CodingEngine::*)(std::string, Value);
  friend Function selected_call(SelectedCall);
};
template <class Tag, typename Tag::Function member> struct SelectedMember {
  friend typename Tag::Function selected_call(Tag) { return member; }
};
template struct SelectedMember<SelectedCall, &CodingEngine::call>;
struct Allocation {
  void *caller;
  std::size_t size;
  bool saved_stack;
};
thread_local bool after_ack = false, measuring = false, cutting = false,
                  stack_busy = false;
thread_local bool before_ack = false;
thread_local std::array<Allocation, 32> allocations{};
thread_local std::size_t allocation_count = 0;
thread_local void *cut_caller = nullptr;
thread_local bool cut_hit = false;
struct ProjectionCancellation {
  int writer = -1;
  bool armed = false, hit = false, failed = false, busy = false;
  std::vector<RawControlToken> pressure;
};
thread_local ProjectionCancellation projection;
void cancel_projection(std::size_t size) noexcept {
  if (!projection.armed || projection.busy || size < 32768 || size > 65536)
    return;
  projection.busy = true;
  std::array<void *, 48> frames{};
  const int count = ::backtrace(frames.data(), static_cast<int>(frames.size()));
  bool actual_eval = false, actual_string = false, actual_read = false,
       string_storage = false;
  bool allocation_frame = false, first_caller = false, direct_eval = false;
  for (int index = 0; index < count; ++index) {
    Dl_info info{};
    if (::dladdr(frames[static_cast<std::size_t>(index)], &info) && info.dli_sname) {
      const bool eval =
          std::strstr(info.dli_sname, "Runtime") && std::strstr(info.dli_sname, "eval");
      if (std::strcmp(info.dli_sname, "_Znwm") == 0)
        allocation_frame = true;
      else if (allocation_frame && !first_caller) {
        first_caller = true;
        direct_eval = eval;
      }
      if (eval)
        actual_eval = true;
      if (std::strstr(info.dli_sname, "lua_string"))
        actual_string = true;
      if (std::strstr(info.dli_sname, "read_value"))
        actual_read = true;
      if (std::strstr(info.dli_sname, "basic_string") &&
          (std::strstr(info.dli_sname, "_M_create") ||
           std::strstr(info.dli_sname, "__init")))
        string_storage = true;
    }
  }
  // Optimized libstdc++ inlines the string helpers: the measured 32769-byte
  // allocation's first exported caller is Runtime::eval itself. Its source
  // copies this Lua result through read_value; later codec/journal allocations
  // have their own intervening callers and cannot select this branch.
  if (actual_eval && ((actual_string && actual_read) || string_storage ||
                      (direct_eval && size == 32769))) {
    projection.armed = false;
    projection.hit = true;
    for (std::uint64_t bytes = 1024 * 1024; bytes; bytes /= 2) {
      auto grant = raw_try_claim_control(bytes);
      if (grant.has_value())
        projection.pressure.push_back(std::move(grant).value());
    }
    projection.failed = ::shutdown(projection.writer, SHUT_WR) != 0;
  }
  projection.busy = false;
}
bool saved_stack() {
  if (stack_busy)
    return false;
  stack_busy = true;
  std::array<void *, 48> frames{};
  const int n = ::backtrace(frames.data(), static_cast<int>(frames.size()));
  bool found = false;
  for (int i = 0; i < n; ++i) {
    Dl_info info{};
    if (::dladdr(frames[static_cast<std::size_t>(i)], &info) && info.dli_sname &&
        std::strstr(info.dli_sname, "make_unique") &&
        std::strstr(info.dli_sname, "SavedState"))
      found = true;
  }
  stack_busy = false;
  return found;
}
void require(bool condition, const char *claim) {
  if (!condition)
    throw std::runtime_error(claim);
}
template <class T> T id(unsigned char n) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{n};
  return unwrap(T::from_bytes(bytes));
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char pattern[] = "/tmp/bb-retained-baseline-XXXXXX";
    const auto *created = ::mkdtemp(pattern);
    require(created, "native private directory");
    path = created;
  }
  ~Directory() {
    if (std::uncaught_exceptions()) {
      std::cerr << "preserved native fixture: " << path << '\n';
      return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
struct Gate {
  bool selected_replace = false, enabled = false;
  unsigned acks = 0;
};
class ForwardDirectory final : public JournalDirectory {
public:
  ForwardDirectory(NativeJournalDirectory native, Gate &gate)
      : native_(std::move(native)), gate_(gate) {}
  Result<std::unique_ptr<JournalFile>>
  create_exclusive(std::string_view name) override {
    return native_.create_exclusive(name);
  }
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view name,
                                                     FileAccess access) override {
    return native_.open_existing(name, access);
  }
  Result<void> replace_file(std::string_view from, std::string_view to) override {
    auto result = native_.replace_file(from, to);
    if (result.has_value() && (to == "audit.state.0" || to == "audit.state.1"))
      gate_.selected_replace = true;
    return result;
  }
  Result<void> synchronize_directory(SyncStrength strength) override {
    auto result = native_.synchronize_directory(strength);
    if (result.has_value() && gate_.selected_replace) {
      gate_.selected_replace = false;
      ++gate_.acks;
      if (gate_.enabled)
        after_ack = true;
    }
    return result;
  }

private:
  NativeJournalDirectory native_;
  Gate &gate_;
};
std::uint64_t little(ByteView bytes) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < bytes.size(); ++i)
    value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[i]))
             << (8 * i);
  return value;
}
bool equal(JournalCursor a, JournalCursor b) {
  return a.journal == b.journal && a.sequence == b.sequence &&
         a.end_offset == b.end_offset;
}
JournalCursor selected_slot(const std::filesystem::path &path, unsigned slot,
                            AuditStreamId journal) {
  std::ifstream input{path / ("audit.state." + std::to_string(slot)), std::ios::binary};
  require(input.good(), "native accepted saved slot exists");
  const std::vector<char> chars{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
  const auto bytes = std::as_bytes(std::span{chars});
  require(bytes.size() >= 196, "native accepted saved header is complete");
  require(little(bytes.last(4)) == crc32c(bytes.first(bytes.size() - 4)),
          "independent saved slot CRC");
  return {journal, little(bytes.subspan(128, 8)), little(bytes.subspan(136, 8))};
}
template <class T> T take(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "complete native metric field");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(value));
  bytes = bytes.subspan(sizeof(value));
  return value;
}
template <class T> T take_identity(ByteView &bytes) {
  IdentityBytes identity{};
  require(bytes.size() >= identity.size(), "complete native metric identity");
  std::memcpy(identity.data(), bytes.data(), identity.size());
  bytes = bytes.subspan(identity.size());
  return unwrap(T::from_bytes(identity));
}
RawEventRef take_ref(ByteView &bytes) {
  RawEventRef reference{};
  require(bytes.size() >= reference.lane.size(), "complete actual raw lane");
  std::memcpy(reference.lane.data(), bytes.data(), reference.lane.size());
  bytes = bytes.subspan(reference.lane.size());
  reference.ordinal = take<std::uint64_t>(bytes);
  return reference;
}
JournalCursor take_cursor(ByteView &bytes) {
  return {take_identity<AuditStreamId>(bytes), take<std::uint64_t>(bytes),
          take<std::uint64_t>(bytes)};
}
struct Record {
  IdentityBytes lane;
  std::uint64_t ordinal;
  std::uint32_t site;
  std::vector<std::byte> body;
};
std::vector<Record> read_records(const Directory &directory,
                                 std::size_t buffer_size = 1024 * 1024) {
  const auto runs = unwrap(discover_raw_runs(directory.path.string()));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "one closed native raw run");
  auto reader =
      unwrap(RawRunReader::open((directory.path / "raw" / runs.front().name).string()));
  std::vector<Record> records;
  std::vector<std::byte> buffer(buffer_size);
  while (auto read = unwrap(reader->read(buffer))) {
    require(read->origin.partition != RawPartition::failure,
            "no ingest failure on native successful ownership");
    auto bytes = read->raw;
    while (!bytes.empty()) {
      const auto length = take<std::uint32_t>(bytes), site = take<std::uint32_t>(bytes);
      const auto ordinal = take<std::uint64_t>(bytes);
      require(length >= 16 && length - 16 <= bytes.size(),
              "complete native raw envelope");
      require(site != 8000 && site != 8001, "no native ingestion fence");
      const auto body = bytes.first(length - 16);
      records.push_back({read->origin.lane, ordinal, site, {body.begin(), body.end()}});
      bytes = bytes.subspan(length - 16);
    }
  }
  std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
    return std::pair{a.lane, a.ordinal} < std::pair{b.lane, b.ordinal};
  });
  std::map<IdentityBytes, std::uint64_t> previous;
  for (const auto &record : records)
    require(record.ordinal == ++previous[record.lane],
            "actual per-lane issuer order has no hole");
  return records;
}
struct NativeFrame {
  PhysicalJournalRecord physical;
  std::vector<std::byte> payload;
};
std::vector<NativeFrame> disk_frames(const Directory &directory,
                                     JournalHeader expected) {
  std::ifstream input{directory.path / "audit", std::ios::binary};
  require(input.good(), "independent original native journal exists");
  const std::vector<char> chars{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
  const auto bytes = std::as_bytes(std::span{chars});
  require(unwrap(decode_journal_header(bytes.first(journal_header_size))) == expected,
          "independent native journal identity");
  std::vector<NativeFrame> frames;
  std::size_t offset = journal_header_size;
  while (offset < bytes.size()) {
    const auto frame =
        unwrap(decode_journal_frame(bytes.subspan(offset), expected.limits));
    const auto checksum =
        static_cast<std::uint32_t>(little(bytes.subspan(offset + 28, 4)));
    if (frame.kind != FrameKind::commit)
      frames.push_back(
          {{expected.environment, expected.journal, frame.kind, frame.sequence,
            frame.batch_first, offset + journal_frame_header_size,
            static_cast<std::uint32_t>(frame.payload.size()), checksum},
           {frame.payload.begin(), frame.payload.end()}});
    offset += frame.encoded_size;
  }
  require(offset == bytes.size(),
          "independent disk scan ends at exact native frame boundary");
  return frames;
}
JournalHeader header() {
  return {id<EnvironmentId>(1),
          id<AuditStreamId>(2),
          3,
          {1024 * 1024, 4 * 1024 * 1024},
          std::nullopt};
}
constexpr JournalCapacity capacity{64 * 1024 * 1024, 4096};
void append_marker(RetainedState &root, const char *label) {
  AuditLog log{root};
  log.record(ApplicationChannel::program, Value::object({{"label", Value{label}}}));
}
Value saved_context() {
  return Value::object({{"version", Value{Number{"1"}}},
                        {"base", Value{std::string(32, '0')}},
                        {"entries", Value{Value::Array{}}},
                        {"live_originals", Value{Value::Array{}}}});
}
void saved_selection() {
  Directory directory;
  Gate gate;
  auto root = unwrap(RetainedState::create(
      std::make_unique<ForwardDirectory>(
          unwrap(NativeJournalDirectory::open(directory.path.string())), gate),
      "audit", header(), capacity));
  append_marker(*root, "first");
  unwrap(root->save_current_state(saved_context()));
  append_marker(*root, "second");
  (void)saved_stack();
  allocation_count = 0;
  measuring = true;
  gate.enabled = true;
  auto measured = root->save_current_state(saved_context());
  after_ack = false;
  measuring = false;
  gate.enabled = false;
  unwrap(std::move(measured));
  require(allocation_count == 0,
          "accepted SavedState transfer performs no postACK allocation");
  const auto current = root->cursor();
  const auto selected = *root->saved_state();
  require(equal(selected.boundary, current),
          "accepted saved slot installs exact live boundary");
  require(equal(selected_slot(directory.path, selected.slot, current.journal), current),
          "independent saved CRC/cursor agrees with selected live state");
  const auto expected = unwrap(root->fact_page(0, 16));
  auto pinned = root->history_snapshot();
  append_marker(*root, "third");
  (void)saved_stack();
  before_ack = true;
  measuring = true;
  allocation_count = 0;
  gate.enabled = true;
  auto staged_measure = root->save_current_state(saved_context());
  before_ack = false;
  after_ack = false;
  measuring = false;
  gate.enabled = false;
  unwrap(std::move(staged_measure));
  require(allocation_count != 0,
          "actual prepublication SavedState allocation measured");
  cut_caller = allocations.front().caller;
  require(allocations.front().saved_stack,
          "measured native SavedState allocation stack");
  const auto accepted_before_cut = root->saved_state()->boundary;
  const auto slot_before_cut = root->saved_state()->slot;
  const auto acks_before_cut = gate.acks;
  append_marker(*root, "prepublication-cut");
  cut_hit = false;
  before_ack = true;
  cutting = true;
  gate.enabled = true;
  auto refused = root->save_current_state(saved_context());
  before_ack = false;
  after_ack = false;
  cutting = false;
  gate.enabled = false;
  require(cut_hit && !refused.has_value() &&
              refused.error().code == ErrorCode::allocation,
          "actual measured SavedState staging allocation fails before publication");
  require(
      gate.acks == acks_before_cut &&
          equal(root->saved_state()->boundary, accepted_before_cut) &&
          root->saved_state()->slot == slot_before_cut &&
          equal(selected_slot(directory.path, slot_before_cut, root->cursor().journal),
                accepted_before_cut),
      "unaccepted staging allocation preserves exact selected disk/live generation");
  unwrap(root->save_current_state(saved_context()));
  const auto cold = unwrap(pinned.page(0, 16));
  require(cold.size() == expected.size(),
          "prior cold catalog lifetime survives new selection");
  for (std::size_t i = 0; i < cold.size(); ++i)
    require(cold[i].record == expected[i].record && cold[i].event == expected[i].event,
            "prior exact facts and references survive generation replacement");
  const auto final = root->cursor();
  root.reset();
  auto reopened = unwrap(
      RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                              NativeJournalDirectory::open(directory.path.string()))),
                          "audit", header(), capacity));
  require(reopened->saved_state() && equal(reopened->saved_state()->boundary, final),
          "independent reopen selects final actual saved slot");
  if (reopened->state() == JournalWriterState::recovery_pending)
    unwrap(reopened->confirm_recovery());
  const auto replayed = unwrap(reopened->fact_page(0, 16));
  require(replayed.size() >= expected.size(),
          "reopen retains all accepted historical originals");
  for (std::size_t i = 0; i < expected.size(); ++i)
    require(replayed[i].record == expected[i].record &&
                replayed[i].event == expected[i].event,
            "reopened exact historical event and reference agree independently");
}
void native_append() {
  Directory directory;
  const std::array<std::byte, 7> original{std::byte{0},   std::byte{255}, std::byte{10},
                                          std::byte{128}, std::byte{65},  std::byte{0},
                                          std::byte{66}};
  JournalCursor source_before{header().journal, 0, journal_header_size};
  auto source_after = source_before, refused_at = source_before;
  std::array<PhysicalJournalRecord, 1> acknowledged{
      {{header().environment, header().journal, FrameKind::source, 0, 0, 0, 0, 0}}};
  bool proposed_ack = false;
  std::vector<RetainedFact> accepted_facts;
  auto runtime = unwrap(
      RawRuntime::open(directory.path.string(), id<EnvironmentId>(1),
                       {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
  auto lane = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{lane};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string(), owner))),
        "audit", header(), capacity, owner));
    source_before = root->cursor();
    const std::array<ByteView, 1> sources{original};
    source_after =
        unwrap(root->append(source_before, sources, {}, acknowledged, &proposed_ack));
    require(proposed_ack,
            "exact proposed batch acknowledgment publishes caller metadata");
    require(source_after.sequence == source_before.sequence + 2,
            "one native source plus real commit accepted");
    require(unwrap(root->source({source_after.journal, source_before.sequence + 1})) ==
                std::vector<std::byte>{original.begin(), original.end()},
            "accepted source retains exact opaque bytes");
    append_marker(*root, "actual-append");
    require(root->fact_count() > 0, "native retained append accepted");
    accepted_facts = unwrap(root->fact_page(0, 16));
    refused_at = root->cursor();
    const auto refused = root->append(refused_at, {}, {});
    require(!refused.has_value() && refused.error().code == ErrorCode::invalid_range &&
                equal(root->cursor(), refused_at) && !root->pending_proposal(),
            "empty native append refuses without changing actual accepted custody");
  }
  lane.reset();
  runtime.reset();
  const auto records = read_records(directory);
  const auto frames = disk_frames(directory, header());
  require(!frames.empty() && acknowledged.front() == frames.front().physical,
          "caller acknowledgment metadata equals independently preserved native "
          "physical source");
  using Key = std::pair<IdentityBytes, std::uint64_t>;
  std::map<Key, const Record *> by_reference;
  for (const auto &record : records)
    require(by_reference.emplace(Key{record.lane, record.ordinal}, &record).second,
            "each actual issuer ordinal identifies one native record");
  const auto referenced = [&](RawEventRef ref, const Record &from) -> const Record & {
    const auto found = by_reference.find({ref.lane, ref.ordinal});
    require(found != by_reference.end() &&
                (ref.lane != from.lane || ref.ordinal < from.ordinal),
            "actual event reference points to an earlier owned native record");
    return *found->second;
  };
  const std::map<std::uint32_t, std::uint32_t> paired{
      {3105, 3104}, {3109, 3108}, {3116, 3115}, {3125, 3124}, {3141, 3140},
      {3145, 3144}, {3151, 3150}, {3161, 3160}, {3165, 3164}, {3167, 3166},
      {3175, 3174}, {3181, 3180}, {3183, 3182}, {3185, 3184}, {3187, 3186},
      {3189, 3188}, {3194, 3193}, {3196, 3195}, {3198, 3197}};
  std::map<Key, unsigned> replies;
  std::map<Key, std::array<unsigned, 2>> stages;
  std::size_t selected_frames = 0, source_fields = 0, source_appends = 0,
              refused_appends = 0;
  for (const auto &record : records) {
    auto body = ByteView{record.body};
    if (const auto pair = paired.find(record.site); pair != paired.end()) {
      const auto ref = take_ref(body);
      require(referenced(ref, record).site == pair->second,
              "native return links its distinct actual request site");
      require(++replies[{ref.lane, ref.ordinal}] == 1,
              "one terminal native return per actual request");
    }
    if (record.site == 3162) {
      const auto parent = take_ref(body);
      require(referenced(parent, record).site == 3160,
              "prepared native frame links actual framed append request");
      PhysicalJournalRecord selected{take_identity<EnvironmentId>(body),
                                     take_identity<AuditStreamId>(body),
                                     take<FrameKind>(body),
                                     take<std::uint64_t>(body),
                                     take<std::uint64_t>(body),
                                     take<std::uint64_t>(body),
                                     take<std::uint32_t>(body),
                                     take<std::uint32_t>(body)};
      require(
          body.empty() && selected_frames < frames.size() &&
              selected == frames[selected_frames].physical,
          "every emitted native frame field equals independent preserved disk framing");
      ++selected_frames;
    } else if (record.site == 3114) {
      const auto parent = take_ref(body);
      (void)referenced(parent, record);
      const auto journal = take_identity<AuditStreamId>(body);
      const auto sequence = take<std::uint64_t>(body),
                 offset = take<std::uint64_t>(body);
      const auto length = take<std::uint32_t>(body);
      const auto found =
          std::find_if(frames.begin(), frames.end(), [&](const auto &frame) {
            return frame.physical.journal == journal &&
                   frame.physical.sequence == sequence;
          });
      require(body.empty() && found != frames.end() &&
                  found->physical.kind == FrameKind::source &&
                  found->physical.payload_offset == offset &&
                  found->physical.payload_size == length &&
                  found->payload ==
                      std::vector<std::byte>{original.begin(), original.end()},
              "staged source fields identify the independently preserved exact opaque "
              "original");
      ++source_fields;
    } else if (record.site == 3100) {
      const auto parent = take_ref(body);
      require(referenced(parent, record).site == 1090,
              "semantic append arguments link existing actual region BEGIN");
      const auto expected = take_cursor(body);
      const auto sources = take<std::size_t>(body), events = take<std::size_t>(body);
      require(!take<bool>(body) && !take<bool>(body) && !take<bool>(body) &&
                  body.empty(),
              "ordinary unprotected native append flags and exact field widths");
      if (sources == 1 && events == 0) {
        require(equal(expected, source_before),
                "source append emits actual caller cursor");
        ++source_appends;
      }
      if (sources == 0 && events == 0) {
        require(equal(expected, refused_at),
                "refused append emits actual unchanged caller cursor");
        ++refused_appends;
      }
    } else if (record.site == 3103) {
      const auto parent = take_ref(body);
      (void)referenced(parent, record);
      const auto stage = take<std::uint8_t>(body);
      (void)take<std::uint64_t>(body);
      (void)take<std::uint64_t>(body);
      (void)take<std::uint64_t>(body);
      (void)take<std::uint32_t>(body);
      const auto prepared = take<bool>(body), pending = take<bool>(body);
      require(body.empty() && (stage == 1 || stage == 2) && prepared == (stage == 1) &&
                  pending == (stage == 1),
              "prepared then accepted publication emits actual pending/custody flags");
      ++stages[{parent.lane, parent.ordinal}][stage - 1];
    } else if (record.site == 3161) {
      const auto actual = take_cursor(body);
      require(
          take<JournalWriterState>(body) == JournalWriterState::live &&
              take<std::uint8_t>(body) == 0 && body.empty(),
          "successful framed return emits native live state and success branch only");
      require(actual.journal == refused_at.journal &&
                  actual.sequence <= refused_at.sequence &&
                  actual.end_offset <= refused_at.end_offset,
              "framed native return stays inside independent final accepted prefix");
      if (actual.sequence == source_after.sequence)
        require(equal(actual, source_after),
                "source framed return equals exact business accepted cursor");
    }
  }
  require(selected_frames == frames.size() && source_fields == 1 &&
              source_appends == 1 && refused_appends == 1,
          "every accepted disk frame and selected success/refusal boundary has its "
          "actual native capture");
  for (const auto &[key, count] : stages) {
    (void)key;
    require(count[0] == 1 && count[1] == 1,
            "each accepted append records one prepared and one published transition");
  }
  for (const auto &record : records)
    if (std::any_of(paired.begin(), paired.end(),
                    [&](const auto &pair) { return pair.second == record.site; }))
      require(replies[{record.lane, record.ordinal}] == 1,
              "every reached native request has exactly one return");
  auto reopened = unwrap(
      RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                              NativeJournalDirectory::open(directory.path.string()))),
                          "audit", header(), capacity));
  if (reopened->state() == JournalWriterState::recovery_pending)
    unwrap(reopened->confirm_recovery());
  const auto recovered = unwrap(reopened->fact_page(0, 16));
  require(recovered.size() == accepted_facts.size(),
          "native raw consumption does not alter preserved semantic facts");
  for (std::size_t i = 0; i < recovered.size(); ++i)
    require(recovered[i].record == accepted_facts[i].record &&
                recovered[i].event == accepted_facts[i].event,
            "independent reopened retained facts preserve actual native identities and "
            "values");
  std::cerr << "native owner fields/frame/original/request-return checks records="
            << records.size() << '\n';
}
void diagnostic_acceptance() {
  Directory directory;
  auto root = unwrap(
      RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(directory.path.string()))),
                            "audit", header(), capacity));
  const auto stale = root->cursor();
  append_marker(*root, "accepted-before-rejection");
  const auto before = root->cursor();
  const std::array<std::byte, 3> original{std::byte{255}, std::byte{0}, std::byte{127}};
  const std::array<ByteView, 1> sources{original};
  const PhysicalJournalRecord untouched{
      header().environment, header().journal, FrameKind::source, 0, 0, 0, 0, 0};
  std::array<PhysicalJournalRecord, 1> acknowledged{untouched};
  bool proposed_ack = true;
  const auto rejected = root->append(stale, sources, {}, acknowledged, &proposed_ack);
  require(!rejected.has_value() && rejected.error().code == ErrorCode::conflict &&
              !proposed_ack && acknowledged.front() == untouched &&
              root->cursor().sequence > before.sequence,
          "accepted diagnostic children never acknowledge the refused caller proposed "
          "batch");
  const auto current = root->cursor();
  proposed_ack = true;
  const auto malformed = root->append(current, sources, {}, {}, &proposed_ack);
  require(!malformed.has_value() &&
              malformed.error().code == ErrorCode::invalid_range && !proposed_ack &&
              equal(root->cursor(), current),
          "caller metadata cardinality is checked before any proposed or diagnostic "
          "publication");
}
void canceled_context_loan() {
  Directory directory;
  auto runtime = unwrap(
      RawRuntime::open(directory.path.string(), id<EnvironmentId>(1),
                       {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
  auto lane = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{lane};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string(), owner))),
        "audit", header(), capacity, owner));
    AuditLog log{*root};
    ContextStore context{log};
    context.append({Value::object({{"role", Value{"user"}},
                                   {"content", Value{"native held stop"}}})},
                   "before.stop");
    const auto view = context.view();
    const auto entry = string_field(field(view, "entries").array().front(), "id");
    context.begin_workflow();
    const auto proposal =
        Value::object({{"base", Value{context.head()}},
                       {"mode", Value{"select"}},
                       {"reason", Value{"native canceled settlement"}},
                       {"source", Value{"native.fixture"}},
                       {"ids", Value{Value::Array{Value{entry}}}}});
    require(field(context.manage(proposal), "staged") == Value{true},
            "real context proposal is pending before cancellation");
    context.append({Value::object({{"type", Value{"function_call"}},
                                   {"call_id", Value{"native.call"}},
                                   {"name", Value{"native"}},
                                   {"arguments", Value{"{}"}}})},
                   "before.call");
    auto protection = unwrap(root->protect_settlement({64 * 1024, 32}));
    auto *bundle = root->raw_stop_bundle();
    require(bundle && bundle->semantic && bundle->physical,
            "complete semantic and physical stop banks preadmitted");
    auto held = unwrap(raw_claim_control(9198));
    auto control = unwrap(held.split(2286));
    auto regions = unwrap(held.split(6912));
    const auto cancellation = unwrap(raw_socket_pair());
    struct Close {
      std::array<int, 2> descriptors;
      ~Close() {
        for (auto descriptor : descriptors)
          (void)::close(descriptor);
      }
    } close{cancellation};
    const auto history = root->history_snapshot();
    require(history.count > 0, "actual resident history tail exists");
    const auto resident = history.count - 1;
    require(
        unwrap(root->fact(resident)).record.sequence >
            root->saved_state()->boundary.sequence,
        "public inspection cut selects actual resident suffix after saved boundary");
    bool refused_foreign = false;
    std::jthread foreign{[&] {
      RawThreadBinding unauthorized{lane};
      try {
        RawRetainedScope invalid{*root, *bundle->semantic, bundle->physical.get(), {}};
      } catch (const Error &error) {
        refused_foreign = error.code == ErrorCode::wrong_incarnation;
      }
    }};
    foreign.join();
    require(
        refused_foreign,
        "same-issuer foreign OS thread refuses scope before mutable archive snapshot");
    require(::shutdown(cancellation[1], SHUT_WR) == 0,
            "actual sticky native cancellation EOF");
    const auto before = root->cursor();
    WorkflowAcceptance accepted;
    const char *stage = "held scope activation";
    try {
      {
        RawWaitScope canceled{{{cancellation[0], -1}, {}}};
        const auto fresh = raw_claim_control(16);
        require(!fresh.has_value() && fresh.error().code == ErrorCode::interrupted,
                "native cancellation refuses fresh admission before cleanup");
        RawRetainedScope prepaid{*root, *bundle->semantic, bundle->physical.get(), {}};
        const auto denied_fact = root->fact(resident);
        const auto denied_page = history.page(resident, 1);
        const auto denied_read = history.read(resident);
        require(!denied_fact.has_value() &&
                    denied_fact.error().code == ErrorCode::capacity &&
                    !denied_page.has_value() &&
                    denied_page.error().code == ErrorCode::capacity &&
                    !denied_read.has_value() &&
                    denied_read.error().code == ErrorCode::capacity,
                "resident public inspection refuses before stealing prepaid stop loan");
        const RawContextOrigin origin{
            {}, &control, &regions, RawContextOrigin::Input::no_new};
        stage = "held context rejection";
        (void)context.finish_workflow(false, Value{}, origin, &accepted);
        stage = "held context view";
        const auto stopping = context.view(origin);
        auto outputs =
            ContextStore::stop_outputs(field(stopping, "entries").array(), true);
        require(outputs.size() == 1,
                "native canceled tool has one existing unanswered output");
        stage = "held stop linkage publication";
        context.append(std::move(outputs), "stop.linkage", origin);
        require(accepted.context_settlement_accepted &&
                    !accepted.boundary_program_accepted &&
                    root->cursor().sequence > before.sequence,
                "actual canceled context rejection and stop linkage preserve accepted "
                "custody using held banks");
      }
    } catch (const Error &error) {
      std::cerr << "canceled semantic native stage=" << stage
                << " error=" << error_name(error.code) << ':' << error.detail << '\n';
      throw;
    }
    require(unwrap(history.read(resident)).record ==
                unwrap(root->fact(resident)).record,
            "ordinary authorized resident readers remain available after stop scope "
            "restoration");
    require(context.pending_proposal() == Value{} &&
                string_field(context.items().back(), "type") == "function_call_output",
            "semantic scope restoration leaves exact business stop output available");
  }
  lane.reset();
  runtime.reset();
  const auto records = read_records(directory);
  require(std::count_if(records.begin(), records.end(),
                        [](const auto &record) { return record.site == 3035; }) == 1,
          "one actual physical stop scope, no invented second allowance");
  const auto start =
      std::find_if(records.begin(), records.end(),
                   [](const auto &record) { return record.site == 3035; });
  require(start != records.end(), "actual stop scope starts");
  const auto end = std::find_if(start, records.end(), [&](const auto &record) {
    if (record.site != 3037)
      return false;
    auto body = ByteView{record.body};
    const auto ref = take_ref(body);
    return ref.lane == start->lane && ref.ordinal == start->ordinal;
  });
  require(end != records.end(), "actual stop scope ends");
  for (auto selected = start; selected != end; ++selected)
    require(selected->site != 3154 && selected->site != 3148 && selected->site != 3144,
            "no public selection or reader invocation consumes stop loan inside actual "
            "scope");
  unsigned accepted_publications = 0;
  for (const auto &record : records)
    if (record.site == 3103) {
      auto body = ByteView{record.body};
      (void)take_ref(body);
      if (take<std::uint8_t>(body) == 2)
        ++accepted_publications;
    }
  require(accepted_publications >= 4,
          "raw custody retains real initial, rejection, call and stop publications");
}
void native_environment() {
  Directory directory;
  auto runtime = unwrap(
      RawRuntime::open(directory.path.string(), id<EnvironmentId>(1),
                       {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
  auto lane = unwrap(runtime->prepare_thread());
  const EnvironmentBounds bounds{header().limits, capacity, 4, capacity.max_records,
                                 capacity.max_file_bytes};
  const std::array<std::byte, 4> original{std::byte{0}, std::byte{255}, std::byte{128},
                                          std::byte{10}};
  JournalCursor accepted{header().journal, 0, journal_header_size};
  {
    RawThreadBinding binding{lane};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    const auto open_directory = [&] {
      return std::make_unique<NativeJournalDirectory>(
          unwrap(NativeJournalDirectory::open(directory.path.string(), owner)));
    };
    auto environment = unwrap(RetainedEnvironment::create(open_directory(), header(),
                                                          capacity, bounds, owner));
    const std::array<ByteView, 1> sources{original};
    const auto proposal =
        unwrap(environment->submit_proposal(environment->cursor(), sources, {}));
    require(!proposal.existing && !proposal.capture && proposal.events.empty(),
            "actual environment native source proposal has no fabricated capture or "
            "events");
    accepted = proposal.cursor;
    require(unwrap(environment->source({accepted.journal, accepted.sequence - 1})) ==
                std::vector<std::byte>{original.begin(), original.end()},
            "actual selected environment retains exact binary source");
    environment.reset();
    environment = unwrap(
        RetainedEnvironment::open(open_directory(), header(), capacity, bounds, owner));
    require(environment->state() == JournalWriterState::recovery_pending,
            "reopened environment preserves required recovery instead of invented live "
            "selection");
    unwrap(environment->confirm_recovery());
    require(
        equal(environment->cursor(), accepted) &&
            unwrap(environment->source({accepted.journal, accepted.sequence - 1})) ==
                std::vector<std::byte>{original.begin(), original.end()},
        "actual reopened selected environment preserves the exact native accepted "
        "source and cursor");
  }
  lane.reset();
  runtime.reset();
  std::ifstream head{directory.path / "head", std::ios::binary};
  const std::vector<char> chars{std::istreambuf_iterator<char>{head},
                                std::istreambuf_iterator<char>{}};
  const auto selected = unwrap(decode_head_selector(std::as_bytes(std::span{chars})));
  require(selected.environment == header().environment &&
              selected.root == header().journal &&
              selected.active == header().journal && selected.generation == 1,
          "independent CRC-valid head bytes select actual native environment root");
  const auto records = read_records(directory);
  unsigned factories = 0, proposal_returns = 0, transaction_prepared = 0,
           transaction_completed = 0;
  for (const auto &record : records) {
    auto body = ByteView{record.body};
    if (record.site == 3261) {
      (void)take_ref(body);
      const auto cursor = take_cursor(body);
      const auto state = take<JournalWriterState>(body);
      require(take_identity<EnvironmentId>(body) == selected.environment &&
                  take_identity<AuditStreamId>(body) == selected.root &&
                  take_identity<AuditStreamId>(body) == selected.active &&
                  take<std::uint64_t>(body) == selected.generation &&
                  !take<bool>(body) && !take<bool>(body) &&
                  take<std::uint8_t>(body) == 0 && body.empty(),
              "environment factory emits exact independently selected head fields and "
              "native success flags");
      if (factories == 0)
        require(
            cursor.sequence == 0 && state == JournalWriterState::live,
            "actual new environment result carries its native initial cursor/state");
      else
        require(equal(cursor, accepted) &&
                    state == JournalWriterState::recovery_pending,
                "actual reopen result reports preserved native recovery-pending state");
      ++factories;
    } else if (record.site == 3263) {
      (void)take_ref(body);
      require(equal(take_cursor(body), accepted) && !take<bool>(body) &&
                  !take<bool>(body) && take<std::size_t>(body) == 0 &&
                  take<std::uint8_t>(body) == 0 && body.empty(),
              "proposal return emits its actual accepted cursor, absent optional "
              "capture and zero event vector");
      ++proposal_returns;
    } else if (record.site == 3248) {
      (void)take_ref(body);
      const auto healthy = take<bool>(body), initial = take<bool>(body),
                 attempted = take<bool>(body), published = take<bool>(body),
                 busy = take<bool>(body);
      require(
          take_identity<EnvironmentId>(body) == selected.environment &&
              take_identity<AuditStreamId>(body) == selected.root &&
              take_identity<AuditStreamId>(body) == selected.active &&
              take<std::uint64_t>(body) == selected.generation && body.empty(),
          "head transaction emits individual actual selector fields without padding");
      if (transaction_prepared == 0) {
        require(!attempted && !healthy && !published && !initial && busy,
                "native head transaction before-effect state");
        ++transaction_prepared;
      } else {
        require(attempted && healthy && published && !initial && busy,
                "native head transaction after-effect state");
        ++transaction_completed;
      }
    }
  }
  require(factories == 2 && proposal_returns == 1 && transaction_prepared == 1 &&
              transaction_completed == 1,
          "actual environment create/reopen/proposal/head transition captures are "
          "complete");
}
void generic_receipt_caller() {
  Directory directory;
  auto runtime = unwrap(
      RawRuntime::open(directory.path.string(), id<EnvironmentId>(1),
                       {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
  auto lane = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{lane};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string(), owner))),
        "audit", header(), capacity, owner));
    const std::vector<std::byte> original{std::byte{0}, std::byte{255}, std::byte{10}};
    const auto invocation = id<InvocationId>(9);
    const auto decision = id<DecisionId>(3);
    const auto attempt = id<OperationAttemptId>(11);
    const std::array admitted{
        RetainedEvent{{},
                      DecisionEvent{decision,
                                    id<ParticipantId>(4),
                                    id<ConversationId>(5),
                                    id<WorkflowId>(6),
                                    id<DefinitionGenerationId>(7),
                                    id<ContextRevisionId>(8),
                                    {invocation},
                                    original}},
        RetainedEvent{{},
                      InvocationEvent{invocation, decision,
                                      id<DefinitionGenerationId>(10), original}},
        RetainedEvent{
            {}, AttemptAdmissionEvent{attempt, invocation, decision, original, true}}};
    RawRetainedExec loan{*root, {}, RawRetainedExec::Kind::adapter_receipt};
    unwrap(root->append_exec(root->cursor(), admitted, loan));
    auto protection = unwrap(root->protect_settlement({64 * 1024, 32}));
    auto regions = unwrap(raw_claim_control(1728));
    const auto cancellation = unwrap(raw_socket_pair());
    struct Close {
      std::array<int, 2> descriptors;
      ~Close() {
        for (auto fd : descriptors)
          (void)::close(fd);
      }
    } close{cancellation};
    struct Boundary final : EffectBoundary {
      int writer;
      std::filesystem::path effect;
      std::vector<RawControlToken> pressure;
      unsigned calls = 0;
      Boundary(int fd, std::filesystem::path path)
          : writer(fd), effect(std::move(path)) {
        pressure.reserve(32);
      }
      Result<void> dispatch(const EffectIntent &) override {
        ++calls;
        std::ofstream file{effect, std::ios::binary};
        file << "accepted-native-effect";
        file.close();
        for (std::uint64_t bytes = 1024 * 1024; bytes; bytes /= 2) {
          auto grant = raw_try_claim_control(bytes);
          if (grant.has_value())
            pressure.push_back(std::move(grant).value());
        }
        require(::shutdown(writer, SHUT_WR) == 0,
                "actual generic sticky cancellation after effect");
        return Result<void>::failure({ErrorCode::interrupted});
      }
    } body{cancellation[1], directory.path / "generic-effect"};
    DispatchReport report{false, Result<void>::success(), Result<void>::success()};
    {
      RawWaitScope waiting{{{cancellation[0], -1}, {}}};
      RawRegionPrepaidScope paid{regions};
      report = unwrap(root->dispatch(attempt, body, &loan));
      const auto denied = raw_claim_control(16);
      require(!denied.has_value() && denied.error().code == ErrorCode::interrupted,
              "actual sticky cancellation remains active after paid receipt");
    }
    require(body.calls == 1 && report.dispatched && !report.effect.has_value() &&
                report.effect.error().code == ErrorCode::interrupted &&
                report.recording.has_value(),
            "generic receipt retains actual interrupted effect under full CONTROL and "
            "sticky cancellation");
    body.pressure.clear();
    std::ifstream effect{body.effect, std::ios::binary};
    const std::string bytes{std::istreambuf_iterator<char>{effect},
                            std::istreambuf_iterator<char>{}};
    require(bytes == "accepted-native-effect",
            "generic effect remains independently visible");
    const auto actual = unwrap(root->attempt(attempt));
    require(actual.receipt && actual.receipt->failure &&
                actual.receipt->failure->code == ErrorCode::interrupted,
            "retained native receipt has actual effect Error, not invented success");
  }
  lane.reset();
  runtime.reset();
  const auto records = read_records(directory);
  unsigned scopes = 0, receipts = 0;
  for (const auto &record : records) {
    if (record.site == 3035)
      ++scopes;
    if (record.site == 3033) {
      auto bytes = ByteView{record.body};
      (void)take_ref(bytes);
      if (take<bool>(bytes))
        (void)take_ref(bytes);
      if (take<RawStorageLoanKind>(bytes) == RawStorageLoanKind::adapter_receipt) {
        require(take<std::uint64_t>(bytes) == 420 && take<std::size_t>(bytes) == 4 &&
                    take<std::size_t>(bytes) == 1 && take<std::size_t>(bytes) == 4,
                "generic receipt uses exact separate finite write envelope");
        (void)take<std::uint64_t>(bytes);
        require(take<std::uint64_t>(bytes) == 56 && take<std::uint64_t>(bytes) == 1 &&
                    bytes.empty(),
                "generic receipt pays exact actual Open dependency frame");
        ++receipts;
      }
    }
  }
  require(scopes == 1 && receipts == 1,
          "one generic receipt physical scope and native admission");
}
void generic_projection_cancellation() {
  Directory directory;
  auto runtime = unwrap(
      RawRuntime::open(directory.path.string(), id<EnvironmentId>(1),
                       {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
  auto lane = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{lane};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string(), owner))),
        "audit", header(), capacity, owner));
    AuditLog log{*root};
    ContextStore context{log};
    struct Unused final : CodingProvider {
      Value respond(const Value &,
                    const std::function<void(std::string_view)> &) override {
        throw std::runtime_error("selected Lua must not call provider");
      }
    } provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    const auto cancellation = unwrap(raw_socket_pair());
    struct Close {
      std::array<int, 2> descriptors;
      ~Close() {
        for (auto fd : descriptors)
          (void)::close(fd);
      }
    } close{cancellation};
    std::array<void *, 48> warm{};
    (void)::backtrace(warm.data(), static_cast<int>(warm.size()));
    projection.pressure.reserve(32);
    projection.writer = cancellation[1];
    projection.armed = true;
    projection.hit = false;
    projection.failed = false;
    std::optional<Error> failure;
    {
      RawWaitScope waiting{{{cancellation[0], -1}, {}}};
      try {
        (void)engine.operator_call(
            "lua", Value::object({{"code", Value{"return string.rep('Q', 32768)"}}}));
      } catch (const Error &error) {
        failure = error;
      }
      projection.armed = false;
      projection.pressure.clear();
    }
    require(projection.hit && !projection.failed,
            "actual Lua result projection allocation forwards and triggers native "
            "sticky EOF");
    require(
        failure && failure->code == ErrorCode::interrupted,
        "generic later semantic result retention preserves actual interrupted Error");
    unsigned receipts = 0, terminals = 0;
    for (std::size_t first = 0; first < root->fact_count(); first += 16)
      for (const auto &fact : unwrap(root->fact_page(first, 16))) {
        if (std::holds_alternative<AdapterReceiptEvent>(fact.event.body))
          ++receipts;
        if (std::holds_alternative<AttemptObservationEvent>(fact.event.body))
          ++terminals;
      }
    require(receipts == 1 && terminals == 0,
            "paid generic receipt retained without fabricated later business terminal "
            "or locator");
  }
  lane.reset();
  runtime.reset();
  const auto records = read_records(directory);
  unsigned exact_originals = 0, failed_retention = 0;
  for (const auto &record : records) {
    auto bytes = ByteView{record.body};
    if (record.site == 2402) {
      (void)take_ref(bytes);
      const auto role = take<std::uint8_t>(bytes);
      const auto length = take<std::size_t>(bytes);
      require(length == bytes.size(),
              "raw original emits its actual complete native byte length");
      if (role == 1) {
        const auto decoded = unwrap(decode_packet(bytes));
        if (const auto *text = std::get_if<std::string>(&decoded.value());
            text && *text == std::string(32768, 'Q'))
          ++exact_originals;
      }
    } else if (record.site == 2417) {
      (void)take_ref(bytes);
      const auto branch = take<RawOperationBranch>(bytes);
      const auto stage = take<std::uint8_t>(bytes);
      const bool failed = take<bool>(bytes);
      if (branch == RawOperationBranch::error && stage == 7 && failed &&
          take<ErrorCode>(bytes) == ErrorCode::interrupted)
        ++failed_retention;
    }
  }
  require(exact_originals == 1 && failed_retention == 1,
          "exact acquired Lua result remains raw on disk with actual semantic "
          "retention failure");
}
void generic_capacity_terminal() {
  Directory directory;
  auto runtime = unwrap(
      RawRuntime::open(directory.path.string(), id<EnvironmentId>(1),
                       {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
  auto lane = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{lane};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto selected = header();
    selected.limits = {2048, 8192};
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string(), owner))),
        "audit", selected, capacity, owner));
    AuditLog log{*root};
    ContextStore context{log};
    struct Unused final : CodingProvider {
      Value respond(const Value &,
                    const std::function<void(std::string_view)> &) override {
        throw std::runtime_error("selected Lua must not call provider");
      }
    } provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    std::optional<Error> failure;
    try {
      (void)engine.operator_call(
          "lua", Value::object({{"code", Value{"return string.rep('C', 4096)"}}}));
    } catch (const Error &error) {
      failure = error;
    }
    require(failure && failure->code == ErrorCode::capacity,
            "actual large generic result cannot fit native product source cap");
    unsigned terminals = 0;
    for (std::size_t first = 0; first < root->fact_count(); first += 16)
      for (const auto &fact : unwrap(root->fact_page(first, 16)))
        if (const auto *terminal =
                std::get_if<AttemptObservationEvent>(&fact.event.body)) {
          const auto decoded =
              unwrap(decode_packet(unwrap(terminal->observation.read())));
          require(terminal->observation.size() < 8192 &&
                      string_field(decoded, "error") == "capacity" &&
                      !decoded.find("result_id") &&
                      terminal->disposition == AttemptDisposition::unknown,
                  "actual generic capture refusal retains bounded native Error "
                  "terminal without invented locator");
          ++terminals;
        }
    require(terminals == 1, "actual generic capacity path retains one honest terminal");
  }
  lane.reset();
  runtime.reset();
  const auto records = read_records(directory);
  unsigned original = 0, fallback = 0, terminal_scopes = 0;
  std::map<std::pair<IdentityBytes, std::uint64_t>, RawStorageLoanKind> loans;
  for (const auto &record : records) {
    auto bytes = ByteView{record.body};
    if (record.site == 2402) {
      (void)take_ref(bytes);
      const auto role = take<RawOperationOriginal>(bytes);
      const auto length = take<std::size_t>(bytes);
      require(length == bytes.size(), "actual complete raw capacity original");
      if (role != RawOperationOriginal::primary &&
          role != RawOperationOriginal::fallback)
        continue;
      const auto decoded = unwrap(decode_packet(bytes));
      if (role == RawOperationOriginal::primary &&
          decoded == Value{std::string(4096, 'C')})
        ++original;
      if (role == RawOperationOriginal::fallback &&
          string_field(decoded, "error") == "capacity")
        ++fallback;
    } else if (record.site == 3033) {
      (void)take_ref(bytes);
      if (take<bool>(bytes))
        (void)take_ref(bytes);
      loans[{record.lane, record.ordinal}] = take<RawStorageLoanKind>(bytes);
    } else if (record.site == 3035) {
      const auto loan = take_ref(bytes);
      if (loans.at({loan.lane, loan.ordinal}) == RawStorageLoanKind::adapter_terminal)
        ++terminal_scopes;
    }
  }
  require(original == 1 && fallback == 1 && terminal_scopes == 1,
          "raw large result and bounded actual Error survive under one paid generic "
          "terminal scope");
}
void native_frame_quota_boundary() {
  Directory directory;
  auto runtime = unwrap(
      RawRuntime::open(directory.path.string(), id<EnvironmentId>(1),
                       {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
  auto lane = unwrap(runtime->prepare_thread());
  auto selected = header();
  selected.limits = {32 * 1024 * 1024, 64 * 1024 * 1024};
  const auto invocation = id<InvocationId>(9);
  const auto decision = id<DecisionId>(3);
  const auto attempt = id<OperationAttemptId>(11);
  {
    RawThreadBinding binding{lane};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string(), owner))),
        "audit", selected, JournalCapacity{128 * 1024 * 1024, 4096}, owner));
    const std::vector<std::byte> continuation{std::byte{0}, std::byte{255},
                                              std::byte{10}};
    const std::vector<std::byte> input(16 * 1024 * 1024 - 60, std::byte{'I'});
    const std::array admitted{
        RetainedEvent{{},
                      DecisionEvent{decision,
                                    id<ParticipantId>(4),
                                    id<ConversationId>(5),
                                    id<WorkflowId>(6),
                                    id<DefinitionGenerationId>(7),
                                    id<ContextRevisionId>(8),
                                    {invocation},
                                    continuation}},
        RetainedEvent{{},
                      InvocationEvent{invocation, decision,
                                      id<DefinitionGenerationId>(10), input}},
        RetainedEvent{
            {}, AttemptAdmissionEvent{attempt, invocation, decision, input, true}}};
    RawRetainedExec loan{*root, {}};
    unwrap(root->append_exec(root->cursor(), admitted, loan));
    unwrap(root->save_current_state(saved_context()));
    struct Boundary final : EffectBoundary {
      unsigned calls = 0;
      Result<void> dispatch(const EffectIntent &) override {
        ++calls;
        return Result<void>::success();
      }
    } body;
    loan.enter({}, RawRetainedOrigin::Input::ordinary);
    const auto report = unwrap(root->dispatch(attempt, body, &loan));
    const RetainedEvent terminal{{},
                                 AttemptObservationEvent{attempt,
                                                         AttemptPhase::terminal,
                                                         AttemptDisposition::success,
                                                         {std::byte{'T'}}}};
    (void)unwrap(root->submit_settlement(terminal));
    loan.leave();
    require(body.calls == 1 && report.dispatched && report.effect.has_value() &&
                report.recording.has_value(),
            "actual archived Invocation boundary dispatch and receipt fit corrected "
            "finite native read loan");
  }
  lane.reset();
  runtime.reset();
  const auto frames = disk_frames(directory, selected);
  const auto invocation_frame =
      std::find_if(frames.begin(), frames.end(), [&](const auto &frame) {
        if (frame.physical.kind != FrameKind::semantic)
          return false;
        const auto decoded =
            unwrap(decode_retained_event(frame.payload, selected.limits.max_payload));
        const auto *actual = std::get_if<InvocationEvent>(&decoded.body);
        return actual && actual->invocation == invocation;
      });
  require(invocation_frame != frames.end() &&
              invocation_frame->physical.payload_size == 16 * 1024 * 1024,
          "independent CRC-valid disk Invocation payload is exactly native16MiB");
  const auto decision_frame =
      std::find_if(frames.begin(), frames.end(), [&](const auto &frame) {
        if (frame.physical.kind != FrameKind::semantic)
          return false;
        const auto decoded =
            unwrap(decode_retained_event(frame.payload, selected.limits.max_payload));
        const auto *actual = std::get_if<DecisionEvent>(&decoded.body);
        return actual && actual->decision == decision;
      });
  require(decision_frame != frames.end(), "independent native Decision frame retained");
  const auto records = read_records(directory, 32 * 1024 * 1024);
  RawEventRef grant{}, scope{}, journal{};
  unsigned full_reads = 0, header_remainders = 0;
  const auto frame_start = invocation_frame->physical.payload_offset - 32;
  for (const auto &record : records) {
    auto bytes = ByteView{record.body};
    if (record.site == 3033) {
      (void)take_ref(bytes);
      if (take<bool>(bytes))
        (void)take_ref(bytes);
      if (take<RawStorageLoanKind>(bytes) != RawStorageLoanKind::selected_exec)
        continue;
      (void)take<std::uint64_t>(bytes);
      (void)take<std::size_t>(bytes);
      (void)take<std::size_t>(bytes);
      (void)take<std::size_t>(bytes);
      (void)take<std::uint64_t>(bytes);
      require(take<std::uint64_t>(bytes) ==
                      504 + 3 * invocation_frame->physical.payload_size +
                          decision_frame->physical.payload_size &&
                  take<std::uint64_t>(bytes) == 12 && bytes.empty(),
              "native caller grant counts frame header inside the Invocation read "
              "ceiling without reencoding");
      grant = {record.lane, record.ordinal};
    } else if (record.site == 3035) {
      const auto actual = take_ref(bytes);
      if (actual.lane == grant.lane && actual.ordinal == grant.ordinal) {
        scope = {record.lane, record.ordinal};
        journal = take_ref(bytes);
      }
    } else if (record.site == 3007 && scope.ordinal && record.lane == scope.lane &&
               record.ordinal > scope.ordinal) {
      (void)take_ref(bytes);
      const auto resource = take_ref(bytes);
      (void)take<std::uint8_t>(bytes);
      (void)take<int>(bytes);
      const auto count = take<std::size_t>(bytes);
      const auto positioned = take<bool>(bytes);
      const auto offset = positioned ? take<off_t>(bytes) : off_t{-1};
      if (resource.lane == journal.lane && resource.ordinal == journal.ordinal &&
          count == 16 * 1024 * 1024 &&
          static_cast<std::uint64_t>(offset) == frame_start)
        ++full_reads;
      if (resource.lane == journal.lane && resource.ordinal == journal.ordinal &&
          count == 32 &&
          static_cast<std::uint64_t>(offset) == frame_start + 16 * 1024 * 1024)
        ++header_remainders;
    }
  }
  require(full_reads == 3 && header_remainders == 3,
          "three actual archived Invocation frames each acquire16MiB plus their32-byte "
          "remainder under the loan");
}
void completion_reentry() {
  Directory directory;
  auto runtime = unwrap(
      RawRuntime::open(directory.path.string(), id<EnvironmentId>(1),
                       {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
  auto lane = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{lane};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string(), owner))),
        "audit", header(), capacity, owner));
    AuditLog log{*root};
    ContextStore outer_context{log};
    struct Unused final : CodingProvider {
      Value respond(const Value &,
                    const std::function<void(std::string_view)> &) override {
        throw std::runtime_error("completion reentry must not call provider");
      }
    } provider;
    CodingEngine outer{log, outer_context, provider, "native.outer"};
    const auto target = directory.path / "completion-child-effect";
    unsigned calls = 0;
    std::optional<Error> failure;
    outer.operation_completed = [&](std::string_view) {
      if (calls)
        return;
      ++calls;
      try {
        (void)(outer.*selected_call(SelectedCall{}))(
            "exec",
            Value::object(
                {{"argv", Value{Value::Array{Value{"/bin/sh"}, Value{"-c"},
                                             Value{"printf reentrant-effect > '" +
                                                   target.string() + "'"}}}},
                 {"yield_ms", Value{Number{1000}}}}));
      } catch (const Error &error) {
        failure = error;
      }
    };
    (void)outer.operator_call("lua", Value::object({{"code", Value{"return 17"}}}));
    std::ifstream effect{target, std::ios::binary};
    const std::string bytes{std::istreambuf_iterator<char>{effect},
                            std::istreambuf_iterator<char>{}};
    if (failure)
      std::cerr << "completion reentry native Error=" << error_name(failure->code)
                << ':' << failure->detail << '\n';
    require(
        calls == 1 && !failure && bytes == "reentrant-effect",
        "actual completed callback reenters normal exec after terminal borrow ends");
  }
  lane.reset();
  runtime.reset();
  const auto records = read_records(directory);
  std::vector<RawEventRef> operations;
  RawEventRef terminal_grant{}, scope{};
  bool returned = false, ended = false;
  for (const auto &record : records) {
    auto body = ByteView{record.body};
    if (record.site == 2400) {
      if (operations.size() == 1)
        require(returned && ended, "outer native terminal scope returns and ends "
                                   "before nested operation starts");
      operations.push_back({record.lane, record.ordinal});
    } else if (record.site == 3033) {
      (void)take_ref(body);
      const bool parent = take<bool>(body);
      const auto origin = parent ? take_ref(body) : RawEventRef{};
      const auto kind = take<RawStorageLoanKind>(body);
      if (kind == RawStorageLoanKind::adapter_terminal && !operations.empty() &&
          origin.lane == operations.front().lane &&
          origin.ordinal == operations.front().ordinal)
        terminal_grant = {record.lane, record.ordinal};
    } else if (record.site == 3035) {
      const auto grant = take_ref(body);
      if (grant.lane == terminal_grant.lane && grant.ordinal == terminal_grant.ordinal)
        scope = {record.lane, record.ordinal};
    } else if (record.site == 3036 || record.site == 3037) {
      const auto parent = take_ref(body);
      if (scope.ordinal && parent.lane == scope.lane &&
          parent.ordinal == scope.ordinal) {
        if (record.site == 3036)
          returned = true;
        else
          ended = true;
      }
    }
  }
  require(operations.size() == 2 && returned && ended,
          "actual outer/nested operation order links completed native terminal scope");
}
void checked_payload_error_state() {
  Directory directory;
  auto runtime = unwrap(
      RawRuntime::open(directory.path.string(), id<EnvironmentId>(1),
                       {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
  auto lane = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{lane};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string(), owner))),
        "audit", header(), capacity, owner));
    const std::array<std::byte, 3> original{std::byte{0}, std::byte{255},
                                            std::byte{10}};
    const auto before = root->cursor();
    const std::array<ByteView, 1> source{original};
    unwrap(root->append(before, source, {}));
    std::ifstream file{directory.path / "audit", std::ios::binary};
    const std::vector<char> preserved{std::istreambuf_iterator<char>{file},
                                      std::istreambuf_iterator<char>{}};
    file.close();
    std::filesystem::resize_file(directory.path / "audit", before.end_offset + 32 + 1);
    const auto failed = root->source({before.journal, before.sequence + 1});
    require(!failed.has_value() && failed.error().code == ErrorCode::incomplete &&
                root->state() == JournalWriterState::blocked,
            "actual native EOF blocks the owned checked-packet reader");
    std::ofstream restore{directory.path / "audit", std::ios::binary | std::ios::trunc};
    restore.write(preserved.data(), static_cast<std::streamsize>(preserved.size()));
    restore.close();
    require(
        disk_frames(directory, header()).front().payload ==
            std::vector<std::byte>{original.begin(), original.end()},
        "original CRC-valid source bytes restored after fixture-only native EOF fault");
  }
  lane.reset();
  runtime.reset();
  const auto records = read_records(directory);
  unsigned failures = 0, positive_prefix = 0, native_eof = 0;
  const auto frames = disk_frames(directory, header());
  require(frames.size() == 1 && frames.front().payload.size() == 3,
          "actual native source restored independently");
  std::ifstream disk{directory.path / "audit", std::ios::binary};
  const std::vector<char> raw{std::istreambuf_iterator<char>{disk},
                              std::istreambuf_iterator<char>{}};
  const auto expected_prefix =
      std::as_bytes(std::span{raw}).subspan(journal_header_size, 33);
  for (const auto &record : records) {
    if (record.site == 3008) {
      auto bytes = ByteView{record.body};
      (void)take_ref(bytes);
      const auto count = take<ssize_t>(bytes);
      const auto failed = take<bool>(bytes);
      if (count == 33 && !failed &&
          std::equal(bytes.begin(), bytes.end(), expected_prefix.begin(),
                     expected_prefix.end()))
        ++positive_prefix;
      if (count == 0 && !failed && bytes.empty())
        ++native_eof;
    }
    if (record.site == 3165) {
      auto bytes = ByteView{record.body};
      (void)take_ref(bytes);
      require(take<JournalWriterState>(bytes) == JournalWriterState::blocked &&
                  take<std::uint8_t>(bytes) == 1 &&
                  take<ErrorCode>(bytes) == ErrorCode::incomplete &&
                  take<std::int64_t>(bytes) == 0 && bytes.empty(),
              "direct checked-read Error emits actual owned state with no invented "
              "acquired count");
      ++failures;
    }
  }
  require(failures == 1 && positive_prefix == 1 && native_eof == 1,
          "one actual failed checked-read state terminal preserves native33-byte "
          "prefix plus EOF");
}
void native_exec_caller() {
  Directory directory;
  auto runtime = unwrap(
      RawRuntime::open(directory.path.string(), id<EnvironmentId>(1),
                       {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
  auto lane = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{lane};
    auto owner = unwrap(RawProductStorageOwner::prepare());
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string(), owner))),
        "audit", header(), capacity, owner));
    AuditLog log{*root};
    ContextStore context{log};
    struct Unused final : CodingProvider {
      Value respond(const Value &,
                    const std::function<void(std::string_view)> &) override {
        throw std::runtime_error("selected native exec must not use provider");
      }
    } provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    const auto target = directory.path / "actual-child-effect";
    const auto script =
        "printf 'native-effect' > '" + target.string() + "'; printf 'opaque-original'";
    std::optional<Error> failure;
    try {
      (void)engine.operator_call(
          "exec",
          Value::object({{"argv", Value{Value::Array{Value{"/bin/sh"}, Value{"-c"},
                                                     Value{script}}}},
                         {"yield_ms", Value{Number{1000}}}}));
    } catch (const Error &error) {
      failure = error;
    }
    std::ifstream effect{target, std::ios::binary};
    const std::string bytes{std::istreambuf_iterator<char>{effect},
                            std::istreambuf_iterator<char>{}};
    require(bytes == "native-effect",
            "actual selected exec child effect is independently visible");
    if (failure)
      std::cerr << "selected exec failure=" << error_name(failure->code) << ':'
                << failure->detail << " state=" << static_cast<unsigned>(root->state())
                << '\n';
    require(!failure && root->state() == JournalWriterState::live,
            "actual selected exec preserves successful receipt and joined terminal "
            "custody");
    const auto invalid = engine.operator_call(
        "exec", Value::object({{"timeout_seconds", Value{Number{0}}}}));
    require(
        string_field(invalid, "error") == "invalid_range",
        "actual exec refusal before child launch retains its bounded failure terminal");
    const auto large = engine.operator_call(
        "lua", Value::object({{"code", Value{"return string.rep('L', 32768)"}}}));
    require(large == Value{std::string(32768, 'L')},
            "actual accepted large generic Lua result remains exact");
    const auto nested_target = directory.path / "nested-child-effect";
    const auto nested_script = "printf nested-effect > '" + nested_target.string() +
                               "'; printf nested-original";
    const auto quoted = unwrap(format_value(Value{nested_script}));
    const auto code = "return blackbird.call('exec',{argv={'/bin/sh','-c'," + quoted +
                      "},yield_ms=1000})";
    (void)engine.operator_call("lua", Value::object({{"code", Value{code}}}));
    std::ifstream nested{nested_target, std::ios::binary};
    const std::string nested_bytes{std::istreambuf_iterator<char>{nested},
                                   std::istreambuf_iterator<char>{}};
    require(nested_bytes == "nested-effect" &&
                root->state() == JournalWriterState::live,
            "actual nested Lua-to-exec admits simultaneous owner holds and preserves "
            "child effect");
    unsigned locators = 0;
    for (std::size_t first = 0; first < root->fact_count(); first += 16)
      for (const auto &fact : unwrap(root->fact_page(first, 16)))
        if (const auto *terminal =
                std::get_if<AttemptObservationEvent>(&fact.event.body)) {
          const auto decoded =
              unwrap(decode_packet(unwrap(terminal->observation.read())));
          if (decoded.find("result_id")) {
            require(terminal->observation.size() < 8192 &&
                        decoded.find("result_record"),
                    "accepted generic result terminal carries actual small locator");
            ++locators;
          }
        }
    require(locators == 3, "no-child exec and both generic Lua owners retain actual "
                           "small terminal locators");
  }
  lane.reset();
  runtime.reset();
  const auto records = read_records(directory);
  std::map<std::pair<IdentityBytes, std::uint64_t>,
           std::pair<RawStorageLoanKind, unsigned>>
      loans;
  for (const auto &record : records) {
    auto body = ByteView{record.body};
    if (record.site == 3033) {
      (void)take_ref(body);
      if (take<bool>(body))
        (void)take_ref(body);
      loans[{record.lane, record.ordinal}] = {take<RawStorageLoanKind>(body), 0};
    } else if (record.site == 3035) {
      const auto loan = take_ref(body);
      const auto found = loans.find({loan.lane, loan.ordinal});
      require(found != loans.end(),
              "every physical caller scope links its actual prior native grant");
      ++found->second.second;
    }
  }
  unsigned exec_loans = 0, receipt_loans = 0, terminal_loans = 0, joined_loans = 0,
           exec_two = 0, exec_three = 0;
  for (const auto &[reference, loan] : loans) {
    (void)reference;
    if (loan.first == RawStorageLoanKind::selected_exec) {
      if (loan.second == 2)
        ++exec_two;
      else if (loan.second == 3)
        ++exec_three;
      else
        require(
            false,
            "actual exec uses only reached prebody/receipt/no-child-terminal scopes");
      ++exec_loans;
    }
    if (loan.first == RawStorageLoanKind::adapter_receipt) {
      require(loan.second == 1, "each generic receipt uses one postbody scope");
      ++receipt_loans;
    }
    if (loan.first == RawStorageLoanKind::adapter_terminal) {
      require(loan.second == 1, "actual generic terminal shares one finite scope");
      ++terminal_loans;
    }
    if (loan.first == RawStorageLoanKind::joined_terminal) {
      require(loan.second == 1, "actual joined Job terminal consumes one joined scope");
      ++joined_loans;
    }
  }
  require(exec_loans == 3 && exec_two == 2 && exec_three == 1 && receipt_loans == 2 &&
              terminal_loans == 2 && joined_loans == 2,
          "whole caller native grants cover actual plain exec, large Lua and nested "
          "Lua-to-exec owners");
}
} // namespace
void *operator new(std::size_t size) {
  void *caller = __builtin_return_address(0);
  if ((after_ack || before_ack) && !stack_busy && (measuring || cutting)) {
    const bool stack = saved_stack();
    if (measuring && (!before_ack || stack) && allocation_count < allocations.size())
      allocations[allocation_count++] = {caller, size, stack};
    if (cutting && caller == cut_caller && stack) {
      cut_hit = true;
      cutting = false;
      throw std::bad_alloc{};
    }
  }
  if (void *memory = std::malloc(size ? size : 1)) {
    cancel_projection(size);
    return memory;
  }
  throw std::bad_alloc{};
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }
int main(int argc, char **argv) {
  try {
    require(argc == 2, "choose genuine baseline case");
    if (std::string_view{argv[1]} == "saved")
      saved_selection();
    else if (std::string_view{argv[1]} == "reentry")
      completion_reentry();
    else if (std::string_view{argv[1]} == "checked-error")
      checked_payload_error_state();
    else if (std::string_view{argv[1]} == "caller") {
      generic_receipt_caller();
      generic_projection_cancellation();
      generic_capacity_terminal();
      native_frame_quota_boundary();
      native_exec_caller();
      completion_reentry();
    } else {
      native_append();
      diagnostic_acceptance();
      canceled_context_loan();
      native_environment();
      checked_payload_error_state();
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const Error &error) {
    std::cerr << "native Error=" << static_cast<unsigned>(error.code) << '\n';
    return 2;
  }
}
