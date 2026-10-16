#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/raw_program.hpp"
#include "blackbird/raw_reader.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <iostream>
#include <map>
#include <source_location>
#include <thread>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#include <unistd.h>

using namespace blackbird;
namespace cuts {
struct Allocator {
  lua_Alloc original = nullptr;
  void *userdata = nullptr;
  lua_State *state = nullptr;
};
thread_local Allocator current;
thread_local bool deny_prepare = false, deny_restore = false, restore_ready = false;
thread_local unsigned failures = 0;
thread_local unsigned cpp_mode = 0;
thread_local bool cpp_busy = false, cpp_hit = false;
thread_local CodingEngine *cpp_engine = nullptr;
bool (*pending_ready)(CodingEngine *) noexcept = nullptr;
bool allocation_cut() noexcept {
  if (!cpp_mode || cpp_busy)
    return false;
  cpp_busy = true;
  std::array<void *, 48> frames{};
  const int count = ::backtrace(frames.data(), static_cast<int>(frames.size()));
  bool wrapper = false, program = false, string = false;
  for (int i = 0; i < count; ++i) {
    Dl_info information{};
    if (::dladdr(frames[static_cast<std::size_t>(i)], &information) &&
        information.dli_sname) {
      wrapper |= std::strstr(information.dli_sname, "invoke_tool") != nullptr;
      program |= std::strstr(information.dli_sname, "program_config") != nullptr;
      string |= std::strstr(information.dli_sname, "basic_string") != nullptr;
    }
  }
  const bool selected = cpp_mode == 1
                            ? wrapper && string
                            : program && pending_ready && pending_ready(cpp_engine);
  if (selected) {
    cpp_mode = 0;
    cpp_hit = true;
  }
  cpp_busy = false;
  return selected;
}
void *denying(void *, void *pointer, std::size_t before, std::size_t after) {
  if (after > before) {
    ++failures;
    return nullptr;
  }
  return current.original(current.userdata, pointer, before, after);
}
void install(lua_State *state) {
  current.state = state;
  current.original = lua_getallocf(state, &current.userdata);
  lua_setallocf(state, denying, nullptr);
}
void restore(lua_State *state) {
  if (current.state == state) {
    lua_setallocf(state, current.original, current.userdata);
    current = {};
  }
}
template <class F> F next(const char *name) {
  const auto symbol = ::dlsym(RTLD_NEXT, name);
  if (!symbol)
    std::abort();
  F function{};
  static_assert(sizeof(function) == sizeof(symbol));
  std::memcpy(&function, &symbol, sizeof(function));
  return function;
}
} // namespace cuts
void *operator new(std::size_t size) {
  if (cuts::allocation_cut())
    throw std::bad_alloc{};
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
#if defined(__linux__)
extern "C" int __real_luaL_ref(lua_State *, int);
extern "C" void __real_lua_setglobal(lua_State *, const char *);
extern "C" int __real_lua_pcallk(lua_State *, int, int, int, lua_KContext,
                                 lua_KFunction);
#define TEST_REF __wrap_luaL_ref
#define TEST_SETGLOBAL __wrap_lua_setglobal
#define TEST_PCALL __wrap_lua_pcallk
#else
#define TEST_REF luaL_ref
#define TEST_SETGLOBAL lua_setglobal
#define TEST_PCALL lua_pcallk
#endif
extern "C" int TEST_REF(lua_State *state, int registry) {
#if defined(__linux__)
  const int result = __real_luaL_ref(state, registry);
#else
  static const auto real = cuts::next<int (*)(lua_State *, int)>("luaL_ref");
  const int result = real(state, registry);
#endif
  if (cuts::deny_prepare) {
    cuts::deny_prepare = false;
    cuts::install(state);
  }
  return result;
}
extern "C" void TEST_SETGLOBAL(lua_State *state, const char *name) {
  if (cuts::deny_restore && cuts::restore_ready &&
      std::strcmp(name, "_native_args") == 0) {
    cuts::deny_restore = false;
    cuts::restore_ready = false;
    cuts::install(state);
  }
#if defined(__linux__)
  __real_lua_setglobal(state, name);
#else
  static const auto real =
      cuts::next<void (*)(lua_State *, const char *)>("lua_setglobal");
  real(state, name);
#endif
}
extern "C" int TEST_PCALL(lua_State *state, int arguments, int results, int handler,
                          lua_KContext context, lua_KFunction continuation) {
#if defined(__linux__)
  const int result =
      __real_lua_pcallk(state, arguments, results, handler, context, continuation);
#else
  static const auto real =
      cuts::next<int (*)(lua_State *, int, int, int, lua_KContext, lua_KFunction)>(
          "lua_pcallk");
  const int result = real(state, arguments, results, handler, context, continuation);
#endif
  // Restore only after the real protected Lua call returns its real status.
  // No fabricated LUA_ERRMEM and no C++ owner lies across a Lua longjmp.
  cuts::restore(state);
  return result;
}
#undef TEST_REF
#undef TEST_SETGLOBAL
#undef TEST_PCALL
namespace {
void require(bool value, std::source_location at = std::source_location::current()) {
  if (!value)
    throw std::runtime_error{"program raw oracle line " + std::to_string(at.line())};
}
template <class T> T identity(unsigned char value) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{value};
  return unwrap(T::from_bytes(bytes));
}
template <class T> T native(ByteView &bytes) {
  require(bytes.size() >= sizeof(T));
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(value));
  bytes = bytes.subspan(sizeof(value));
  return value;
}
class Provider final : public CodingProvider {
public:
  unsigned calls = 0;
  bool queued = false, empty = false;
  std::string effect;
  Value respond(const Value &,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    if (!queued && !empty && effect.empty())
      throw std::runtime_error{"unexpected provider"};
    std::string acquired;
    if (!effect.empty() && calls == 1) {
      require(effect.find('\'') == std::string::npos);
      const auto arguments = unwrap(dump_json(Value::object(
          {{"argv", Value{Value::Array{Value{"/bin/sh"}, Value{"-c"},
                                       Value{"printf one >> '" + effect + "'"}}}}})));
      acquired = "{\"output\":[{\"type\":\"function_call\",\"call_id\":\"fixture-"
                 "default-effect\",\"name\":\"exec\",\"arguments\":" +
                 unwrap(dump_json(Value{arguments})) + "}]}";
    }
    const std::string_view body =
        !acquired.empty() ? std::string_view{acquired}
        : queued && calls == 1
            ? R"({"output":[{"type":"function_call","call_id":"fixture-selection","name":"workflow_invoke","arguments":"{\"name\":\"nativefixture\"}"}]})"
            : R"({"output":[]})";
    capture(body);
    return unwrap(parse_json(body));
  }
};
Value config(std::string source) {
  return unwrap(parse_json(
      "{\"modules\":[],\"model\":\"\",\"effort\":\"\",\"workflow_prefix\":\"\","
      "\"workflows\":[{\"name\":\"nativefixture\",\"description\":\"Fixture\","
      "\"aliases\":[\"nativefixture\"],\"bare\":true,\"powerwords\":[],\"source\":" +
      unwrap(dump_json(Value{std::move(source)})) + "}]}"));
}
std::string literal(const Value &value) {
  return "blackbird.json.decode([==[" + unwrap(dump_json(value)) + "]==])";
}
struct Record {
  std::uint32_t site;
  RawEventRef ref;
  std::vector<std::byte> body;
};
RawEventRef reference(ByteView &bytes) {
  return {native<IdentityBytes>(bytes), native<std::uint64_t>(bytes)};
}
bool same(RawEventRef left, RawEventRef right) {
  return left.lane == right.lane && left.ordinal == right.ordinal;
}
std::vector<Record> records(const std::filesystem::path &directory) {
  const auto runs = unwrap(discover_raw_runs(directory.string()));
  require(runs.size() == 1 && !runs[0].error);
  const auto &snapshot = runs[0].snapshot;
  if (!snapshot)
    throw std::runtime_error{"Missing preserved raw run snapshot"};
  require(snapshot->closed);
  auto reader = unwrap(RawRunReader::open(directory.string() + "/raw/" + runs[0].name));
  std::vector<std::byte> scratch(16 * 1024 * 1024);
  std::vector<Record> output;
  while (const auto row = unwrap(reader->read(scratch))) {
    ByteView bytes{row->raw};
    require(native<std::uint32_t>(bytes) == row->raw.size());
    const auto site = native<std::uint32_t>(bytes);
    const auto ordinal = native<std::uint64_t>(bytes);
    require(site != 8001);
    output.push_back({site, {row->origin.lane, ordinal}, {bytes.begin(), bytes.end()}});
  }
  return output;
}
const Record &find(const std::vector<Record> &all, RawEventRef ref,
                   std::uint32_t site) {
  const auto found = std::find_if(all.begin(), all.end(), [&](const Record &row) {
    return row.site == site && same(row.ref, ref);
  });
  require(found != all.end());
  return *found;
}
std::string text(ByteView &bytes) {
  const auto length = native<std::size_t>(bytes);
  require(bytes.size() >= length);
  std::string value{reinterpret_cast<const char *>(bytes.data()), length};
  bytes = bytes.subspan(length);
  return value;
}
// BBM2 fixture bytes specified directly: only this known object/array/string
// shape, not a generic Value walker or the production packet/raw encoder.
void count(std::string &bytes, std::size_t value) {
  do {
    const auto low = static_cast<unsigned char>(value & 127);
    value >>= 7;
    bytes.push_back(static_cast<char>(low | (value ? 128 : 0)));
  } while (value);
}
void atom(std::string &bytes, std::string_view value) {
  if (value == "source") {
    bytes.append("\10\13", 2);
  } else {
    bytes.push_back('\6');
    count(bytes, value.size());
    bytes.append(value);
  }
}
std::string definitions_packet(std::string_view source) {
  std::string body{"BBM\2\11\1\12\6", 8};
  atom(body, "aliases");
  body.append("\11\1", 2);
  atom(body, "nativefixture");
  atom(body, "bare");
  body.push_back('\2');
  atom(body, "description");
  atom(body, "Fixture");
  atom(body, "name");
  atom(body, "nativefixture");
  atom(body, "powerwords");
  body.append("\11\0", 2);
  atom(body, "source");
  atom(body, source);
  return body;
}
std::string config_packet(std::string_view source) {
  std::string body{"BBM\2\12\5", 6};
  atom(body, "effort");
  atom(body, "");
  atom(body, "model");
  atom(body, "");
  atom(body, "modules");
  body.append("\11\0", 2);
  atom(body, "workflow_prefix");
  atom(body, "");
  atom(body, "workflows");
  body += definitions_packet(source).substr(4);
  return body;
}
struct ProgramAccess {
  using Type = Value (CodingEngine::*)(const Value &, DomainAdmission *);
  friend Type member(ProgramAccess);
};
template <class Tag, typename Tag::Type Method> struct Access {
  friend typename Tag::Type member(Tag) { return Method; }
};
template struct Access<ProgramAccess, &CodingEngine::program_config>;
struct IssuerAccess {
  using Type = std::shared_ptr<RawProgramIssuer> CodingEngine::*;
  friend Type member(IssuerAccess);
};
template struct Access<IssuerAccess, &CodingEngine::program_issuer_>;
struct PendingAccess {
  using Type = RawProgramLifetime CodingEngine::*;
  friend Type member(PendingAccess);
};
template struct Access<PendingAccess, &CodingEngine::pending_program_trace_>;
struct RevisionAccess {
  using Type = std::string CodingEngine::*;
  friend Type member(RevisionAccess);
};
template struct Access<RevisionAccess, &CodingEngine::pending_program_revision_>;
struct CreditAccess {
  using Type = std::optional<RawControlToken> RawProgramLifetime::*;
  friend Type member(CreditAccess);
};
template struct Access<CreditAccess, &RawProgramLifetime::credit_>;
bool pending_ready(CodingEngine *engine) noexcept {
  return engine && !(engine->*member(RevisionAccess{})).empty() &&
         ((engine->*member(PendingAccess{})).*member(CreditAccess{})).has_value();
}

template <class F>
std::vector<Record> scenario(const std::filesystem::path &directory, F &&body) {
  std::filesystem::create_directories(directory);
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
  const auto environment = identity<EnvironmentId>(91);
  const JournalHeader header{environment,
                             identity<AuditStreamId>(92),
                             3,
                             {1024 * 1024, 4 * 1024 * 1024},
                             std::nullopt};
  {
    auto root = unwrap(
        RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                  NativeJournalDirectory::open(directory.string()))),
                              "audit", header, {256 * 1024 * 1024, 30000}));
    AuditLog log{*root};
    ContextStore context{log};
    Provider provider;
    CodingEngine engine{log, context, provider, "test"};
    auto runtime = unwrap(RawRuntime::open(
        directory.string(), environment,
        {{256 * 1024 * 1024, 2 * 1024 * 1024, 128 * 1024}, 128 * 1024}));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    if constexpr (std::is_invocable_v<F, CodingEngine &, RawRuntime &, Provider &>)
      std::forward<F>(body)(engine, *runtime, provider);
    else
      std::forward<F>(body)(engine, *runtime);
  }
  return records(directory);
}
void lifecycle(const std::filesystem::path &base) {
  const auto all =
      scenario(base / "lifecycle", [&](CodingEngine &engine, RawRuntime &runtime) {
        const auto initial = engine.workflows()->revision();
        const auto proposal = Value::object({{"proposal", config("return true")}});
        try {
          engine.turn({"", "blackbird.call('program_config'," + literal(proposal) +
                               ");blackbird.call('program_config'," +
                               literal(proposal) + ");error('after two real stages')"});
          require(false);
        } catch (const Error &error) {
          require(error.code == ErrorCode::external_unknown);
        }
        require(engine.workflows()->revision() == initial);
        engine.turn({"", "return true"});
        auto worker_handle = unwrap(runtime.prepare_thread());
        std::exception_ptr failed;
        std::jthread worker{[&, handle = std::move(worker_handle)] {
          try {
            RawThreadBinding worker_binding{handle};
            const auto staged = (engine.*member(ProgramAccess{}))(proposal, nullptr);
            require(field(staged, "staged") == Value{true});
          } catch (...) {
            failed = std::current_exception();
          }
        }};
        worker.join();
        if (failed)
          std::rethrow_exception(failed);
        (void)unwrap(runtime.status());
        const auto issuer = engine.*member(IssuerAccess{});
        require(issuer != nullptr);
        std::vector<RawControlToken> pressure;
        pressure.reserve(32);
        {
          const std::lock_guard lock{issuer->mutex};
          RawThreadBinding issuer_binding{issuer->handle};
          for (std::uint64_t bytes = 2 * 1024 * 1024; bytes; bytes /= 2) {
            auto grant = raw_try_claim_control(bytes);
            if (grant.has_value())
              pressure.push_back(std::move(grant).value());
          }
        }
        bool refused = false;
        {
          RawCaptureFailureScope capture_failure{true};
          try {
            RawProgramLifetime extra{issuer, {}, false};
          } catch (const Error &error) {
            require(error.code == ErrorCode::capacity);
            refused = true;
          }
          require(capture_failure.error() == std::optional{Error{ErrorCode::capacity}});
        }
        require(refused);
        engine.turn({"", "return true"});
        {
          const std::lock_guard lock{issuer->mutex};
          RawThreadBinding issuer_binding{issuer->handle};
          pressure.clear();
        }
        require(engine.workflows()->revision() == initial);
      });
  unsigned begins = 0, superseded = 0, discarded = 0;
  for (const auto &row : all) {
    if (row.site == 3830)
      ++begins;
    if (row.site == 3832) {
      ByteView bytes{row.body};
      const auto parent = reference(bytes);
      require(same(find(all, parent, 3830).ref, parent) && parent.lane == row.ref.lane);
      const auto reason = native<std::uint8_t>(bytes);
      require(!native<bool>(bytes) && !native<bool>(bytes) && bytes.empty());
      superseded += reason == 1 ? 1U : 0U;
      discarded += reason == 2 ? 1U : 0U;
    }
  }
  require(begins == 3 && superseded == 1 && discarded == 2);
}
void guard_oracles(const std::filesystem::path &base) {
  for (unsigned mode = 0; mode < 2; ++mode) {
    const auto all = scenario(base / (mode ? "depth" : "count"), [&](CodingEngine
                                                                         &engine,
                                                                     RawRuntime &) {
      const auto source =
          mode ? "return blackbird.call('workflow_invoke',{name='nativefixture'})"
               : "return true";
      const auto proposal = Value::object({{"proposal", config(source)}});
      require(field(engine.operator_call("program_config", proposal), "staged") ==
              Value{true});
      const std::string program =
          mode ? "blackbird.call('workflow_invoke',{name='nativefixture'});return true"
               : "for i=1,64 do "
                 "blackbird.call('workflow_invoke',{name='nativefixture'}) end;"
                 "pcall(function()blackbird.call('workflow_invoke',{name='"
                 "nativefixture'})end);"
                 "blackbird.call('workflow_registry',{});return true";
      try {
        engine.turn({"", program});
        require(false);
      } catch (const Error &error) {
        require(error.code == ErrorCode::capacity);
      }
    });
    bool reached = false;
    unsigned last_depth = 0, last_invocations = 0, guards = 0;
    for (const auto &row : all)
      if (row.site == 3824) {
        ByteView bytes{row.body};
        (void)reference(bytes);
        const auto depth = native<unsigned>(bytes),
                   invocations = native<unsigned>(bytes);
        last_depth = depth;
        last_invocations = invocations;
        ++guards;
        require(bytes.empty());
        // Each nested workflow uses selection + execution operations. The real
        // public path reaches depth7/count8, then the existing operation-depth16
        // guard refuses its ninth selection before execute_workflow is entered.
        if (mode)
          reached |= depth == 7 && invocations == 8;
        else
          reached |= depth == 0 && invocations == 65;
      }
    if (!reached)
      std::cerr << "guard mode=" << mode << " records=" << guards
                << " last=" << last_depth << '/' << last_invocations << '\n';
    require(reached);
  }
}
void parked_oracles(const std::filesystem::path &base) {
  for (unsigned mode = 0; mode < 2; ++mode) {
    const auto directory = base / (mode ? "parked-broken" : "parked");
    const auto effect = directory / "effect";
    const auto all = scenario(
        directory, [&](CodingEngine &engine, RawRuntime &, Provider &provider) {
          const auto source = "blackbird.call('write_file',{path=" +
                              unwrap(dump_json(Value{effect.string()})) +
                              ",content='exactly-one-parked-effect'});return true";
          const auto proposal = Value::object({{"proposal", config(source)}});
          require(field(engine.operator_call("program_config", proposal), "staged") ==
                  Value{true});
          provider.queued = true;
          std::string program =
              "local r=blackbird.request();local "
              "selected=blackbird.call('workflow_invoke',{name='nativefixture'});"
              "assert(selected.status=='accepted');";
          if (!mode)
            program += "blackbird.append({{type='function_call_output',call_id='"
                       "fixture-selection',output=blackbird.json.encode(selected)}});";
          program += "return true";
          bool failed = false;
          try {
            engine.turn({"", program});
          } catch (const Error &error) {
            require(mode && error.code == ErrorCode::conflict);
            failed = true;
          }
          require(failed == (mode != 0) && provider.calls == 1);
          require(std::filesystem::exists(effect) == (mode == 0));
          if (!mode)
            require(read_file(effect) == "exactly-one-parked-effect");
        });
    unsigned requested = 0, installed = 0, ended = 0, executed = 0;
    for (const auto &row : all) {
      executed += row.site == 3820 ? 1U : 0U;
      ByteView bytes{row.body};
      if (row.site == 3833) {
        (void)find(all, reference(bytes), 3810);
        require(native<std::size_t>(bytes) == 0 && bytes.empty());
        ++requested;
      } else if (row.site == 3834) {
        const auto parent = reference(bytes);
        require(find(all, parent, 3833).ref.lane == row.ref.lane);
        require(native<std::size_t>(bytes) == 1 && bytes.empty());
        ++installed;
      } else if (row.site == 3835) {
        const auto parent = reference(bytes);
        require(find(all, parent, 3833).ref.lane == row.ref.lane);
        require(native<RawOperationBranch>(bytes) == RawOperationBranch::returned);
        require(native<std::uint8_t>(bytes) == (mode ? 1 : 0));
        const bool linked = native<bool>(bytes);
        require(linked == (mode == 0));
        if (linked)
          (void)find(all, reference(bytes), 3820);
        require(!native<bool>(bytes) && bytes.empty());
        ++ended;
      }
    }
    require(requested == 1 && installed == 1 && ended == 1 &&
            executed == (mode ? 0U : 1U));
  }
}
void default_oracles(const std::filesystem::path &base) {
  for (unsigned mode = 0; mode < 2; ++mode) {
    const auto effect =
        base / (mode ? "file-default-effect" : "embedded-default-effect");
    const auto all = scenario(
        base / (mode ? "default-file" : "default-embedded"),
        [&](CodingEngine &engine, RawRuntime &, Provider &provider) {
          provider.effect = effect.string();
          if (mode)
            engine.turn(
                {"",
                 read_file(std::filesystem::path{__FILE__}.parent_path().parent_path() /
                           "programs/turn.lua")});
          else
            require(engine.operator_turn("ultracode native fixture",
                                         "error('wrong route')"));
          require(provider.calls == 2 && read_file(effect) == "one");
        });
    std::vector<std::pair<std::int64_t, std::int64_t>> observed;
    for (const auto &row : all)
      if (row.site == 1510 || row.site == 1512) {
        ByteView bytes{row.body};
        (void)native<IdentityBytes>(bytes);
        (void)reference(bytes);
        const auto authored = native<std::int64_t>(bytes);
        if (authored == 3880 || authored == 3881) {
          observed.emplace_back(authored, native<std::int64_t>(bytes));
          require(bytes.empty());
        }
        if (authored == 3882) {
          observed.emplace_back(authored, native<int>(bytes));
          require(bytes.empty());
        }
      }
    require(observed ==
            std::vector<std::pair<std::int64_t, std::int64_t>>{
                {3880, 1}, {3881, 1}, {3882, 0}, {3880, 2}, {3881, 0}, {3882, 0}});
  }
}
Value tool(std::string source) {
  return Value::object(
      {{"definition",
        Value::object(
            {{"name", Value{"native_tool"}},
             {"description", Value{"Fixture"}},
             {"parameters",
              unwrap(parse_json(
                  R"({"type":"object","properties":{"value":{"type":"string"}},"required":["value"],"additionalProperties":false})"))},
             {"source", Value{std::move(source)}}})}});
}
void lua_allocation_oracles(const std::filesystem::path &base) {
  for (unsigned mode = 0; mode < 3; ++mode) {
    std::string source;
    const auto all = scenario(
        base / ("lua-allocation-" + std::to_string(mode)),
        [&](CodingEngine &engine, RawRuntime &) {
          source = mode == 0
                       ? "return args.value"
                       : "_native_args=nil;setmetatable(_G,{__newindex=function(t,k,v) "
                         "if k=='_native_args' then local "
                         "allocated=string.rep('r',8192) end;rawset(t,k,v) end});"
                         "blackbird.display('arm-restore');";
          if (mode == 1)
            source += "return true";
          if (mode == 2)
            source += "error('primary-execution-failure')";
          require(field(engine.operator_call("tool_define", tool(source)), "staged") ==
                  Value{true});
          cuts::failures = 0;
          cuts::deny_prepare = false;
          cuts::deny_restore = mode != 0;
          engine.effect_policy = [&](std::string_view name, const Value &) {
            if (name == "native_tool" && mode == 0)
              cuts::deny_prepare = true;
          };
          engine.display = [&](std::string_view value) {
            if (value == "arm-restore")
              cuts::restore_ready = true;
          };
          try {
            const auto result = engine.operator_call(
                "native_tool",
                Value::object({{"value", Value{std::string(8192, 'a')}}}));
            require(mode == 0 && string_field(result, "error") == "allocation");
          } catch (const Error &error) {
            require(mode != 0 && error.code == ErrorCode::external_unknown);
          }
          require(cuts::failures > 0 && !cuts::current.state);
          cuts::deny_prepare = cuts::deny_restore = cuts::restore_ready = false;
        });
    unsigned faulted = 0, sources = 0, restored = 0, released = 0, terminals = 0;
    for (const auto &row : all) {
      ByteView bytes{row.body};
      if (row.site == 3838) {
        (void)find(all, reference(bytes), 3836);
        require(text(bytes) == "local fn = function(args)\n" + source +
                                   "\nend\nreturn fn(_native_args)" &&
                bytes.empty());
        ++sources;
      }
      if (row.site == 3837) {
        (void)find(all, reference(bytes), 3836);
        require(native<RawOperationBranch>(bytes) == RawOperationBranch::error);
        require(native<std::uint8_t>(bytes) == 0 && native<bool>(bytes));
        require(native<ErrorCode>(bytes) ==
                (mode == 2 ? ErrorCode::external_unknown : ErrorCode::allocation));
        require(native<std::int64_t>(bytes) == 0 && bytes.empty());
        ++terminals;
      }
      if (row.site != 3841)
        continue;
      const auto parent = reference(bytes);
      ByteView request{find(all, parent, 3840).body};
      (void)reference(request);
      const auto phase = native<std::uint8_t>(request);
      const auto prior_captured = native<bool>(request);
      (void)native<int>(request);
      require(request.empty());
      require(native<RawOperationBranch>(bytes) == RawOperationBranch::returned);
      (void)native<std::uint8_t>(bytes);
      require(!native<bool>(bytes));
      const auto status = native<int>(bytes);
      const auto captured = native<bool>(bytes);
      require(bytes.empty());
      if (status == LUA_ERRMEM) {
        require(phase == (mode == 0 ? 0 : 1) && captured);
        require(prior_captured == (mode != 0));
        ++faulted;
      }
      if (phase == 1 && status == LUA_OK) {
        require(captured);
        ++restored;
      }
      if (phase == 2 && status == LUA_OK) {
        require(!captured && prior_captured);
        ++released;
      }
    }
    if (sources != 1 || faulted != 1 || restored != (mode == 2 ? 2U : 3U) ||
        released != (mode == 2 ? 2U : 3U))
      std::cerr << "Lua cut mode=" << mode << " wrappers=" << sources
                << " faults=" << faulted << " restored=" << restored
                << " released=" << released << '\n';
    require(sources == 1 && faulted == 1 && terminals == 1);
    // Defining the tool and invoking it each have an operator wrapper frame.
    // The inner tool frame is also restored/released except when its primary
    // execution error plus failed restoration retains it until VM close.
    require(restored == (mode == 2 ? 2U : 3U) && released == (mode == 2 ? 2U : 3U));
  }
}
void set(Value &value, std::string_view name, Value replacement) {
  for (auto &[key, entry] : value.object())
    if (key == name) {
      entry = std::move(replacement);
      return;
    }
  require(false);
}
void selection_oracles(const std::filesystem::path &base) {
  constexpr std::string_view revision = "fixture-revision-with-variable-native-width";
  const auto all = scenario(base / "selection", [&](CodingEngine &, RawRuntime &) {
    constexpr std::string_view label = "actual fixture caller";
    const RawProgramContext origin{
        RawEventRef{
            raw_lane_epoch(),
            unwrap(raw_emit(
                9200, std::array{std::as_bytes(std::span{label.data(), label.size()})},
                RawPartition::control))},
        {}};
    auto first = field(config("return true"), "workflows").array()[0];
    auto second = field(config("return true"), "workflows").array()[0];
    set(first, "powerwords",
        unwrap(parse_json(R"([{"token":"wordone","color":"keyword"}])")));
    set(second, "name", Value{"second"});
    set(second, "aliases", Value{Value::Array{Value{"second"}}});
    set(second, "powerwords",
        unwrap(parse_json(R"([{"token":"wordtwo","color":"keyword"}])")));
    auto cfg = config("return true");
    set(cfg, "workflows", Value{Value::Array{first, second}});
    WorkflowRegistry registry{cfg, revision, &origin};
    require(registry.select("/nativefixture\targs", &origin)->arguments == "\targs");
    require(!registry.select("/unknown wordone", &origin));
    try {
      (void)registry.select("wordone wordtwo", &origin);
      require(false);
    } catch (const Error &error) {
      require(error.code == ErrorCode::conflict);
    }
    require(string_field(registry.named("second", &origin), "name") == "second");
    WorkflowRegistry unselected{cfg, revision};
    require(unselected.select("wordone")->trigger == "wordone");
  });
  unsigned constructors = 0, selections = 0, triggers = 0, failed = 0;
  std::array<unsigned, 4> routes{};
  for (const auto &row : all) {
    ByteView bytes{row.body};
    if (row.site == 3800) {
      require(native<std::uint8_t>(bytes) == 5);
      ++constructors;
    }
    if (row.site == 3810) {
      require(native<bool>(bytes));
      (void)find(all, reference(bytes), 9200);
      require(text(bytes) == revision);
      (void)native<std::size_t>(bytes);
      require(bytes.empty());
      ++selections;
    }
    if (row.site == 3812) {
      (void)find(all, reference(bytes), 3810);
      const auto route = native<std::uint8_t>(bytes);
      require(route < routes.size());
      const auto indexed = native<bool>(bytes);
      require(indexed == (route == 1 || route == 2));
      if (indexed)
        require(native<std::size_t>(bytes) == 0);
      require(bytes.empty());
      ++routes[route];
    }
    if (row.site == 3814) {
      (void)find(all, reference(bytes), 3810);
      const auto trigger = text(bytes);
      require((trigger == "/nativefixture" || trigger == "wordone") && bytes.empty());
      ++triggers;
    }
    if (row.site == 3813) {
      (void)find(all, reference(bytes), 3810);
      const auto branch = native<RawOperationBranch>(bytes);
      (void)native<std::uint8_t>(bytes);
      const auto error = native<bool>(bytes);
      if (error) {
        require(branch == RawOperationBranch::error &&
                native<ErrorCode>(bytes) == ErrorCode::conflict);
        require(native<std::int64_t>(bytes) == 0);
        ++failed;
      } else {
        require(branch == RawOperationBranch::returned);
        (void)native<bool>(bytes);
      }
      require(bytes.empty());
    }
  }
  require(constructors == 1 && selections == 4 && triggers == 2 && failed == 1 &&
          routes == std::array<unsigned, 4>{1, 1, 1, 1});
}
void source_admission() {
  constexpr std::string_view revision = "native-sized-revision";
  const auto proposal = config("return true");
  const auto lane = identity<EnvironmentId>(101).bytes();
  const auto name = "/bb-program-cut-" + std::to_string(::getpid());
  // The revision is paid CONTROL. The declared definitions DATA source49+65536
  // lacks exactly one byte; the fixed request and Error terminal still fit.
  auto channel = unwrap(RawChannel::create(name, lane, {49 + 65536 - 1, 4096, 4096}));
  RawProducer producer{channel};
  {
    RawThreadBinding binding{producer, lane};
    const std::uint8_t fixture = 1;
    const RawProgramContext origin{
        RawEventRef{lane, unwrap(raw_emit(9200,
                                          std::array{std::as_bytes(
                                              std::span{&fixture, std::size_t{1}})},
                                          RawPartition::control))},
        {}};
    RawCaptureFailureScope failure{true};
    try {
      WorkflowRegistry registry{proposal, revision, &origin};
      require(false);
    } catch (const Error &error) {
      require(error == Error{ErrorCode::capacity});
    }
    require(failure.error() == std::optional{Error{ErrorCode::capacity}});
  }
  unsigned acquired = 0, completed = 0;
  for (const auto partition :
       {RawPartition::data, RawPartition::control, RawPartition::failure}) {
    const auto output = unwrap(channel.read(partition, 0, 128 * 1024));
    ByteView bytes{output.bytes};
    while (!bytes.empty()) {
      const auto length = native<std::uint32_t>(bytes),
                 site = native<std::uint32_t>(bytes);
      (void)native<std::uint64_t>(bytes);
      require(length >= 16 && length - 16 <= bytes.size());
      ByteView body = bytes.first(length - 16);
      bytes = bytes.subspan(length - 16);
      require(site != 3803 && site != 3804 && site != 8000 && site != 8001);
      if (site == 3802) {
        (void)reference(body);
        require(native<std::uint8_t>(body) == 2);
        require(text(body) == revision && body.empty());
        ++acquired;
      }
      if (site == 3801) {
        (void)reference(body);
        require(native<RawOperationBranch>(body) == RawOperationBranch::error);
        require(native<std::uint8_t>(body) == 2 && native<bool>(body));
        require(native<ErrorCode>(body) == ErrorCode::capacity &&
                native<std::int64_t>(body) == 0 && body.empty());
        ++completed;
      }
    }
  }
  require(acquired == 1 && completed == 1);
  unwrap(RawChannel::unlink(name));
}
void invalid_oracles(const std::filesystem::path &base) {
  for (unsigned mode = 0; mode < 2; ++mode) {
    const auto all = scenario(
        base / (mode ? "oversized-proposal" : "invalid-syntax"),
        [&](CodingEngine &engine, RawRuntime &) {
          const auto before = engine.workflows()->revision();
          const auto proposal =
              Value::object({{"proposal", config(mode ? std::string(65536, 'x')
                                                      : "not lua syntax !!!")}});
          if (mode) {
            const auto refused = engine.operator_call("program_config", proposal);
            require(string_field(refused, "error") == "capacity" &&
                    string_field(refused, "resource") == "program_encoding" &&
                    string_field(refused, "effect_outcome") == "not_started");
          } else
            require(string_field(engine.operator_call("program_config", proposal),
                                 "error") == "invalid_range");
          require(engine.workflows()->revision() == before);
        });
    unsigned failed_encoder = 0, invalid_syntax = 0, issued = 0;
    for (const auto &row : all) {
      issued += row.site == 3809 || row.site == 3830 ? 1U : 0U;
      ByteView bytes{row.body};
      if (mode && row.site == 3805) {
        (void)find(all, reference(bytes), 3804);
        require(native<RawOperationBranch>(bytes) == RawOperationBranch::returned);
        (void)native<std::uint8_t>(bytes);
        require(native<bool>(bytes));
        require(native<ErrorCode>(bytes) == ErrorCode::capacity &&
                native<std::int64_t>(bytes) == 0);
        require(!native<bool>(bytes) && bytes.empty());
        ++failed_encoder;
      }
      if (!mode && row.site == 3808) {
        (void)find(all, reference(bytes), 3807);
        require(native<RawOperationBranch>(bytes) == RawOperationBranch::returned);
        require(native<std::uint8_t>(bytes) == 4 && !native<bool>(bytes));
        require(!native<bool>(bytes) && bytes.empty());
        ++invalid_syntax;
      }
    }
    require(issued == 0 && failed_encoder == (mode ? 1U : 0U) &&
            invalid_syntax == (mode ? 0U : 1U));
  }
}
void pending_destruction(const std::filesystem::path &base) {
  const auto all =
      scenario(base / "pending-destruction", [&](CodingEngine &engine, RawRuntime &) {
        engine.turn({"", "return true"});
        const auto proposal = Value::object({{"proposal", config("return true")}});
        require(field((engine.*member(ProgramAccess{}))(proposal, nullptr), "staged") ==
                Value{true});
        // Return with the actual pending state still installed. scenario destroys
        // its runtime object before the engine; the retained original issuer keeps
        // native emission/service lifetime through the engine's real destruction.
      });
  unsigned began = 0, ended = 0;
  for (const auto &row : all) {
    began += row.site == 3830 ? 1U : 0U;
    if (row.site == 3832) {
      ByteView bytes{row.body};
      const auto parent = reference(bytes);
      require(find(all, parent, 3830).ref.lane == row.ref.lane);
      require(native<std::uint8_t>(bytes) == 4 && !native<bool>(bytes) &&
              !native<bool>(bytes) && bytes.empty());
      ++ended;
    }
  }
  require(began == 1 && ended == 1);
}
void cpp_allocation_oracles(const std::filesystem::path &base) {
  cuts::pending_ready = pending_ready;
  for (unsigned mode = 1; mode <= 2; ++mode) {
    const auto directory = base / (mode == 1 ? "cpp-wrapper-allocation"
                                             : "cpp-installed-return-allocation");
    const auto effect = directory / "forbidden";
    const auto all = scenario(directory, [&](CodingEngine &engine, RawRuntime &) {
      engine.turn({"", "return true"});
      cuts::cpp_hit = false;
      if (mode == 1) {
        const auto source = "blackbird.call('write_file',{path=" +
                            unwrap(dump_json(Value{effect.string()})) +
                            ",content='forbidden'});return true";
        require(field(engine.operator_call("tool_define", tool(source)), "staged") ==
                Value{true});
        engine.effect_policy = [&](std::string_view name, const Value &) {
          if (name == "native_tool")
            cuts::cpp_mode = 1;
        };
        const auto result =
            engine.operator_call("native_tool", Value::object({{"value", Value{"x"}}}));
        require(string_field(result, "error") == "allocation");
      } else {
        const auto before = engine.workflows()->revision();
        const auto proposal = Value::object({{"proposal", config("return true")}});
        cuts::cpp_engine = &engine;
        cuts::cpp_mode = 2;
        try {
          (void)(engine.*member(ProgramAccess{}))(proposal, nullptr);
          require(false);
        } catch (const std::bad_alloc &) {
        }
        require(pending_ready(&engine) && engine.workflows()->revision() == before);
        engine.turn({"", "return true"});
        require(!pending_ready(&engine) && engine.workflows()->revision() == before);
      }
      require(cuts::cpp_hit && cuts::cpp_mode == 0 && !std::filesystem::exists(effect));
      cuts::cpp_engine = nullptr;
    });
    unsigned wrapper = 0, original = 0, allocation = 0, pending = 0, discarded = 0;
    for (const auto &row : all) {
      wrapper += row.site == 3836 ? 1U : 0U;
      original += row.site == 3838 ? 1U : 0U;
      pending += row.site == 3830 ? 1U : 0U;
      ByteView body{row.body};
      if (row.site == (mode == 1 ? 3837U : 3801U)) {
        const auto parent = reference(body);
        if (mode == 2) {
          ByteView entered{find(all, parent, 3800).body};
          if (native<std::uint8_t>(entered) != 2)
            continue;
        } else
          (void)find(all, parent, 3836);
        const auto branch = native<RawOperationBranch>(body);
        if (branch == RawOperationBranch::allocation) {
          require(native<std::uint8_t>(body) == (mode == 1 ? 0 : 8));
          require(!native<bool>(body) && body.empty());
          ++allocation;
          if (mode == 1)
            for (const auto &between : all)
              if (between.ref.lane == row.ref.lane &&
                  between.ref.ordinal > parent.ordinal &&
                  between.ref.ordinal < row.ref.ordinal)
                require(between.site != 3840 && between.site != 1506);
        }
      }
      if (mode == 2 && row.site == 3832) {
        const auto parent = reference(body);
        (void)find(all, parent, 3830);
        require(native<std::uint8_t>(body) == 2 && !native<bool>(body) &&
                !native<bool>(body) && body.empty());
        ++discarded;
      }
    }
    require(allocation == 1 && (mode == 1 ? wrapper == 1 && original == 0
                                          : pending == 1 && discarded == 1));
  }
}
void reopen_oracle(const std::filesystem::path &base) {
  const auto directory = base / "selected-reopen";
  const auto effect = directory / "forbidden";
  std::filesystem::create_directories(directory);
  std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
  const auto environment = identity<EnvironmentId>(101);
  const JournalHeader header{environment,
                             identity<AuditStreamId>(102),
                             3,
                             {1024 * 1024, 4 * 1024 * 1024},
                             std::nullopt};
  const JournalCapacity limits{256 * 1024 * 1024, 30000};
  const auto source =
      "blackbird.call('write_file',{path=" + unwrap(dump_json(Value{effect.string()})) +
      ",content='must-not-run-on-restore'});return true";
  std::string revision;
  {
    auto root = unwrap(
        RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                  NativeJournalDirectory::open(directory.string()))),
                              "audit", header, limits));
    AuditLog log{*root};
    ContextStore context{log};
    Provider provider;
    CodingEngine engine{log, context, provider, "test"};
    require(field(engine.operator_call("program_config",
                                       Value::object({{"proposal", config(source)}})),
                  "staged") == Value{true});
    revision = engine.workflows()->revision();
    provider.queued = true;
    try {
      engine.turn({"", "blackbird.request();assert(blackbird.call('workflow_invoke',"
                       "{name='nativefixture'}).status=='accepted');return true"});
      require(false);
    } catch (const Error &error) {
      require(error.code == ErrorCode::conflict);
    }
    // Leave a genuine staged generation for ordinary destruction, after the
    // genuinely parked continuation was discarded by its protocol refusal.
    const auto staged = (engine.*member(ProgramAccess{}))(
        Value::object(
            {{"proposal", config("error('abandoned-pending-must-not-restore')")}}),
        nullptr);
    require(field(staged, "staged") == Value{true});
    require(engine.workflows()->revision() == revision && provider.calls == 1);
    require(!std::filesystem::exists(effect));
  }
  {
    auto root = unwrap(
        RetainedState::open(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(directory.string()))),
                            "audit", header, limits));
    unwrap(root->confirm_recovery());
    recover_coding_session(*root);
    AuditLog log{*root};
    ContextStore context{log};
    Provider provider;
    auto runtime = unwrap(RawRuntime::open(
        directory.string(), environment,
        {{256 * 1024 * 1024, 2 * 1024 * 1024, 128 * 1024}, 128 * 1024}));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    CodingEngine engine{log, context, provider, "test"};
    require(engine.workflows()->revision() == revision);
    require(string_field(engine.workflows()->definitions().array().at(0), "source") ==
            source);
    require(!pending_ready(&engine) && provider.calls == 0 &&
            !std::filesystem::exists(effect));
  }
  const auto all = records(directory);
  unsigned bootstrap = 0, terminal = 0, original = 0;
  for (const auto &row : all) {
    require(row.site != 3830 && row.site != 3833 && row.site != 3820);
    ByteView body{row.body};
    if (row.site == 3800 && native<std::uint8_t>(body) == 0) {
      require(!native<bool>(body) && native<bool>(body));
      (void)native<IdentityBytes>(body);
      require(body.empty());
      ++bootstrap;
      for (const auto &end : all)
        if (end.site == 3801) {
          ByteView outcome{end.body};
          if (!same(reference(outcome), row.ref))
            continue;
          require(native<RawOperationBranch>(outcome) == RawOperationBranch::returned);
          (void)native<std::uint8_t>(outcome);
          require(!native<bool>(outcome) && outcome.empty());
          ++terminal;
        }
    }
    if (row.site == 3803) {
      const auto parent = reference(body);
      (void)find(all, parent, 3800);
      if (native<std::uint8_t>(body) != 1)
        continue;
      require(text(body) == definitions_packet(source) && body.empty());
      ++original;
    }
  }
  require(bootstrap == 1 && terminal == 1 && original == 1 &&
          !std::filesystem::exists(effect));
}
} // namespace
int main() {
  try {
    const auto directory = std::filesystem::temp_directory_path() /
                           ("blackbird-program-raw-" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
    const auto environment = identity<EnvironmentId>(81);
    const JournalHeader header{environment,
                               identity<AuditStreamId>(82),
                               3,
                               {1024 * 1024, 4 * 1024 * 1024},
                               std::nullopt};
    std::string expected_source, expected_revision;
    IdentityBytes main_lane{};
    {
      auto root = unwrap(
          RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                    NativeJournalDirectory::open(directory.string()))),
                                "audit", header, {128 * 1024 * 1024, 20000}));
      AuditLog log{*root};
      ContextStore context{log};
      Provider provider;
      CodingEngine engine{log, context, provider, "test"};
      auto runtime = unwrap(RawRuntime::open(
          directory.string(), environment,
          {{256 * 1024 * 1024, 2 * 1024 * 1024, 128 * 1024}, 128 * 1024}));
      auto handle = unwrap(runtime->prepare_thread());
      RawThreadBinding binding{handle};
      main_lane = raw_lane_epoch();
      const auto effect = directory / "effect";
      const auto source = "blackbird.call('write_file',{path=" +
                          unwrap(dump_json(Value{effect.string()})) +
                          ",content='one-real-workflow-effect'});return args.arguments";
      expected_source = source;
      auto result = engine.operator_call("program_config",
                                         Value::object({{"proposal", config(source)}}));
      require(field(result, "staged") == Value{true});
      expected_revision = engine.workflows()->revision();
      require(engine.operator_turn("/nativefixture \tA\nB", "error('wrong route')"));
      require(engine.workflow_result() == Value{" \tA\nB"});
      require(read_file(effect) == "one-real-workflow-effect");
    }
    const auto all = records(directory);
    unsigned calls = 0, pending = 0, executed = 0;
    unsigned proposals = 0, definitions = 0, pending_ends = 0, transfers = 0;
    for (const auto &record : all) {
      ByteView bytes{record.body};
      const auto site = record.site;
      calls += site == 3800 ? 1U : 0U;
      pending += site == 3830 ? 1U : 0U;
      executed += site == 3820 ? 1U : 0U;
      if (site == 3803) {
        const auto owner = reference(bytes);
        require(find(all, owner, 3800).ref.lane == main_lane);
        const auto kind = native<std::uint8_t>(bytes);
        const auto body = text(bytes);
        require(bytes.empty());
        if (kind == 0) {
          require(body == config_packet(expected_source));
          ++proposals;
        } else {
          require(kind == 1 && body == definitions_packet(expected_source));
          ++definitions;
        }
      }
      if (site == 3830) {
        const auto parent = reference(bytes);
        require(find(all, parent, 3800).ref.lane == main_lane);
        require(record.ref.lane != main_lane);
        require(native<std::uint8_t>(bytes) == 0);
        require(bytes.size() == 16);
      }
      if (site == 3831) {
        const auto parent = reference(bytes);
        require(find(all, parent, 3830).ref.lane == record.ref.lane);
        require(text(bytes) == expected_revision && bytes.empty());
      }
      if (site == 3832) {
        const auto parent = reference(bytes);
        require(find(all, parent, 3830).ref.lane == record.ref.lane);
        require(native<std::uint8_t>(bytes) == 0 && native<bool>(bytes));
        const auto activation = reference(bytes);
        ByteView activated{find(all, activation, 1905).body};
        (void)reference(activated);
        require(native<std::uint8_t>(activated) == 0);
        require(text(activated) == expected_revision && activated.empty());
        require(!native<bool>(bytes) && bytes.empty());
        ++pending_ends;
      }
      if (site == 3820) {
        const auto selection = reference(bytes);
        (void)find(all, selection, 3810);
        require(bytes.size() == 16 + 2 * sizeof(unsigned));
      }
      if (site == 3841) {
        const auto parent = reference(bytes);
        ByteView entered{find(all, parent, 3840).body};
        (void)reference(entered);
        const auto phase = native<std::uint8_t>(entered);
        (void)native<bool>(entered);
        (void)native<int>(entered);
        require(entered.empty());
        require(native<RawOperationBranch>(bytes) == RawOperationBranch::returned);
        (void)native<std::uint8_t>(bytes);
        require(!native<bool>(bytes));
        require(native<int>(bytes) == LUA_OK);
        require(native<bool>(bytes) == (phase != 2) && bytes.empty());
        ++transfers;
      }
    }
    require(calls == 5 && pending == 1 && pending_ends == 1 && executed == 1);
    // program_config's operator wrapper, workflow_invoke's operator wrapper,
    // and the actual workflow child each prepare/restore/release once.
    if (proposals != 1 || definitions != 2 || transfers != 9)
      std::cerr << "core proposals=" << proposals << " definitions=" << definitions
                << " transfers=" << transfers << '\n';
    require(proposals == 1 && definitions == 2 && transfers == 9);
    lifecycle(directory);
    guard_oracles(directory);
    parked_oracles(directory);
    default_oracles(directory);
    lua_allocation_oracles(directory);
    selection_oracles(directory);
    source_admission();
    invalid_oracles(directory);
    pending_destruction(directory);
    cpp_allocation_oracles(directory);
    reopen_oracle(directory);
    std::filesystem::remove_all(directory);
    std::cout << "program workflow native raw passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
