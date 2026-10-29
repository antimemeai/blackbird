#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/raw_bridge.hpp"
#include "blackbird/raw_reader.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <map>
#include <source_location>
#include <thread>
#include <unistd.h>
extern "C" {
#include <lua.h>
}
#if defined(__linux__)
extern "C" const char *__real_lua_pushlstring(lua_State *, const char *, std::size_t);
extern "C" void *__real_lua_newuserdatauv(lua_State *, std::size_t, int);
extern "C" int __real_lua_pcallk(lua_State *, int, int, int, lua_KContext,
                                 lua_KFunction);
#define TEST_PUSH __wrap_lua_pushlstring
#define TEST_USERDATA __wrap_lua_newuserdatauv
#define TEST_PCALL __wrap_lua_pcallk
#else
#define TEST_PUSH lua_pushlstring
#define TEST_USERDATA lua_newuserdatauv
#define TEST_PCALL lua_pcallk
#endif
using namespace blackbird;
namespace cut {
using Push = const char *(*)(lua_State *, const char *, std::size_t);
using Userdata = void *(*)(lua_State *, std::size_t, int);
using Protected = int (*)(lua_State *, int, int, int, lua_KContext, lua_KFunction);
Userdata userdata = nullptr;
Protected protected_call = nullptr;
thread_local lua_State *denied_state = nullptr;
Push original = nullptr;
thread_local bool gc = false, deny = false, deny_module = false;
thread_local std::string_view target;
thread_local unsigned reached = 0, denied = 0;
thread_local bool deny_holder = false;
thread_local unsigned holder_refusals = 0;
struct Allocator {
  lua_Alloc allocate;
  void *context;
};
thread_local Allocator allocator{};
void *refuse(void *, void *pointer, std::size_t old, std::size_t next) {
  if (next > old) {
    ++denied;
    return nullptr;
  }
  return allocator.allocate(allocator.context, pointer, old, next);
}
} // namespace cut
void *operator new(std::size_t size) {
  if (cut::deny_holder && size == sizeof(RawModuleToken)) {
    cut::deny_holder = false;
    ++cut::holder_refusals;
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
extern "C" const char *TEST_PUSH(lua_State *state, const char *text, std::size_t size) {
  if (!cut::target.empty() && std::string_view{text, size} == cut::target) {
    cut::target = {};
    ++cut::reached;
    if (cut::gc) {
      cut::gc = false;
      (void)lua_gc(state, LUA_GCCOLLECT, 0);
    }
    if (cut::deny) {
      cut::deny = false;
      cut::allocator.allocate = lua_getallocf(state, &cut::allocator.context);
      lua_setallocf(state, cut::refuse, nullptr);
      cut::denied_state = state;
    }
  }
  return cut::original(state, text, size);
}
extern "C" void *TEST_USERDATA(lua_State *state, std::size_t size, int values) {
  if (cut::deny_module && size == sizeof(void *) && values == 2) {
    cut::deny_module = false;
    cut::allocator.allocate = lua_getallocf(state, &cut::allocator.context);
    lua_setallocf(state, cut::refuse, nullptr);
    cut::denied_state = state;
    ++cut::reached;
  }
  return cut::userdata(state, size, values);
}
extern "C" int TEST_PCALL(lua_State *state, int arguments, int results, int handler,
                          lua_KContext context, lua_KFunction continuation) {
  const int returned =
      cut::protected_call(state, arguments, results, handler, context, continuation);
  if (cut::denied_state == state) {
    lua_setallocf(state, cut::allocator.allocate, cut::allocator.context);
    cut::denied_state = nullptr;
  }
  return returned;
}
namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
  if (!value)
    throw std::runtime_error{"bridge oracle line " + std::to_string(at.line())};
}
template <class T> T id(unsigned char first) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{first};
  return unwrap(T::from_bytes(bytes));
}
template <class T> T native(ByteView &bytes) {
  require(bytes.size() >= sizeof(T));
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
  std::uint32_t site;
  Ref own;
  std::vector<std::byte> body;
};
std::vector<Row> records(const std::filesystem::path &directory) {
  const auto runs = unwrap(discover_raw_runs(directory.string()));
  require(runs.size() == 1);
  const auto &snapshot = runs[0].snapshot;
  if (!snapshot.has_value())
    throw std::runtime_error{"missing closed snapshot"};
  require(snapshot->closed);
  auto reader = unwrap(RawRunReader::open((directory / "raw" / runs[0].name).string()));
  std::vector<std::byte> scratch(16 * 1024 * 1024);
  std::vector<Row> output;
  while (const auto row = unwrap(reader->read(scratch))) {
    ByteView body{row->raw};
    require(native<std::uint32_t>(body) == row->raw.size());
    const auto site = native<std::uint32_t>(body);
    const auto ordinal = native<std::uint64_t>(body);
    require(site != 8001);
    output.push_back({site, {row->origin.lane, ordinal}, {body.begin(), body.end()}});
  }
  return output;
}
const Row &find(const std::vector<Row> &rows, Ref own, std::uint32_t site) {
  for (const auto &row : rows)
    if (row.own == own && row.site == site)
      return row;
  require(false);
  return rows.front();
}
class Provider final : public CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw std::runtime_error{"no provider authorized"};
  }
};
struct Directory {
  std::filesystem::path path;
  explicit Directory(std::string_view name) {
    path = std::filesystem::temp_directory_path() /
           ("blackbird-bridge-" + std::to_string(::getpid()) + "-" + std::string{name});
    std::filesystem::create_directories(path);
    std::filesystem::permissions(path, std::filesystem::perms::owner_all);
  }
  ~Directory() {
    if (std::uncaught_exceptions())
      std::cerr << "preserved " << path << '\n';
    else
      std::filesystem::remove_all(path);
  }
};
template <class F>
std::vector<Row> scenario(const std::filesystem::path &path, F &&body) {
  const auto environment = id<EnvironmentId>(71);
  {
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(path.string()))),
        "audit",
        {environment, id<AuditStreamId>(72), 3, {1024 * 1024, 4 * 1024 * 1024}, {}},
        {256 * 1024 * 1024, 30000}));
    AuditLog log{*root};
    ContextStore context{log};
    Provider provider;
    CodingEngine engine{log, context, provider, "test"};
    auto runtime = unwrap(RawRuntime::open(
        path.string(), environment,
        {{256 * 1024 * 1024, 2 * 1024 * 1024, 128 * 1024}, 128 * 1024}));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    if constexpr (std::is_invocable_v<F, CodingEngine &, RawRuntime &>)
      body(engine, *runtime);
    else
      body(engine);
  }
  return records(path);
}

Value modules(Value::Array definitions) {
  return Value::object(
      {{"proposal", Value::object({{"modules", Value{std::move(definitions)}},
                                   {"model", Value{""}},
                                   {"effort", Value{""}}})}});
}
Value definition(std::string name, std::string source) {
  return Value::object(
      {{"name", Value{std::move(name)}}, {"source", Value{std::move(source)}}});
}
std::string stage(CodingEngine &engine, Value::Array definitions) {
  const auto result =
      engine.operator_call("program_config", modules(std::move(definitions)));
  require(field(result, "staged") == Value{true});
  return string_field(result, "pending_revision");
}
const Row &returned(const std::vector<Row> &rows, Ref request, std::uint32_t site);
struct Entry {
  Ref vm;
  IdentityBytes generation;
  int operation = -1, arguments = 0;
  std::optional<Ref> operation_parent, module_parent;
};
void before(const Row &first, const Row &second) {
  if (first.own.first == second.own.first)
    require(first.own.second < second.own.second);
}
Entry entry(const std::vector<Row> &rows, const Row &row) {
  ByteView body{row.body};
  Entry value{};
  value.vm = ref(body);
  value.generation = native<IdentityBytes>(body);
  ByteView vm{find(rows, value.vm, 1500).body};
  require(native<IdentityBytes>(vm) == value.generation);
  require(native<bool>(vm));
  const auto journal = native<IdentityBytes>(vm);
  const auto sequence = native<std::uint64_t>(vm);
  require(vm.empty());
  bool source_generation = false;
  for (const auto &source : rows)
    if (source.site == 1902) {
      ByteView fields{source.body};
      (void)ref(fields);
      if (native<IdentityBytes>(fields) != value.generation)
        continue;
      const auto size = native<std::size_t>(fields);
      require(size == fields.size() && size != 0);
      ByteView preserved{returned(rows, source.own, 1903).body};
      (void)ref(preserved);
      require(native<std::uint8_t>(preserved) == 0);
      require(native<IdentityBytes>(preserved) == journal &&
              native<std::uint64_t>(preserved) == sequence && preserved.empty());
      source_generation = true;
    }
  require(source_generation);
  if (row.site == 4800) {
    value.operation = native<int>(body);
    value.arguments = native<int>(body);
    require(value.operation == 1 || value.operation == 7 || value.operation == 8 ||
            value.operation == 9 || value.operation == 10);
    require(value.arguments == (value.operation == 1 ? 2 : 1));
    if (native<bool>(body))
      value.operation_parent = ref(body);
    if (native<bool>(body))
      value.module_parent = ref(body);
  } else {
    value.arguments = native<int>(body);
    require(value.arguments == 2 && native<int>(body) == LUA_TSTRING);
    if (native<bool>(body))
      value.module_parent = ref(body);
    if (native<bool>(body))
      value.operation_parent = ref(body);
  }
  require(body.empty());
  if (value.operation_parent) {
    const auto &operation = find(rows, *value.operation_parent, 2400);
    ByteView fields{operation.body};
    if (native<bool>(fields))
      (void)ref(fields);
    require(native<IdentityBytes>(fields) == value.generation);
    before(operation, row);
  }
  if (value.module_parent) {
    const auto &parent = find(rows, *value.module_parent, 4820);
    ByteView fields{parent.body};
    require(ref(fields) == value.vm &&
            native<IdentityBytes>(fields) == value.generation);
    before(parent, row);
  }
  return value;
}
const Row &returned(const std::vector<Row> &rows, Ref request, std::uint32_t site) {
  const Row *result = nullptr;
  for (const auto &row : rows)
    if (row.site == site) {
      ByteView body{row.body};
      if (ref(body) == request) {
        require(result == nullptr);
        result = &row;
      }
    }
  require(result != nullptr);
  return *result;
}
void validate(const std::vector<Row> &rows) {
  std::map<Ref, unsigned> ends;
  for (const auto &row : rows) {
    ByteView body{row.body};
    if (row.site == 4800 || row.site == 4820)
      (void)entry(rows, row);
    if (row.site == 4801 || row.site == 4825) {
      const auto parent = ref(body);
      const auto &request = find(rows, parent, row.site == 4801 ? 4800 : 4820);
      (void)entry(rows, request);
      before(request, row);
      ++ends[parent];
      const auto branch = native<RawOperationBranch>(body);
      const auto stage = native<std::uint8_t>(body);
      const bool has_error = native<bool>(body);
      std::optional<Error> error;
      if (has_error)
        error = Error{native<ErrorCode>(body), native<std::int64_t>(body)};
      if (row.site == 4801) {
        require(stage == 5);
        const bool success = native<bool>(body);
        require(success == (branch == RawOperationBranch::returned) &&
                has_error != success);
        if (success)
          require(native<int>(body) == 1);
        else
          require(error.has_value());
      } else {
        const auto reason = native<std::uint8_t>(body);
        require(reason <= 4);
        const bool observed = native<bool>(body);
        if (observed)
          require(reason == 0 && stage == 8 && branch == RawOperationBranch::returned &&
                  !has_error);
        if (observed) {
          const int boolean = native<int>(body);
          require(boolean == 0 || boolean == 1);
        }
        const bool value = native<bool>(body);
        if (value) {
          const int type = native<int>(body);
          const void *pointer = native<const void *>(body);
          // __gc can supply only the userdata. Preserve lua_type's actual
          // absent-argument result instead of requiring an invented nil value.
          if (type == LUA_TNONE)
            require(reason == 3 && pointer == nullptr);
          else
            require(type >= LUA_TNIL && type <= LUA_TTHREAD);
          if (type == LUA_TTABLE || type == LUA_TSTRING || type == LUA_TFUNCTION)
            require(pointer != nullptr);
        }
        if (native<bool>(body)) {
          const int inner = native<int>(body);
          require(inner == LUA_OK || inner == LUA_ERRRUN);
          const auto &call_link = returned(rows, parent, 4834);
          ByteView linked{call_link.body};
          (void)ref(linked);
          const auto call = ref(linked);
          ByteView actual{returned(rows, call, 1518).body};
          (void)ref(actual);
          require(native<int>(actual) == inner && actual.empty());
        }
        if (reason >= 1 && reason <= 3)
          require(branch == RawOperationBranch::unfinished && !observed && !has_error);
        if (reason == 4)
          require(has_error && error->code == ErrorCode::allocation);
      }
      require(body.empty());
    }
    if (row.site == 4803 || row.site == 4832) {
      const auto parent = ref(body);
      const auto value =
          entry(rows, find(rows, parent, row.site == 4803 ? 4800 : 4820));
      const auto mode = native<std::uint8_t>(body);
      const int base = native<int>(body);
      require(base == value.arguments && body.empty());
      if (row.site == 4832)
        require(mode == 3);
      else {
        ByteView end{returned(rows, parent, 4801).body};
        (void)ref(end);
        const auto branch = native<RawOperationBranch>(end);
        require(mode == (branch != RawOperationBranch::returned ? 1U
                         : value.operation == 10                ? 2U
                                                                : 0U));
      }
    }
    if (row.site == 4804 || row.site == 4833) {
      const auto parent = ref(body);
      const auto &request = find(rows, parent, row.site == 4804 ? 4803 : 4832);
      before(request, row);
      ByteView offered{request.body};
      (void)ref(offered);
      (void)native<std::uint8_t>(offered);
      const int base = native<int>(offered), status = native<int>(body),
                top = native<int>(body);
      require((status == LUA_OK || status == LUA_ERRMEM) && top == base + 1 &&
              body.empty());
    }
    if (row.site == 4824) {
      const auto token = ref(body), source = ref(body);
      const auto value = entry(rows, find(rows, token, 4820));
      ByteView acquired{find(rows, source, 1506).body};
      require(ref(acquired) == value.vm && native<std::uint8_t>(acquired) == 5);
      const auto size = native<std::size_t>(acquired);
      require(size == acquired.size());
      require(body.empty());
    }
    if (row.site == 4830) {
      const auto token = ref(body), source = ref(body);
      const auto value = entry(rows, find(rows, token, 4820));
      ByteView load{find(rows, source, 1506).body};
      require(ref(load) == value.vm);
      const void *function = native<const void *>(body),
                 *environment = native<const void *>(body);
      require(function && environment && native<int>(body) == 1 && body.empty());
      ByteView outcome{returned(rows, source, 1507).body};
      (void)ref(outcome);
      require(native<int>(outcome) == LUA_OK && native<bool>(outcome));
      require(native<const void *>(outcome) == function && outcome.empty());
    }
    if (row.site == 4834) {
      const auto token = ref(body), call = ref(body);
      const auto value = entry(rows, find(rows, token, 4820));
      const void *function = native<const void *>(body);
      require(function && body.empty());
      ByteView actual{find(rows, call, 1517).body};
      require(ref(actual) == value.vm && native<const void *>(actual) == function &&
              actual.empty());
      ByteView assignment{returned(rows, token, 4830).body};
      (void)ref(assignment);
      (void)ref(assignment);
      require(native<const void *>(assignment) == function);
    }
    if (row.site == 4831) {
      const auto parent = ref(body);
      before(find(rows, parent, 4830), row);
      require(native<bool>(body) && native<std::size_t>(body) == 5);
      require(
          std::ranges::equal(body, std::as_bytes(std::span{"_ENV", std::size_t{5}})));
    }
  }
  for (const auto &row : rows)
    if (row.site == 4800 || row.site == 4820)
      require(ends[row.own] == 1);
}
void ordinary() {
  Directory directory{"ordinary"};
  const auto effect = directory.path / "effect";
  std::string revision;
  const auto rows = scenario(directory.path, [&](CodingEngine &engine) {
    revision = stage(
        engine,
        {definition("fixture", "blackbird.call('write_file',{path=" +
                                   unwrap(dump_json(Value{effect.string()})) +
                                   ",content='one-native-module'});return {answer=42}"),
         definition("falsevalue", "return false"), definition("nilvalue", "return nil"),
         definition("cycle", "return blackbird.module('cycle')")});
    engine.turn(
        {"",
         R"lua(local first=blackbird.module('fixture');assert(first.answer==42 and first==blackbird.module('fixture'))
      assert(blackbird.module('falsevalue')==false and blackbird.module('falsevalue')==false)
      assert(not pcall(blackbird.module,'nilvalue'));assert(not pcall(blackbird.module,'cycle'));assert(not pcall(blackbird.module,'absent'))
      local text='embedded\0 snowman ☃';assert(blackbird.json.decode(blackbird.json.encode(text))==text)
      assert(#blackbird.array({1,2})==2);return true)lua"});
    require(read_file(effect) == "one-native-module");
  });
  validate(rows);
  unsigned hits = 0, misses = 0, loading = 0, cleared = 0, cache = 0, names = 0;
  std::map<Ref, std::string> actual_names;
  std::map<std::string, unsigned> name_counts;
  for (const auto &row : rows)
    if (row.site == 4821) {
      ByteView body{row.body};
      const auto parent = ref(body);
      const auto size = native<std::size_t>(body);
      require(size == body.size());
      const std::string name{reinterpret_cast<const char *>(body.data()), size};
      require(name == "fixture" || name == "falsevalue" || name == "nilvalue" ||
              name == "cycle" || name == "absent");
      actual_names.emplace(parent, name);
      ++name_counts[name];
    }
  for (const auto &row : rows) {
    ByteView body{row.body};
    if (row.site == 4826) {
      const auto parent = ref(body);
      const std::string expected =
          "@module:" + actual_names.at(parent) + ":" + revision;
      require(native<std::size_t>(body) == expected.size() &&
              native<std::size_t>(body) == expected.size() + 1);
      require(body.size() == expected.size() + 1 &&
              std::memcmp(body.data(), expected.c_str(), body.size()) == 0);
    }
    if (row.site == 4822) {
      (void)ref(body);
      const auto kind = native<std::uint8_t>(body);
      const bool returned = native<bool>(body);
      if (kind == 1) {
        hits += returned ? 1U : 0U;
        misses += returned ? 0U : 1U;
      }
    }
    if (row.site == 4823) {
      (void)ref(body);
      const auto kind = native<std::uint8_t>(body);
      const auto type = native<int>(body);
      (void)native<const void *>(body);
      require(body.empty());
      if (kind == 1) {
        require(type == LUA_TBOOLEAN);
        ++loading;
      }
      if (kind == 2) {
        require(type == LUA_TNIL);
        ++cleared;
      }
      if (kind == 3)
        ++cache;
    }
    if (row.site == 4821) {
      (void)ref(body);
      const auto size = native<std::size_t>(body);
      require(size == body.size());
      ++names;
    }
  }
  require(hits == 2 && misses == 6 && loading == 4 && cleared == 4 && cache == 2 &&
          names == 8);
  require(name_counts == std::map<std::string, unsigned>{{"fixture", 2},
                                                         {"falsevalue", 2},
                                                         {"nilvalue", 1},
                                                         {"cycle", 2},
                                                         {"absent", 1}});
}
void yielded() {
  Directory directory{"yield"};
  const auto one = directory.path / "one", two = directory.path / "two";
  const auto source = [&](const auto &path) {
    return "blackbird.call('write_file',{path=" +
           unwrap(dump_json(Value{path.string()})) +
           ",content='one'});return {value='actual'}";
  };
  const auto rows = scenario(directory.path, [&](CodingEngine &engine) {
    stage(engine, {definition("one", source(one)), definition("two", source(two))});
    engine.display = [&](std::string_view text) {
      if (text == "suspended")
        require(!std::filesystem::exists(one) && !std::filesystem::exists(two));
    };
    engine.turn(
        {"",
         R"lua(local original=blackbird.call;blackbird.call=function(name,q) if name=='module_source' then coroutine.yield(q.name) end;return original(name,q) end
      local a=coroutine.create(function() return blackbird.module('one') end);local b=coroutine.create(function() return blackbird.module('two') end)
      local ok,value=coroutine.resume(a);assert(ok and value=='one');ok,value=coroutine.resume(b);assert(ok and value=='two');blackbird.display('suspended')
      ok,value=coroutine.resume(b);assert(ok and value.value=='actual');local second=value;ok,value=coroutine.resume(a);assert(ok and value.value=='actual' and value~=second)
      assert(coroutine.status(a)=='dead' and coroutine.status(b)=='dead')
      blackbird.call=original;assert(blackbird.module('one')==value and blackbird.module('two')==second);return true)lua"});
    require(read_file(one) == "one" && read_file(two) == "one");
  });
  validate(rows);
  unsigned entries = 0, success = 0, calls = 0;
  for (const auto &row : rows) {
    entries += row.site == 4820 ? 1U : 0U;
    calls += row.site == 4834 ? 1U : 0U;
    if (row.site == 4825) {
      ByteView body{row.body};
      (void)ref(body);
      require(native<RawOperationBranch>(body) == RawOperationBranch::returned);
      (void)native<std::uint8_t>(body);
      require(!native<bool>(body));
      require(native<std::uint8_t>(body) == 0 && native<bool>(body) &&
              native<int>(body) == 1);
      ++success;
    }
  }
  require(entries == 4 && success == 4 && calls == 2);
}
void abandoned() {
  Directory directory{"abandoned"};
  const auto rows = scenario(directory.path, [&](CodingEngine &engine) {
    stage(engine, {definition("fixture", "return 42")});
    engine.turn(
        {"",
         R"lua(local original=blackbird.call;blackbird.call=function(n,q) if n=='module_source' then coroutine.yield('parked') end;return original(n,q) end
      local closed=coroutine.create(function() return blackbird.module('fixture') end);assert(coroutine.resume(closed));assert(coroutine.close(closed))
      local dead=coroutine.create(function() return blackbird.module('fixture') end);assert(coroutine.resume(dead));dead=nil;collectgarbage();collectgarbage()
      retained=coroutine.create(function() return blackbird.module('fixture') end);assert(coroutine.resume(retained));return true)lua"});
  });
  validate(rows);
  unsigned scope = 0, gc = 0, vm = 0, executed = 0;
  for (const auto &row : rows) {
    executed += row.site == 4834 ? 1U : 0U;
    if (row.site == 4825) {
      ByteView body{row.body};
      (void)ref(body);
      require(native<RawOperationBranch>(body) == RawOperationBranch::unfinished);
      (void)native<std::uint8_t>(body);
      require(!native<bool>(body));
      const auto reason = native<std::uint8_t>(body);
      scope += reason == 3 ? 1U : 0U;
      gc += reason == 1 ? 1U : 0U;
      vm += reason == 2 ? 1U : 0U;
      require(!native<bool>(body));
    }
  }
  require(scope == 1 && gc == 1 && vm == 1 && executed == 0);
}
void exceptions() {
  for (unsigned mode = 0; mode < 6; ++mode) {
    Directory directory{"exception-" + std::to_string(mode)};
    const auto marker = directory.path / "effect";
    const auto rows = scenario(directory.path, [&](CodingEngine &engine) {
      unsigned after_foreign = 0;
      engine.display = [&](std::string_view text) {
        if (text == "after-foreign")
          ++after_foreign;
        if (text != "native")
          return;
        switch (mode) {
        case 0:
          throw Error{ErrorCode::conflict, 77};
        case 1:
          throw std::bad_alloc{};
        case 2:
          throw std::length_error{"actual length"};
        case 3:
          throw std::runtime_error{"actual standard"};
        default:
          throw 19;
        }
      };
      bool actual_foreign = false;
      try {
        const std::string tail =
            mode == 4   ? "pcall(function()blackbird.display('native')end);"
                          "blackbird.display('after-foreign');return true"
            : mode == 5 ? "pcall(function()blackbird.call('lua',{code=\"blackbird."
                          "display('native')\"})end);"
                          "blackbird.display('after-foreign');return true"
                        : "blackbird.display('native');return true";
        engine.turn({"", "blackbird.call('write_file',{path=" +
                             unwrap(dump_json(Value{marker.string()})) +
                             ",content='one'});" + tail});
        require(false);
      } catch (const Error &error) {
        require(mode < 4);
        require(error.code == (mode == 0   ? ErrorCode::conflict
                               : mode == 1 ? ErrorCode::allocation
                                           : ErrorCode::external_unknown));
        if (mode == 0)
          require(error.detail == 77);
      } catch (int value) {
        require(mode >= 4 && value == 19);
        actual_foreign = true;
      }
      require(actual_foreign == (mode >= 4) && after_foreign == 0);
      require(read_file(marker) == "one");
    });
    validate(rows);
    unsigned observed = 0;
    for (const auto &row : rows)
      if (row.site == 4801) {
        ByteView body{row.body};
        (void)ref(body);
        const auto branch = native<RawOperationBranch>(body);
        if (branch == (mode == 0   ? RawOperationBranch::error
                       : mode == 1 ? RawOperationBranch::allocation
                       : mode == 2 ? RawOperationBranch::length
                       : mode == 3 ? RawOperationBranch::standard
                                   : RawOperationBranch::nonstandard)) {
          require(native<std::uint8_t>(body) == 5 && native<bool>(body));
          require(native<ErrorCode>(body) == (mode == 0 ? ErrorCode::conflict
                                              : mode == 1
                                                  ? ErrorCode::allocation
                                                  : ErrorCode::external_unknown));
          require(native<std::int64_t>(body) == (mode == 0 ? 77 : 0));
          require(!native<bool>(body) && body.empty());
          ++observed;
        }
      }
    require(observed == (mode == 5 ? 2U : 1U));
  }
}
void reentry() {
  for (unsigned mode = 0; mode < 3; ++mode) {
    Directory directory{"reentry-" + std::to_string(mode)};
    unsigned nested = 0;
    const std::string outer(8192, 'z'), primary_text(511, 'p');
    const auto rows = scenario(directory.path, [&](CodingEngine &engine) {
      engine.display = [&](std::string_view text) {
        if (text == "arm") {
          cut::target = mode == 2 ? std::string_view{primary_text}
                        : mode    ? std::string_view{"conflict"}
                                  : std::string_view{outer};
          cut::gc = true;
          cut::deny = mode == 2;
        }
        if (text == "nested") {
          ++nested;
          throw Error{ErrorCode::invalid_range, 32};
        }
        if (text == "primary") {
          if (mode == 2)
            throw std::runtime_error{primary_text};
          throw Error{ErrorCode::conflict, 77};
        }
      };
      cut::reached = cut::denied = 0;
      std::string code =
          R"lua(collectgarbage('stop');local trash=setmetatable({}, {__gc=function() assert(blackbird.json.decode('{"inner":7}').inner==7);pcall(blackbird.display,'nested') end});trash=nil;blackbird.display('arm');)lua";
      if (mode)
        code += "blackbird.display('primary');return true";
      else
        code += "local value=blackbird.json.decode(" +
                unwrap(dump_json(Value{unwrap(dump_json(Value{outer}))})) +
                ");assert(value==string.rep('z',8192));return true";
      try {
        engine.turn({"", code});
        require(mode == 0);
      } catch (const Error &error) {
        require(mode != 0 && error == (mode == 2 ? Error{ErrorCode::external_unknown}
                                                 : Error{ErrorCode::conflict, 77}));
      }
      require(nested == 1 && cut::reached == 1);
      if (mode == 2)
        require(cut::denied > 0);
    });
    validate(rows);
    bool original = false;
    for (const auto &row : rows)
      if (row.site == 4802) {
        ByteView body{row.body};
        (void)ref(body);
        const auto role = native<std::uint8_t>(body);
        const auto size = native<std::size_t>(body);
        if (role == 5 &&
            std::string_view{reinterpret_cast<const char *>(body.data()), size} ==
                (mode == 2 ? std::string_view{primary_text}
                           : std::string_view{"conflict"}))
          original = true;
      }
    require(mode == 0 || original);
  }
}
void setup_refusal() {
  Directory directory{"setup-refusal"};
  const auto effect = directory.path / "forbidden";
  const auto rows = scenario(directory.path, [&](CodingEngine &engine) {
    stage(engine, {definition("fixture", "blackbird.call('write_file',{path=" +
                                             unwrap(dump_json(Value{effect.string()})) +
                                             ",content='forbidden'});return 42")});
    engine.display = [](std::string_view text) {
      if (text == "arm-setup")
        cut::deny_module = true;
    };
    cut::reached = cut::denied = 0;
    try {
      engine.turn(
          {"",
           "blackbird.display('arm-setup');blackbird.module('fixture');return true"});
      require(false);
    } catch (const Error &error) {
      require(error.code == ErrorCode::allocation);
    }
    require(!std::filesystem::exists(effect) && cut::reached == 1 && cut::denied > 0);
  });
  validate(rows);
  unsigned allocations = 0, calls = 0;
  for (const auto &row : rows) {
    calls += row.site == 4834 ? 1U : 0U;
    if (row.site == 4833) {
      ByteView body{row.body};
      (void)ref(body);
      require(native<int>(body) == LUA_ERRMEM);
      ++allocations;
    }
  }
  require(allocations == 1 && calls == 0);
}
void holder_refusal() {
  Directory directory{"holder-refusal"};
  const auto effect = directory.path / "forbidden";
  const auto rows = scenario(directory.path, [&](CodingEngine &engine) {
    stage(engine, {definition("fixture", "blackbird.call('write_file',{path=" +
                                             unwrap(dump_json(Value{effect.string()})) +
                                             ",content='forbidden'});return 42")});
    engine.display = [](std::string_view text) {
      if (text == "arm-holder")
        cut::deny_holder = true;
    };
    cut::holder_refusals = 0;
    try {
      engine.turn(
          {"",
           "blackbird.display('arm-holder');blackbird.module('fixture');return true"});
      require(false);
    } catch (const Error &error) {
      require(error.code == ErrorCode::allocation);
    }
    require(cut::holder_refusals == 1 && !std::filesystem::exists(effect));
  });
  unsigned entries = 0, names = 0, ends = 0, setup = 0, loads = 0;
  for (const auto &row : rows) {
    entries += row.site == 4820 ? 1U : 0U;
    setup += row.site == 4832 ? 1U : 0U;
    loads += row.site == 4824 || row.site == 4834 ? 1U : 0U;
    ByteView body{row.body};
    if (row.site == 4821) {
      (void)find(rows, ref(body), 4820);
      require(native<std::size_t>(body) == 7 && body.size() == 7 &&
              std::memcmp(body.data(), "fixture", 7) == 0);
      ++names;
    }
    if (row.site == 4825) {
      (void)find(rows, ref(body), 4820);
      require(native<std::uint8_t>(body) == 4); // actual C++ allocation branch
      require(native<std::uint8_t>(body) == 0 && native<bool>(body));
      require(native<ErrorCode>(body) == ErrorCode::allocation &&
              native<std::int64_t>(body) == 0);
      require(native<std::uint8_t>(body) == 4);
      require(!native<bool>(body) && !native<bool>(body) && !native<bool>(body));
      require(body.empty());
      ++ends;
    }
  }
  require(entries == 1 && names == 1 && ends == 1 && setup == 0 && loads == 0);
}
void cold_credit() {
  Directory directory{"cold-credit"};
  const auto environment = id<EnvironmentId>(91);
  std::unique_ptr<RawModuleToken> token;
  std::shared_ptr<RawModuleIssuer> issuer;
  std::vector<RawControlToken> pressure;
  {
    auto runtime = unwrap(RawRuntime::open(directory.path.string(), environment,
                                           {{16 * 1024 * 1024, 32768, 4096}, 4096}));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    const auto vm = RawEventRef{raw_lane_epoch(), unwrap(raw_emit(9200, {}))};
    const auto generation = id<EnvironmentId>(93).bytes();
    const int argc = 2, type = LUA_TSTRING;
    const bool absent = false;
    issuer = RawModuleIssuer::prepare();
    const std::array fields{ByteView{vm.lane},
                            std::as_bytes(std::span{&vm.ordinal, std::size_t{1}}),
                            ByteView{generation},
                            std::as_bytes(std::span{&argc, std::size_t{1}}),
                            std::as_bytes(std::span{&type, std::size_t{1}}),
                            std::as_bytes(std::span{&absent, std::size_t{1}}),
                            std::as_bytes(std::span{&absent, std::size_t{1}})};
    token = std::make_unique<RawModuleToken>(issuer, fields, "cold-exact-name");
    {
      const std::lock_guard lock{issuer->mutex};
      RawThreadBinding original{issuer->handle};
      for (std::uint64_t size = 32768; size; size /= 2) {
        auto held = raw_try_claim_control(size);
        if (held.has_value())
          pressure.push_back(std::move(held).value());
      }
    }
    // Also retain the actual runtime service past this creating object's exit.
  }
  const auto creating = token->reference();
  std::jthread final_drop{[owned = std::move(token)]() mutable { owned.reset(); }};
  final_drop.join();
  pressure.clear();
  issuer.reset();
  const auto rows = records(directory.path);
  unsigned ends = 0;
  for (const auto &row : rows)
    if (row.site == 4825) {
      ByteView body{row.body};
      const auto parent = ref(body);
      require(parent == Ref{creating.lane, creating.ordinal});
      require(row.own.first == creating.lane &&
              native<RawOperationBranch>(body) == RawOperationBranch::unfinished);
      ++ends;
    }
  require(ends == 1);
}

void chunk_credit() {
  constexpr std::uint64_t data_capacity = 65536;
  const std::string original{"native-chunk"}, native_name = original + '\0';
  const auto required = std::uint64_t{57} + original.size();
  for (const bool short_grant : {false, true}) {
    Directory directory{short_grant ? "chunk-short" : "chunk-exact"};
    RawEventRef request{};
    {
      auto runtime =
          unwrap(RawRuntime::open(directory.path.string(), id<EnvironmentId>(94),
                                  {{data_capacity, 32768, 4096}, 4096}));
      auto handle = unwrap(runtime->prepare_thread());
      RawThreadBinding binding{handle};
      const auto vm = RawEventRef{raw_lane_epoch(), unwrap(raw_emit(9200, {}))};
      const auto generation = id<EnvironmentId>(95).bytes();
      const int argc = 2, type = LUA_TSTRING;
      const bool absent = false;
      auto issuer = RawModuleIssuer::prepare();
      const std::array fields{ByteView{vm.lane},
                              std::as_bytes(std::span{&vm.ordinal, std::size_t{1}}),
                              ByteView{generation},
                              std::as_bytes(std::span{&argc, std::size_t{1}}),
                              std::as_bytes(std::span{&type, std::size_t{1}}),
                              std::as_bytes(std::span{&absent, std::size_t{1}}),
                              std::as_bytes(std::span{&absent, std::size_t{1}})};
      RawModuleToken token{issuer, fields, {}};
      request = token.reference();
      // Wait outside the issuer mutex. This lane has no published DATA yet,
      // then one held claim leaves precisely the declared native body grant.
      RawThreadBinding original_issuer{issuer->handle};
      unwrap(raw_wait_data(data_capacity));
      auto pressure = unwrap(
          raw_try_claim_data(data_capacity - required + (short_grant ? 1U : 0U)));
      unsigned after = 0;
      try {
        token.chunk(original, native_name);
        ++after;
        token.finish(0, RawOperationBranch::returned);
      } catch (const Error &error) {
        require(short_grant && error.code == ErrorCode::capacity);
        token.finish(4, RawOperationBranch::error, &error);
      }
      require(after == (short_grant ? 0U : 1U));
    }
    const auto rows = records(directory.path);
    unsigned chunks = 0, ends = 0;
    for (const auto &row : rows) {
      if (row.site == 4826) {
        ByteView body{row.body};
        require(ref(body) == Ref{request.lane, request.ordinal});
        require(native<std::size_t>(body) == original.size());
        require(native<std::size_t>(body) == native_name.size());
        require(body.size() == native_name.size() &&
                std::memcmp(body.data(), native_name.data(), body.size()) == 0);
        ++chunks;
      }
      if (row.site == 4825) {
        ByteView body{row.body};
        require(ref(body) == Ref{request.lane, request.ordinal});
        require(native<std::uint8_t>(body) == (short_grant ? 3U : 1U));
        require(native<std::uint8_t>(body) == (short_grant ? 3U : 8U));
        require(native<bool>(body) == short_grant);
        if (short_grant) {
          require(native<ErrorCode>(body) == ErrorCode::capacity);
          require(native<std::int64_t>(body) == 0);
        }
        require(native<std::uint8_t>(body) == (short_grant ? 4U : 0U));
        require(!native<bool>(body) && !native<bool>(body) && !native<bool>(body));
        require(body.empty());
        ++ends;
      }
    }
    require(chunks == (short_grant ? 0U : 1U) && ends == 1);
  }
}

} // namespace
int main(int argc, char **argv) {
  try {
#if defined(__linux__)
    cut::original = __real_lua_pushlstring;
    cut::userdata = __real_lua_newuserdatauv;
    cut::protected_call = __real_lua_pcallk;
#else
    const auto symbol = ::dlsym(RTLD_NEXT, "lua_pushlstring");
    static_assert(sizeof(symbol) == sizeof(cut::original));
    std::memcpy(&cut::original, &symbol, sizeof(symbol));
    require(cut::original != nullptr);
    const auto userdata = ::dlsym(RTLD_NEXT, "lua_newuserdatauv"),
               protected_call = ::dlsym(RTLD_NEXT, "lua_pcallk");
    static_assert(sizeof(userdata) == sizeof(cut::userdata) &&
                  sizeof(protected_call) == sizeof(cut::protected_call));
    std::memcpy(&cut::userdata, &userdata, sizeof(userdata));
    std::memcpy(&cut::protected_call, &protected_call, sizeof(protected_call));
    require(cut::userdata && cut::protected_call);
#endif
    if (argc == 2 && std::string_view{argv[1]} == "--holder-only") {
      holder_refusal();
      return 0;
    }
    ordinary();
    yielded();
    abandoned();
    exceptions();
    reentry();
    setup_refusal();
    holder_refusal();
    cold_credit();
    chunk_credit();
    std::cout << "bridge module native raw passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
