#include "blackbird/raw_file.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/tools.hpp"
#include "raw_file_stdio.hpp"
#include <atomic>
#include <csignal>
#include <cstdarg>
#include <sys/mman.h>
#include <thread>
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <new>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace blackbird;
namespace {
using NativeRead = ssize_t (*)(int, void *, std::size_t);
using NativeCwd = char *(*)(char *, std::size_t);
NativeCwd native_cwd = nullptr;
using NativeFread = std::size_t (*)(void *, std::size_t, std::size_t, FILE *);
NativeRead native_read = nullptr;
NativeFread native_fread = nullptr;
using NativeOpen = int (*)(const char *, int, ...);
NativeOpen native_open = nullptr;
using NativeWrite = ssize_t (*)(int, const void *, std::size_t);
using NativeInt = int (*)(int);
using NativeRename = int (*)(const char *, const char *);
using NativeUnlink = int (*)(const char *);
using NativeTemporary = int (*)(char *);
NativeWrite native_write = nullptr;
NativeInt native_close = nullptr, native_sync = nullptr;
NativeRename native_rename = nullptr;
NativeUnlink native_unlink = nullptr;
NativeTemporary native_temporary = nullptr;
struct CwdFact {
  std::size_t capacity;
  bool success;
  int error;
  std::vector<std::byte> bytes;
};
bool tracking_cwd = false;
std::vector<CwdFact> cwd_facts;
#if defined(__GLIBCXX__)
using NativeStat = int (*)(const char *, struct stat *);
using NativeRealpath = char *(*)(const char *, char *);
using NativeReadlink = ssize_t (*)(const char *, char *, std::size_t);
NativeStat native_stat = nullptr, native_lstat = nullptr;
NativeRealpath native_realpath = nullptr;
NativeReadlink native_readlink = nullptr;
std::string fallback_path, fallback_link, fallback_target;
std::optional<off_t> fallback_size;
std::size_t fallback_lstats = 0, fallback_links = 0;
std::vector<std::size_t> fallback_capacities;
bool fallback_full = false, fallback_final_race = false;
#endif
enum class Cut {
  none,
  first_error,
  partial_error,
  interrupted,
  short_eof,
  short_write,
  interrupted_write,
  zero_write,
  write_error,
  sync_error,
  close_error,
  rename_error,
  unlink_error,
  cancel_temporary,
  cancel_close,
  cancel_rename,
  allocation,
  capture_open,
  capture_temporary,
  capture_close,
  capture_rename,
  capture_close_unlink,
  first_closed,
  partial_closed
};
Cut cut = Cut::none;
int cancellation_endpoint = -1;
struct stat temporary_identity{};
char temporary_name[PATH_MAX]{};
int temporary_fd = -1;
std::size_t read_calls = 0, write_calls = 0, close_calls = 0, unlink_calls = 0;
std::size_t rename_calls = 0, sync_calls = 0;
std::atomic<std::uint64_t> *publication_word = nullptr;
std::uint64_t original_publication = 0;
bool publication_damaged = false;
void damage_publication() {
  if (!publication_word || publication_damaged)
    std::abort();
  original_publication = publication_word->load(std::memory_order_acquire);
  publication_word->store(original_publication + 1, std::memory_order_release);
  publication_damaged = true;
}
void restore_publication() {
  if (publication_damaged) {
    publication_word->store(original_publication, std::memory_order_release);
    publication_damaged = false;
  }
}
std::size_t stdio_calls = 0;
bool stdio_selected = false;
int stdio_error = 0;
bool allocation_ready = false;
std::size_t allocation_floor = 8193;
std::vector<std::size_t> prefix_requests;
bool prefix_observation = false;
thread_local bool allocation_fired = false;
bool same_inode(int fd, const struct stat &expected) {
  struct stat actual{};
  return ::fstat(fd, &actual) == 0 && actual.st_dev == expected.st_dev &&
         actual.st_ino == expected.st_ino;
}
void require(bool, const char *);
extern bool observing;
bool temporary_matches(int fd) {
  return observing && temporary_fd >= 0 && same_inode(fd, temporary_identity);
}
void cancel_native() {
  require(cancellation_endpoint >= 0, "actual owned cancellation endpoint");
  require(::shutdown(cancellation_endpoint, SHUT_WR) == 0,
          "actual native sticky cancellation EOF");
}
struct stat selected{};
bool observing = false;
thread_local bool inside_fread = false;
std::vector<std::byte> acquired;
void require(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error{message};
}
bool matches(int fd) {
  struct stat actual{};
  return observing && ::fstat(fd, &actual) == 0 && actual.st_dev == selected.st_dev &&
         actual.st_ino == selected.st_ino;
}
void preserved(void *bytes, std::size_t size) {
  const auto *first = static_cast<std::byte *>(bytes);
  acquired.insert(acquired.end(), first, first + size);
}
template <class T> T take(ByteView &body) {
  require(body.size() >= sizeof(T), "actual native field exists");
  T result{};
  std::memcpy(&result, body.data(), sizeof(result));
  body = body.subspan(sizeof(result));
  return result;
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char name[] = "/tmp/bb-file-native-XXXXXX";
    const auto *created = ::mkdtemp(name);
    require(created, "native file fixture directory");
    path = created;
  }
  ~Directory() {
    if (std::uncaught_exceptions())
      std::cerr << "preserved file fixture " << path << '\n';
    else
      std::filesystem::remove_all(path);
  }
};
struct Record {
  RawEventRef ref;
  std::uint32_t site;
  std::vector<std::byte> body;
};
std::vector<Record> records(const Directory &directory, bool failed_capture = false) {
  const auto runs = unwrap(discover_raw_runs(directory.path.string()));
  require(runs.size() == 1, "actual native raw run present");
  const auto &snapshot = runs[0].snapshot;
  if (!snapshot)
    throw std::runtime_error{"actual native raw snapshot missing"};
  require(snapshot->closed, "actual native raw run closed");
  auto reader =
      unwrap(RawRunReader::open((directory.path / "raw" / runs[0].name).string()));
  std::vector<std::byte> output(32 * 1024 * 1024);
  std::vector<Record> result;
  while (const auto record = unwrap(reader->read(output))) {
    auto body = record->raw;
    require(take<std::uint32_t>(body) == record->raw.size(),
            "exact raw envelope length");
    const auto site = take<std::uint32_t>(body);
    const auto ordinal = take<std::uint64_t>(body);
    require(failed_capture || (site != 8001 && site != 8000),
            "no failed native ingestion");
    result.push_back(
        {{record->origin.lane, ordinal}, site, {body.begin(), body.end()}});
  }
  return result;
}
RawEventRef ref(ByteView &body) {
  return {take<IdentityBytes>(body), take<std::uint64_t>(body)};
}
bool equal(RawEventRef a, RawEventRef b) {
  return a.lane == b.lane && a.ordinal == b.ordinal;
}
std::vector<std::byte> originals(const std::vector<Record> &events,
                                 std::uint8_t wanted) {
  std::vector<std::byte> result;
  for (const auto &event : events) {
    if (event.site != 3407)
      continue;
    ByteView body{event.body};
    const auto request = ref(body);
    require(request.ordinal < event.ref.ordinal && request.lane == event.ref.lane,
            "original references real earlier request on actual producer");
    const auto role = take<std::uint8_t>(body);
    const auto extent = take<std::size_t>(body);
    if (role >= 2 && role <= 4) {
      if (take<bool>(body))
        require(take<std::size_t>(body) == extent, "known CString native extent");
    }
    require(extent == body.size(), "actual unchanged original extent");
    if (role == wanted)
      result.insert(result.end(), body.begin(), body.end());
  }
  return result;
}
template <class F>
std::vector<Record>
capture(Directory &directory, F &&action, std::uint64_t control = 256 * 1024,
        std::uint64_t data = 64 * 1024 * 1024, bool failed_capture = false) {
  IdentityBytes id{};
  id[0] = std::byte{52};
  {
    auto runtime = unwrap(RawRuntime::open(directory.path.string(),
                                           unwrap(EnvironmentId::from_bytes(id)),
                                           {{data, control, 4096}, 4096}));
    auto thread = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{thread};
      const std::byte marker{7};
      const ByteView parts[]{ByteView{&marker, 1}};
      const RawEventRef parent{raw_lane_epoch(), unwrap(raw_emit(9200, parts))};
      const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
      action(context);
    }
    thread.reset();
  }
  return records(directory, failed_capture);
}
void select(const std::filesystem::path &file, Cut requested) {
  require(::stat(file.c_str(), &selected) == 0, "independent selected inode");
  cut = requested;
  observing = true;
  acquired.clear();
  acquired.reserve(32 * 1024 * 1024);
  read_calls = write_calls = close_calls = unlink_calls = rename_calls = sync_calls = 0;
  stdio_calls = 0;
  stdio_selected = false;
  stdio_error = 0;
  temporary_fd = -1;
  temporary_name[0] = '\0';
  allocation_ready = false;
  allocation_fired = false;
}
void stop() {
  observing = false;
  cut = Cut::none;
  allocation_ready = false;
}
std::string disk(const std::filesystem::path &file) {
  std::ifstream input{file, std::ios::binary};
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
void fixture_file(const std::filesystem::path &file, std::string_view content) {
  std::ofstream output{file, std::ios::binary};
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
}
const Record &find(const std::vector<Record> &events, std::uint32_t site) {
  const auto it = std::find_if(events.begin(), events.end(),
                               [&](const auto &value) { return value.site == site; });
  require(it != events.end(), "reached native event exists");
  return *it;
}
std::size_t count(const std::vector<Record> &events, std::uint32_t site) {
  return static_cast<std::size_t>(
      std::count_if(events.begin(), events.end(),
                    [&](const auto &value) { return value.site == site; }));
}
void linked(const std::vector<Record> &events, std::uint32_t request_site,
            std::uint32_t returned_site) {
  require(count(events, request_site) == count(events, returned_site),
          "native request and return remain distinct balanced events");
  for (const auto &event : events) {
    if (event.site != returned_site)
      continue;
    ByteView body{event.body};
    const auto actual = ref(body);
    require(std::any_of(events.begin(), events.end(),
                        [&](const auto &request) {
                          return request.site == request_site &&
                                 equal(actual, request.ref) &&
                                 request.ref.ordinal < event.ref.ordinal;
                        }),
            "native return references actual earlier request");
  }
}
void read_faults() {
  const std::string source(16384, '\xA5');
  for (const auto fault : {Cut::none, Cut::first_error, Cut::partial_error,
                           Cut::interrupted, Cut::short_eof, Cut::allocation}) {
    Directory directory;
    const auto file = directory.path / "source";
    fixture_file(file, source);
    std::string answer;
    bool io = false, allocation = false;
    const auto events = capture(directory, [&](const RawAdapterContext &context) {
      select(file, fault);
      try {
        answer = read_file(file, source.size(), &context);
      } catch (const Error &error) {
        io = error == Error{ErrorCode::io};
      } catch (const std::bad_alloc &) {
        allocation = true;
      }
      stop();
    });
    require(close_calls == 1,
            "one actual read resource close after every business outcome");
    require(originals(events, 1) == acquired,
            "exact acquired prefix retained before native/business failure");
    linked(events, 3420, 3421);
    linked(events, 3422, 3423);
    linked(events, 3410, 3411);
    require(count(events, 3444) == 1,
            "actual acquired read resource has one ownership END");
    if (fault == Cut::allocation) {
      require(allocation && allocation_fired && acquired.size() == 8192,
              "actual post-read business append allocation cut preserves prefix");
      ByteView body{find(events, 3411).body};
      (void)ref(body);
      require(take<RawFileBranch>(body) == RawFileBranch::allocation && body.empty(),
              "allocation terminal has no fabricated Error fields");
    } else if (fault == Cut::short_eof) {
      require(!io && answer == source.substr(0, 7) && read_calls == 2,
              "positive short prefix followed by actual EOF returns original prefix");
    } else if (fault == Cut::first_error || fault == Cut::partial_error ||
               fault == Cut::interrupted) {
#if defined(__GLIBCXX__)
      if (fault == Cut::interrupted)
        require(!io && answer == source && read_calls == 4,
                "GNU stream EINTR retries preserve business bytes");
      else
        require(io && answer.empty(),
                "GNU native error preserves prior business exception mapping");
#else
      require(!io && answer == (fault == Cut::partial_error ? source.substr(0, 7)
                                                            : std::string{}),
              "libc++ char-filebuf native error remains EOF/prefix business result");
#endif
      bool failure = false;
      for (const auto &event : events) {
        if (event.site != 3423)
          continue;
        ByteView body{event.body};
        (void)ref(body);
        const auto returned = take<ssize_t>(body);
        const auto failed = take<bool>(body);
        if (returned < 0) {
          require(failed &&
                      take<int>(body) == (fault == Cut::interrupted ? EINTR : EIO) &&
                      body.empty(),
                  "actual negative read return and errno preserved separately from "
                  "business EOF");
          failure = true;
        }
      }
      require(failure, "actual native read failure remains audit event even on "
                       "successful business prefix");
    } else
      require(answer == source && acquired.size() == source.size(),
              "exact two business batches and EOF");
  }
}
void write_faults() {
  const std::string before = "before";
  const std::string after{"a\0b\xff"
                          "c",
                          5};
  for (const auto fault :
       {Cut::none, Cut::short_write, Cut::interrupted_write, Cut::zero_write,
        Cut::write_error, Cut::sync_error, Cut::close_error, Cut::rename_error,
        Cut::unlink_error, Cut::cancel_temporary, Cut::cancel_close,
        Cut::cancel_rename}) {
    Directory directory;
    const auto file = directory.path / "target";
    fixture_file(file, before);
    require(::chmod(file.c_str(), 0600) == 0, "native prior mode");
    std::optional<Error> failure;
    int endpoints[2]{-1, -1};
    require(::socketpair(AF_UNIX, SOCK_STREAM, 0, endpoints) == 0,
            "native cancellation socketpair");
    cancellation_endpoint = endpoints[0];
    const auto events = capture(directory, [&](const RawAdapterContext &context) {
      select(file, fault);
      RawWaitScope cancelled{{{endpoints[1], -1}, {}}};
      try {
        write_file(file, after, &context);
      } catch (const Error &error) {
        failure = error;
      }
      stop();
    });
    require(native_close(endpoints[0]) == 0 && native_close(endpoints[1]) == 0,
            "fixture owns cancellation endpoints until raw owner finished");
    cancellation_endpoint = -1;
    const bool success = fault == Cut::none || fault == Cut::short_write ||
                         fault == Cut::interrupted_write || fault == Cut::cancel_rename;
    require(success == !failure.has_value(),
            "actual native write failure/cancellation maps to existing owner outcome");
    require(disk(file) == (success ? after : before),
            "actual atomic replacement remains unchanged on prior failure");
    require(
        close_calls == 1,
        "actual temporary descriptor consumed once before capture or further effects");
    require(count(events, 3438) == 1 && count(events, 3439) == 1 &&
                count(events, 3444) == 1,
            "held close request/return/END survive original business failure and "
            "sticky cancellation");
    require(unlink_calls == (success ? 0u : 1u),
            "only owned original temporary name gets cleanup attempt");
    require(originals(events, 6) ==
                std::vector<std::byte>(
                    reinterpret_cast<const std::byte *>(after.data()),
                    reinterpret_cast<const std::byte *>(after.data() + after.size())),
            "native write references exactly one unchanged proposed binary body");
    const auto &creation = find(events, 3430);
    const auto &ended = find(events, 3444);
    ByteView ending{ended.body};
    require(equal(ref(ending), creation.ref),
            "END names actual successful mkstemp request");
    require(take<std::uint8_t>(ending) == (success ? 2 : 3) && ending.empty(),
            "END states actual ownership transition without inferring syscall success");
    ByteView closed{find(events, 3439).body};
    require(equal(ref(closed), creation.ref) && take<bool>(closed),
            "close return names actual resource and captured request");
    const auto close_request = ref(closed);
    require(equal(close_request, find(events, 3438).ref),
            "close native return references real close request");
    const int returned = take<int>(closed);
    const bool failed = take<bool>(closed);
    require(
        returned == (fault == Cut::close_error ? -1 : 0) &&
            failed == (fault == Cut::close_error),
        "once-only close records actual returned value even if lexical ownership ends");
    if (failed)
      require(take<int>(closed) == EIO, "actual close errno");
    require(closed.empty(), "close has no extra invented fields");
    if (fault == Cut::unlink_error) {
      require(failure == Error{ErrorCode::io, EIO},
              "original write failure survives cleanup unlink failure");
      ByteView unlinked{find(events, 3443).body};
      (void)ref(unlinked);
      require(take<bool>(unlinked), "unlink captured real request");
      (void)ref(unlinked);
      require(take<int>(unlinked) == -1 && take<bool>(unlinked) &&
                  take<int>(unlinked) == EACCES && unlinked.empty(),
              "distinct actual failed unlink retained");
      require(disk(temporary_name).empty(),
              "failed unlink leaves actual empty temporary for inspection");
      require(native_unlink(temporary_name) == 0,
              "fixture removes deliberately failed cleanup remainder");
    } else if (fault == Cut::cancel_rename) {
      require(
          disk(temporary_name) == "new",
          "consumed temporary name is never unlinked after actual successful rename");
      require(native_unlink(temporary_name) == 0,
              "fixture owns later replacement inode");
    } else
      require(!std::filesystem::exists(temporary_name),
              "original owned temporary removed or renamed");
    if (success) {
      struct stat actual{};
      require(::stat(file.c_str(), &actual) == 0 && (actual.st_mode & 0777) == 0600,
              "native replacement preserves existing mode");
      linked(events, 3434, 3435);
      linked(events, 3436, 3437);
      linked(events, 3440, 3441);
      const auto &input = find(events, 3407);
      for (const auto &event : events) {
        if (event.site != 3434)
          continue;
        ByteView body{event.body};
        (void)ref(body);
        require(equal(ref(body), creation.ref), "write resource actual mkstemp");
        require(take<std::uint8_t>(body) == 8, "actual native write opcode");
        require(take<int>(body) == temporary_fd, "actual native descriptor argument");
        const auto remaining = take<std::size_t>(body);
        require(equal(ref(body), input.ref), "write names already owned proposed body");
        const auto offset = take<std::size_t>(body);
        require(remaining == after.size() - offset && body.empty(),
                "write offset/remaining are original business locals");
      }
    }
  }
}
void paths_and_tools() {
  Directory directory;
  const auto file = directory.path / "binary";
  const std::string source{"left\0old\nright", 14};
  fixture_file(file, source);
  const auto alias = directory.path / "alias";
  std::filesystem::create_symlink(file.filename(), alias);
  bool observed = false;
  const auto events = capture(directory, [&](const RawAdapterContext &context) {
    std::error_code error;
    require(raw_file_canonical(alias, error, &context) ==
                    std::filesystem::canonical(file) &&
                !error,
            "explicit mechanical canonical retains native symlink result");
    LocalTools tool{[&](std::string_view label, std::string_view bytes) {
      observed = true;
      require(label == "file.before" && bytes == source,
              "actual observer gets unchanged whole before");
      throw Error{ErrorCode::conflict, 19};
    }};
    bool thrown = false;
    try {
      (void)tool.run(
          "write_file",
          Value::object({{"path", Value{alias.string()}}, {"content", Value{"new"}}}),
          &context);
    } catch (const Error &error) {
      thrown = error == Error{ErrorCode::conflict, 19};
    }
    require(thrown && observed && disk(file) == source,
            "actual observer failure precedes temporary/effect");
    LocalTools edits;
    (void)edits.run("edit_file",
                    Value::object({{"path", Value{alias.string()}},
                                   {"old", Value{"old"}},
                                   {"new", Value{"updated"}}}),
                    &context);
    require(disk(file) == std::string{"left\0updated\nright", 18},
            "binary edit through actual symlink preserves untouched bytes");
    fixture_file(file, "duplicate duplicate");
    bool conflict = false;
    try {
      (void)edits.run("edit_file",
                      Value::object({{"path", Value{file.string()}},
                                     {"old", Value{"duplicate"}},
                                     {"new", Value{"x"}}}),
                      &context);
    } catch (const Error &error) {
      conflict = error == Error{ErrorCode::conflict};
    }
    require(conflict && disk(file) == "duplicate duplicate",
            "existing duplicate-edit conflict has no native write effect");
    const std::filesystem::path embedded{file.string() + std::string{"\0suffix", 7}};
    require(read_file(embedded, 64, &context) == "duplicate duplicate",
            "native operand stops at first NUL while business path retains suffix");
    (void)edits.run("read_file", Value::object({{"path", Value{embedded.string()}}}),
                    &context);
    auto invalid_bytes = (directory.path / "invalid-").string();
    invalid_bytes.push_back('\xff');
    const std::filesystem::path invalid{invalid_bytes};
    std::error_code old_error, actual_error;
    const auto old_path = std::filesystem::canonical(invalid, old_error);
    require(raw_file_canonical(invalid, actual_error, &context) == old_path &&
                actual_error.value() == old_error.value(),
            "invalid UTF8 operand preserves native platform outcome without invented "
            "acceptance");
  });
  const auto completed = originals(events, 6);
  const std::string expected_proposals =
      "new" + std::string{"left\0updated\nright", 18};
  require(completed ==
              std::vector<std::byte>(
                  reinterpret_cast<const std::byte *>(expected_proposals.data()),
                  reinterpret_cast<const std::byte *>(expected_proposals.data() +
                                                      expected_proposals.size())),
          "completed write proposal survives throwing file.before; actual edit output "
          "emitted once");
  const auto proposal =
      std::find_if(events.begin(), events.end(), [](const auto &event) {
        if (event.site != 3407)
          return false;
        ByteView body{event.body};
        (void)ref(body);
        return take<std::uint8_t>(body) == 6;
      });
  require(
      proposal != events.end() &&
          proposal->ref.ordinal < find(events, 3420).ref.ordinal,
      "write proposal is preserved before subsequent file-before source acquisition");
  linked(events, 3400, 3401);
  linked(events, 3405, 3406);
  linked(events, 3410, 3411);
  require(count(events, 3430) == 1,
          "only actual successful edit reaches temporary acquisition");
  bool whole_nul = false, native_nul = false;
  for (const auto &event : events) {
    if (event.site == 3420) {
      ByteView body{event.body};
      (void)ref(body);
      (void)ref(body);
      (void)take<std::uint8_t>(body);
      (void)take<int>(body);
      require(!take<bool>(body), "read open has no native mode operand");
      const auto extent = take<std::size_t>(body);
      require(extent == body.size() && body.back() == std::byte{},
              "actual native CString operand includes its first NUL");
      require(std::find(body.begin(), body.end() - 1, std::byte{}) == body.end() - 1,
              "native operand cannot include business suffix beyond NUL");
      native_nul = true;
    }
    if (event.site == 3402) {
      ByteView body{event.body};
      (void)ref(body);
      (void)take<std::uint8_t>(body);
      (void)take<std::size_t>(body);
      whole_nul =
          whole_nul || std::find(body.begin(), body.end(), std::byte{}) != body.end();
    }
  }
  // The direct mechanical embedded-NUL call has no fabricated semantic argument;
  // a selected tool call below supplies that separate actual business surface.
  require(native_nul && whole_nul,
          "native operand and whole business path remain distinct actual surfaces");
}
void optout_and_capacity() {
  Directory directory;
  const auto file = directory.path / "source";
  fixture_file(file, "data");
  const auto events = capture(directory, [&](const RawAdapterContext &context) {
    require(read_file(file) == "data", "ordinary default helper remains opt-out");
    std::error_code error;
    require(raw_file_canonical(file, error) == std::filesystem::canonical(file) &&
                !error,
            "default canonical opt-out");
    select(file, Cut::none);
    auto held = unwrap(raw_claim_control(256 * 1024 - 120));
    bool refused = false;
    try {
      (void)read_file(file, 64, &context);
    } catch (const Error &failure) {
      refused = failure == Error{ErrorCode::capacity};
    }
    stop();
    require(refused && read_calls == 0 && close_calls == 0,
            "impossible held cleanup grant refuses before any relevant descriptor "
            "acquisition");
  });
  require(
      count(events, 3420) == 0 && count(events, 3422) == 0,
      "opt-out and refused resource acquisition produce no fabricated native events");
}
void exact_admission() {
  for (const auto allowance : {std::uint64_t{364}, std::uint64_t{363}}) {
    Directory directory;
    const auto file = directory.path / "source";
    fixture_file(file, std::string(8192, 'x'));
    bool refused = false;
    const auto events = capture(directory, [&](const RawAdapterContext &context) {
      select(file, Cut::none);
      // Independently summed literal widths: helper terminal50 + held close
      // request69/return74/END41 + read request77/return53 =364.
      auto held = unwrap(raw_claim_control(256 * 1024 - allowance));
      try {
        require(read_file(file, 8192, &context) == std::string(8192, 'x'),
                "exact-credit read bytes");
      } catch (const Error &error) {
        refused = error == Error{ErrorCode::capacity};
      }
      stop();
    });
    require(refused == (allowance == 363),
            "exact independently summed read CONTROL boundary");
    require(read_calls == (allowance == 364 ? 2u : 0u) && close_calls == 1,
            "one-byte-short read pair refuses native acquisition but releases already "
            "acquired fd once");
    require(
        count(events, 3422) == read_calls && count(events, 3439) == 1,
        "exact sufficient/insufficient control grants preserve truthful native events");
  }
  for (const auto allowance : {std::uint64_t{8241}, std::uint64_t{8240}}) {
    Directory directory;
    const auto file = directory.path / "source";
    fixture_file(file, std::string(8192, 'y'));
    bool refused = false;
    const auto events = capture(directory, [&](const RawAdapterContext &context) {
      select(file, Cut::none);
      // envelope16/requestR24/role1/extent8 + actual8192 maximum prefix.
      auto held = unwrap(raw_claim_data(64 * 1024 * 1024 - allowance));
      try {
        (void)read_file(file, 8192, &context);
      } catch (const Error &error) {
        refused = error == Error{ErrorCode::capacity};
      }
      stop();
    });
    require(refused == (allowance == 8240) &&
                read_calls == (allowance == 8241 ? 2u : 0u),
            "literal maximum DATA and maximum-minus-one refuse before acquisition");
    require(originals(events, 1) == acquired,
            "exact DATA credit never loses acquired prefix");
  }
  for (const auto allowance : {std::uint64_t{91}, std::uint64_t{90}}) {
    Directory directory;
    bool reached = false, refused = false;
    const auto events = capture(directory, [&](const RawAdapterContext &context) {
      auto held = unwrap(raw_claim_control(256 * 1024 - allowance));
      try {
        RawFileToolTrace trace{&context, RawFileMethod::read};
        (void)trace.run([&]() -> Value {
          reached = true;
          throw Error{ErrorCode::conflict, 7};
        });
      } catch (const Error &error) {
        refused = error == Error{ErrorCode::capacity};
      }
    });
    require(reached == (allowance == 91) && refused == (allowance == 90),
            "tool's literal41 request plus50 terminal is admitted wholly before body");
    require(count(events, 3400) == (reached ? 1u : 0u) &&
                count(events, 3401) == (reached ? 1u : 0u),
            "no pretend tool request on failed admission");
  }
}
// Test-only mirror of the qualified shared native header; corruption is confined
// to one publication word while the actual recorder/supervisor are stopped.
struct alignas(64) SharedWord {
  std::atomic<std::uint64_t> value;
};
struct SharedPartition {
  SharedWord published, acknowledged;
};
struct SharedHeader {
  SharedWord ready;
  IdentityBytes epoch;
  RawCapacities capacities;
  std::array<std::uint64_t, 3> offsets;
  std::uint64_t mapped_bytes;
  SharedWord data_armed, space_armed;
  std::array<SharedPartition, 3> partitions;
};
struct Publication {
  void *mapping = MAP_FAILED;
  explicit Publication(const Directory &directory) {
    std::array<char, 32> name{};
    for (const auto &run : std::filesystem::directory_iterator{directory.path / "raw"})
      for (const auto &entry : std::filesystem::directory_iterator{run.path()}) {
        if (!entry.path().filename().string().starts_with("lane."))
          continue;
        std::ifstream input{entry.path(), std::ios::binary};
        std::array<char, 80> registered{};
        input.read(registered.data(), registered.size());
        if (input.gcount() != static_cast<std::streamsize>(registered.size()))
          continue;
        if (std::memcmp(registered.data(), raw_lane_epoch().data(), 16) == 0)
          std::memcpy(name.data(), registered.data() + 40, name.size());
      }
    require(name[0] == '/',
            "actual registered producer mapping selected by current epoch");
    const int fd = ::shm_open(name.data(), O_RDWR, 0);
    require(fd >= 0, "actual existing fixture source mapping");
    mapping = ::mmap(nullptr, sizeof(SharedHeader), PROT_READ | PROT_WRITE, MAP_SHARED,
                     fd, 0);
    require(native_close(fd) == 0 && mapping != MAP_FAILED,
            "actual header mapping owned by fixture");
    auto &header = *static_cast<SharedHeader *>(mapping);
    require(header.epoch == raw_lane_epoch() && header.capacities[1] == 256 * 1024 &&
                header.offsets[0] == sizeof(SharedHeader),
            "qualified existing native header layout");
    publication_word = &header.partitions[1].published.value;
  }
  ~Publication() {
    restore_publication();
    publication_word = nullptr;
    if (mapping != MAP_FAILED)
      (void)::munmap(mapping, sizeof(SharedHeader));
  }
};
bool stopped(pid_t pid) {
#if defined(__APPLE__)
  proc_bsdinfo info{};
  return ::proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) ==
             static_cast<int>(sizeof(info)) &&
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
struct Paused {
  std::array<pid_t, 2> pids;
  explicit Paused(RawRuntimeStatus status) : pids{status.supervisor, status.recorder} {
    for (const auto pid : pids) {
      require(::kill(pid, SIGSTOP) == 0, "actual holder stop request");
      while (!stopped(pid))
        std::this_thread::yield();
    }
  }
  ~Paused() {
    for (const auto pid : pids)
      (void)::kill(pid, SIGCONT);
  }
};
void capture_failures() {
  for (const auto fault :
       {Cut::capture_open, Cut::capture_temporary, Cut::capture_close,
        Cut::capture_rename, Cut::capture_close_unlink}) {
    Directory directory;
    const auto file = directory.path / "target";
    fixture_file(file, "old");
    IdentityBytes id{};
    id[0] = std::byte{53};
    bool failed = false;
    {
      auto runtime = unwrap(RawRuntime::open(
          directory.path.string(), unwrap(EnvironmentId::from_bytes(id)),
          {{64 * 1024 * 1024, 256 * 1024, 4096}, 4096}));
      auto thread = unwrap(runtime->prepare_thread());
      {
        RawThreadBinding binding{thread};
        const std::byte byte{9};
        const ByteView parts[]{ByteView{&byte, 1}};
        const RawEventRef parent{raw_lane_epoch(), unwrap(raw_emit(9200, parts))};
        const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
        Publication publication{directory};
        Paused paused{unwrap(runtime->status())};
        select(file, fault);
        try {
          if (fault == Cut::capture_open)
            (void)read_file(file, 64, &context);
          else
            write_file(file, "accepted", &context);
        } catch (const Error &error) {
          failed = error == Error{ErrorCode::conflict};
        }
        restore_publication();
        stop();
      }
      thread.reset();
    }
    require(failed && close_calls == 1, "actual post-effect publication failure "
                                        "preserves once-only descriptor cleanup");
    const auto events = records(directory, true);
    require(count(events, 8001) == 1,
            "actual failed publication writes existing ingest failure event");
    require(
        count(events, 9200) == 1,
        "original pre-failure bytes preserved independently of damaged publication");
    if (fault == Cut::capture_open || fault == Cut::capture_temporary) {
      const auto &returned = find(events, 3439);
      ByteView body{returned.body};
      const auto resource = ref(body);
      require(!take<bool>(body) && take<int>(body) == 0 && !take<bool>(body) &&
                  body.empty(),
              "actual close proceeds after failed request capture without fabricated "
              "request reference");
      require(
          equal(resource, find(events, fault == Cut::capture_open ? 3420u : 3430u).ref),
          "cleanup return retains actual acquired resource provenance");
    }
    if (fault == Cut::capture_rename) {
      require(disk(file) == "accepted" && unlink_calls == 0 &&
                  disk(temporary_name) == "new",
              "native rename success remains and old-name replacement survives "
              "return-capture failure");
      require(native_unlink(temporary_name) == 0,
              "fixture owns replacement after failed rename capture");
    } else {
      require(disk(file) == "old",
              "pre-rename capture failure leaves original target untouched");
      if (fault != Cut::capture_open) {
        if (fault == Cut::capture_close_unlink) {
          ByteView body{find(events, 3443).body};
          (void)ref(body);
          require(take<bool>(body), "failed unlink has actual acknowledged request");
          (void)ref(body);
          require(take<int>(body) == -1 && take<bool>(body) &&
                      take<int>(body) == EACCES && body.empty(),
                  "held native unlink failure distinct from original capture conflict");
          require(unlink_calls == 1 && native_unlink(temporary_name) == 0,
                  "fixture owns truly unresolved temporary after failed unlink");
        } else
          require(unlink_calls == 1 && !std::filesystem::exists(temporary_name),
                  "held unlink still consumes original temp after capture failure");
      }
    }
  }
}
void old_stdio_contract() {
  for (const auto fault : {Cut::first_closed, Cut::partial_closed}) {
    Directory directory;
    const auto file = directory.path / "source";
    const std::string source(16384, 's');
    fixture_file(file, source);
    select(file, fault);
    std::string old_answer, new_answer;
    std::optional<Error> old_error, new_error;
    try {
      old_answer = read_file(file);
    } catch (const Error &error) {
      old_error = error;
    }
    const auto old_acquired = acquired;
#if defined(_LIBCPP_VERSION)
    require(stdio_calls > 0 && stdio_error == EBADF,
            "actual old libc fread forwards a genuine lost-descriptor failure, not a "
            "mocked EOF");
#else
    require(read_calls > 0,
            "actual old GNU basic_file read reaches forwarded native failure");
#endif
    stop();
    const auto events = capture(directory, [&](const RawAdapterContext &context) {
      select(file, fault);
      try {
        new_answer = read_file(file, source.size(), &context);
      } catch (const Error &error) {
        new_error = error;
      }
      stop();
    });
    std::cerr << "stdio fault=" << static_cast<int>(fault)
              << " oldbytes=" << old_answer.size() << " newbytes=" << new_answer.size()
              << " oldacquired=" << old_acquired.size()
              << " newacquired=" << acquired.size() << " oldError=" << bool(old_error)
              << " newError=" << bool(new_error) << '\n';
    require(old_answer == new_answer && old_error == new_error &&
                old_acquired == acquired,
            "unchanged stdio helper and selected owned loop preserve actual business "
            "prefix/error mapping");
    require(originals(events, 1) == acquired,
            "new loop preserves exact positive bytes before native EBADF");
    const auto expected = fault == Cut::partial_closed ? 4096u : 0u;
    require(acquired.size() == expected,
            "independently selected actual first/positive-prefix failure extent");
#if defined(_LIBCPP_VERSION)
    require(!new_error && new_answer == source.substr(0, expected),
            "libc++ fread error remains existing EOF/prefix business outcome");
#else
    require(new_error == Error{ErrorCode::io},
            "GNU native acquisition error remains original stream exception mapping");
#endif
  }
}
#if defined(__GLIBCXX__)
void linux_fallback() {
  for (const auto size : {off_t{4094}, off_t{4095}, off_t{4096}}) {
    Directory directory;
    const auto target = directory.path / "target";
    const auto link = directory.path / "link";
    fixture_file(target, "native");
    std::filesystem::create_symlink("target", link);
    std::error_code error;
    std::filesystem::path answer;
    const auto events = capture(directory, [&](const RawAdapterContext &context) {
      fallback_path = link.string();
      fallback_link = fallback_path;
      fallback_target = target.string();
      fallback_size = size;
      fallback_lstats = fallback_links = 0;
      fallback_capacities.clear();
      fallback_full = true;
      answer = raw_file_canonical(link, error, &context);
      fallback_path.clear();
      fallback_link.clear();
      fallback_size.reset();
    });
    require(fallback_lstats == 2,
            "literal GCC fallback acquires fresh second lstat for readlink sizing");
    if (size == 4096) {
      require(
          answer.empty() && error.value() == ENAMETOOLONG &&
              fallback_capacities == std::vector<std::size_t>{4097},
          "full4097 uses literal current-size>4096 rejection without invented8192 cap");
    } else {
      const auto initial = static_cast<std::size_t>(size) + 1;
      require(answer == target && !error &&
                  fallback_capacities == std::vector<std::size_t>{initial, initial * 2},
              "full4095/4096 doubles the actual supplied capacity before native "
              "resolution");
    }
    std::vector<off_t> actual_sizes;
    for (const auto &event : events) {
      if (event.site != 3446)
        continue;
      ByteView body{event.body};
      (void)ref(body);
      if (take<int>(body) != 0 || take<bool>(body))
        continue;
      const auto mode = take<mode_t>(body);
      const auto actual = take<off_t>(body);
      if (S_ISLNK(mode))
        actual_sizes.push_back(actual);
    }
    require(actual_sizes == std::vector<off_t>{6, size},
            "both actually returned lstat sizes emitted unchanged, never reused first "
            "result");
    require(count(events, 3447) == fallback_capacities.size() &&
                count(events, 3448) == fallback_capacities.size(),
            "each reached fallback readlink is a distinct actual request and return");
    const auto outputs = originals(events, 5);
    const auto first = static_cast<std::size_t>(size) + 1;
    require(
        outputs.size() == first + (size == 4096 ? 0 : 6) &&
            std::all_of(outputs.begin(),
                        outputs.begin() + static_cast<std::ptrdiff_t>(first),
                        [](auto value) { return value == std::byte{'a'}; }),
        "positive full readlink prefix preserved before growth or ENAMETOOLONG branch");
  }
  for (const std::size_t links : {39u, 40u}) {
    Directory directory;
    const auto target = directory.path / "target";
    fixture_file(target, "native");
    for (std::size_t index = 0; index < links; ++index)
      std::filesystem::create_symlink(
          index + 1 == links ? "target" : "link." + std::to_string(index + 1),
          directory.path / ("link." + std::to_string(index)));
    const auto start = directory.path / "link.0";
    std::error_code error;
    std::filesystem::path answer;
    const auto events = capture(directory, [&](const RawAdapterContext &context) {
      fallback_path = start.string();
      fallback_links = 0;
      answer = raw_file_canonical(start, error, &context);
      fallback_path.clear();
    });
    require(count(events, 3447) == links && count(events, 3448) == links,
            "40th actual readlink happened before literal decrement rejects expansion");
    require(links == 39 ? answer == target && !error
                        : answer.empty() && error.value() == ELOOP,
            "native39/40 symlink expansion outcomes match literal reference");
  }
  {
    Directory directory;
    const auto target = directory.path / "target", link = directory.path / "link";
    fixture_file(target, "native");
    std::filesystem::create_symlink("target", link);
    const auto events = capture(directory, [&](const RawAdapterContext &context) {
      fallback_path = link.string();
      fallback_target = target.string();
      fallback_final_race = true;
      std::error_code error;
      const auto answer = raw_file_canonical(link, error, &context);
      fallback_path.clear();
      fallback_final_race = false;
      require(
          answer.empty() && !error && !std::filesystem::exists(target),
          "actual final exists race clears path without forcing initial-branch ENOENT");
    });
    require(count(events, 3447) == 1,
            "real final exists race follows actual symlink payload");
  }
}
#endif
void cwd_and_failed_output() {
  Directory directory;
  fixture_file(directory.path / "source", "native");
  const int prior = ::open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  require(prior >= 0 && ::chdir(directory.path.c_str()) == 0,
          "actual relative-path owner cwd");
  struct Restore {
    int fd;
    ~Restore() {
      (void)::fchdir(fd);
      (void)native_close(fd);
    }
  } restore{prior};
  std::array<char, 8192> cwd{};
  require(::getcwd(cwd.data(), cwd.size()) != nullptr, "independent actual cwd value");
  const std::string cwd_bytes{cwd.data(), std::strlen(cwd.data()) + 1};
  const auto absent = std::filesystem::path{cwd.data()} / "absent";
  std::array<char, PATH_MAX> failed{};
  require(::realpath(absent.c_str(), failed.data()) == nullptr,
          "independent actual native failed realpath");
  const int expected_error = errno;
  const auto events = capture(directory, [&](const RawAdapterContext &context) {
    std::error_code error;
    require(raw_file_canonical("source", error, &context) ==
                    std::filesystem::path{cwd.data()} / "source" &&
                !error,
            "explicit mechanical relative canonical retains actual getcwd result");
    const auto result = raw_file_canonical("absent", error, &context);
    require(result.empty() && error.value() == expected_error,
            "native failed canonical returns actual error_code, not fabricated "
            "Blackbird Error");
    write_file("created", "created", &context);
    require(disk(directory.path / "created") == "created",
            "relative nonexistent atomic write preserves native absolute path");
  });
  bool saw_unknown = false;
  for (const auto &event : events) {
    if (event.site != 3407)
      continue;
    ByteView body{event.body};
    (void)ref(body);
    const auto role = take<std::uint8_t>(body);
    const auto extent = take<std::size_t>(body);
    if (role == 2) {
      require(take<bool>(body) && take<std::size_t>(body) == extent &&
                  extent == cwd_bytes.size() &&
                  std::equal(body.begin(), body.end(),
                             reinterpret_cast<const std::byte *>(cwd_bytes.data()),
                             reinterpret_cast<const std::byte *>(cwd_bytes.data() +
                                                                 cwd_bytes.size())),
              "native getcwd known CString includes actual NUL and no initialized "
              "unused tail");
    } else if (role == 3) {
      const bool known = take<bool>(body);
      if (known) {
        require(take<std::size_t>(body) == extent,
                "realpath known native CString extent");
      } else {
        require(extent == failed.size() &&
                    std::equal(body.begin(), body.end(),
                               reinterpret_cast<const std::byte *>(failed.data()),
                               reinterpret_cast<const std::byte *>(failed.data() +
                                                                   failed.size())),
                "failed native realpath preserves complete initialized caller storage "
                "with unknown used length");
        saw_unknown = true;
      }
    }
  }
  require(
      saw_unknown,
      "actual native failure original exists separately from successful helper return");
}
void proposal_admission() {
  for (const auto method : {RawFileMethod::write, RawFileMethod::edit}) {
    for (const bool insufficient : {false, true}) {
      Directory directory;
      const auto file = directory.path / "unchanged";
      fixture_file(file, "unchanged");
      bool acquired_proposal = false, refused = false;
      std::string next = method == RawFileMethod::edit ? "x old y" : "";
      const std::string expected =
          method == RawFileMethod::edit ? "x updated y" : "new";
      const std::size_t extent = expected.size();
      // Literal native envelope16 + requestR24 + role1 + size_t8 + actual output.
      const auto allowance = 49 + extent - (insufficient ? 1 : 0);
      const auto events = capture(directory, [&](const RawAdapterContext &context) {
        RawFileToolTrace trace{&context, method};
        trace.argument(method == RawFileMethod::edit ? 2 : 3,
                       method == RawFileMethod::edit ? "updated" : "new");
        auto held = unwrap(raw_claim_data(64 * 1024 * 1024 - allowance));
        try {
          (void)trace.run([&] {
            auto bank = trace.prepare_proposal(extent);
            if (method == RawFileMethod::edit)
              next.replace(2, 3, "updated");
            else
              next = "new";
            acquired_proposal = true;
            const auto input = trace.proposed(next, bank);
            require(input.length == extent && input.body.ordinal != 0,
                    "completed real copy/replace has actual preserved body Ref");
            return Value{};
          });
        } catch (const Error &error) {
          refused = error == Error{ErrorCode::capacity};
        }
      });
      require(acquired_proposal == !insufficient && refused == insufficient,
              "literal proposal maximum and maximum-minus-one admit before actual "
              "copy/SAME replacement");
      require(next == (insufficient ? (method == RawFileMethod::edit ? "x old y" : "")
                                    : expected),
              "failed proposal grant leaves actual destination representation "
              "unacquired/unmodified");
      const auto preserved = originals(events, 6);
      require(insufficient
                  ? preserved.empty()
                  : preserved ==
                        std::vector<std::byte>(
                            reinterpret_cast<const std::byte *>(expected.data()),
                            reinterpret_cast<const std::byte *>(expected.data() +
                                                                expected.size())),
              "only completed proposed representation is emitted unchanged");
      require(count(events, 3430) == 0 && count(events, 3434) == 0 &&
                  disk(file) == "unchanged",
              "proposal permission boundary has no native write effect or invented "
              "acquisition");
    }
  }
}
void absolute_paths() {
  Directory directory;
  const auto absolute_input = directory.path / "native";
  const auto current = std::filesystem::current_path();
  const std::filesystem::path relative_input{"sub/../native"};
  const auto expected_relative = current / relative_input;
  RawEventRef relative_body{}, absolute_body{};
  const auto events = capture(directory, [&](const RawAdapterContext &context) {
    std::error_code error;
    tracking_cwd = true;
    cwd_facts.clear();
    cwd_facts.reserve(4);
    require(raw_file_absolute(relative_input, error, &context, &relative_body) ==
                    expected_relative &&
                !error,
            "typed selected absolute uses unchanged actual cwd/path join");
    tracking_cwd = false;
    require(raw_file_absolute(absolute_input, error, &context, &absolute_body) ==
                    absolute_input &&
                !error,
            "absolute input copies actual native path without extra cwd acquisition");
    RawEventRef inactive{};
    require(raw_file_absolute(absolute_input, error, nullptr, &inactive) ==
                    absolute_input &&
                !error && inactive.ordinal == 0,
            "default mechanical absolute remains optout and never invents outputRef");
  });
  require(cwd_facts.size() == 1 && cwd_facts[0].success &&
              cwd_facts[0].capacity == 8192,
          "one actual supplied native cwd extent at ordinary relative join");
  require(count(events, 3426) == 1 && count(events, 3427) == 1,
          "no synthetic cwd acquisition for already absolute path");
  for (const auto &event : events) {
    if (event.site != 3407)
      continue;
    ByteView body{event.body};
    const auto helper = ref(body);
    const auto role = take<std::uint8_t>(body);
    const auto extent = take<std::size_t>(body);
    if (role != 7)
      continue;
    const auto wanted = equal(event.ref, relative_body) ? expected_relative.native()
                                                        : absolute_input.native();
    require(equal(event.ref, relative_body) || equal(event.ref, absolute_body),
            "actual returned pathRef names actual original event");
    require(extent == wanted.size() && body.size() == wanted.size() &&
                std::equal(body.begin(), body.end(),
                           reinterpret_cast<const std::byte *>(wanted.data())),
            "completed returned native path emitted unchanged immediately");
    const auto owner =
        std::find_if(events.begin(), events.end(),
                     [&](const auto &record) { return equal(record.ref, helper); });
    require(owner != events.end() && owner->site == 3410,
            "returned path links actual absolute helper begin");
    ByteView request{owner->body};
    (void)ref(request);
    require(take<RawFileMethod>(request) == RawFileMethod::absolute,
            "native absolute6 method distinct from canonical/exists");
  }
  for (const bool insufficient : {false, true}) {
    Directory bound_directory;
    RawEventRef body{};
    bool refused = false;
    const auto amount = 49 + absolute_input.native().size() - (insufficient ? 1 : 0);
    const auto rows = capture(bound_directory, [&](const RawAdapterContext &context) {
      auto held = unwrap(raw_claim_data(64 * 1024 * 1024 - amount));
      std::error_code error;
      try {
        require(raw_file_absolute(absolute_input, error, &context, &body) ==
                        absolute_input &&
                    !error,
                "literal absolute input copy grant succeeds");
      } catch (const Error &e) {
        refused = e == Error{ErrorCode::capacity};
      }
    });
    require(refused == insufficient && (body.ordinal != 0) == !insufficient,
            "exact actual absolute copy bound and minus-one refuse before returned "
            "representation");
    require(count(rows, 3426) == 0 &&
                originals(rows, 7).size() ==
                    (insufficient ? 0 : absolute_input.native().size()),
            "copy grant boundary performs no cwd syscall and has no fabricated "
            "returned body");
  }
  // A long borrowed relative operand makes the final join grant dominate the
  // earlier fixed8192-byte getcwd output grant, so its own boundary is reached.
  const std::filesystem::path long_relative{std::string(16384, 'j')};
  const auto expected_join = current / long_relative;
  require(current.native().size() > 1 && current.native().back() != '/',
          "independent join inserts the actual one separator");
  for (const bool insufficient : {false, true}) {
    Directory bound_directory;
    RawEventRef returned{};
    bool refused = false;
    const auto amount = 49 + current.native().size() + 1 +
                        long_relative.native().size() - (insufficient ? 1 : 0);
    const auto rows = capture(bound_directory, [&](const RawAdapterContext &context) {
      auto held = unwrap(raw_claim_data(64 * 1024 * 1024 - amount));
      std::error_code error;
      try {
        require(raw_file_absolute(long_relative, error, &context, &returned) ==
                        expected_join &&
                    !error,
                "literal cwd/path join grant succeeds before completed return");
      } catch (const Error &e) {
        refused = e == Error{ErrorCode::capacity};
      }
    });
    require(refused == insufficient && (returned.ordinal != 0) == !insufficient &&
                count(rows, 3426) == 1,
            "max/minus-one reaches native cwd then refuses actual join before "
            "representation acquisition");
    require(
        originals(rows, 2).size() == current.native().size() + 1,
        "already acquired native cwd original survives joined-output credit refusal");
    require(originals(rows, 7).size() ==
                (insufficient ? 0 : expected_join.native().size()),
            "no returned-path original/ref is fabricated when its preeffect join grant "
            "refuses");
  }
}
void absolute_uncapped_and_error() {
  Directory directory;
  const int old_cwd = ::open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  require(old_cwd >= 0, "fixture owns original cwd incarnation");
  const auto deep = directory.path / "deep";
  std::filesystem::create_directory(deep);
  std::vector<int> parents;
  parents.reserve(101);
  parents.push_back(::open(deep.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  require(parents.back() >= 0, "native deep directory root fd");
  const std::string component(90, 'd');
  for (std::size_t n = 0; n < 100; ++n) {
    require(::mkdirat(parents.back(), component.c_str(), 0700) == 0,
            "native descriptor-relative deep component");
    const int fd =
        ::openat(parents.back(), component.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    require(fd >= 0, "actual deep directory owner");
    parents.push_back(fd);
  }
  struct Restore {
    int original;
    std::vector<int> &parents;
    const std::string &component;
    ~Restore() {
      (void)::fchdir(original);
      (void)native_close(original);
      for (std::size_t n = parents.size(); n > 1; --n) {
        (void)native_close(parents[n - 1]);
        (void)::unlinkat(parents[n - 2], component.c_str(), AT_REMOVEDIR);
      }
      (void)native_close(parents.front());
    }
  } restore{old_cwd, parents, component};
  const auto rows = capture(directory, [&](const RawAdapterContext &context) {
    require(::fchdir(parents.back()) == 0,
            "actual native cwd beyond old8192 and PATH_MAX assumptions");
    std::array<char, 16384> expected{};
    require(native_cwd(expected.data(), expected.size()) != nullptr,
            "independently acquired actual long cwd");
    const std::string wanted = std::string{expected.data()} + "/leaf";
    require(wanted.size() > 8192, "actual long path requires native getcwd growth");
    cwd_facts.clear();
    cwd_facts.reserve(4);
    tracking_cwd = true;
    std::error_code error;
    RawEventRef returned{};
    const auto path = raw_file_absolute("leaf", error, &context, &returned);
    tracking_cwd = false;
    require(
        path.native() == wanted && !error && returned.ordinal != 0,
        "uncapped actual cwd/path result funded before existing join and preserved");
    require(::fchdir(old_cwd) == 0, "restore real cwd before owner close");
  });
  require(cwd_facts.size() == 2 && cwd_facts[0].capacity == 8192 &&
              !cwd_facts[0].success && cwd_facts[0].error == ERANGE &&
              cwd_facts[1].capacity == 16384 && cwd_facts[1].success,
          "actual native caller-buffer growth has no synthesized PATH_MAX bound");
  std::vector<std::byte> stored;
  for (const auto &row : rows) {
    if (row.site != 3407)
      continue;
    ByteView body{row.body};
    (void)ref(body);
    const auto role = take<std::uint8_t>(body);
    const auto extent = take<std::size_t>(body);
    if (role != 2)
      continue;
    const auto known = take<bool>(body);
    if (known)
      require(take<std::size_t>(body) == extent,
              "actual long cwd CString includes NUL");
    stored.insert(stored.end(), body.begin(), body.end());
  }
  std::vector<std::byte> expected;
  for (const auto &fact : cwd_facts)
    expected.insert(expected.end(), fact.bytes.begin(), fact.bytes.end());
  require(
      stored == expected,
      "full initialized failed native cwd output and grown success preserved exactly");
  // A removed but genuinely current directory produces native ENOENT. The API's
  // empty returned path is distinct from any acquired cwd/source success.
  Directory missing;
  const auto gone = missing.path / "gone";
  std::filesystem::create_directory(gone);
  require(::chdir(gone.c_str()) == 0 && ::rmdir(gone.c_str()) == 0,
          "actual missing cwd native state");
  const auto failed_rows = capture(missing, [&](const RawAdapterContext &context) {
    std::error_code error;
    RawEventRef body{};
    const auto result = raw_file_absolute("leaf", error, &context, &body);
    require(result.empty() && error.value() == ENOENT && body.ordinal != 0,
            "native getcwd error returns actual empty path/error_code with real "
            "returned body Ref");
    require(::fchdir(old_cwd) == 0, "restore cwd after real native error");
  });
  require(
      count(failed_rows, 3426) == 1 && count(failed_rows, 3427) == 1 &&
          count(failed_rows, 3407) == 2 && originals(failed_rows, 7).empty(),
      "native failed cwd original separate from actual zero-byte returned path body");
}
void fixed_header_prefix() {
  for (const auto fault : {Cut::none, Cut::short_eof, Cut::first_error,
                           Cut::interrupted, Cut::allocation}) {
    Directory directory;
    const auto file = directory.path / "audit-prefix";
    std::string source(2048, 'x');
    for (std::size_t index = 0; index < source.size(); ++index)
      source[index] = static_cast<char>(index % 256);
    fixture_file(file, source);
    select(file, fault);
    prefix_requests.clear();
    prefix_requests.reserve(8);
    prefix_observation = true;
    allocation_floor = 112;
    std::string answer;
    std::optional<Error> error;
    bool allocated = false;
    RawEventRef helper{};
    const auto events = capture(directory, [&](const RawAdapterContext &context) {
      try {
        answer = read_file_prefix(file, 112, &context, &helper);
      } catch (const Error &failure) {
        error = failure;
      } catch (const std::bad_alloc &) {
        allocated = true;
      }
      observing = false;
    });
    prefix_observation = false;
    allocation_floor = 8193;
    require(helper.ordinal && close_calls == 1,
            "actual prefix request and one paid close");
    const auto bytes = originals(events, 1);
    require(bytes == acquired,
            "every actual native prefix byte is preserved unchanged");
    if (fault == Cut::none || fault == Cut::interrupted)
      require(
          !error && !allocated && answer == source.substr(0, 112) &&
              acquired.size() == 112 && read_calls == (fault == Cut::none ? 1 : 2),
          "large audit exact112 prefix succeeds without trailing read or size refusal");
    else if (fault == Cut::short_eof)
      require(error == Error{ErrorCode::incomplete} && acquired.size() == 7 &&
                  read_calls == 2,
              "actual short prefix EOF preserves partial original and incomplete");
    else if (fault == Cut::first_error)
      require(error == Error{ErrorCode::io, EIO} && acquired.empty() && read_calls == 1,
              "actual negative prefix read owns literal native Error");
    else
      require(allocated && allocation_fired && acquired.size() == 112 && answer.empty(),
              "real prefix append allocation fails only after raw112 original");
    std::size_t calls = 0, ends = 0;
    for (const auto &event : events) {
      auto body = ByteView{event.body};
      require(event.site != 3424 && event.site != 3445,
              "prefix never stats whole file size");
      if (event.site == 3410) {
        (void)ref(body);
        require(take<RawFileMethod>(body) == RawFileMethod::prefix &&
                    take<std::size_t>(body) == 112 && body.empty(),
                "distinct prefix7 native requested length");
      }
      if (event.site == 3422) {
        require(equal(ref(body), helper),
                "prefix native read belongs to actual helper");
        (void)ref(body);
        require(take<std::uint8_t>(body) == 2, "literal native read method");
        (void)take<int>(body);
        require(calls < prefix_requests.size() &&
                    take<std::size_t>(body) == prefix_requests[calls++] && body.empty(),
                "actual requested prefix native size");
      }
      if (event.site == 3411) {
        require(equal(ref(body), helper), "prefix terminal actual owner");
        const auto branch = take<RawFileBranch>(body);
        if (error)
          require(branch == RawFileBranch::error &&
                      take<ErrorCode>(body) == error->code &&
                      take<std::int64_t>(body) == error->detail && body.empty(),
                  "prefix actual Error code/detail terminal");
        else if (allocated)
          require(branch == RawFileBranch::allocation && body.empty(),
                  "prefix allocation is not fabricated Error");
        else
          require(branch == RawFileBranch::returned && take<std::size_t>(body) == 112 &&
                      body.empty(),
                  "prefix actual complete returned count");
        ++ends;
      }
    }
    require(calls == read_calls && ends == 1, "actual native reads and one terminal");
  }
  Directory directory;
  const auto file = directory.path / "ordinary";
  fixture_file(file, std::string(2048, 'q'));
  const auto events = capture(directory, [&](const RawAdapterContext &) {
    RawEventRef absent{{}, 777};
    require(read_file_prefix(file, 112, nullptr, &absent) == std::string(112, 'q') &&
                !absent.ordinal,
            "default prefix forwards native behavior and clears absent source");
    bool short_read = false;
    try {
      (void)read_file_prefix(file, 2049);
    } catch (const Error &error) {
      short_read = error == Error{ErrorCode::incomplete};
    }
    require(short_read, "ordinary prefix retains short-read incomplete semantics");
  });
  require(std::none_of(events.begin(), events.end(),
                       [](const auto &event) {
                         return event.site >= 3400 && event.site < 3500;
                       }),
          "default prefix is selected only by explicit context");
}
void limit_prefix() {
  Directory directory;
  IdentityBytes id{};
  id[0] = std::byte{51};
  const auto environment = unwrap(EnvironmentId::from_bytes(id));
  const auto file = directory.path / "source";
  constexpr std::size_t total = 16 * 1024 * 1024 + 8192;
  std::vector<std::byte> expected(total);
  for (std::size_t index = 0; index < expected.size(); ++index)
    expected[index] = static_cast<std::byte>(index % 256);
  {
    std::ofstream output{file, std::ios::binary};
    output.write(reinterpret_cast<const char *>(expected.data()),
                 static_cast<std::streamsize>(expected.size()));
  }
  select(file, Cut::none);
  acquired.reserve(total + 8192);
  bool limited = false;
  {
    auto runtime =
        unwrap(RawRuntime::open(directory.path.string(), environment,
                                {{64 * 1024 * 1024, 256 * 1024, 4096}, 4096}));
    auto thread = unwrap(runtime->prepare_thread());
    {
      RawThreadBinding binding{thread};
      const std::byte marker{7};
      const ByteView parts[]{ByteView{&marker, 1}};
      const RawEventRef parent{raw_lane_epoch(), unwrap(raw_emit(9200, parts))};
      const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
      LocalTools tools;
      observing = true;
      try {
        (void)tools.run("read_file", Value::object({{"path", Value{file.string()}}}),
                        &context);
      } catch (const Error &error) {
        limited = error == Error{ErrorCode::capacity};
      }
      observing = false;
    }
    thread.reset();
  }
  std::cerr << "old/native limit=" << limited << " acquired=" << acquired.size()
            << " expected=" << total << '\n';
  require(
      limited && acquired.size() == total && acquired == expected,
      "actual old/new native acquisition reaches limit with independently exact bytes");
  const auto runs = unwrap(discover_raw_runs(directory.path.string()));
  require(runs.size() == 1, "actual native raw run present");
  const auto &snapshot = runs[0].snapshot;
  if (!snapshot)
    throw std::runtime_error{"actual native raw snapshot missing"};
  require(snapshot->closed, "actual native raw run closed");
  auto reader =
      unwrap(RawRunReader::open((directory.path / "raw" / runs[0].name).string()));
  std::array<std::byte, 32768> output{};
  std::vector<std::byte> retained;
  retained.reserve(total);
  while (const auto record = unwrap(reader->read(output))) {
    auto body = record->raw;
    require(take<std::uint32_t>(body) == record->raw.size(),
            "exact raw envelope length");
    const auto site = take<std::uint32_t>(body);
    (void)take<std::uint64_t>(body);
    require(site != 8001 && site != 8000, "no failed native ingestion");
    if (site != 3407)
      continue;
    (void)take<IdentityBytes>(body);
    (void)take<std::uint64_t>(body);
    const auto role = take<std::uint8_t>(body);
    const auto size = take<std::size_t>(body);
    if (role != 1)
      continue;
    require(size == body.size(), "read original exact native returned extent");
    retained.insert(retained.end(), body.begin(), body.end());
  }
  require(retained == acquired,
          "each native acquired prefix is already on disk before read limit refusal");
}
} // namespace
#if defined(__GLIBCXX__)
extern "C" char *realpath(const char *name, char *output) {
  if (!fallback_path.empty() && fallback_path == name) {
    std::memcpy(output, "/failed", 8);
    errno = ENAMETOOLONG;
    return nullptr;
  }
  return native_realpath(name, output);
}
extern "C" int lstat(const char *name, struct stat *output) {
  const int result = native_lstat(name, output);
  const int error = errno;
  if (result == 0 && !fallback_link.empty() && fallback_link == name) {
    ++fallback_lstats;
    if (fallback_lstats == 2 && fallback_size)
      output->st_size = *fallback_size;
  }
  errno = error;
  return result;
}
extern "C" int stat(const char *name, struct stat *output) {
  if (fallback_final_race && fallback_target == name) {
    fallback_final_race = false;
    require(native_unlink(name) == 0, "actual final canonical target removal");
  }
  return native_stat(name, output);
}
extern "C" ssize_t readlink(const char *name, char *output, std::size_t capacity) {
  if (!fallback_link.empty() && fallback_link == name) {
    fallback_capacities.push_back(capacity);
    ++fallback_links;
    if (fallback_full && fallback_links == 1) {
      std::memset(output, 'a', capacity);
      return static_cast<ssize_t>(capacity);
    }
  }
  return native_readlink(name, output, capacity);
}
#endif
extern "C" char *getcwd(char *output, std::size_t capacity) {
  auto *result = native_cwd(output, capacity);
  const int error = errno;
  if (tracking_cwd) {
    const auto extent = result ? std::strlen(output) + 1 : capacity;
    const auto *first = reinterpret_cast<const std::byte *>(output);
    cwd_facts.push_back({capacity, result != nullptr, error, {first, first + extent}});
  }
  errno = error;
  return result;
}
extern "C" int open(const char *name, int flags, ...) {
  mode_t mode = 0;
  bool supplied = (flags & O_CREAT) != 0;
#if defined(O_TMPFILE)
  supplied = supplied || (flags & O_TMPFILE) == O_TMPFILE;
#endif
  if (supplied) {
    va_list args;
    va_start(args, flags);
    mode = static_cast<mode_t>(va_arg(args, int));
    va_end(args);
  }
  const int fd = supplied ? native_open(name, flags, mode) : native_open(name, flags);
  const int error = errno;
  if (fd >= 0 && matches(fd) && cut == Cut::capture_open)
    damage_publication();
  errno = error;
  return fd;
}
extern "C" ssize_t read(int fd, void *bytes, std::size_t size) {
  const bool selected_fd = !inside_fread && matches(fd);
  if (!selected_fd)
    return native_read(fd, bytes, size);
  ++read_calls;
  if (prefix_observation)
    prefix_requests.push_back(size);
  if (cut == Cut::first_closed || (cut == Cut::partial_closed && read_calls == 2))
    require(native_close(fd) == 0,
            "fixture really loses selected descriptor before native read");
  if (cut == Cut::partial_closed && read_calls == 1)
    size = std::min<std::size_t>(size, 4096);
  if (cut == Cut::first_error || (cut == Cut::partial_error && read_calls == 2)) {
    errno = EIO;
    return -1;
  }
  if (cut == Cut::interrupted && read_calls == 1) {
    errno = EINTR;
    return -1;
  }
  if (cut == Cut::short_eof && read_calls == 2)
    return 0;
  if ((cut == Cut::partial_error || cut == Cut::short_eof) && read_calls == 1)
    size = std::min<std::size_t>(size, 7);
  const auto result = native_read(fd, bytes, size);
  const int error = errno;
  if (result > 0)
    preserved(bytes, static_cast<std::size_t>(result));
  if (cut == Cut::allocation && result > 0)
    allocation_ready = true;
  errno = error;
  return result;
}
extern "C" int mkstemp(char *name) {
  const int fd = native_temporary(name), error = errno;
  if (observing && fd >= 0) {
    temporary_fd = fd;
    require(::fstat(fd, &temporary_identity) == 0, "actual temporary inode");
    require(std::strlen(name) < sizeof(temporary_name), "actual fixture name fits");
    std::memcpy(temporary_name, name, std::strlen(name) + 1);
    if (cut == Cut::cancel_temporary)
      cancel_native();
    if (cut == Cut::capture_temporary)
      damage_publication();
  }
  errno = error;
  return fd;
}
extern "C" ssize_t write(int fd, const void *bytes, std::size_t size) {
  if (!temporary_matches(fd))
    return native_write(fd, bytes, size);
  ++write_calls;
  if (cut == Cut::write_error || cut == Cut::unlink_error) {
    errno = EIO;
    return -1;
  }
  if (cut == Cut::zero_write)
    return 0;
  if (cut == Cut::interrupted_write && write_calls == 1) {
    errno = EINTR;
    return -1;
  }
  if (cut == Cut::short_write && write_calls == 1)
    size = std::min<std::size_t>(size, 3);
  return native_write(fd, bytes, size);
}
extern "C" int fsync(int fd) {
  if (temporary_matches(fd)) {
    ++sync_calls;
    if (cut == Cut::sync_error) {
      errno = EIO;
      return -1;
    }
  }
  return native_sync(fd);
}
extern "C" int close(int fd) {
  const bool owned = temporary_matches(fd) || matches(fd);
  if (owned) {
    ++close_calls;
    if (cut == Cut::capture_open || cut == Cut::capture_temporary)
      restore_publication();
  }
  const int result = native_close(fd), error = errno;
  if (owned && cut == Cut::cancel_close)
    cancel_native();
  if (owned && (cut == Cut::capture_close || cut == Cut::capture_close_unlink))
    damage_publication();
  if (owned && cut == Cut::close_error) {
    errno = EIO;
    return -1;
  }
  errno = error;
  return result;
}
extern "C" int rename(const char *old_name, const char *new_name) {
  const bool owned =
      observing && temporary_name[0] && std::strcmp(old_name, temporary_name) == 0;
  if (owned) {
    ++rename_calls;
    if (cut == Cut::rename_error) {
      errno = EIO;
      return -1;
    }
  }
  const int result = native_rename(old_name, new_name), error = errno;
  if (owned && result == 0 &&
      (cut == Cut::cancel_rename || cut == Cut::capture_rename)) {
    // A genuinely different inode now occupies the consumed temporary pathname.
    const int replacement = ::open(old_name, O_WRONLY | O_CREAT | O_EXCL, 0600);
    require(replacement >= 0, "native replacement at consumed temporary name");
    require(native_write(replacement, "new", 3) == 3, "native replacement bytes");
    require(native_close(replacement) == 0, "native replacement close");
    if (cut == Cut::capture_rename)
      damage_publication();
    else
      cancel_native();
  }
  errno = error;
  return result;
}
extern "C" int unlink(const char *name) {
  if (observing && temporary_name[0] && std::strcmp(name, temporary_name) == 0) {
    ++unlink_calls;
    if (cut == Cut::capture_close || cut == Cut::capture_close_unlink)
      restore_publication();
    if (cut == Cut::unlink_error || cut == Cut::capture_close_unlink) {
      errno = EACCES;
      return -1;
    }
  }
  return native_unlink(name);
}
void *operator new(std::size_t size) {
  if (allocation_ready && !allocation_fired && size >= allocation_floor) {
    allocation_fired = true;
    allocation_ready = false;
    throw std::bad_alloc{};
  }
  if (void *value = std::malloc(size ? size : 1))
    return value;
  throw std::bad_alloc{};
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *value) noexcept { std::free(value); }
void operator delete[](void *value) noexcept { std::free(value); }
void operator delete(void *value, std::size_t) noexcept { std::free(value); }
void operator delete[](void *value, std::size_t) noexcept { std::free(value); }
extern "C" std::size_t observed_fread(void *bytes, std::size_t size, std::size_t count,
                                      FILE *file) {
  const bool selected_fd = matches(::fileno(file));
  inside_fread = true;
  const auto result = native_fread(bytes, size, count, file);
  const int error = errno;
  inside_fread = false;
  if (selected_fd && result > 0)
    preserved(bytes, result * size);
  errno = error;
  return result;
}
extern "C" std::size_t bb_file_before_stdio(const FileStdioRequest &request) {
  stdio_selected = matches(request.fd);
  if (!stdio_selected)
    return request.count;
  ++stdio_calls;
  if (cut == Cut::first_closed || (cut == Cut::partial_closed && stdio_calls == 2))
    require(native_close(request.fd) == 0,
            "fixture really loses old stdio descriptor before libc acquisition");
  if (cut == Cut::partial_closed && stdio_calls == 1 && request.size == 1)
    return std::min<std::size_t>(request.count, 4096);
  return request.count;
}
extern "C" void bb_file_after_stdio(const FileStdioReturn &actual) {
  if (stdio_selected && actual.returned == 0 && actual.failed)
    stdio_error = actual.error;
}
extern "C" void bb_file_observed_stdio(int fd, void *bytes, std::size_t size) {
  if (matches(fd) || stdio_selected)
    preserved(bytes, size);
}
int main(int argc, char **argv) {
#if defined(__GLIBCXX__)
  native_stat = reinterpret_cast<NativeStat>(::dlsym(RTLD_NEXT, "stat"));
  native_lstat = reinterpret_cast<NativeStat>(::dlsym(RTLD_NEXT, "lstat"));
  native_realpath = reinterpret_cast<NativeRealpath>(::dlsym(RTLD_NEXT, "realpath"));
  native_readlink = reinterpret_cast<NativeReadlink>(::dlsym(RTLD_NEXT, "readlink"));
  require(native_stat && native_lstat && native_realpath && native_readlink,
          "native fallback forwarding functions");
#endif
  native_cwd = reinterpret_cast<NativeCwd>(::dlsym(RTLD_NEXT, "getcwd"));
  native_open = reinterpret_cast<NativeOpen>(::dlsym(RTLD_NEXT, "open"));
  native_read = reinterpret_cast<NativeRead>(::dlsym(RTLD_NEXT, "read"));
  native_fread = reinterpret_cast<NativeFread>(::dlsym(RTLD_NEXT, "fread"));
  native_write = reinterpret_cast<NativeWrite>(::dlsym(RTLD_NEXT, "write"));
  native_close = reinterpret_cast<NativeInt>(::dlsym(RTLD_NEXT, "close"));
  native_sync = reinterpret_cast<NativeInt>(::dlsym(RTLD_NEXT, "fsync"));
  native_rename = reinterpret_cast<NativeRename>(::dlsym(RTLD_NEXT, "rename"));
  native_unlink = reinterpret_cast<NativeUnlink>(::dlsym(RTLD_NEXT, "unlink"));
  native_temporary = reinterpret_cast<NativeTemporary>(::dlsym(RTLD_NEXT, "mkstemp"));
  require(native_cwd && native_open && native_read && native_fread && native_write &&
              native_close && native_sync && native_rename && native_unlink &&
              native_temporary,
          "native forwarding functions");
  try {
    if (argc == 2 && std::string_view{argv[1]} == "stdio") {
      old_stdio_contract();
      return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "prefix") {
      fixed_header_prefix();
      return 0;
    }
    fixed_header_prefix();
    limit_prefix();
    read_faults();
    write_faults();
    paths_and_tools();
    optout_and_capacity();
    exact_admission();
#if defined(__GLIBCXX__)
    linux_fallback();
#endif
    capture_failures();
    cwd_and_failed_output();
    proposal_admission();
    absolute_paths();
    absolute_uncapped_and_error();
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
