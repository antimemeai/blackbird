#include "blackbird/context.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/retained_proposal.hpp"
#include "blackbird/saved_state.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <dlfcn.h>
#include <execinfo.h>
#include <fstream>
#include <map>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc.h>
#endif
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace blackbird;
namespace {
thread_local bool fail_allocations = false;
struct Allocation {
  void *caller;
  std::size_t size;
  bool pin_stack;
};
thread_local std::array<Allocation, 64> history_allocations{};
thread_local std::size_t history_allocation_count = 0;
thread_local bool measure_history = false, history_cut_hit = false;
thread_local void *history_cut = nullptr;
thread_local bool inspect_history_stack = false;
bool actual_pin_stack() {
  if (inspect_history_stack)
    return false;
  inspect_history_stack = true;
  std::array<void *, 64> stack{};
  const int count = ::backtrace(stack.data(), static_cast<int>(stack.size()));
  bool found = false;
  for (int index = 0; index < count; ++index) {
    Dl_info frame{};
    if (::dladdr(stack[static_cast<std::size_t>(index)], &frame) && frame.dli_sname &&
        (std::strstr(frame.dli_sname, "archive_reader") ||
         std::strstr(frame.dli_sname, "history_snapshot")))
      found = true;
  }
  inspect_history_stack = false;
  return found;
}
const char *stage = "setup";
void require(bool condition, const char *claim) {
  if (!condition)
    throw std::runtime_error(claim);
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char name[] = "/tmp/blackbird-context-raw-XXXXXX";
    const auto *created = ::mkdtemp(name);
    require(created != nullptr, "private native Context directory");
    path = created;
  }
  ~Directory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
template <class T> T identity(unsigned n) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(n);
  return unwrap(T::from_bytes(bytes));
}
void baseline(bool pending) {
  Directory directory;
  auto runtime =
      unwrap(RawRuntime::open(directory.path.string(), identity<EnvironmentId>(73),
                              {{1024 * 1024, 1024 * 1024, 4096}, 4096}));
  auto lane = unwrap(runtime->prepare_thread());
  {
    RawThreadBinding binding{lane};
    const JournalHeader header{identity<EnvironmentId>(73),
                               identity<AuditStreamId>(74),
                               3,
                               {1024 * 1024, 4 * 1024 * 1024},
                               std::nullopt};
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string()))),
        "audit", header, {32 * 1024 * 1024, 4096}));
    AuditLog log{*root};
    ContextStore context{log};
    const auto item = Value::object(
        {{"role", Value{"assistant"}}, {"content", Value{std::string{"a\0b", 3}}}});
    const auto before = root->cursor();
    context.append({item}, "native.fixture");
    const auto view = context.view();
    require(context.items() == Value::Array{item} && context.head().size() == 32 &&
                root->cursor().sequence > before.sequence,
            "actual Context append and view preserve native business state");
    if (pending) {
      const auto id = string_field(field(view, "entries").array().front(), "id");
      const auto proposal = Value::object({{"base", Value{context.head()}},
                                           {"mode", Value{"select"}},
                                           {"reason", Value{"native.fixture"}},
                                           {"source", Value{"native.fixture"}},
                                           {"ids", Value{Value::Array{Value{id}}}}});
      context.begin_workflow();
      const auto staged = context.manage(proposal);
      require(field(staged, "staged") == Value{true}, "actual managed proposal staged");
      const auto owned = context.pending_proposal();
      const Value boundary;
      bool refused = false;
      fail_allocations = true;
      try {
        (void)context.finish_workflow(true, boundary);
      } catch (const std::bad_alloc &) {
        fail_allocations = false;
        refused = true;
      } catch (...) {
        fail_allocations = false;
        throw;
      }
      fail_allocations = false;
      require(refused, "actual settlement allocation refusal occurred");
      const auto actual = context.pending_proposal();
      std::cerr << "actual pending preserved=" << (actual == owned) << '\n';
      require(
          actual == owned,
          "unaccepted native settlement must preserve exact owned pending proposal");
    }
  }
  lane.reset();
  runtime.reset();
  if (pending)
    return;
  const auto runs = unwrap(discover_raw_runs(directory.path.string()));
  require(runs.size() == 1, "one actual native raw run");
  auto reader =
      unwrap(RawRunReader::open((directory.path / "raw" / runs.front().name).string()));
  std::vector<std::byte> bytes(1024 * 1024);
  unsigned requests = 0, records = 0;
  while (auto event = unwrap(reader->read(bytes))) {
    ++records;
    std::uint32_t site = 0;
    require(event->raw.size() >= raw_envelope_size, "actual native record envelope");
    ::memcpy(&site, event->raw.data() + 4, sizeof(site));
    if (site == 1800)
      ++requests;
  }
  std::cerr << "actual Context entries=1 raw records=" << records
            << " owner1800=" << requests << '\n';
  require(requests != 0,
          "actual selected Context operation must emit1800 owner request");
}

void measure_pin(RetainedState &root) {
  // Prime native unwind setup before observing any actual history allocations.
  (void)actual_pin_stack();
  history_allocation_count = 0;
  measure_history = true;
  try {
    (void)root.history_snapshot();
  } catch (...) {
    measure_history = false;
    throw;
  }
  measure_history = false;
  for (std::size_t i = 0; i < history_allocation_count; ++i) {
    Dl_info info{};
    if (history_allocations[i].pin_stack &&
        ::dladdr(history_allocations[i].caller, &info) && info.dli_sname &&
        std::string_view{info.dli_sname}.find("archive_reader") !=
            std::string_view::npos) {
      history_cut = history_allocations[i].caller;
      history_cut_hit = false;
      std::cerr << "actual archive_reader closure allocation="
                << history_allocations[i].size << '\n';
      return;
    }
  }
  for (std::size_t i = 0; i < history_allocation_count; ++i) {
    if (history_allocations[i].caller && history_allocations[i].pin_stack) {
      history_cut = history_allocations[i].caller;
      history_cut_hit = false;
      std::cerr << "actual history pin stack allocation=" << history_allocations[i].size
                << '\n';
      return;
    }
  }
  for (std::size_t i = 0; i < history_allocation_count; ++i) {
    Dl_info info{};
    (void)::dladdr(history_allocations[i].caller, &info);
    std::cerr << "history allocation=" << history_allocations[i].size
              << " caller=" << (info.dli_sname ? info.dli_sname : "unresolved") << '\n';
  }
  throw std::runtime_error(
      "actual measured history allocation must have a pinning stack");
}
struct NativeFault {
  bool write = false, replace = false, read = false;
  unsigned write_hits = 0, replace_hits = 0, read_hits = 0;
};
class FaultFile final : public JournalFile {
public:
  FaultFile(std::unique_ptr<JournalFile> file, NativeFault &fault, bool cold = false)
      : file_(std::move(file)), fault_(fault), cold_(cold) {}
  Result<std::size_t> read_at(std::uint64_t offset, MutableByteView bytes) override {
    if (fault_.read && cold_) {
      fault_.read = false;
      ++fault_.read_hits;
      return Result<std::size_t>::failure({ErrorCode::io, EIO});
    }
    return file_->read_at(offset, bytes);
  }
  Result<std::size_t> write_at(std::uint64_t offset, ByteView bytes) override {
    if (fault_.write) {
      fault_.write = false;
      ++fault_.write_hits;
      return Result<std::size_t>::failure({ErrorCode::io, EIO});
    }
    return file_->write_at(offset, bytes);
  }
  Result<std::uint64_t> extent() override { return file_->extent(); }
  Result<void> synchronize(SyncStrength strength) override {
    return file_->synchronize(strength);
  }
  Result<void> lock_writer() override { return file_->lock_writer(); }
  void release_writer() noexcept override { file_->release_writer(); }

private:
  std::unique_ptr<JournalFile> file_;
  NativeFault &fault_;
  bool cold_;
};
class FaultDirectory final : public JournalDirectory {
public:
  FaultDirectory(std::unique_ptr<JournalDirectory> directory, NativeFault &fault)
      : directory_(std::move(directory)), fault_(fault) {}
  Result<std::unique_ptr<JournalFile>>
  create_exclusive(std::string_view name) override {
    auto result = directory_->create_exclusive(name);
    if (!result.has_value())
      return result;
    return Result<std::unique_ptr<JournalFile>>::success(
        std::make_unique<FaultFile>(std::move(result).value(), fault_, name == "cold"));
  }
  Result<std::unique_ptr<JournalFile>> open_existing(std::string_view name,
                                                     FileAccess access) override {
    auto result = directory_->open_existing(name, access);
    if (!result.has_value())
      return result;
    return Result<std::unique_ptr<JournalFile>>::success(
        std::make_unique<FaultFile>(std::move(result).value(), fault_, name == "cold"));
  }
  Result<void> replace_file(std::string_view from, std::string_view to) override {
    if (fault_.replace) {
      fault_.replace = false;
      ++fault_.replace_hits;
      return Result<void>::failure({ErrorCode::io, EIO});
    }
    return directory_->replace_file(from, to);
  }
  Result<void> synchronize_directory(SyncStrength strength) override {
    return directory_->synchronize_directory(strength);
  }

private:
  std::unique_ptr<JournalDirectory> directory_;
  NativeFault &fault_;
};
Value proposal(ContextStore &context) {
  Value::Array ids;
  const auto view = context.view();
  for (const auto &entry : field(view, "entries").array())
    ids.emplace_back(string_field(entry, "id"));
  return Value::object({{"base", Value{context.head()}},
                        {"mode", Value{"select"}},
                        {"reason", Value{std::string{"r\0z", 3}}},
                        {"source", Value{"native.owner"}},
                        {"ids", Value{std::move(ids)}}});
}
struct Resume {
  std::array<pid_t, 2> pids{-1, -1};
  ~Resume() {
    for (auto pid : pids)
      if (pid > 0)
        (void)::kill(pid, SIGCONT);
  }
};
bool stopped(pid_t pid) {
#if defined(__APPLE__)
  proc_bsdinfo info{};
  return ::proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) ==
             sizeof(info) &&
         info.pbi_status == SSTOP;
#else
  std::ifstream input{"/proc/" + std::to_string(pid) + "/status"};
  std::string line;
  while (std::getline(input, line))
    if (line.starts_with("State:"))
      return line.find('T') != std::string::npos;
  return false;
#endif
}
void stop_holder(pid_t pid) {
  require(::kill(pid, SIGSTOP) == 0, "stop actual recorder holder");
  for (unsigned i = 0; i < 100000; ++i) {
    if (stopped(pid))
      return;
    std::this_thread::yield();
  }
  throw std::runtime_error("actual holder did not stop");
}
struct Socket {
  std::array<int, 2> fds = unwrap(raw_socket_pair());
  ~Socket() {
    for (auto fd : fds)
      (void)::close(fd);
  }
};
void fill_control() {
  for (auto amount : {std::uint64_t{1024}, std::uint64_t{16}})
    for (;;) {
      auto granted = raw_try_claim_control(amount);
      if (!granted.has_value()) {
        require(granted.error().code == ErrorCode::capacity, "physical capacity only");
        break;
      }
      auto credit = std::move(granted).value();
      const std::array<std::byte, 1008> padding{};
      const ByteView bytes{padding};
      const auto parts = amount == 16 ? std::span<const ByteView>{}
                                      : std::span{&bytes, std::size_t{1}};
      (void)unwrap(raw_emit(9290, parts, RawPartition::control, &credit));
    }
  require(!raw_try_claim_control(16).has_value(), "actual ordinary CONTROL full");
}
template <class T> T take(ByteView &body) {
  require(body.size() >= sizeof(T), "literal native field present");
  T value{};
  std::memcpy(&value, body.data(), sizeof(T));
  body = body.subspan(sizeof(T));
  return value;
}
RawEventRef take_ref(ByteView &body) {
  RawEventRef value{};
  require(body.size() >= 16, "actual lane bytes");
  std::memcpy(value.lane.data(), body.data(), 16);
  body = body.subspan(16);
  value.ordinal = take<std::uint64_t>(body);
  return value;
}
template <class T> T take_identity(ByteView &body) {
  IdentityBytes bytes{};
  require(body.size() >= bytes.size(), "actual native identity width");
  std::memcpy(bytes.data(), body.data(), bytes.size());
  body = body.subspan(bytes.size());
  return unwrap(T::from_bytes(bytes));
}
JournalCursor take_cursor(ByteView &body) {
  return {take_identity<AuditStreamId>(body), take<std::uint64_t>(body),
          take<std::uint64_t>(body)};
}
bool same_cursor(JournalCursor a, JournalCursor b) {
  return a.journal == b.journal && a.sequence == b.sequence &&
         a.end_offset == b.end_offset;
}
struct Record {
  IdentityBytes lane;
  RawPartition partition;
  std::uint64_t ordinal;
  std::uint32_t site;
  std::vector<std::byte> body;
};
std::vector<Record> read_records(const Directory &directory) {
  const auto runs = unwrap(discover_raw_runs(directory.path.string()));
  require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
          "one native closed raw run");
  auto reader =
      unwrap(RawRunReader::open((directory.path / "raw" / runs.front().name).string()));
  std::vector<Record> records;
  std::array<std::byte, 1048576> output{};
  while (auto read = unwrap(reader->read(output))) {
    auto bytes = read->raw;
    while (!bytes.empty()) {
      const auto length = take<std::uint32_t>(bytes), site = take<std::uint32_t>(bytes);
      const auto ordinal = take<std::uint64_t>(bytes);
      require(length >= 16 && length - 16 <= bytes.size(), "complete actual envelope");
      const auto body = bytes.first(length - 16);
      records.push_back({read->origin.lane,
                         read->origin.partition,
                         ordinal,
                         site,
                         {body.begin(), body.end()}});
      bytes = bytes.subspan(length - 16);
    }
  }
  std::sort(records.begin(), records.end(),
            [](const auto &a, const auto &b) { return a.ordinal < b.ordinal; });
  if (std::any_of(records.begin(), records.end(),
                  [](const auto &r) { return r.partition == RawPartition::failure; })) {
    for (const auto &r : records)
      if (r.ordinal + 20 >= records.back().ordinal) {
        std::cerr << "last actual ordinal=" << r.ordinal << " site=" << r.site
                  << " body=" << r.body.size();
        if (r.partition == RawPartition::failure) {
          auto bytes = ByteView{r.body};
          std::cerr << " error=" << error_name(take<ErrorCode>(bytes)) << ":"
                    << take<std::int64_t>(bytes);
        }
        std::cerr << '\n';
      }
  }
  for (std::size_t i = 0; i < records.size(); ++i)
    require(records[i].lane == records.front().lane && records[i].ordinal == i + 1 &&
                records[i].site != 8000 &&
                records[i].partition != RawPartition::failure,
            "sole actual producer ordinal order, no loss or fence");
  return records;
}
void verify_pairs(const std::vector<Record> &records) {
  const std::map<std::uint32_t, std::uint32_t> pairs{
      {1800, 1815}, {1801, 1802}, {1804, 1805}, {1811, 1812},
      {1813, 1814}, {1817, 1818}, {1819, 1820}};
  std::map<std::uint64_t, unsigned> terminals;
  for (const auto &record : records) {
    if (record.site < 1800 || record.site > 1827 || record.site == 1800)
      continue;
    auto bytes = ByteView{record.body};
    const auto ref = take_ref(bytes);
    require(ref.lane == record.lane && ref.ordinal != 0 && ref.ordinal < record.ordinal,
            "actual earlier originating Ref");
    const auto found =
        std::find_if(records.begin(), records.end(), [&](const auto &request) {
          return request.ordinal == ref.ordinal;
        });
    require(found != records.end(), "actual request exists on disk");
    if (std::any_of(pairs.begin(), pairs.end(),
                    [&](const auto &p) { return p.second == record.site; })) {
      const auto expected = pairs.find(found->site);
      require(expected != pairs.end() && expected->second == record.site,
              "terminal names actual matching request");
      ++terminals[ref.ordinal];
    }
  }
  for (const auto &record : records)
    if (pairs.contains(record.site))
      require(terminals[record.ordinal] == 1,
              "exactly one success/failure terminal for each acquired owner request");
  for (const auto &record : records)
    if (record.site == 1800) {
      auto bytes = ByteView{record.body};
      const auto action = take<RawContextAction>(bytes);
      const auto parent = take<bool>(bytes);
      if (parent) {
        const auto origin = take_ref(bytes);
        require(origin.lane == record.lane && origin.ordinal < record.ordinal &&
                    std::any_of(
                        records.begin(), records.end(),
                        [&](const auto &r) { return r.ordinal == origin.ordinal; }),
                "actual child owner parent exists before child request");
      }
      if (action != RawContextAction::validate_seed) {
        const auto head_length = take<std::size_t>(bytes);
        require((head_length == 0 || head_length == 32) && bytes.size() >= head_length,
                "actual revision native text width");
        bytes = bytes.subspan(head_length);
        for (unsigned i = 0; i < 3; ++i)
          (void)take<std::size_t>(bytes);
        for (unsigned i = 0; i < 3; ++i)
          (void)take<bool>(bytes);
      }
      require(bytes.empty(),
              "owner native request contains no invented state or padding");
    }
}

struct SourceExpected {
  RecordReference record;
  ApplicationRecordId application;
  ApplicationChannel channel;
  std::size_t length;
};
struct Oracle {
  struct FailedPublication {
    ApplicationRecordId application;
    ApplicationChannel channel;
    JournalCursor expected;
    Error error;
  };
  std::vector<SourceExpected> sources;
  std::vector<FailedPublication> failed_publications;
  std::map<std::pair<IdentityBytes, std::uint64_t>, JournalCursor> commits;
  std::vector<std::pair<bool, bool>> finishes;
  std::vector<std::vector<std::byte>> resident;
  std::vector<std::vector<std::byte>> pages;
  void collect(RetainedState &root) {
    const auto cursor = root.cursor();
    auto disk = unwrap(
        root.read_original_range(0, static_cast<std::size_t>(cursor.end_offset)));
    const auto header =
        unwrap(decode_journal_header(ByteView{disk.bytes}.first(journal_header_size)));
    std::size_t offset = journal_header_size;
    std::vector<std::uint64_t> batch;
    while (offset < disk.bytes.size()) {
      auto length = ByteView{disk.bytes}.subspan(offset + 24, 4);
      const auto payload = take<std::uint32_t>(length);
      const auto size = journal_frame_header_size + payload;
      const auto frame = unwrap(decode_journal_frame(
          ByteView{disk.bytes}.subspan(offset, size), header.limits));
      offset += size;
      if (frame.kind == FrameKind::commit) {
        const JournalCursor accepted{cursor.journal, frame.sequence, offset};
        for (auto sequence : batch)
          commits.emplace(std::pair{cursor.journal.bytes(), sequence}, accepted);
        batch.clear();
      } else
        batch.push_back(frame.sequence);
    }
    require(offset == cursor.end_offset && batch.empty(),
            "independent native accepted prefix ends at an actual commit");
    for (std::size_t i = 0; i < root.fact_count(); ++i) {
      const auto fact = unwrap(root.fact(i));
      const auto *application = std::get_if<ApplicationRecordEvent>(&fact.event.body);
      if (!application)
        continue;
      sources.push_back({fact.record, application->identity, application->channel,
                         application->payload.size()});
      if (application->channel == ApplicationChannel::context) {
        const auto packet = unwrap(read_packet(application->payload));
        if (const auto *fresh = packet.find("originals"))
          for (const auto &entry : fresh->array())
            resident.push_back(unwrap(encode_packet(entry)));
      }
    }
  }
  void collect_failed(RetainedState &root, Error error) {
    const auto &owned = root.pending_proposal();
    if (!owned)
      throw std::runtime_error(
          "actual unaccepted proposal remains in native owner custody");
    const auto pending = unwrap(decode_retained_proposal(
        owned->bytes, root.journal_usage().limit.max_file_bytes));
    for (const auto &bytes : pending.events) {
      const auto event =
          unwrap(decode_retained_event(bytes, root.journal_limits().max_payload));
      const auto *application = std::get_if<ApplicationRecordEvent>(&event.body);
      require(application != nullptr, "actual failed fixture application draft");
      failed_publications.push_back(
          {application->identity, application->channel, pending.expected, error});
    }
  }
  void page(const Value &value) {
    const auto &hex = string_field(value, "bytes");
    require(hex.size() % 2 == 0, "native business page hex pairs");
    std::vector<std::byte> bytes;
    auto nibble = [](char c) -> unsigned {
      require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'),
              "business native hex digit");
      return c <= '9' ? static_cast<unsigned>(c - '0')
                      : static_cast<unsigned>(c - 'a' + 10);
    };
    for (std::size_t i = 0; i < hex.size(); i += 2)
      bytes.push_back(
          static_cast<std::byte>((nibble(hex[i]) << 4U) | nibble(hex[i + 1])));
    pages.push_back(std::move(bytes));
  }
  void verify(const std::vector<Record> &records) const {
    unsigned retained = 0, resident_reads = 0, io_reads = 0, lookups = 0,
             pages_seen = 0, callback_failures = 0, allocation_failures = 0,
             accepted_records = 0, accepted_boundaries = 0,
             failed_publications_seen = 0;
    std::size_t finish_index = 0;
    for (const auto &record : records) {
      if (record.site == 1801) {
        auto body = ByteView{record.body};
        (void)take_ref(body);
        const auto application = take_identity<ApplicationRecordId>(body);
        const auto channel = take<ApplicationChannel>(body);
        const auto proposed = take_cursor(body);
        require(body.empty(), "publication request exact native ID/channel/cursor");
        const auto returned =
            std::find_if(records.begin(), records.end(), [&](const auto &candidate) {
              if (candidate.site != 1802)
                return false;
              auto bytes = ByteView{candidate.body};
              return take_ref(bytes).ordinal == record.ordinal;
            });
        require(returned != records.end(), "actual publication return exists");
        auto bytes = ByteView{returned->body};
        (void)take_ref(bytes);
        const auto branch = take<std::uint8_t>(bytes);
        const auto kind = take<std::uint8_t>(bytes);
        if (branch == 0) {
          const auto fact =
              std::find_if(sources.begin(), sources.end(), [&](const auto &source) {
                return source.application == application && source.channel == channel &&
                       source.record.journal == proposed.journal;
              });
          require(fact != sources.end(),
                  "publication identity/channel belong to actual retained fact");
          require(fact->record.sequence > proposed.sequence,
                  "actual publication follows requested native prefix");
          require(
              (proposed.sequence == 0 && proposed.end_offset == journal_header_size) ||
                  std::any_of(commits.begin(), commits.end(),
                              [&](const auto &commit) {
                                return same_cursor(commit.second, proposed);
                              }),
              "requested prefix equals an independent native commit cursor");
          if (kind == 1) {
            const RecordReference actual{take_identity<AuditStreamId>(bytes),
                                         take<std::uint64_t>(bytes)};
            require(actual == fact->record && bytes.empty(),
                    "publication returns exact independent retained RecordReference");
            ++accepted_records;
          } else {
            require(kind == 2, "successful publication has actual native return kind");
            const auto actual = take_cursor(bytes);
            const auto commit = commits.find(
                std::pair{fact->record.journal.bytes(), fact->record.sequence});
            require(commit != commits.end() && same_cursor(actual, commit->second) &&
                        bytes.empty(),
                    "batch publication returns actual native disk commit boundary");
            ++accepted_boundaries;
          }
        } else {
          require(branch == 1 && kind == 0,
                  "failed publication carries no fabricated record/cursor");
          const Error actual{take<ErrorCode>(bytes), take<std::int64_t>(bytes)};
          const auto failed =
              std::find_if(failed_publications.begin(), failed_publications.end(),
                           [&](const auto &expected) {
                             return expected.application == application &&
                                    expected.channel == channel &&
                                    same_cursor(expected.expected, proposed) &&
                                    expected.error == actual;
                           });
          require(failed != failed_publications.end() && bytes.empty(),
                  "publication EIO names actual held unaccepted ID/channel/prefix");
          ++failed_publications_seen;
        }
      } else if (record.site == 1813) {
        auto body = ByteView{record.body};
        const auto request_parent = take_ref(body);
        (void)request_parent;
        (void)take<std::uint8_t>(body);
        const auto kind = take<std::uint8_t>(body);
        const auto length = take<std::size_t>(body);
        const auto has_record = take<bool>(body);
        std::optional<RecordReference> physical;
        if (has_record) {
          IdentityBytes journal{};
          require(body.size() >= 16, "native journal ID bytes");
          std::memcpy(journal.data(), body.data(), 16);
          body = body.subspan(16);
          physical.emplace(unwrap(AuditStreamId::from_bytes(journal)),
                           take<std::uint64_t>(body));
        }
        const auto has_application = take<bool>(body);
        std::optional<ApplicationRecordId> app;
        if (has_application) {
          IdentityBytes id{};
          require(body.size() >= 16, "native application ID bytes");
          std::memcpy(id.data(), body.data(), 16);
          body = body.subspan(16);
          app = unwrap(ApplicationRecordId::from_bytes(id));
        }
        const auto channel = take<ApplicationChannel>(body);
        (void)take<std::size_t>(body);
        require(body.empty(), "1813 exact native field widths");
        if (physical) {
          const auto source =
              std::find_if(sources.begin(), sources.end(),
                           [&](const auto &e) { return e.record == *physical; });
          require(source != sources.end() && app && source->application == *app &&
                      source->channel == channel,
                  "original acquisition names actual retained fact identity/channel");
          if (kind == 1)
            require(source->length == length,
                    "actual retained input size, no fake resident source");
        }
        const auto returned =
            std::find_if(records.begin(), records.end(), [&](const auto &r) {
              if (r.site != 1814)
                return false;
              auto bytes = ByteView{r.body};
              return take_ref(bytes).ordinal == record.ordinal;
            });
        require(returned != records.end(), "actual acquired input has distinct return");
        auto bytes = ByteView{returned->body};
        (void)take_ref(bytes);
        const auto branch = take<std::uint8_t>(bytes);
        if (branch == 0) {
          require(take<std::size_t>(bytes) == length,
                  "input returns actual acquired byte length");
          if (kind == 0) {
            require(returned->partition == RawPartition::data &&
                        std::any_of(resident.begin(), resident.end(),
                                    [&](const auto &e) {
                                      return ByteView{e}.size() == bytes.size() &&
                                             std::equal(e.begin(), e.end(),
                                                        bytes.begin());
                                    }),
                    "literal resident original bytes emitted before decode");
            ++resident_reads;
          } else {
            require(
                returned->partition == RawPartition::control && physical &&
                    bytes.empty(),
                "retained original uses actual locator without another payload copy");
            ++retained;
          }
        } else {
          const auto code = take<ErrorCode>(bytes);
          const auto detail = take<std::int64_t>(bytes);
          require(code == ErrorCode::io && detail == EIO && bytes.empty(),
                  "selected native input EIO preserved exactly before decode");
          ++io_reads;
        }
      } else if (record.site == 1820) {
        auto body = ByteView{record.body};
        const auto request = take_ref(body);
        const auto branch = take<std::uint8_t>(body);
        if (branch == 0) {
          require(body.size() == 26, "actual lookup record24/channel2");
          const auto found =
              std::find_if(records.begin(), records.end(),
                           [&](const auto &r) { return r.ordinal == request.ordinal; });
          require(found != records.end() && found->site == 1819 &&
                      found->body.size() == 75,
                  "lookup request R24/family1/kind2/id16/cursor32 =75 body/91 record "
                  "bytes");
          ++lookups;
        }
      } else if (record.site == 1809) {
        auto bytes = ByteView{record.body};
        (void)take_ref(bytes);
        const auto length = take<std::size_t>(bytes);
        require(length == bytes.size() && record.partition == RawPartition::data &&
                    std::any_of(pages.begin(), pages.end(),
                                [&](const auto &e) {
                                  return e.size() == bytes.size() &&
                                         std::equal(e.begin(), e.end(), bytes.begin());
                                }),
                "existing exact business page emitted before hex presentation");
        ++pages_seen;
      } else if (record.site == 1805) {
        auto bytes = ByteView{record.body};
        (void)take_ref(bytes);
        const auto branch = take<std::uint8_t>(bytes);
        if (branch == 1) {
          const auto code = take<ErrorCode>(bytes);
          const auto detail = take<std::int64_t>(bytes);
          require(code == ErrorCode::capacity && detail == 71 && bytes.empty(),
                  "actual protection failure code/detail");
          ++callback_failures;
        }
      } else if (record.site == 1827) {
        auto body = ByteView{record.body};
        const auto request = take_ref(body);
        const auto success = take<bool>(body);
        const auto boundary = take<bool>(body);
        require(body.empty(), "finish actual two bool widths");
        const auto owner =
            std::find_if(records.begin(), records.end(),
                         [&](const auto &r) { return r.ordinal == request.ordinal; });
        require(owner != records.end() && owner->site == 1800 &&
                    owner->body.front() ==
                        static_cast<std::byte>(RawContextAction::finish),
                "finish parameters name actual finish owner");
        require(
            finish_index < finishes.size() &&
                finishes[finish_index] == std::pair{success, boundary},
            "finish arguments equal actual caller sequence including true/boundary");
        ++finish_index;
      } else if (record.site == 1815) {
        auto bytes = ByteView{record.body};
        (void)take_ref(bytes);
        const auto branch = take<std::uint8_t>(bytes);
        (void)take<std::uint8_t>(bytes);
        if (branch == 4) {
          require(bytes.empty(), "actual allocation terminal has only branch/stage");
          ++allocation_failures;
        }
      }
    }
    require(retained > 0 && resident_reads > 0 && io_reads == 1 && lookups >= 2 &&
                pages_seen == pages.size() && callback_failures == 1 &&
                allocation_failures >= 4 && accepted_records > 0 &&
                accepted_boundaries == 2 &&
                failed_publications_seen == failed_publications.size() &&
                failed_publications_seen == 2 && finish_index == finishes.size(),
            "all selected native success/failure/provenance cuts reached");
  }
};
void owner() {
  Directory directory;
  NativeFault fault;
  Oracle oracle;
  auto runtime =
      unwrap(RawRuntime::open(directory.path.string(), identity<EnvironmentId>(73),
                              {{1048576, 262144, 4096}, 4096}));
  auto lane = unwrap(runtime->prepare_thread());
  std::exception_ptr failure;
  try {
    {
      RawThreadBinding binding{lane};
      const JournalHeader header{identity<EnvironmentId>(73),
                                 identity<AuditStreamId>(74),
                                 3,
                                 {1048576, 4194304},
                                 std::nullopt};
      auto native = [&] {
        return std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(directory.path.string())));
      };
      auto root = unwrap(
          RetainedState::create(std::make_unique<FaultDirectory>(native(), fault),
                                "audit", header, {67108864, 8192}));
      AuditLog log{*root};
      ContextStore context{log};
      const auto item = Value::object(
          {{"role", Value{"assistant"}}, {"content", Value{std::string{"a\0b", 3}}}});
      stage = "native append, views, projections, resident original";
      context.append({item}, "native.owner");
      const auto first_view = context.view();
      require(context.items() == Value::Array{item}, "exact original business bytes");
      (void)context.stats();
      (void)context.originals();
      (void)context.cancellation_budget(field(first_view, "entries").array(), Value{});
      const auto entry =
          string_field(field(first_view, "entries").array().front(), "id");
      const auto query = Value::object({{"kind", Value{"originals"}},
                                        {"entry", Value{entry}},
                                        {"limit", Value{Number{65536}}}});
      const auto page = context.inspect(query);
      oracle.page(page);
      require(field(page, "next") == field(page, "total_bytes"),
              "native inspection page completes");
      stage = "actual callback failure before publication";
      const auto before_callback = root->cursor();
      const auto prior = context.view();
      context.protect = [](const Value::Array &, const Value &,
                           const RawEventRef &request) {
        require(request.ordinal != 0, "actual1804 passed to native callback");
        throw Error{ErrorCode::capacity, 71};
      };
      bool callback_failed = false;
      try {
        context.append({item}, "protect.failure");
      } catch (const Error &error) {
        callback_failed = error == Error{ErrorCode::capacity, 71};
      }
      context.protect = {};
      require(callback_failed && context.view() == prior &&
                  root->cursor().sequence == before_callback.sequence &&
                  root->cursor().end_offset == before_callback.end_offset,
              "real protection refusal preserves state and cursor");
      stage = "postaccepted append history pin allocation";
      measure_pin(*root);
      const auto before_append = root->cursor();
      bool pin_failed = false;
      try {
        context.append({item}, "accepted.pin");
      } catch (const std::bad_alloc &) {
        pin_failed = true;
      }
      require(pin_failed && history_cut_hit &&
                  root->cursor().sequence > before_append.sequence &&
                  context.items().size() == 2,
              "accepted append remains active after actual archive closure allocation "
              "failure");
      (void)context.originals();
      stage = "postaccepted staged batch history pin allocation";
      context.begin_workflow();
      const auto planned = proposal(context);
      (void)context.manage(planned);
      const auto staged_head = context.head();
      const auto before_settle = root->cursor();
      const auto program = Value::object({{"generation", Value{"native.fixture"}}});
      WorkflowAcceptance acceptance;
      oracle.finishes.emplace_back(true, true);
      measure_pin(*root);
      pin_failed = false;
      try {
        (void)context.finish_workflow(true, program, {}, &acceptance);
      } catch (const std::bad_alloc &) {
        pin_failed = true;
      }
      require(pin_failed && history_cut_hit && acceptance.context_settlement_accepted &&
                  acceptance.boundary_program_accepted &&
                  context.pending_proposal() == Value{} &&
                  context.head() != staged_head &&
                  root->cursor().sequence > before_settle.sequence,
              "actual batch acceptance flags survive history failure and clear "
              "accepted pending");
      const auto accepted_cursor = root->cursor();
      oracle.finishes.emplace_back(true, false);
      (void)context.finish_workflow(true);
      require(root->cursor().sequence == accepted_cursor.sequence &&
                  root->cursor().end_offset == accepted_cursor.end_offset,
              "accepted pending cannot be appended again on retry");
      stage = "postaccepted program only history pin allocation";
      oracle.finishes.emplace_back(true, true);
      measure_pin(*root);
      pin_failed = false;
      acceptance = {};
      try {
        (void)context.finish_workflow(true, program, {}, &acceptance);
      } catch (const std::bad_alloc &) {
        pin_failed = true;
      }
      require(pin_failed && history_cut_hit &&
                  !acceptance.context_settlement_accepted &&
                  acceptance.boundary_program_accepted,
              "program acceptance distinct from Context settlement");

      stage = "accepted rejection history pin fails before separate program write";
      context.begin_workflow();
      (void)context.manage(proposal(context));
      const auto competing = context.view();
      require(field(context.edit(competing), "accepted") == Value{true},
              "actual competing context edit creates settlement conflict");
      const auto conflict_head = context.head();
      oracle.finishes.emplace_back(true, true);
      measure_pin(*root);
      pin_failed = false;
      acceptance = {};
      try {
        (void)context.finish_workflow(true, program, {}, &acceptance);
      } catch (const std::bad_alloc &) {
        pin_failed = true;
      }
      require(pin_failed && history_cut_hit && acceptance.context_settlement_accepted &&
                  !acceptance.boundary_program_accepted &&
                  context.pending_proposal() == Value{} &&
                  context.head() == conflict_head,
              "accepted rejection retires pending without claiming unattempted program "
              "activation");
      const auto rejected_cursor = root->cursor();
      oracle.finishes.emplace_back(true, false);
      (void)context.finish_workflow(true);
      require(root->cursor().sequence == rejected_cursor.sequence,
              "accepted rejection cannot be written again");
      stage = "ordinary save native replace failure";
      fault.replace = true;
      const auto saved = context.checkpoint();
      require(!saved.has_value() && fault.replace_hits == 1 &&
                  saved.error() == Error{ErrorCode::io, EIO},
              "actual native save error remains Result and terminal");

      stage = "accepted saved publication followed by history pin allocation";
      const auto saved_before = root->saved_state()->boundary.sequence;
      measure_pin(*root);
      const auto pinned_save = context.checkpoint();
      require(!pinned_save.has_value() &&
                  pinned_save.error() == Error{ErrorCode::allocation} &&
                  history_cut_hit &&
                  root->saved_state()->boundary.sequence == root->cursor().sequence &&
                  root->saved_state()->boundary.sequence > saved_before,
              "actual saved publication success survives subsequent history allocation "
              "refusal");
      stage = "cancelled full ordinary CONTROL, held stop bank";
      const auto call = Value::object({{"type", Value{"function_call"}},
                                       {"call_id", Value{"owned.call"}},
                                       {"name", Value{"owned"}},
                                       {"arguments", Value{"{}"}}});
      context.begin_workflow();
      require(field(context.manage(proposal(context)), "staged") == Value{true},
              "actual stop proposal staged before open tool batch");
      context.append({call}, "native.call");

      require(root->saved_state() != nullptr, "actual previous saved boundary exists");
      const auto saved_boundary = root->saved_state()->boundary.sequence;
      const auto tick = Value::object({{"native", Value{true}}});
      while (root->cursor().sequence - saved_boundary < 64)
        (void)log.record(ApplicationChannel::log, tick);
      const auto issued_limit = root->issuer_counter();
      bool exhausted = false;
      for (unsigned i = 0; i < 1024; ++i) {
        const auto issued = log.issue();
        std::uint64_t counter = 0;
        std::memcpy(&counter, issued.bytes().data() + 8, 8);
        if (counter == issued_limit) {
          exhausted = true;
          break;
        }
      }
      require(exhausted && root->issuer_counter() == issued_limit,
              "actual durable issuer reservoir exhausted without fresh reservation");
      auto stop_bank = unwrap(raw_claim_control(9198));
      auto semantic = unwrap(stop_bank.split(2286));
      auto regions = unwrap(stop_bank.split(6912));
      oracle.finishes.emplace_back(false, false);
      {
        const auto holders = unwrap(runtime->status());
        Resume resume{{holders.recorder, holders.supervisor}};
        stop_holder(holders.recorder);
        stop_holder(holders.supervisor);
        fill_control();
        Socket cancellation;
        require(::shutdown(cancellation.fds[1], SHUT_WR) == 0,
                "actual sticky cancellation EOF");
        RawContextOrigin stop_origin{
            {}, &semantic, &regions, RawContextOrigin::Input::no_new};
        {
          RawWaitScope wait{{{cancellation.fds[0], -1}, {}}};
          auto fresh = raw_claim_control(16);
          require(!fresh.has_value() && fresh.error().code == ErrorCode::interrupted,
                  "actual fresh admission interrupted");
          (void)context.finish_workflow(false, Value{}, stop_origin, &acceptance);
          const auto stopping = context.view(stop_origin);
          const auto outputs =
              ContextStore::stop_outputs(field(stopping, "entries").array(), true);
          require(outputs.size() == 1, "one actual unresolved tool output");
          context.append(outputs, "stop.linkage", stop_origin);
          // Independent native layout: BEGIN/END/outcome/error and one journal
          // observation total276; this actual one-event append adds52 bytes.
          constexpr std::uint64_t one_event_region = 276 + 52;
          require(acceptance.context_settlement_accepted &&
                      !acceptance.boundary_program_accepted &&
                      root->issuer_counter() > issued_limit &&
                      regions.remaining() == 6912 - 3 * one_event_region,
                  "actual canceled rejection and issuer refill use original held "
                  "region bank");
          require(context.checkpoint_error() ==
                          std::optional<Error>{Error{ErrorCode::capacity}} &&
                      root->saved_state()->boundary.sequence == saved_boundary,
                  "required optional checkpoint refuses unpaid input as held Result "
                  "without undoing accepted stop append");
        }
      }
      require(context.pending_proposal() == Value{} && context.items().size() == 4 &&
                  string_field(context.items().back(), "type") ==
                      "function_call_output",
              "actual stopped workflow retained exact output and settled pending");
      stage = "native write failure preserves unaccepted staged proposal";
      const JournalHeader failed_header{header.environment, identity<AuditStreamId>(75),
                                        3, header.limits, std::nullopt};
      auto failed_root = unwrap(
          RetainedState::create(std::make_unique<FaultDirectory>(native(), fault),
                                "fault", failed_header, {33554432, 4096}));
      AuditLog failed_log{*failed_root};
      ContextStore failed_context{failed_log};
      failed_context.append({item}, "native.fault");
      failed_context.begin_workflow();
      (void)failed_context.manage(proposal(failed_context));
      const auto owned = failed_context.pending_proposal();
      const auto failed_cursor = failed_root->cursor();
      acceptance = {};
      oracle.finishes.emplace_back(true, true);
      fault.write = true;
      bool write_failed = false;
      try {
        (void)failed_context.finish_workflow(true, program, {}, &acceptance);
      } catch (const Error &error) {
        write_failed = error == Error{ErrorCode::io, EIO};
      }
      require(write_failed && fault.write_hits == 1 &&
                  !acceptance.context_settlement_accepted &&
                  !acceptance.boundary_program_accepted &&
                  failed_context.pending_proposal() == owned &&
                  failed_root->cursor().sequence == failed_cursor.sequence,
              "native EIO before acceptance retains exact pending and false flags");
      oracle.collect_failed(*failed_root, {ErrorCode::io, EIO});
      stage = "saved reconstruction and native archive original lookup/read";
      const JournalHeader cold_header{header.environment, identity<AuditStreamId>(76),
                                      4, header.limits, std::nullopt};
      stage = "cold root create";
      auto cold_root = unwrap(
          RetainedState::create(std::make_unique<FaultDirectory>(native(), fault),
                                "cold", cold_header, {33554432, 4096}));
      std::string removed;
      Value cold_view;
      {
        AuditLog cold_log{*cold_root};
        stage = "cold initial Context";
        ContextStore cold_context{cold_log};
        stage = "cold initial append";
        cold_context.append({item, item}, "cold.originals");
        const auto original_view = cold_context.view();
        removed = string_field(field(original_view, "entries").array().front(), "id");
        const auto selected = Value::object(
            {{"base", Value{cold_context.head()}},
             {"entries",
              Value{Value::Array{field(original_view, "entries").array().back()}}}});
        require(field(cold_context.edit(selected), "accepted") == Value{true},
                "native edit archives first original");
        stage = "cold selected checkpoint";
        require(cold_context.checkpoint().has_value(),
                "native saved accelerator for cold replay");
        cold_context.append({item}, "cold.tail");
        cold_view = cold_context.view();
      }
      cold_root.reset();
      stage = "cold root open";
      cold_root =
          unwrap(RetainedState::open(std::make_unique<FaultDirectory>(native(), fault),
                                     "cold", cold_header, {33554432, 4096}));
      stage = "cold confirm recovery";
      unwrap(cold_root->confirm_recovery());
      struct Empty final : CustodyVerifier {
        Result<void> verify(std::span<const AttemptState> attempts) override {
          return attempts.empty()
                     ? Result<void>::success()
                     : Result<void>::failure({ErrorCode::external_unknown});
        }
      } verifier;
      stage = "cold reconcile";
      unwrap(cold_root->reconcile(verifier));
      stage = "native cold reconstruction input EIO before decode";
      AuditLog cold_log{*cold_root};
      fault.read = true;
      bool reconstruction_failed = false;
      try {
        ContextStore failed_reconstruction{cold_log};
      } catch (const Error &error) {
        reconstruction_failed = error == Error{ErrorCode::io, EIO};
      }
      require(reconstruction_failed && fault.read_hits == 1,
              "actual cold replay source acquisition failure recorded before decode");
      stage = "cold saved constructor";
      ContextStore cold_context{cold_log};
      require(cold_context.view() == cold_view,
              "saved replay recovers exact accepted view");
      const auto cold_query = Value::object({{"kind", Value{"originals"}},
                                             {"entry", Value{removed}},
                                             {"limit", Value{Number{65536}}}});
      stage = "cold archived original read";
      const auto cold_page = cold_context.inspect(cold_query);
      oracle.page(cold_page);
      require(field(cold_page, "next") == field(cold_page, "total_bytes"),
              "actual cold original retained page");
      stage = "cold restore before native source fault";
      cold_context.restore(removed);
      require(cold_context.items() == Value::Array{item, item, item},
              "native cold restore recovers exact original bytes/order");
      const auto restored_view = cold_context.view();
      fault.read = true;
      bool read_failed = false;
      try {
        (void)cold_context.inspect(cold_query);
      } catch (const Error &error) {
        read_failed = error == Error{ErrorCode::io, EIO};
      }
      require(
          read_failed && fault.read_hits == 2 && cold_context.view() == restored_view &&
              cold_root->state() == JournalWriterState::blocked,
          "actual archive lookup source EIO records failure and fences journal writes");
      bool restore_refused = false;
      try {
        cold_context.restore(removed);
      } catch (const Error &error) {
        restore_refused = error.code == ErrorCode::audit_unavailable;
      }
      require(restore_refused && cold_context.view() == restored_view,
              "blocked native source refuses subsequent publication without changing "
              "accepted view");
      oracle.collect(*root);
      oracle.collect(*failed_root);
      oracle.collect(*cold_root);
    }
  } catch (...) {
    failure = std::current_exception();
  }
  lane.reset();
  runtime.reset();
  const auto records = read_records(directory);
  if (failure)
    std::rethrow_exception(failure);
  verify_pairs(records);
  oracle.verify(records);
  require(std::count_if(records.begin(), records.end(),
                        [](const auto &r) { return r.site == 1823; }) >= 2,
          "managed actual fields emitted");
}
} // namespace
void *operator new(std::size_t size) {
  auto *caller = __builtin_return_address(0);
  if (measure_history && !inspect_history_stack &&
      history_allocation_count < history_allocations.size()) {
    const bool pin_stack = actual_pin_stack();
    history_allocations[history_allocation_count++] = {caller, size, pin_stack};
  }
  if (history_cut != nullptr && caller == history_cut && !inspect_history_stack &&
      actual_pin_stack()) {
    history_cut = nullptr;
    history_cut_hit = true;
    throw std::bad_alloc{};
  }
  if (fail_allocations)
    throw std::bad_alloc{};
  if (auto *pointer = std::malloc(size == 0 ? 1 : size))
    return pointer;
  throw std::bad_alloc{};
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void *pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}
int main(int argc, char **argv) {
  try {
    if (argc == 2)
      baseline(std::string_view{argv[1]} == "--pending-red");
    else {
      baseline(true);
      owner();
    }
    return 0;
  } catch (const Error &error) {
    std::cerr << stage << ": " << error_name(error.code) << ':' << error.detail << '\n';
    return 2;
  } catch (const std::exception &error) {
    fail_allocations = false;
    std::cerr << stage << ": " << error.what() << '\n';
    return 1;
  }
}
