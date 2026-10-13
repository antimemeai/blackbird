#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/raw_authored.hpp"
#include "blackbird/raw_command.hpp"
#include "blackbird/raw_operation.hpp"
#include "blackbird/raw_provider.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/raw_turn.hpp"
#include "stream_capture.hpp"
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstring>
#include <set>
#include <thread>

namespace blackbird {
namespace {
template <class T> ByteView raw_native(const T &value) noexcept {
  return std::as_bytes(std::span{&value, std::size_t{1}});
}
ByteView raw_text(std::string_view value) noexcept {
  return std::as_bytes(std::span{value.data(), value.size()});
}
void emit_provider_fact(std::uint32_t site, std::span<const ByteView> fields,
                        RawControlToken *claim = nullptr) {
  struct RestoreErrno {
    int saved = errno;
    ~RestoreErrno() { errno = saved; }
  } restore;
  if (claim == nullptr) {
    std::uint64_t bytes = raw_envelope_size;
    for (const auto field : fields) {
      if (field.size() > UINT32_MAX - bytes) {
        raw_fence({ErrorCode::overflow});
        throw Error{ErrorCode::overflow};
      }
      bytes += field.size();
    }
    const auto admitted = raw_wait_data(bytes);
    if (!admitted.has_value()) {
      if (admitted.error().code != ErrorCode::interrupted)
        raw_fence(admitted.error());
      throw admitted.error();
    }
  }
  const auto emitted =
      raw_emit(site, fields, claim ? RawPartition::control : RawPartition::data, claim);
  if (!emitted.has_value())
    throw emitted.error();
}
enum class ValueTag { array, object, number, null };
char markers[4]; // array, object, exact number, null (registry keys, no string
                 // allocation).
void tag(lua_State *L, ValueTag kind) {
  lua_rawgetp(L, LUA_REGISTRYINDEX, &markers[static_cast<int>(kind)]);
  lua_setmetatable(L, -2);
}
bool tagged(lua_State *L, int index, ValueTag kind) {
  if (!lua_getmetatable(L, index))
    return false;
  lua_rawgetp(L, LUA_REGISTRYINDEX, &markers[static_cast<int>(kind)]);
  const bool equal = lua_rawequal(L, -1, -2) != 0;
  lua_pop(L, 2);
  return equal;
}
void push_value(lua_State *L, const Value &j) {
  std::visit(
      [&](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
          lua_newtable(L);
          tag(L, ValueTag::null);
        } else if constexpr (std::is_same_v<T, bool>)
          lua_pushboolean(L, v);
        else if constexpr (std::is_same_v<T, std::string>)
          lua_pushlstring(L, v.data(), v.size());
        else if constexpr (std::is_same_v<T, Number>) {
          std::visit(
              [&](const auto &n) {
                using N = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<N, std::int64_t>)
                  lua_pushinteger(L, n);
                else if constexpr (std::is_same_v<N, double>)
                  lua_pushnumber(L, n);
                else if constexpr (std::is_same_v<N, std::uint64_t>) {
                  if (n <= static_cast<std::uint64_t>(INT64_MAX))
                    lua_pushinteger(L, static_cast<lua_Integer>(n));
                  else {
                    lua_createtable(L, 3, 0);
                    lua_pushinteger(L, 0);
                    lua_rawseti(L, -2, 1);
                    lua_pushinteger(L, static_cast<lua_Integer>(n & 0xffffffff));
                    lua_rawseti(L, -2, 2);
                    lua_pushinteger(L, static_cast<lua_Integer>(n >> 32));
                    lua_rawseti(L, -2, 3);
                    tag(L, ValueTag::number);
                  }
                } else {
                  lua_createtable(L, 4, 0);
                  lua_pushinteger(L, 1);
                  lua_rawseti(L, -2, 1);
                  lua_pushboolean(L, n.negative);
                  lua_rawseti(L, -2, 2);
                  lua_pushinteger(L, n.exponent);
                  lua_rawseti(L, -2, 3);
                  lua_createtable(
                      L,
                      static_cast<int>(std::min<std::size_t>(n.limbs.size(), INT_MAX)),
                      0);
                  for (std::size_t i = 0; i < n.limbs.size(); ++i) {
                    lua_pushinteger(L, n.limbs[i]);
                    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
                  }
                  lua_rawseti(L, -2, 4);
                  tag(L, ValueTag::number);
                }
              },
              v.storage());
        } else if constexpr (std::is_same_v<T, Value::Array>) {
          lua_newtable(L);
          for (std::size_t i = 0; i < v.size(); ++i) {
            push_value(L, v[i]);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
          }
          tag(L, ValueTag::array);
        } else {
          lua_newtable(L);
          for (const auto &[key, value] : v) {
            lua_pushlstring(L, key.data(), key.size());
            push_value(L, value);
            lua_rawset(L, -3);
          }
          tag(L, ValueTag::object);
        }
      },
      j.value());
}
std::string lua_string(lua_State *L, int index) {
  if (lua_type(L, index) != LUA_TSTRING)
    throw Error{ErrorCode::invalid_range};
  std::size_t length = 0;
  const char *value = lua_tolstring(L, index, &length);
  return {value, length};
}
// Caller reserves the full traversal stack first. Only raw, nonallocating Lua
// reads occur while C++ owners exist; no metamethods/string-key interning here.
struct ValueRead {
  std::size_t depth;
  std::size_t *nodes;
};
Value read_value(lua_State *L, int index, ValueRead budget) {
  const auto depth = budget.depth;
  auto &nodes = *budget.nodes;
  if (depth > 128 || ++nodes > 500000)
    throw Error{ErrorCode::capacity};
  index = lua_absindex(L, index);
  switch (lua_type(L, index)) {
  case LUA_TNIL:
    return Value{};
  case LUA_TBOOLEAN:
    return Value{lua_toboolean(L, index) != 0};
  case LUA_TSTRING:
    return Value{lua_string(L, index)};
  case LUA_TNUMBER:
    return lua_isinteger(L, index) ? Value{Number{lua_tointeger(L, index)}}
                                   : Value{Number{lua_tonumber(L, index)}};
  case LUA_TTABLE: {
    if (tagged(L, index, ValueTag::null))
      return Value{};
    if (tagged(L, index, ValueTag::number)) {
      auto integer = [&](lua_Integer field) {
        lua_rawgeti(L, index, field);
        if (!lua_isinteger(L, -1))
          throw Error{ErrorCode::invalid_range};
        const auto n = lua_tointeger(L, -1);
        lua_pop(L, 1);
        return n;
      };
      const auto kind = integer(1);
      if (kind == 0) {
        const auto low = integer(2), high = integer(3);
        if (low < 0 || high < 0 || low > UINT32_MAX || high > UINT32_MAX)
          throw Error{ErrorCode::invalid_range};
        return Value{Number{static_cast<std::uint64_t>(low) |
                            (static_cast<std::uint64_t>(high) << 32)}};
      }
      if (kind != 1)
        throw Error{ErrorCode::invalid_range};
      lua_rawgeti(L, index, 2);
      if (lua_type(L, -1) != LUA_TBOOLEAN)
        throw Error{ErrorCode::invalid_range};
      const bool negative = lua_toboolean(L, -1) != 0;
      lua_pop(L, 1);
      Decimal n{negative, integer(3), {}};
      lua_rawgeti(L, index, 4);
      if (lua_type(L, -1) != LUA_TTABLE)
        throw Error{ErrorCode::invalid_range};
      const auto count = lua_rawlen(L, -1);
      if (!count || count > 500000 - nodes)
        throw Error{ErrorCode::capacity};
      nodes += count;
      n.limbs.reserve(count);
      for (std::size_t i = 1; i <= count; ++i) {
        lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
        const auto limb = lua_tointeger(L, -1);
        if (!lua_isinteger(L, -1) || limb < 0 || limb >= 1000000000)
          throw Error{ErrorCode::invalid_range};
        n.limbs.push_back(static_cast<std::uint32_t>(limb));
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      return Value{Number{std::move(n)}};
    }
    const auto length = lua_rawlen(L, index);
    const bool array = tagged(L, index, ValueTag::array) ||
                       (!tagged(L, index, ValueTag::object) && length != 0);
    if (array) {
      std::size_t count = 0;
      lua_pushnil(L);
      while (lua_next(L, index)) {
        if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1 ||
            static_cast<unsigned long long>(lua_tointeger(L, -2)) > length)
          throw Error{ErrorCode::invalid_range};
        ++count;
        lua_pop(L, 1);
      }
      if (count != length)
        throw Error{ErrorCode::invalid_range};
      Value::Array values;
      values.reserve(length);
      for (std::size_t i = 0; i < length; ++i) {
        lua_rawgeti(L, index, static_cast<lua_Integer>(i + 1));
        values.push_back(read_value(L, -1, {depth + 1, &nodes}));
        lua_pop(L, 1);
      }
      return Value{std::move(values)};
    }
    Value::Object fields;
    lua_pushnil(L);
    while (lua_next(L, index)) {
      auto key = lua_string(L, -2);
      auto value = read_value(L, -1, {depth + 1, &nodes});
      fields.emplace_back(std::move(key), std::move(value));
      lua_pop(L, 1);
    }
    std::sort(fields.begin(), fields.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    return Value::object(std::move(fields));
  }
  default:
    throw Error{ErrorCode::unsupported};
  }
}
Value error_value(Error e) {
  return Value::object(
      {{"error", Value{error_name(e.code)}}, {"detail", Value{Number{e.detail}}}});
}
template <class T> T parse_id(std::string_view s) {
  if (s.size() != 32)
    throw Error{ErrorCode::invalid_identity};
  IdentityBytes out{};
  for (std::size_t i = 0; i < 16; ++i) {
    unsigned n = 0;
    const auto r = std::from_chars(s.data() + 2 * i, s.data() + 2 * i + 2, n, 16);
    if (r.ec != std::errc{} || r.ptr != s.data() + 2 * i + 2)
      throw Error{ErrorCode::invalid_identity};
    out[i] = static_cast<std::byte>(n);
  }
  return unwrap(T::from_bytes(out));
}
void validate_protocol(const Value::Array &items,
                       std::optional<RawEventRef> parent = {}) {
  RawProtocolTrace trace{items.size(), parent};
  trace.run([&] {
    std::set<std::string> seen, pending;
    for (const auto &item : items) {
      const auto *type = item.find("type");
      if (type == nullptr)
        continue;
      const auto &s = type->string();
      if (s == "function_call") {
        const auto &call = string_field(item, "call_id");
        if (!seen.insert(call).second) {
          trace.conflict(call);
          throw Error{ErrorCode::conflict};
        }
        pending.insert(call);
      }
      if (s == "function_call_output") {
        const auto &call = string_field(item, "call_id");
        if (pending.erase(call) != 1) {
          trace.conflict(call);
          throw Error{ErrorCode::conflict};
        }
      }
    }
    if (!pending.empty()) {
      trace.conflict(*pending.begin());
      throw Error{ErrorCode::conflict};
    }
  });
}
} // namespace
Value OpenAiCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture) {
  return respond(request, capture, nullptr);
}
Value OpenAiCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture,
    const RawProviderOwner *owner) {
  return respond(request, capture, owner, nullptr);
}
Value OpenAiCodingProvider::respond(
    const Value &request, const std::function<void(std::string_view)> &capture,
    const RawProviderOwner *owner, const RawAdapterContext *context) {
  std::optional<RawWaitScope> deadline_scope;
  if (config_.timeout_seconds >= 1 && config_.timeout_seconds <= 3600) {
    auto control = raw_wait_control();
    control.deadline = std::chrono::steady_clock::now() +
                       std::chrono::seconds{config_.timeout_seconds};
    deadline_scope.emplace(control);
  }
  RawProviderPhase adapter;
  return adapter.run([&] {
    adapter.start(5, context);
    auto config = config_;
    config.raw_owner = owner;
    config.raw_context = context;
    config.response_observer = capture;
    config.cancelled = cancelled;
    auto login = unwrap(codex_login(config));
    auto raw = unwrap(openai_http(config, login, "responses", &request));
    return unwrap(completed_response(raw, {}, context));
  });
}
struct CodingEngine::CommandTaskActivity {
  RawThreadHandle owner;
  RawTaskActivityTrace trace;
  std::function<void(const Value &)> &observer;
  std::string id, attempt_text, operation;
  OperationAttemptId attempt;
  CommandTaskMap::node_type *pending_node = nullptr;
  bool *transferred = nullptr;
  bool bound = false, ended = false;
  CommandTaskActivity(std::function<void(const Value &)> &callback, std::string task,
                      OperationAttemptId actual_attempt, std::string encoded_attempt,
                      std::string operation_name)
      : owner(raw_current_thread()), observer(callback), id(std::move(task)),
        attempt_text(std::move(encoded_attempt)), operation(std::move(operation_name)),
        attempt(actual_attempt) {}
  CommandTaskActivity(const CommandTaskActivity &) = delete;
  CommandTaskActivity &operator=(const CommandTaskActivity &) = delete;
  ~CommandTaskActivity() {
    if (bound && !ended)
      emit(3);
  }
  void bind(RawEventRef operation_request) noexcept {
    trace.binding(operation_request, attempt, id);
    bound = true;
  }
  void emit(std::uint8_t branch, const Value *exit = nullptr) noexcept {
    if (!bound || ended)
      return;
    if (branch != 0)
      ended = true;
    RawThreadBinding binding{owner};
    const auto activity = trace.activity(branch);
    const auto notification = trace.notification(activity, bool(observer));
    if (!observer) {
      trace.notified(notification, 0);
      return;
    }
    try {
      std::string phase{branch == 0   ? "running"
                        : branch == 1 ? "finished"
                        : branch == 2 ? "failed"
                                      : "outcome unknown"};
      if (exit)
        phase += " (exit " + exit->number().text() + ")";
      observer(Value::object({{"task_id", Value{id}},
                              {"attempt", Value{attempt_text}},
                              {"operation", Value{operation}},
                              {"phase", Value{std::move(phase)}}}));
      trace.notified(notification, 1);
    } catch (const Error &error) {
      trace.notified(notification, 2, error);
    } catch (const std::bad_alloc &) {
      trace.notified(notification, 3);
    } catch (...) {
      trace.notified(notification, 4);
    }
  }
};
struct CodingEngine::Runtime {
  CodingEngine &engine;
  struct Trace {
    RawThreadHandle handle;
    RawAuthoredOwner owner;
    std::optional<RawControlToken> setup;
    RawAuthoredReference installation;
    explicit Trace(CodingEngine &engine)
        : handle(raw_current_thread()),
          owner{engine.generation_.bytes(), {}, raw_enabled(), {}} {}
    void begin(const std::optional<SourceReference> &source, std::size_t length) {
      if (!owner.enabled)
        return;
      setup.emplace(unwrap(raw_claim_control(175)));
      const bool available = source.has_value();
      if (available) {
        const std::array fields{ByteView{owner.generation},
                                ByteView{source->journal.bytes()},
                                raw_native(source->sequence), raw_native(length)};
        (void)raw_authored_publish(1505, fields);
        const std::array request{ByteView{owner.generation}, raw_native(available),
                                 ByteView{source->journal.bytes()},
                                 raw_native(source->sequence)};
        owner.vm = raw_authored_publish(1500, request);
      } else {
        const std::array request{ByteView{owner.generation}, raw_native(available)};
        owner.vm = raw_authored_publish(1500, request);
      }
    }
    void allocated(bool created) {
      const std::uint8_t outcome = created ? 1 : 0;
      const std::array fields{ByteView{owner.vm.lane}, raw_native(owner.vm.ordinal),
                              raw_native(outcome)};
      (void)raw_authored_publish(1501, fields, setup ? &*setup : nullptr);
    }
    void installing() {
      const std::array fields{ByteView{owner.vm.lane}, raw_native(owner.vm.ordinal)};
      installation = raw_authored_publish(1502, fields, setup ? &*setup : nullptr);
    }
    void installed(int lua_status) {
      const std::array fields{ByteView{installation.lane},
                              raw_native(installation.ordinal), raw_native(lua_status)};
      (void)raw_authored_publish(1503, fields, setup ? &*setup : nullptr);
    }
    void finish(std::uint8_t branch, std::optional<Error> caught_error = {}) noexcept {
      if (!setup.has_value() || owner.vm.ordinal == 0) {
        setup.reset();
        return;
      }
      if (owner.admission_failure.has_value())
        caught_error = owner.admission_failure;
      try {
        if (caught_error.has_value()) {
          const std::array fields{ByteView{owner.vm.lane}, raw_native(owner.vm.ordinal),
                                  raw_native(branch), raw_native(caught_error->code),
                                  raw_native(caught_error->detail)};
          (void)raw_authored_publish(1504, fields, &*setup);
        } else {
          const std::array fields{ByteView{owner.vm.lane}, raw_native(owner.vm.ordinal),
                                  raw_native(branch)};
          (void)raw_authored_publish(1504, fields, &*setup);
        }
      } catch (const Error &failure) {
        raw_fence(failure);
      }
      setup.reset();
    }
  } trace;
  static_assert(sizeof(lua_Integer) == 8 &&
                std::numeric_limits<lua_Integer>::is_signed &&
                std::is_same_v<lua_Number, double> && sizeof(int) == 4);
  enum class SourceKind : std::uint8_t {
    root = 1,
    tool,
    tool_api,
    print,
    module,
    validation
  };
  struct Loaded {
    int lua_status;
    RawAuthoredReference reference;
  };
  Loaded load(lua_State *L, std::string_view source, const char *name, SourceKind kind,
              RawControlToken *credit) {
    if (!trace.owner.enabled)
      return {luaL_loadbufferx(L, source.data(), source.size(), name, "t"), {}};
    const auto length = source.size();
    const std::array requested{ByteView{trace.owner.vm.lane},
                               raw_native(trace.owner.vm.ordinal), raw_native(kind),
                               raw_native(length), raw_text(source)};
    const auto request = raw_authored_publish(1506, requested);
    const int lua_status = luaL_loadbufferx(L, source.data(), length, name, "t");
    const bool available = lua_status == LUA_OK;
    if (available) {
      const void *function = lua_topointer(L, -1);
      const std::array fields{ByteView{request.lane}, raw_native(request.ordinal),
                              raw_native(lua_status), raw_native(available),
                              raw_native(function)};
      (void)raw_authored_publish(1507, fields, credit);
    } else {
      const std::array fields{ByteView{request.lane}, raw_native(request.ordinal),
                              raw_native(lua_status), raw_native(available)};
      (void)raw_authored_publish(1507, fields, credit);
    }
    return {lua_status, request};
  }
  void acquisition_failed(Error why) noexcept {
    if (why.code == ErrorCode::interrupted || why.code == ErrorCode::audit_unavailable)
      trace.owner.admission_failure = why;
  }
  std::optional<RawControlToken> claim(std::uint64_t bytes) {
    if (!trace.owner.enabled)
      return {};
    if (trace.owner.admission_failure.has_value())
      throw *trace.owner.admission_failure;
    auto result = raw_claim_control(bytes);
    if (!result.has_value()) {
      trace.owner.admission_failure = result.error();
      throw result.error();
    }
    return std::move(result).value();
  }
  int load_and_call(lua_State *L, std::string_view source, const char *name,
                    SourceKind kind, int results) {
    auto credit = claim(161);
    try {
      const auto loaded = load(L, source, name, kind, credit ? &*credit : nullptr);
      if (loaded.lua_status != LUA_OK)
        return loaded.lua_status;
      if (!trace.owner.enabled) {
        failure.reset();
        return lua_pcall(L, 0, results, 0);
      }
      const std::array fields{
          ByteView{trace.owner.vm.lane}, raw_native(trace.owner.vm.ordinal),
          ByteView{loaded.reference.lane}, raw_native(loaded.reference.ordinal)};
      const auto request =
          raw_authored_publish(1508, fields, credit ? &*credit : nullptr);
      failure.reset();
      const int lua_status = lua_pcall(L, 0, results, 0);
      const std::array returned{ByteView{request.lane}, raw_native(request.ordinal),
                                raw_native(lua_status)};
      (void)raw_authored_publish(1509, returned, credit ? &*credit : nullptr);
      return lua_status;
    } catch (const Error &why) {
      acquisition_failed(why);
      throw;
    }
  }
  int validation_load(std::string_view source, const char *name) {
    auto credit = claim(53);
    try {
      return load(state, source, name, SourceKind::validation,
                  credit ? &*credit : nullptr)
          .lua_status;
    } catch (const Error &why) {
      acquisition_failed(why);
      throw;
    }
  }
  inline static char authored_error_tag;
  static int authored_error_text(lua_State *L) {
    const auto *why = static_cast<const Error *>(lua_touserdata(L, 1));
    lua_pushstring(L, error_name(why->code));
    return 1;
  }
  static int authored_raise(lua_State *L, Error why) {
    auto *stored = static_cast<Error *>(lua_newuserdatauv(L, sizeof(Error), 0));
    std::construct_at(stored, why);
    lua_rawgetp(L, LUA_REGISTRYINDEX, &authored_error_tag);
    lua_setmetatable(L, -2);
    return lua_error(L);
  }
  static std::optional<Error> authored_error(lua_State *L, int index) noexcept {
    if (lua_type(L, index) != LUA_TUSERDATA || lua_rawlen(L, index) != sizeof(Error))
      return {};
    if (lua_getmetatable(L, index) == 0)
      return {};
    lua_rawgetp(L, LUA_REGISTRYINDEX, &authored_error_tag);
    const bool tagged = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    if (!tagged)
      return {};
    return *static_cast<const Error *>(lua_touserdata(L, index));
  }
  static int authored(lua_State *L) {
    auto *self = static_cast<Runtime *>(lua_touserdata(L, lua_upvalueindex(1)));
    bool failed = false;
    Error rejected{ErrorCode::external_unknown};
    {
      auto admitted = raw_authored_admit(self->trace.owner);
      if (!admitted.has_value()) {
        rejected = admitted.error();
        failed = true;
      } else {
        auto credit = std::move(admitted).value();
        const int count = lua_gettop(L), site_type = lua_type(L, 1),
                  value_type = lua_type(L, 2);
        const bool site_available = lua_isinteger(L, 1) != 0;
        const lua_Integer site = site_available ? lua_tointeger(L, 1) : 0;
        try {
          if (count != 2 || !site_available || site <= 0)
            throw Error{ErrorCode::invalid_range};
          if (value_type != LUA_TNUMBER && value_type != LUA_TBOOLEAN &&
              value_type != LUA_TSTRING)
            throw Error{ErrorCode::unsupported};
          if (value_type == LUA_TSTRING) {
            std::size_t length = 0;
            const char *bytes = lua_tolstring(L, 2, &length);
            const std::array fields{ByteView{self->trace.owner.generation},
                                    ByteView{self->trace.owner.vm.lane},
                                    raw_native(self->trace.owner.vm.ordinal),
                                    raw_native(site),
                                    raw_native(length),
                                    std::as_bytes(std::span{bytes, length})};
            if (self->trace.owner.enabled)
              (void)raw_authored_publish(1513, fields);
          } else if (value_type == LUA_TBOOLEAN) {
            const int value = lua_toboolean(L, 2);
            const std::array fields{ByteView{self->trace.owner.generation},
                                    ByteView{self->trace.owner.vm.lane},
                                    raw_native(self->trace.owner.vm.ordinal),
                                    raw_native(site), raw_native(value)};
            if (self->trace.owner.enabled)
              (void)raw_authored_publish(1512, fields);
          } else if (lua_isinteger(L, 2)) {
            const lua_Integer value = lua_tointeger(L, 2);
            const std::array fields{ByteView{self->trace.owner.generation},
                                    ByteView{self->trace.owner.vm.lane},
                                    raw_native(self->trace.owner.vm.ordinal),
                                    raw_native(site), raw_native(value)};
            if (self->trace.owner.enabled)
              (void)raw_authored_publish(1510, fields);
          } else {
            const lua_Number value = lua_tonumber(L, 2);
            const std::array fields{ByteView{self->trace.owner.generation},
                                    ByteView{self->trace.owner.vm.lane},
                                    raw_native(self->trace.owner.vm.ordinal),
                                    raw_native(site), raw_native(value)};
            if (self->trace.owner.enabled)
              (void)raw_authored_publish(1511, fields);
          }
        } catch (const Error &caught_error) {
          if (credit.has_value()) {
            try {
              if (site_available) {
                const std::array fields{ByteView{self->trace.owner.generation},
                                        ByteView{self->trace.owner.vm.lane},
                                        raw_native(self->trace.owner.vm.ordinal),
                                        raw_native(count),
                                        raw_native(site_type),
                                        raw_native(value_type),
                                        raw_native(site_available),
                                        raw_native(site),
                                        raw_native(caught_error.code),
                                        raw_native(caught_error.detail)};
                (void)raw_authored_publish(1515, fields, &*credit);
              } else {
                const std::array fields{ByteView{self->trace.owner.generation},
                                        ByteView{self->trace.owner.vm.lane},
                                        raw_native(self->trace.owner.vm.ordinal),
                                        raw_native(count),
                                        raw_native(site_type),
                                        raw_native(value_type),
                                        raw_native(site_available),
                                        raw_native(caught_error.code),
                                        raw_native(caught_error.detail)};
                (void)raw_authored_publish(1515, fields, &*credit);
              }
            } catch (const Error &ingest) {
              raw_fence(ingest);
            }
          }
          if (caught_error.code == ErrorCode::interrupted ||
              caught_error.code == ErrorCode::audit_unavailable)
            self->trace.owner.admission_failure = caught_error;
          rejected = caught_error;
          failed = true;
        }
      }
    }
    if (failed)
      return authored_raise(L, rejected);
    return 0;
  }
  static int module_load(lua_State *L) {
    auto *self = static_cast<Runtime *>(lua_touserdata(L, lua_upvalueindex(1)));
    luaL_checktype(L, 1, LUA_TSTRING);
    luaL_checktype(L, 2, LUA_TSTRING);
    luaL_checktype(L, 3, LUA_TTABLE);
    int lua_status = LUA_ERRRUN;
    bool failed = false;
    Error rejected{ErrorCode::external_unknown};
    {
      try {
        auto credit = self->claim(53);
        std::size_t length = 0;
        const char *source = lua_tolstring(L, 1, &length);
        const char *name = lua_tostring(L, 2);
        lua_status = self->load(L, {source, length}, name, SourceKind::module,
                                credit ? &*credit : nullptr)
                         .lua_status;
      } catch (const Error &caught_error) {
        self->acquisition_failed(caught_error);
        rejected = caught_error;
        failed = true;
      }
    }
    if (failed)
      return authored_raise(L, rejected);
    if (lua_status != LUA_OK) {
      lua_pushnil(L);
      lua_insert(L, -2);
      return 2;
    }
    lua_pushvalue(L, 3);
    (void)lua_setupvalue(L, -2, 1);
    return 1;
  }
  static int module_call(lua_State *L) {
    auto *self = static_cast<Runtime *>(lua_touserdata(L, lua_upvalueindex(1)));
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (!lua_checkstack(L, 2))
      return luaL_error(L, "Module call stack exhausted");
    int lua_status = LUA_ERRRUN;
    bool failed = false;
    Error rejected{ErrorCode::external_unknown};
    {
      try {
        auto credit = self->claim(92);
        const void *function =
            self->trace.owner.enabled ? lua_topointer(L, 1) : nullptr;
        const std::array fields{ByteView{self->trace.owner.vm.lane},
                                raw_native(self->trace.owner.vm.ordinal),
                                raw_native(function)};
        const auto request =
            self->trace.owner.enabled
                ? raw_authored_publish(1517, fields, credit ? &*credit : nullptr)
                : RawAuthoredReference{};
        lua_pushvalue(L, 1);
        lua_status = lua_pcall(L, 0, 1, 0);
        const std::array returned{ByteView{request.lane}, raw_native(request.ordinal),
                                  raw_native(lua_status)};
        if (self->trace.owner.enabled)
          (void)raw_authored_publish(1518, returned, credit ? &*credit : nullptr);
      } catch (const Error &caught_error) {
        self->acquisition_failed(caught_error);
        rejected = caught_error;
        failed = true;
      }
    }
    if (failed)
      return authored_raise(L, rejected);
    lua_pushboolean(L, lua_status == LUA_OK);
    lua_insert(L, -2);
    return 2;
  }
  lua_State *state = nullptr;
  Value scratch;
  std::string error;
  std::optional<Error> failure;
  std::array<char, 512> bridge_error{};
  void failed(Error why, const char *message) noexcept {
    failure = why;
    if (why.code == ErrorCode::capacity)
      engine.capacity_stopped_ = true;
    const auto count = std::min(std::strlen(message), bridge_error.size() - 1);
    std::memcpy(bridge_error.data(), message, count);
    bridge_error[count] = '\0';
  }
  explicit Runtime(CodingEngine &e) : engine(e), trace(e) {
    trace.begin(e.root_source_, e.root_source_length_);
    try {
      state = luaL_newstate();
      trace.allocated(state != nullptr);
      if (state == nullptr) {
        trace.finish(2, Error{ErrorCode::allocation});
        throw Error{ErrorCode::allocation};
      }
      *static_cast<Runtime **>(lua_getextraspace(state)) = this;
      lua_sethook(state, interrupt_hook, LUA_MASKCOUNT, 1000);
      lua_pushcfunction(state, install);
      lua_pushlightuserdata(state, this);
      trace.installing();
      const int lua_status = lua_pcall(state, 1, 0, 0);
      trace.installed(lua_status);
      if (lua_status != LUA_OK) {
        lua_close(state);
        state = nullptr;
        const auto caught_error = failure.value_or(Error{ErrorCode::allocation});
        trace.finish(3, caught_error);
        throw caught_error;
      }
    } catch (const Error &caught) {
      if (state != nullptr) {
        lua_close(state);
        state = nullptr;
        trace.finish(1, caught);
      }
      throw;
    }
  }
  ~Runtime() {
    std::optional<RawThreadBinding> retained;
    if (trace.handle)
      retained.emplace(trace.handle);
    if (state != nullptr)
      lua_close(state);
    trace.finish(1);
  }
  enum Op {
    request_op,
    call_op,
    context_op,
    edit_op,
    originals_op,
    restore_op,
    append_op,
    decode_op,
    encode_op,
    display_op,
    array_op,
    present_op,
    restarting_op,
    stats_op,
    manage_op,
    inspect_op,
    packet_decode_op,
    packet_encode_op,
    format_op
  };
  static int install(lua_State *L) {
    auto *self = static_cast<Runtime *>(lua_touserdata(L, 1));
    bool failed = false;
    {
      try {
        return install_impl(L);
      } catch (const Error &error) {
        self->failed(error, error_name(error.code));
        failed = true;
      } catch (const std::bad_alloc &) {
        self->failed({ErrorCode::allocation}, "allocation");
        failed = true;
      }
    }
    if (failed)
      return luaL_error(L, "VM installation failed");
    return 0;
  }
  static int install_impl(lua_State *L) {
    auto *self = static_cast<Runtime *>(lua_touserdata(L, 1));
    // Host effects go through the owned audit boundary, including module reads.
    const luaL_Reg libraries[] = {{LUA_GNAME, luaopen_base},
                                  {LUA_COLIBNAME, luaopen_coroutine},
                                  {LUA_TABLIBNAME, luaopen_table},
                                  {LUA_STRLIBNAME, luaopen_string},
                                  {LUA_MATHLIBNAME, luaopen_math},
                                  {LUA_UTF8LIBNAME, luaopen_utf8},
                                  {nullptr, nullptr}};
    for (const auto *library = libraries; library->name != nullptr; ++library) {
      luaL_requiref(L, library->name, library->func, 1);
      lua_pop(L, 1);
    }
    lua_pushnil(L);
    lua_setglobal(L, "dofile");
    lua_pushnil(L);
    lua_setglobal(L, "loadfile");
    for (int i = 0; i < 4; ++i) {
      lua_newtable(L);
      lua_rawsetp(L, LUA_REGISTRYINDEX, &markers[i]);
    }
    lua_newtable(L);
    lua_pushliteral(L, "authored native error");
    lua_setfield(L, -2, "__metatable");
    lua_pushcfunction(L, authored_error_text);
    lua_setfield(L, -2, "__tostring");
    lua_rawsetp(L, LUA_REGISTRYINDEX, &authored_error_tag);
    lua_newtable(L);
    constexpr const char *names[] = {
        "request", "call",          "context",       "edit",   "originals",
        "restore", "append",        "decode",        "encode", "display",
        "array",   "present",       "restarting",    "stats",  "manage",
        "inspect", "decode_packet", "encode_packet", "format"};
    for (int i = 0; i < 19; ++i) {
      lua_pushlightuserdata(L, self);
      lua_pushinteger(L, i);
      lua_pushcclosure(L, bridge, 2);
      lua_setfield(L, -2, names[i]);
    }
    lua_pushlightuserdata(L, self);
    lua_pushcclosure(L, authored, 1);
    lua_setfield(L, -2, "emit");
    lua_pushlightuserdata(L, self);
    lua_pushcclosure(L, module_load, 1);
    lua_setfield(L, -2, "_module_load");
    lua_pushlightuserdata(L, self);
    lua_pushcclosure(L, module_call, 1);
    lua_setfield(L, -2, "_module_call");
    lua_newtable(L);
    lua_getfield(L, -2, "decode");
    lua_setfield(L, -2, "decode");
    lua_getfield(L, -2, "encode");
    lua_setfield(L, -2, "encode");
    lua_setfield(L, -2, "json");
    lua_newtable(L);
    lua_getfield(L, -2, "decode_packet");
    lua_setfield(L, -2, "decode");
    lua_getfield(L, -2, "encode_packet");
    lua_setfield(L, -2, "encode");
    lua_setfield(L, -2, "binary");
    lua_pushvalue(L, -1);
    lua_setglobal(L, "blackbird");
    lua_setglobal(L, "arco"); // Compatibility alias for retained programs.
    constexpr const char *tool_api =
        "function blackbird.decide(args) return blackbird.call('decision_model',args) "
        "end "
        "function blackbird.colleague(q) return blackbird.call('colleague',q) end "
        "blackbird.participants={} "
        "for _,n in "
        "ipairs({'start','read','send','cancel','await','join','archive','configure'}) "
        "do "
        "blackbird.participants[n]=function(q) return "
        "blackbird.call('participant_'..n,q or {}) end end "
        "blackbird.process={} "
        "for _,op in "
        "ipairs({'read','foreground','background','input','resize','signal','stop','"
        "archive','configure'}) do "
        "local operation=op; blackbird.process[operation]=function(q) q=q or {}; "
        "q.op=operation; return blackbird.call('process',q) end end "
        "blackbird.tasks={} "
        "function blackbird.tasks.read(q) return blackbird.call('tasks_read',{query=q "
        "or {}}) end "
        "function blackbird.tasks.edit(q) return blackbird.call('tasks_edit',q) end "
        "function blackbird.define_tool(d) return blackbird.call('tool_define',"
        "{definition=d}) end "
        "do local cache,loading={},{}; local load_module,call_module="
        "blackbird._module_load,blackbird._module_call; "
        "blackbird._module_load=nil;blackbird._module_call=nil; "
        "function blackbird.module(name) "
        "if cache[name]~=nil then return cache[name] end "
        "assert(not loading[name], 'cyclic module import: '..name); "
        "local r=blackbird.call('module_source',{name=name}); "
        "assert(r.source, 'module unavailable: '..name); "
        "local env=setmetatable({}, {__index=_G}); "
        "local fn=assert(load_module(r.source, '@module:'..name..':'..r.revision, "
        "env)); "
        "loading[name]=true; local ok,value=call_module(fn); loading[name]=nil; "
        "if not ok then error(value) end; "
        "assert(value~=nil, 'module must return a value'); "
        "cache[name]=value; return value end end";
    if (self->load_and_call(L, tool_api, "tool-api", SourceKind::tool_api, 0) != LUA_OK)
      return lua_error(L);

    constexpr const char *print_program =
        "function print(...) local v={} for i=1,select('#',...) do "
        "v[i]=tostring(select(i,...)) end "
        "blackbird.display(table.concat(v,'\\t')..'\\n') end";
    if (self->load_and_call(L, print_program, "print-program", SourceKind::print, 0) !=
        LUA_OK)
      return lua_error(L);
    return 0;
  }
  static int bridge(lua_State *L) {
    auto *self = static_cast<Runtime *>(lua_touserdata(L, lua_upvalueindex(1)));
    const auto op = static_cast<Op>(lua_tointeger(L, lua_upvalueindex(2)));
    if (op == array_op) {
      luaL_checktype(L, 1, LUA_TTABLE);
      lua_pushvalue(L, 1);
      tag(L, ValueTag::array);
      return 1;
    }
    if (!lua_checkstack(L, 1024))
      return luaL_error(L, "Lua traversal stack exhausted");
    self->failure.reset();
    bool failed = false;
    {
      try {
        std::size_t nodes = 0;
        switch (op) {
        case packet_decode_op:
          self->scratch = unwrap(decode_packet_value(read_value(L, 1, {0, &nodes})));
          break;
        case packet_encode_op:
          self->scratch =
              Value{unwrap(encode_packet_string(read_value(L, 1, {0, &nodes})))};
          break;
        case format_op:
          self->scratch = Value{unwrap(format_value(read_value(L, 1, {0, &nodes})))};
          break;
        case stats_op:
          self->scratch = self->engine.stats();
          break;
        case restarting_op:
          self->scratch = Value{self->engine.restart_note_.has_value()};
          break;
        case request_op:
          self->scratch = self->engine.request(lua_isnoneornil(L, 1)
                                                   ? Value::object({})
                                                   : read_value(L, 1, {0, &nodes}));
          break;
        case call_op: {
          auto name = lua_string(L, 1);
          auto args = read_value(L, 2, {0, &nodes});
          self->scratch = self->engine.call(std::move(name), std::move(args));
          break;
        }
        case manage_op:
          self->scratch = self->engine.context_.manage(read_value(L, 1, {0, &nodes}));
          break;
        case inspect_op:
          self->scratch = self->engine.context_.inspect(read_value(L, 1, {0, &nodes}));
          break;
        case context_op:

          self->scratch = self->engine.context_.view();
          break;
        case edit_op:
          self->scratch = self->engine.context_.edit(read_value(L, 1, {0, &nodes}));
          break;
        case originals_op:
          self->scratch = self->engine.context_.originals();
          break;
        case restore_op:
          self->engine.context_.restore(lua_string(L, 1));
          self->scratch = self->engine.context_.view();
          break;
        case append_op: {
          auto items = read_value(L, 1, {0, &nodes});
          self->engine.context_.append(items.array(), "lua.synthetic");
          self->scratch = self->engine.context_.view();
          break;
        }
        case decode_op:
          self->scratch = unwrap(parse_json(lua_string(L, 1)));
          break;
        case encode_op:
          self->scratch = Value{unwrap(dump_json(read_value(L, 1, {0, &nodes})))};
          break;
        case display_op: {
          auto s = lua_string(L, 1);
          self->engine.log_.original({"display", s, Value{}});
          if (self->engine.display)
            self->engine.display(s);
          self->scratch = Value{};
          break;
        }
        case present_op:
          self->engine.present(read_value(L, 1, {0, &nodes}));
          self->scratch = Value{};
          break;
        case array_op:
          break;
        }
      } catch (const Error &e) {
        self->failed(e, error_name(e.code));
        failed = true;
      } catch (const std::bad_alloc &) {
        self->failed({ErrorCode::allocation}, "allocation");
        failed = true;
      } catch (const std::exception &e) {
        self->failed({ErrorCode::external_unknown}, e.what());
        failed = true;
      }
    }
    // No owning automatic C++ objects live across Lua allocation/error jumps.
    if (failed) {
      lua_pushstring(L, self->bridge_error.data());
      return lua_error(L);
    }
    push_value(L, self->scratch);
    return 1;
  }
  static void interrupt_hook(lua_State *L, lua_Debug *) {
    auto *self = *static_cast<Runtime **>(lua_getextraspace(L));
    try {
      self->engine.poll_commands();
    } catch (const Error &error) {
      self->failure = error;
    } catch (...) {
      self->failure = Error{ErrorCode::external_unknown};
    }
    if (self->failure)
      luaL_error(L, "Command owner failed");
    if (self->engine.cancelled && self->engine.cancelled()) {
      self->failure = Error{ErrorCode::interrupted};
      luaL_error(L, "Turn interrupted");
    }
  }
  bool valid_module_source(std::string_view source) {
    const int base = lua_gettop(state);
    try {
      const bool valid = validation_load(source, "arco-module-candidate") == LUA_OK;
      lua_settop(state, base);
      return valid;
    } catch (...) {
      lua_settop(state, base);
      throw;
    }
  }
  bool valid_tool_source(std::string_view source) {
    const int base = lua_gettop(state);
    const auto wrapped = "return function(args)\n" + std::string{source} + "\nend";
    try {
      const bool valid = validation_load(wrapped, "arco-tool-definition") == LUA_OK;
      lua_settop(state, base);
      return valid;
    } catch (...) {
      lua_settop(state, base);
      throw;
    }
  }
  Value invoke_tool(const Value &definition, const Value &arguments) {
    return eval("local fn = function(args)\n" + string_field(definition, "source") +
                    "\nend\nreturn fn(_native_args)",
                &arguments, SourceKind::tool);
  }
  Value eval(std::string_view source, const Value *arguments = nullptr,
             SourceKind kind = SourceKind::root) {
    const int base = lua_gettop(state);
    bool installed_arguments = false;
    auto restore_arguments = [&] {
      if (installed_arguments) {
        lua_pushvalue(state, base + 1);
        lua_setglobal(state, "_native_args");
        installed_arguments = false;
      }
    };
    try {
      if (arguments) {
        if (!lua_checkstack(state, 1024))
          throw Error{ErrorCode::allocation};
        lua_getglobal(state, "_native_args");
        push_value(state, *arguments);
        lua_setglobal(state, "_native_args");
        installed_arguments = true;
      }
      if (load_and_call(state, source, "arco-program", kind, 1) != LUA_OK) {
        const auto native_error = authored_error(state, -1);
        const char *message = lua_tostring(state, -1);
        error = native_error.has_value() ? error_name(native_error->code)
                                         : (message == nullptr ? "Lua error" : message);
        const auto lost = error.size() > 512 ? error.size() - 512 : 0;
        error.resize(std::min<std::size_t>(error.size(), 512));
        RetainedState::MaintenanceScope maintenance{engine.log_.root()};
        engine.log_.original(
            {"lua.error", error,
             Value::object(
                 {{"generation", Value{hex_identity(engine.generation_.bytes())}},
                  {"unretained_bytes", Value{Number{lost}}}})});
        throw native_error.value_or(
            failure.value_or(Error{ErrorCode::external_unknown}));
      }
      if (!lua_checkstack(state, 1024))
        throw Error{ErrorCode::allocation};
      std::size_t nodes = 0;
      restore_arguments();
      auto result = read_value(state, -1, {0, &nodes});
      lua_settop(state, base);
      return result;
    } catch (...) {
      restore_arguments();
      lua_settop(state, base);
      throw;
    }
  }
};
namespace {
// Intentionally small supported schema: a flat object of scalar JSON types.
// Unsupported schema keywords reject rather than pretending to enforce them.
void validate_tool_schema(const Value &schema) {
  if (string_field(schema, "type") != "object")
    throw Error{ErrorCode::invalid_range};
  for (const auto &[k, v] : schema.object()) {
    (void)v;
    if (k != "type" && k != "properties" && k != "required" &&
        k != "additionalProperties")
      throw Error{ErrorCode::invalid_range};
  }
  const auto &properties = field(schema, "properties").object();
  if (properties.size() > 64)
    throw Error{ErrorCode::capacity};
  for (const auto &[key, property] : properties) {
    if (key.empty() || key.size() > 128)
      throw Error{ErrorCode::invalid_range};
    const auto &type = string_field(property, "type");
    if (type != "string" && type != "number" && type != "integer" &&
        type != "boolean" && type != "null")
      throw Error{ErrorCode::invalid_range};
    for (const auto &[k, v] : property.object()) {
      if (k != "type" && k != "description")
        throw Error{ErrorCode::invalid_range};
      if (k == "description" && v.string().size() > 4096)
        throw Error{ErrorCode::invalid_range};
    }
  }
  std::set<std::string> required;
  for (const auto &key : field(schema, "required").array()) {
    if (!field(schema, "properties").find(key.string()) ||
        !required.insert(key.string()).second)
      throw Error{ErrorCode::invalid_range};
  }
  if (std::get<bool>(field(schema, "additionalProperties").value()))
    throw Error{ErrorCode::invalid_range};
}
struct ToolValidation {
  const Value &schema;
  const Value &arguments;
};
void validate_tool_arguments(ToolValidation input) {
  const auto &schema = input.schema;
  const auto &args = input.arguments;
  for (const auto &key : field(schema, "required").array())
    if (!args.find(key.string()))
      throw Error{ErrorCode::invalid_range};
  for (const auto &[key, value] : args.object()) {
    const auto *property = field(schema, "properties").find(key);
    if (!property)
      throw Error{ErrorCode::invalid_range};
    const auto &type = string_field(*property, "type");
    bool ok =
        (type == "string" && std::holds_alternative<std::string>(value.value())) ||
        (type == "boolean" && std::holds_alternative<bool>(value.value())) ||
        (type == "null" && value == Value{}) ||
        (type == "number" && std::holds_alternative<Number>(value.value()));
    if (type == "integer" && std::holds_alternative<Number>(value.value())) {
      const auto &text = value.number().text();
      ok = text.find_first_of(".eE") == std::string::npos;
    }
    if (!ok)
      throw Error{ErrorCode::invalid_range};
  }
}
Value default_context_budget() {

  return Value::object(
      {{"enabled", Value{false}},
       {"trigger_bytes", Value{Number{"262144"}}},
       {"target_bytes", Value{Number{"131072"}}},
       {"reason", Value{"opt-in; byte policy is not a provider token limit"}}});
}
std::uint64_t command_number(const Value &value, std::string_view key,
                             std::uint64_t limit) {
  const auto *item = value.find(key);
  if (!item || !std::holds_alternative<Number>(item->value()))
    throw Error{ErrorCode::invalid_range};
  const auto text = item->number().text();
  std::uint64_t number = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
      number > limit)
    throw Error{ErrorCode::invalid_range};
  return number;
}
std::size_t budget_number(const Value &value, std::string_view key) {
  const auto &text = field(value, key).number().text();
  std::size_t n = 0;
  auto parsed = std::from_chars(text.data(), text.data() + text.size(), n);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || n == 0 ||
      n > 64 * 1024 * 1024)
    throw Error{ErrorCode::invalid_range};
  return n;
}
void validate_context_budget(const Value &value, RawPolicyFields &fields) {
  if (!std::holds_alternative<Value::Object>(value.value()))
    throw Error{ErrorCode::invalid_range};
  for (const auto &[key, v] : value.object()) {
    (void)v;
    if (key != "enabled" && key != "trigger_bytes" && key != "target_bytes" &&
        key != "reason")
      throw Error{ErrorCode::invalid_range};
  }
  const auto *enabled = std::get_if<bool>(&field(value, "enabled").value());
  if (!enabled)
    throw Error{ErrorCode::invalid_range};
  fields.enabled = *enabled;
  const auto target = budget_number(value, "target_bytes");
  fields.target = target;
  const auto trigger = budget_number(value, "trigger_bytes");
  fields.trigger = trigger;
  if (target >= trigger || string_field(value, "reason").empty() ||
      string_field(value, "reason").size() > 4096)
    throw Error{ErrorCode::invalid_range};
}
} // namespace
Value CodingEngine::program_config(const Value &arguments) {
  if (const auto *proposal = arguments.find("proposal")) {
    if (const auto *base = arguments.find("base");
        base && base->string() != program_revision_)
      throw Error{ErrorCode::conflict};
    for (const auto &[key, value] : proposal->object()) {
      (void)value;
      if (key != "modules" && key != "model" && key != "effort" && key != "workflows" &&
          key != "workflow_prefix")
        throw Error{ErrorCode::invalid_range};
    }
    const auto &model = string_field(*proposal, "model");
    const auto &effort = string_field(*proposal, "effort");
    if (model.size() > 256 ||
        (!effort.empty() && effort != "low" && effort != "medium" && effort != "high" &&
         effort != "xhigh"))
      throw Error{ErrorCode::invalid_range};
    const auto &modules = field(*proposal, "modules").array();
    if (modules.size() > 32 || unwrap(encode_packet_string(*proposal)).size() > 65536)
      throw Error{ErrorCode::capacity};
    std::set<std::string> names;
    for (const auto &module : modules) {
      const auto &name = string_field(module, "name");
      const auto &source = string_field(module, "source");
      if (module.object().size() != 2 || name.empty() || name.size() > 64 ||
          !std::all_of(name.begin(), name.end(),
                       [](char c) {
                         return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                                c == '.';
                       }) ||
          !names.insert(name).second || source.empty() || source.size() > 16384 ||
          !runtime_->valid_module_source(source))
        throw Error{ErrorCode::invalid_range};
    }
    auto candidate_registry =
        std::make_shared<WorkflowRegistry>(*proposal, "candidate");
    for (const auto &d : candidate_registry->definitions().array())
      if (!runtime_->valid_tool_source(string_field(d, "source")))
        throw Error{ErrorCode::invalid_range};
    // Copy/issue before publication; validation never executes candidate code.
    auto next = *proposal;
    auto revision =
        hex_identity(unwrap(log_.root().issue<DefinitionGenerationId>()).bytes());
    pending_workflows_ = std::make_shared<WorkflowRegistry>(next, revision);
    pending_program_config_ = std::move(next);
    pending_program_revision_ = std::move(revision);
  }
  return Value::object(
      {{"revision", Value{program_revision_}},
       {"effective", program_config_},
       {"pending", pending_program_config_},
       {"launch_defaults",
        Value::object({{"model", Value{model_}}, {"effort", Value{effort_}}})},
       {"tools", tool_registry()},
       {"request_defaults",
        Value::object(
            {{"model", Value{string_field(program_config_, "model").empty()
                                 ? model_
                                 : string_field(program_config_, "model")}},
             {"effort", Value{string_field(program_config_, "effort").empty()
                                  ? effort_
                                  : string_field(program_config_, "effort")}}})},
       {"context_policy", budget_view(string_field(program_config_, "model").empty()
                                          ? model_
                                          : string_field(program_config_, "model"),
                                      0)},
       {"governing_workflow",
        Value::object(
            {{"generation", Value{hex_identity(generation_.bytes())}},
             {"source", Value{"retained program.source; selected workflow file is read "
                              "at turn admission"}},
             {"selection",
              Value{"--workflow or /workflow; no staged selector in this API"}}})},
       {"pending_revision", pending_program_config_ == Value{}
                                ? Value{}
                                : Value{pending_program_revision_}},
       {"staged", Value{arguments.find("proposal") != nullptr}},
       {"activation", Value{"successful-workflow-boundary"}}});
}
Value CodingEngine::module_source(const Value &arguments) {
  const auto &name = string_field(arguments, "name");
  for (const auto &module : field(program_config_, "modules").array())
    if (string_field(module, "name") == name)
      return Value::object({{"name", Value{name}},
                            {"source", field(module, "source")},
                            {"revision", Value{program_revision_}}});
  throw Error{ErrorCode::invalid_range};
}
Value CodingEngine::tool_registry() const {
  return Value::object(
      {{"revision", Value{tools_revision_}},
       {"effective", lua_tools_},
       {"pending", pending_lua_tools_},
       {"pending_revision",
        pending_lua_tools_ == Value{} ? Value{} : Value{pending_tools_revision_}}});
}
Value CodingEngine::request_tools() const {
  auto result = tool_definitions();
  for (const auto &d : lua_tools_.array())
    result.array().push_back(Value::object({{"type", Value{"function"}},
                                            {"name", field(d, "name")},
                                            {"description", field(d, "description")},
                                            {"parameters", field(d, "parameters")}}));
  return result;
}
Value CodingEngine::define_tool(const Value &arguments) {
  if (const auto *base = arguments.find("base");
      base && base->string() != tools_revision_)
    throw Error{ErrorCode::conflict};
  const auto &definition = field(arguments, "definition");
  for (const auto &[k, v] : definition.object()) {
    (void)v;
    if (k != "name" && k != "description" && k != "parameters" && k != "source")
      throw Error{ErrorCode::invalid_range};
  }
  const auto &name = string_field(definition, "name");
  if (name.empty() || name.size() > 64 || name == "provider" ||
      !std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-';
      }))
    throw Error{ErrorCode::invalid_range};
  const auto native_definitions = tool_definitions();
  for (const auto &d : native_definitions.array())
    if (string_field(d, "name") == name)
      throw Error{ErrorCode::conflict};
  const auto &description = string_field(definition, "description");
  const auto &source = string_field(definition, "source");
  if (description.empty() || description.size() > 4096 || source.empty() ||
      source.size() > 16384)
    throw Error{ErrorCode::invalid_range};
  validate_tool_schema(field(definition, "parameters"));
  if (!runtime_->valid_tool_source(source))
    throw Error{ErrorCode::invalid_range};
  auto next = pending_lua_tools_ == Value{} ? lua_tools_ : pending_lua_tools_;
  auto found =
      std::find_if(next.array().begin(), next.array().end(),
                   [&](const Value &d) { return string_field(d, "name") == name; });
  if (found == next.array().end())
    next.array().push_back(definition);
  else
    *found = definition;
  if (next.array().size() > 32 || unwrap(encode_packet_string(next)).size() > 65536)
    throw Error{ErrorCode::capacity};
  auto revision =
      hex_identity(unwrap(log_.root().issue<DefinitionGenerationId>()).bytes());
  pending_lua_tools_ = std::move(next);
  pending_tools_revision_ = std::move(revision);
  auto result = tool_registry();
  result.object().emplace_back("staged", Value{true});
  return result;
}
Value CodingEngine::budget_view(std::string_view model, std::size_t input_bytes) const {
  RawPolicyTrace trace{2, budget_revision_, 119,
                       turn_trace_ ? std::optional{turn_trace_->reference()}
                                   : std::nullopt};
  return budget_view(model, input_bytes, trace);
}
Value CodingEngine::budget_view(std::string_view model, std::size_t input_bytes,
                                RawPolicyTrace &trace) const {
  return trace.run([&] {
    const bool enabled = std::get<bool>(field(budget_, "enabled").value());
    std::optional<std::size_t> trigger;
    if (enabled)
      trigger = budget_number(budget_, "trigger_bytes");
    const bool triggered = trigger && input_bytes >= *trigger;
    trace.observation(enabled, trigger, input_bytes, triggered);
    auto result = Value::object(
        {{"revision", Value{budget_revision_}},
         {"effective", budget_},
         {"pending", pending_budget_},
         {"pending_revision",
          pending_budget_ == Value{} ? Value{} : Value{pending_budget_revision_}},
         {"triggered", Value{triggered}},
         {"input_bytes", Value{Number{input_bytes}}},
         {"units",
          Value{"native binary input packet bytes; not tokens or total wire bytes"}},
         {"capability",
          Value::object(
              {{"provider", Value{std::string{provider_.provider_identity()}}},
               {"provider_source",
                Value{"native adapter identity; unavailable for unspecified adapters"}},
               {"model", Value{std::string{model}}},
               {"model_source",
                Value{"effective request model; stats uses engine default"}},
               {"context_window_tokens", Value{}},
               {"context_window_source",
                Value{"unavailable; no qualified model capability catalog"}}})}});
    trace.finish(0);
    return result;
  });
}
Value CodingEngine::context_budget(const Value &arguments) {
  const bool proposal_present = arguments.find("proposal") != nullptr;
  RawPolicyTrace trace{proposal_present ? std::uint8_t{1} : std::uint8_t{0},
                       budget_revision_, proposal_present ? 318U : 170U,
                       turn_trace_ ? std::optional{turn_trace_->reference()}
                                   : std::nullopt};
  return trace.run([&] {
    if (!proposal_present) {
      RawPolicyTrace view{2, budget_revision_, 119, trace.reference(), trace.bank()};
      auto result = budget_view(
          string_field(program_config_, "model").empty()
              ? model_
              : string_field(program_config_, "model"),
          unwrap(encode_packet_string(Value{context_.items({view.reference()})}))
              .size(),
          view);
      trace.finish(0);
      return result;
    }
    auto proposal = field(arguments, "proposal");
    const auto *base = proposal.find("base");
    if (base && base->string() != budget_revision_) {
      auto result = Value::object(
          {{"staged", Value{false}}, {"reason", Value{"stale-policy-revision"}}});
      trace.finish(1);
      return result;
    }
    if (base) {
      auto &fields = proposal.object();
      fields.erase(std::remove_if(fields.begin(), fields.end(),
                                  [](const auto &f) { return f.first == "base"; }),
                   fields.end());
    }
    RawPolicyFields parsed;
    try {
      validate_context_budget(proposal, parsed);
    } catch (...) {
      trace.validation(parsed);
      throw;
    }
    trace.validation(parsed);
    auto revision =
        hex_identity(unwrap(log_.root().issue<DefinitionGenerationId>()).bytes());
    RawPolicyTrace view{2, budget_revision_, 119, trace.reference(), trace.bank()};
    pending_budget_ = std::move(proposal);
    pending_budget_revision_ = std::move(revision);
    trace.staged(pending_budget_revision_);
    auto result = budget_view(
        string_field(program_config_, "model").empty()
            ? model_
            : string_field(program_config_, "model"),
        unwrap(encode_packet_string(Value{context_.items({view.reference()})})).size(),
        view);
    result.object().emplace_back("staged", Value{true});
    trace.finish(0);
    return result;
  });
}
CodingEngine::CodingEngine(AuditLog &log, ContextStore &context,
                           CodingProvider &provider, std::string model,
                           DecisionModelConfig decisions)
    : log_(log), context_(context), tasks_(log), provider_(provider),
      model_(std::move(model)), decision_models_(std::move(decisions)),
      identity_(session_identity(log)),
      generation_(unwrap(log.root().issue<DefinitionGenerationId>())) {
  command_jobs_ = std::make_shared<CommandJobs>();
  observations_ = std::make_unique<Observations>(log_);
  program_config_.object().emplace_back("workflows", default_workflows());
  program_config_.object().emplace_back("workflow_prefix", Value{""});
  program_revision_ = "builtin-ultracode-v1";
  budget_ = default_context_budget();
  for (const auto &packet : log_.root().current_programs()) {
    if (packet.find("label") && string_field(packet, "label") == "session.recovery")
      if (const auto *metadata = packet.find("metadata"))
        if (const auto *uncontained = metadata->find("uncontained_exec"))
          uncontained_exec_recovery_ = *uncontained == Value{true};
    const auto *label = packet.find("label");
    if (label && label->string() == "workflow-config-effective-v1") {
      if (const auto *config = packet.find("program_config")) {
        program_config_ = *config;
        program_revision_ = string_field(packet, "program_revision");
      }
      lua_tools_ = field(packet, "tools");
      tools_revision_ = string_field(packet, "tools_revision");
    }
    if (!label || (label->string() != "context-budget-effective-v1" &&
                   label->string() != "workflow-config-effective-v1"))

      continue;
    auto policy = field(packet, "policy");
    const auto &revision = string_field(packet, "revision");
    RawPolicyTrace restore{3, revision, 119};
    restore.run([&] {
      RawPolicyFields parsed;
      try {
        validate_context_budget(policy, parsed);
      } catch (...) {
        restore.validation(parsed);
        throw;
      }
      restore.validation(parsed);
      budget_ = std::move(policy);
      budget_revision_ = revision;
      restore.finish(0);
    });
  }
  Value participants_saved;
  for (const auto &packet : log_.root().current_programs())
    if (string_field(packet, "label") == "participants-state-v1")
      participants_saved = packet;
  participants_ = std::make_unique<Participants>(
      participants_saved,
      [&](const OriginalCapture &capture) {
        return log_.original(log_.issue(), capture);
      },
      [&](const Value &snapshot) {
        const auto identity = log_.issue();
        log_.record(identity, ApplicationChannel::program, snapshot);
        return std::optional{identity};
      },
      [&](const Value &activity) {
        if (task_activity)
          task_activity(activity);
      });
  workflows_ = std::make_shared<WorkflowRegistry>(program_config_, program_revision_);
  if (context_.head().empty())
    context_.append({}, "initial");
}
CodingEngine::~CodingEngine() { command_jobs_->shutdown(); }
void CodingEngine::shutdown_commands() {
  command_jobs_->shutdown();
  std::optional<Error> error;
  for (unsigned round = 0; round < 4 && command_jobs_->debt(); ++round) {
    try {
      poll_commands();
    } catch (const Error &failure) {
      error = failure;
    }
  }
  if (error)
    throw *error;
}
void CodingEngine::poll_commands() {
  if (pumping_commands_)
    return;
  pumping_commands_ = true;
  struct Reset {
    bool &flag;
    ~Reset() { flag = false; }
  } reset{pumping_commands_};
  try {
    if (!command_failure_)
      for (const auto &request : command_jobs_->requests())
        (void)call("process", request);
    for (unsigned drain_round = 0; drain_round < 4; ++drain_round) {
      auto events = command_jobs_->drain();
      for (auto &event : events) {
        if (event.bytes) {
          if (command_failure_) {
            continue;
          }
          auto preserve_credit = raw_command_claim(154);
          const auto id_size = event.job_id.size(), byte_size = event.bytes->size();
          const auto preserve_request =
              raw_command_linked(2011, event.original,
                                 std::array{detail::raw_native_field(id_size),
                                            detail::raw_string_field(event.job_id),
                                            detail::raw_native_field(event.offset),
                                            detail::raw_native_field(byte_size)},
                                 preserve_credit ? &*preserve_credit : nullptr);
          const auto semantic_before = log_.root().cursor();
          try {
            if (command_jobs_->retained_end(event.job_id) != event.offset)
              throw Error{ErrorCode::conflict};
            log_.original(
                {"process.output", *event.bytes,
                 Value::object({{"attempt", Value{event.job_id}},
                                {"byte_start", Value{Number{event.offset}}}})});
            command_jobs_->retained(event.job_id, event.offset + event.bytes->size());
            const auto retained_end = event.offset + event.bytes->size();
            const std::uint8_t branch = 1;
            const Error error{};
            (void)raw_command_linked(2012, preserve_request,
                                     std::array{detail::raw_native_field(retained_end),
                                                detail::raw_native_field(branch),
                                                detail::raw_native_field(error.code),
                                                detail::raw_native_field(error.detail)},
                                     preserve_credit ? &*preserve_credit : nullptr);
            if (process_output) {
              try {
                if (string_field(command_jobs_->foreground(), "job_id") == event.job_id)
                  process_output(*event.bytes);
                else
                  process_output("\n[command " + event.job_id + "]\n" + *event.bytes);
              } catch (...) {
              } // Presentation cannot undo successfully retained bytes.
            }
          } catch (const Error &error) {
            const std::uint8_t branch = 2;
            const auto retained_end = event.offset;
            (void)raw_command_linked(2012, preserve_request,
                                     std::array{detail::raw_native_field(retained_end),
                                                detail::raw_native_field(branch),
                                                detail::raw_native_field(error.code),
                                                detail::raw_native_field(error.detail)},
                                     preserve_credit ? &*preserve_credit : nullptr);
            if (error.code == ErrorCode::interrupted &&
                log_.root().state() == JournalWriterState::live &&
                log_.root().cursor().journal == semantic_before.journal &&
                log_.root().cursor().sequence == semantic_before.sequence &&
                log_.root().cursor().end_offset == semantic_before.end_offset)
              throw;
            if (diagnostic)
              diagnostic(
                  "command capture failed: " + std::string{error_name(error.code)} +
                  ":" + std::to_string(error.detail) + "\n");
            command_failure_ = error;
            capacity_stopped_ = error.code == ErrorCode::capacity;
            command_jobs_->shutdown();
            // The job still owns the unacknowledged exact original. No drop.
            command_jobs_->capture_failed_all();
          } catch (...) {
            const std::uint8_t branch = 3;
            const Error error{ErrorCode::allocation};
            const auto retained_end = event.offset;
            (void)raw_command_linked(2012, preserve_request,
                                     std::array{detail::raw_native_field(retained_end),
                                                detail::raw_native_field(branch),
                                                detail::raw_native_field(error.code),
                                                detail::raw_native_field(error.detail)},
                                     preserve_credit ? &*preserve_credit : nullptr);
            command_jobs_->shutdown();
            command_jobs_->capture_failed_all();
            throw error;
          }
          continue;
        }
        if (command_failure_) {
          for (auto &[key, value] : event.result.object()) {
            if (key == "effect_outcome")
              value = Value{"unknown"};
            if (key == "capture_complete")
              value = Value{false};
          }
        }
        event.result.object().emplace_back(
            "retained_bytes", Value{Number{command_jobs_->retained_end(event.job_id)}});
        const auto encoded = unwrap(encode_packet(event.result));
        const auto disposition =
            string_field(event.result, "effect_outcome") == "unknown"
                ? AttemptDisposition::unknown
            : event.result.find("exit_code") &&
                    field(event.result, "exit_code") == Value{Number{0}}
                ? AttemptDisposition::success
                : AttemptDisposition::failure;
        command_jobs_->settlement(event, [&](RawRetainedJobLoan *loan) {
          const auto submitted = unwrap(log_.root().submit_settlement(
              {{},
               AttemptObservationEvent{parse_id<OperationAttemptId>(event.job_id),
                                       AttemptPhase::terminal,
                                       disposition,
                                       {encoded.begin(), encoded.end()}}},
              loan ? loan->acknowledgment() : std::span<PhysicalJournalRecord>{},
              loan ? loan->acknowledged_flag() : nullptr));
          if (submitted.evidence == RetainedEvidence::uncertain ||
              submitted.evidence == RetainedEvidence::recovered_pending)
            throw Error{ErrorCode::audit_unavailable};
        });
        if (const auto task = command_tasks_.find(event.job_id);
            task != command_tasks_.end()) {
          auto accepted = command_tasks_.extract(task);
          const auto branch =
              disposition == AttemptDisposition::success   ? std::uint8_t{1}
              : disposition == AttemptDisposition::failure ? std::uint8_t{2}
                                                           : std::uint8_t{3};
          accepted.mapped().emit(branch, event.result.find("exit_code"));
        }
        if (process_output && event.result.find("exit_code") &&
            string_field(command_jobs_->foreground(), "job_id") == event.job_id)
          try {
            process_output("\n[exit " +
                           field(event.result, "exit_code").number().text() + "]\n");
          } catch (...) {
          }
      }
      if (!command_failure_)
        break;
    }
    if (settlement_ && !turn_running_ && !operation_depth_ && !command_jobs_->debt())
      settlement_.reset();
  } catch (const Error &error) {
    if (error.code == ErrorCode::interrupted)
      throw;
    command_jobs_->stop_all();
    command_failure_ = error;
    if (error.code != ErrorCode::capacity)
      log_.root().block_admission();
    throw;
  }
  if (command_failure_)
    throw *command_failure_;
}
void CodingEngine::poll_participants() { participants_->drain(); }
void CodingEngine::shutdown_participants() {
  participants_->shutdown();
  if ((!cancelled || !cancelled()) && log_.root().state() == JournalWriterState::live)
    participants_->drain();
}
void recover_coding_session(RetainedState &root, RecoveryMode mode) {
  struct CodingRecovery final : CustodyVerifier {
    struct Abandoned {
      OperationAttemptId attempt;
      std::string operation;
      bool local;
      bool uncontained;
    };
    RetainedState &root;
    RecoveryMode mode;
    std::vector<Abandoned> abandoned;
    CodingRecovery(RetainedState &value, RecoveryMode selected)
        : root(value), mode(selected) {}
    Result<void> verify(std::span<const AttemptState> attempts) override {
      for (const auto &attempt : attempts) {
        const auto recovered_decision = root.decision(attempt.admission.decision);
        if (!recovered_decision.has_value())
          return Result<void>::failure(recovered_decision.error());
        const auto *decision = &recovered_decision.value();
        const auto metadata = read_packet(decision->continuation);
        if (!metadata.has_value())
          return Result<void>::failure({ErrorCode::external_unknown});
        const auto *operation = metadata.value().find("operation");
        if (!operation || !std::holds_alternative<std::string>(operation->value()))
          return Result<void>::failure({ErrorCode::external_unknown});
        const auto &name = operation->string();
        const bool provider =
            name == "provider" || name == "colleague" || name == "participant_start" ||
            name == "participant_send" || name == "participant_read" ||
            name == "participant_cancel" || name == "participant_await" ||
            name == "participant_join" || name == "participant_archive" ||
            name == "participant_configure";
        const bool local = name == "exec" || name == "process" || name == "read_file" ||
                           name == "write_file" || name == "edit_file" ||
                           name == "lua" || name == "workflow_execute" ||
                           name == "workflow_invoke";
        if (!provider && (!local || mode != RecoveryMode::acknowledge_local_unknowns))
          return Result<void>::failure({ErrorCode::external_unknown});
        const auto *input = metadata.value().find("input");
        const auto *generation = metadata.value().find("generation");
        const auto *revision = metadata.value().find("revision");
        const std::string_view admitted_input{
            reinterpret_cast<const char *>(attempt.admission.input.data()),
            attempt.admission.input.size()};
        const auto recovered_invocation = root.invocation(attempt.admission.invocation);
        if (!recovered_invocation.has_value())
          return Result<void>::failure(recovered_invocation.error());
        const auto &invocation = recovered_invocation.value();
        const bool linked =
            invocation.decision == decision->decision &&
            invocation.definition == decision->definition &&
            invocation.input == attempt.admission.input &&
            decision->planned_invocations.size() == 1 &&
            decision->planned_invocations.front() == invocation.invocation;
        // New decisions bind the already-retained invocation rather than embedding
        // another full input. Older decisions keep their explicit input check.
        bool input_bound = false;
        if (const auto *binding = metadata.value().find("input_binding")) {
          const auto *invocation_id = metadata.value().find("invocation");
          const auto parsed_input = decode_packet_string(admitted_input);
          input_bound =
              parsed_input.has_value() &&
              std::holds_alternative<Value::Object>(parsed_input.value().value()) &&
              !input && *binding == Value{"invocation-v1"} && invocation_id &&
              *invocation_id == Value{hex_identity(invocation.invocation.bytes())};
        } else if (input) {
          const auto encoded_input = encode_packet_string(*input);
          input_bound = std::holds_alternative<Value::Object>(input->value()) &&
                        encoded_input.has_value() &&
                        encoded_input.value() == admitted_input;
        }
        if (!linked || !input_bound || !generation || !revision ||
            !std::holds_alternative<std::string>(generation->value()) ||
            !std::holds_alternative<std::string>(revision->value()) ||
            generation->string() != hex_identity(decision->definition.bytes()) ||
            revision->string() != hex_identity(decision->context.bytes()))
          return Result<void>::failure({ErrorCode::external_unknown});
        abandoned.push_back({attempt.admission.attempt, name, local,
                             attempt.opened && (name == "exec" || name == "process")});
      }
      return Result<void>::success();
    }
  } custody{root, mode};
  unwrap(root.reconcile(custody));
  try {
    std::vector<RetainedEvent> observations;
    Value::Array uncontained_attempts;
    for (const auto &abandoned : custody.abandoned) {
      if (abandoned.local) {
        const auto observation = unwrap(encode_packet(Value::object(
            {{"recovery", Value{"acknowledge-local-unknowns"}},
             {"attempt", Value{hex_identity(abandoned.attempt.bytes())}},
             {"operation", Value{abandoned.operation}},
             {"outcome", Value{"unknown"}},
             {"uncontained_exec", Value{abandoned.uncontained}},
             {"message", Value{"Prior process operation abandoned; outcome unknown. "
                               "No effect replayed, PID adopted or process exit "
                               "or containment established."}}})));
        observations.push_back(
            {{},
             AttemptObservationEvent{abandoned.attempt, AttemptPhase::terminal,
                                     AttemptDisposition::unknown, observation}});
        if (abandoned.uncontained)
          uncontained_attempts.emplace_back(hex_identity(abandoned.attempt.bytes()));
        continue;
      }
      const std::string message = "Provider or participant operation abandoned during "
                                  "prior process; outcome unknown. "
                                  "No response accepted and no request replayed.";
      const auto bytes = std::as_bytes(std::span{message.data(), message.size()});
      observations.push_back({{},
                              AttemptObservationEvent{abandoned.attempt,
                                                      AttemptPhase::terminal,
                                                      AttemptDisposition::unknown,
                                                      {bytes.begin(), bytes.end()}}});
    }
    if (!uncontained_attempts.empty()) {
      const auto marker = unwrap(encode_packet(Value::object(
          {{"label", Value{"session.recovery"}},
           {"metadata",
            Value::object({{"mode", Value{"acknowledge-local-unknowns"}},
                           {"uncontained_exec", Value{true}},
                           {"attempts", Value{std::move(uncontained_attempts)}}})},
           {"message", Value{"Prior process custody unavailable; outcome unknown. "
                             "No PID adopted or effect replayed."}}})));
      observations.push_back(
          {{},
           ApplicationRecordEvent{unwrap(root.issue<ApplicationRecordId>()),
                                  ApplicationChannel::program, marker}});
    }
    if (!observations.empty())
      (void)unwrap(root.append(root.cursor(), {}, observations));
  } catch (...) {
    root.block_admission();
    throw;
  }
}

void CodingEngine::protect_workflow(const Value::Array &entries, const Value &proposal,
                                    std::optional<RawEventRef> parent) {
  if (capacity_stopped_)
    throw Error{ErrorCode::capacity};
  try {
    if (!parent && turn_trace_)
      parent = turn_trace_->reference();
    const RawContextOrigin origin{parent.value_or(RawEventRef{})};
    auto credit = context_.cancellation_budget(entries, proposal, origin);
    const auto existing =
        context_.cancellation_budget(field(context_.view(origin), "entries").array(),
                                     context_.pending_proposal(origin), origin);
    credit.max_file_bytes = std::max(credit.max_file_bytes, existing.max_file_bytes);
    credit.max_records = std::max(credit.max_records, existing.max_records);
    // One future admission plus each outstanding nested attempt. Error sources,
    // issuers, receipt and terminal are bounded per slot. Depth is limited below.
    credit.max_file_bytes += (operation_depth_ + 1 + command_jobs_->debt()) * 8192;
    credit.max_records += (operation_depth_ + 1 + command_jobs_->debt()) * 16;
    unwrap(log_.root().refresh_settlement(credit));
  } catch (const Error &e) {
    if (e.code == ErrorCode::capacity)
      capacity_stopped_ = true;
    throw;
  }
}
Value CodingEngine::operation(
    std::string_view name, const Value &input,
    const std::function<Value(OperationAttemptId, InvocationId, DefinitionGenerationId,
                              CommandTaskActivity *, RawRetainedExec *)> &body,
    RawProviderOperationContext *provider_operation) {
  auto &root = log_.root();
  RawOperationTrace trace{root};
  std::optional<RawEventRef> parent;
  if (operation_trace_ && operation_trace_->reference().ordinal)
    parent = operation_trace_->reference();
  else if (provider_operation && provider_operation->parent &&
           provider_operation->parent->ordinal)
    parent = provider_operation->parent;
  else if (turn_trace_ && turn_trace_->reference().ordinal)
    parent = turn_trace_->reference();
  trace.start(parent, generation_, operation_depth_, pumping_commands_,
              capacity_stopped_);
  auto *previous_trace = operation_trace_;
  operation_trace_ = &trace;
  struct RestoreOperation {
    RawOperationTrace *&selection;
    RawOperationTrace *previous;
    ~RestoreOperation() { selection = previous; }
  } restore_operation{operation_trace_, previous_trace};
  return trace.run([&]() -> Value {
    trace.stage(1);
    trace.name(name);
    poll_commands();
    std::string task_id;
    if (const auto *binding = input.find("task_id"); name == "exec" && binding) {
      task_id = binding->string();
      if (!tasks_.state().item(task_id))
        throw Error{ErrorCode::invalid_range};
    }
    if (trace.callback(0, !pumping_commands_ && static_cast<bool>(cancelled),
                       [&] { return cancelled(); }))
      throw Error{ErrorCode::interrupted};
    trace.callback(1, static_cast<bool>(effect_policy),
                   [&] { effect_policy(name, input); });
    if (capacity_stopped_ || operation_depth_ >= 16) {
      capacity_stopped_ = true;
      throw Error{ErrorCode::capacity};
    }
    ++operation_depth_;
    struct Depth {
      std::size_t &value;
      ~Depth() { --value; }
    } depth{operation_depth_};
    if (log_.root().protected_settlement())
      protect_workflow(field(context_.view(), "entries").array(),
                       context_.pending_proposal());
    const auto began = operation_completed || operation_outcome
                           ? std::chrono::steady_clock::now()
                           : std::chrono::steady_clock::time_point{};
    trace.callback(2, static_cast<bool>(operation_started),
                   [&] { operation_started(name); });
    trace.callback(3, static_cast<bool>(status), [&] {
      std::string description{name};
      for (const auto key : {"path", "command", "argv"}) {
        const auto *value = input.find(key);
        if (value) {
          description += " · " + unwrap(format_value(*value));
          break;
        }
      }
      status(description);
    });
    trace.stage(2);
    std::optional<RawProviderPhase> provider_result;
    if (provider_operation)
      provider_result.emplace(provider_link_credit);
    auto encode_original = [&](const Value &value, RawOperationOriginal role,
                               RawCapacityClaim *data,
                               std::optional<RawEventRef> provider = {}) {
      const ValueLimits limits{};
      const auto native_role = static_cast<std::uint8_t>(role);
      const std::array request{raw_native(native_role), raw_native(limits.bytes),
                               raw_native(limits.nodes), raw_native(limits.depth)};
      trace.event(2419, request);
      auto encoder_return = [&](RawOperationBranch branch, const Error *error,
                                const std::size_t *length) {
        std::array<ByteView, 4> fields{
            raw_native(native_role), raw_native(branch), {}, {}};
        std::size_t n = 2;
        if (error) {
          fields[n++] = raw_native(error->code);
          fields[n++] = raw_native(error->detail);
        }
        if (length)
          fields[n++] = raw_native(*length);
        trace.event(2418, std::span{fields}.first(n));
      };
      auto encoded = [&] {
        try {
          return unwrap(encode_packet(value));
        } catch (const Error &error) {
          encoder_return(RawOperationBranch::error, &error, nullptr);
          throw;
        } catch (const std::bad_alloc &) {
          encoder_return(RawOperationBranch::allocation, nullptr, nullptr);
          throw;
        } catch (const std::length_error &) {
          encoder_return(RawOperationBranch::length, nullptr, nullptr);
          throw;
        } catch (const std::exception &) {
          encoder_return(RawOperationBranch::standard, nullptr, nullptr);
          throw;
        } catch (...) {
          encoder_return(RawOperationBranch::nonstandard, nullptr, nullptr);
          throw;
        }
      }();
      const auto original = trace.original(role, encoded, data, provider);
      if (provider_operation && provider)
        provider_operation->result = original;
      trace.source(role, original);
      const auto length = encoded.size();
      encoder_return(RawOperationBranch::returned, nullptr, &length);
      return encoded;
    };
    const auto issue = [&]<class T>() {
      RawRegionPrepaidScope paid{trace.retained_bank()};
      return unwrap(root.issue<T>());
    };
    RawRegion admission{raw_regions::command_admission};
    trace.region({raw_lane_epoch(), admission.begin_ordinal()});
    const auto decision = issue.template operator()<DecisionId>();
    trace.identity(0, decision.bytes());
    const auto invocation = issue.template operator()<InvocationId>();
    trace.identity(1, invocation.bytes());
    const auto admitted_generation = generation_;
    trace.identity(3, admitted_generation.bytes());
    const auto attempt = issue.template operator()<OperationAttemptId>();
    trace.identity(2, attempt.bytes());
    const auto exec_id = name == "exec" ? hex_identity(attempt.bytes()) : std::string{};
    std::optional<RawCapacityClaim> input_data, result_data;
    if (trace.enabled())
      input_data.emplace(unwrap(raw_claim_data(49 + ValueLimits{}.bytes)));
    auto encoded_input = encode_original(input, RawOperationOriginal::input,
                                         input_data ? &*input_data : nullptr);
    input_data.reset();
    const ImmutableBytes bytes{std::move(encoded_input)};
    if (trace.enabled())
      result_data.emplace(unwrap(raw_claim_data(49 + ValueLimits{}.bytes)));
    const auto context = parse_id<ContextRevisionId>(context_.head());
    trace.identity(4, context.bytes());
    const auto meta = Value::object(
        {{"operation", Value{std::string{name}}},
         {"input_binding", Value{"invocation-v1"}},
         {"input_encoding", Value{"bbm2"}},
         {"invocation", Value{hex_identity(invocation.bytes())}},
         {"revision", Value{context_.head()}},
         {"generation", Value{hex_identity(admitted_generation.bytes())}}});
    const auto continuation_bytes = unwrap(encode_packet(meta));
    CommandTaskMap staged_tasks;
    CommandTaskMap::node_type task_node;
    if (!task_id.empty()) {
      staged_tasks.try_emplace(exec_id, task_activity, std::move(task_id), attempt,
                               exec_id, std::string{name});
      task_node = staged_tasks.extract(exec_id);
    }
    struct TaskRun {
      CommandTaskActivity *activity;
      bool transferred = false;
      ~TaskRun() {
        if (activity && !transferred)
          activity->emit(3);
      }
    } task_run{task_node.empty() ? nullptr : &task_node.mapped()};
    if (task_run.activity) {
      task_run.activity->pending_node = &task_node;
      task_run.activity->transferred = &task_run.transferred;
    }
    // Fresh identities: one ordered batch, no authoritative partial admission.
    const std::array<RetainedEvent, 3> admitted{
        RetainedEvent{
            {},
            DecisionEvent{decision,
                          identity_.actor,
                          identity_.conversation,
                          identity_.workflow,
                          admitted_generation,
                          context,
                          {invocation},
                          {continuation_bytes.begin(), continuation_bytes.end()}}},
        RetainedEvent{
            {}, InvocationEvent{invocation, decision, admitted_generation, bytes}},
        RetainedEvent{
            {}, AttemptAdmissionEvent{attempt, invocation, decision, bytes, true}}};
    admission.linkage({raw_text(context_.head()), attempt.bytes()});
    trace.stage(3);
    const auto expected_admission = root.cursor();
    const std::uint8_t admission_kind = 0;
    const auto admission_count = admitted.size();
    const std::array admission_request{
        raw_native(admission_kind), ByteView{expected_admission.journal.bytes()},
        raw_native(expected_admission.sequence),
        raw_native(expected_admission.end_offset), raw_native(admission_count)};
    trace.event(2404, admission_request);
    std::optional<RawRetainedExec> exec_admission;
    exec_admission.emplace(root, trace.reference(),
                           name == "exec" ? RawRetainedExec::Kind::selected_exec
                                          : RawRetainedExec::Kind::adapter_receipt);
    try {
      const auto written = [&] {
        RawRegionPrepaidScope paid{trace.retained_bank()};
        return unwrap(exec_admission ? root.append_exec(expected_admission, admitted,
                                                        *exec_admission)
                                     : root.append(expected_admission, {}, admitted));
      }();
      const auto branch = RawOperationBranch::returned;
      const std::array fields{raw_native(admission_kind), raw_native(branch),
                              ByteView{written.journal.bytes()},
                              raw_native(written.sequence),
                              raw_native(written.end_offset)};
      trace.event(2405, fields);
    } catch (const Error &error) {
      const auto branch = RawOperationBranch::error;
      const std::array fields{raw_native(admission_kind), raw_native(branch),
                              raw_native(error.code), raw_native(error.detail)};
      trace.event(2405, fields);
      admission.error(error);
      admission.outcome(RawRegionOutcome::rejected);
      throw;
    } catch (const std::bad_alloc &) {
      const auto branch = RawOperationBranch::allocation;
      const std::array fields{raw_native(admission_kind), raw_native(branch)};
      trace.event(2405, fields);
      admission.outcome(RawRegionOutcome::exception);
      throw;
    } catch (const std::length_error &) {
      const auto branch = RawOperationBranch::length;
      const std::array fields{raw_native(admission_kind), raw_native(branch)};
      trace.event(2405, fields);
      admission.outcome(RawRegionOutcome::exception);
      throw;
    } catch (const std::exception &) {
      const auto branch = RawOperationBranch::standard;
      const std::array fields{raw_native(admission_kind), raw_native(branch)};
      trace.event(2405, fields);
      admission.outcome(RawRegionOutcome::exception);
      throw;
    } catch (...) {
      const auto branch = RawOperationBranch::nonstandard;
      const std::array fields{raw_native(admission_kind), raw_native(branch)};
      trace.event(2405, fields);
      admission.outcome(RawRegionOutcome::exception);
      throw;
    }
    admission.outcome(RawRegionOutcome::admitted);
    admission.finish();
    if (task_run.activity) {
      task_run.activity->bind({raw_lane_epoch(), admission.begin_ordinal()});
      task_run.activity->emit(0);
    }
    if (provider_operation) {
      provider_operation->owner = {
          attempt.bytes(), invocation.bytes(), admitted_generation.bytes(), {}};
      const RawAdapterContext attempt_context{&provider_operation->owner,
                                              provider_operation->parent, nullptr,
                                              &provider_operation->receipt};
      provider_operation->attempt_phase->start(3, &attempt_context);
      provider_operation->attempt_phase->stage(22);
    }
    Value result;
    std::string output;
    bool adapter_capacity = false, timed_out = false, persistent_command = false;
    auto prepare = [&](Value adapter_result, const std::optional<Error> &effect_error) {
      result = effect_error ? error_value(*effect_error) : std::move(adapter_result);
      if (name == "program_config" && effect_error) {
        result.object().emplace_back(
            "message",
            Value{
                effect_error->code == ErrorCode::conflict
                    ? "Registration conflict: duplicate workflow name, alias, bare "
                      "name "
                      "or powerword, built-in command collision, or stale base "
                      "revision. "
                      "No candidate was staged."
                    : "Invalid program configuration: require modules/model/effort and "
                      "optional workflows/workflow_prefix. Workflow definitions "
                      "require "
                      "name, description, source (Lua body with args), aliases, bare "
                      "and "
                      "powerwords. No candidate was staged."});
      }
      if (name == "read_file" && effect_error &&
          effect_error->code == ErrorCode::invalid_range) {
        result.object().emplace_back(
            "message",
            Value{"Use one range object with mode lines or bytes and optional "
                  "start/end; omit range for the whole file. Lines are one-based "
                  "inclusive, bytes zero-based half-open. Do not combine range "
                  "with legacy byte_/line_ fields. Endpoints must be nonnegative "
                  "integers, line start >=1, end >=start."});
        const auto *path = input.find("path");
        result.object().emplace_back(
            "example",
            Value::object({{"path", path ? *path : Value{"file"}},
                           {"range", Value::object({{"mode", Value{"lines"}},
                                                    {"start", Value{Number{"1"}}},
                                                    {"end", Value{Number{"20"}}}})}}));
      }
      if ((name == "tasks_read" || name == "tasks_edit") && effect_error)
        result.object().emplace_back(
            "message",
            Value{
                "Require an object query or edit {op_id,ops}. add/move/archive/list "
                "need "
                "current list base; set/move/archive need the read item version. "
                "Exactly "
                "tasks and one level of subtasks. States "
                "queued/active/blocked/done/dropped. "
                "Read tasks_read for current IDs and versions. Batch limit64, page "
                "limit64; "
                "titles512 bytes, notes2048 bytes. No changes accepted for an invalid "
                "batch."});

      adapter_capacity = name == "process" && effect_error &&
                         effect_error->code == ErrorCode::capacity;
      timed_out = (effect_error && effect_error->code == ErrorCode::io &&
                   effect_error->detail == ETIMEDOUT) ||
                  (name == "exec" && result.find("timed_out") &&
                   field(result, "timed_out") == Value{true});
      persistent_command = name == "exec" && command_jobs_->contains(exec_id);
      if (name == "exec" && !result.find("output_ref")) {
        result.object().emplace_back("output_ref",
                                     Value{hex_identity(attempt.bytes())});
        if (timed_out) {
          result.object().emplace_back("timed_out", Value{true});
          result.object().emplace_back("effect_outcome", Value{"unknown"});
        }
      }
      auto encode = [&] {
        auto encoded = encode_original(result, RawOperationOriginal::primary,
                                       result_data ? &*result_data : nullptr,
                                       provider_result
                                           ? std::optional{provider_result->reference()}
                                           : std::nullopt);
        result_data.reset();
        output.assign(reinterpret_cast<const char *>(encoded.data()), encoded.size());
      };
      if (provider_result) {
        provider_result->run([&] {
          const RawAdapterContext source_context{&provider_operation->owner,
                                                 std::nullopt, nullptr,
                                                 &provider_operation->receipt};
          provider_result->start(9, &source_context);
          provider_result->stage(24);
          if (provider_operation->receipt.response)
            provider_result->link(*provider_operation->receipt.response);
          encode();
        });
      } else
        encode();
    };
    struct Boundary final : EffectBoundary {
      const std::function<Value(OperationAttemptId, InvocationId,
                                DefinitionGenerationId, CommandTaskActivity *,
                                RawRetainedExec *)> &fn;
      decltype(prepare) &prepare_result;
      RawOperationTrace &trace;
      InvocationId invocation;
      DefinitionGenerationId generation;
      CommandTaskActivity *activity;
      RawRetainedExec *raw_exec;
      Value result;
      std::optional<Error> error;
      std::exception_ptr deferred;
      std::optional<RawRegionPrepaidScope> receipt_scope;
      bool dispatched = false;
      Boundary(decltype(fn) body_fn, decltype(prepare) &preparation,
               RawOperationTrace &owner, InvocationId actual_invocation,
               DefinitionGenerationId actual_generation, CommandTaskActivity *task,
               RawRetainedExec *actual_exec)
          : fn(body_fn), prepare_result(preparation), trace(owner),
            invocation(actual_invocation), generation(actual_generation),
            activity(task), raw_exec(actual_exec) {}
      Result<void> dispatch(const EffectIntent &intent) override {
        receipt_scope.reset();
        if (raw_exec)
          raw_exec->suspend();
        trace.stage(4);
        const auto input_bytes = intent.input.size();
        const std::array request{
            ByteView{intent.environment.bytes()}, ByteView{intent.actor.bytes()},
            ByteView{intent.invocation.bytes()}, ByteView{intent.attempt.bytes()},
            raw_native(input_bytes)};
        trace.event(2406, request);
        dispatched = true;
        auto returned = [&](RawOperationBranch branch,
                            const Error *native_error = nullptr) {
          const bool failed = native_error != nullptr;
          std::array<ByteView, 4> fields{
              raw_native(branch), raw_native(failed), {}, {}};
          std::size_t n = 2;
          if (native_error) {
            fields[n++] = raw_native(native_error->code);
            fields[n++] = raw_native(native_error->detail);
          }
          trace.event(2407, std::span{fields}.first(n));
        };
        RawOperationBranch actual_branch = RawOperationBranch::returned;
        std::optional<Error> native_error;
        try {
          result = fn(intent.attempt, invocation, generation, activity, raw_exec);
        } catch (const Error &e) {
          error = e;
          native_error = e;
          actual_branch = RawOperationBranch::error;
        } catch (const std::bad_alloc &) {
          error = Error{ErrorCode::allocation};
          actual_branch = RawOperationBranch::allocation;
        } catch (const std::length_error &) {
          error = Error{ErrorCode::external_unknown};
          actual_branch = RawOperationBranch::length;
        } catch (const std::exception &) {
          error = Error{ErrorCode::external_unknown};
          actual_branch = RawOperationBranch::standard;
        } catch (...) {
          error = Error{ErrorCode::external_unknown};
          deferred = std::current_exception();
          actual_branch = RawOperationBranch::nonstandard;
        }
        returned(actual_branch, native_error ? &*native_error : nullptr);
        if (raw_exec)
          raw_exec->resume();
        trace.stage(5);
        if (!deferred) {
          try {
            prepare_result(std::move(result), error);
          } catch (...) {
            deferred = std::current_exception();
          }
        }
        // The real adapter receipt preserves the actual effect Result even if the
        // existing result preparation failed; no post-effect DATA admission occurs.
        receipt_scope.emplace(trace.retained_bank());
        return error ? Result<void>::failure(*error) : Result<void>::success();
      }
    } boundary{body,
               prepare,
               trace,
               invocation,
               admitted_generation,
               task_run.activity,
               exec_admission ? &*exec_admission : nullptr};
    auto settle = [&](const RetainedEvent &event, bool settlement, bool fallback) {
      const auto &observation = std::get<AttemptObservationEvent>(event.body);
      const auto observation_bytes = observation.observation.size();
      const std::array request{raw_native(settlement),
                               raw_native(fallback),
                               ByteView{attempt.bytes()},
                               raw_native(observation.phase),
                               raw_native(observation.disposition),
                               raw_native(observation_bytes)};
      trace.event(2413, request);
      const auto recorded = [&] {
        RawRegionPrepaidScope paid{trace.retained_bank()};
        if (exec_admission)
          exec_admission->enter_terminal(trace.reference());
        return settlement ? root.submit_settlement(event) : root.submit(event);
      }();
      if (recorded.has_value()) {
        const auto branch = RawOperationBranch::returned;
        const auto &actual = recorded.value();
        const std::array fields{raw_native(branch), raw_native(actual.existing),
                                ByteView{actual.record.journal.bytes()},
                                raw_native(actual.record.sequence),
                                raw_native(actual.evidence)};
        trace.event(2414, fields);
      } else {
        const auto branch = RawOperationBranch::error;
        const std::array fields{raw_native(branch), raw_native(recorded.error().code),
                                raw_native(recorded.error().detail)};
        trace.event(2414, fields);
      }
      return recorded;
    };
    auto interrupted_terminal = [&](std::string_view message) {
      const auto bytes = std::as_bytes(std::span{message.data(), message.size()});
      const RetainedEvent event{{},
                                AttemptObservationEvent{attempt,
                                                        AttemptPhase::terminal,
                                                        AttemptDisposition::unknown,
                                                        {bytes.begin(), bytes.end()}}};
      (void)unwrap(settle(event, true, false));
    };
    const auto report = [&] {
      struct EndExecScope {
        RawRetainedExec *loan;
        ~EndExecScope() {
          if (loan)
            loan->leave();
        }
      } end{exec_admission ? &*exec_admission : nullptr};
      if (exec_admission && exec_admission->full_exec())
        exec_admission->enter(trace.reference(), RawRetainedOrigin::Input::ordinary);
      struct ClearReceiptScope {
        Boundary &owner;
        ~ClearReceiptScope() { owner.receipt_scope.reset(); }
      } clear{boundary};
      boundary.receipt_scope.emplace(trace.retained_bank());
      const std::array dispatch_request{ByteView{attempt.bytes()}};
      trace.event(2420, dispatch_request);
      const auto returned = [&](RawOperationBranch branch,
                                const Error *error = nullptr) {
        const bool failed = error != nullptr;
        std::array<ByteView, 4> fields{raw_native(branch), raw_native(failed), {}, {}};
        std::size_t count = 2;
        if (error) {
          fields[count++] = raw_native(error->code);
          fields[count++] = raw_native(error->detail);
        }
        trace.event(2421, std::span{fields}.first(count));
      };
      const auto dispatched = [&] {
        try {
          return root.dispatch(attempt, boundary,
                               exec_admission ? &*exec_admission : nullptr);
        } catch (const Error &error) {
          returned(RawOperationBranch::error, &error);
          throw;
        } catch (const std::bad_alloc &) {
          returned(RawOperationBranch::allocation);
          throw;
        } catch (const std::length_error &) {
          returned(RawOperationBranch::length);
          throw;
        } catch (const std::exception &) {
          returned(RawOperationBranch::standard);
          throw;
        } catch (...) {
          returned(RawOperationBranch::nonstandard);
          throw;
        }
      }();
      returned(RawOperationBranch::returned,
               dispatched.has_value() ? nullptr : &dispatched.error());
      if (!dispatched.has_value() && name == "exec" && !boundary.dispatched &&
          dispatched.error().code == ErrorCode::interrupted) {
        interrupted_terminal(
            "exec interrupted before adapter dispatch; no effect replayed");
      }
      auto actual = unwrap(dispatched);
      const bool effect_ok = actual.effect.has_value(),
                 recording_ok = actual.recording.has_value();
      std::array<ByteView, 7> fields{raw_native(actual.dispatched),
                                     raw_native(effect_ok),
                                     raw_native(recording_ok),
                                     {},
                                     {},
                                     {},
                                     {}};
      std::size_t n = 3;
      if (!effect_ok) {
        fields[n++] = raw_native(actual.effect.error().code);
        fields[n++] = raw_native(actual.effect.error().detail);
      }
      if (!recording_ok) {
        fields[n++] = raw_native(actual.recording.error().code);
        fields[n++] = raw_native(actual.recording.error().detail);
      }
      trace.event(2408, std::span{fields}.first(n));
      return actual;
    }();
    trace.stage(6);
    unwrap(report.recording);
    if (boundary.deferred)
      std::rethrow_exception(boundary.deferred);
    if (!report.dispatched)
      throw Error{ErrorCode::conflict};
    if (name == "exec" && boundary.error &&
        boundary.error->code == ErrorCode::interrupted &&
        !command_jobs_->contains(exec_id)) {
      interrupted_terminal("exec adapter interrupted before command registration; "
                           "outcome unknown; no replay");
      throw *boundary.error;
    }
    trace.stage(7);
    auto retain = [&](std::uint8_t kind, OriginalCapture capture) {
      const auto identity = issue.template operator()<ApplicationRecordId>();
      const auto expected = root.cursor();
      const std::array request{raw_native(kind), ByteView{identity.bytes()},
                               ByteView{expected.journal.bytes()},
                               raw_native(expected.sequence),
                               raw_native(expected.end_offset)};
      trace.event(2410, request);
      std::optional<SourceReference> source;
      try {
        RawRegionPrepaidScope paid{trace.retained_bank()};
        source = log_.original(identity, std::move(capture));
      } catch (const Error &error) {
        const auto branch = RawOperationBranch::error;
        const std::array fields{raw_native(kind), raw_native(branch),
                                raw_native(error.code), raw_native(error.detail)};
        trace.event(2411, fields);
        throw;
      } catch (const std::bad_alloc &) {
        const auto branch = RawOperationBranch::allocation;
        const std::array fields{raw_native(kind), raw_native(branch)};
        trace.event(2411, fields);
        throw;
      } catch (const std::length_error &) {
        const auto branch = RawOperationBranch::length;
        const std::array fields{raw_native(kind), raw_native(branch)};
        trace.event(2411, fields);
        throw;
      } catch (const std::exception &) {
        const auto branch = RawOperationBranch::standard;
        const std::array fields{raw_native(kind), raw_native(branch)};
        trace.event(2411, fields);
        throw;
      } catch (...) {
        const auto branch = RawOperationBranch::nonstandard;
        const std::array fields{raw_native(kind), raw_native(branch)};
        trace.event(2411, fields);
        throw;
      }
      const auto branch = RawOperationBranch::returned;
      if (!source)
        throw Error{ErrorCode::incomplete};
      const std::array fields{raw_native(kind), raw_native(branch),
                              ByteView{source->journal.bytes()},
                              raw_native(source->sequence)};
      trace.event(2411, fields);
      return identity;
    };
    std::string result_id;
    std::size_t result_record = 0;
    try {
      const auto identity =
          retain(0, {"operation.result", output,
                     Value::object({{"attempt", Value{hex_identity(attempt.bytes())}},
                                    {"source_encoding", Value{"bbm2"}},
                                    {"operation", Value{std::string{name}}}})});
      result_id = hex_identity(identity.bytes());
      result_record = root.fact_count() - 1;
    } catch (const Error &e) {
      if (e.code != ErrorCode::capacity)
        throw;
      capacity_stopped_ = true;
      unretained_bytes_ += output.size();
      boundary.error = e;
      adapter_capacity = false;
      if (command_jobs_->active()) {
        command_failure_ = e;
        const auto registry = command_jobs_->read(Value::object({}));
        for (const auto &job : field(registry, "jobs").array())
          if (string_field(job, "state") != "completed")
            command_jobs_->capture_failed(string_field(job, "job_id"));
        command_jobs_->shutdown();
      }
      result = error_value(e);
      auto fallback = encode_original(result, RawOperationOriginal::fallback, nullptr);
      output.assign(reinterpret_cast<const char *>(fallback.data()), fallback.size());
    }
    if (boundary.error && boundary.error->code == ErrorCode::capacity &&
        !adapter_capacity) {
      capacity_stopped_ = true;
      RetainedState::MaintenanceScope maintenance{root};
      constexpr std::string_view message =
          "effect outcome unknown; no replay; earlier originals retained, rejected "
          "bytes not retained";
      const auto original = trace.original(RawOperationOriginal::capacity_message,
                                           raw_text(message), nullptr);
      trace.source(RawOperationOriginal::capacity_message, original);
      (void)retain(
          1, {"capacity.stop", message,
              Value::object({{"attempt", Value{hex_identity(attempt.bytes())}},
                             {"unretained_bytes", Value{Number{unretained_bytes_}}}})});
    }
    // The result original is already retained. Settlement carries its locator,
    // not another complete response. On capture refusal retain the bounded error.
    const auto observed = unwrap(encode_packet(
        result_id.empty()
            ? result
            : Value::object({{"result_id", Value{result_id}},
                             {"result_record", Value{Number{result_record}}}})));
    auto disposition =
        boundary.error ? AttemptDisposition::failure : AttemptDisposition::success;
    if (boundary.error && !adapter_capacity &&
        (boundary.error->code == ErrorCode::capacity || name == "provider" ||
         name == "exec" || name == "beads" || name == "colleague" ||
         name == "decision_model") &&
        (boundary.error->code == ErrorCode::capacity ||
         boundary.error->code == ErrorCode::interrupted ||
         boundary.error->code == ErrorCode::io ||
         boundary.error->code == ErrorCode::incomplete ||
         boundary.error->code == ErrorCode::allocation ||
         boundary.error->code == ErrorCode::external_unknown ||
         boundary.error->code == ErrorCode::provider_transport))
      disposition = AttemptDisposition::unknown;
    if (boundary.error && name == "beads" && beads_.mutation_may_have_started())
      disposition = AttemptDisposition::unknown;
    if (!boundary.error && name == "exec" && result.find("exit_code") &&
        field(result, "exit_code").number().text() != "0")
      disposition = AttemptDisposition::failure;
    if (!boundary.error && persistent_command && result.find("effect_outcome") &&
        string_field(result, "effect_outcome") == "unknown")
      disposition = AttemptDisposition::unknown;
    if (!boundary.error && name == "beads") {
      const auto state = string_field(result, "status");
      disposition = state == "unknown" ? AttemptDisposition::unknown
                    : (state == "ok" || state == "configured" || state == "cached")
                        ? AttemptDisposition::success
                        : AttemptDisposition::failure;
    }
    if (!boundary.error && name == "colleague") {
      const auto state = string_field(result, "status");
      const auto remote = string_field(result, "remote_disposition");
      disposition = remote == "unknown"    ? AttemptDisposition::unknown
                    : state == "completed" ? AttemptDisposition::success
                                           : AttemptDisposition::failure;
    }
    trace.disposition(disposition);
    const bool has_error = boundary.error.has_value();
    std::array<ByteView, 7> disposition_fields{raw_native(disposition),
                                               raw_native(timed_out),
                                               raw_native(persistent_command),
                                               raw_native(adapter_capacity),
                                               raw_native(has_error),
                                               {},
                                               {}};
    std::size_t disposition_count = 5;
    if (boundary.error) {
      disposition_fields[disposition_count++] = raw_native(boundary.error->code);
      disposition_fields[disposition_count++] = raw_native(boundary.error->detail);
    }
    trace.event(2412, std::span{disposition_fields}.first(disposition_count));
    trace.stage(8);

    RetainedEvent terminal{{},
                           AttemptObservationEvent{attempt,
                                                   AttemptPhase::terminal,
                                                   disposition,
                                                   {observed.begin(), observed.end()}}};
    if (!persistent_command) {
      struct EndTerminalScope {
        RawRetainedExec *loan;
        ~EndTerminalScope() {
          if (loan)
            loan->leave();
        }
      } end{exec_admission ? &*exec_admission : nullptr};
      auto recorded = settle(terminal, boundary.error.has_value(), false);
      if (!recorded.has_value() && recorded.error().code == ErrorCode::capacity) {
        capacity_stopped_ = true;
        boundary.error = recorded.error();
        result = error_value(*boundary.error);
        const std::string bounded =
            "capacity stop after dispatch; outcome unknown; no replay";
        const auto bounded_raw =
            std::as_bytes(std::span{bounded.data(), bounded.size()});
        disposition = AttemptDisposition::unknown;
        trace.disposition(disposition);
        recorded =
            settle({{},
                    AttemptObservationEvent{attempt,
                                            AttemptPhase::terminal,
                                            disposition,
                                            {bounded_raw.begin(), bounded_raw.end()}}},
                   true, true);
      }
      (void)unwrap(std::move(recorded));
    }
    if (task_run.activity && !task_run.transferred) {
      auto branch = disposition == AttemptDisposition::unknown   ? std::uint8_t{3}
                    : disposition == AttemptDisposition::failure ? std::uint8_t{2}
                                                                 : std::uint8_t{1};
      const Value *task_exit = nullptr;
      if (name == "exec" && !boundary.error && branch == 2)
        task_exit = result.find("exit_code");
      task_run.activity->emit(branch, task_exit);
    }
    if (operation_completed || operation_outcome) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - began)
                               .count();
      const auto outcome =
          persistent_command && !boundary.error && !result.find("exit_code")
              ? "backgrounded"
          : timed_out && name == "exec"                ? "timed out; outcome unknown"
          : disposition == AttemptDisposition::success ? "completed"
          : disposition == AttemptDisposition::unknown ? "stopped; outcome unknown"
          : boundary.error && boundary.error->code == ErrorCode::interrupted
              ? "interrupted (recorded failure)"
              : "failed";
      const auto completion =
          std::string{name} + " " + outcome + " · " + std::to_string(elapsed) + "ms";
      trace.callback(4, static_cast<bool>(operation_outcome),
                     [&] { operation_outcome(completion, disposition); });
      trace.callback(5, static_cast<bool>(operation_completed),
                     [&] { operation_completed(completion); });
    }
    trace.stage(9);
    trace.callback(
        6, boundary.error && name != "provider" && static_cast<bool>(process_output),
        [&] {
          process_output("\n[" + std::string{name} + ": " +
                         error_name(boundary.error->code) + "]\n");
        });
    if (command_failure_ && boundary.error &&
        boundary.error->code == ErrorCode::capacity) {
      try {
        poll_commands();
      } catch (const Error &error) {
        if (error.code != ErrorCode::capacity)
          throw;
      }
    }
    if (boundary.error &&
        ((boundary.error->code == ErrorCode::capacity && !adapter_capacity) ||
         boundary.error->code == ErrorCode::interrupted ||
         (name == "provider" || name == "workflow_execute") ||
         root.state() != JournalWriterState::live))
      throw *boundary.error;
    if (name == "beads") {
      const auto *stopped = result.find("interrupted");
      if (stopped && std::get<bool>(stopped->value()))
        throw Error{ErrorCode::interrupted};
    }
    return result;
  });
}

Value CodingEngine::execute_workflow(const WorkflowContinuation &invocation) {
  try {
    if (workflow_depth_ >= 8 || ++workflow_invocations_ > 64)
      throw Error{ErrorCode::capacity};
    return operation(
        "workflow_execute", invocation.arguments,
        [&](OperationAttemptId attempt, InvocationId, DefinitionGenerationId,
            CommandTaskActivity *, RawRetainedExec *) {
          const auto metadata = Value::object(
              {{"name", field(invocation.definition, "name")},
               {"revision", Value{invocation.revision}},
               {"definition", invocation.definition},
               {"invocation", invocation.arguments},
               {"generation", Value{hex_identity(generation_.bytes())}},
               {"attempt", Value{hex_identity(attempt.bytes())}},
               {"selection_attempt", Value{invocation.selection_attempt}}});
          log_.original({"workflow.source",
                         string_field(invocation.definition, "source"), metadata});
          log_.record(ApplicationChannel::program, metadata);
          ++workflow_depth_;
          struct Depth {
            unsigned &n;
            ~Depth() { --n; }
          } depth{workflow_depth_};
          Runtime child{*this};
          return child.invoke_tool(invocation.definition, invocation.arguments);
        });
  } catch (const Error &e) {
    workflow_failure_ = e;
    throw;
  }
}
void CodingEngine::drain_workflows() {
  if (draining_workflows_ || workflow_continuations_.empty())
    return;
  validate_protocol(context_.items());
  draining_workflows_ = true;
  struct Drain {
    bool &flag;
    ~Drain() { flag = false; }
  } drain{draining_workflows_};
  while (!workflow_continuations_.empty()) {
    if (restart_note_)
      throw Error{ErrorCode::busy};
    if (cancelled && cancelled())
      throw Error{ErrorCode::interrupted};
    auto next = std::move(workflow_continuations_.front());
    workflow_continuations_.erase(workflow_continuations_.begin());
    const auto result = execute_workflow(next);
    context_.append(
        {Value::object(
            {{"role", Value{"developer"}},
             {"content",
              Value{"Registered workflow " + string_field(next.definition, "name") +
                    " completed in this turn: " + unwrap(format_value(result))}}})},
        "workflow-continuation");
  }
}
Value CodingEngine::request(Value options) {
  RawProviderPhase assembly{2 * provider_scalar_credit + sizeof(IdentityBytes) +
                            sizeof(std::size_t) + 32};
  return assembly.run([&] {
    assembly.start(1);
    assembly.stage(10);
    RawRegion preparation{raw_regions::provider_prepare};
    if (restart_note_)
      throw Error{ErrorCode::busy};
    assembly.stage(11);
    validate_protocol(context_.items(), assembly.reference());
    assembly.stage(12);
    drain_workflows();
    auto items = context_.items();
    validate_protocol(items, assembly.reference());
    auto request = Value::object(
        {{"model", Value{string_field(program_config_, "model").empty()
                             ? model_
                             : string_field(program_config_, "model")}},
         {"instructions",
          Value{"You are Blackbird, a programmable coding colleague for an expert "
                "operator. Use local file/process tools without approval ceremony. "
                "Context is ordinary editable data: inspect it, transform it with Lua, "
                "repair it from originals. A timed-out tool returns a result: inspect "
                "its "
                "retained output and choose a new attempt or another approach; never "
                "blindly repeat an effect with unknown outcome. Keep tool call/result "
                "linkage valid. Governing "
                "Lua workflow changes activate next turn. For compiled changes, build "
                "build/release/blackbird and request restart with a note listing "
                "changes, "
                "checks and next steps; the same session will resume with continue. "
                "Bound hardening before starting: list concrete remediation tasks, "
                "direct checks and a time/resource allowance. Layer one: remediate "
                "and fix. Layer two: one recheck and fix its findings within that "
                "allowance. Never a third assurance layer: no reviews of reviews, "
                "certifying rechecks, unrelated full-suite reruns or silent budget "
                "resets. Settled checks stay settled absent relevant change or new "
                "defect evidence. If blocked at the bound, retain the unsafe candidate "
                "inactive and continue independent useful work; do not call it safe. "
                "Work carefully and verify "
                "actual results. Host cwd: " +
                std::filesystem::current_path().string()}},
         {"reasoning",
          Value::object(
              {{"effort", Value{string_field(program_config_, "effort").empty()
                                    ? effort_
                                    : string_field(program_config_, "effort")}}})},
         {"input", Value{std::move(items)}},
         {"tools", request_tools()},
         {"parallel_tool_calls", Value{false}},
         {"include", Value{Value::Array{Value{"reasoning.encrypted_content"}}}},
         {"store", Value{false}},
         {"stream", Value{true}}});
    assembly.stage(13);
    if (!std::holds_alternative<Value::Object>(options.value()))
      throw Error{ErrorCode::invalid_range};
    unsigned max_attempts = 5;
    unsigned base_ms = 1000;
    unsigned cap_ms = 16000;
    if (const auto *policy = options.find("retry_policy")) {
      if (!std::holds_alternative<Value::Object>(policy->value()))
        throw Error{ErrorCode::invalid_range};
      for (const auto &[key, value] : policy->object()) {
        if (!std::holds_alternative<Number>(value.value()))
          throw Error{ErrorCode::invalid_range};
        unsigned n = 0;
        const auto &text = value.number().text();
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), n);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
          throw Error{ErrorCode::invalid_range};
        if (key == "max_attempts" && n >= 1 && n <= 100)
          max_attempts = n;
        else if (key == "base_ms" && n <= 60000)
          base_ms = n;
        else if (key == "cap_ms" && n <= 60000)
          cap_ms = n;
        else
          throw Error{ErrorCode::invalid_range};
      }
    }
    for (auto &[key, value] : options.object()) {
      if (key == "retry_policy")
        continue;
      auto &fields = request.object();
      auto found = std::find_if(fields.begin(), fields.end(),
                                [&](const auto &f) { return f.first == key; });
      if (found == fields.end())
        fields.emplace_back(key, std::move(value));
      else
        found->second = std::move(value);
    }
    const auto *store = std::get_if<bool>(&field(request, "store").value());
    if (!tasks_.state().empty() || tasks_.state().revision() != 0) {
      auto &instructions =
          std::find_if(request.object().begin(), request.object().end(),
                       [](const auto &f) { return f.first == "instructions"; })
              ->second;
      instructions = Value{
          instructions.string() + "\nCURRENT_TASKS " +
          unwrap(format_value(tasks_.read(Value::object({{"status", Value{"open"}},
                                                         {"limit", Value{Number{"16"}}},
                                                         {"compact", Value{true}}})))) +
          "\nTasks are durable session working state. Read more with tasks_read; use "
          "tasks_edit for small atomic batches at real work transitions. Keep stable "
          "IDs through renames. Exactly tasks and subtasks, no deeper nesting. "
          "Multiple "
          "active tasks are fine. Completion is explicit, including parents. Use item "
          "versions for set, and current list base for structural edits. Preserve "
          "op_id "
          "and arguments for retries; resolve conflicts by reading before a new edit. "
          "Use exec task_id for observed activity; a running command does not itself "
          "complete a task. Unfinished tasks remain across turns and compaction."};
    }
    const auto *stream = std::get_if<bool>(&field(request, "stream").value());
    if (store == nullptr || *store || stream == nullptr || !*stream)
      throw Error{ErrorCode::unsupported};
    validate_protocol(field(request, "input").array(), assembly.reference());
    const auto budget =
        budget_view(string_field(request, "model"),
                    unwrap(encode_packet_string(field(request, "input"))).size());
    if (std::get<bool>(field(budget, "triggered").value())) {
      auto &instructions =
          std::find_if(request.object().begin(), request.object().end(),
                       [](const auto &f) { return f.first == "instructions"; })
              ->second;
      instructions = Value{
          instructions.string() + "\nCONTEXT_BUDGET " + unwrap(format_value(budget)) +
          "\nThe configured advisory trigger is reached. Inspect the working map "
          "and propose managed context "
          "with context_manage or Lua toward target_bytes, or revise/defer policy "
          "with context_budget and a reason. "
          "Do not discard live instructions or incomplete tool groups. Destructive "
          "proposals remain model-authored; "
          "native acceptance checks structure, not summary quality. Managed "
          "proposals and policy changes activate only "
          "after successful workflow completion: finish this workflow to use them "
          "on the next turn. "
          "Repair may exceed the target; retrieve bounded originals and restore "
          "when useful. "
          "Neither target nor trigger is a token estimate, hard provider limit, or "
          "audit reclamation."};
    }
    assembly.stage(14);
    ProviderOriginalLease final_packet{{ProviderOriginalRole::request_packet,
                                        ProviderOriginalMode::codec_adopt,
                                        ValueLimits{}.bytes,
                                        {}},
                                       assembly.reference()};
    final_packet.adopt(unwrap(encode_packet_string(request)));
    const auto prepared_request = final_packet.response_body();
    last_request_bytes_ = Value{Number{prepared_request.size()}};
    usage_ = Value{};
    if (diagnostic || display)
      (diagnostic ? diagnostic : display)("\n[request " +
                                          last_request_bytes_.number().text() +
                                          " native request bytes]\n");
    assembly.stage(15);
    const auto origin = context_.head();
    if (origin.size() > 32)
      throw Error{ErrorCode::invalid_range};
    const auto origin_bytes = origin.size();
    const std::array generation_fields{ByteView{generation_.bytes()}};
    assembly.scalar(15, 8, generation_fields);
    const std::array origin_fields{raw_native(origin_bytes), raw_text(origin)};
    assembly.scalar(15, 7, origin_fields);
    if (preparation.enabled()) {
      preparation.linkage({raw_text(origin), {}});
      preparation.observe({RawObservationKind::request_bytes, 0,
                           prepared_request.size(), log_.root().cursor().sequence});
      preparation.outcome(RawRegionOutcome::success);
    }
    preparation.finish();
    assembly.finish(1);
    constexpr std::uint64_t decision_bytes = raw_envelope_size + provider_ref_bytes +
                                             sizeof(unsigned) + provider_ref_bytes +
                                             sizeof(unsigned) + sizeof(bool);
    constexpr std::uint64_t clock_bytes = raw_envelope_size + provider_ref_bytes +
                                          sizeof(clockid_t) + 2 * sizeof(int) +
                                          sizeof(time_t) + sizeof(long);
    constexpr std::uint64_t wait_bytes = provider_phase_credit + 2 * clock_bytes;
    const std::uint64_t retry_extra =
        raw_envelope_size + provider_ref_bytes + sizeof(IdentityBytes) +
        3 * sizeof(unsigned) +
        static_cast<std::uint64_t>(max_attempts) * decision_bytes +
        static_cast<std::uint64_t>(max_attempts - 1) * wait_bytes;
    RawProviderPhase group{retry_extra};
    return group.run([&] {
      const RawAdapterContext group_context{nullptr, assembly.reference(), nullptr,
                                            nullptr};
      group.start(2, &group_context);
      group.stage(22);
      // One live request group; each failed attempt stays independently recorded.
      const auto retry_id = unwrap(log_.root().issue<OperationAttemptId>());
      const auto group_ref = group.reference();
      const std::array policy_fields{
          ByteView{group_ref.lane},   raw_native(group_ref.ordinal),
          ByteView{retry_id.bytes()}, raw_native(max_attempts),
          raw_native(base_ms),        raw_native(cap_ms)};
      group.emit(1325, policy_fields);
      const auto retry_group = hex_identity(retry_id.bytes());
      provider_.cancelled = [this] {
        poll_commands();
        return cancelled && cancelled();
      };
      Value response;
      unsigned delay_ms = std::min(base_ms, cap_ms);
      for (unsigned ordinal = 1;; ++ordinal) {
        RawProviderPhase attempt_phase{2 * provider_link_credit};
        RawProviderOperationContext operation_context;
        operation_context.attempt_phase = &attempt_phase;
        operation_context.parent = group.reference();
        group.stage(20);
        if (cancelled && cancelled())
          throw Error{ErrorCode::interrupted};
        group.stage(21);
        const auto journal = log_.root().journal_usage();
        if (journal.approaching() || journal.extent_error) {
          const auto remaining = journal.remaining_bytes();
          const auto warning =
              "audit headroom: " +
              (remaining ? std::to_string(*remaining) + " bytes"
                         : "bytes unavailable") +
              ", " + std::to_string(journal.remaining_records()) +
              " indexed records remaining; observation only, no handoff reserve; "
              "context compaction does not reclaim audit history";
          if (status)
            status(warning);
          if (notice || display)
            (notice ? notice : display)("\n[" + warning + "]\n");
        }
        ResponsePreview preview;
        previewed_.clear();
        try {
          group.stage(22);
          response = attempt_phase.run([&] {
            return operation(
                "provider", request,
                [&](OperationAttemptId attempt, InvocationId, DefinitionGenerationId,
                    CommandTaskActivity *, RawRetainedExec *) {
                  const auto admission = unwrap(log_.root().attempt(attempt));
                  const auto *instructions = request.find("instructions");
                  if (instructions &&
                      std::holds_alternative<std::string>(instructions->value()))
                    observations_->doctrine(instructions->string(),
                                            admission.admission.invocation, attempt);
                  log_.record(
                      ApplicationChannel::log,
                      Value::object(
                          {{"label", Value{"provider.request"}},
                           {"metadata",
                            Value::object(
                                {{"attempt", Value{hex_identity(attempt.bytes())}},
                                 {"revision", Value{origin}},
                                 {"generation",
                                  Value{hex_identity(generation_.bytes())}},
                                 {"input_binding", Value{"attempt-invocation-v1"}},
                                 {"retry_group", Value{retry_group}},
                                 {"ordinal", Value{Number{ordinal}}}})}}));
                  RawRegion transport{raw_regions::provider_transport, {.cpu = false}};
                  if (transport.enabled())
                    transport.linkage({raw_text(origin), attempt.bytes()});
                  RawProviderPhase receipt{provider_link_credit};
                  RawProviderPhase acceptance{provider_link_credit +
                                              provider_scalar_credit +
                                              sizeof(std::size_t) + 32};
                  StreamCapture capture{[&](std::string_view block) {
                    try {
                      log_.original(
                          {"provider.stream", block,
                           Value::object(
                               {{"attempt", Value{hex_identity(attempt.bytes())}},
                                {"revision", Value{origin}},
                                {"capture_policy", Value{"bounded-blocks-v1"}}})});
                    } catch (const Error &e) {
                      if (e.code == ErrorCode::capacity)
                        unretained_bytes_ += block.size();
                      throw;
                    }
                  }};
                  Value result;
                  try {
                    operation_context.owner.dispatch = {raw_lane_epoch(),
                                                        transport.begin_ordinal()};
                    const auto &owner = operation_context.owner;
                    const RawAdapterContext source_context{
                        &owner, attempt_phase.reference(), nullptr,
                        &operation_context.receipt};
                    attempt_phase.stage(23);
                    result = provider_.respond(
                        request,
                        [&](std::string_view raw) {
                          poll_commands();
                          capture.append(raw);
                          // Preview follows transport delivery, not diagnostic flush
                          // cadence.
                          for (const auto &delta : preview.feed(raw, &source_context)) {
                            RawProviderPhase presentation;
                            presentation.run([&] {
                              presentation.start(8, &source_context);
                              previewed_[delta.item_id] += delta.text;
                              if (display)
                                display(delta.text);
                            });
                          }
                        },
                        &owner, &source_context);
                    receipt.start(9, &source_context);
                    if (operation_context.receipt.response)
                      receipt.link(*operation_context.receipt.response);
                    receipt.finish(1);
                  } catch (const Error &error) {
                    transport.error(error);
                    transport.outcome(RawRegionOutcome::exception);
                    transport.finish();
                    // No destructor I/O and no retry if a prior sink write was
                    // uncertain.
                    capture.flush();
                    throw;
                  } catch (...) {
                    transport.outcome(RawRegionOutcome::exception);
                    transport.finish();
                    capture.flush();
                    throw;
                  }
                  transport.outcome(RawRegionOutcome::returned);
                  transport.finish();
                  attempt_phase.stage(24);
                  capture.flush();
                  return acceptance.run([&] {
                    const RawAdapterContext acceptance_context{
                        &operation_context.owner, attempt_phase.reference(), nullptr,
                        &operation_context.receipt};
                    acceptance.start(10, &acceptance_context);
                    if (operation_context.receipt.response)
                      acceptance.link(*operation_context.receipt.response);
                    acceptance.stage(25);
                    return import_provider_response(std::move(result));
                  });
                },
                &operation_context);
          });
          const bool eligible = false;
          const auto attempt_ref = attempt_phase.reference();
          const std::array decision_fields{ByteView{group_ref.lane},
                                           raw_native(group_ref.ordinal),
                                           raw_native(ordinal),
                                           ByteView{attempt_ref.lane},
                                           raw_native(attempt_ref.ordinal),
                                           raw_native(delay_ms),
                                           raw_native(eligible)};
          group.emit(1326, decision_fields);
          break;
        } catch (const Error &e) {
          const bool transient_http =
              e.code == ErrorCode::external_unknown &&
              (e.detail == 408 || e.detail == 429 || e.detail == 500 ||
               e.detail == 502 || e.detail == 503 || e.detail == 504);
          const bool transient_curl =
              e.code == ErrorCode::provider_transport &&
              (e.detail == 5 || e.detail == 6 || e.detail == 7 || e.detail == 16 ||
               e.detail == 18 || e.detail == 28 || e.detail == 52 || e.detail == 55 ||
               e.detail == 56 || e.detail == 92 || e.detail == 95 || e.detail == 96);
          group.stage(27);
          const bool eligible = ordinal < max_attempts &&
                                (transient_http || transient_curl) &&
                                log_.root().state() == JournalWriterState::live;
          const auto attempt_ref = attempt_phase.reference();
          const std::array decision_fields{ByteView{group_ref.lane},
                                           raw_native(group_ref.ordinal),
                                           raw_native(ordinal),
                                           ByteView{attempt_ref.lane},
                                           raw_native(attempt_ref.ordinal),
                                           raw_native(delay_ms),
                                           raw_native(eligible)};
          // No attempt begin exists if native retained admission itself refused.
          if (attempt_ref.ordinal)
            group.emit(1326, decision_fields);
          if (!eligible)
            throw;
          const auto message =
              "provider attempt " + std::to_string(ordinal) + " stopped (" +
              error_name(e.code) + ":" + std::to_string(e.detail) +
              "); partial output not accepted; retry " + std::to_string(ordinal + 1) +
              "/" + std::to_string(max_attempts) + " in " + std::to_string(delay_ms) +
              "ms";
          log_.original({"provider.retry", message,
                         Value::object({{"retry_group", Value{retry_group}}})});
          if (status)
            status(message);
          if (notice || display)
            (notice ? notice : display)("\n[" + message + "]\n");
          group.stage(28);
          RawProviderPhase wait_phase{group.split(wait_bytes)};
          wait_phase.run([&] {
            const RawAdapterContext wait_context{nullptr, group.reference(), nullptr,
                                                 nullptr};
            wait_phase.start(4, &wait_context);
            auto clocks = wait_phase.split(2 * clock_bytes);
            const auto clock = [&] {
              const int saved_errno = errno;
              const clockid_t method = CLOCK_MONOTONIC;
              timespec value{};
              const int result = ::clock_gettime(method, &value);
              const int native_errno = errno;
              const auto ref = wait_phase.reference();
              const std::array fields{
                  ByteView{ref.lane},       raw_native(ref.ordinal),
                  raw_native(method),       raw_native(result),
                  raw_native(native_errno), raw_native(value.tv_sec),
                  raw_native(value.tv_nsec)};
              emit_provider_fact(1327, fields, clocks ? &*clocks : nullptr);
              errno = saved_errno;
            };
            clock();
            const auto until =
                std::chrono::steady_clock::now() + std::chrono::milliseconds{delay_ms};
            try {
              while (std::chrono::steady_clock::now() < until) {
                if (cancelled && cancelled())
                  throw Error{ErrorCode::interrupted};
                std::this_thread::sleep_for(std::chrono::milliseconds{25});
              }
            } catch (...) {
              clock();
              throw;
            }
            clock();
          });
          delay_ms = std::min(cap_ms, delay_ms * 2);
        }
      }
      group.stage(29);
      if (const auto *usage = response.find("usage");
          usage && std::holds_alternative<Value::Object>(usage->value())) {
        Value::Object selected;
        auto select = [](const Value &source, std::string_view key,
                         Value::Object &dest) {
          const auto *v = source.find(key);
          if (!v || !std::holds_alternative<Number>(v->value()))
            return;
          const auto &n = v->number().text();
          if (!n.empty() && std::all_of(n.begin(), n.end(),
                                        [](char c) { return c >= '0' && c <= '9'; }))
            dest.emplace_back(std::string{key}, *v);
        };
        for (const auto key : {"input_tokens", "output_tokens", "total_tokens"})
          select(*usage, key, selected);
        for (const auto &[usage_group, key] :
             {std::pair{"input_tokens_details", "cached_tokens"},
              std::pair{"output_tokens_details", "reasoning_tokens"}}) {
          if (const auto *details = usage->find(usage_group)) {
            Value::Object kept;
            select(*details, key, kept);
            if (!kept.empty())
              selected.emplace_back(usage_group, Value::object(std::move(kept)));
          }
        }
        if (!selected.empty()) {
          usage_ = Value::object(std::move(selected));
          if (observed_usage)
            observed_usage(usage_);
          if (diagnostic || display)
            (diagnostic ? diagnostic : display)("\n[provider usage " +
                                                unwrap(format_value(usage_)) + "]\n");
        }
      }
      group.stage(30);
      if (context_.head() != origin)
        throw Error{ErrorCode::conflict};
      RawProviderPhase final_acceptance{provider_scalar_credit + sizeof(std::size_t) +
                                        32};
      const RawAdapterContext final_context{nullptr, group.reference(), nullptr,
                                            nullptr};
      final_acceptance.run([&] {
        final_acceptance.start(10, &final_context);
        final_acceptance.stage(31);
        context_.append(field(response, "output").array(), "provider.branch:" + origin);
        const auto &head = context_.head();
        const auto size = head.size();
        if (size > 32)
          throw Error{ErrorCode::invalid_range};
        const std::array fields{raw_native(size), raw_text(head)};
        final_acceptance.scalar(31, 7, fields);
      });
      return response;
    });
  });
}
Value CodingEngine::call(std::string name, Value arguments) {
  if (name == "provider")
    throw Error{ErrorCode::invalid_range};
  return operation(
      name, arguments,
      [&](OperationAttemptId attempt, InvocationId invocation,
          DefinitionGenerationId admitted_generation, CommandTaskActivity *activity,
          RawRetainedExec *raw_exec) {
        if (name == "exec") {
          const auto id = hex_identity(attempt.bytes());
          const auto limits = log_.root().journal_limits();
          if (limits.max_payload < 2048 || limits.max_batch_bytes < 4096)
            throw Error{ErrorCode::capacity};
          auto credit = context_.cancellation_budget(
              field(context_.view(), "entries").array(), context_.pending_proposal());
          credit.max_file_bytes +=
              (operation_depth_ + command_jobs_->debt() + 2) * 8192;
          credit.max_records += (operation_depth_ + command_jobs_->debt() + 2) * 16;
          if (!settlement_)
            settlement_ = unwrap(log_.root().protect_settlement(credit));
          else
            unwrap(log_.root().refresh_settlement(credit));
          JobWaitOptions options;
          if (arguments.find("yield_ms"))
            options.yield_ms = command_number(arguments, "yield_ms", 3600000);
          if (arguments.find("output_max_bytes"))
            options.max_output = static_cast<std::size_t>(
                command_number(arguments, "output_max_bytes", 16 * 1024 * 1024));
          options.stop_on_cancel = true;
          command_jobs_->start(id, arguments, raw_exec);
          if (activity) {
            if (!activity->pending_node || !activity->transferred)
              throw Error{ErrorCode::conflict};
            auto inserted = command_tasks_.insert(std::move(*activity->pending_node));
            if (!inserted.inserted) {
              *activity->pending_node = std::move(inserted.node);
              throw Error{ErrorCode::conflict};
            }
            *activity->transferred = true;
            activity->pending_node = nullptr;
            activity->transferred = nullptr;
          }
          return command_jobs_->wait(
              id, options, [this] { poll_commands(); },
              [this] { return cancelled && cancelled(); });
        }
        if (name == "process") {
          if (string_field(arguments, "op") == "foreground") {
            JobWaitOptions options;
            if (arguments.find("yield_ms"))
              options.yield_ms = command_number(arguments, "yield_ms", 3600000);
            return command_jobs_->wait(
                string_field(arguments, "job_id"), options, [this] { poll_commands(); },
                [this] { return cancelled && cancelled(); });
          }
          return command_jobs_->control(arguments);
        }
        if (name == "decision_model") {
          decision_models_.cancelled = cancelled;
          decision_models_.observer = [&](std::string_view label,
                                          std::string_view raw) {
            log_.original(
                {label, raw,
                 Value::object({{"attempt", Value{hex_identity(attempt.bytes())}}})});
          };
          auto result = decision_models_.evaluate(arguments);
          result.object().emplace_back("audit_ref",
                                       Value{hex_identity(attempt.bytes())});
          return result;
        }
        if (name == "beads") {
          beads_.cancelled = cancelled;
          beads_.observer = [&](std::string_view label, std::string_view raw) {
            log_.original(
                {label, raw,
                 Value::object({{"attempt", Value{hex_identity(attempt.bytes())}}})});
          };
          auto result = beads_.run(arguments);
          result.object().emplace_back("audit_ref",
                                       Value{hex_identity(attempt.bytes())});
          return result;
        }

        if (name == "workflow_registry")
          return workflows_->discover();
        if (name == "workflow_invoke") {
          const auto &definition = workflows_->named(string_field(arguments, "name"));
          WorkflowContinuation continuation{definition, arguments,
                                            workflows_->revision(),
                                            hex_identity(attempt.bytes())};
          const auto selection = Value::object(
              {{"name", field(definition, "name")},
               {"revision", Value{workflows_->revision()}},
               {"definition", definition},
               {"invocation", arguments},
               {"selection_attempt", Value{continuation.selection_attempt}}});
          log_.original(
              {"workflow.selection", string_field(definition, "source"), selection});
          log_.record(ApplicationChannel::program, selection);
          const auto unanswered = ContextStore::stop_outputs(
              field(context_.view(), "entries").array(), false);
          if (!unanswered.empty()) {
            if (workflow_continuations_.size() >= 8)
              throw Error{ErrorCode::capacity};
            workflow_continuations_.push_back(std::move(continuation));
            return Value::object(
                {{"status", Value{"accepted"}},
                 {"name", field(definition, "name")},
                 {"revision", Value{workflows_->revision()}},
                 {"execution",
                  Value{"same-turn after tool outputs, before next request or "
                        "successful boundary"}}});
          }
          return execute_workflow(continuation);
        }
        if (name == "program_config")
          return program_config(arguments);
        if (name == "module_source")
          return module_source(arguments);
        if (name == "tool_define")
          return define_tool(arguments);
        if (name == "tool_registry")
          return tool_registry();
        for (const auto &definition : lua_tools_.array()) {
          if (string_field(definition, "name") != name)
            continue;
          validate_tool_arguments(
              {.schema = field(definition, "parameters"), .arguments = arguments});
          log_.original(
              {"tool.source", string_field(definition, "source"),
               Value::object({{"name", Value{name}},
                              {"revision", Value{tools_revision_}},
                              {"attempt", Value{hex_identity(attempt.bytes())}}})});
          return runtime_->invoke_tool(definition, arguments);
        }
        if (name == "restart") {

          auto note = string_field(arguments, "note");
          if (note.empty() || note.size() > 65536)
            throw Error{ErrorCode::invalid_range};
          restart_note_ = std::move(note);
          return Value::object({{"scheduled", Value{true}}});
        }
        if (name == "read_process_output") {
          const auto &reference = string_field(arguments, "output_ref");
          auto read_credit = raw_command_claim(58);
          const auto reference_size = reference.size();
          const std::uint8_t mode = 2;
          const auto request = raw_command_data(
              2037, std::array{detail::raw_native_field(reference_size),
                               detail::raw_string_field(reference),
                               detail::raw_native_field(mode)});
          bool known = false;
          std::string raw;
          try {
            for (std::size_t ordinal = 0; ordinal < log_.root().fact_count();
                 ++ordinal) {
              const auto fact = unwrap(log_.root().fact(ordinal));
              const auto *record =
                  std::get_if<ApplicationRecordEvent>(&fact.event.body);
              if (!record || record->channel != ApplicationChannel::log)
                continue;
              const auto packet = unwrap(read_packet(record->payload));
              const auto &label = string_field(packet, "label");
              if (label != "operation.result" && label != "process.output")
                continue;
              const auto &metadata = field(packet, "metadata");
              const auto *old_attempt = metadata.find("attempt");
              if (!old_attempt || old_attempt->string() != reference)
                continue;
              if (label == "operation.result") {
                const auto *op = metadata.find("operation");
                if (op && op->string() == "exec")
                  known = true;
              }
              if (label == "process.output") {
                known = true;
                for (const auto dependency : fact.event.dependencies) {
                  auto source_credit = raw_command_claim(96);
                  const auto bytes = unwrap(log_.root().source(dependency));
                  const auto source_size = bytes.size();
                  (void)raw_command_linked(
                      2038, request,
                      std::array{ByteView{fact.record.journal.bytes()},
                                 detail::raw_native_field(fact.record.sequence),
                                 ByteView{dependency.journal.bytes()},
                                 detail::raw_native_field(dependency.sequence),
                                 detail::raw_native_field(source_size)},
                      source_credit ? &*source_credit : nullptr);
                  raw.append(reinterpret_cast<const char *>(bytes.data()),
                             bytes.size());
                }
              }
            }
            if (!known)
              throw Error{ErrorCode::invalid_range};
            // Retrieval supports byte ranges only, never file line semantics.
            if (arguments.find("line_start") || arguments.find("line_end"))
              throw Error{ErrorCode::invalid_range};
            auto result = process_output_presentation(raw, arguments);
            const std::uint8_t branch = 1;
            const Error error{};
            const auto source_size = raw.size();
            (void)raw_command_linked(2039, request,
                                     std::array{detail::raw_native_field(branch),
                                                detail::raw_native_field(error.code),
                                                detail::raw_native_field(error.detail),
                                                detail::raw_native_field(source_size)},
                                     read_credit ? &*read_credit : nullptr);
            return result;
          } catch (const Error &error) {
            const std::uint8_t branch = 2;
            const auto source_size = raw.size();
            (void)raw_command_linked(2039, request,
                                     std::array{detail::raw_native_field(branch),
                                                detail::raw_native_field(error.code),
                                                detail::raw_native_field(error.detail),
                                                detail::raw_native_field(source_size)},
                                     read_credit ? &*read_credit : nullptr);
            throw;
          } catch (...) {
            const std::uint8_t branch = 3;
            const Error error{ErrorCode::allocation};
            const auto source_size = raw.size();
            (void)raw_command_linked(2039, request,
                                     std::array{detail::raw_native_field(branch),
                                                detail::raw_native_field(error.code),
                                                detail::raw_native_field(error.detail),
                                                detail::raw_native_field(source_size)},
                                     read_credit ? &*read_credit : nullptr);
            throw;
          }
        }
        if (name == "context_repair") {
          if (string_field(arguments, "base") != context_.head() ||
              operation_depth_ != 1 || detail::owned_children.load() != 0 ||
              log_.root().state() != JournalWriterState::live)
            throw Error{ErrorCode::conflict};
          auto outputs = ContextStore::stop_outputs(
              field(context_.view(), "entries").array(), false);
          for (auto &output : outputs) {
            const auto &id = string_field(output, "call_id");
            if (id.empty())
              throw Error{ErrorCode::conflict};
            const auto eligible = std::any_of(
                repairable_outputs_.begin(), repairable_outputs_.end(),
                [&](const Value &old) { return string_field(old, "call_id") == id; });
            if (!eligible)
              throw Error{ErrorCode::conflict};
            output = Value::object(
                {{"type", Value{"function_call_output"}},
                 {"call_id", Value{id}},
                 {"output",
                  Value::object(
                      {{"error", Value{"workflow_result_missing"}},
                       {"effect_outcome", Value{"unknown"}},
                       {"replayed", Value{false}},
                       {"detail", Value{"Protocol placeholder only. Inspect retained "
                                        "program, tool results and audit for actual "
                                        "effects; no automatic retry."}}})}});
          }
          auto candidate = context_.items();
          candidate.insert(candidate.end(), outputs.begin(), outputs.end());
          // Reject duplicates/orphan outputs before publishing anything.
          validate_protocol(candidate);
          const auto count = outputs.size();
          if (!outputs.empty())
            context_.append(std::move(outputs), "workflow.protocol-repair");
          return Value::object({{"repaired", Value{Number{count}}},
                                {"base", Value{context_.head()}},
                                {"effect_outcome", Value{"unknown"}},
                                {"replayed", Value{false}}});
        }
        if (name == "context_budget")
          return context_budget(arguments);
        if (name == "context_manage") {
          auto proposal = field(arguments, "proposal");
          // Explicit invocation-time snapshot convenience. Supplied bases remain strict
          // CAS.
          if (std::holds_alternative<Value::Object>(proposal.value()) &&
              !proposal.find("base"))
            proposal.object().emplace_back("base", Value{context_.head()});
          return context_.manage(proposal);
        }
        if (name == "rageshake") {
          const auto &observation = string_field(arguments, "observation");
          if (observation.empty() || observation.size() > 65536)
            throw Error{ErrorCode::invalid_range};
          const auto references = arguments.find("references")
                                      ? field(arguments, "references")
                                      : Value::object({});
          if (!std::holds_alternative<Value::Object>(references.value()) ||
              unwrap(encode_packet_string(references)).size() > 65536)
            throw Error{ErrorCode::invalid_range};
          std::set<std::string> active;
          for (const auto &unresolved : unwrap(log_.root().unresolved_attempts()))
            active.insert(hex_identity(unresolved.admission.attempt.bytes()));
          Value::Array active_ids;
          for (const auto &id : active) {
            if (active_ids.size() == 64)
              break;
            active_ids.push_back(Value{id});
          }
          auto detail = Value::object(
              {{"observation", Value{observation}},
               {"references", references},
               {"active_attempts", Value{std::move(active_ids)}},
               {"active_attempt_count", Value{Number{active.size()}}},
               {"revision", Value{context_.head()}},
               {"generation", Value{hex_identity(generation_.bytes())}},
               {"actor", Value{hex_identity(identity_.actor.bytes())}},
               {"conversation", Value{hex_identity(identity_.conversation.bytes())}},
               {"workflow", Value{hex_identity(identity_.workflow.bytes())}},
               {"model", Value{model_}},
               {"effort", Value{effort_}},
               {"audit_end", Value{Number{log_.root().fact_count()}}},
               {"reporting_attempt", Value{hex_identity(attempt.bytes())}},
               {"repair_required_now", Value{false}}});
          const auto complaint = log_.original(
              {"complaint.detail", unwrap(encode_packet_string(detail)),
               Value::object({{"attempt", Value{hex_identity(attempt.bytes())}}})});
          // Only a non-sensitive locator goes to beads (and hence issue backups).
          // The observation and provider/context history remain in the local audit.
          auto delivery = call(
              "exec",
              Value::object(
                  {{"argv",
                    Value{Value::Array{
                        Value{"bd"}, Value{"create"},
                        Value{"Blackbird model complaint " + complaint},
                        Value{"--type"}, Value{"bug"}, Value{"--description"},
                        Value{"Local audit complaint " + complaint + "; conversation " +
                              hex_identity(identity_.conversation.bytes()) +
                              ". Inspect complaint.detail locally. Advisory; no "
                              "immediate repair required."},
                        Value{"--json"}}}},
                   {"timeout_seconds", Value{Number{"20"}}},
                   {"output_max_bytes", Value{Number{"4096"}}}}));
          const auto *exit = delivery.find("exit_code");
          const bool delivered = exit && exit->number().text() == "0";
          auto result =
              Value::object({{"complaint", Value{complaint}},
                             {"retained_locally", Value{true}},
                             {"bead_created", Value{delivered}},
                             {"delivery", delivery},
                             {"sink", Value{"external consumer not configured"}},
                             {"repair_required_now", Value{false}}});
          log_.original({"complaint.delivery", unwrap(encode_packet_string(result)),
                         Value::object({{"complaint", Value{complaint}}})});
          return result;
        }
        if (name == "audit_inspect")
          return log_.inspect(field(arguments, "query"), attempt);
        if (name == "trajectory_read")
          return log_.trajectory(field(arguments, "query"), attempt);
        if (name == "variables_read")
          return log_.variables(field(arguments, "query"), attempt);
        if (name == "git_observe")
          return observations_->git(arguments, cancelled);
        if (name == "participant_configure")
          return participants_->configure(arguments);
        if (name == "participant_read")
          return participants_->read(arguments);
        if (name == "participant_send")
          return participants_->send(
              arguments, {{},
                          RawParticipantIdentities{attempt.bytes(), invocation.bytes(),
                                                   admitted_generation.bytes()}});
        if (name == "participant_cancel")
          return participants_->cancel(arguments);
        if (name == "participant_await")
          return participants_->await(arguments, cancelled);
        if (name == "participant_join")
          return participants_->join(arguments, cancelled);
        if (name == "participant_archive")
          return participants_->archive(arguments);
        if (name == "participant_start") {
          if (const auto *task = arguments.find("task_id");
              task && !task->string().empty())
            if (field(tasks_.read(Value::object({{"id", *task}})), "items")
                    .array()
                    .empty())
              throw Error{ErrorCode::invalid_range};
          const auto transport =
              participant_transport
                  ? participant_transport
                  : ParticipantTransport{[&](const Value &prepared,
                                             const ColleagueCapture &capture,
                                             const std::function<bool()> &stop,
                                             const RawAdapterContext *context) {
                      return native_colleague_transport(prepared, capture, stop,
                                                        nullptr, context);
                    }};
          return participants_->start(
              hex_identity(attempt.bytes()), arguments, transport,
              {{},
               RawParticipantIdentities{attempt.bytes(), invocation.bytes(),
                                        admitted_generation.bytes()}});
        }
        if (name == "provider_auth_status")
          return ProviderAuth{}.status();
        if (name == "colleague_catalog")
          return colleague_catalog();
        if (name == "colleague") {
          const auto capture = [&](std::string_view label, std::string_view raw) {
            log_.original(
                {"colleague." + std::string{label}, raw,
                 Value::object({{"attempt", Value{hex_identity(attempt.bytes())}},
                                {"request_id", arguments.find("request_id")
                                                   ? field(arguments, "request_id")
                                                   : Value{}}})});
          };
          const auto transport =
              colleague_transport
                  ? colleague_transport
                  : ColleagueTransport{[&](const Value &prepared,
                                           const ColleagueCapture &retain,
                                           const RawAdapterContext *context) {
                      return native_colleague_transport(prepared, retain, cancelled,
                                                        nullptr, context);
                    }};
          const RawProviderOwner owner{
              attempt.bytes(), invocation.bytes(), admitted_generation.bytes(), {}};
          const RawAdapterContext context{&owner, std::nullopt, nullptr, nullptr};
          return call_colleague(arguments, capture, transport, &context);
        }
        if (name == "tasks_read")
          return tasks_.read(arguments.find("query") ? field(arguments, "query")
                                                     : Value::object({}));
        if (name == "tasks_edit")
          return tasks_.edit(arguments);
        if (name == "context_inspect")
          return context_.inspect(field(arguments, "query"));
        if (name == "context_stats")

          return stats();
        if (name == "context_view")
          return context_.view();
        if (name == "context_edit")
          return context_.edit(field(arguments, "candidate"));
        if (name == "context_originals")
          return context_.originals();
        if (name == "context_restore") {
          context_.restore(string_field(arguments, "entry"));
          return context_.view();
        }
        if (name == "lua")
          return runtime_->eval(string_field(arguments, "code"));
        LocalTools tools{[&](std::string_view label, std::string_view raw) {
          try {
            log_.original(
                {label, raw,
                 Value::object({{"attempt", Value{hex_identity(attempt.bytes())}}})});
          } catch (const Error &e) {
            if (e.code == ErrorCode::capacity)
              unretained_bytes_ += raw.size();
            throw;
          }
          if (label == "process.output" && process_output)
            process_output(raw);
        }};
        tools.cancelled = [this] {
          poll_commands();
          return cancelled && cancelled();
        };
        auto result = tools.run(name, arguments);
        if (name == "exec" && process_output)
          process_output("\n[exit " + field(result, "exit_code").number().text() +
                         "]\n");
        return result;
      });
}
Value CodingEngine::stats() const {
  auto result = context_.stats();
  const auto journal = log_.root().journal_usage();
  const auto maintenance_error = log_.root().maintenance_error();
  result.object().emplace_back(
      "maintenance_error", maintenance_error
                               ? Value{std::string{error_name(maintenance_error->code)}}
                               : Value{});
  result.object().emplace_back("resident_tail_records",
                               Value{Number{log_.root().tail_facts().size()}});
  auto number = [](std::uint64_t n) { return Value{Number{n}}; };
  result.object().emplace_back(
      "audit",
      Value::object(
          {{"scope", Value{"physical observation; not admission permission or "
                           "reserved headroom"}},
           {"journal", Value{hex_identity(journal.prefix.journal.bytes())}},
           {"prefix_end", number(journal.prefix.end_offset)},
           {"observed_extent",
            journal.observed_extent ? number(*journal.observed_extent) : Value{}},
           {"max_file_bytes", number(journal.limit.max_file_bytes)},
           {"remaining_bytes",
            journal.remaining_bytes() ? number(*journal.remaining_bytes()) : Value{}},
           {"max_records", number(journal.limit.max_records)},
           {"indexed_records", number(journal.indexed_records)},
           {"remaining_records", number(journal.remaining_records())},
           {"writer_state", number(static_cast<unsigned>(journal.state))},
           {"approaching", Value{journal.approaching()}},
           {"extent_error",
            journal.extent_error
                ? Value::object(
                      {{"code",
                        Value{std::string{error_name(journal.extent_error->code)}}},
                       {"detail", Value{Number{journal.extent_error->detail}}}})
                : Value{}}}));
  result.object().emplace_back(
      "context_budget",
      budget_view(string_field(program_config_, "model").empty()
                      ? model_
                      : string_field(program_config_, "model"),
                  unwrap(encode_packet_string(Value{context_.items()})).size()));
  result.object().emplace_back("last_request_bytes", last_request_bytes_);
  result.object().emplace_back("usage", usage_);
  result.object().emplace_back("uncontained_exec_recovery",
                               Value{uncontained_exec_recovery_});
  result.object().emplace_back(
      "request_scope", Value{"last request in this process; null if unavailable"});
  return result;
}
void CodingEngine::validate_session_switch() const {
  if (participants_->active() || command_jobs_->active())
    throw Error{ErrorCode::busy};
}
void CodingEngine::validate_restart() const {
  if (uncontained_exec_recovery_)
    throw Error{ErrorCode::external_unknown};
  validate_session_switch();
  validate_protocol(context_.items());
}
void CodingEngine::effort(std::string value) {
  if (value != "low" && value != "medium" && value != "high" && value != "xhigh")
    throw Error{ErrorCode::invalid_range};
  effort_ = std::move(value);
}
void CodingEngine::present(const Value &item) {
  std::string text;
  for (const auto &part : field(item, "content").array())
    if (string_field(part, "type") == "output_text")
      text += string_field(part, "text");
  const auto *id = item.find("id");
  auto found = previewed_.find(id ? id->string() : "");
  if (found != previewed_.end()) {
    if (!text.starts_with(found->second)) {
      if (notice || display)
        (notice ? notice
                : display)("\n[Final response differs from streamed preview]\n");
    } else
      text.erase(0, found->second.size());
    previewed_.erase(found);
  }
  if (!text.empty()) {
    log_.original({"display", text, Value{}});
    if (display)
      display(text);
  }
}
Error CodingEngine::claim_backstop() {
  if (turn_running_ || operation_depth_ != 0 || !failed_turn_ || backstop_claimed_ ||
      failed_turn_->code == ErrorCode::interrupted || (cancelled && cancelled()) ||
      uncontained_exec_recovery_ || !detail::locally_quiescent() ||
      log_.root().state() != JournalWriterState::live)
    throw Error{ErrorCode::conflict};
  backstop_claimed_ = true;
  return *failed_turn_;
}
Value CodingEngine::operator_call(std::string_view name, const Value &arguments) {
  // Quote only the operation name; arguments transfer as native values.
  auto literal = [](std::string_view value) {
    std::string out = "\"";
    for (const char ch : value) {
      const auto c = static_cast<unsigned char>(ch);
      out += "\\";
      out += static_cast<char>('0' + c / 100);
      out += static_cast<char>('0' + (c / 10) % 10);
      out += static_cast<char>('0' + c % 10);
    }
    return out + "\"";
  };
  operator_arguments_ = arguments;
  const auto program = "return blackbird.call(" + literal(name) + ",_native_args)";
  turn({"", program});
  return workflow_result_;
}
bool CodingEngine::operator_turn(std::string_view prompt, std::string_view fallback) {
  if (prompt.empty())
    return false;
  const auto selected = workflows_->select(prompt);
  if (!selected) {
    if (prompt.starts_with("/"))
      return false;
    turn({prompt, fallback});
    return true;
  }
  const auto invocation = Value::object({{"name", field(selected->definition, "name")},
                                         {"arguments", Value{selected->arguments}},
                                         {"prompt", Value{std::string{prompt}}},
                                         {"trigger", Value{selected->trigger}},
                                         {"origin", Value{"operator"}}});
  operator_arguments_ = invocation;
  const auto program = "return blackbird.call('workflow_invoke',_native_args)";
  turn({prompt, program});
  return true;
}
void CodingEngine::turn(TurnInput input) {
  RawTurnTrace trace{turn_running_, backstop_claimed_, generation_, context_.head()};
  std::uint8_t branch = 4, stage = 0;
  std::optional<Error> terminal_error;
  struct Terminal {
    RawTurnTrace &trace;
    std::uint8_t &branch, &stage;
    std::optional<Error> &error;
    ~Terminal() { trace.finish(branch, stage, error); }
  } terminal{trace, branch, stage, terminal_error};
  if (backstop_claimed_ || turn_running_) {
    branch = 1;
    terminal_error = Error{ErrorCode::conflict};
    throw *terminal_error;
  }
  const auto is_null = [](const Value &value) noexcept {
    return std::holds_alternative<std::nullptr_t>(value.value());
  };
  const auto state = [&](std::uint8_t reached) noexcept {
    trace.state(reached, {turn_running_, backstop_claimed_, capacity_stopped_,
                          !is_null(pending_budget_), !is_null(pending_program_config_),
                          !is_null(pending_lua_tools_), bool(pending_workflows_),
                          workflow_depth_, workflow_invocations_, operation_depth_,
                          unretained_bytes_});
  };
  const auto clear_value = [](Value &value) noexcept {
    const auto discarded = std::move(value);
  };
  const auto clear_pending = [&]() noexcept {
    clear_value(pending_budget_);
    pending_workflows_.reset();
    clear_value(pending_program_config_);
    pending_program_revision_.clear();
    clear_value(pending_lua_tools_);
    pending_tools_revision_.clear();
    pending_budget_revision_.clear();
  };
  auto *previous_trace = turn_trace_;
  turn_trace_ = &trace;
  turn_running_ = true;
  struct Running {
    CodingEngine &engine;
    RawTurnTrace *previous;
    const decltype(state) &capture;
    ~Running() {
      engine.turn_running_ = false;
      engine.workflow_continuations_.clear();
      engine.operator_arguments_.reset();
      capture(5);
      engine.turn_trace_ = previous;
    }
  } running{*this, previous_trace, state};
  auto &root = log_.root();
  const RawContextOrigin origin{trace.reference()};
  const RawContextOrigin stop_origin{trace.reference(), trace.context_bank(),
                                     trace.region_bank(),
                                     RawContextOrigin::Input::no_new};
  WorkflowAcceptance acceptance;
  bool workflow_began = false, activation_applied = false;
  bool activate_program = false, activate_tools = false, activate_budget = false;
  const auto apply_activation = [&]() noexcept {
    if (!acceptance.boundary_program_accepted || activation_applied)
      return;
    if (activate_program) {
      program_config_.swap(pending_program_config_);
      program_revision_.swap(pending_program_revision_);
      workflows_.swap(pending_workflows_);
    }
    if (activate_tools) {
      lua_tools_.swap(pending_lua_tools_);
      tools_revision_.swap(pending_tools_revision_);
    }
    if (activate_budget) {
      budget_.swap(pending_budget_);
      budget_revision_.swap(pending_budget_revision_);
    }
    activation_applied = true;
    if (activate_program)
      trace.activation(0, program_revision_);
    if (activate_tools)
      trace.activation(1, tools_revision_);
    if (activate_budget)
      trace.activation(2, budget_revision_);
    clear_pending();
  };
  const auto placeholders = [&](bool interrupted, RawContextOrigin context_origin) {
    const auto request = trace.placeholder_request(interrupted, context_.head());
    try {
      auto result = ContextStore::stop_outputs(
          field(context_.view(context_origin), "entries").array(), interrupted);
      trace.placeholder_return(request, RawPlaceholderOutcome::returned, result.size());
      return result;
    } catch (const Error &error) {
      trace.placeholder_return(request, RawPlaceholderOutcome::error, 0, error);
      throw;
    } catch (const std::bad_alloc &) {
      trace.placeholder_return(request, RawPlaceholderOutcome::allocation);
      throw;
    } catch (...) {
      trace.placeholder_return(request, RawPlaceholderOutcome::unknown);
      throw;
    }
  };
  struct SettlementRelease {
    CodingEngine &engine;
    ~SettlementRelease() {
      if (!engine.command_jobs_->debt())
        engine.settlement_.reset();
    }
  } settlement_release{*this};
  struct Reset {
    ContextStore &context;
    ~Reset() { context.protect = {}; }
  } reset{context_};
  try {
    // The null business value is prepared before effects; cleanup uses moves.
    const Value empty;
    const auto rollback = [&](const Error *error) {
      apply_activation();
      clear_pending();
      context_.protect = {};
      stage = 4;
      state(stage);
      RetainedState::MaintenanceScope maintenance{root};
      std::optional<RawRetainedScope> stop;
      if (auto *bundle = root.raw_stop_bundle(); bundle && bundle->semantic)
        stop.emplace(root, *bundle->semantic, bundle->physical.get(),
                     trace.reference());
      if (workflow_began)
        (void)context_.finish_workflow(false, empty, stop_origin);
      restart_note_.reset();
      if (error &&
          (error->code == ErrorCode::interrupted ||
           error->code == ErrorCode::capacity) &&
          root.state() == JournalWriterState::live) {
        auto results = placeholders(error->code == ErrorCode::interrupted, stop_origin);
        if (!results.empty())
          context_.append(std::move(results), "stop.linkage", stop_origin);
      }
    };
    try {
      failed_turn_.reset();
      clear_value(workflow_result_);
      workflow_continuations_.clear();
      workflow_invocations_ = 0;
      workflow_failure_.reset();
      clear_pending();
      repairable_outputs_ = placeholders(false, origin);
      restart_note_.reset();
      capacity_stopped_ = false;
      unretained_bytes_ = 0;
      state(stage);
      const bool existed = bool(settlement_);
      auto credit = context_.cancellation_budget(
          field(context_.view(origin), "entries").array(), empty, origin);
      credit.max_file_bytes += (1 + command_jobs_->debt()) * 8192;
      credit.max_records += (1 + command_jobs_->debt()) * 16;
      if (!settlement_)
        settlement_ = unwrap(root.protect_settlement(credit));
      else
        unwrap(root.refresh_settlement(credit));
      trace.admission(existed, credit, root.cursor());
      context_.protect = [this](const Value::Array &entries, const Value &proposal,
                                const RawEventRef &parent) {
        protect_workflow(entries, proposal, parent);
      };
      context_.begin_workflow(origin);
      workflow_began = true;
      stage = 1;
      state(stage);
      generation_ = unwrap(root.issue<DefinitionGenerationId>());
      root_source_.reset();
      const auto source_request = trace.source_request(generation_, input.program);
      try {
        root_source_ = log_.retain_program(input.program, generation_);
        trace.source_return(source_request, 0, root_source_);
      } catch (const Error &error) {
        trace.source_return(source_request, 1, {}, error);
        throw;
      } catch (const std::bad_alloc &) {
        trace.source_return(source_request, 2);
        throw;
      } catch (...) {
        trace.source_return(source_request, 3);
        throw;
      }
      root_source_length_ = input.program.size();
      if (!input.prompt.empty())
        context_.append(
            {Value::object({{"role", Value{"user"}},
                            {"content", Value{std::string{input.prompt}}}})},
            "operator", origin);
      stage = 2;
      state(stage);
      runtime_ = std::make_unique<Runtime>(*this);
      workflow_result_ = runtime_->eval(
          input.program, operator_arguments_ ? &*operator_arguments_ : nullptr);
      operator_arguments_.reset();
      drain_workflows();
      if (workflow_failure_)
        throw *workflow_failure_;
      if (capacity_stopped_)
        throw Error{ErrorCode::capacity};
      if (cancelled && cancelled())
        throw Error{ErrorCode::interrupted};
      if (restart_note_)
        validate_protocol(context_.items(origin), trace.reference());
      activate_program = !is_null(pending_program_config_);
      activate_tools = !is_null(pending_lua_tools_);
      activate_budget = !is_null(pending_budget_);
      const auto boundary_program =
          !activate_budget && !activate_tools && !activate_program
              ? empty
              : Value::object(
                    {{"label", Value{"workflow-config-effective-v1"}},
                     {"program_config",
                      activate_program ? pending_program_config_ : program_config_},
                     {"program_revision",
                      Value{activate_program ? pending_program_revision_
                                             : program_revision_}},
                     {"policy", activate_budget ? pending_budget_ : budget_},
                     {"revision", Value{activate_budget ? pending_budget_revision_
                                                        : budget_revision_}},
                     {"tools", activate_tools ? pending_lua_tools_ : lua_tools_},
                     {"tools_revision", Value{activate_tools ? pending_tools_revision_
                                                             : tools_revision_}},
                     {"activation", Value{"successful-workflow-boundary"}}});
      stage = 3;
      state(stage);
      auto settlement =
          context_.finish_workflow(true, boundary_program, origin, &acceptance);
      apply_activation();
      if (!is_null(settlement) && display) {
        try {
          display("Managed context: " + unwrap(format_value(settlement)) + "\n");
        } catch (...) { /* Notification cannot undo committed workflow work. */
        }
      }
    } catch (const Error &error) {
      rollback(&error);
      throw;
    } catch (...) {
      rollback(nullptr);
      throw;
    }
    branch = 0;
  } catch (const Error &error) {
    failed_turn_ = error;
    terminal_error = error;
    branch = 2;
    throw;
  } catch (const std::bad_alloc &) {
    failed_turn_ = Error{ErrorCode::allocation};
    branch = 3;
    throw;
  } catch (...) {
    failed_turn_ = Error{ErrorCode::external_unknown};
    branch = 4;
    throw;
  }
}

} // namespace blackbird
