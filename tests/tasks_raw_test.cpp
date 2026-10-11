#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/tasks.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

using namespace blackbird;
namespace {
thread_local RetainedState *watched_root = nullptr;
thread_local const TaskState *watched_state = nullptr;
thread_local std::uint64_t before_sequence = 0;
thread_local std::size_t failed_size = 0;
thread_local bool fail_after_append = false;
thread_local RawChannel *cut_channel = nullptr;
thread_local RawProducer *cut_producer = nullptr;
thread_local dev_t cut_device = 0;
thread_local ino_t cut_inode = 0;
thread_local int cancel_write = -1;
thread_local bool cut_write_failure = false, cut_sync_cancel = false, cut_fired = false;
bool publication_active() {
  if (!cut_channel)
    return false;
  const auto cursor = unwrap(cut_channel->cursors(RawPartition::control));
  const auto batch =
      unwrap(cut_channel->read(RawPartition::control, cursor.acknowledged, 65536));
  std::size_t offset = 0;
  while (offset < batch.bytes.size()) {
    std::uint32_t size = 0, site = 0;
    std::memcpy(&size, batch.bytes.data() + offset, sizeof(size));
    std::memcpy(&site, batch.bytes.data() + offset + 4, sizeof(site));
    if (site == 1704)
      return true;
    if (size < 16 || size > batch.bytes.size() - offset)
      std::abort();
    offset += size;
  }
  return false;
}
bool selected_file(int fd) {
  struct stat metadata{};
  return ::fstat(fd, &metadata) == 0 && metadata.st_dev == cut_device &&
         metadata.st_ino == cut_inode;
}
} // namespace
void *operator new(std::size_t size) {
  if (fail_after_append && watched_root && watched_state &&
      watched_root->cursor().sequence > before_sequence &&
      watched_state->revision() == 0) {
    fail_after_append = false;
    failed_size = size;
    throw std::bad_alloc{};
  }
  if (void *pointer = std::malloc(size == 0 ? 1 : size))
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
  ::operator delete[](pointer);
}

extern "C" ssize_t pwrite(int fd, const void *bytes, std::size_t size, off_t offset) {
  using Native = ssize_t (*)(int, const void *, std::size_t, off_t);
  static const auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "pwrite"));
  if (!native)
    std::abort();
  if (cut_write_failure && selected_file(fd) && publication_active()) {
    cut_write_failure = false;
    cut_fired = true;
    errno = EIO;
    return -1;
  }
  return native(fd, bytes, size, offset);
}
namespace {
void after_sync(int fd, int result) {
  const int saved_error = errno;
  if (result == 0 && cut_sync_cancel && selected_file(fd) && publication_active()) {
    cut_sync_cancel = false;
    cut_fired = true;
    const char byte = 'c';
    if (::write(cancel_write, &byte, 1) != 1 || !cut_producer)
      std::abort();
    const auto remaining = unwrap(cut_producer->available(RawPartition::control));
    if (remaining < 16)
      std::abort();
    const std::vector<std::byte> padding(static_cast<std::size_t>(remaining - 16));
    const ByteView view{padding};
    unwrap(cut_producer->reserve(RawPartition::control, remaining));
    (void)unwrap(cut_producer->emit(9112, std::span{&view, 1}, RawPartition::control));
  }
  errno = saved_error;
}
} // namespace
extern "C" int fsync(int fd) {
  using Native = int (*)(int);
  static const auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "fsync"));
  if (!native)
    std::abort();
  const int result = native(fd);
  after_sync(fd, result);
  return result;
}
#if defined(__APPLE__)
extern "C" int fcntl(int fd, int command, ...) {
  using Native = int (*)(int, int, ...);
  static const auto native = reinterpret_cast<Native>(::dlsym(RTLD_NEXT, "fcntl"));
  if (!native)
    std::abort();
  if (command == F_FULLFSYNC || command == F_GETFD || command == F_GETFL ||
      command == F_GETOWN) {
    const int result = native(fd, command);
    if (command == F_FULLFSYNC)
      after_sync(fd, result);
    return result;
  }
  va_list arguments;
  va_start(arguments, command);
  int result;
  if (command == F_GETPATH || command == F_GETLK || command == F_SETLK ||
      command == F_SETLKW || command == F_PREALLOCATE || command == F_RDADVISE ||
      command == F_LOG2PHYS || command == F_LOG2PHYS_EXT) {
    void *argument = va_arg(arguments, void *);
    result = native(fd, command, argument);
  } else {
    const int argument = va_arg(arguments, int);
    result = native(fd, command, argument);
  }
  va_end(arguments);
  return result;
}
#endif

namespace {
template <class T> T id(unsigned char n) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{n};
  return unwrap(T::from_bytes(bytes));
}
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
template <class T> void append(std::vector<std::byte> &bytes, const T &value) {
  const auto field = std::as_bytes(std::span{&value, std::size_t{1}});
  bytes.insert(bytes.end(), field.begin(), field.end());
}
template <class T> T native(ByteView bytes, std::size_t offset) {
  check(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset,
        "short native field");
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(value));
  return value;
}
struct Record {
  RawOrigin origin;
  std::uint32_t site;
  std::uint64_t ordinal;
  std::vector<std::byte> body;
};
std::vector<std::byte> reference(const Record &record) {
  std::vector<std::byte> result{record.origin.lane.begin(), record.origin.lane.end()};
  append(result, record.ordinal);
  return result;
}
const Record &linked(const std::vector<Record> &records, std::uint32_t site,
                     const Record &request) {
  const auto ref = reference(request);
  const Record *found = nullptr;
  for (const auto &record : records)
    if (record.site == site && record.body.size() >= ref.size() &&
        std::equal(ref.begin(), ref.end(), record.body.begin())) {
      check(!found, "duplicate linked outcome");
      found = &record;
    }
  check(found != nullptr, "missing linked owner outcome");
  return *found;
}
Value task_request(std::string_view op_id, std::string_view title,
                   std::uint64_t base = 0) {
  return Value::object(
      {{"op_id", Value{std::string{op_id}}},
       {"base", Value{Number{base}}},
       {"ops", Value{Value::Array{Value::object(
                   {{"op", Value{"add"}}, {"title", Value{std::string{title}}}})}}}});
}
template <class F> void error_is(F &&action, ErrorCode expected) {
  bool refused = false;
  try {
    action();
  } catch (const Error &error) {
    refused = error.code == expected;
  }
  check(refused, "incorrect actual task error");
}
void raw_surfaces(const char *path) {
  std::uint64_t accepted_cursor = 0;
  ApplicationRecordId published = id<ApplicationRecordId>(99);
  {
    auto runtime = unwrap(RawRuntime::open(path, id<EnvironmentId>(1)));
    RawThreadBinding binding{unwrap(runtime->prepare_thread())};
    const JournalHeader header{id<EnvironmentId>(1),
                               id<AuditStreamId>(8),
                               3,
                               {1024 * 1024, 4 * 1024 * 1024},
                               std::nullopt};
    auto root = unwrap(
        RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                  unwrap(NativeJournalDirectory::open(path))),
                              "raw-business", header, {64 * 1024 * 1024, 20000}));
    AuditLog log{*root};
    TaskStore tasks{log};
    Value packet;
    unsigned callbacks = 0;
    tasks.changed = [&](const Value &delta) {
      packet = delta;
      ++callbacks;
      throw Error{ErrorCode::io, 91};
    };
    const auto request = task_request("seed", "task");
    const auto first = tasks.edit(request);
    check(tasks.state().revision() == 1 && callbacks == 1,
          "accepted task not committed");
    accepted_cursor = root->cursor().sequence;
    const auto &facts = root->committed_facts();
    for (const auto &fact : facts)
      if (const auto *event = std::get_if<ApplicationRecordEvent>(&fact.event.body);
          event && event->channel == ApplicationChannel::program)
        published = event->identity;
    check(tasks.edit(request) == first && callbacks == 1 &&
              root->cursor().sequence == accepted_cursor,
          "duplicate edited task again");
    const auto stale = tasks.edit(task_request("stale", "task", 0));
    check(std::get<bool>(field(stale, "conflict").value()), "stale edit accepted");
    const auto reuse = tasks.edit(task_request("seed", "changed", 0));
    check(std::get<bool>(field(reuse, "conflict").value()), "reused op_id accepted");
    error_is([&] { (void)tasks.edit(Value::object({{"invalid", Value{true}}})); },
             ErrorCode::invalid_range);
    error_is(
        [&] {
          (void)tasks.edit(task_request("oversized", std::string(70000, 'x'), 1));
        },
        ErrorCode::invalid_range);
    Value deep = Value{true};
    for (unsigned n = 0; n < 140; ++n)
      deep = Value{Value::Array{std::move(deep)}};
    error_is(
        [&] {
          (void)tasks.state().prepare(Value::object(
              {{"op_id", Value{"codec-failure"}}, {"ops", std::move(deep)}}));
        },
        ErrorCode::capacity);
    const auto page = tasks.read();
    check(field(page, "items").array().size() == 1, "native page missing task");
    const auto conflict = tasks.read(Value::object({{"revision", Value{Number{0}}}}));
    check(std::get<bool>(field(conflict, "conflict").value()), "stale read accepted");
    const auto saved = tasks.snapshot();
    TaskState restored;
    restored.restore(saved);
    check(restored.snapshot() == saved, "actual restore mismatch");
    error_is([&] { restored.restore(Value::object({})); }, ErrorCode::corrupt);
    check(restored.revision() == 1, "malformed restore replaced task state");
    TaskState replayed;
    replayed.replay(packet);
    check(replayed.snapshot() == saved, "actual replay mismatch");
    error_is([&] { replayed.replay(packet); }, ErrorCode::corrupt);
    (void)tasks.operator_command("help");
    error_is([&] { (void)tasks.operator_command("apply !"); }, ErrorCode::corrupt);
    check(root->cursor().sequence == accepted_cursor && callbacks == 1,
          "reconstruction/invalid actions published a fresh edit");
  }
  const auto runs = unwrap(discover_raw_runs(path));
  check(runs.size() == 1, "expected one actual capture run");
  auto reader = unwrap(RawRunReader::open(
      (std::filesystem::path{path} / "raw" / runs[0].name).string()));
  check(reader->snapshot().closed, "task capture not closed");
  std::vector<std::byte> output(1024 * 1024);
  std::vector<Record> records;
  while (const auto record = unwrap(reader->read(output))) {
    const auto site = native<std::uint32_t>(record->raw, 4);
    if (site >= 1700 && site <= 1713)
      records.push_back({record->origin,
                         site,
                         native<std::uint64_t>(record->raw, 8),
                         {record->raw.begin() + 16, record->raw.end()}});
  }
  check(!records.empty(), "actual task edit/read/operator emitted no task1700 request");
  std::sort(records.begin(), records.end(),
            [](const Record &left, const Record &right) {
              return left.ordinal < right.ordinal;
            });
  const auto &edit = records.front();
  check(edit.site == 1700, "first task owner not edit");
  std::vector<std::byte> expected;
  append(expected, std::uint8_t{6});
  append(expected, false);
  append(expected, std::uint64_t{0});
  check(edit.body == expected, "edit BEGIN original native body mismatch");
  const auto edit_ref = reference(edit);
  const Record *prepare = nullptr;
  for (const auto &record : records)
    if (record.site == 1700 && native<std::uint8_t>(record.body, 0) == 1 &&
        native<bool>(record.body, 1) && record.body.size() == 34 &&
        std::equal(edit_ref.begin(), edit_ref.end(), record.body.begin() + 2)) {
      prepare = &record;
      break;
    }
  check(prepare != nullptr, "edit missing actual child preparation");
  const auto &source = linked(records, 1701, *prepare);
  constexpr unsigned char packet_bytes[] = {
      'B', 'B', 'M', 2,   10,  3,   6,   5,   'o', 'p', '_', 'i', 'd', 6,   4,
      's', 'e', 'e', 'd', 6,   4,   'b', 'a', 's', 'e', 3,   0,   6,   3,   'o',
      'p', 's', 9,   1,   10,  2,   6,   2,   'o', 'p', 6,   3,   'a', 'd', 'd',
      6,   5,   't', 'i', 't', 'l', 'e', 6,   4,   't', 'a', 's', 'k'};
  expected = reference(*prepare);
  append(expected, std::size(packet_bytes));
  for (const auto byte : packet_bytes)
    expected.push_back(std::byte{byte});
  check(source.body == expected, "existing encoded request original bytes differ");
  const auto &publication = linked(records, 1704, edit);
  expected = reference(edit);
  const auto prep_ref = reference(*prepare);
  expected.insert(expected.end(), prep_ref.begin(), prep_ref.end());
  expected.insert(expected.end(), published.bytes().begin(), published.bytes().end());
  check(publication.body == expected, "publication identity/owner linkage mismatch");
  expected = reference(publication);
  append(expected, std::uint8_t{0});
  check(linked(records, 1705, publication).body == expected,
        "actual append return mismatch");
  const auto &callback = linked(records, 1709, edit);
  expected = reference(callback);
  append(expected, std::uint8_t{3});
  append(expected, ErrorCode::io);
  append(expected, std::int64_t{91});
  check(linked(records, 1710, callback).body == expected,
        "swallowed callback native error missing");
  unsigned requests = 0, terminals = 0, duplicates = 0, state_publications = 0;
  unsigned replay_prepare = 0, replay_commit = 0, codec_failures = 0, oversized = 0;
  unsigned prepared_returns = 0, read_returns = 0, snapshot_returns = 0;
  const auto populations = [](std::vector<std::byte> &body) {
    append(body, std::size_t{1});
    for (unsigned n = 0; n < 4; ++n)
      append(body, std::size_t{0});
  };
  const auto no_return = [&](std::uint32_t site, const Record &request) {
    const auto ref = reference(request);
    check(std::none_of(records.begin(), records.end(),
                       [&](const Record &other) {
                         return other.site == site && other.body.size() >= ref.size() &&
                                std::equal(ref.begin(), ref.end(), other.body.begin());
                       }),
          "failed owner fabricated successful return");
  };
  const auto parent_request = [&](const Record &child, std::uint8_t action) {
    check(child.body.size() == 34 && native<bool>(child.body, 1),
          "missing actual child parent");
    const Record *found = nullptr;
    for (const auto &request : records)
      if (request.site == 1700 && native<std::uint8_t>(request.body, 0) == action) {
        const auto ref = reference(request);
        if (std::equal(ref.begin(), ref.end(), child.body.begin() + 2)) {
          check(!found, "ambiguous parent request");
          found = &request;
        }
      }
    check(found && found->ordinal < child.ordinal,
          "child references wrong actual owner");
  };
  for (const auto &record : records) {
    check(record.origin.lane == edit.origin.lane,
          "unexpected mixed producer chronology");
    check(record.origin.partition == (record.site == 1701 || record.site == 1712
                                          ? RawPartition::data
                                          : RawPartition::control),
          "wrong task partition");
    if (record.site == 1700) {
      ++requests;
      const auto action = native<std::uint8_t>(record.body, 0);
      const auto &terminal = linked(records, 1711, record);
      check(terminal.ordinal > record.ordinal, "terminal precedes request");
      const auto branch = native<std::uint8_t>(terminal.body, 24);
      auto expected_return = reference(record);
      std::uint32_t return_site = 0;
      if (action == 1 || action == 8) {
        if (branch == 0) {
          return_site = 1702;
          append(expected_return, std::uint64_t{1});
          append(expected_return, std::uint64_t{2});
          append(expected_return, std::size_t{1}); // changed
          append(expected_return, std::size_t{0}); // removed
          append(expected_return, true);           // staged order present
          append(expected_return, std::size_t{1});
          append(expected_return, std::size_t{1}); // receipts
          populations(expected_return);
          ++prepared_returns;
        } else
          no_return(1702, record);
        if (action == 8)
          parent_request(record, 5);
      } else if (action == 2) {
        check(branch == 0 || branch == 2, "unexpected read result branch");
        return_site = 1707;
        append(expected_return, std::uint64_t{1});
        append(expected_return, std::size_t{1});
        populations(expected_return);
        append(expected_return, branch == 2);
        if (branch == 0) {
          append(expected_return, std::uint64_t{0});
          append(expected_return, std::size_t{1}); // cursor
          append(expected_return, std::size_t{1}); // returned rows
        }
        ++read_returns;
      } else if (action == 3) {
        check(branch == 0, "actual snapshot did not return");
        return_site = 1708;
        append(expected_return, std::uint64_t{1});
        append(expected_return, std::uint64_t{2});
        for (unsigned n = 0; n < 3; ++n)
          append(expected_return, std::size_t{1});
        ++snapshot_returns;
      } else if (action == 4 || action == 7 || action == 9) {
        if (branch == 0) {
          return_site = 1706;
          append(expected_return, std::uint64_t{1});
          append(expected_return, std::uint64_t{2});
          for (unsigned n = 0; n < 3; ++n)
            append(expected_return, std::size_t{1});
          populations(expected_return);
        } else
          no_return(1706, record);
        if (action == 7)
          parent_request(record, 6);
        if (action == 9)
          parent_request(record, 5);
      } else if (action == 5 && branch != 0)
        no_return(1706, record);
      if (return_site) {
        const auto &returned = linked(records, return_site, record);
        check(returned.body == expected_return && returned.ordinal > record.ordinal &&
                  returned.ordinal < terminal.ordinal,
              "actual owner return native body/link/order mismatch");
      }
      if (action == 8)
        ++replay_prepare;
      if (action == 9)
        ++replay_commit;
      if (action == 1 && native<std::uint8_t>(terminal.body, 24) == 3 &&
          terminal.body.size() == 35 &&
          native<ErrorCode>(terminal.body, 26) == ErrorCode::capacity) {
        ++codec_failures;
        const auto ref = reference(record);
        check(std::none_of(records.begin(), records.end(),
                           [&](const Record &other) {
                             return other.site == 1701 && other.body.size() >= 24 &&
                                    std::equal(ref.begin(), ref.end(),
                                               other.body.begin());
                           }),
              "failed encoder fabricated source bytes");
      }
    }
    if (record.site == 1711)
      ++terminals;
    if (record.site == 1703)
      ++duplicates;
    if (record.site == 1706) {
      ++state_publications;
      check(record.body.size() == 104 && native<std::uint64_t>(record.body, 24) == 1 &&
                native<std::uint64_t>(record.body, 32) == 2 &&
                native<std::size_t>(record.body, 40) == 1 &&
                native<std::size_t>(record.body, 48) == 1 &&
                native<std::size_t>(record.body, 56) == 1 &&
                native<std::size_t>(record.body, 64) == 1,
            "native state publication fields mismatch");
      for (std::size_t offset = 72; offset < 104; offset += sizeof(std::size_t))
        check(native<std::size_t>(record.body, offset) == 0,
              "native status population mismatch");
    }
    if (record.site == 1701 && native<std::size_t>(record.body, 24) > 65536) {
      ++oversized;
      const auto length = native<std::size_t>(record.body, 24);
      check(length == record.body.size() - 32 &&
                std::all_of(record.body.end() - 70000, record.body.end(),
                            [](std::byte b) { return b == std::byte{'x'}; }),
            "oversized business input not preserved before refusal");
    }
    if (record.site == 1712)
      check(linked(records, 1713, record).ordinal > record.ordinal,
            "operator result absent");
  }
  const auto site_count = [&](std::uint32_t site) {
    return static_cast<std::size_t>(
        std::count_if(records.begin(), records.end(),
                      [&](const Record &record) { return record.site == site; }));
  };
  check(site_count(1702) == prepared_returns && site_count(1707) == read_returns &&
            site_count(1708) == snapshot_returns,
        "orphan prepared/read/snapshot return");
  check(requests == terminals && duplicates == 1 && state_publications == 3 &&
            replay_prepare == 1 && replay_commit == 1 && codec_failures == 1 &&
            oversized == 1 && prepared_returns == 2 && read_returns == 2 &&
            snapshot_returns == 3,
        "task branch census differs from actual executed branches");
}

struct RamLane {
  std::string name;
  RawChannel channel;
  RawProducer producer;
  static inline unsigned count = 0;
  explicit RamLane(std::uint64_t data)
      : name("/blackbird-task-fixture-" + std::to_string(::getpid()) + "-" +
             std::to_string(++count)),
        channel(unwrap(RawChannel::create(name, IdentityBytes{std::byte{67}},
                                          {data, 16384, 256}))),
        producer(channel) {}
  ~RamLane() { (void)RawChannel::unlink(name); }
};
void acquisition_refusal() {
  RamLane lane{1024};
  RawThreadBinding binding{lane.producer, lane.channel.epoch()};
  TaskState state;
  error_is([&] { (void)state.prepare(task_request("small-lane", "task")); },
           ErrorCode::capacity);
  check(state.revision() == 0 &&
            unwrap(lane.channel.cursors(RawPartition::data)).published == 0,
        "failed DATA acquisition encoded/published source");
  const auto control = unwrap(lane.channel.read(RawPartition::control, 0, 16384));
  check(control.bytes.size() == 77 && native<std::uint32_t>(control.bytes, 4) == 1700 &&
            native<std::uint32_t>(control.bytes, 30) == 1711 &&
            native<std::uint8_t>(control.bytes, 66) == 3 &&
            native<std::uint8_t>(control.bytes, 67) == 2 &&
            native<ErrorCode>(control.bytes, 68) == ErrorCode::capacity,
        "failed DATA acquisition lost held native terminal");
  check(unwrap(lane.producer.available(RawPartition::control)) == 16384 - 77,
        "failed owner leaked held outcomes");
}
void publication_cut(const char *path, bool write_failure) {
  RamLane lane{32 * 1024 * 1024};
  RawThreadBinding binding{lane.producer, lane.channel.epoch()};
  const std::string name = write_failure ? "task-write-refusal" : "task-post-sync";
  const JournalHeader header{id<EnvironmentId>(1),
                             id<AuditStreamId>(72),
                             3,
                             {1024 * 1024, 4 * 1024 * 1024},
                             std::nullopt};
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                       unwrap(NativeJournalDirectory::open(path))),
                                   name, header, {64 * 1024 * 1024, 20000}));
  AuditLog log{*root};
  TaskStore tasks{log};
  unsigned callbacks = 0;
  tasks.changed = [&](const Value &) { ++callbacks; };
  // Issue the actual business identity reservoir before arming the append cut.
  (void)log.issue();
  struct stat metadata{};
  check(::stat((std::filesystem::path{path} / name).c_str(), &metadata) == 0,
        "actual product audit absent");
  std::array<int, 2> cancellation{};
  check(::pipe(cancellation.data()) == 0, "native cancellation pipe failed");
  const auto before = root->cursor().sequence;
  cut_channel = &lane.channel;
  cut_producer = &lane.producer;
  cut_device = metadata.st_dev;
  cut_inode = metadata.st_ino;
  cancel_write = cancellation[1];
  cut_write_failure = write_failure;
  cut_sync_cancel = !write_failure;
  cut_fired = false;
  bool refused = false;
  Error actual{ErrorCode::external_unknown};
  {
    RawWaitScope wait{{{cancellation[0], -1}, std::nullopt}};
    try {
      (void)tasks.edit(task_request("publication-cut", "task"));
    } catch (const Error &error) {
      refused = true;
      actual = error;
    }
    cut_channel = nullptr;
    cut_producer = nullptr;
    cut_write_failure = cut_sync_cancel = false;
  }
  (void)::close(cancellation[0]);
  (void)::close(cancellation[1]);
  if (!cut_fired)
    std::fprintf(stderr, "unreached native cut: write=%d refused=%d error=%s\n",
                 write_failure, refused, error_name(actual.code));
  check(cut_fired, "native publication cut was not reached");
  std::fprintf(stderr,
               "publication cut: write=%d refused=%d error=%s/%lld revision=%llu "
               "callbacks=%u cursor=%llu/%llu\n",
               write_failure, refused, error_name(actual.code),
               static_cast<long long>(actual.detail),
               static_cast<unsigned long long>(tasks.state().revision()), callbacks,
               static_cast<unsigned long long>(root->cursor().sequence),
               static_cast<unsigned long long>(before));
  check(write_failure ? refused && actual.code == ErrorCode::io &&
                            actual.detail == EIO && tasks.state().revision() == 0 &&
                            callbacks == 0 && root->cursor().sequence == before
                      : !refused && tasks.state().revision() == 1 && callbacks == 1 &&
                            root->cursor().sequence > before,
        "actual append refusal/prepaid post-append behavior mismatch");
  check(unwrap(lane.producer.available(RawPartition::data)) > 31 * 1024 * 1024,
        "small request retained its unused16MiB DATA allowance");
  const auto control = unwrap(lane.channel.read(RawPartition::control, 0, 16384));
  unsigned publication_returns = 0, state_returns = 0, edit_terminals = 0;
  std::uint64_t edit_ordinal = 0;
  for (std::size_t offset = 0; offset < control.bytes.size();) {
    const auto size = native<std::uint32_t>(control.bytes, offset);
    const auto site = native<std::uint32_t>(control.bytes, offset + 4);
    const ByteView body = ByteView{control.bytes}.subspan(offset + 16, size - 16);
    if (site == 1700 && native<std::uint8_t>(body, 0) == 6)
      edit_ordinal = native<std::uint64_t>(control.bytes, offset + 8);
    if (site == 1705) {
      ++publication_returns;
      check(native<std::uint8_t>(body, 24) == (write_failure ? 3 : 0),
            "wrong actual append result branch");
      if (write_failure)
        check(native<ErrorCode>(body, 25) == ErrorCode::io &&
                  native<std::int64_t>(body, 26) == EIO,
              "append refusal original native error missing");
    }
    if (site == 1706)
      ++state_returns;
    if (site == 1711 && native<std::uint64_t>(body, 16) == edit_ordinal) {
      ++edit_terminals;
      check(native<std::uint8_t>(body, 24) == (write_failure ? 3 : 0),
            "lost prepaid edit outcome");
    }
    offset += size;
  }
  check(publication_returns == 1 && state_returns == (write_failure ? 0U : 1U) &&
            edit_terminals == 1,
        "publication cut outcome linkage/count mismatch");
  const auto cursors = unwrap(lane.channel.cursors(RawPartition::control));
  check(unwrap(lane.producer.available(RawPartition::control)) ==
            16384 - cursors.published,
        "publication cut leaked CONTROL claims");
}

} // namespace
int main(int argc, char **argv) {
  char pattern[] = "/tmp/bb-task-raw-XXXXXX";
  const char *path = ::mkdtemp(pattern);
  if (!path)
    return 2;
  int result = 0;
  try {
    if (argc > 1 && std::string_view{argv[1]} == "surfaces") {
      raw_surfaces(path);
      std::filesystem::remove_all(path);
      return 0;
    }
    const JournalHeader header{id<EnvironmentId>(1),
                               id<AuditStreamId>(2),
                               3,
                               {1024 * 1024, 4 * 1024 * 1024},
                               std::nullopt};
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                         unwrap(NativeJournalDirectory::open(path))),
                                     "audit", header, {64 * 1024 * 1024, 20000}));
    AuditLog log{*root};
    TaskStore tasks{log};
    unsigned callbacks = 0;
    tasks.changed = [&](const Value &) { ++callbacks; };
    const auto request = Value::object(
        {{"op_id", Value{"seed"}},
         {"base", Value{Number{0}}},
         {"ops", Value{Value::Array{Value::object(
                     {{"op", Value{"add"}}, {"title", Value{"task"}}})}}}});
    watched_root = root.get();
    watched_state = &tasks.state();
    // Identity-reservoir publication is not acceptance of the task delta.
    // Establish it before selecting the actual task append interval.
    (void)log.issue();
    before_sequence = root->cursor().sequence;
    fail_after_append = true;
    bool refused = false;
    try {
      (void)tasks.edit(request);
    } catch (const std::bad_alloc &) {
      refused = true;
    }
    fail_after_append = false;
    watched_root = nullptr;
    watched_state = nullptr;
    const bool appended = root->cursor().sequence > before_sequence;
    std::fprintf(stderr,
                 "post-append allocation: size=%zu refused=%d appended=%d "
                 "revision=%llu callbacks=%u\n",
                 failed_size, refused, appended,
                 static_cast<unsigned long long>(tasks.state().revision()), callbacks);
    if (!appended || refused || tasks.state().revision() != 1 || callbacks != 1)
      result = 1;
    if (result == 0) {
      raw_surfaces(path);
      acquisition_refusal();
      publication_cut(path, true);
      publication_cut(path, false);
    }
  } catch (const Error &error) {
    fail_after_append = false;
    std::fprintf(stderr, "setup/operation error %s (%lld)\n", error_name(error.code),
                 static_cast<long long>(error.detail));
    result = 2;
  } catch (const std::exception &error) {
    fail_after_append = false;
    std::fprintf(stderr, "setup/operation exception %s\n", error.what());
    result = 2;
  } catch (...) {
    fail_after_append = false;
    result = 2;
  }
  std::filesystem::remove_all(path);
  return result;
}
