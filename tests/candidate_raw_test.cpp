#define main candidate_program_main
#include "../src/candidate.cpp"
#undef main
#include "blackbird/raw_reader.hpp"
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <set>
#include <signal.h>
#include <thread>

#if defined(__linux__)
extern "C" ssize_t __real_write(int, const void *, std::size_t);
#define CANDIDATE_WRITE __wrap_write
#else
#define CANDIDATE_WRITE write
#endif
namespace native_cut {
using Write = ssize_t (*)(int, const void *, std::size_t);
Write original = nullptr;
thread_local bool active = false;
thread_local unsigned calls = 0;
thread_local int observed = -1;
thread_local bool fail_sync = false;
thread_local bool deny_allocation = false;
thread_local unsigned allocation_refusals = 0;
} // namespace native_cut
extern "C" ssize_t CANDIDATE_WRITE(int fd, const void *bytes, std::size_t size) {
  if (native_cut::active && fd == native_cut::observed) {
    ++native_cut::calls;
    if (native_cut::calls == 1) {
      // Interrupt a real descriptor syscall: closed fd is a real kernel error.
      // EINTR itself is tested by business retries elsewhere; this cut returns
      // the genuine shorter native write before forwarding the remaining bytes.
      return native_cut::original(fd, bytes, std::min<std::size_t>(size, 3));
    }
  }
  return native_cut::original(fd, bytes, size);
}
void *operator new(std::size_t size) {
  if (native_cut::deny_allocation) {
    native_cut::deny_allocation = false;
    ++native_cut::allocation_refusals;
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
namespace fixture {
using namespace blackbird;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
struct Directory {
  fs::path path;
  explicit Directory(std::string_view kind) {
    auto pattern = (fs::temp_directory_path() /
                    ("blackbird-candidate-raw-" + std::string(kind) + "-XXXXXX"))
                       .string();
    std::vector<char> text(pattern.begin(), pattern.end());
    text.push_back(0);
    const auto *made = ::mkdtemp(text.data());
    check(made, "native mkdtemp");
    path = made;
  }
  ~Directory() {
    std::error_code ignored;
    fs::remove_all(path, ignored);
  }
};
void put(const fs::path &path, std::string_view bytes) {
  std::ofstream output{path, std::ios::binary};
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  check(bool(output), "native fixture write");
}
std::string disk(const fs::path &path) {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input}, {}};
}
std::string subprocess(std::vector<std::string> argv, int expected = 0) {
  detail::Child child;
  child.start(std::move(argv), {}, true);
  child.close_input();
  int status = 0;
  auto result = child.collect(detail::Clock::now() + std::chrono::seconds(40),
                              2 * 1024 * 1024, &status);
  if (expected == 0 && status != 0)
    throw std::runtime_error("native Candidate CLI refusal: " + result);
  check(expected == 0 || status != 0, "native Candidate expected refusal");
  return result;
}
Value object(std::initializer_list<std::pair<std::string, Value>> fields) {
  return Value::object(fields);
}
Value string(std::string value) { return Value{std::move(value)}; }
Value number(std::size_t value) { return Value{Number{value}}; }
std::string external_json(const Value &value) { return unwrap(dump_json(value)); }
EnvironmentId environment(unsigned char byte) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{byte};
  return unwrap(EnvironmentId::from_bytes(bytes));
}
using Ref = std::pair<IdentityBytes, std::uint64_t>;
template <class T> T native(ByteView &bytes) {
  check(bytes.size() >= sizeof(T), "literal Candidate native field extent");
  T result{};
  std::memcpy(&result, bytes.data(), sizeof(T));
  bytes = bytes.subspan(sizeof(T));
  return result;
}
Ref reference(ByteView &bytes) {
  return {native<IdentityBytes>(bytes), native<std::uint64_t>(bytes)};
}
struct Row {
  Ref own;
  std::uint32_t site;
  std::vector<std::byte> body;
};
std::vector<Row> records(const fs::path &directory) {
  const auto runs = unwrap(discover_raw_runs(directory.string()));
  check(!runs.empty(), "actual Candidate raw run discovery");
  std::vector<Row> rows;
  for (const auto &run : runs) {
    const auto &snapshot = run.snapshot;
    check(snapshot.has_value() && snapshot->closed, "actual Candidate recorder closed");
    auto reader = unwrap(RawRunReader::open((directory / "raw" / run.name).string()));
    std::vector<std::byte> scratch(17 * 1024 * 1024);
    while (const auto row = unwrap(reader->read(scratch))) {
      ByteView fields{row->raw};
      check(native<std::uint32_t>(fields) == row->raw.size(),
            "literal Candidate E16 extent");
      const auto site = native<std::uint32_t>(fields);
      const auto ordinal = native<std::uint64_t>(fields);
      check(site != 8000 && site != 8001, "Candidate ingestion failure");
      rows.push_back(
          {{row->origin.lane, ordinal}, site, {fields.begin(), fields.end()}});
    }
  }
  return rows;
}
std::string original(ByteView &bytes) {
  const auto size = native<std::size_t>(bytes);
  check(bytes.size() == size, "Candidate exact literal original length");
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
std::size_t count(const std::vector<Row> &rows, std::uint32_t site) {
  return static_cast<std::size_t>(std::count_if(
      rows.begin(), rows.end(), [&](const Row &row) { return row.site == site; }));
}
void cli(const fs::path &executable) {
  Directory directory{"cli"};
  const auto repo = directory.path / "primary", pool = directory.path / "pool";
  fs::create_directory(repo);
  subprocess({"git", "init", "-q", repo.string()});
  subprocess({"git", "-C", repo.string(), "config", "user.name", "Native Candidate"});
  subprocess({"git", "-C", repo.string(), "config", "user.email",
              "candidate@example.invalid"});
  subprocess({"git", "-C", repo.string(), "config", "commit.gpgsign", "false"});
  put(repo / "source", "unchanged baseline\n");
  subprocess({"git", "-C", repo.string(), "add", "source"});
  subprocess({"git", "-C", repo.string(), "commit", "-qm", "baseline"});
  put(repo / "unrelated", "keep original dirty bytes\n");
  fs::create_directory(pool);
  const auto permissions = fs::status(pool).permissions();
  unsigned sequence = 0;
  std::vector<std::vector<std::string>> expected_argv;
  auto call = [&](std::string command, const Value &request, bool rejected = false) {
    const auto file =
        directory.path / ("request-" + std::to_string(sequence++) + ".json");
    put(file, external_json(request));
    auto args = std::vector<std::string>{executable.string(), pool.string(),
                                         std::move(command), file.string()};
    expected_argv.push_back(args);
    const auto output = subprocess(std::move(args), rejected ? 1 : 0);
    return rejected ? Value{} : unwrap(parse_json(output));
  };
  call("init", object({{"repo", string(repo.string())}, {"cap", number(1)}}));
  check(fs::status(pool).permissions() == permissions,
        "Candidate changed legacy pool permissions");
  const auto raw = pool / ".raw-candidate";
  check((fs::status(raw).permissions() & fs::perms::group_all) == fs::perms::none,
        "Candidate raw directory not private");
  call("dispatch", object({}));
  auto base = subprocess({"git", "-C", repo.string(), "rev-parse", "HEAD"});
  while (!base.empty() && base.back() == '\n')
    base.pop_back();
  call("enqueue", object({{"name", string("native-one")},
                          {"base", string(base)},
                          {"hypothesis", string("exact native body")},
                          {"discriminator", string("unchanged bytes")},
                          {"identities", object({{"program", string("literal")}})},
                          {"baseline", string(base)}}));
  call("dispatch", object({}));
  call("dispatch", object({})); // real full pool: no second worktree
  const std::string output{"A\x00\xff\r\n", 5};
  const auto result = call(
      "run",
      object(
          {{"slot", number(0)},
           {"seconds", number(5)},
           {"argv",
            Value{Value::Array{
                string("/bin/sh"), string("-c"),
                string("printf 'A\\000\\377\\r\\n'; printf x >> effect; exit 7")}}}}));
  const auto locator = result.find("output");
  check(locator && disk(locator->string()) == output,
        "Candidate output binary bytes changed");
  check(disk(pool / "slot-0" / "effect") == "x",
        "Candidate real effect not exactly once");
  call("run",
       object({{"slot", number(0)},
               {"seconds", number(1)},
               {"argv", Value{Value::Array{string("/bin/true")}}}}),
       true);
  const auto rows = records(raw);
  check(count(rows, 3900) == expected_argv.size() &&
            count(rows, 3902) == expected_argv.size(),
        "Candidate CLI request/terminal count");
  check(count(rows, 3918) == 1 && count(rows, 3919) == 1 && count(rows, 3921) == 1,
        "Candidate run settled more than once or skipped");
  check(count(rows, 3908) > 0 && count(rows, 3913) > 0,
        "Candidate actual predicate/save linkage missing");
  std::map<Ref, std::map<std::size_t, std::string>> argv;
  std::map<Ref, std::string> bodies;
  for (const auto &row : rows)
    if (row.site == 3912) {
      ByteView fields{row.body};
      (void)reference(fields);
      (void)native<std::uint8_t>(fields);
      bodies.emplace(row.own, original(fields));
    }
  unsigned literal_writes = 0;
  for (const auto &row : rows)
    if (row.site == 3932) {
      ByteView fields{row.body};
      (void)reference(fields);
      (void)native<int>(fields);
      const auto source = reference(fields);
      const auto offset = native<std::size_t>(fields);
      const auto amount = native<std::size_t>(fields);
      check(fields.empty() && bodies.contains(source) &&
                offset <= bodies.at(source).size() &&
                amount == bodies.at(source).size() - offset,
            "Candidate write body source/offset/remaining drift");
      ++literal_writes;
    }
  check(literal_writes > 0, "Candidate actual body writes not reached");
  bool literal_output = false;
  std::map<Ref, unsigned> closes;
  std::map<Ref, int> opened;
  for (const auto &row : rows) {
    ByteView fields{row.body};
    if (row.site == 3900) {
      check(native<int>(fields) == 4 && fields.empty(), "Candidate actual argc shape");
    }
    if (row.site == 3901) {
      const auto owner = reference(fields);
      const auto index = native<std::size_t>(fields);
      const auto bytes = original(fields);
      check(!bytes.empty() && bytes.back() == '\0',
            "Candidate actual CString terminator missing");
      check(argv[owner].emplace(index, bytes).second, "Candidate duplicate argv index");
    }
    if (row.site == 3912) {
      (void)reference(fields);
      const auto kind = native<std::uint8_t>(fields);
      const auto bytes = original(fields);
      literal_output |= kind == 3 && bytes == output;
    }
    if (row.site == 3934) {
      const auto owner = reference(fields);
      const int fd = native<int>(fields);
      (void)native<std::uint8_t>(fields);
      check(fields.empty(), "Candidate acquired fd shape");
      opened[owner] = fd;
    }
    if (row.site == 3935) {
      const auto owner = reference(fields);
      const int fd = native<int>(fields);
      const int returned = native<int>(fields);
      const bool failed = native<bool>(fields);
      if (failed)
        (void)native<int>(fields);
      check(fields.empty() && returned == 0 && !failed, "Candidate real close outcome");
      check(opened.contains(owner) && opened.at(owner) == fd,
            "Candidate close detached from actual acquisition");
      ++closes[owner];
    }
  }
  check(literal_output, "Candidate raw completed stdout lost binary bytes");
  check(opened.size() == closes.size(), "Candidate acquired descriptor not closed");
  for (const auto &[owner, amount] : closes) {
    (void)owner;
    check(amount == 1, "Candidate descriptor closed twice");
  }
  std::multiset<std::vector<std::string>> actual_lists, expected_lists;
  std::multiset<std::string> actual_arguments, expected_arguments;
  for (const auto &[owner, values] : argv) {
    (void)owner;
    check(values.size() == 4,
          "Candidate original argv elements not grouped with actual call");
    std::vector<std::string> ordered;
    for (const auto &[index, value] : values) {
      (void)index;
      actual_arguments.insert(value);
      ordered.push_back(value);
    }
    actual_lists.insert(std::move(ordered));
  }
  for (const auto &values : expected_argv) {
    std::vector<std::string> actual_buffers;
    for (const auto &value : values) {
      auto buffer = value;
      buffer.push_back('\0');
      actual_buffers.push_back(buffer);
      expected_arguments.insert(buffer);
    }
    expected_lists.insert(std::move(actual_buffers));
  }
  check(actual_lists == expected_lists, "Candidate argv element order changed");
  check(actual_arguments == expected_arguments,
        "Candidate argv was joined/reencoded/omitted");
  check(disk(repo / "unrelated") == "keep original dirty bytes\n",
        "Candidate touched unrelated primary dirt");
}
void native_owners() {
  Directory directory{"native"};
  {
    auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(73),
                                           {{32 * 1024 * 1024, 32768, 32768}, 32768}));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto parent = RawEventRef{raw_lane_epoch(), unwrap(raw_emit(9210, {}))};
    const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
    const auto file = directory.path / "owned";
    Descriptor descriptor{&context};
    const int flags = O_CREAT | O_RDWR | O_CLOEXEC;
    const mode_t mode = 0600;
    const std::uint8_t method = 1;
    const int fd =
        native_call(&context, {3930, 3931},
                    std::array{raw_native_field(method), raw_native_field(flags),
                               raw_native_field(mode)},
                    [&](RawEventRef request) {
                      const int result = ::open(file.c_str(), flags, mode);
                      descriptor.accept(result, request);
                      return result;
                    });
    check(fd >= 0, "actual native owner open");
    descriptor.observed(1);
    native_cut::observed = fd;
    native_cut::active = true;
    native_cut::calls = 0;
    const std::string bytes = "native original bytes";
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      const auto requested = bytes.size() - offset;
      const auto amount = native_call(
          &context, {3932, 3933},
          std::array{raw_native_field(fd), raw_native_field(offset),
                     raw_native_field(requested)},
          [&](RawEventRef) { return ::write(fd, bytes.data() + offset, requested); });
      check(amount > 0, "real shortened native write");
      offset += static_cast<std::size_t>(amount);
    }
    native_cut::active = false;
    check(native_cut::calls == 2, "actual native partial write loop");
    int cancellation[2];
    check(::pipe(cancellation) == 0, "native sticky cancellation pipe");
    const char sticky = 'x';
    check(native_cut::original(cancellation[1], &sticky, 1) == 1,
          "native cancellation readiness");
    std::vector<RawControlToken> pressure;
    for (std::uint64_t size = 32768; size; size /= 2)
      while (true) {
        auto credit = raw_try_claim_control(size);
        if (!credit.has_value())
          break;
        pressure.push_back(std::move(credit).value());
      }
    RawWaitControl control;
    control.cancellation_fds = {cancellation[0], -1};
    RawWaitScope wait{control};
    check(descriptor.close() == 0,
          "paid close blocked behind ordinary CONTROL/cancellation");
    errno = 0;
    check(::fcntl(fd, F_GETFD) < 0 && errno == EBADF,
          "actual fd remained live after paid close");
    pressure.clear();
    ::close(cancellation[0]);
    ::close(cancellation[1]);
    check(disk(file) == bytes, "native partial loop changed written bytes");
  }
  const auto rows = records(directory.path);
  check(count(rows, 3934) == 1 && count(rows, 3935) == 1,
        "actual held fd close exactly once");
  std::vector<ssize_t> writes;
  for (const auto &row : rows)
    if (row.site == 3933) {
      ByteView fields{row.body};
      (void)reference(fields);
      writes.push_back(native<ssize_t>(fields));
      check(!native<bool>(fields) && fields.empty(), "native ssize_t/errno shape");
    }
  check(writes == std::vector<ssize_t>{3, 18}, "raw native partial ssize_t originals");
}
void save_refusal() {
  Directory directory{"refusal"};
  const auto pool = directory.path / "pool", repo = directory.path / "primary";
  fs::create_directory(pool);
  fs::create_directory(repo);
  subprocess({"git", "init", "-q", repo.string()});
  {
    auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(74),
                                           {{16 * 1024 * 1024, 32768, 32768}, 32768}));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto parent = RawEventRef{raw_lane_epoch(), unwrap(raw_emit(9210, {}))};
    const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
    Manager manager{pool, &context};
    bool refused = false;
    try {
      (void)manager.perform(
          "init", object({{"repo", string(repo.string())}, {"cap", number(1)}}));
    } catch (const Error &error) {
      refused = error.code == ErrorCode::capacity;
    }
    check(refused,
          "Candidate packet original capacity did not refuse before encoder/write");
    check(!fs::exists(pool / "state.bbm"), "Candidate capture refusal replaced state");
  }
  const auto rows = records(directory.path);
  check(count(rows, 3910) == 1 && count(rows, 3911) == 1,
        "Candidate failed save missing terminal");
  check(count(rows, 3932) == 0, "Candidate capture refusal wrote a record body");
}
void cpp_failures() {
  Directory directory{"cpp"};
  const fs::path input{std::string(140, 'q')};
  const auto expected = fs::canonical(directory.path);
  {
    auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(76)));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto parent = RawEventRef{raw_lane_epoch(), unwrap(raw_emit(9210, {}))};
    const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
    native_cut::deny_allocation = true;
    bool allocation = false;
    try {
      (void)path_text(input, &context);
    } catch (const std::bad_alloc &) {
      allocation = true;
    }
    native_cut::deny_allocation = false;
    check(allocation && native_cut::allocation_refusals == 1,
          "actual C++ path-string acquisition allocation cut not reached");
    const auto returned = ::canonical(directory.path, &context);
    check(returned == expected, "selected canonical returned path changed");
    bool missing = false;
    try {
      (void)::canonical(directory.path / "missing", &context);
    } catch (const fs::filesystem_error &error) {
      missing = error.code().value() == ENOENT;
    }
    check(missing, "actual native canonical filesystem_error changed");
  }
  const auto rows = records(directory.path);
  check(count(rows, 3940) == 1 && count(rows, 3941) == 1 && count(rows, 3942) == 0,
        "Cpp allocation invented completed original");
  std::map<Ref, Ref> file_calls;
  for (const auto &row : rows)
    if (row.site == 3410) {
      ByteView fields{row.body};
      const auto parent = reference(fields);
      check(native<std::uint8_t>(fields) == 4,
            "Candidate used wrong File native method for canonical");
      (void)native<std::size_t>(fields);
      check(fields.empty(), "File canonical owner request extent");
      file_calls.emplace(row.own, parent);
    }
  check(file_calls.size() == 2, "actual canonical bypassed existing File owner");
  bool returned_path = false;
  bool allocation = false, missing = false;
  for (const auto &row : rows)
    if (row.site == 3941 || row.site == 3939) {
      ByteView fields{row.body};
      const auto caller = reference(fields);
      const auto branch = native<std::uint8_t>(fields);
      (void)native<std::uint8_t>(fields);
      check(!native<bool>(fields), "Cpp standard native outcome invented Error");
      if (row.site == 3941) {
        allocation = branch == 4 && fields.empty();
      } else if (branch == 1) {
        check(!native<bool>(fields) && native<bool>(fields),
              "canonical returned native flags");
        const auto owner = reference(fields);
        const auto length = native<std::size_t>(fields);
        returned_path = fields.empty() && file_calls.contains(owner) &&
                        file_calls.at(owner) == caller &&
                        length == expected.native().size();
      } else {
        check(branch == 6 && native<bool>(fields), "Cpp filesystem native code absent");
        missing =
            native<int>(fields) == ENOENT && !native<bool>(fields) && fields.empty();
      }
    }
  check(allocation && missing && returned_path,
        "actual C++ allocation/filesystem outcome bytes missing");
}
void native_long_cwd() {
#if defined(__linux__)
  Directory directory{"long-cwd"};
  const int saved = ::open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  check(saved >= 0, "native saved cwd descriptor");
  std::vector<int> directories;
  directories.push_back(
      ::open(directory.path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  check(directories.back() >= 0, "native long root descriptor");
  const std::string component(180, 'd');
  std::string expected = fs::canonical(directory.path).native();
  try {
    while (expected.size() <= static_cast<std::size_t>(PATH_MAX) + 512) {
      check(::mkdirat(directories.back(), component.c_str(), 0700) == 0,
            "native long component mkdirat");
      const int opened = ::openat(directories.back(), component.c_str(),
                                  O_RDONLY | O_DIRECTORY | O_CLOEXEC);
      check(opened >= 0, "native long component openat");
      directories.push_back(opened);
      expected += '/';
      expected += component;
    }
    check(::fchdir(directories.back()) == 0, "native deep fchdir");
    check(fs::current_path().native() == expected,
          "actual GNU uncapped current-directory reference");
    std::error_code ordinary_error;
    const auto ordinary = fs::canonical(".", ordinary_error);
    check(ordinary.empty() && ordinary_error.value() == ENAMETOOLONG,
          "actual GNU canonical rejects the long absolute native pathname");
    {
      auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(78)));
      auto handle = unwrap(runtime->prepare_thread());
      RawThreadBinding binding{handle};
      const auto parent = RawEventRef{raw_lane_epoch(), unwrap(raw_emit(9210, {}))};
      const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
      bool rejected = false;
      try {
        (void)::canonical(".", &context);
      } catch (const fs::filesystem_error &error) {
        rejected = error.code() == ordinary_error && error.path1() == fs::path{"."};
      }
      check(rejected, "selected canonical preserves actual GNU long-path error");
    }
    check(::fchdir(saved) == 0, "restore native cwd");
    const auto rows = records(directory.path);
    bool acquired = false, linked = false;
    std::map<Ref, Ref> owners;
    std::set<Ref> failed_helpers;
    for (const auto &row : rows) {
      ByteView fields{row.body};
      if (row.site == 3410) {
        const auto parent = reference(fields);
        check(native<std::uint8_t>(fields) == 4, "long canonical File method");
        owners.emplace(row.own, parent);
      }
      if (row.site == 3407) {
        (void)reference(fields);
        const auto role = native<std::uint8_t>(fields);
        if (role == 2) {
          const auto extent = native<std::size_t>(fields);
          const bool known = native<bool>(fields);
          const auto used = known ? native<std::size_t>(fields) : std::size_t{0};
          acquired |= known && used == expected.size() + 1 && extent == used &&
                      fields.size() == extent &&
                      std::memcmp(fields.data(), expected.c_str(), extent) == 0;
        }
      }
      if (row.site == 3411) {
        const auto owner = reference(fields);
        check(native<RawFileBranch>(fields) == RawFileBranch::returned &&
                  native<std::size_t>(fields) == 0 && native<bool>(fields) &&
                  native<int>(fields) == ENAMETOOLONG && fields.empty(),
              "long canonical File terminal preserves empty path and native error");
        failed_helpers.insert(owner);
      }
      if (row.site == 3939) {
        const auto parent = reference(fields);
        check(native<RawOperationBranch>(fields) == RawOperationBranch::standard,
              "long canonical standard exception");
        (void)native<std::uint8_t>(fields);
        check(!native<bool>(fields) && native<bool>(fields) &&
                  native<int>(fields) == ENAMETOOLONG && !native<bool>(fields) &&
                  fields.empty(),
              "long canonical reports native code without invented returned path");
        linked = owners.size() == 1 && owners.begin()->second == parent &&
                 failed_helpers.contains(owners.begin()->first);
      }
    }
    check(acquired && linked,
          "long canonical native cwd acquisition/caller File link missing");
  } catch (...) {
    (void)::fchdir(saved);
    for (std::size_t index = directories.size(); index > 1; --index) {
      ::close(directories[index - 1]);
      (void)::unlinkat(directories[index - 2], component.c_str(), AT_REMOVEDIR);
    }
    ::close(directories.front());
    ::close(saved);
    throw;
  }
  for (std::size_t index = directories.size(); index > 1; --index) {
    ::close(directories[index - 1]);
    check(::unlinkat(directories[index - 2], component.c_str(), AT_REMOVEDIR) == 0,
          "native deep directory cleanup");
  }
  ::close(directories.front());
  ::close(saved);
#else
  std::cerr << "GNU long-cwd native profile remains pending Linux availability\n";
#endif
}
void scoped_deadline_origin() {
  Directory directory{"deadline-origin"};
  auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(77)));
  auto handle = unwrap(runtime->prepare_thread());
  RawThreadBinding binding{handle};
  RawCaptureFailureScope previous{true};
  const Error retained{ErrorCode::io, EBADF};
  raw_capture_failed(retained);
  const Error timeout{ErrorCode::interrupted, ETIMEDOUT};
  {
    RawCaptureFailureScope local{true};
    RawWaitControl control;
    control.deadline = detail::Clock::now() - std::chrono::seconds(1);
    RawWaitScope wait{control};
    const auto actual = raw_claim_control(16);
    check(!actual.has_value() && actual.error() == timeout,
          "actual expired admission deadline");
    check(!local.consume({ErrorCode::interrupted, ECANCELED}),
          "local scope consumed unequal cancellation");
    check(local.consume(actual.error()) && !local.error().has_value(),
          "local scope failed actual deadline translation");
    local.finish();
    check(!local.consume(timeout), "inactive scope consumed an error");
  }
  check(previous.error().has_value() && *previous.error() == retained,
        "deadline translation cleared previous owner's failure");
}
void run_capture_failure() {
  Directory directory{"running-refusal"};
  const auto repo = directory.path / "primary", pool = directory.path / "pool";
  fs::create_directory(repo);
  fs::create_directory(pool);
  subprocess({"git", "init", "-q", repo.string()});
  subprocess({"git", "-C", repo.string(), "config", "user.name", "Native Candidate"});
  subprocess({"git", "-C", repo.string(), "config", "user.email",
              "candidate@example.invalid"});
  subprocess({"git", "-C", repo.string(), "config", "commit.gpgsign", "false"});
  put(repo / "source", "original\n");
  subprocess({"git", "-C", repo.string(), "add", "source"});
  subprocess({"git", "-C", repo.string(), "commit", "-qm", "original"});
  auto base = subprocess({"git", "-C", repo.string(), "rev-parse", "HEAD"});
  while (!base.empty() && base.back() == '\n')
    base.pop_back();
  {
    Manager seed{pool};
    (void)seed.perform("init",
                       object({{"repo", string(repo.string())}, {"cap", number(1)}}));
    (void)seed.perform("enqueue",
                       object({{"name", string("capture-once")},
                               {"base", string(base)},
                               {"hypothesis", string("actual child stop")},
                               {"discriminator", string("PID gone before return")},
                               {"identities", object({{"program", string("literal")}})},
                               {"baseline", string(base)}}));
    (void)seed.perform("dispatch", object({}));
  }
  const auto pid_file = directory.path / "pid",
             forbidden = directory.path / "forbidden";
  int cancellation[2];
  check(::pipe(cancellation) == 0, "actual run cancellation pipe");
  bool signalled = false, refused = false;
  {
    auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment(75)));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto parent = RawEventRef{raw_lane_epoch(), unwrap(raw_emit(9210, {}))};
    const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
    Manager manager{pool, &context};
    std::jthread interrupt{[&] {
      const auto deadline = detail::Clock::now() + std::chrono::seconds(8);
      while (!fs::exists(pid_file) && detail::Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      if (fs::exists(pid_file)) {
        const char sticky = 'x';
        signalled = native_cut::original(cancellation[1], &sticky, 1) == 1;
      }
    }};
    RawWaitControl control;
    control.cancellation_fds = {cancellation[0], -1};
    control.deadline = detail::Clock::now() + std::chrono::seconds(10);
    RawWaitScope wait{control};
    try {
      (void)manager.perform(
          "run",
          object({{"slot", number(0)},
                  {"seconds", number(12)},
                  {"argv",
                   Value{Value::Array{string("/bin/sh"), string("-c"),
                                      string("echo $$ > \"$1\"; printf prefix; sleep "
                                             "10; printf forbidden > \"$2\""),
                                      string("native"), string(pid_file.string()),
                                      string(forbidden.string())}}}}));
    } catch (const Error &error) {
      refused = error.code == ErrorCode::interrupted;
    }
    interrupt.join();
    check(
        signalled && refused,
        "actual selected run capture failure was swallowed into ordinary unknown save");
    const auto pid = static_cast<pid_t>(std::stoi(disk(pid_file)));
    errno = 0;
    check(::kill(pid, 0) < 0 && errno == ESRCH,
          "owned child survived capture failure return");
    check(!fs::exists(forbidden), "failed capture allowed later child effect");
    const auto state = unwrap(decode_packet_string(disk(pool / "state.bbm")));
    const auto *slots = state.find("slots");
    check(slots && slots->array()[0].find("status")->string() == "running",
          "capture failure published replacement settled state");
    unsigned intents = 0, observations = 0;
    for (const auto &entry : fs::directory_iterator(pool / "records")) {
      const auto record = decode_packet_string(disk(entry.path()));
      if (!record.has_value())
        continue;
      const auto *action = record.value().find("action");
      if (!action)
        continue;
      intents += action->string() == "run.intent" ? 1U : 0U;
      observations += action->string() == "run.observation" ? 1U : 0U;
    }
    check(intents == 1 && observations == 0,
          "capture failure replayed or settled durable run intent");
  }
  ::close(cancellation[0]);
  ::close(cancellation[1]);
  const auto rows = records(directory.path);
  check(count(rows, 3918) == 1 && count(rows, 3919) == 1 && count(rows, 3921) == 0,
        "capture failure fabricated completed output");
  bool interrupted = false;
  for (const auto &row : rows)
    if (row.site == 3919) {
      ByteView fields{row.body};
      (void)reference(fields);
      check(native<std::uint8_t>(fields) == 3,
            "capture run terminal did not retain native Error");
      (void)native<std::uint8_t>(fields);
      check(native<bool>(fields), "capture terminal native Error absent");
      interrupted = native<std::uint8_t>(fields) == 13 &&
                    native<std::int64_t>(fields) == ECANCELED && fields.empty();
    }
  check(interrupted, "capture terminal lost actual interrupted origin");
}
} // namespace fixture
int main(int argc, char **argv) {
  try {
#if defined(__linux__)
    native_cut::original = __real_write;
#else
    native_cut::original =
        reinterpret_cast<native_cut::Write>(::dlsym(RTLD_NEXT, "write"));
#endif
    fixture::check(native_cut::original && argc == 2, "Candidate native fixture setup");
    std::cerr << "Candidate CLI native originals\n";
    fixture::cli(fs::absolute(argv[1]));
    std::cerr << "Candidate paid native fd and partial write\n";
    fixture::native_owners();
    std::cerr << "Candidate packet/save capture refusal\n";
    fixture::save_refusal();
    std::cerr << "Candidate actual running capture failure cleanup\n";
    fixture::run_capture_failure();
    std::cerr << "Candidate actual C++ path allocation/filesystem error\n";
    fixture::cpp_failures();
    std::cerr << "Candidate actual scoped deadline origin\n";
    fixture::scoped_deadline_origin();
    fixture::native_long_cwd();
    return 0;
  } catch (const blackbird::Error &error) {
    std::cerr << blackbird::error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
