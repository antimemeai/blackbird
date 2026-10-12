#include "blackbird/coding.hpp"
#include "blackbird/raw_authored.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/raw_runtime.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#if defined(__linux__)
lua_State *__real_luaL_newstate();
int __real_luaL_loadbufferx(lua_State *, const char *, std::size_t, const char *,
                            const char *);
lua_Number __real_lua_tonumberx(lua_State *, int, int *);
#endif
}
#include <bit>
#include <csignal>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <source_location>
#include <thread>
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/proc.h>
#endif
#include <unistd.h>

namespace {
using namespace blackbird;
std::atomic<pid_t> stopped_recorder{-1};
void check(bool good, int line) {
  if (!good) {
    std::fprintf(stderr, "raw_authored:%d\n", line);
    const auto recorder = stopped_recorder.load();
    if (recorder > 0)
      (void)::kill(recorder, SIGCONT);
    std::abort();
  }
}
#define CHECK(...) check((__VA_ARGS__), __LINE__)
template <class T>
T take(Result<T> result,
       std::source_location location = std::source_location::current()) {
  if (!result.has_value())
    std::fprintf(stderr, "raw_authored native error:%s:%lld at %u\n",
                 error_name(result.error().code),
                 static_cast<long long>(result.error().detail), location.line());
  CHECK(result.has_value());
  return std::move(result).value();
}
struct Directory {
  std::array<char, 64> name{};
  Directory() {
    const std::string pattern = "/tmp/blackbird-raw-authored-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), name.begin());
    CHECK(::mkdtemp(name.data()) != nullptr);
  }
  ~Directory() { std::filesystem::remove_all(name.data()); }
};
void joined_owner_transfer() {
  Directory directory;
  IdentityBytes identity{};
  identity[0] = std::byte{39};
  const auto environment = take(EnvironmentId::from_bytes(identity));
  IdentityBytes original_epoch{};
  CHECK(!raw_current_thread());
  {
    auto runtime = take(RawRuntime::open(directory.name.data(), environment,
                                         {{4096, 4096, 4096}, 4096}));
    auto other = take(runtime->prepare_thread());
    RawThreadHandle retained;
    std::optional<RawControlToken> end;
    std::thread first{[&] {
      const auto prepared = take(runtime->prepare_thread());
      RawThreadBinding binding{prepared};
      retained = raw_current_thread();
      CHECK(retained == prepared);
      original_epoch = raw_lane_epoch();
      {
        RawThreadBinding nested{other};
        CHECK(raw_current_thread() == other && raw_lane_epoch() != original_epoch);
      }
      CHECK(raw_current_thread() == retained && raw_lane_epoch() == original_epoch);
      end.emplace(take(raw_claim_control(16)));
      CHECK(take(raw_emit(9250, {})) == 1);
    }};
    first.join();
    CHECK(!raw_current_thread());
    std::thread successor{[&] {
      RawThreadBinding unrelated{other};
      {
        RawThreadBinding old_vm{retained};
        CHECK(raw_lane_epoch() == original_epoch);
        if (!end.has_value())
          std::abort();
        CHECK(take(raw_emit(9251, {}, RawPartition::control, &*end)) == 2);
      }
      CHECK(raw_current_thread() == other && raw_lane_epoch() != original_epoch);
    }};
    successor.join();
    CHECK(!raw_current_thread());
    if (!end.has_value())
      std::abort();
    CHECK(end->remaining() == 0);
  }
  const auto raw = std::filesystem::path{directory.name.data()} / "raw";
  auto entries = std::filesystem::directory_iterator{raw};
  CHECK(entries != std::filesystem::directory_iterator{});
  auto reader = take(RawRunReader::open(entries->path().string()));
  CHECK(reader->snapshot().closed);
  std::array<std::byte, 16> bytes{};
  unsigned seen = 0;
  while (const auto record = take(reader->read(bytes))) {
    CHECK(record->origin.lane == original_epoch && record->origin.source_start == 0);
    CHECK(record->raw.size() == 16);
    std::uint32_t length = 0, site = 0;
    std::uint64_t ordinal = 0;
    std::memcpy(&length, record->raw.data(), 4);
    std::memcpy(&site, record->raw.data() + 4, 4);
    std::memcpy(&ordinal, record->raw.data() + 8, 8);
    CHECK(length == 16);
    if (record->origin.partition == RawPartition::data)
      CHECK(site == 9250 && ordinal == 1);
    else
      CHECK(record->origin.partition == RawPartition::control && site == 9251 &&
            ordinal == 2);
    ++seen;
  }
  CHECK(seen == 2);
}
template <class T> void append(std::vector<std::byte> &bytes, const T &value) {
  const auto raw = std::as_bytes(std::span{&value, std::size_t{1}});
  bytes.insert(bytes.end(), raw.begin(), raw.end());
}
void native_originals() {
  Directory directory;
  IdentityBytes identity{}, generation{}, lane{};
  identity[0] = std::byte{44};
  generation[0] = std::byte{45};
  std::vector<std::pair<std::uint32_t, std::vector<std::byte>>> expected;
  {
    auto runtime = take(RawRuntime::open(directory.name.data(),
                                         take(EnvironmentId::from_bytes(identity)),
                                         {{4096, 4096, 4096}, 4096}));
    RawThreadBinding binding{take(runtime->prepare_thread())};
    lane = raw_lane_epoch();
    const auto vm = take(raw_emit(9252, {}));
    RawAuthoredOwner owner{generation, {lane, vm}, true};
    const auto body = [&](std::int64_t site) {
      std::vector<std::byte> bytes{generation.begin(), generation.end()};
      bytes.insert(bytes.end(), lane.begin(), lane.end());
      append(bytes, vm);
      append(bytes, site);
      return bytes;
    };
    const std::int64_t integer = INT64_MIN;
    auto integer_body = body(7);
    append(integer_body, integer);
    expected.emplace_back(1510, std::move(integer_body));
    CHECK(take(raw_authored_emit(owner, 7, integer)) == 2);
    const double real = -0.0;
    auto real_body = body(8);
    append(real_body, real);
    expected.emplace_back(1511, std::move(real_body));
    CHECK(take(raw_authored_emit(owner, 8, real)) == 3);
    const bool boolean = false;
    auto bool_body = body(9);
    append(bool_body, boolean);
    expected.emplace_back(1514, std::move(bool_body));
    CHECK(take(raw_authored_emit(owner, 9, boolean)) == 4);
    const std::array bytes{std::byte{0}, std::byte{255}, std::byte{17}};
    auto bytes_body = body(10);
    append(bytes_body, bytes.size());
    bytes_body.insert(bytes_body.end(), bytes.begin(), bytes.end());
    expected.emplace_back(1513, std::move(bytes_body));
    CHECK(take(raw_authored_emit(owner, 10, ByteView{bytes})) == 5);
    const auto rejected = raw_authored_emit(owner, 0, integer);
    CHECK(!rejected.has_value() && rejected.error().code == ErrorCode::invalid_range);
    auto refusal_body = body(0);
    append(refusal_body, std::uint8_t{1});
    append(refusal_body, ErrorCode::invalid_range);
    append(refusal_body, std::int64_t{0});
    expected.emplace_back(1516, std::move(refusal_body));
  }
  const auto run = std::filesystem::directory_iterator{
      std::filesystem::path{directory.name.data()} / "raw"};
  auto reader = take(RawRunReader::open(run->path().string()));
  std::array<std::byte, 4096> bytes{};
  std::vector<
      std::pair<std::uint64_t, std::pair<std::uint32_t, std::vector<std::byte>>>>
      ordered;
  while (const auto record = take(reader->read(bytes))) {
    std::uint32_t site{};
    std::uint64_t ordinal{};
    std::memcpy(&site, record->raw.data() + 4, 4);
    std::memcpy(&ordinal, record->raw.data() + 8, 8);
    if (site >= 1510 && site <= 1516)
      ordered.emplace_back(
          ordinal, std::pair{site, std::vector<std::byte>{record->raw.begin() + 16,
                                                          record->raw.end()}});
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  std::vector<std::pair<std::uint32_t, std::vector<std::byte>>> actual;
  for (auto &entry : ordered)
    actual.push_back(std::move(entry.second));
  CHECK(actual == expected);
}
template <class T> T identity(unsigned char value) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{value};
  return take(T::from_bytes(bytes));
}
class UnusedProvider final : public CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw Error{ErrorCode::corrupt};
  }
};
struct LuaFixture {
  Directory directory;
  std::unique_ptr<RetainedState> root = take(
      RetainedState::create(std::make_unique<NativeJournalDirectory>(take(
                                NativeJournalDirectory::open(directory.name.data()))),
                            "audit",
                            {identity<EnvironmentId>(51),
                             identity<AuditStreamId>(52),
                             3,
                             {4 * 1024 * 1024, 16 * 1024 * 1024},
                             std::nullopt},
                            {64 * 1024 * 1024, 10000}));
  AuditLog log{*root};
  ContextStore context{log};
  UnusedProvider provider;
  std::unique_ptr<CodingEngine> engine =
      std::make_unique<CodingEngine>(log, context, provider, "test");
};
struct AuthoredRecord {
  std::uint32_t site;
  std::uint64_t ordinal;
  IdentityBytes lane;
  std::vector<std::byte> body;
};
template <class T> T field(ByteView bytes, std::size_t offset) {
  CHECK(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset);
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}
std::vector<AuthoredRecord> records(const Directory &directory) {
  const auto entries = take(discover_raw_runs(directory.name.data()));
  CHECK(entries.size() == 1);
  auto reader = take(RawRunReader::open(
      (std::filesystem::path{directory.name.data()} / "raw" / entries[0].name)
          .string()));
  CHECK(reader->snapshot().closed);
  std::vector<std::byte> buffer(65536);
  std::vector<AuthoredRecord> out;
  for (;;) {
    auto result = reader->read(buffer);
    if (!result.has_value() && result.error().code == ErrorCode::capacity) {
      // The rejected 4MiB+1 turn source is preserved as 1902 (64-byte prefix).
      // A refused read keeps its cursor; retry the same original, without skip.
      CHECK(buffer.size() == 65536);
      buffer.resize(4 * 1024 * 1024 + 65);
      result = reader->read(buffer);
    }
    const auto record = take(std::move(result));
    if (!record)
      break;
    const auto site = field<std::uint32_t>(record->raw, 4);
    if (site >= 1500 && site <= 1599)
      out.push_back({site,
                     field<std::uint64_t>(record->raw, 8),
                     record->origin.lane,
                     {record->raw.begin() + 16, record->raw.end()}});
  }
  return out;
}
std::vector<AuthoredRecord> sites(const std::vector<AuthoredRecord> &all,
                                  std::uint32_t site) {
  std::vector<AuthoredRecord> out;
  for (const auto &record : all)
    if (record.site == site)
      out.push_back(record);
  return out;
}
std::vector<std::byte> prefix(const AuthoredRecord &vm, std::int64_t site) {
  std::vector<std::byte> out{vm.body.begin(), vm.body.begin() + 16};
  out.insert(out.end(), vm.lane.begin(), vm.lane.end());
  append(out, vm.ordinal);
  append(out, site);
  return out;
}
std::atomic<bool> observe_numbers{false};
std::atomic<int> warning_mode{0};
std::array<double, 8> native_numbers{};
std::size_t native_number_count = 0;
std::atomic<unsigned> warning_count{0};
void warning(void *, const char *, int continuation) {
  if (continuation == 0)
    warning_count.fetch_add(1);
}
template <class Function> Function next_function(const char *name) {
  void *symbol = ::dlsym(RTLD_NEXT, name);
  CHECK(symbol != nullptr);
  Function function{};
  static_assert(sizeof(function) == sizeof(symbol));
  std::memcpy(&function, &symbol, sizeof(function));
  return function;
}
void lua_originals() {
  LuaFixture fixture;
  const std::string program = R"lua(
assert(rawequal(blackbird,arco)); assert(blackbird._module_load==nil and blackbird._module_call==nil)
assert(select('#',blackbird.emit(7,math.mininteger))==0)
blackbird.emit(8,math.maxinteger); arco.emit(9,false); blackbird.emit(10,true)
blackbird.emit(11,string.char(0,255,17,0,191)); blackbird.emit(12,'')
blackbird.emit(13,-0.0);blackbird.emit(14,math.huge);blackbird.emit(15,0/0)
local touched=false; local bad=setmetatable({}, {__tostring=function() touched=true;error('coerced') end})
assert(not pcall(blackbird.emit));assert(not pcall(blackbird.emit,1));assert(not pcall(blackbird.emit,1,2,3))
assert(not pcall(blackbird.emit,'7',2));assert(not pcall(blackbird.emit,1.0,2));assert(not pcall(blackbird.emit,0,2));assert(not pcall(blackbird.emit,-1,2))
assert(not pcall(blackbird.emit,16,bad));assert(not pcall(blackbird.emit,bad,2));assert(not touched)
local ok,native=pcall(blackbird.emit,17,nil);assert(not ok and type(native)=='userdata')
assert(not pcall(blackbird.emit,21,native));assert(not pcall(blackbird.emit,18,function()end))
assert(not pcall(blackbird.emit,19,coroutine.create(function()end)))
assert(not pcall(blackbird.emit,20,string.rep('x',32768)));return true
)lua";
  {
    auto runtime = take(RawRuntime::open(fixture.directory.name.data(),
                                         identity<EnvironmentId>(51),
                                         // Turn/Context cleanup prepays 10544;
                                         // keep the authored DATA refusal at 32768.
                                         {{32768, 32768, 4096}, 4096}));
    RawThreadBinding binding{take(runtime->prepare_thread())};
    native_number_count = 0;
    observe_numbers.store(true);
    fixture.engine->turn({"", program});
    observe_numbers.store(false);
    CHECK(fixture.engine->workflow_result() == Value{true});
    fixture.engine.reset();
  }
  const auto all = records(fixture.directory);
  const auto vms = sites(all, 1500);
  CHECK(vms.size() == 1);
  const auto &vm = vms.front();
  CHECK(vm.body.size() == 41 && field<bool>(vm.body, 16));
  IdentityBytes journal{};
  std::copy_n(vm.body.begin() + 17, 16, journal.begin());
  const SourceReference retained{take(AuditStreamId::from_bytes(journal)),
                                 field<std::uint64_t>(vm.body, 33)};
  const auto original = take(fixture.root->source(retained));
  CHECK(original.size() == program.size() &&
        std::equal(original.begin(), original.end(),
                   reinterpret_cast<const std::byte *>(program.data())));
  const auto bindings = sites(all, 1505);
  CHECK(bindings.size() == 1 && bindings[0].ordinal < vm.ordinal);
  CHECK(std::equal(vm.body.begin(), vm.body.begin() + 16, bindings[0].body.begin()));
  CHECK(field<std::uint64_t>(bindings[0].body, 32) == retained.sequence &&
        field<std::size_t>(bindings[0].body, 40) == program.size());
  bool root_loaded = false;
  for (const auto &load : sites(all, 1506)) {
    if (field<std::uint8_t>(load.body, 24) == 1) {
      CHECK(field<std::size_t>(load.body, 25) == program.size() &&
            load.body.size() == 33 + program.size());
      CHECK(std::equal(load.body.begin() + 33, load.body.end(), original.begin()));
      root_loaded = true;
      const auto returned = sites(all, 1507);
      const auto match =
          std::find_if(returned.begin(), returned.end(), [&](const auto &record) {
            return field<std::uint64_t>(record.body, 16) == load.ordinal;
          });
      CHECK(match != returned.end() && field<int>(match->body, 24) == LUA_OK &&
            field<bool>(match->body, 28));
    }
  }
  CHECK(root_loaded && native_number_count == 3);
  struct ExpectedSite {
    std::uint32_t event;
    std::int64_t local;
  };
  const auto expect = [&](ExpectedSite site, const auto &value) {
    auto expected = prefix(vm, site.local);
    append(expected, value);
    const auto actual = sites(all, site.event);
    const auto match =
        std::find_if(actual.begin(), actual.end(),
                     [&](const auto &record) { return record.body == expected; });
    CHECK(match != actual.end());
  };
  expect({1510, 7}, std::int64_t{INT64_MIN});
  expect({1510, 8}, std::int64_t{INT64_MAX});
  expect({1512, 9}, int{0});
  expect({1512, 10}, int{1});
  expect({1511, 13}, native_numbers[0]);
  expect({1511, 14}, native_numbers[1]);
  expect({1511, 15}, native_numbers[2]);
  auto binary = prefix(vm, 11);
  const std::array exact{std::byte{0}, std::byte{255}, std::byte{17}, std::byte{0},
                         std::byte{191}};
  append(binary, exact.size());
  binary.insert(binary.end(), exact.begin(), exact.end());
  const auto strings = sites(all, 1513);
  CHECK(strings.size() == 2);
  CHECK(std::any_of(strings.begin(), strings.end(),
                    [&](const auto &record) { return record.body == binary; }));
  auto empty = prefix(vm, 12);
  append(empty, std::size_t{0});
  CHECK(std::any_of(strings.begin(), strings.end(),
                    [&](const auto &record) { return record.body == empty; }));
  const auto refusals = sites(all, 1515);
  CHECK(refusals.size() == 14);
  // Literal Lua C API type values: missing=-1, nil=0, number=3, string=4,
  // table=5, function=6, userdata=7, thread=8. Arity/type originals stay exact.
  struct Rejected {
    int argc, site_type, value_type;
    bool integer;
    std::int64_t site;
    ErrorCode code;
  };
  const std::array rejected{Rejected{0, -1, -1, false, 0, ErrorCode::invalid_range},
                            Rejected{1, 3, -1, true, 1, ErrorCode::invalid_range},
                            Rejected{3, 3, 3, true, 1, ErrorCode::invalid_range},
                            Rejected{2, 4, 3, false, 0, ErrorCode::invalid_range},
                            Rejected{2, 3, 3, false, 0, ErrorCode::invalid_range},
                            Rejected{2, 3, 3, true, 0, ErrorCode::invalid_range},
                            Rejected{2, 3, 3, true, -1, ErrorCode::invalid_range},
                            Rejected{2, 3, 5, true, 16, ErrorCode::unsupported},
                            Rejected{2, 5, 3, false, 0, ErrorCode::invalid_range},
                            Rejected{2, 3, 0, true, 17, ErrorCode::unsupported},
                            Rejected{2, 3, 7, true, 21, ErrorCode::unsupported},
                            Rejected{2, 3, 6, true, 18, ErrorCode::unsupported},
                            Rejected{2, 3, 8, true, 19, ErrorCode::unsupported},
                            Rejected{2, 3, 4, true, 20, ErrorCode::capacity}};
  for (const auto &why : rejected) {
    auto expected = prefix(vm, 0);
    expected.resize(40);
    append(expected, why.argc);
    append(expected, why.site_type);
    append(expected, why.value_type);
    append(expected, why.integer);
    if (why.integer)
      append(expected, why.site);
    append(expected, why.code);
    append(expected, std::int64_t{0});
    CHECK(std::any_of(refusals.begin(), refusals.end(),
                      [&](const auto &record) { return record.body == expected; }));
  }
  CHECK(sites(all, 1504).size() == 1 && sites(all, 1509).size() == 3);
}
void error_attribution() {
  for (const bool rethrow : {false, true}) {
    LuaFixture fixture;
    {
      auto runtime = take(
          RawRuntime::open(fixture.directory.name.data(), identity<EnvironmentId>(51)));
      RawThreadBinding binding{take(runtime->prepare_thread())};
      const auto program =
          rethrow
              ? "local ok,e=pcall(blackbird.emit,1,{});assert(not ok);error(e)"
              : "assert(not pcall(blackbird.emit,1,{}));error('distinct Lua failure')";
      bool rejected = false;
      try {
        fixture.engine->turn({"", program});
      } catch (const Error &why) {
        CHECK(why.code ==
              (rethrow ? ErrorCode::unsupported : ErrorCode::external_unknown));
        rejected = true;
      }
      CHECK(rejected);
      fixture.engine.reset();
    }
    const auto all = records(fixture.directory);
    CHECK(sites(all, 1515).size() == 1);
    const auto returns = sites(all, 1509);
    CHECK(std::count_if(returns.begin(), returns.end(), [](const auto &record) {
            return field<int>(record.body, 24) != 0;
          }) == 1);
  }
}
void generations_and_sources() {
  LuaFixture fixture;
  const std::string first = R"lua(
blackbird.call('program_config',{proposal={modules=blackbird.array({{name='sample',source='blackbird.emit(7,71);return {n=71}'}}),model='',effort=''}})
blackbird.define_tool({name='authored_tool',description='test',parameters={type='object',properties={},required=blackbird.array({}),additionalProperties=false},source='blackbird.emit(7,72);return 72'})
old=setmetatable({}, {__gc=function()blackbird.emit(7,70)end});return true
)lua";
  const std::string second = "local m=blackbird.module('sample');assert(m.n==71 and "
                             "m==blackbird.module('sample'));assert(blackbird.call('"
                             "authored_tool',{})==72);blackbird.emit(7,73);return true";
  {
    auto runtime = take(RawRuntime::open(fixture.directory.name.data(),
                                         identity<EnvironmentId>(51),
                                         {{65536, 32768, 8192}, 8192}));
    const auto original = take(runtime->prepare_thread()),
               successor = take(runtime->prepare_thread());
    std::thread initial{[&] {
      RawThreadBinding binding{original};
      fixture.engine->turn({"", first});
    }};
    initial.join();
    std::thread replacement{[&] {
      RawThreadBinding binding{successor};
      fixture.engine->turn({"", second});
    }};
    replacement.join();
    std::thread close{[&] {
      RawThreadBinding binding{original};
      fixture.engine.reset();
    }};
    close.join();
  }
  const auto all = records(fixture.directory), vms = sites(all, 1500);
  CHECK(vms.size() == 2 && vms[0].lane != vms[1].lane);
  for (const auto &vm : vms)
    CHECK(vm.body.size() == 41 && field<bool>(vm.body, 16));
  CHECK(
      !std::equal(vms[0].body.begin(), vms[0].body.begin() + 16, vms[1].body.begin()));
  const auto metrics = sites(all, 1510), ends = sites(all, 1504);
  CHECK(metrics.size() == 4 && ends.size() == 2);
  for (const std::int64_t expected : {70, 71, 72, 73})
    CHECK(std::count_if(metrics.begin(), metrics.end(), [&](const auto &metric) {
            return field<std::int64_t>(metric.body, 48) == expected;
          }) == 1);
  for (const auto &metric : metrics) {
    CHECK(metric.body.size() == 56);
    const auto value = field<std::int64_t>(metric.body, 48);
    CHECK(field<std::int64_t>(metric.body, 40) == 7);
    const auto selected = std::find_if(vms.begin(), vms.end(), [&](const auto &vm) {
      return std::equal(metric.body.begin() + 16, metric.body.begin() + 32,
                        vm.lane.begin()) &&
             field<std::uint64_t>(metric.body, 32) == vm.ordinal;
    });
    CHECK(selected != vms.end() && metric.lane == selected->lane &&
          std::equal(metric.body.begin(), metric.body.begin() + 16,
                     selected->body.begin()));
    CHECK(selected->body.size() == 41 && field<bool>(selected->body, 16));
    IdentityBytes journal{};
    std::copy_n(selected->body.begin() + 17, 16, journal.begin());
    const auto source =
        take(fixture.root->source({take(AuditStreamId::from_bytes(journal)),
                                   field<std::uint64_t>(selected->body, 33)}));
    const auto &expected_source = value == 70 ? first : second;
    CHECK(source.size() == expected_source.size() &&
          std::equal(source.begin(), source.end(),
                     reinterpret_cast<const std::byte *>(expected_source.data())));
  }
  for (const auto &vm : vms) {
    const auto owns = [&](const auto &end) {
      return end.body.size() >= 24 &&
             std::equal(end.body.begin(), end.body.begin() + 16, vm.lane.begin()) &&
             field<std::uint64_t>(end.body, 16) == vm.ordinal;
    };
    CHECK(std::count_if(ends.begin(), ends.end(), owns) == 1);
    const auto &end = *std::find_if(ends.begin(), ends.end(), owns);
    std::vector<std::byte> expected{vm.lane.begin(), vm.lane.end()};
    append(expected, vm.ordinal);
    append(expected, std::uint8_t{1}); // Actual ordinary lua_close, without Error.
    CHECK(end.lane == vm.lane && end.body == expected);
    // This fixture gives each VM its own lane. Compare original lane ordinals,
    // independently of recorder ordering across DATA and CONTROL partitions.
    for (const auto &record : all)
      if (record.lane == vm.lane && record.site != 1504)
        CHECK(record.ordinal < end.ordinal);
  }
  unsigned module_loads = 0, tool_loads = 0, validation_loads = 0;
  for (const auto &load : sites(all, 1506)) {
    const auto kind = field<std::uint8_t>(load.body, 24);
    const std::string_view bytes{reinterpret_cast<const char *>(load.body.data() + 33),
                                 load.body.size() - 33};
    if (kind == 5) {
      CHECK(bytes == "blackbird.emit(7,71);return {n=71}");
      ++module_loads;
      const auto returns = sites(all, 1507);
      const auto returned =
          std::find_if(returns.begin(), returns.end(), [&](const auto &record) {
            return record.lane == load.lane &&
                   field<std::uint64_t>(record.body, 16) == load.ordinal;
          });
      CHECK(returned != returns.end() && field<int>(returned->body, 24) == 0);
      const auto executions = sites(all, 1517);
      CHECK(std::any_of(executions.begin(), executions.end(), [&](const auto &record) {
        return record.lane == load.lane && field<const void *>(record.body, 24) ==
                                               field<const void *>(returned->body, 29);
      }));
    }
    if (kind == 2) {
      CHECK(bytes == "local fn = function(args)\nblackbird.emit(7,72);return "
                     "72\nend\nreturn fn(_native_args)");
      ++tool_loads;
    }
    if (kind == 6)
      ++validation_loads;
    const auto source_size = field<std::size_t>(load.body, 25);
    CHECK(source_size == bytes.size());
  }
  CHECK(module_loads == 1 && tool_loads == 1 && validation_loads == 2);
  CHECK(sites(all, 1517).size() == 1 && sites(all, 1518).size() == 1);
}
std::atomic<int> watched_stop{-1}, poll_notice{-1};
void stopped(pid_t pid) {
  for (;;) {
#if defined(__APPLE__)
    proc_bsdinfo observed{};
    const int size =
        ::proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &observed, sizeof(observed));
    CHECK(size == static_cast<int>(sizeof(observed)));
    if (observed.pbi_status == SSTOP)
      return;
#else
    const auto path = "/proc/" + std::to_string(pid) + "/stat";
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0);
    std::array<char, 4096> bytes{};
    const auto size = ::read(fd, bytes.data(), bytes.size());
    CHECK(::close(fd) == 0 && size > 0);
    const std::string_view raw{bytes.data(), static_cast<std::size_t>(size)};
    const auto end = raw.rfind(')');
    CHECK(end != std::string_view::npos && end + 2 < raw.size());
    if (raw[end + 2] == 'T')
      return;
#endif
    std::this_thread::yield();
  }
}
std::string channel_name(const IdentityBytes &epoch) {
  std::string name = "/bb";
  constexpr char digits[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 12; ++i) {
    const auto byte = std::to_integer<unsigned>(epoch[i]);
    name += digits[byte >> 4];
    name += digits[byte & 15];
  }
  return name;
}
void finalizers(bool warnings) {
  LuaFixture fixture;
  std::array<int, 2> stop{}, notice{};
  CHECK(::pipe(stop.data()) == 0 && ::pipe(notice.data()) == 0);
  // The real turn also holds its Context/terminal bank before the full-DATA cut.
  constexpr RawCapacities capacity{32768, 32768, 4096};
  auto runtime = take(RawRuntime::open(fixture.directory.name.data(),
                                       identity<EnvironmentId>(51), {capacity, 4096}));
  auto original = take(runtime->prepare_thread()),
       unrelated = take(runtime->prepare_thread());
  IdentityBytes lane{};
  warning_count.store(0);
  warning_mode.store(warnings ? 2 : 1);
  std::thread initial{[&] {
    RawThreadBinding binding{original};
    lane = raw_lane_epoch();
    fixture.engine->turn({"", R"lua(
first=setmetatable({}, {__gc=function()blackbird.emit(41,'first')end})
second=setmetatable({}, {__gc=function()blackbird.emit(42,'second')end})
invalid=setmetatable({}, {__gc=function()blackbird.emit(43,{})end});return true
)lua"});
  }};
  initial.join();
  warning_mode.store(0);
  const auto status = take(runtime->status());
  stopped_recorder.store(status.recorder);
  CHECK(::kill(status.recorder, SIGSTOP) == 0);
  stopped(status.recorder);
  auto channel = take(RawChannel::open(channel_name(lane), lane, capacity));
  {
    RawThreadBinding binding{original};
    const auto cursor = take(channel.cursors(RawPartition::data));
    const auto available = capacity[0] - (cursor.published - cursor.acknowledged);
    CHECK(available >= 16);
    std::vector<std::byte> filler(static_cast<std::size_t>(available) - 16);
    CHECK(raw_wait_data(available).has_value());
    const std::array fields{ByteView{filler}};
    CHECK(raw_emit(9253, fields).has_value());
  }
  watched_stop.store(stop[0]);
  poll_notice.store(notice[1]);
  std::thread close{[&] {
    RawThreadBinding binding{unrelated};
    RawWaitControl control;
    control.cancellation_fds[0] = stop[0];
    RawWaitScope caller{control};
    fixture.engine.reset();
  }};
  char entered{};
  CHECK(::read(notice[0], &entered, 1) == 1 && entered == 'p');
  const auto full = take(channel.cursors(RawPartition::data));
  CHECK(full.published - full.acknowledged == capacity[0]);
  const char canceled = 'c';
  CHECK(::write(stop[1], &canceled, 1) == 1);
  close.join();
  watched_stop.store(-1);
  CHECK(warning_count.load() == (warnings ? 3U : 0U));
  CHECK(take(channel.cursors(RawPartition::failure)).published == 0);
  CHECK(::kill(status.recorder, SIGCONT) == 0);
  // Registered handles must retire before the runtime's orderly drain.
  original.reset();
  unrelated.reset();
  runtime.reset();
  stopped_recorder.store(-1);
  for (const auto fd : {stop[0], stop[1], notice[0], notice[1]})
    CHECK(::close(fd) == 0);
  const auto all = records(fixture.directory);
  CHECK(sites(all, 1513).empty());
  const auto refusals = sites(all, 1515);
  CHECK(refusals.size() == 2);
  CHECK(std::any_of(refusals.begin(), refusals.end(), [](const auto &record) {
    return field<std::int64_t>(record.body, 53) == 43 &&
           field<ErrorCode>(record.body, 61) == ErrorCode::unsupported;
  }));
  CHECK(std::any_of(refusals.begin(), refusals.end(), [](const auto &record) {
    return field<std::int64_t>(record.body, 53) == 42 &&
           field<ErrorCode>(record.body, 61) == ErrorCode::interrupted;
  }));
  const auto ends = sites(all, 1504);
  CHECK(ends.size() == 1 && ends[0].lane == lane && ends[0].body.size() == 34);
  CHECK(field<std::uint8_t>(ends[0].body, 24) == 1 &&
        field<ErrorCode>(ends[0].body, 25) == ErrorCode::interrupted);
}
std::atomic<bool> fail_allocation{false}, fail_load{false};
void *deny_allocator(void *, void *, std::size_t, std::size_t) { return nullptr; }
struct Allocator {
  lua_Alloc original;
  void *userdata;
};
void *allocate_growth(const Allocator &allocator, void *pointer, std::size_t before,
                      std::size_t after) {
  if (after > before)
    return nullptr;
  return allocator.original(allocator.userdata, pointer, before, after);
}
void *deny_growth(void *userdata, void *pointer, std::size_t before,
                  std::size_t after) {
  return allocate_growth(*static_cast<const Allocator *>(userdata), pointer, before,
                         after);
}
void disabled_vm_owner() {
  LuaFixture fixture;
  // This VM never admitted raw capture. A later binding cannot invent its VM R.
  fixture.engine->turn({"", R"lua(
    assert(select('#',blackbird.emit(7,42))==0)
    assert(not pcall(blackbird.emit,7,{}))
    held=setmetatable({}, {__gc=function() blackbird.emit(8,43) end})
    return true
  )lua"});
  {
    auto runtime = take(
        RawRuntime::open(fixture.directory.name.data(), identity<EnvironmentId>(51)));
    RawThreadBinding binding{take(runtime->prepare_thread())};
    fixture.engine.reset();
    CHECK(take(raw_emit(9254, {})) == 1);
  }
  CHECK(records(fixture.directory).empty());
}
void failure_branches() {
  for (const unsigned branch : {0U, 1U, 2U, 3U}) {
    LuaFixture fixture;
    {
      auto runtime = take(
          RawRuntime::open(fixture.directory.name.data(), identity<EnvironmentId>(51)));
      RawThreadBinding binding{take(runtime->prepare_thread())};
      fail_allocation.store(branch == 0);
      fail_load.store(branch == 1);
      const std::string program = branch == 2   ? "this is not Lua !"
                                  : branch == 3 ? std::string(4 * 1024 * 1024 + 1, 'x')
                                                : "return true";
      bool rejected = false;
      try {
        fixture.engine->turn({"", program});
      } catch (const Error &why) {
        CHECK(why.code == (branch == 0   ? ErrorCode::allocation
                           : branch == 3 ? ErrorCode::capacity
                                         : ErrorCode::external_unknown));
        rejected = true;
      }
      fail_allocation.store(false);
      fail_load.store(false);
      CHECK(rejected);
      fixture.engine.reset();
    }
    const auto all = records(fixture.directory), vms = sites(all, 1500),
               loads = sites(all, 1507);
    if (branch == 3) {
      CHECK(vms.empty() && sites(all, 1505).empty());
      continue;
    }
    CHECK(vms.size() == 1);
    const auto allocations = sites(all, 1501), ends = sites(all, 1504);
    CHECK(allocations.size() == 1 && ends.size() == 1);
    if (branch == 0) {
      CHECK(field<std::uint8_t>(allocations[0].body, 24) == 0 &&
            field<std::uint8_t>(ends[0].body, 24) == 2);
      CHECK(sites(all, 1502).empty() && loads.empty());
    } else {
      CHECK(field<std::uint8_t>(allocations[0].body, 24) == 1);
      const auto failed =
          std::find_if(loads.begin(), loads.end(), [](const auto &record) {
            return field<int>(record.body, 24) != LUA_OK;
          });
      CHECK(failed != loads.end() &&
            field<int>(failed->body, 24) == (branch == 1 ? LUA_ERRMEM : LUA_ERRSYNTAX));
      CHECK(!field<bool>(failed->body, 28) && failed->body.size() == 29);
      CHECK(sites(all, 1509).size() ==
            2); // Installed helpers ran; rejected root never ran.
    }
  }
}
} // namespace
extern "C" int poll(pollfd *watches, nfds_t count, int timeout) {
  using Function = int (*)(pollfd *, nfds_t, int);
  static const auto function = next_function<Function>("poll");
  const int stop = watched_stop.load();
  if (stop >= 0 && timeout != 0)
    for (nfds_t i = 0; i < count; ++i)
      if (watches[i].fd == stop) {
        const int notice = poll_notice.exchange(-1);
        if (notice >= 0) {
          const char entered = 'p';
          CHECK(::write(notice, &entered, 1) == 1);
        }
        break;
      }
  return function(watches, count, timeout);
}
#if defined(__linux__)
extern "C" lua_State *__wrap_luaL_newstate() {
  const auto function = __real_luaL_newstate;
#else
extern "C" lua_State *luaL_newstate() {
  using Function = lua_State *(*)();
  static const auto function = next_function<Function>("luaL_newstate");
#endif
  lua_State *state = nullptr;
  if (fail_allocation.load()) {
    // The real native VM allocator returns null; no fabricated Lua status.
    state = lua_newstate(deny_allocator, nullptr);
  } else
    state = function();
  if (state && warning_mode.load() == 2)
    lua_setwarnf(state, warning, nullptr);
  if (state && warning_mode.load() == 1)
    lua_setwarnf(state, nullptr, nullptr);
  return state;
}
#if defined(__linux__)
extern "C" int __wrap_luaL_loadbufferx(lua_State *L, const char *bytes,
                                       std::size_t size, const char *name,
                                       const char *mode) {
  const auto function = __real_luaL_loadbufferx;
#else
extern "C" int luaL_loadbufferx(lua_State *L, const char *bytes, std::size_t size,
                                const char *name, const char *mode) {
  using Function =
      int (*)(lua_State *, const char *, std::size_t, const char *, const char *);
  static const auto function = next_function<Function>("luaL_loadbufferx");
#endif
  if (!fail_load.load() || std::strcmp(name, "arco-program") != 0)
    return function(L, bytes, size, name, mode);
  Allocator allocator{};
  allocator.original = lua_getallocf(L, &allocator.userdata);
  lua_setallocf(L, deny_growth, &allocator);
  const int outcome = function(L, bytes, size, name, mode);
  lua_setallocf(L, allocator.original, allocator.userdata);
  return outcome;
}
#if defined(__linux__)
extern "C" lua_Number __wrap_lua_tonumberx(lua_State *L, int index, int *is_number) {
  const auto function = __real_lua_tonumberx;
#else
extern "C" lua_Number lua_tonumberx(lua_State *L, int index, int *is_number) {
  using Function = lua_Number (*)(lua_State *, int, int *);
  static const auto function = next_function<Function>("lua_tonumberx");
#endif
  const auto value = function(L, index, is_number);
  if (observe_numbers.load() && index == 2) {
    CHECK(native_number_count < native_numbers.size());
    native_numbers[native_number_count++] = value;
  }
  return value;
}
int main() {
  joined_owner_transfer();
  native_originals();
  lua_originals();
  error_attribution();
  generations_and_sources();
  finalizers(false);
  finalizers(true);
  failure_branches();
  disabled_vm_owner();
  // Native byte helpers must expose the reviewed zero-transform API.
  RawAuthoredOwner owner{IdentityBytes{}, RawAuthoredReference{}, false};
  CHECK(take(raw_authored_emit(owner, 7, std::int64_t{-17})) == 0);
  CHECK(take(raw_authored_emit(owner, 8, true)) == 0);
}
