#include "blackbird/context.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <new>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#include <thread>
#include <tuple>
#include <unistd.h>
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc.h>
#endif
using namespace blackbird;
namespace {
thread_local std::size_t allocation_cut = 0;
thread_local bool allocation_hit = false, measure_source = false;
thread_local std::size_t source_allocations = 0;
thread_local std::size_t hex_allocation = 0;
thread_local bool read_cut = false;
thread_local unsigned read_hits = 0;
dev_t read_device = 0;
ino_t read_inode = 0;
thread_local bool fill_after_read = false, fill_on_allocation = false;
thread_local bool ordinary_full = false;
thread_local unsigned successful_reads = 0, successful_headers = 0;
std::atomic<int> cancellation_observer{-1};
std::mutex cancellation_mutex;
std::condition_variable cancellation_changed;
bool cancellation_waiting = false, cancellation_finished = false;
std::atomic<bool> cancellation_initially_unready{false};
std::atomic<ssize_t> cancellation_written{0};
std::atomic<int> cancellation_native_return{-1};
const char *stage = "setup";
void fill_ordinary_control() {
  for (const auto size : {std::uint64_t{1024}, std::uint64_t{16}})
    for (;;) {
      auto acquired = raw_try_claim_control(size);
      if (!acquired.has_value()) {
        if (acquired.error().code != ErrorCode::capacity)
          throw acquired.error();
        break;
      }
      auto credit = std::move(acquired).value();
      const std::array<std::byte, 1008> padding{};
      const ByteView bytes{padding};
      const auto parts =
          size == 16 ? std::span<const ByteView>{} : std::span{&bytes, std::size_t{1}};
      (void)unwrap(raw_emit(9290, parts, RawPartition::control, &credit));
    }
  ordinary_full = !raw_try_claim_control(16).has_value();
}
} // namespace
void *operator new(std::size_t size) {
  if (fill_on_allocation) {
    fill_on_allocation = false;
    fill_ordinary_control();
  }
  if (allocation_cut && allocation_cut == size) {
    allocation_cut = 0;
    allocation_hit = true;
    throw std::bad_alloc{};
  }
  if (measure_source && size == 65536)
    ++source_allocations;
  if (measure_source && size >= 131072 && size <= 131136)
    hex_allocation = size;
  if (auto *memory = std::malloc(size ? size : 1))
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
extern "C" ssize_t pread(int descriptor, void *output, std::size_t size, off_t offset) {
  using Native = ssize_t (*)(int, void *, std::size_t, off_t);
  static auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "pread"));
  if (!native)
    std::abort();
  if (read_cut) {
    struct stat metadata{};
    if (::fstat(descriptor, &metadata) == 0 && metadata.st_dev == read_device &&
        metadata.st_ino == read_inode) {
      read_cut = false;
      ++read_hits;
      errno = EIO;
      return -1;
    }
  }
  const auto result = native(descriptor, output, size, offset);
  const int native_errno = errno;
  if (fill_after_read) {
    struct stat metadata{};
    if (::fstat(descriptor, &metadata) == 0 && metadata.st_dev == read_device &&
        metadata.st_ino == read_inode) {
      if (result > 0) {
        if (offset == 0)
          ++successful_headers;
        else {
          ++successful_reads;
          if (successful_reads == 1)
            fill_on_allocation = true;
        }
      }
    }
  }
  errno = native_errno;
  return result;
}
extern "C" int poll(pollfd *descriptors, nfds_t count, int timeout) {
  using Native = int (*)(pollfd *, nfds_t, int);
  static const auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "poll"));
  if (!native)
    std::abort();
  const auto selected = cancellation_observer.load();
  bool selected_wait = false;
  if (selected >= 0 && timeout != 0 &&
      raw_wait_control().cancellation_fds[0] == selected)
    for (nfds_t i = 0; i < count; ++i)
      selected_wait |= descriptors[i].fd == selected;
  if (selected_wait) {
    const int saved = errno;
    pollfd ready{selected, POLLIN, 0};
    cancellation_initially_unready.store(native(&ready, 1, 0) == 0);
    {
      std::lock_guard lock{cancellation_mutex};
      cancellation_waiting = true;
    }
    cancellation_changed.notify_one();
    errno = saved;
  }
  const auto result = native(descriptors, count, timeout);
  if (selected_wait)
    cancellation_native_return.store(result);
  return result;
}
namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class T> T identity(unsigned char value) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{value};
  return unwrap(T::from_bytes(bytes));
}
Value number(std::size_t value) { return Value{Number{value}}; }
struct Temporary {
  std::filesystem::path path;
  Temporary() {
    char name[] = "/tmp/blackbird-query-raw-XXXXXX";
    require(::mkdtemp(name) != nullptr, "temporary directory");
    path = name;
  }
  ~Temporary() {
    if (std::uncaught_exceptions())
      std::cerr << "failed fixture " << path << '\n';
    else
      std::filesystem::remove_all(path);
  }
};
struct ResumeHolders {
  std::array<pid_t, 2> pids{-1, -1};
  ~ResumeHolders() {
    for (const auto pid : pids)
      if (pid > 0)
        (void)::kill(pid, SIGCONT);
  }
};
bool holder_stopped(pid_t pid) {
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
void confirm_holder_stopped(pid_t pid) {
  for (unsigned attempt = 0; attempt < 100000; ++attempt) {
    if (holder_stopped(pid))
      return;
    std::this_thread::yield();
  }
  throw std::runtime_error{"test-owned recorder holder did not actually stop"};
}
struct Cancellation {
  std::array<int, 2> endpoints{-1, -1};
  std::thread controller;
  Cancellation() {
    require(::pipe(endpoints.data()) == 0, "actual cancellation pipe");
    cancellation_observer.store(endpoints[0]);
    cancellation_initially_unready.store(false);
    cancellation_written.store(0);
    cancellation_native_return.store(-1);
    {
      std::lock_guard lock{cancellation_mutex};
      cancellation_waiting = false;
      cancellation_finished = false;
    }
    try {
      controller = std::thread{[this] {
        std::unique_lock lock{cancellation_mutex};
        cancellation_changed.wait(
            lock, [] { return cancellation_waiting || cancellation_finished; });
        const bool entered = cancellation_waiting;
        lock.unlock();
        if (entered) {
          const char byte = 1;
          cancellation_written.store(::write(endpoints[1], &byte, 1));
        }
      }};
    } catch (...) {
      cancellation_observer.store(-1);
      for (const auto fd : endpoints)
        (void)::close(fd);
      throw;
    }
  }
  void finish() {
    {
      std::lock_guard lock{cancellation_mutex};
      cancellation_finished = true;
    }
    cancellation_changed.notify_one();
    if (controller.joinable())
      controller.join();
  }
  ~Cancellation() {
    finish();
    cancellation_observer.store(-1);
    for (const auto fd : endpoints)
      (void)::close(fd);
  }
};
using Bytes = std::vector<std::byte>;
template <class T> void append(Bytes &bytes, const T &value) {
  const auto raw = std::as_bytes(std::span{&value, 1});
  bytes.insert(bytes.end(), raw.begin(), raw.end());
}
void append(Bytes &bytes, const IdentityBytes &value) {
  bytes.insert(bytes.end(), value.begin(), value.end());
}
template <class... T> Bytes body(const T &...values) {
  Bytes result;
  (append(result, values), ...);
  return result;
}
template <class T> T take(ByteView &bytes) {
  require(bytes.size() >= sizeof(T), "complete native field");
  T result{};
  std::memcpy(&result, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return result;
}
struct Record {
  IdentityBytes lane;
  std::uint64_t ordinal;
  std::uint32_t site;
  RawPartition partition;
  Bytes bytes;
};
template <class F>
std::vector<Record> capture(const Temporary &temporary, unsigned count,
                            EnvironmentId environment, F &&query,
                            bool fixture_fill = false) {
  const auto session = temporary.path / ("capture-" + std::to_string(count));
  std::filesystem::create_directory(session);
  require(::chmod(session.c_str(), 0700) == 0, "private capture directory");
  {
    auto runtime = unwrap(RawRuntime::open(session.string(), environment));
    auto producer = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{producer};
      if constexpr (std::is_invocable_v<F, RawRuntime &>)
        query(*runtime);
      else
        query();
    }
    producer.reset();
  }
  const auto runs = unwrap(discover_raw_runs(session.string()));
  require(runs.size() == 1, "actual recorder published one run");
  const auto &snapshot_value = runs[0].snapshot;
  if (!snapshot_value)
    throw std::runtime_error("actual recorder snapshot exists");
  const auto &snapshot = *snapshot_value;
  require(snapshot.closed, "actual recorder closed one run");
  const auto &descriptions = snapshot.descriptions;
  const std::string_view description{
      reinterpret_cast<const char *>(descriptions.data()), descriptions.size()};
  require(description.find("query2300-2308") != std::string_view::npos,
          "runtime actually installed query native descriptions");
  auto reader = unwrap(RawRunReader::open((session / "raw" / runs[0].name).string()));
  std::array<std::byte, 131072> output{};
  std::vector<Record> result;
  while (const auto read = unwrap(reader->read(output))) {
    auto bytes = read->raw;
    while (!bytes.empty()) {
      const auto length = take<std::uint32_t>(bytes);
      const auto site = take<std::uint32_t>(bytes);
      const auto ordinal = take<std::uint64_t>(bytes);
      require(length >= 16 && length - 16 <= bytes.size(), "complete envelope");
      require((site >= 2300 && site <= 2308) || (fixture_fill && site == 9290),
              "query never fences or emits another owner");
      const auto native = bytes.first(length - 16);
      result.push_back({read->origin.lane,
                        ordinal,
                        site,
                        read->origin.partition,
                        {native.begin(), native.end()}});
      bytes = bytes.subspan(length - 16);
    }
  }
  std::sort(result.begin(), result.end(),
            [](const auto &a, const auto &b) { return a.ordinal < b.ordinal; });
  require(!result.empty(), "actual query emitted records");
  for (std::size_t i = 0; i < result.size(); ++i)
    require(result[i].lane == result[0].lane && result[i].ordinal == i + 1,
            "one actual producer, no duplicate or missing ordinal");
  if (fixture_fill) {
    require(std::any_of(result.begin(), result.end(),
                        [](const auto &record) { return record.site == 9290; }),
            "actual fixture fill is disk-preserved");
    std::erase_if(result, [](const auto &record) { return record.site == 9290; });
  }
  return result;
}
struct Oracle {
  const std::vector<Record> &records;
  std::size_t cursor = 0;
  Bytes ref(std::size_t ordinal) const {
    return body(records.front().lane, static_cast<std::uint64_t>(ordinal));
  }
  void expect(std::uint32_t site, const Bytes &expected,
              RawPartition partition = RawPartition::control) {
    require(cursor < records.size(), "expected raw event exists");
    const auto &actual = records[cursor++];
    if (actual.site != site || actual.bytes != expected ||
        actual.partition != partition) {
      std::cerr << "ordinal " << cursor << " actual site=" << actual.site
                << " size=" << actual.bytes.size() << " expected site=" << site
                << " size=" << expected.size() << '\n';
      throw std::runtime_error("exact native event differs from independent fixture");
    }
  }
  template <class... T> Bytes linked(std::size_t ordinal, const T &...values) const {
    auto result = ref(ordinal);
    (append(result, values), ...);
    return result;
  }
  void begin(std::uint8_t method, JournalCursor root, std::size_t facts,
             std::optional<OperationAttemptId> attempt = {}) {
    auto expected = body(method, attempt.has_value());
    if (attempt)
      append(expected, attempt->bytes());
    append(expected, root.journal.bytes());
    append(expected, root.sequence);
    append(expected, root.end_offset);
    append(expected, facts);
    expect(2300, std::move(expected));
  }
  void field(std::uint8_t tag, std::size_t value) {
    expect(2301, linked(1, tag, value));
  }
  void fact(std::size_t index, RecordReference reference, std::uint16_t kind,
            std::uint8_t evidence = 0) {
    const auto ordinal = cursor + 1;
    expect(2302, linked(1, std::uint8_t{1}, index));
    expect(2303, linked(ordinal, std::uint8_t{0}, reference.journal.bytes(),
                        reference.sequence, kind, evidence));
  }
  void filter(std::size_t ordinal, std::uint8_t branch = 1) {
    expect(2304, linked(ordinal, branch));
  }
  struct PacketInput {
    std::size_t size;
    std::uint8_t method = 1;
    std::size_t prior = 0;
    std::size_t byte_limit = 16777216;
    std::uint8_t branch = 0;
    std::optional<Error> error = {};
  };
  void packet(RecordReference record, PacketInput input) {
    const auto [size, method, prior, byte_limit, branch, error] = input;
    const auto ordinal = cursor + 1;
    auto expected = body(method);
    const auto query = ref(1);
    expected.insert(expected.end(), query.begin(), query.end());
    append(expected, record.journal.bytes());
    append(expected, record.sequence);
    append(expected, size);
    append(expected, byte_limit);
    append(expected, std::size_t{500000});
    append(expected, std::size_t{128});
    append(expected, prior != 0);
    if (prior) {
      const auto read = ref(prior);
      expected.insert(expected.end(), read.begin(), read.end());
    }
    expect(2307, std::move(expected));
    if (method == 1) {
      const auto read_ordinal = cursor + 1;
      expect(2302,
             linked(ordinal, std::uint8_t{4}, record.journal.bytes(), record.sequence));
      expect(2303, linked(read_ordinal, std::uint8_t{0}, size));
    }
    auto returned = linked(ordinal, branch, std::uint8_t{2});
    if (error) {
      append(returned, error->code);
      append(returned, error->detail);
    }
    expect(2308, std::move(returned));
  }
  void done() const { require(cursor == records.size(), "no unexamined raw events"); }
};
struct Failure {
  std::uint8_t branch;
  std::uint8_t query_stage;
  std::optional<Error> error;
};
void terminal_failure(const std::vector<Record> &records, Failure failure) {
  const auto [branch, query_stage, error] = failure;
  Oracle oracle{records};
  auto expected = oracle.linked(1, branch, std::uint8_t{0});
  if (error) {
    append(expected, error->code);
    append(expected, error->detail);
  }
  append(expected, query_stage);
  std::size_t terminals = 0;
  for (const auto &record : records)
    if (record.site == 2306) {
      ++terminals;
      require(record.bytes == expected && &record == &records.back(),
              "one final failure with native stage/error, no early success");
    }
  require(terminals == 1, "failure terminal is present exactly once");
}
void run() {
  Temporary temporary;
  const JournalHeader header{identity<EnvironmentId>(71),
                             identity<AuditStreamId>(72),
                             3,
                             {1024 * 1024, 4 * 1024 * 1024},
                             {}};
  auto root = unwrap(
      RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(temporary.path.string()))),
                            "audit", header, JournalCapacity{32 * 1024 * 1024, 4096}));
  AuditLog log{*root};
  log.original({"query.binary.fixture", std::string_view{"A\0\xffZ", 4}, Value{}});
  require(root->fact_count() == 2, "fixture has reservation and original metadata");
  const auto first = unwrap(root->fact(0)), original = unwrap(root->fact(1));
  const auto payload_size =
      std::get<ApplicationRecordEvent>(original.event.body).payload.size();
  auto before = root->cursor();
  unsigned count = 0;
  stage = "exact inspect page";
  auto records = capture(temporary, ++count, header.environment, [&] {
    const auto result = log.inspect(Value::object({}));
    require(field(result, "records").array().size() == 2 &&
                field(result, "end") == number(2) && field(result, "next") == number(2),
            "actual page returned the pinned two facts");
  });
  Oracle page{records};
  page.begin(1, before, 2);
  page.field(1, 2);
  page.field(2, 32);
  page.field(5, 4096);
  page.field(4, 0);
  page.fact(0, first.record, 1);
  page.filter(6);
  page.fact(1, original.record, 14);
  page.packet(original.record, {.size = payload_size});
  page.filter(9);
  page.expect(2306, page.linked(1, std::uint8_t{0}, std::uint8_t{1}, std::size_t{2},
                                std::size_t{2}, std::size_t{2}));
  page.done();
  require(root->cursor().journal == before.journal &&
              root->cursor().sequence == before.sequence &&
              root->cursor().end_offset == before.end_offset && root->fact_count() == 2,
          "query never appends semantic facts");

  stage = "exact source binary and actual caller";
  const auto caller = identity<OperationAttemptId>(81);
  records = capture(temporary, ++count, header.environment, [&] {
    const auto result = log.inspect(Value::object({{"record", number(1)},
                                                   {"source", number(0)},
                                                   {"offset", number(1)},
                                                   {"limit", number(2)}}),
                                    caller);
    require(string_field(result, "hex") == "00ff" && field(result, "next") == number(3),
            "binary source preserved and bounded correctly");
  });
  Oracle source{records};
  source.begin(1, before, 2, caller);
  source.field(1, 2);
  source.field(2, 32);
  source.field(5, 2);
  source.field(6, 1);
  source.fact(1, original.record, 14);
  source.packet(original.record, {.size = payload_size});
  source.filter(6);
  source.expect(2302, source.linked(1, std::uint8_t{4}, original.record.journal.bytes(),
                                    original.record.sequence));
  source.expect(2303, source.linked(13, std::uint8_t{0}, payload_size));
  source.field(7, 0);
  const auto dependency = original.event.dependencies.at(0);
  source.expect(2302, source.linked(1, std::uint8_t{3}, dependency.journal.bytes(),
                                    dependency.sequence));
  source.expect(2303, source.linked(16, std::uint8_t{0}, std::size_t{4}));
  source.field(8, 1);
  source.expect(2306, source.linked(1, std::uint8_t{0}, std::uint8_t{2}, std::size_t{2},
                                    std::size_t{4}, std::size_t{1}, std::size_t{3},
                                    std::size_t{2}));
  source.done();

  stage = "validation failures";
  for (const auto &query : {Value::object({{"limit", number(65537)}}),
                            Value::object({{"record", number(2)}}),
                            Value::object({{"count", Value{false}}})}) {
    records = capture(temporary, ++count, header.environment, [&] {
      bool refused = false;
      try {
        (void)log.inspect(query);
      } catch (const Error &error) {
        refused = error == Error{ErrorCode::invalid_range};
      }
      require(refused, "invalid query returns native invalid_range");
    });
    terminal_failure(records, {1, 1, Error{ErrorCode::invalid_range}});
  }
  records = capture(temporary, ++count, header.environment, [&] {
    bool refused = false;
    try {
      (void)log.variables(Value::object({{"variables", Value{false}}}));
    } catch (const Error &error) {
      refused = error == Error{ErrorCode::invalid_range};
    }
    require(refused, "variables wrapper rejects caller-owned variables field");
  });
  Oracle wrapper{records};
  wrapper.begin(3, before, 2);
  wrapper.expect(2306, wrapper.linked(1, std::uint8_t{1}, std::uint8_t{0},
                                      ErrorCode::invalid_range, std::int64_t{0},
                                      std::uint8_t{1}));
  wrapper.done();

  stage = "real allocation failures after fact read and during result construction";
  for (const bool empty : {false, true}) {
    const auto query =
        empty ? Value::object({{"cursor", number(2)}}) : Value::object({});
    records = capture(temporary, ++count, header.environment, [&] {
      allocation_hit = false;
      allocation_cut = 4 * sizeof(Value::Object::value_type);
      bool failed = false;
      try {
        (void)log.inspect(query);
      } catch (const std::bad_alloc &) {
        failed = true;
      }
      allocation_cut = 0;
      require(failed && allocation_hit, "actual native Value allocation cut executed");
    });
    terminal_failure(records, {2, static_cast<std::uint8_t>(empty ? 4 : 3), {}});
    const auto fact_returns =
        std::count_if(records.begin(), records.end(),
                      [](const auto &record) { return record.site == 2303; });
    require(fact_returns == (empty ? 0 : 1),
            "fact result recorded before summary allocation failure");
    require(std::none_of(records.begin(), records.end(),
                         [](const auto &record) { return record.site == 2304; }),
            "no selection falsely recorded before unfinished summary");
  }

  stage = "large source ownership move and output allocation failure";
  log.original({"query.copy.fixture", std::string(65536, 'Q'), Value{}});
  const auto large_record = root->fact_count() - 1;
  const auto large_query = Value::object({{"record", number(large_record)},
                                          {"source", number(0)},
                                          {"limit", number(65536)}});
  records = capture(temporary, ++count, header.environment, [&] {
    source_allocations = 0;
    measure_source = true;
    auto result = log.inspect(large_query);
    measure_source = false;
    require(source_allocations == 0,
            "source ownership move removes measured 65536-byte copy allocation");
    const auto &hex = string_field(result, "hex");
    require(hex.size() == 131072 && field(result, "total_bytes") == number(65536),
            "exact large source length");
    for (std::size_t i = 0; i < hex.size(); i += 2)
      require(hex[i] == '5' && hex[i + 1] == '1',
              "exact large original byte at each position");
  });
  require(records.back().site == 2306, "large source returned one terminal");
  records = capture(temporary, ++count, header.environment, [&] {
    allocation_hit = false;
    require(hex_allocation != 0, "actual hex allocation size was measured");
    allocation_cut = hex_allocation;
    bool failed = false;
    try {
      (void)log.inspect(large_query);
    } catch (const std::bad_alloc &) {
      failed = true;
    }
    allocation_cut = 0;
    require(failed && allocation_hit,
            "actual hex reserve allocation failed after source acquisition");
  });
  terminal_failure(records, {2, 4, {}});
  bool acquired = false;
  for (const auto &record : records)
    if (record.site == 2303 && record.bytes.size() == 33) {
      auto bytes = ByteView{record.bytes}.subspan(24);
      if (take<std::uint8_t>(bytes) == 0 && take<std::size_t>(bytes) == 65536)
        acquired = true;
    }
  require(acquired,
          "actual source acquisition result precedes failed output allocation");

  stage = "direct packet decode references actual prior acquisition";
  before = root->cursor();
  const auto facts = root->fact_count();
  const auto packet_value = unwrap(
      read_packet(std::get<ApplicationRecordEvent>(original.event.body).payload));
  const auto expanded = unwrap(format_value(packet_value));
  records = capture(temporary, ++count, header.environment, [&] {
    const auto result =
        log.inspect(Value::object({{"record", number(1)}, {"packet", Value{true}}}));
    require(field(result, "packet") == packet_value &&
                field(result, "decoded_bytes") == number(expanded.size()),
            "decoded packet is actual retained metadata");
  });
  Oracle decoded{records};
  decoded.begin(1, before, facts);
  decoded.field(1, facts);
  decoded.field(2, 32);
  decoded.field(5, 4096);
  decoded.field(6, 1);
  decoded.fact(1, original.record, 14);
  decoded.packet(original.record, {.size = payload_size});
  decoded.filter(6);
  decoded.expect(2302,
                 decoded.linked(1, std::uint8_t{4}, original.record.journal.bytes(),
                                original.record.sequence));
  decoded.expect(2303, decoded.linked(13, std::uint8_t{0}, payload_size));
  decoded.expect(2301, decoded.linked(1, std::uint8_t{10}, true));
  decoded.packet(original.record,
                 {.size = payload_size, .method = 2, .prior = 13, .byte_limit = 4096});
  decoded.expect(2306, decoded.linked(1, std::uint8_t{0}, std::uint8_t{3}, facts,
                                      payload_size, expanded.size()));
  decoded.done();

  stage = "variables wrapper, exact variable bytes and both filter outcomes";
  log.record(ApplicationChannel::log,
             Value::object({{"label", Value{"variable.sample"}},
                            {"variable", Value{"cpu"}},
                            {"value", number(17)}}));
  const auto sample_index = root->fact_count() - 1;
  require(sample_index == large_record + 1,
          "variable fixture immediately follows nonvariable original");
  const auto large_fact = unwrap(root->fact(large_record)),
             sample_fact = unwrap(root->fact(sample_index));
  before = root->cursor();
  records = capture(temporary, ++count, header.environment, [&] {
    const auto result = log.variables(Value::object({{"cursor", number(large_record)},
                                                     {"scan", number(2)},
                                                     {"variable", Value{"cpu"}}}));
    const auto &rows = field(result, "events").array();
    require(rows.size() == 1 && string_field(rows[0], "variable") == "cpu" &&
                field(rows[0], "value") == number(17),
            "variables reader selected actual cpu sample");
  });
  Oracle variables{records};
  variables.begin(3, before, root->fact_count());
  variables.field(1, root->fact_count());
  variables.field(2, 32);
  variables.field(3, 2);
  variables.expect(2301, variables.linked(1, std::uint8_t{9}, true));
  auto variable_body = variables.linked(1, std::size_t{3});
  for (const char value : std::string_view{"cpu"})
    variable_body.push_back(std::byte{static_cast<unsigned char>(value)});
  variables.expect(2305, std::move(variable_body), RawPartition::data);
  variables.field(4, large_record);
  for (const auto &[index, fact] : std::array{std::pair{large_record, large_fact},
                                              std::pair{sample_index, sample_fact}}) {
    const auto ordinal = variables.cursor + 1;
    variables.fact(index, fact.record, 14);
    variables.packet(
        fact.record,
        {.size = std::get<ApplicationRecordEvent>(fact.event.body).payload.size()});
    variables.filter(ordinal, index == sample_index ? 1 : 2);
  }
  variables.expect(2306, variables.linked(1, std::uint8_t{0}, std::uint8_t{4},
                                          root->fact_count(), large_record,
                                          root->fact_count(), std::size_t{1}));
  variables.done();

  stage = "native attempt optional fields, no inferred terminal flag";
  const auto decision = identity<DecisionId>(90);
  const auto invocation = identity<InvocationId>(91);
  const auto attempt = identity<OperationAttemptId>(92);
  const auto sibling_invocation = identity<InvocationId>(98);
  const auto sibling_attempt = identity<OperationAttemptId>(99);
  const auto decision_index = root->fact_count();
  std::size_t observation_index = 0;
  const std::array<RetainedEvent, 3> admission{
      RetainedEvent{{},
                    DecisionEvent{decision,
                                  identity<ParticipantId>(93),
                                  identity<ConversationId>(94),
                                  identity<WorkflowId>(95),
                                  identity<DefinitionGenerationId>(96),
                                  identity<ContextRevisionId>(97),
                                  {invocation, sibling_invocation},
                                  ImmutableBytes{}}},
      RetainedEvent{{},
                    InvocationEvent{invocation, decision,
                                    identity<DefinitionGenerationId>(96),
                                    ImmutableBytes{}}},
      RetainedEvent{
          {}, AttemptAdmissionEvent{attempt, invocation, decision, ImmutableBytes{}}}};
  unwrap(root->append(root->cursor(), {}, admission));
  for (unsigned state = 0; state < 5; ++state) {
    if (state == 1)
      unwrap(root->submit({{}, AttemptOpenEvent{attempt}}));
    if (state == 2) {
      unwrap(root->submit(
          {{},
           AttemptObservationEvent{attempt, AttemptPhase::running,
                                   AttemptDisposition::none, ImmutableBytes{}}}));
      observation_index = root->fact_count() - 1;
    }
    if (state == 3)
      unwrap(root->submit({{}, AdapterReceiptEvent{attempt, Error{ErrorCode::io, 5}}}));
    if (state == 4)
      unwrap(root->submit(
          {{},
           AttemptObservationEvent{attempt, AttemptPhase::terminal,
                                   AttemptDisposition::failure, ImmutableBytes{}}}));
    before = root->cursor();
    records = capture(temporary, ++count, header.environment, [&] {
      const auto result = log.trajectory(Value::object(
          {{"end", number(0)}, {"attempt", Value{hex_identity(attempt.bytes())}}}));
      require(field(result, "events").array().empty(),
              "pinned empty prefix contains no fabricated attempt rows");
    });
    Oracle optional{records};
    optional.begin(2, before, root->fact_count());
    optional.field(1, 0);
    optional.field(2, 32);
    optional.field(3, 128);
    optional.expect(2301, optional.linked(1, std::uint8_t{11}, attempt.bytes()));
    optional.expect(2302, optional.linked(1, std::uint8_t{2}, attempt.bytes()));
    auto native_attempt =
        optional.linked(6, std::uint8_t{0}, attempt.bytes(), invocation.bytes(),
                        decision.bytes(), state >= 1, state >= 2);
    if (state >= 2) {
      append(native_attempt, static_cast<std::uint8_t>(state == 4 ? 3U : 1U));
      append(native_attempt, static_cast<std::uint8_t>(state == 4 ? 2U : 0U));
    }
    append(native_attempt, state >= 3);
    if (state >= 3) {
      append(native_attempt, true);
      append(native_attempt, ErrorCode::io);
      append(native_attempt, std::int64_t{5});
    }
    append(native_attempt, std::uint8_t{0});
    append(native_attempt, false);
    optional.expect(2303, std::move(native_attempt));
    optional.field(4, 0);
    optional.expect(2306,
                    optional.linked(1, std::uint8_t{0}, std::uint8_t{4}, std::size_t{0},
                                    std::size_t{0}, std::size_t{0}, std::size_t{0}));
    optional.done();
  }
  stage = "nonempty attempt filter links selected and sibling skipped facts";
  const auto selected_index = root->fact_count() - 1;
  const auto selected_fact = unwrap(root->fact(selected_index));
  const std::array<RetainedEvent, 2> sibling{
      RetainedEvent{{},
                    InvocationEvent{sibling_invocation, decision,
                                    identity<DefinitionGenerationId>(96),
                                    ImmutableBytes{}}},
      RetainedEvent{{},
                    AttemptAdmissionEvent{sibling_attempt, sibling_invocation, decision,
                                          ImmutableBytes{}}}};
  unwrap(root->append(root->cursor(), {}, sibling));
  const auto sibling_invocation_fact = unwrap(root->fact(selected_index + 1));
  const auto sibling_admission_fact = unwrap(root->fact(selected_index + 2));
  const auto pinned_end = root->fact_count();
  before = root->cursor();
  records = capture(temporary, ++count, header.environment, [&] {
    const auto result = log.trajectory(
        Value::object({{"cursor", number(selected_index)},
                       {"end", number(pinned_end)},
                       {"count", number(3)},
                       {"scan", number(3)},
                       {"attempt", Value{hex_identity(attempt.bytes())}}}));
    const auto &rows = field(result, "events").array();
    require(rows.size() == 1 && field(rows[0], "record") == number(selected_index) &&
                string_field(rows[0], "attempt") == hex_identity(attempt.bytes()) &&
                string_field(rows[0], "outcome") == "failure" &&
                field(result, "next") == number(pinned_end),
            "actual attempt filter selects terminal fact and scans both siblings");
  });
  Oracle filtered{records};
  filtered.begin(2, before, pinned_end);
  filtered.field(1, pinned_end);
  filtered.field(2, 3);
  filtered.field(3, 3);
  filtered.expect(2301, filtered.linked(1, std::uint8_t{11}, attempt.bytes()));
  filtered.expect(2302, filtered.linked(1, std::uint8_t{2}, attempt.bytes()));
  filtered.expect(2303, filtered.linked(6, std::uint8_t{0}, attempt.bytes(),
                                        invocation.bytes(), decision.bytes(), true,
                                        true, std::uint8_t{3}, std::uint8_t{2}, true,
                                        true, ErrorCode::io, std::int64_t{5},
                                        std::uint8_t{0}, false));
  filtered.field(4, selected_index);
  const auto selected_read = filtered.cursor + 1;
  filtered.fact(selected_index, selected_fact.record, 6);
  filtered.packet(selected_fact.record,
                  {.size = 0, .branch = 1, .error = Error{ErrorCode::corrupt}});
  filtered.filter(selected_read, 1);
  for (const auto &[index, fact, kind] : std::array{
           std::tuple{selected_index + 1, sibling_invocation_fact, std::uint16_t{3}},
           std::tuple{selected_index + 2, sibling_admission_fact, std::uint16_t{4}}}) {
    const auto fact_read = filtered.cursor + 1;
    filtered.fact(index, fact.record, kind);
    filtered.filter(fact_read, 3);
  }
  filtered.expect(2306, filtered.linked(1, std::uint8_t{0}, std::uint8_t{4}, pinned_end,
                                        selected_index, pinned_end, std::size_t{1}));
  filtered.done();
  require(root->cursor().journal == before.journal &&
              root->cursor().sequence == before.sequence &&
              root->cursor().end_offset == before.end_offset &&
              root->fact_count() == pinned_end,
          "attempt filter never appends semantic facts");
  stage = "handled summary decode failures still emit helper Error and query success";
  for (const auto &[index, kind] :
       std::array{std::pair{decision_index, std::uint16_t{2}},
                  std::pair{observation_index, std::uint16_t{6}}}) {
    const auto fact = unwrap(root->fact(index));
    before = root->cursor();
    records = capture(temporary, ++count, header.environment, [&] {
      const auto result = log.trajectory(Value::object(
          {{"cursor", number(index)}, {"count", number(1)}, {"scan", number(1)}}));
      require(field(result, "events").array().size() == 1,
              "handled decode error preserves selected metadata row");
    });
    Oracle handled{records};
    handled.begin(2, before, root->fact_count());
    handled.field(1, root->fact_count());
    handled.field(2, 1);
    handled.field(3, 1);
    handled.field(4, index);
    handled.fact(index, fact.record, kind);
    handled.packet(fact.record,
                   {.size = 0, .branch = 1, .error = Error{ErrorCode::corrupt}});
    handled.filter(6);
    handled.expect(2306, handled.linked(1, std::uint8_t{0}, std::uint8_t{4},
                                        root->fact_count(), index, index + 1,
                                        std::size_t{1}));
    handled.done();
  }
  stage = "diagnostic request links the actual metadata read and native clock";
  const auto stored_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
  require(stored_seconds >= 0, "native diagnostic store clock");
  const auto stored_now = static_cast<std::uint64_t>(stored_seconds);
  const std::string diagnostic_name =
      std::to_string(stored_now + 30ULL * 24 * 60 * 60) +
      ".00000000000000000000000000000001";
  const std::string diagnostic_bytes{"A\0\xffZ", 4};
  unwrap(root->store_diagnostic(
      diagnostic_name,
      std::as_bytes(std::span{diagnostic_bytes.data(), diagnostic_bytes.size()}),
      stored_now));
  for (const bool invalid : {false, true}) {
    log.record(
        ApplicationChannel::log,
        Value::object(
            {{"label", Value{"provider.stream"}},
             {"metadata",
              Value::object({{"diagnostic_file",
                              Value{invalid ? "not-canonical" : diagnostic_name}}})}}));
    const auto index = root->fact_count() - 1;
    const auto fact = unwrap(root->fact(index));
    const auto size = std::get<ApplicationRecordEvent>(fact.event.body).payload.size();
    before = root->cursor();
    const auto clock_before = std::chrono::duration_cast<std::chrono::seconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
    records = capture(temporary, ++count, header.environment, [&] {
      const auto query =
          Value::object({{"record", number(index)}, {"diagnostic", Value{true}}});
      if (invalid) {
        bool failed = false;
        try {
          (void)log.inspect(query);
        } catch (const Error &error) {
          failed = error == Error{ErrorCode::invalid_range};
        }
        require(failed, "malformed diagnostic request fails natively");
      } else {
        const auto result = log.inspect(query);
        require(string_field(result, "hex") == "4100ff5a" &&
                    field(result, "total_bytes") == number(4),
                "actual diagnostic acquisition preserves binary bytes");
      }
    });
    const auto clock_after = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    Oracle diagnostic{records};
    diagnostic.begin(1, before, root->fact_count());
    diagnostic.field(1, root->fact_count());
    diagnostic.field(2, 32);
    diagnostic.field(5, 4096);
    diagnostic.field(6, index);
    diagnostic.fact(index, fact.record, 14);
    diagnostic.packet(fact.record, {.size = size});
    diagnostic.filter(6);
    diagnostic.expect(2302,
                      diagnostic.linked(1, std::uint8_t{4}, fact.record.journal.bytes(),
                                        fact.record.sequence));
    diagnostic.expect(2303, diagnostic.linked(13, std::uint8_t{0}, size));
    diagnostic.packet(fact.record, {.size = size});
    require(diagnostic.cursor < records.size(), "diagnostic actual request exists");
    auto time_field = ByteView{records[diagnostic.cursor].bytes}.subspan(73);
    const auto now = take<std::uint64_t>(time_field);
    require(time_field.empty() && clock_before >= 0 &&
                now >= static_cast<std::uint64_t>(clock_before) &&
                now <= static_cast<std::uint64_t>(clock_after),
            "diagnostic copies native acquired seconds within actual call interval");
    auto request = diagnostic.linked(1, std::uint8_t{5}, fact.record.journal.bytes(),
                                     fact.record.sequence);
    const auto metadata = diagnostic.ref(16);
    request.insert(request.end(), metadata.begin(), metadata.end());
    append(request, now);
    diagnostic.expect(2302, std::move(request));
    if (invalid) {
      diagnostic.expect(2303,
                        diagnostic.linked(19, std::uint8_t{1}, ErrorCode::invalid_range,
                                          std::int64_t{0}));
      diagnostic.expect(2306, diagnostic.linked(1, std::uint8_t{1}, std::uint8_t{0},
                                                ErrorCode::invalid_range,
                                                std::int64_t{0}, std::uint8_t{2}));
    } else {
      diagnostic.expect(2303, diagnostic.linked(19, std::uint8_t{0}, std::size_t{4}));
      diagnostic.field(8, 0);
      diagnostic.expect(2306, diagnostic.linked(1, std::uint8_t{0}, std::uint8_t{2},
                                                root->fact_count(), std::size_t{4},
                                                std::size_t{0}, std::size_t{4},
                                                std::size_t{4}));
    }
    diagnostic.done();
  }
  stage = "cold native journal read returns EIO after real reopen";
  {
    Temporary cold;
    std::size_t cold_index = 0;
    {
      auto created = unwrap(RetainedState::create(
          std::make_unique<NativeJournalDirectory>(
              unwrap(NativeJournalDirectory::open(cold.path.string()))),
          "audit", header, JournalCapacity{32 * 1024 * 1024, 4096}));
      AuditLog created_log{*created};
      created_log.original({"cold.fixture", "bytes", Value{}});
      cold_index = created->fact_count() - 1;
    }
    stage = "cold native journal reopen setup";
    auto reopened = unwrap(
        RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(cold.path.string()))),
                            "audit", header, JournalCapacity{32 * 1024 * 1024, 4096}));
    unwrap(reopened->confirm_recovery());
    AuditLog cold_log{*reopened};
    stage = "cold native fact fixture setup";
    const auto fact = unwrap(reopened->fact(cold_index));
    const auto size = std::get<ApplicationRecordEvent>(fact.event.body).payload.size();
    struct stat metadata{};
    require(::stat((cold.path / "audit").c_str(), &metadata) == 0,
            "actual journal inode");
    read_device = metadata.st_dev;
    read_inode = metadata.st_ino;
    const auto query = Value::object({{"record", number(cold_index)}});
    stage = "cold native journal read returns EIO after real reopen";
    records = capture(cold, 1, header.environment, [&] {
      read_hits = 0;
      read_cut = true;
      bool failed = false;
      try {
        (void)cold_log.inspect(query);
      } catch (const Error &error) {
        failed = error == Error{ErrorCode::io, EIO};
      }
      read_cut = false;
      require(failed && read_hits == 1,
              "actual reopened metadata pread returned EIO exactly once");
    });
    Oracle io{records};
    io.begin(1, reopened->cursor(), reopened->fact_count());
    io.field(1, reopened->fact_count());
    io.field(2, 32);
    io.field(5, 4096);
    io.field(6, cold_index);
    io.fact(cold_index, fact.record, 14, static_cast<std::uint8_t>(fact.evidence));
    auto request = body(std::uint8_t{1});
    const auto query_ref = io.ref(1);
    request.insert(request.end(), query_ref.begin(), query_ref.end());
    append(request, fact.record.journal.bytes());
    append(request, fact.record.sequence);
    append(request, size);
    append(request, std::size_t{16777216});
    append(request, std::size_t{500000});
    append(request, std::size_t{128});
    append(request, false);
    io.expect(2307, std::move(request));
    io.expect(2302, io.linked(8, std::uint8_t{4}, fact.record.journal.bytes(),
                              fact.record.sequence));
    io.expect(2303, io.linked(9, std::uint8_t{1}, ErrorCode::io, std::int64_t{EIO}));
    io.expect(2308, io.linked(8, std::uint8_t{1}, std::uint8_t{1}, ErrorCode::io,
                              std::int64_t{EIO}));
    io.expect(2306, io.linked(1, std::uint8_t{1}, std::uint8_t{0}, ErrorCode::io,
                              std::int64_t{EIO}, std::uint8_t{3}));
    io.done();

    stage = "cancelled next read admission preserves already-held query outcomes";
    const auto pinned_cursor = reopened->cursor();
    const auto pinned_facts = reopened->fact_count();
    records = capture(
        cold, 2, header.environment,
        [&](RawRuntime &runtime) {
          const auto holders = unwrap(runtime.status());
          ResumeHolders resume{{holders.recorder, holders.supervisor}};
          require(::kill(holders.recorder, SIGSTOP) == 0 &&
                      ::kill(holders.supervisor, SIGSTOP) == 0,
                  "stop only actual test-owned Rec/Sup");
          confirm_holder_stopped(holders.recorder);
          confirm_holder_stopped(holders.supervisor);
          Cancellation cancellation;
          RawWaitScope wait{{{cancellation.endpoints[0], -1}, {}}};
          successful_reads = successful_headers = 0;
          ordinary_full = false;
          fill_after_read = true;
          struct ResetNativeCut {
            ~ResetNativeCut() { fill_after_read = fill_on_allocation = false; }
          } reset;
          bool interrupted = false;
          try {
            (void)cold_log.inspect(query);
          } catch (const Error &error) {
            interrupted = error == Error{ErrorCode::interrupted, ECANCELED};
          }
          cancellation.finish();
          if (!(interrupted && ordinary_full && successful_reads == 1 &&
                successful_headers == 1 && cancellation_initially_unready.load() &&
                cancellation_written.load() == 1 &&
                cancellation_native_return.load() > 0))
            std::cerr << "cancel cut interrupted=" << interrupted
                      << " full=" << ordinary_full << " frames=" << successful_reads
                      << " headers=" << successful_headers
                      << " initially_unready=" << cancellation_initially_unready.load()
                      << " wrote=" << cancellation_written.load()
                      << " native_poll=" << cancellation_native_return.load() << '\n';
          require(interrupted && ordinary_full && successful_reads == 1 &&
                      successful_headers == 1 &&
                      cancellation_initially_unready.load() &&
                      cancellation_written.load() == 1 &&
                      cancellation_native_return.load() > 0,
                  "actual next admission waited before native cancellation; only "
                  "the prior native read ran");
          require(holder_stopped(holders.recorder) &&
                      holder_stopped(holders.supervisor),
                  "held query outcomes publish while Rec/Sup remain stopped");
        },
        true);
    Oracle cancelled{records};
    cancelled.begin(1, pinned_cursor, pinned_facts);
    cancelled.field(1, pinned_facts);
    cancelled.field(2, 32);
    cancelled.field(5, 4096);
    cancelled.field(6, cold_index);
    cancelled.fact(cold_index, fact.record, 14,
                   static_cast<std::uint8_t>(fact.evidence));
    cancelled.packet(fact.record, {.size = size});
    cancelled.filter(6);
    cancelled.expect(2306, cancelled.linked(1, std::uint8_t{1}, std::uint8_t{0},
                                            ErrorCode::interrupted,
                                            static_cast<std::int64_t>(ECANCELED),
                                            std::uint8_t{2}));
    cancelled.done();
    for (std::size_t i = 0; i < 10; ++i)
      require(records[i].ordinal == i + 1,
              "first exact read return precedes all physical capacity fill");
    require(records[10].ordinal > 11 && records[11].ordinal > 12,
            "actual helper return and selection publish after physical fill");
    require(reopened->cursor().journal == pinned_cursor.journal &&
                reopened->cursor().sequence == pinned_cursor.sequence &&
                reopened->cursor().end_offset == pinned_cursor.end_offset &&
                reopened->fact_count() == pinned_facts,
            "cancelled query never appends semantic facts");
  }
  std::cout
      << "native query events exact; validation, allocation and native EIO preserved; "
         "source copy allocations 1 -> 0\n";
}
} // namespace
int main() {
  try {
    run();
    return 0;
  } catch (const Error &error) {
    std::cerr << stage << ": " << error_name(error.code) << '\n';
  } catch (const std::exception &error) {
    std::cerr << stage << ": " << error.what() << '\n';
  }
  return 1;
}
