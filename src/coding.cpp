#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/raw_authored.hpp"
#include "blackbird/raw_bridge.hpp"
#include "blackbird/raw_command.hpp"
#include "blackbird/raw_complaint.hpp"
#include "blackbird/raw_operation.hpp"
#include "blackbird/raw_provider.hpp"
#include "blackbird/raw_restart.hpp"
#include "blackbird/raw_runtime.hpp"
#include "blackbird/raw_turn.hpp"
#include "blackbird/request_cache.hpp"
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
void OpenAiCodingProvider::begin_request_group(const Value &,
                                               const RawAdapterContext *context) {
  if (request_binding_)
    throw Error{ErrorCode::busy};
  auto config = config_;
  config.cancelled = cancelled;
  config.raw_context = context;
  request_binding_ = unwrap(openai_request_binding(config));
}
void OpenAiCodingProvider::end_request_group() noexcept { request_binding_.reset(); }
bool OpenAiCodingProvider::supports_prompt_cache_policy() const noexcept {
  if (!request_binding_)
    return false;
  const auto matches = [&](std::string_view key, std::string_view expected) {
    const auto *value = request_binding_->find(key);
    const auto *text = value ? std::get_if<std::string>(&value->value()) : nullptr;
    return text && *text == expected;
  };
  return matches("kind", "api_key") &&
         matches("base_url", "https://api.openai.com/v1") &&
         matches("protocol", "responses");
}
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
    if (responding_)
      throw Error{ErrorCode::busy};
    responding_ = true;
    struct Responding {
      bool &active;
      ~Responding() { active = false; }
    } responding{responding_};
    auto config = config_;
    config.raw_owner = owner;
    config.raw_context = context;
    config.response_observer = capture;
    config.cancelled = cancelled;
    const bool temporary_group = !request_binding_;
    if (temporary_group)
      begin_request_group(request, context);
    struct End {
      OpenAiCodingProvider &provider;
      bool temporary;
      ~End() {
        if (temporary)
          provider.end_request_group();
      }
    } end{*this, temporary_group};
    if (!request_binding_)
      throw Error{ErrorCode::corrupt};
    auto login = unwrap(codex_login(config, false, &*request_binding_));
    auto raw = unwrap(openai_http(config, login, "responses", &request));
    return unwrap(completed_response(raw, {}, context));
  });
}
struct CodingEngine::CommandTaskActivity {
  RawThreadHandle owner;
  RawTaskActivityTrace trace;
  std::function<void(const Value &)> &observer;
  std::function<void(const Value &, RawTaskPresentation)> &native_observer;
  std::string id, attempt_text, operation;
  OperationAttemptId attempt;
  CommandTaskMap::node_type *pending_node = nullptr;
  bool *transferred = nullptr;
  bool bound = false, ended = false;
  CommandTaskActivity(
      std::function<void(const Value &)> &callback,
      std::function<void(const Value &, RawTaskPresentation)> &native_callback,
      std::string task, OperationAttemptId actual_attempt, std::string encoded_attempt,
      std::string operation_name)
      : owner(raw_current_thread()), observer(callback),
        native_observer(native_callback), id(std::move(task)),
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
    const auto notification =
        trace.notification(activity, bool(native_observer) || bool(observer));
    if (!native_observer && !observer) {
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
      const auto event = Value::object({{"task_id", Value{id}},
                                        {"attempt", Value{attempt_text}},
                                        {"operation", Value{operation}},
                                        {"phase", Value{std::move(phase)}}});
      if (native_observer)
        native_observer(
            event,
            RawTaskPresentation{
                activity.ordinal ? std::optional{activity} : std::nullopt, {}, {}});
      else
        observer(event);
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
    RawAuthoredReference loaded_source{};
    auto *token = current_module(L);
    {
      RawCaptureFailureScope capture{self->trace.owner.enabled};
      try {
        auto credit = self->claim(53);
        std::size_t length = 0;
        const char *source = lua_tolstring(L, 1, &length);
        std::size_t name_size = 0;
        const char *name = lua_tolstring(L, 2, &name_size);
        if (token)
          token->chunk({name, name_size}, {name, std::strlen(name) + 1});
        capture.require();
        const auto loaded = self->load(L, {source, length}, name, SourceKind::module,
                                       credit ? &*credit : nullptr);
        lua_status = loaded.lua_status;
        loaded_source = loaded.reference;
        if (token)
          (void)token->event({4824, 3}, std::array{ByteView{loaded_source.lane},
                                                   raw_native(loaded_source.ordinal)});
        capture.require();
      } catch (const Error &caught_error) {
        self->acquisition_failed(caught_error);
        rejected = caught_error;
        failed = true;
      } catch (const std::bad_alloc &) {
        rejected = {ErrorCode::allocation};
        failed = true;
      } catch (...) {
        rejected = {ErrorCode::external_unknown};
        failed = true;
      }
      if (capture.error()) {
        self->trace.owner.admission_failure = *capture.error();
        if (token)
          token->failed(*capture.error());
      }
    }
    if (failed)
      return authored_raise(L, rejected);
    if (lua_status != LUA_OK) {
      lua_pushnil(L);
      lua_insert(L, -2);
      return 2;
    }
    {
      RawCaptureFailureScope capture{self->trace.owner.enabled};
      try {
        const int index = 1;
        const void *function = lua_topointer(L, -1), *environment = lua_topointer(L, 3);
        const auto assignment =
            token ? token->event({4830, 4},
                                 std::array{ByteView{loaded_source.lane},
                                            raw_native(loaded_source.ordinal),
                                            raw_native(function),
                                            raw_native(environment), raw_native(index)})
                  : RawEventRef{};
        capture.require();
        lua_pushvalue(L, 3);
        const char *name = lua_setupvalue(L, -2, index);
        const bool present = name != nullptr;
        if (token && present) {
          const auto size = std::strlen(name) + 1;
          if (size > 5) {
            raw_capture_failed({ErrorCode::overflow});
            throw Error{ErrorCode::overflow};
          }
          (void)token->linked(4831, assignment,
                              std::array{raw_native(present), raw_native(size),
                                         std::as_bytes(std::span{name, size})});
        } else if (token)
          (void)token->linked(4831, assignment, std::array{raw_native(present)});
        capture.require();
      } catch (const Error &why) {
        rejected = why;
        failed = true;
      } catch (...) {
        rejected = {ErrorCode::external_unknown};
        failed = true;
      }
      if (capture.error()) {
        self->trace.owner.admission_failure = *capture.error();
        if (token)
          token->failed(*capture.error());
      }
    }
    if (failed)
      return authored_raise(L, rejected);
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
    auto *token = current_module(L);
    {
      RawCaptureFailureScope capture{self->trace.owner.enabled};
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
        if (token)
          (void)token->event({4834, 5}, std::array{ByteView{request.lane},
                                                   raw_native(request.ordinal),
                                                   raw_native(function)});
        capture.require();
        lua_pushvalue(L, 1);
        lua_status = lua_pcall(L, 0, 1, 0);
        if (token)
          token->inner_status(lua_status);
        const std::array returned{ByteView{request.lane}, raw_native(request.ordinal),
                                  raw_native(lua_status)};
        if (self->trace.owner.enabled)
          (void)raw_authored_publish(1518, returned, credit ? &*credit : nullptr);
      } catch (const Error &caught_error) {
        self->acquisition_failed(caught_error);
        rejected = caught_error;
        failed = true;
      } catch (const std::bad_alloc &) {
        rejected = {ErrorCode::allocation};
        failed = true;
      } catch (...) {
        rejected = {ErrorCode::external_unknown};
        failed = true;
      }
      if (capture.error()) {
        self->trace.owner.admission_failure = *capture.error();
        if (token)
          token->failed(*capture.error());
      }
    }
    if (failed)
      return authored_raise(L, rejected);
    lua_pushboolean(L, lua_status == LUA_OK);
    lua_insert(L, -2);
    return 2;
  }
  lua_State *state = nullptr;
  std::string error;
  std::optional<Error> failure;
  struct NativeExceptionFrame {
    std::exception_ptr primary;
  };
  NativeExceptionFrame *native_exception_frame = nullptr;
  struct ArgumentFrame {
    const Value *arguments = nullptr;
    int previous = LUA_NOREF;
    bool captured = false;
    std::uint8_t phase = 0;
  };
  ArgumentFrame *argument_frame = nullptr;
  bool argument_restore_failed = false;
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
    if (module_issuer)
      module_issuer->closing.store(true);
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
    lua_newtable(L);
    lua_newtable(L);
    lua_pushliteral(L, "k");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &module_current_key);
    lua_newtable(L);
    lua_pushcfunction(L, module_gc);
    lua_setfield(L, -2, "__gc");
    lua_pushlightuserdata(L, self);
    lua_pushcclosure(L, module_close, 1);
    lua_setfield(L, -2, "__close");
    lua_rawsetp(L, LUA_REGISTRYINDEX, &module_tag_key);
    lua_pushcfunction(L, create_module_userdata);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &module_setup_key);
    lua_pushcfunction(L, prepare_native);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &preparation_key);
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
    lua_pushlightuserdata(L, self);
    lua_pushcclosure(L, module_begin, 1);
    lua_setfield(L, -2, "_module_begin");
    lua_pushlightuserdata(L, self);
    lua_pushcclosure(L, module_observe, 1);
    lua_setfield(L, -2, "_module_observe");
    lua_pushlightuserdata(L, self);
    lua_pushcclosure(L, module_end, 1);
    lua_setfield(L, -2, "_module_end");
    lua_rawgetp(L, LUA_REGISTRYINDEX, &module_current_key);
    lua_setfield(L, -2, "_module_current");
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
        "do local cache,loading={},{};local "
        "load_module,call_module,begin,observe,finish,current,protected,running="
        "blackbird._module_load,blackbird._module_call,blackbird._module_begin,"
        "blackbird._module_observe,blackbird._module_end,blackbird._module_current,"
        "pcall,coroutine.running; "
        "blackbird._module_load=nil;blackbird._module_call=nil;blackbird._module_begin="
        "nil;blackbird._module_observe=nil;blackbird._module_end=nil;blackbird._module_"
        "current=nil; "
        "local function import(name,token) "
        "local cached=cache[name];local hit=cached~=nil;observe(token,1,hit);if hit "
        "then return cached end "
        "local active=loading[name];observe(token,2,not not active);assert(not "
        "active,'cyclic module import: '..name); "
        "local r=blackbird.call('module_source',{name=name});assert(r.source,'module "
        "unavailable: '..name); "
        "local env=setmetatable({}, {__index=_G});local "
        "fn=assert(load_module(r.source,'@module:'..name..':'..r.revision,env)); "
        "loading[name]=true;observe(token,3,loading[name]);local "
        "ok,value=call_module(fn);"
        "loading[name]=nil;observe(token,4,loading[name]);if not ok then error(value) "
        "end;"
        "assert(value~=nil,'module must return a "
        "value');cache[name]=value;observe(token,5,cache[name]);return value end "
        "function blackbird.module(name) local key=running();local parent=current[key];"
        "local token <close> =begin(name,parent);if token==nil then return "
        "import(name,nil) end;current[key]=token;"
        "local "
        "ok,value=protected(import,name,token);current[key]=parent;finish(token,ok,"
        "value);"
        "if not ok then error(value) end;return value end end";
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
  struct ModuleUserdata {
    RawModuleToken *token = nullptr;
  };
  inline static char module_tag_key, module_current_key;
  std::shared_ptr<RawModuleIssuer> module_issuer;
  struct ModuleSetup {
    RawModuleToken *token;
    bool attached = false;
  };
  ModuleSetup *module_setup = nullptr;
  static ModuleUserdata *module_userdata(lua_State *L, int index) noexcept {
    if (lua_type(L, index) != LUA_TUSERDATA ||
        lua_rawlen(L, index) != sizeof(ModuleUserdata))
      return nullptr;
    if (!lua_getmetatable(L, index))
      return nullptr;
    lua_rawgetp(L, LUA_REGISTRYINDEX, &module_tag_key);
    const bool tagged = lua_rawequal(L, -1, -2) != 0;
    lua_pop(L, 2);
    return tagged ? static_cast<ModuleUserdata *>(lua_touserdata(L, index)) : nullptr;
  }
  static RawModuleToken *current_module(lua_State *L) noexcept {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &module_current_key);
    lua_pushthread(L);
    lua_rawget(L, -2);
    auto *stored = module_userdata(L, -1);
    auto *token = stored ? stored->token : nullptr;
    lua_pop(L, 2);
    return token && token->active() ? token : nullptr;
  }
  static int module_gc(lua_State *L) {
    auto *self = *static_cast<Runtime **>(lua_getextraspace(L));
    RawCaptureFailureScope capture{self->trace.owner.enabled};
    auto *stored = module_userdata(L, 1);
    if (stored && stored->token) {
      stored->token->finish(stored->token->closing() ? 2 : 1,
                            RawOperationBranch::unfinished);
      delete stored->token;
      stored->token = nullptr;
    }
    if (capture.error()) {
      self->trace.owner.admission_failure = *capture.error();
      self->failure = *capture.error();
    }
    return 0;
  }
  static int module_close(lua_State *L) {
    auto *self = *static_cast<Runtime **>(lua_getextraspace(L));
    RawCaptureFailureScope capture{self->trace.owner.enabled};
    auto *stored = module_userdata(L, 1);
    if (!stored || !stored->token || !stored->token->active())
      return 0;
    lua_rawgetp(L, LUA_REGISTRYINDEX, &module_current_key);
    lua_getiuservalue(L, 1, 2);
    lua_rawget(L, -2);
    const bool current = lua_rawequal(L, 1, -1) != 0;
    lua_pop(L, 1);
    if (current) {
      lua_getiuservalue(L, 1, 2);
      lua_getiuservalue(L, 1, 1);
      lua_rawset(L, -3);
    }
    lua_pop(L, 1);
    const int type = lua_type(L, 2);
    const void *value = lua_topointer(L, 2);
    stored->token->finish(3, RawOperationBranch::unfinished, nullptr,
                          {.type = &type, .value = &value});
    if (capture.error()) {
      self->trace.owner.admission_failure = *capture.error();
      self->failure = *capture.error();
    }
    return 0;
  }
  static int create_module_userdata(lua_State *L) {
    auto *self = *static_cast<Runtime **>(lua_getextraspace(L));
    auto *stored =
        static_cast<ModuleUserdata *>(lua_newuserdatauv(L, sizeof(ModuleUserdata), 2));
    stored->token = nullptr;
    lua_pushvalue(L, 1);
    lua_setiuservalue(L, -2, 1);
    lua_pushthread(L);
    lua_setiuservalue(L, -2, 2);
    lua_rawgetp(L, LUA_REGISTRYINDEX, &module_tag_key);
    lua_setmetatable(L, -2);
    stored->token = self->module_setup->token;
    self->module_setup->attached = true;
    return 1;
  }
  inline static char module_setup_key;
  static int module_begin(lua_State *L) {
    auto *self = static_cast<Runtime *>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!self->trace.owner.enabled) {
      lua_pushnil(L);
      return 1;
    }
    bool failed = false;
    Error rejected{ErrorCode::external_unknown};
    {
      RawCaptureFailureScope capture{true};
      std::optional<RawModuleToken> paid;
      std::unique_ptr<RawModuleToken> token;
      RawModuleToken *offered = nullptr;
      RawOperationBranch outcome = RawOperationBranch::error;
      try {
        if (self->trace.owner.admission_failure)
          throw *self->trace.owner.admission_failure;
        if (!lua_checkstack(L, 8))
          throw Error{ErrorCode::allocation};
        if (!self->module_issuer)
          self->module_issuer = RawModuleIssuer::prepare();
        const auto vm = self->trace.owner.vm;
        const auto generation = self->trace.owner.generation;
        const int argc = lua_gettop(L), type = lua_type(L, 1);
        auto *parent = module_userdata(L, 2);
        const auto parent_ref = parent && parent->token && parent->token->active()
                                    ? parent->token->reference()
                                    : RawEventRef{};
        const bool parent_present = parent_ref.ordinal != 0;
        const auto operation = self->engine.operation_trace_
                                   ? self->engine.operation_trace_->reference()
                                   : RawEventRef{};
        const bool operation_present = operation.ordinal != 0;
        std::array<ByteView, 12> fields{};
        std::size_t count = 0;
        fields[count++] = vm.lane;
        fields[count++] = raw_native(vm.ordinal);
        fields[count++] = generation;
        fields[count++] = raw_native(argc);
        fields[count++] = raw_native(type);
        fields[count++] = raw_native(parent_present);
        if (parent_present) {
          fields[count++] = parent_ref.lane;
          fields[count++] = raw_native(parent_ref.ordinal);
        }
        fields[count++] = raw_native(operation_present);
        if (operation_present) {
          fields[count++] = operation.lane;
          fields[count++] = raw_native(operation.ordinal);
        }
        std::optional<std::string_view> name;
        if (type == LUA_TSTRING) {
          std::size_t size = 0;
          const char *text = lua_tolstring(L, 1, &size);
          name = std::string_view{text, size};
        }
        paid.emplace(self->module_issuer, std::span{fields}.first(count), name);
        capture.require();
        token = std::make_unique<RawModuleToken>(std::move(*paid));
        offered = token.get();
        const auto mode = std::uint8_t{3};
        const int base = lua_gettop(L);
        const auto request =
            offered->event({4832, 1}, std::array{raw_native(mode), raw_native(base)});
        capture.require();
        ModuleSetup setup{offered};
        auto *previous = self->module_setup;
        self->module_setup = &setup;
        struct Restore {
          ModuleSetup *&current;
          ModuleSetup *previous;
          ~Restore() { current = previous; }
        } restore{self->module_setup, previous};
        lua_rawgetp(L, LUA_REGISTRYINDEX, &module_setup_key);
        lua_pushvalue(L, 2);
        const int lua_status = lua_pcall(L, 1, 1, 0);
        if (setup.attached)
          (void)token.release();
        const int top = lua_gettop(L);
        (void)offered->linked(4833, request,
                              std::array{raw_native(lua_status), raw_native(top)});
        capture.require();
        if (lua_status != LUA_OK)
          throw Error{lua_status == LUA_ERRMEM ? ErrorCode::allocation
                                               : ErrorCode::external_unknown};
      } catch (const Error &why) {
        rejected = why;
        failed = true;
      } catch (const std::bad_alloc &) {
        rejected = {ErrorCode::allocation};
        outcome = RawOperationBranch::allocation;
        failed = true;
      } catch (...) {
        rejected = {ErrorCode::external_unknown};
        outcome = RawOperationBranch::nonstandard;
        failed = true;
      }
      if (failed) {
        raw_capture_failed(rejected);
        self->trace.owner.admission_failure = capture.error().value_or(rejected);
        self->failure = rejected;
        auto *owned = offered ? offered : paid ? &*paid : nullptr;
        if (owned)
          owned->finish(4, outcome, &rejected);
      }
    }
    if (failed)
      return authored_raise(L, rejected);
    return 1;
  }
  static int module_observe(lua_State *L) {
    if (lua_isnil(L, 1))
      return 0;
    auto *self = static_cast<Runtime *>(lua_touserdata(L, lua_upvalueindex(1)));
    bool failed = false;
    Error rejected{ErrorCode::external_unknown};
    {
      RawCaptureFailureScope capture{self->trace.owner.enabled};
      auto *stored = module_userdata(L, 1);
      try {
        if (!stored || !stored->token || !lua_isinteger(L, 2))
          throw Error{ErrorCode::invalid_range};
        const auto phase = lua_tointeger(L, 2);
        if (phase == 1 || phase == 2) {
          const auto branch = static_cast<std::uint8_t>(phase);
          const bool predicate = lua_toboolean(L, 3) != 0;
          (void)stored->token->event(
              {4822, 2}, std::array{raw_native(branch), raw_native(predicate)});
        } else if (phase >= 3 && phase <= 5) {
          const auto mutation = static_cast<std::uint8_t>(phase - 2);
          const int type = lua_type(L, 3);
          const void *value = lua_topointer(L, 3);
          (void)stored->token->event(
              {4823, static_cast<std::uint8_t>(phase == 3   ? 5
                                               : phase == 4 ? 6
                                                            : 7)},
              std::array{raw_native(mutation), raw_native(type), raw_native(value)});
        } else
          throw Error{ErrorCode::invalid_range};
        capture.require();
      } catch (const Error &why) {
        rejected = why;
        failed = true;
      } catch (...) {
        rejected = {ErrorCode::external_unknown};
        failed = true;
      }
      if (failed) {
        if (stored && stored->token)
          stored->token->failed(rejected);
        if (capture.error())
          self->trace.owner.admission_failure = *capture.error();
      }
    }
    if (failed)
      return authored_raise(L, rejected);
    return 0;
  }
  static int module_end(lua_State *L) {
    auto *self = static_cast<Runtime *>(lua_touserdata(L, lua_upvalueindex(1)));
    auto *stored = module_userdata(L, 1);
    if (!stored || !stored->token || !stored->token->active())
      return authored_raise(L, {ErrorCode::invalid_range});
    bool failed = false;
    Error rejected{ErrorCode::external_unknown};
    {
      RawCaptureFailureScope capture{self->trace.owner.enabled};
      const int success = lua_toboolean(L, 2), type = lua_type(L, 3);
      const void *value = lua_topointer(L, 3);
      stored->token->finish(0, RawOperationBranch::returned, nullptr,
                            {.success = &success, .type = &type, .value = &value});
      if (capture.error()) {
        rejected = *capture.error();
        self->trace.owner.admission_failure = rejected;
        failed = true;
      }
    }
    if (failed)
      return authored_raise(L, rejected);
    return 0;
  }
  struct NativeFrame {
    Runtime &runtime;
    RawCaptureFailureScope capture;
    RawBridgeTrace raw;
    std::optional<RawCapacityClaim> error_credit;
    std::optional<Value> value;
    std::optional<Error> primary;
    std::exception_ptr native_exception;
    NativeExceptionFrame *evaluation;
    RawOperationBranch branch = RawOperationBranch::returned;
    std::array<char, 512> message{};
    std::size_t message_size = 0;
    bool prepared = false;
    explicit NativeFrame(Runtime &owner)
        : runtime(owner),
          capture(owner.trace.owner.enabled && owner.trace.owner.vm.ordinal != 0),
          evaluation(owner.native_exception_frame) {}
    void failed(Error why, const char *text, RawOperationBranch outcome) noexcept {
      primary = why;
      branch = outcome;
      if (why.code == ErrorCode::capacity)
        runtime.engine.capacity_stopped_ = true;
      message_size = std::min(std::strlen(text), message.size() - 1);
      std::memcpy(message.data(), text, message_size);
      message[message_size] = '\0';
    }
    void recording() noexcept {
      if (capture.error()) {
        runtime.trace.owner.admission_failure = *capture.error();
        failed(*capture.error(), error_name(capture.error()->code),
               RawOperationBranch::error);
      }
    }
    void begin(lua_State *L, int operation) {
      const auto vm = runtime.trace.owner.vm;
      const auto generation = runtime.trace.owner.generation;
      const auto operation_ref = runtime.engine.operation_trace_
                                     ? runtime.engine.operation_trace_->reference()
                                     : RawEventRef{};
      const bool operation_present = operation_ref.ordinal != 0;
      if (!lua_checkstack(L, 8))
        throw Error{ErrorCode::allocation};
      auto *parent_module = current_module(L);
      const auto module_ref =
          parent_module ? parent_module->reference() : RawEventRef{};
      const bool module_present = module_ref.ordinal != 0;
      const int argc = lua_gettop(L);
      std::array<ByteView, 12> fields{};
      std::size_t count = 0;
      fields[count++] = vm.lane;
      fields[count++] = raw_native(vm.ordinal);
      fields[count++] = generation;
      fields[count++] = raw_native(operation);
      fields[count++] = raw_native(argc);
      fields[count++] = raw_native(operation_present);
      if (operation_present) {
        fields[count++] = operation_ref.lane;
        fields[count++] = raw_native(operation_ref.ordinal);
      }
      fields[count++] = raw_native(module_present);
      if (module_present) {
        fields[count++] = module_ref.lane;
        fields[count++] = raw_native(module_ref.ordinal);
      }
      raw.start(4800, 4801, 264, runtime.trace.owner.enabled && vm.ordinal != 0,
                std::span{fields}.first(count));
      capture.require();
      error_credit = raw.data(560);
      capture.require();
    }
    void original(std::uint8_t role, std::string_view text) {
      if (text.size() > UINT32_MAX - 49) {
        raw_capture_failed({ErrorCode::overflow});
        throw Error{ErrorCode::overflow};
      }
      auto credit = raw.data(49 + text.size());
      const auto size = text.size();
      raw.original(4802, std::array{raw_native(role), raw_native(size), raw_text(text)},
                   credit ? &*credit : nullptr);
      capture.require();
    }
  };
  NativeFrame *bridge_frame = nullptr;
  struct Preparation {
    const Value *value = nullptr;
    const char *message = nullptr;
    std::size_t size = 0;
    std::uint8_t mode = 0;
  };
  Preparation *preparation = nullptr;
  inline static char preparation_key;
  static int prepare_native(lua_State *L) {
    auto *self = *static_cast<Runtime **>(lua_getextraspace(L));
    auto *offered = self->preparation;
    if (offered->mode == 1)
      lua_pushlstring(L, offered->message, offered->size);
    else if (offered->mode == 2) {
      luaL_checktype(L, 1, LUA_TTABLE);
      lua_pushvalue(L, 1);
      tag(L, ValueTag::array);
    } else
      push_value(L, *offered->value);
    return 1;
  }
  int prepare_unrecorded(NativeFrame &frame, lua_State *L, std::uint8_t mode) {
    Preparation offered{frame.value ? &*frame.value : nullptr, frame.message.data(),
                        frame.message_size, mode};
    auto *previous = preparation;
    preparation = &offered;
    struct Restore {
      Preparation *&current;
      Preparation *previous;
      ~Restore() { current = previous; }
    } restore{preparation, previous};
    lua_rawgetp(L, LUA_REGISTRYINDEX, &preparation_key);
    if (mode == 2)
      lua_pushvalue(L, 1);
    const int lua_status = lua_pcall(L, mode == 2 ? 1 : 0, 1, 0);
    frame.prepared = true;
    return lua_status;
  }
  int prepare(NativeFrame &frame, lua_State *L, std::uint8_t mode) {
    if (!lua_checkstack(L, 4))
      throw Error{ErrorCode::allocation};
    const int base = lua_gettop(L);
    const auto request =
        frame.raw.event(4803, std::array{raw_native(mode), raw_native(base)});
    if (mode != 1)
      frame.capture.require();
    const int lua_status = prepare_unrecorded(frame, L, mode);
    const int top = lua_gettop(L);
    if (frame.raw.enabled())
      frame.raw.linked(4804, request,
                       std::array{raw_native(lua_status), raw_native(top)});
    frame.capture.require();
    return lua_status;
  }
  static std::string bridge_string(lua_State *L, int index, NativeFrame &frame,
                                   std::uint8_t role) {
    if (lua_type(L, index) != LUA_TSTRING)
      throw Error{ErrorCode::invalid_range};
    std::size_t size = 0;
    const char *text = lua_tolstring(L, index, &size);
    frame.original(role, {text, size});
    return {text, size};
  }
  static int bridge(lua_State *L) {
    auto *self = static_cast<Runtime *>(lua_touserdata(L, lua_upvalueindex(1)));
    const auto op = static_cast<Op>(lua_tointeger(L, lua_upvalueindex(2)));
    bool raise = false;
    {
      NativeFrame frame{*self};
      auto *previous = self->bridge_frame;
      const auto previous_failure = self->failure;
      self->bridge_frame = &frame;
      self->failure.reset();
      struct Restore {
        Runtime &runtime;
        NativeFrame *previous;
        ~Restore() { runtime.bridge_frame = previous; }
      } restore{*self, previous};
      try {
        if (frame.evaluation && frame.evaluation->primary)
          std::rethrow_exception(frame.evaluation->primary);
        frame.begin(L, static_cast<int>(op));
        frame.raw.stage(1);
        if (self->argument_restore_failed)
          throw Error{ErrorCode::external_unknown};
        if (self->trace.owner.admission_failure)
          throw *self->trace.owner.admission_failure;
        if (!lua_checkstack(L, 1024))
          throw Error{ErrorCode::allocation};
        frame.raw.stage(2);
        std::size_t nodes = 0;
        frame.raw.stage(3);
        switch (op) {
        case packet_decode_op:
          frame.value = unwrap(decode_packet_value(read_value(L, 1, {0, &nodes})));
          break;
        case packet_encode_op:
          frame.value =
              Value{unwrap(encode_packet_string(read_value(L, 1, {0, &nodes})))};
          break;
        case format_op:
          frame.value = Value{unwrap(format_value(read_value(L, 1, {0, &nodes})))};
          break;
        case stats_op:
          frame.value = self->engine.stats();
          break;
        case restarting_op:
          frame.value = Value{self->engine.restart_note_.has_value()};
          break;
        case request_op:
          frame.value = self->engine.request(lua_isnoneornil(L, 1)
                                                 ? Value::object({})
                                                 : read_value(L, 1, {0, &nodes}));
          break;
        case call_op: {
          auto name = bridge_string(L, 1, frame, 1);
          auto args = read_value(L, 2, {0, &nodes});
          frame.value = self->engine.call(std::move(name), std::move(args));
          break;
        }
        case manage_op:
          frame.value = self->engine.context_.manage(read_value(L, 1, {0, &nodes}));
          break;
        case inspect_op:
          frame.value = self->engine.context_.inspect(read_value(L, 1, {0, &nodes}));
          break;
        case context_op:
          frame.value = self->engine.context_.view();
          break;
        case edit_op:
          frame.value = self->engine.context_.edit(read_value(L, 1, {0, &nodes}));
          break;
        case originals_op:
          frame.value = self->engine.context_.originals();
          break;
        case restore_op:
          self->engine.context_.restore(bridge_string(L, 1, frame, 3));
          frame.value = self->engine.context_.view();
          break;
        case append_op: {
          auto items = read_value(L, 1, {0, &nodes});
          self->engine.context_.append(items.array(), "lua.synthetic");
          frame.value = self->engine.context_.view();
          break;
        }
        case decode_op:
          frame.value = unwrap(parse_json(bridge_string(L, 1, frame, 2)));
          break;
        case encode_op:
          frame.value = Value{unwrap(dump_json(read_value(L, 1, {0, &nodes})))};
          break;
        case display_op: {
          auto text = bridge_string(L, 1, frame, 4);
          self->engine.log_.original({"display", text, Value{}});
          if (self->engine.display)
            self->engine.display(text);
          frame.value = Value{};
          break;
        }
        case present_op:
          self->engine.present(read_value(L, 1, {0, &nodes}));
          frame.value = Value{};
          break;
        case array_op:
          break;
        }
        frame.capture.require();
      } catch (const Error &why) {
        frame.failed(why, error_name(why.code), RawOperationBranch::error);
      } catch (const std::bad_alloc &) {
        frame.failed({ErrorCode::allocation}, "allocation",
                     RawOperationBranch::allocation);
      } catch (const std::length_error &why) {
        frame.failed({ErrorCode::external_unknown}, why.what(),
                     RawOperationBranch::length);
      } catch (const std::exception &why) {
        frame.failed({ErrorCode::external_unknown}, why.what(),
                     RawOperationBranch::standard);
      } catch (...) {
        frame.native_exception = std::current_exception();
        frame.failed({ErrorCode::external_unknown}, "foreign native exception",
                     RawOperationBranch::nonstandard);
      }
      frame.recording();
      if (frame.primary) {
        try {
          const auto role = std::uint8_t{5};
          const auto size = frame.message_size;
          frame.raw.original(4802,
                             std::array{raw_native(role), raw_native(size),
                                        raw_text({frame.message.data(), size})},
                             frame.error_credit ? &*frame.error_credit : nullptr);
        } catch (const Error &) {
          frame.recording();
        }
      }
      frame.raw.stage(4);
      try {
        const int status = self->prepare(frame, L,
                                         frame.primary    ? 1
                                         : op == array_op ? 2
                                                          : 0);
        if (status != LUA_OK && !frame.primary)
          frame.failed(self->failure.value_or(Error{status == LUA_ERRMEM
                                                        ? ErrorCode::allocation
                                                        : ErrorCode::external_unknown}),
                       status == LUA_ERRMEM ? "allocation" : "Lua preparation failed",
                       RawOperationBranch::error);
        raise = frame.primary.has_value() || status != LUA_OK;
      } catch (const Error &why) {
        frame.failed(why, error_name(why.code), RawOperationBranch::error);
        raise = true;
      } catch (const std::exception &) {
        frame.failed({ErrorCode::external_unknown}, "native preparation failed",
                     RawOperationBranch::nonstandard);
        raise = true;
      } catch (...) {
        if (!frame.native_exception)
          frame.native_exception = std::current_exception();
        frame.failed({ErrorCode::external_unknown}, "native preparation failed",
                     RawOperationBranch::nonstandard);
        raise = true;
      }
      frame.recording();
      if (raise && !frame.prepared && lua_checkstack(L, 4))
        (void)self->prepare_unrecorded(frame, L, 1);
      const bool returned = !raise && !frame.primary;
      const int returns = 1;
      frame.raw.stage(5);
      if (returned)
        frame.raw.finish(frame.branch, nullptr,
                         std::array{raw_native(returned), raw_native(returns)});
      else
        frame.raw.finish(frame.branch, frame.primary ? &*frame.primary : nullptr,
                         std::array{raw_native(returned)});
      frame.recording();
      raise |= frame.primary.has_value();
      self->failure = previous ? previous_failure : frame.primary;
      // Carry the actual exception to the enclosing protected eval, after this
      // callback's owners are gone. A finalizer cannot replace an outer callback's
      // existing primary failure in the same eval.
      if (frame.native_exception && frame.evaluation && !frame.evaluation->primary &&
          !(previous && previous->evaluation == frame.evaluation && previous->primary))
        frame.evaluation->primary = frame.native_exception;
      frame.capture.finish();
    }
    // Protected preparation already rooted the result/error. No C++ owner remains.
    if (raise)
      return lua_error(L);
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
  struct ToolInvocation {
    const Value &definition;
    const Value &arguments;
    const RawProgramContext *origin = nullptr;
  };
  Value invoke_tool(ToolInvocation invocation) {
    const auto &source = string_field(invocation.definition, "source");
    const auto length = source.size();
    constexpr std::string_view prefix = "local fn = function(args)\n";
    constexpr std::string_view suffix = "\nend\nreturn fn(_native_args)";
    RawProgramCall wrapper{3836, 3837, invocation.origin,
                           std::array{raw_native(length)}};
    return wrapper.run([&] {
      if (length > UINT32_MAX - 48 - prefix.size() - suffix.size())
        throw Error{ErrorCode::overflow};
      const auto total = prefix.size() + length + suffix.size();
      auto original = wrapper.data(48 + total);
      std::string wrapped;
      wrapped.reserve(total);
      wrapped.append(prefix);
      wrapped.append(source);
      wrapped.append(suffix);
      wrapper.bytes(3838, wrapped, original);
      return eval(wrapped, &invocation.arguments, SourceKind::tool);
    });
  }
  static int transfer_arguments(lua_State *L) {
    auto *self = *static_cast<Runtime **>(lua_getextraspace(L));
    auto *frame = self->argument_frame;
    if (frame->phase == 1) {
      if (frame->previous == LUA_REFNIL)
        lua_pushnil(L);
      else
        lua_rawgeti(L, LUA_REGISTRYINDEX, frame->previous);
      lua_setglobal(L, "_native_args");
    } else if (frame->phase == 2) {
      luaL_unref(L, LUA_REGISTRYINDEX, frame->previous);
    } else {
      lua_getglobal(L, "_native_args");
      frame->previous = luaL_ref(L, LUA_REGISTRYINDEX);
      frame->captured = true;
      push_value(L, *frame->arguments);
      lua_setglobal(L, "_native_args");
    }
    return 0;
  }
  int protected_arguments(ArgumentFrame &frame, std::uint8_t phase) {
    const RawProgramContext origin{
        RawEventRef{trace.owner.vm.lane, trace.owner.vm.ordinal}, {}};
    RawProgramCall transfer{3840, 3841, &origin,
                            std::array{raw_native(phase), raw_native(frame.captured),
                                       raw_native(frame.previous)},
                            sizeof(int) + sizeof(bool)};
    return transfer.run([&] {
      if (!lua_checkstack(state, 1024))
        throw Error{ErrorCode::allocation};
      auto *previous = argument_frame;
      argument_frame = &frame;
      frame.phase = phase;
      lua_pushcfunction(state, transfer_arguments);
      int lua_status = LUA_OK;
      try {
        lua_status = lua_pcall(state, 0, 0, 0);
      } catch (...) {
        argument_frame = previous;
        throw;
      }
      argument_frame = previous;
      if (phase == 2 && lua_status == LUA_OK) {
        frame.captured = false;
        frame.previous = LUA_NOREF;
      }
      transfer.finish(RawOperationBranch::returned, nullptr,
                      std::array{raw_native(lua_status), raw_native(frame.captured)});
      return lua_status;
    });
  }
  static int error_text(lua_State *L) {
    const char *message = lua_tostring(L, 1);
    if (message == nullptr)
      lua_pushnil(L);
    else
      lua_pushvalue(L, 1);
    return 1;
  }
  Value eval(std::string_view source, const Value *arguments = nullptr,
             SourceKind kind = SourceKind::root) {
    NativeExceptionFrame native_exception;
    struct RestoreExceptionFrame {
      NativeExceptionFrame *&current;
      NativeExceptionFrame *previous;
      ~RestoreExceptionFrame() { current = previous; }
    } restore_exception{native_exception_frame, native_exception_frame};
    native_exception_frame = &native_exception;
    const int base = lua_gettop(state);
    ArgumentFrame frame{arguments};
    auto restore_arguments = [&] {
      if (frame.captured) {
        const int restore_base = lua_gettop(state);
        try {
          const int lua_status = protected_arguments(frame, 1);
          lua_settop(state, restore_base);
          if (lua_status != LUA_OK) {
            argument_restore_failed = true;
            throw Error{lua_status == LUA_ERRMEM ? ErrorCode::allocation
                                                 : ErrorCode::external_unknown};
          }
          const int released = protected_arguments(frame, 2);
          lua_settop(state, restore_base);
          if (released != LUA_OK) {
            argument_restore_failed = true;
            throw Error{released == LUA_ERRMEM ? ErrorCode::allocation
                                               : ErrorCode::external_unknown};
          }
        } catch (...) {
          argument_restore_failed = true;
          throw;
        }
      }
    };
    try {
      if (argument_restore_failed)
        throw Error{ErrorCode::external_unknown};
      if (arguments) {
        const int lua_status = protected_arguments(frame, 0);
        frame.arguments = nullptr;
        if (lua_status != LUA_OK)
          throw Error{lua_status == LUA_ERRMEM ? ErrorCode::allocation
                                               : ErrorCode::external_unknown};
      }
      const int execution_status =
          load_and_call(state, source, "arco-program", kind, 1);
      if (native_exception.primary)
        std::rethrow_exception(native_exception.primary);
      if (execution_status != LUA_OK) {
        const auto native_error = authored_error(state, -1);
        const RawProgramContext text_origin{
            RawEventRef{trace.owner.vm.lane, trace.owner.vm.ordinal}, {}};
        const std::uint8_t phase = 3;
        RawProgramCall conversion{3840, 3841, &text_origin,
                                  std::array{raw_native(phase),
                                             raw_native(frame.captured),
                                             raw_native(frame.previous)},
                                  sizeof(int) + sizeof(bool)};
        const int converted = conversion.run([&] {
          if (!lua_checkstack(state, 2))
            throw Error{ErrorCode::allocation};
          lua_pushcfunction(state, error_text);
          lua_pushvalue(state, -2);
          const int lua_status = lua_pcall(state, 1, 1, 0);
          conversion.finish(
              RawOperationBranch::returned, nullptr,
              std::array{raw_native(lua_status), raw_native(frame.captured)});
          return lua_status;
        });
        if (converted != LUA_OK)
          throw Error{ErrorCode::allocation};
        const char *message =
            lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : nullptr;
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
      if (argument_restore_failed)
        throw Error{ErrorCode::external_unknown};
      std::size_t nodes = 0;
      restore_arguments();
      if (native_exception.primary)
        std::rethrow_exception(native_exception.primary);
      auto result = read_value(state, -1, {0, &nodes});
      lua_settop(state, base);
      return result;
    } catch (...) {
      frame.arguments = nullptr;
      const auto primary = native_exception.primary ? native_exception.primary
                                                    : std::current_exception();
      try {
        restore_arguments();
      } catch (...) {
        // The registry root survives until Runtime closes. Both native statuses
        // are already emitted; restoration cannot replace the primary failure.
      }
      lua_settop(state, base);
      std::rethrow_exception(primary);
    }
  }
};
namespace {
// Intentionally small supported schema: a flat object of scalar JSON types.
// Unsupported schema keywords reject rather than pretending to enforce them.
void validate_tool_schema(const Value &schema, RawProgramCall *trace = nullptr) {
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
    if (trace)
      trace->validation(3, properties.size());
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

  // Enabled by default (a1v C1): 384/192 KiB from the measured 2026-10-17
  // campaign requests (papers/clm-a1v-2026-10-19/REQUEST_SIZES.md).
  return Value::object(
      {{"enabled", Value{true}},
       {"trigger_bytes", Value{Number{"393216"}}},
       {"target_bytes", Value{Number{"196608"}}},
       {"reason", Value{"default from measured request sizes; byte policy is not a "
                        "provider token limit"}}});
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
namespace {
RawProgramContext program_origin(const RawOperationTrace *operation,
                                 const RawTurnTrace *turn,
                                 DefinitionGenerationId generation) {
  return {operation ? std::optional{operation->reference()}
          : turn    ? std::optional{turn->reference()}
                    : std::nullopt,
          generation.bytes()};
}
template <class Refused>
std::string program_encoded(RawProgramCall &trace, std::uint8_t kind,
                            const Value &value, Refused &&refused) {
  const ValueLimits limits{65536, 500000, 128};
  auto data = trace.data(49 + limits.bytes);
  const auto origin = trace.context();
  RawProgramCall encoding{3804, 3805, &origin,
                          std::array{raw_native(kind), raw_native(limits.bytes),
                                     raw_native(limits.nodes),
                                     raw_native(limits.depth)},
                          sizeof(bool)};
  auto encoded = encoding.run([&] {
    auto result = encode_packet_string(value, limits);
    const bool success = result.has_value();
    if (success)
      trace.encoded(kind, result.value(), data);
    encoding.finish(RawOperationBranch::returned, success ? nullptr : &result.error(),
                    std::array{raw_native(success)});
    return result;
  });
  if (!encoded.has_value() && encoded.error().code == ErrorCode::capacity)
    std::forward<Refused>(refused)();
  return unwrap(std::move(encoded));
}
template <class F>
bool program_syntax(RawProgramCall &trace, std::uint8_t kind, std::string_view source,
                    F &&validate) {
  trace.stage(4);
  const auto origin = trace.context();
  const auto size = source.size();
  RawProgramCall syntax{3807, 3808, &origin,
                        std::array{raw_native(kind), raw_native(size)}, sizeof(bool)};
  syntax.stage(4);
  return syntax.run([&] {
    const bool valid = std::forward<F>(validate)();
    syntax.finish(RawOperationBranch::returned, nullptr, std::array{raw_native(valid)});
    return valid;
  });
}
} // namespace
Value CodingEngine::program_config(const Value &arguments, DomainAdmission *admission) {
  const auto origin = program_origin(operation_trace_, turn_trace_, generation_);
  RawProgramCall trace{arguments.find("proposal") ? std::uint8_t{2} : std::uint8_t{1},
                       &origin};
  return trace.run([&] {
    trace.stage(1);
    trace.revision(0, program_revision_);
    if (const auto *proposal = arguments.find("proposal")) {
      if (const auto *base = arguments.find("base");
          base && base->string() != program_revision_)
        throw Error{ErrorCode::conflict};
      for (const auto &[key, value] : proposal->object()) {
        (void)value;
        if (key != "modules" && key != "model" && key != "effort" &&
            key != "workflows" && key != "workflow_prefix" && key != "prompt_cache")
          throw Error{ErrorCode::invalid_range};
      }
      const auto &model = string_field(*proposal, "model");
      const auto &effort = string_field(*proposal, "effort");
      if (model.size() > 256 ||
          (!effort.empty() && effort != "low" && effort != "medium" &&
           effort != "high" && effort != "xhigh" && effort != "max"))
        throw Error{ErrorCode::invalid_range};
      if (const auto *cache = proposal->find("prompt_cache"))
        (void)prompt_cache_mode(*cache);
      const auto &modules = field(*proposal, "modules").array();
      trace.stage(2);
      if (modules.size() > 32) {
        if (admission)
          admission->reject(AdmissionResource::program_modules, modules.size(), 32);
        throw Error{ErrorCode::capacity};
      }
      (void)program_encoded(trace, 0, *proposal, [&] {
        if (admission)
          admission->reject(AdmissionResource::program_encoding);
      });
      std::set<std::string> names;
      for (const auto &module : modules) {
        trace.stage(3);
        trace.validation(0, modules.size());
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
            !program_syntax(trace, 0, source,
                            [&] { return runtime_->valid_module_source(source); }))
          throw Error{ErrorCode::invalid_range};
      }
      const auto child_origin = trace.context();
      auto candidate_registry =
          std::make_shared<WorkflowRegistry>(*proposal, "candidate", &child_origin);
      for (const auto &d : candidate_registry->definitions().array())
        if (!program_syntax(trace, 1, string_field(d, "source"), [&] {
              return runtime_->valid_tool_source(string_field(d, "source"));
            }))
          throw Error{ErrorCode::invalid_range};
      // Copy/issue before publication; validation never executes candidate code.
      auto next = *proposal;
      if (trace.enabled() && !program_issuer_)
        program_issuer_ = RawProgramIssuer::prepare();
      RawProgramLifetime pending{trace.enabled() ? program_issuer_ : nullptr,
                                 trace.reference(), false};
      try {
        trace.stage(5);
        auto issued_credit = trace.control(57);
        const auto issued = unwrap(log_.root().issue<DefinitionGenerationId>());
        const std::uint8_t kind = 0;
        trace.event(3809, std::array{raw_native(kind), ByteView{issued.bytes()}},
                    issued_credit);
        pending.generation(issued.bytes());
        auto revision = hex_identity(issued.bytes());
        trace.stage(6);
        auto registry =
            std::make_shared<WorkflowRegistry>(next, revision, &child_origin);
        trace.stage(7);
        pending_program_trace_.finish(1);
        pending_workflows_ = std::move(registry);
        pending_program_config_ = std::move(next);
        pending_program_revision_ = std::move(revision);
        pending.installed(pending_program_revision_);
        pending_program_trace_ = std::move(pending);
      } catch (const Error &error) {
        pending.finish(3, &error);
        throw;
      } catch (...) {
        pending.finish(3);
        throw;
      }
    }
    trace.stage(8);
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
               {"source",
                Value{"retained program.source; selected workflow file is read "
                      "at turn admission"}},
               {"selection",
                Value{"--workflow or /workflow; no staged selector in this API"}}})},
         {"pending_revision", pending_program_config_ == Value{}
                                  ? Value{}
                                  : Value{pending_program_revision_}},
         {"staged", Value{arguments.find("proposal") != nullptr}},
         {"activation", Value{"successful-workflow-boundary"}}});
  });
}
Value CodingEngine::module_source(const Value &arguments) {
  const auto origin = program_origin(operation_trace_, turn_trace_, generation_);
  const std::uint8_t kind = 2;
  RawProgramCall trace{3815, 3816, &origin, std::array{raw_native(kind)}};
  return trace.run([&] {
    const auto &name = string_field(arguments, "name");
    auto body = trace.data(48 + name.size());
    trace.bytes(3817, name, body);
    for (const auto &module : field(program_config_, "modules").array())
      if (string_field(module, "name") == name)
        return Value::object({{"name", Value{name}},
                              {"source", field(module, "source")},
                              {"revision", Value{program_revision_}}});
    throw Error{ErrorCode::invalid_range};
  });
}
Value CodingEngine::tool_registry() const {
  const auto origin = program_origin(operation_trace_, turn_trace_, generation_);
  const std::uint8_t kind = 1;
  RawProgramCall trace{3815, 3816, &origin, std::array{raw_native(kind)}};
  return trace.run([&] {
    return Value::object(
        {{"revision", Value{tools_revision_}},
         {"effective", lua_tools_},
         {"pending", pending_lua_tools_},
         {"pending_revision",
          pending_lua_tools_ == Value{} ? Value{} : Value{pending_tools_revision_}}});
  });
}
Value CodingEngine::request_tools() const {
  const auto origin = program_origin(operation_trace_, turn_trace_, generation_);
  const std::uint8_t kind = 3;
  RawProgramCall trace{3815, 3816, &origin, std::array{raw_native(kind)}};
  return trace.run([&] {
    auto result = tool_definitions();
    for (const auto &d : lua_tools_.array())
      result.array().push_back(Value::object({{"type", Value{"function"}},
                                              {"name", field(d, "name")},
                                              {"description", field(d, "description")},
                                              {"parameters", field(d, "parameters")}}));
    return result;
  });
}
Value CodingEngine::define_tool(const Value &arguments) {
  const auto origin = program_origin(operation_trace_, turn_trace_, generation_);
  RawProgramCall trace{4, &origin};
  return trace.run([&] {
    trace.stage(1);
    trace.revision(1, tools_revision_);
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
    trace.stage(3);
    validate_tool_schema(field(definition, "parameters"), &trace);
    if (!program_syntax(trace, 2, source,
                        [&] { return runtime_->valid_tool_source(source); }))
      throw Error{ErrorCode::invalid_range};
    auto next = pending_lua_tools_ == Value{} ? lua_tools_ : pending_lua_tools_;
    auto found =
        std::find_if(next.array().begin(), next.array().end(),
                     [&](const Value &d) { return string_field(d, "name") == name; });
    if (found == next.array().end())
      next.array().push_back(definition);
    else
      *found = definition;
    trace.stage(2);
    if (next.array().size() > 32 ||
        program_encoded(trace, 2, next, [] {}).size() > 65536)
      throw Error{ErrorCode::capacity};
    if (trace.enabled() && !program_issuer_)
      program_issuer_ = RawProgramIssuer::prepare();
    RawProgramLifetime pending{trace.enabled() ? program_issuer_ : nullptr,
                               trace.reference(), false, 1};
    try {
      trace.stage(5);
      auto issued_credit = trace.control(57);
      const auto issued = unwrap(log_.root().issue<DefinitionGenerationId>());
      const std::uint8_t kind = 1;
      trace.event(3809, std::array{raw_native(kind), ByteView{issued.bytes()}},
                  issued_credit);
      pending.generation(issued.bytes());
      auto revision = hex_identity(issued.bytes());
      trace.stage(7);
      pending_tools_trace_.finish(1);
      pending_lua_tools_ = std::move(next);
      pending_tools_revision_ = std::move(revision);
      pending.installed(pending_tools_revision_);
      pending_tools_trace_ = std::move(pending);
    } catch (const Error &error) {
      pending.finish(3, &error);
      throw;
    } catch (...) {
      pending.finish(3);
      throw;
    }
    trace.stage(8);
    auto result = tool_registry();
    result.object().emplace_back("staged", Value{true});
    return result;
  });
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
  const RawProgramContext bootstrap{{}, generation_.bytes()};
  RawProgramCall trace{0, &bootstrap};
  trace.run([&] {
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
        // A recorded built-in policy was never a session choice ("initial-disabled"
        // before a1v): resolve it to the current default, keep chosen ones exactly.
        if (revision == "builtin-default" || revision == "initial-disabled") {
          budget_ = default_context_budget();
          budget_revision_ = "builtin-default";
        } else {
          budget_ = std::move(policy);
          budget_revision_ = revision;
        }
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
        },
        [&](const Value &activity, RawEventRef source) {
          if (task_activity_native)
            task_activity_native(
                activity,
                RawTaskPresentation{
                    source.ordinal ? std::optional{source} : std::nullopt, {}, {}});
          else if (task_activity)
            task_activity(activity);
        });
    const auto origin = trace.context();
    workflows_ =
        std::make_shared<WorkflowRegistry>(program_config_, program_revision_, &origin);
    if (context_.head().empty())
      context_.append({}, "initial");
  });
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
ParticipantObservationReader CodingEngine::participant_observer() const {
  return participants_->observer();
}
void CodingEngine::shutdown_participants() {
  participants_->shutdown();
  if ((!cancelled || !cancelled()) && log_.root().state() == JournalWriterState::live)
    participants_->drain();
}
void recover_coding_session(RetainedState &root, RecoveryMode mode,
                            const RawAdapterContext *context) {
  const auto cursor = root.cursor();
  RawSessionCall whole{4440, 4441, context,
                       std::array{raw_native(mode), ByteView{cursor.journal.bytes()},
                                  raw_native(cursor.sequence),
                                  raw_native(cursor.end_offset)}};
  whole.run([&] {
    auto blocked = whole.control(41);
    struct CodingRecovery final : CustodyVerifier {
      struct Abandoned {
        OperationAttemptId attempt;
        std::string operation;
        bool local;
        bool uncontained;
      };
      RetainedState &root;
      RecoveryMode mode;
      RawSessionCall &whole;
      std::vector<Abandoned> abandoned;
      CodingRecovery(RetainedState &value, RecoveryMode selected, RawSessionCall &owner)
          : root(value), mode(selected), whole(owner) {}
      Result<void> verify(std::span<const AttemptState> attempts) override {
        const auto owner = whole.context();
        const auto count = attempts.size();
        RawSessionCall verify{4442, 4443, &owner, std::array{raw_native(count)}, 10};
        return verify.run([&] {
          for (const auto &attempt : attempts) {
            const auto parent = verify.context();
            RawSessionCall check{
                4444, 4445, &parent,
                std::array{ByteView{attempt.admission.attempt.bytes()},
                           ByteView{attempt.admission.decision.bytes()},
                           ByteView{attempt.admission.invocation.bytes()},
                           raw_native(attempt.opened)},
                10};
            const auto checked = check.run([&] {
              auto classify = [&]() -> Result<void> {
                auto decision_credit = check.control(89);
                const auto recovered_decision =
                    root.decision(attempt.admission.decision);
                if (!recovered_decision.has_value()) {
                  const std::uint8_t kind = 3;
                  const auto &error = recovered_decision.error();
                  (void)check.event(4446,
                                    std::array{raw_native(kind), raw_native(error.code),
                                               raw_native(error.detail)},
                                    decision_credit);
                  return Result<void>::failure(error);
                }
                const auto *decision = &recovered_decision.value();
                const std::uint8_t decision_kind = 1;
                (void)check.event(4446,
                                  std::array{raw_native(decision_kind),
                                             ByteView{decision->decision.bytes()},
                                             ByteView{decision->definition.bytes()},
                                             ByteView{decision->context.bytes()}},
                                  decision_credit);
                const auto metadata =
                    raw_restart_packet(check, 9, decision->continuation);
                if (!raw_restart_predicate(
                        check, 1, [&] { return metadata.has_value(); }, 4447))
                  return Result<void>::failure({ErrorCode::external_unknown});
                const auto *operation = metadata.value().find("operation");
                if (!raw_restart_predicate(
                        check, 2,
                        [&] {
                          return operation && std::holds_alternative<std::string>(
                                                  operation->value());
                        },
                        4447))
                  return Result<void>::failure({ErrorCode::external_unknown});
                const auto &name = operation->string();
                (void)raw_restart_source(check, 8, name);
                const bool provider = raw_restart_predicate(
                    check, 3,
                    [&] {
                      return name == "provider" || name == "colleague" ||
                             name == "participant_start" ||
                             name == "participant_send" || name == "participant_read" ||
                             name == "participant_cancel" ||
                             name == "participant_await" ||
                             name == "participant_join" ||
                             name == "participant_archive" ||
                             name == "participant_configure";
                    },
                    4447);
                const bool local = raw_restart_predicate(
                    check, 4,
                    [&] {
                      return name == "exec" || name == "process" ||
                             name == "read_file" || name == "write_file" ||
                             name == "edit_file" || name == "lua" ||
                             name == "workflow_execute" || name == "workflow_invoke";
                    },
                    4447);
                if (!provider &&
                    (!local || !raw_restart_predicate(
                                   check, 5,
                                   [&] {
                                     return mode ==
                                            RecoveryMode::acknowledge_local_unknowns;
                                   },
                                   4447)))
                  return Result<void>::failure({ErrorCode::external_unknown});
                const auto *input = metadata.value().find("input");
                const auto *generation = metadata.value().find("generation");
                const auto *revision = metadata.value().find("revision");
                const std::string_view admitted_input{
                    reinterpret_cast<const char *>(attempt.admission.input.data()),
                    attempt.admission.input.size()};
                (void)raw_restart_source(check, 14, admitted_input);
                auto invocation_credit = check.control(97);
                const auto recovered_invocation =
                    root.invocation(attempt.admission.invocation);
                if (!recovered_invocation.has_value()) {
                  const std::uint8_t kind = 4;
                  const auto &error = recovered_invocation.error();
                  (void)check.event(4446,
                                    std::array{raw_native(kind), raw_native(error.code),
                                               raw_native(error.detail)},
                                    invocation_credit);
                  (void)raw_restart_predicate(
                      check, 6, [&] { return recovered_invocation.has_value(); }, 4447);
                  return Result<void>::failure(error);
                }
                const auto &invocation = recovered_invocation.value();
                const std::uint8_t invocation_kind = 2;
                const auto input_size = invocation.input.size();
                (void)check.event(4446,
                                  std::array{raw_native(invocation_kind),
                                             ByteView{invocation.invocation.bytes()},
                                             ByteView{invocation.decision.bytes()},
                                             ByteView{invocation.definition.bytes()},
                                             raw_native(input_size)},
                                  invocation_credit);
                (void)raw_restart_predicate(
                    check, 6, [&] { return recovered_invocation.has_value(); }, 4447);
                const bool linked = raw_restart_predicate(
                    check, 7,
                    [&] {
                      return invocation.decision == decision->decision &&
                             invocation.definition == decision->definition &&
                             invocation.input == attempt.admission.input &&
                             decision->planned_invocations.size() == 1 &&
                             decision->planned_invocations.front() ==
                                 invocation.invocation;
                    },
                    4447);
                bool input_bound = false;
                if (const auto *binding = metadata.value().find("input_binding")) {
                  const auto *invocation_id = metadata.value().find("invocation");
                  const auto parsed_input = decode_packet_string(admitted_input);
                  input_bound =
                      parsed_input.has_value() &&
                      std::holds_alternative<Value::Object>(
                          parsed_input.value().value()) &&
                      !input && *binding == Value{"invocation-v1"} && invocation_id &&
                      *invocation_id ==
                          Value{raw_restart_hex(check, invocation.invocation.bytes())};
                } else if (input) {
                  auto encoded_credit = check.source_credit(ValueLimits{}.bytes);
                  const auto encoded_input = encode_packet_string(*input);
                  if (encoded_input.has_value())
                    (void)raw_restart_source(check, 10, encoded_input.value(),
                                             encoded_credit);
                  input_bound = std::holds_alternative<Value::Object>(input->value()) &&
                                encoded_input.has_value() &&
                                encoded_input.value() == admitted_input;
                }
                if (!linked ||
                    !raw_restart_predicate(
                        check, 8, [&] { return input_bound; }, 4447) ||
                    !raw_restart_predicate(
                        check, 9, [&] { return generation != nullptr; }, 4447) ||
                    !raw_restart_predicate(
                        check, 10, [&] { return revision != nullptr; }, 4447) ||
                    !raw_restart_predicate(
                        check, 11,
                        [&] {
                          return std::holds_alternative<std::string>(
                              generation->value());
                        },
                        4447) ||
                    !raw_restart_predicate(
                        check, 12,
                        [&] {
                          return std::holds_alternative<std::string>(revision->value());
                        },
                        4447))
                  return Result<void>::failure({ErrorCode::external_unknown});
                (void)raw_restart_source(check, 13, generation->string());
                if (!raw_restart_predicate(
                        check, 13,
                        [&] {
                          return generation->string() ==
                                 raw_restart_hex(check, decision->definition.bytes());
                        },
                        4447))
                  return Result<void>::failure({ErrorCode::external_unknown});
                (void)raw_restart_source(check, 13, revision->string());
                if (!raw_restart_predicate(
                        check, 14,
                        [&] {
                          return revision->string() ==
                                 raw_restart_hex(check, decision->context.bytes());
                        },
                        4447))
                  return Result<void>::failure({ErrorCode::external_unknown});
                auto collected = check.control(41);
                auto identity = check.control(56);
                abandoned.push_back(
                    {attempt.admission.attempt, name, local,
                     attempt.opened && (name == "exec" || name == "process")});
                const std::uint8_t mutation = 1;
                (void)check.event(4448, std::array{raw_native(mutation)}, collected);
                (void)check.event(
                    4449, std::array{ByteView{attempt.admission.attempt.bytes()}},
                    identity);
                return Result<void>::success();
              };
              const auto result = classify();
              raw_restart_result(check, result);
              return result;
            });
            if (!checked.has_value()) {
              raw_restart_result(verify, checked);
              return checked;
            }
          }
          const auto result = Result<void>::success();
          raw_restart_result(verify, result);
          return result;
        });
      }
    } custody{root, mode, whole};
    whole.stage(1);
    raw_restart_mutation(whole, 2, [&] { unwrap(root.reconcile(custody)); }, 4448);
    try {
      whole.stage(2);
      std::vector<RetainedEvent> observations;
      Value::Array uncontained_attempts;
      for (const auto &abandoned : custody.abandoned) {
        if (abandoned.local) {
          auto credit = whole.source_credit(ValueLimits{}.bytes);
          const auto observation = unwrap(encode_packet(Value::object(
              {{"recovery", Value{"acknowledge-local-unknowns"}},
               {"attempt", Value{raw_restart_hex(whole, abandoned.attempt.bytes())}},
               {"operation", Value{abandoned.operation}},
               {"outcome", Value{"unknown"}},
               {"uncontained_exec", Value{abandoned.uncontained}},
               {"message", Value{"Prior process operation abandoned; outcome unknown. "
                                 "No effect replayed, PID adopted or process exit "
                                 "or containment established."}}})));
          (void)raw_restart_source(
              whole, 11,
              {reinterpret_cast<const char *>(observation.data()), observation.size()},
              credit);
          observations.push_back(
              {{},
               AttemptObservationEvent{abandoned.attempt, AttemptPhase::terminal,
                                       AttemptDisposition::unknown, observation}});
          if (abandoned.uncontained)
            uncontained_attempts.emplace_back(
                raw_restart_hex(whole, abandoned.attempt.bytes()));
          continue;
        }
        constexpr std::string_view message =
            "Provider or participant operation abandoned during "
            "prior process; outcome unknown. "
            "No response accepted and no request replayed.";
        (void)raw_restart_source(whole, 11, message);
        const auto bytes = std::as_bytes(std::span{message.data(), message.size()});
        observations.push_back({{},
                                AttemptObservationEvent{abandoned.attempt,
                                                        AttemptPhase::terminal,
                                                        AttemptDisposition::unknown,
                                                        {bytes.begin(), bytes.end()}}});
      }
      if (!uncontained_attempts.empty()) {
        auto credit = whole.source_credit(ValueLimits{}.bytes);
        const auto marker = unwrap(encode_packet(Value::object(
            {{"label", Value{"session.recovery"}},
             {"metadata",
              Value::object({{"mode", Value{"acknowledge-local-unknowns"}},
                             {"uncontained_exec", Value{true}},
                             {"attempts", Value{std::move(uncontained_attempts)}}})},
             {"message", Value{"Prior process custody unavailable; outcome unknown. "
                               "No PID adopted or effect replayed."}}})));
        (void)raw_restart_source(
            whole, 12, {reinterpret_cast<const char *>(marker.data()), marker.size()},
            credit);
        observations.push_back(
            {{},
             ApplicationRecordEvent{unwrap(root.issue<ApplicationRecordId>()),
                                    ApplicationChannel::program, marker}});
      }
      if (!observations.empty()) {
        whole.stage(3);
        const auto parent = whole.context();
        const auto at = root.cursor();
        const auto count = observations.size();
        RawSessionCall append{4450, 4451, &parent,
                              std::array{ByteView{at.journal.bytes()},
                                         raw_native(at.sequence),
                                         raw_native(at.end_offset), raw_native(count)},
                              32};
        append.run([&] {
          auto completion = whole.control(41);
          const auto returned = unwrap(root.append(at, {}, observations));
          append.require_capture();
          append.finish(RawOperationBranch::returned, nullptr,
                        std::array{ByteView{returned.journal.bytes()},
                                   raw_native(returned.sequence),
                                   raw_native(returned.end_offset)});
          const std::uint8_t mutation = 3;
          (void)whole.event(4448, std::array{raw_native(mutation)}, completion);
        });
      }
    } catch (...) {
      root.block_admission();
      const std::uint8_t mutation = 4;
      (void)whole.event(4448, std::array{raw_native(mutation)}, blocked);
      throw;
    }
  });
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
    RawProviderOperationContext *provider_operation,
    const CommandStartStatus *command_start, const DomainAdmission *domain_admission,
    const FileEffectObservation *file_effect) {
  auto &root = log_.root();
  struct FileObservationFence {
    const FileEffectObservation *effect;
    bool &blocked;
    bool retained = false;
    ~FileObservationFence() {
      if (effect && effect->applied() && !retained)
        blocked = true;
    }
  } file_observation{file_effect, file_effect_unretained_};
  RawOperationTrace trace{root};
  RawCaptureFailureScope operation_capture{trace.enabled()};
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
      if (!task_id.empty() && !tasks_.state().item(task_id))
        throw Error{ErrorCode::invalid_range};
    }
    if (trace.callback(0, !pumping_commands_ && static_cast<bool>(cancelled),
                       [&] { return cancelled(); }))
      throw Error{ErrorCode::interrupted};
    trace.callback(1, static_cast<bool>(effect_policy),
                   [&] { effect_policy(name, input); });
    if (file_effect_unretained_)
      throw Error{ErrorCode::incomplete};
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
      staged_tasks.try_emplace(exec_id, task_activity, task_activity_native,
                               std::move(task_id), attempt, exec_id, std::string{name});
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
    bool adapter_capacity = false, timed_out = false, persistent_command = false,
         command_refused = false, domain_refused = false;
    auto prepare = [&](Value adapter_result, const std::optional<Error> &effect_error,
                       bool presentation_fallback = false) {
      result = effect_error ? error_value(*effect_error) : std::move(adapter_result);
      if (effect_error && file_effect && file_effect->applied())
        result.object().emplace_back("effect_outcome", Value{"applied"});
      domain_refused = domain_admission && domain_admission->refusal() &&
                       effect_error && effect_error->code == ErrorCode::capacity &&
                       !operation_capture.error() && !root.raw_owner_blocked();
      if (name == "program_config" && effect_error && !domain_refused) {
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
            Value{"Provide exactly one of path or snapshot. Use one range object "
                  "with mode lines or bytes and optional start/end; omit range for "
                  "the whole file. output_max_bytes must be a nonnegative integer. "
                  "Lines are one-based "
                  "inclusive, bytes zero-based half-open. Do not combine range "
                  "with legacy byte_/line_ fields. Endpoints must be nonnegative "
                  "integers, line start >=1, end >=start."});
        const auto *path = input.find("path");
        const auto *snapshot = input.find("snapshot");
        result.object().emplace_back(
            "example",
            Value::object({{snapshot ? "snapshot" : "path", snapshot ? *snapshot
                                                            : path   ? *path
                                                                     : Value{"file"}},
                           {"range", Value::object({{"mode", Value{"lines"}},
                                                    {"start", Value{Number{"1"}}},
                                                    {"end", Value{Number{"20"}}}})}}));
      }
      if ((name == "tasks_read" || name == "tasks_edit") && effect_error &&
          !domain_refused)
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

      adapter_capacity = domain_refused || (name == "process" && effect_error &&
                                            effect_error->code == ErrorCode::capacity);
      command_refused = command_start && effect_error &&
                        effect_error->code == ErrorCode::capacity &&
                        !command_start->worker_started &&
                        command_start->refusal != CommandStartStatus::Refusal::none;
      if (command_refused) {
        const bool live =
            command_start->refusal == CommandStartStatus::Refusal::live_limit;
        adapter_capacity = true;
        result.object().emplace_back("resource",
                                     Value{live ? "live_commands" : "command_records"});
        result.object().emplace_back("effect_outcome", Value{"not_started"});
        result.object().emplace_back("attempt", Value{exec_id});
        result.object().emplace_back(
            "count",
            Value{Number{live ? command_start->live : command_start->resident}});
        result.object().emplace_back(
            "limit",
            Value{Number{live ? command_start->max_live : command_start->max_records}});
        result.object().emplace_back(
            "message",
            Value{live ? "Live command limit reached; this command was not started. "
                         "Inspect process read or /jobs; wait for existing commands "
                         "or explicitly stop one before choosing further work."
                       : "Resident command record limit reached; this command was "
                         "not started. Inspect process read or /jobs; settle pending "
                         "output/controls or explicitly archive an eligible completed "
                         "job before choosing further work."});
      }
      if (domain_refused) {
        const auto &refusal = *domain_admission->refusal();
        result.object().emplace_back("resource",
                                     Value{admission_resource_name(refusal.resource)});
        result.object().emplace_back("effect_outcome", Value{"not_started"});
        result.object().emplace_back("attempt", Value{hex_identity(attempt.bytes())});
        if (refusal.count)
          result.object().emplace_back("count", Value{Number{*refusal.count}});
        if (refusal.limit)
          result.object().emplace_back("limit", Value{Number{*refusal.limit}});
        result.object().emplace_back(
            "message", Value{"Resource owner refused this request before publication; "
                             "prior accepted work is unchanged. Inspect the named "
                             "resource and choose further work. No automatic retry."});
      }
      timed_out = (effect_error && effect_error->code == ErrorCode::io &&
                   effect_error->detail == ETIMEDOUT) ||
                  (name == "exec" && result.find("timed_out") &&
                   field(result, "timed_out") == Value{true});
      persistent_command = name == "exec" && command_jobs_->contains(exec_id);
      if (name == "exec" && !command_refused && !result.find("output_ref")) {
        result.object().emplace_back("output_ref",
                                     Value{hex_identity(attempt.bytes())});
        if (timed_out) {
          result.object().emplace_back("timed_out", Value{true});
          result.object().emplace_back("effect_outcome", Value{"unknown"});
        }
      }
      auto encode = [&] {
        auto encoded = encode_original(
            result,
            presentation_fallback ? RawOperationOriginal::fallback
                                  : RawOperationOriginal::primary,
            !presentation_fallback && result_data ? &*result_data : nullptr,
            provider_result ? std::optional{provider_result->reference()}
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
      std::optional<Error> error, presentation_error;
      const FileEffectObservation *file_effect;
      const RawCaptureFailureScope &capture;
      RetainedState &root;
      std::exception_ptr deferred;
      std::optional<RawRegionPrepaidScope> receipt_scope;
      bool dispatched = false;
      Boundary(decltype(fn) body_fn, decltype(prepare) &preparation,
               RawOperationTrace &owner, InvocationId actual_invocation,
               DefinitionGenerationId actual_generation, CommandTaskActivity *task,
               RawRetainedExec *actual_exec, const FileEffectObservation *effect,
               const RawCaptureFailureScope &capture_scope, RetainedState &retained)
          : fn(body_fn), prepare_result(preparation), trace(owner),
            invocation(actual_invocation), generation(actual_generation),
            activity(task), raw_exec(actual_exec), file_effect(effect),
            capture(capture_scope), root(retained) {}
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
          auto recover_allocation = [&] {
            if (!file_effect || !file_effect->applied() || capture.error() ||
                root.raw_owner_blocked())
              return;
            // One bounded presentation attempt; the adapter is never called again.
            // Keep its actual receipt separate from this preparation failure.
            presentation_error = Error{ErrorCode::allocation};
            try {
              prepare_result(Value{}, error ? error : presentation_error, true);
              deferred = nullptr;
            } catch (...) {
              // Preserve the first preparation failure; admission will fence below.
            }
          };
          try {
            prepare_result(std::move(result), error);
          } catch (const std::bad_alloc &) {
            deferred = std::current_exception();
            recover_allocation();
          } catch (const Error &failure) {
            deferred = std::current_exception();
            if (failure.code == ErrorCode::allocation)
              recover_allocation();
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
               exec_admission ? &*exec_admission : nullptr,
               file_effect,
               operation_capture,
               root};
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
    if (boundary.deferred) {
      if (file_effect && file_effect->applied())
        capacity_stopped_ = true;
      std::rethrow_exception(boundary.deferred);
    }
    if (!boundary.error && boundary.presentation_error)
      boundary.error = boundary.presentation_error;
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
      file_observation.retained = true;
    } catch (const Error &e) {
      if (e.code != ErrorCode::capacity)
        throw;
      capacity_stopped_ = true;
      unretained_bytes_ += output.size();
      boundary.error = e;
      adapter_capacity = false;
      command_refused = false;
      domain_refused = false;
      if (command_jobs_->active()) {
        command_failure_ = e;
        const auto registry = command_jobs_->read(Value::object({}));
        for (const auto &job : field(registry, "jobs").array())
          if (string_field(job, "state") != "completed")
            command_jobs_->capture_failed(string_field(job, "job_id"));
        command_jobs_->shutdown();
      }
      result = error_value(e);
      if (file_effect && file_effect->applied())
        result.object().emplace_back("effect_outcome", Value{"applied"});
      auto fallback = encode_original(result, RawOperationOriginal::fallback, nullptr);
      output.assign(reinterpret_cast<const char *>(fallback.data()), fallback.size());
    }
    if (boundary.error && boundary.error->code == ErrorCode::capacity &&
        !adapter_capacity) {
      capacity_stopped_ = true;
      RetainedState::MaintenanceScope maintenance{root};
      const std::string_view message =
          file_effect && file_effect->applied()
              ? "file replacement applied; result capture failed; no replay; earlier "
                "originals retained"
              : "effect outcome unknown; no replay; earlier originals retained, "
                "rejected "
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
        adapter_capacity = false;
        command_refused = false;
        domain_refused = false;
        boundary.error = recorded.error();
        result = error_value(*boundary.error);
        if (file_effect && file_effect->applied())
          result.object().emplace_back("effect_outcome", Value{"applied"});
        const std::string bounded =
            file_effect && file_effect->applied()
                ? "capacity stop after dispatch; file replacement applied; no replay"
                : "capacity stop after dispatch; outcome unknown; no replay";
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
          (command_refused || domain_refused) ? "refused; not started"
          : boundary.error && file_effect && file_effect->applied()
              ? "file replacement applied; result failed"
          : persistent_command && !boundary.error && !result.find("exit_code")
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
                         error_name(boundary.error->code) + "]" +
                         ((command_refused || domain_refused)
                              ? " " + string_field(result, "message")
                          : file_effect && file_effect->applied()
                              ? " file replacement applied; no replay"
                              : std::string{}) +
                         "\n");
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

Value CodingEngine::execute_workflow(WorkflowContinuation &invocation) {
  try {
    const RawProgramContext origin{invocation.selection, generation_.bytes()};
    RawProgramCall trace{3820, 3821, &origin,
                         std::array{ByteView{generation_.bytes()},
                                    raw_native(workflow_depth_),
                                    raw_native(workflow_invocations_)},
                         2 * sizeof(unsigned)};
    invocation.parked.finish(0, nullptr, trace.reference());
    auto end = [&](RawOperationBranch branch, const Error *error = nullptr) {
      trace.finish(
          branch, error,
          std::array{raw_native(workflow_depth_), raw_native(workflow_invocations_)});
    };
    try {
      auto guard = trace.control(48);
      auto association = trace.control(72 + invocation.revision.size());
      auto identities = trace.control(88);
      if (workflow_depth_ >= 8) {
        trace.event(
            3824,
            std::array{raw_native(workflow_depth_), raw_native(workflow_invocations_)},
            guard);
        throw Error{ErrorCode::capacity};
      }
      ++workflow_invocations_;
      trace.event(
          3824,
          std::array{raw_native(workflow_depth_), raw_native(workflow_invocations_)},
          guard);
      if (workflow_invocations_ > 64)
        throw Error{ErrorCode::capacity};
      auto result = operation(
          "workflow_execute", invocation.arguments,
          [&](OperationAttemptId attempt, InvocationId invocation_id,
              DefinitionGenerationId generation, CommandTaskActivity *,
              RawRetainedExec *) {
            trace.event(3822,
                        std::array{ByteView{attempt.bytes()},
                                   ByteView{invocation_id.bytes()},
                                   ByteView{generation.bytes()}},
                        identities);
            const auto operation_ref =
                operation_trace_ ? operation_trace_->reference() : RawEventRef{};
            const auto revision_size = invocation.revision.size();
            trace.event(3823,
                        std::array{ByteView{operation_ref.lane},
                                   raw_native(operation_ref.ordinal),
                                   raw_native(revision_size),
                                   raw_text(invocation.revision)},
                        association);
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
            const auto wrapper_origin = trace.context();
            return child.invoke_tool({.definition = invocation.definition,
                                      .arguments = invocation.arguments,
                                      .origin = &wrapper_origin});
          });
      end(RawOperationBranch::returned);
      return result;
    } catch (const Error &error) {
      end(RawOperationBranch::error, &error);
      throw;
    } catch (const std::bad_alloc &) {
      end(RawOperationBranch::allocation);
      throw;
    } catch (const std::length_error &) {
      end(RawOperationBranch::length);
      throw;
    } catch (const std::exception &) {
      end(RawOperationBranch::standard);
      throw;
    } catch (...) {
      end(RawOperationBranch::nonstandard);
      throw;
    }
  } catch (const Error &e) {
    invocation.parked.finish(0, &e);
    workflow_failure_ = e;
    throw;
  } catch (...) {
    invocation.parked.finish(0);
    throw;
  }
}
void CodingEngine::drain_workflows() {
  if (draining_workflows_ || workflow_continuations_.empty())
    return;
  const RawProgramContext origin{
      turn_trace_ ? std::optional{turn_trace_->reference()} : std::nullopt, {}};
  const auto count = workflow_continuations_.size();
  RawProgramCall trace{3825, 3826, &origin,
                       std::array{raw_native(draining_workflows_), raw_native(count)}};
  trace.run([&] {
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
  });
}
Value CodingEngine::request(Value options) {
  struct RequestGroup {
    CodingProvider &provider;
    bool &active;
    bool owned = false;
    bool began = false;
    ~RequestGroup() {
      if (!owned)
        return;
      if (began)
        provider.end_request_group();
      provider.request_group_owned_ = false;
      active = false;
    }
  } request_group{provider_, request_group_active_};
  RawProviderPhase assembly{2 * provider_scalar_credit + sizeof(IdentityBytes) +
                            sizeof(std::size_t) + 32};
  return assembly.run([&] {
    assembly.start(1);
    if (request_group_active_ || provider_.request_group_owned_)
      throw Error{ErrorCode::busy};
    assembly.stage(10);
    RawRegion preparation{raw_regions::provider_prepare};
    if (restart_note_)
      throw Error{ErrorCode::busy};
    assembly.stage(11);
    validate_protocol(context_.items(), assembly.reference());
    assembly.stage(12);
    drain_workflows();
    auto items = context_.items();
    auto origin = context_.head();
    validate_protocol(items, assembly.reference());
    auto request = Value::object(
        {{"model", Value{string_field(program_config_, "model").empty()
                             ? model_
                             : string_field(program_config_, "model")}},
         {"instructions",
          Value{"You are Blackbird, a programmable coding colleague for an expert "
                "operator. Use local file/process tools without approval ceremony. "
                "Gather source with targeted searches and ranges; expand when the "
                "question remains unresolved. Inspect omission counts and recover "
                "needed facts from captured originals. Compose mechanical reads, "
                "loops and projections in Lua rather than requesting a model decision "
                "for each step; return useful results with recovery handles. Preserve "
                "constraints and complete tool groups. Select model/effort explicitly "
                "for the task; smaller prompts alone do not prove cheaper work. "
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
    const auto cache_mode =
        options.find("cache_policy") ? prompt_cache_mode(field(options, "cache_policy"))
        : program_config_.find("prompt_cache")
            ? prompt_cache_mode(field(program_config_, "prompt_cache"))
            : PromptCacheMode::provider_default;
    const auto *state_choice = options.find("state_context");
    if (state_choice && *state_choice != Value{"auto"} &&
        *state_choice != Value{"omit"})
      throw Error{ErrorCode::invalid_range};
    const bool state_context = !state_choice || *state_choice == Value{"auto"};
    for (auto &[key, value] : options.object()) {
      if (key == "retry_policy" || key == "cache_policy" || key == "state_context")
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
    std::string operational_state;
    if (state_context && (!tasks_.state().empty() || tasks_.state().revision() != 0)) {
      operational_state +=
          "CURRENT_TASKS " +
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
          "complete a task. Unfinished tasks remain across turns and compaction.\n";
    }
    const auto *stream = std::get_if<bool>(&field(request, "stream").value());
    if (store == nullptr || *store || stream == nullptr || !*stream)
      throw Error{ErrorCode::unsupported};
    validate_protocol(field(request, "input").array(), assembly.reference());
    const auto input_bytes =
        unwrap(encode_packet_string(field(request, "input"))).size();
    const auto budget = budget_view(string_field(request, "model"), input_bytes);
    const bool triggered = std::get<bool>(field(budget, "triggered").value());
    // The model always knows its context size (a1v C2). A provider that retains
    // request state appends this item to context on every request, so there the
    // line rides only with the trigger rather than accumulating stale sizes.
    if (state_context && (!provider_.retain_request_state() || triggered)) {
      const auto &policy = field(budget, "effective");
      const bool enabled = std::get<bool>(field(policy, "enabled").value());
      operational_state +=
          "CONTEXT_SIZE input_bytes=" + std::to_string(input_bytes) +
          " entries=" + std::to_string(field(request, "input").array().size()) +
          (enabled
               ? " trigger_bytes=" + field(policy, "trigger_bytes").number().text() +
                     " target_bytes=" + field(policy, "target_bytes").number().text()
               : std::string{" budget=off"}) +
          " (native packet bytes, not tokens)\n";
    }
    if (state_context && triggered) {
      operational_state +=
          "CONTEXT_BUDGET basis=selected_input_before_state " +
          unwrap(format_value(budget)) +
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
          "audit reclamation.";
    }
    auto reusable_input_items = field(request, "input").array().size();
    if (!operational_state.empty()) {
      auto state_item =
          Value::object({{"role", Value{"developer"}},
                         {"content", Value{std::move(operational_state)}}});
      if (provider_.retain_request_state()) {
        context_.append({state_item}, "provider.request-state");
        origin = context_.head();
        ++reusable_input_items;
      }
      for (auto &[key, input] : request.object())
        if (key == "input") {
          input.array().push_back(std::move(state_item));
          break;
        }
    }
    assembly.stage(14);
    request_group_active_ = true;
    provider_.request_group_owned_ = true;
    request_group.owned = true;
    const RawAdapterContext preparation_context{nullptr, assembly.reference(), nullptr,
                                                nullptr};
    provider_.cancelled = [this] {
      poll_commands();
      return cancelled && cancelled();
    };
    request_group.began = true;
    provider_.begin_request_group(request, &preparation_context);
    prepare_request_cache(request,
                          {cache_mode, provider_.supports_prompt_cache_policy(),
                           provider_.provider_identity(), reusable_input_items});
    if (context_.head() != origin)
      throw Error{ErrorCode::conflict};
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
          const bool typed_http429 =
              e.code == ErrorCode::provider_rate_limit && e.detail == 429;
          group.stage(27);
          const bool eligible = ordinal < max_attempts &&
                                (transient_http || transient_curl || typed_http429) &&
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
  CommandStartStatus command_start;
  DomainAdmission domain_admission;
  FileEffectObservation file_effect;
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
          command_jobs_->start(id, arguments, raw_exec, &command_start);
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
          const RawAdapterContext raw_context{
              nullptr,
              operation_trace_ ? std::optional{operation_trace_->reference()}
                               : std::nullopt,
              nullptr, nullptr};
          auto result = decision_models_.evaluate(arguments, &raw_context);
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
          const RawAdapterContext beads_capture{
              nullptr,
              operation_trace_ ? std::optional{operation_trace_->reference()}
                               : std::nullopt,
              nullptr, nullptr};
          auto result = beads_.run(arguments, &beads_capture);
          result.object().emplace_back("audit_ref",
                                       Value{hex_identity(attempt.bytes())});
          return result;
        }

        if (name == "workflow_registry") {
          const auto origin =
              program_origin(operation_trace_, turn_trace_, generation_);
          return workflows_->discover(&origin);
        }
        if (name == "workflow_invoke") {
          const auto origin =
              program_origin(operation_trace_, turn_trace_, generation_);
          RawProgramCall selection_trace{7, &origin};
          return selection_trace.run([&] {
            const auto selected_origin = selection_trace.context();
            RawEventRef selected;
            const auto &definition = workflows_->named(string_field(arguments, "name"),
                                                       &selected_origin, &selected);
            WorkflowContinuation continuation{definition,
                                              arguments,
                                              workflows_->revision(),
                                              hex_identity(attempt.bytes()),
                                              selected,
                                              {}};
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
              if (selection_trace.enabled() && !program_issuer_)
                program_issuer_ = RawProgramIssuer::prepare();
              RawProgramLifetime parked{selection_trace.enabled() ? program_issuer_
                                                                  : nullptr,
                                        selected, true};
              parked.queued(workflow_continuations_.size());
              continuation.parked = std::move(parked);
              try {
                workflow_continuations_.push_back(std::move(continuation));
              } catch (const Error &error) {
                continuation.parked.finish(2, &error, {}, RawOperationBranch::error);
                throw;
              } catch (const std::bad_alloc &) {
                continuation.parked.finish(2, nullptr, {},
                                           RawOperationBranch::allocation);
                throw;
              } catch (...) {
                continuation.parked.finish(2, nullptr, {},
                                           RawOperationBranch::nonstandard);
                throw;
              }
              workflow_continuations_.back().parked.installed(
                  workflow_continuations_.size());
              return Value::object(
                  {{"status", Value{"accepted"}},
                   {"name", field(definition, "name")},
                   {"revision", Value{workflows_->revision()}},
                   {"execution",
                    Value{"same-turn after tool outputs, before next request or "
                          "successful boundary"}}});
            }
            return execute_workflow(continuation);
          });
        }
        if (name == "program_config")
          return program_config(arguments, &domain_admission);
        if (name == "module_source")
          return module_source(arguments);
        if (name == "tool_define")
          return define_tool(arguments);
        if (name == "tool_registry")
          return tool_registry();
        for (const auto &definition : lua_tools_.array()) {
          if (string_field(definition, "name") != name)
            continue;
          const auto origin =
              program_origin(operation_trace_, turn_trace_, generation_);
          const auto revision_size = tools_revision_.size();
          const auto source_size = string_field(definition, "source").size();
          RawProgramCall tool_trace{3827, 3828, &origin,
                                    std::array{raw_native(revision_size),
                                               raw_text(tools_revision_),
                                               raw_native(source_size)}};
          return tool_trace.run([&] {
            validate_tool_arguments(
                {.schema = field(definition, "parameters"), .arguments = arguments});
            log_.original(
                {"tool.source", string_field(definition, "source"),
                 Value::object({{"name", Value{name}},
                                {"revision", Value{tools_revision_}},
                                {"attempt", Value{hex_identity(attempt.bytes())}}})});
            const auto wrapper_origin = tool_trace.context();
            return runtime_->invoke_tool({.definition = definition,
                                          .arguments = arguments,
                                          .origin = &wrapper_origin});
          });
        }
        if (name == "restart") {
          const RawAdapterContext parent{
              nullptr,
              operation_trace_ ? std::optional{operation_trace_->reference()}
                               : std::nullopt,
              nullptr, nullptr};
          const std::uint8_t method = 1;
          RawSessionCall call{
              4420, 4421, &parent,
              std::array{raw_native(method), ByteView{generation_.bytes()}}};
          return call.run([&] {
            const auto &borrowed = string_field(arguments, "note");
            (void)raw_restart_source(call, 1, borrowed);
            auto note = borrowed;
            if (note.empty() || note.size() > 65536)
              throw Error{ErrorCode::invalid_range};
            raw_restart_mutation(
                call, 1, [&] { restart_note_ = std::move(note); }, 4423);
            return Value::object({{"scheduled", Value{true}}});
          });
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
          const RawAdapterContext origin{
              nullptr,
              operation_trace_ ? std::optional{operation_trace_->reference()}
                               : std::nullopt,
              nullptr, nullptr};
          RawSessionCall complaint_trace{
              4500, 4501, &origin,
              std::array{ByteView{attempt.bytes()}, ByteView{invocation.bytes()},
                         ByteView{admitted_generation.bytes()}}};
          return complaint_trace.run([&] {
            auto predicate = [&](RawSessionCall &owner, std::uint8_t kind,
                                 auto &&evaluate) {
              auto credit = owner.control(42);
              const bool value = evaluate();
              (void)owner.event(4503, std::array{raw_native(kind), raw_native(value)},
                                credit);
              return value;
            };
            auto hexadecimal = [&](RawSessionCall &owner, std::uint8_t role,
                                   const IdentityBytes &bytes) {
              auto native_credit = owner.control(57);
              auto source_credit = owner.source_credit(32);
              (void)owner.event(4505, std::array{raw_native(role), ByteView{bytes}},
                                native_credit);
              auto text = hex_identity(bytes);
              (void)owner.source(role, text, source_credit, 4502);
              return text;
            };
            auto borrowed = [&](std::uint8_t role,
                                const std::string &bytes) -> const std::string & {
              (void)complaint_trace.source(role, bytes, 4502);
              return bytes;
            };
            complaint_trace.stage(1);
            const auto &observation = string_field(arguments, "observation");
            (void)complaint_trace.source(1, observation, 4502);
            if (predicate(complaint_trace, 1, [&] { return observation.empty(); }) ||
                predicate(complaint_trace, 2,
                          [&] { return observation.size() > 65536; }))
              throw Error{ErrorCode::invalid_range};
            complaint_trace.stage(2);
            const auto references = arguments.find("references")
                                        ? field(arguments, "references")
                                        : Value::object({});
            if (!predicate(complaint_trace, 3, [&] {
                  return std::holds_alternative<Value::Object>(references.value());
                }))
              throw Error{ErrorCode::invalid_range};
            auto references_credit = complaint_trace.source_credit(ValueLimits{}.bytes);
            const auto encoded_references = unwrap(encode_packet_string(references));
            (void)complaint_trace.source(2, encoded_references, references_credit,
                                         4502);
            if (predicate(complaint_trace, 4,
                          [&] { return encoded_references.size() > 65536; }))
              throw Error{ErrorCode::invalid_range};
            complaint_trace.stage(3);
            std::set<std::string> active;
            for (const auto &unresolved : unwrap(log_.root().unresolved_attempts())) {
              auto credit = complaint_trace.control(56);
              (void)complaint_trace.event(
                  4504, std::array{ByteView{unresolved.admission.attempt.bytes()}},
                  credit);
              active.insert(hexadecimal(complaint_trace, 3,
                                        unresolved.admission.attempt.bytes()));
            }
            Value::Array active_ids;
            for (const auto &id : active) {
              auto credit = complaint_trace.control(50);
              const auto size = active_ids.size();
              const bool full = size == 64;
              const std::uint8_t kind = 5;
              (void)complaint_trace.event(
                  4503,
                  std::array{raw_native(kind), raw_native(full), raw_native(size)},
                  credit);
              if (full)
                break;
              active_ids.push_back(Value{id});
            }
            complaint_trace.stage(4);
            auto detail = Value::object(
                {{"observation", Value{observation}},
                 {"references", references},
                 {"active_attempts", Value{std::move(active_ids)}},
                 {"active_attempt_count", Value{Number{[&] {
                    auto credit = complaint_trace.control(48);
                    const auto size = active.size();
                    (void)complaint_trace.event(4506, std::array{raw_native(size)},
                                                credit);
                    return size;
                  }()}}},
                 {"revision", Value{borrowed(4, context_.head())}},
                 {"generation",
                  Value{hexadecimal(complaint_trace, 5, generation_.bytes())}},
                 {"actor",
                  Value{hexadecimal(complaint_trace, 6, identity_.actor.bytes())}},
                 {"conversation", Value{hexadecimal(complaint_trace, 7,
                                                    identity_.conversation.bytes())}},
                 {"workflow",
                  Value{hexadecimal(complaint_trace, 8, identity_.workflow.bytes())}},
                 {"model", Value{borrowed(17, model_)}},
                 {"effort", Value{borrowed(18, effort_)}},
                 {"audit_end", Value{Number{[&] {
                    auto credit = complaint_trace.control(48);
                    const auto count = log_.root().fact_count();
                    (void)complaint_trace.event(4507, std::array{raw_native(count)},
                                                credit);
                    return count;
                  }()}}},
                 {"reporting_attempt",
                  Value{hexadecimal(complaint_trace, 9, attempt.bytes())}},
                 {"repair_required_now", Value{false}}});
            auto detail_credit = complaint_trace.source_credit(ValueLimits{}.bytes);
            const auto encoded_detail = unwrap(encode_packet_string(detail));
            const auto detail_original =
                complaint_trace.source(10, encoded_detail, detail_credit, 4502);
            complaint_trace.stage(5);
            RawEventRef complaint_original;
            const auto complaint = [&] {
              const auto parent = complaint_trace.context();
              RawSessionCall retain{4510, 4511, &parent,
                                    std::array{ByteView{detail_original.lane},
                                               raw_native(detail_original.ordinal)},
                                    24};
              retain.stage(5);
              return retain.run([&] {
                auto locator_credit = retain.source_credit(32);
                auto locator = log_.original(
                    {"complaint.detail", encoded_detail,
                     Value::object({{"attempt", Value{hexadecimal(
                                                    retain, 9, attempt.bytes())}}})});
                complaint_original = retain.source(11, locator, locator_credit, 4502);
                retain.require_capture();
                retain.finish(RawOperationBranch::returned, nullptr,
                              std::array{ByteView{complaint_original.lane},
                                         raw_native(complaint_original.ordinal)});
                return locator;
              });
            }();
            complaint_trace.stage(6);
            constexpr char title_prefix[] = "Blackbird model complaint ";
            auto title_credit = complaint_trace.source_credit(
                raw_session_bound(sizeof(title_prefix) - 1, complaint.size()));
            auto title = title_prefix + complaint;
            const auto title_original =
                complaint_trace.source(12, title, title_credit, 4502);
            constexpr char description_prefix[] = "Local audit complaint ";
            constexpr char description_middle[] = "; conversation ";
            constexpr char description_suffix[] =
                ". Inspect complaint.detail locally. Advisory; no immediate repair "
                "required.";
            auto description_credit = complaint_trace.source_credit(raw_session_bound(
                raw_session_bound(
                    raw_session_bound(sizeof(description_prefix) - 1, complaint.size()),
                    sizeof(description_middle) - 1),
                raw_session_bound(32, sizeof(description_suffix) - 1)));
            auto description =
                description_prefix + complaint + description_middle +
                hexadecimal(complaint_trace, 7, identity_.conversation.bytes()) +
                description_suffix;
            const auto description_original =
                complaint_trace.source(13, description, description_credit, 4502);
            complaint_trace.stage(7);
            const auto parent = complaint_trace.context();
            RawSessionCall deliver{4520, 4521, &parent,
                                   std::array{ByteView{complaint_original.lane},
                                              raw_native(complaint_original.ordinal),
                                              ByteView{title_original.lane},
                                              raw_native(title_original.ordinal),
                                              ByteView{description_original.lane},
                                              raw_native(description_original.ordinal)},
                                   1};
            deliver.stage(7);
            bool delivered = false;
            auto delivery = deliver.run([&] {
              auto acquired =
                  call("exec",
                       Value::object(
                           {{"argv",
                             Value{Value::Array{
                                 Value{"bd"}, Value{"create"}, Value{std::move(title)},
                                 Value{"--type"}, Value{"bug"}, Value{"--description"},
                                 Value{std::move(description)}, Value{"--json"}}}},
                            {"timeout_seconds", Value{Number{"20"}}},
                            {"output_max_bytes", Value{Number{"4096"}}}}));
              deliver.require_capture();
              complaint_trace.require_capture();
              complaint_trace.stage(8);
              deliver.stage(8);
              const auto *exit = acquired.find("exit_code");
              if (predicate(deliver, 6, [&] { return exit != nullptr; })) {
                auto text_credit = deliver.source_credit(21);
                const auto text = exit->number().text();
                (void)deliver.source(16, text, text_credit, 4502);
                delivered = predicate(deliver, 7, [&] { return text == "0"; });
              }
              deliver.finish(RawOperationBranch::returned, nullptr,
                             std::array{raw_native(delivered)});
              return acquired;
            });
            auto result =
                Value::object({{"complaint", Value{complaint}},
                               {"retained_locally", Value{true}},
                               {"bead_created", Value{delivered}},
                               {"delivery", delivery},
                               {"sink", Value{"external consumer not configured"}},
                               {"repair_required_now", Value{false}}});
            complaint_trace.stage(9);
            auto delivery_credit = complaint_trace.source_credit(ValueLimits{}.bytes);
            const auto encoded_delivery = unwrap(encode_packet_string(result));
            const auto delivery_original =
                complaint_trace.source(14, encoded_delivery, delivery_credit, 4502);
            complaint_trace.stage(10);
            RawSessionCall retain{4530, 4531, &parent,
                                  std::array{ByteView{delivery_original.lane},
                                             raw_native(delivery_original.ordinal)},
                                  24};
            retain.stage(10);
            retain.run([&] {
              auto locator_credit = retain.source_credit(32);
              const auto locator =
                  log_.original({"complaint.delivery", encoded_delivery,
                                 Value::object({{"complaint", Value{complaint}}})});
              const auto locator_original =
                  retain.source(15, locator, locator_credit, 4502);
              retain.require_capture();
              retain.finish(RawOperationBranch::returned, nullptr,
                            std::array{ByteView{locator_original.lane},
                                       raw_native(locator_original.ordinal)});
            });
            return result;
          });
        }
        if (name == "audit_inspect")
          return log_.inspect(field(arguments, "query"), attempt);
        if (name == "trajectory_read")
          return log_.trajectory(field(arguments, "query"), attempt);
        if (name == "variables_read")
          return log_.variables(field(arguments, "query"), attempt);
        if (name == "git_observe") {
          const RawAdapterContext raw_context{
              nullptr,
              operation_trace_ ? std::optional{operation_trace_->reference()}
                               : std::nullopt,
              nullptr, nullptr};
          return observations_->git(arguments, cancelled, &raw_context);
        }
        if (name == "participant_configure")
          return participants_->configure(arguments);
        if (name == "participant_read")
          return participants_->read(arguments);
        if (name == "participant_send")
          return participants_->send(
              arguments,
              {{},
               RawParticipantIdentities{attempt.bytes(), invocation.bytes(),
                                        admitted_generation.bytes()}},
              &domain_admission);
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
                                        admitted_generation.bytes()}},
              &domain_admission);
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
          return tasks_.edit(arguments, {}, &domain_admission);
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
        if (name == "read_file" && arguments.find("snapshot")) {
          if (arguments.find("path"))
            throw Error{ErrorCode::invalid_range};
          const auto &snapshot = field(arguments, "snapshot");
          if (!std::holds_alternative<Value::Object>(snapshot.value()) ||
              snapshot.object().size() != 2 || !snapshot.find("journal") ||
              !snapshot.find("sequence") ||
              !std::holds_alternative<std::string>(field(snapshot, "journal").value()))
            throw Error{ErrorCode::invalid_range};
          const auto journal =
              parse_id<AuditStreamId>(string_field(snapshot, "journal"));
          const auto sequence = command_number(snapshot, "sequence", UINT64_MAX);
          if (sequence == 0)
            throw Error{ErrorCode::invalid_range};
          const auto raw = unwrap(log_.root().source({journal, sequence}));
          const std::string bytes{reinterpret_cast<const char *>(raw.data()),
                                  raw.size()};
          auto result = file_content_presentation(bytes, arguments);
          result.object().emplace_back("snapshot", snapshot);
          return result;
        }
        std::optional<SourceReference> file_snapshot;
        LocalTools tools{[&](std::string_view label, std::string_view raw) {
          try {
            const auto source = log_.original(
                log_.issue(),
                {label, raw,
                 Value::object({{"attempt", Value{hex_identity(attempt.bytes())}}})});
            if (label == "file.read")
              file_snapshot = source;
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
        auto result = tools.run(name, arguments, nullptr, nullptr, &file_effect);
        if (name == "read_file" && file_snapshot)
          result.object().emplace_back(
              "snapshot",
              Value::object(
                  {{"journal", Value{hex_identity(file_snapshot->journal.bytes())}},
                   {"sequence", Value{Number{file_snapshot->sequence}}}}));
        if (name == "exec" && process_output)
          process_output("\n[exit " + field(result, "exit_code").number().text() +
                         "]\n");
        return result;
      },
      nullptr, name == "exec" ? &command_start : nullptr, &domain_admission,
      &file_effect);
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
void CodingEngine::validate_session_switch(const RawAdapterContext *context) const {
  const std::uint8_t method = 3;
  RawSessionCall call{4420, 4421, context,
                      std::array{raw_native(method), ByteView{generation_.bytes()}}};
  call.run([&] {
    if (raw_restart_predicate(
            call, 2, [&] { return participants_->active(); }, 4422) ||
        raw_restart_predicate(call, 3, [&] { return command_jobs_->active(); }, 4422))
      throw Error{ErrorCode::busy};
  });
}
void CodingEngine::validate_restart(const RawAdapterContext *context) const {
  const std::uint8_t method = 4;
  RawSessionCall call{4420, 4421, context,
                      std::array{raw_native(method), ByteView{generation_.bytes()}}};
  call.run([&] {
    if (raw_restart_predicate(
            call, 4, [&] { return uncontained_exec_recovery_; }, 4422))
      throw Error{ErrorCode::external_unknown};
    const auto parent = call.context();
    validate_session_switch(&parent);
    validate_protocol(context_.items());
  });
}
std::optional<std::string>
CodingEngine::take_restart_note(const RawAdapterContext *context) {
  const std::uint8_t method = 2;
  RawSessionCall call{4420, 4421, context,
                      std::array{raw_native(method), ByteView{generation_.bytes()}}};
  return call.run([&] {
    if (raw_restart_predicate(call, 1, [&] { return restart_note_.has_value(); }, 4422))
      (void)raw_restart_source(call, 1, *restart_note_);
    std::optional<std::string> result;
    raw_restart_mutation(
        call, 1, [&] { result = std::exchange(restart_note_, std::nullopt); }, 4423);
    return result;
  });
}
void CodingEngine::effort(std::string value) {
  if (value != "low" && value != "medium" && value != "high" && value != "xhigh" &&
      value != "max")
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
Error CodingEngine::claim_backstop(const RawAdapterContext *raw_context) {
  RawSessionCall claim{4710, 4711, raw_context, {}, 9};
  return claim.run([&] {
    auto predicate = [&](std::uint8_t kind, auto &&evaluate) {
      auto credit = claim.control(42);
      const bool value = evaluate();
      (void)claim.event(
          4703,
          std::array{detail::raw_native_field(kind), detail::raw_native_field(value)},
          credit);
      return value;
    };
    if (predicate(21, [&] { return turn_running_; }) ||
        predicate(22, [&] { return operation_depth_ != 0; }) ||
        predicate(23, [&] { return !failed_turn_; }) ||
        predicate(24, [&] { return backstop_claimed_; }) ||
        predicate(25, [&] { return failed_turn_->code == ErrorCode::interrupted; }) ||
        predicate(26, [&] { return cancelled && cancelled(); }) ||
        predicate(27, [&] { return uncontained_exec_recovery_; }) ||
        predicate(28, [&] { return !detail::locally_quiescent(); }) ||
        predicate(29, [&] { return log_.root().state() != JournalWriterState::live; }))
      throw Error{ErrorCode::conflict};
    auto mutation = claim.control(41);
    backstop_claimed_ = true;
    const std::uint8_t kind = 1;
    (void)claim.event(4706, std::array{detail::raw_native_field(kind)}, mutation);
    claim.finish(RawOperationBranch::returned, nullptr,
                 std::array{detail::raw_native_field(failed_turn_->code),
                            detail::raw_native_field(failed_turn_->detail)});
    return *failed_turn_;
  });
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
  const RawProgramContext origin{{}, generation_.bytes()};
  RawProgramCall trace{6, &origin};
  return trace.run([&] {
    if (prompt.empty())
      return false;
    const auto selection_origin = trace.context();
    const auto selected = workflows_->select(prompt, &selection_origin);
    if (!selected) {
      if (prompt.starts_with("/"))
        return false;
      turn({prompt, fallback});
      return true;
    }
    const auto invocation =
        Value::object({{"name", field(selected->definition, "name")},
                       {"arguments", Value{selected->arguments}},
                       {"prompt", Value{std::string{prompt}}},
                       {"trigger", Value{selected->trigger}},
                       {"origin", Value{"operator"}}});
    operator_arguments_ = invocation;
    const auto program = "return blackbird.call('workflow_invoke',_native_args)";
    turn({prompt, program});
    return true;
  });
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
  if (file_effect_unretained_) {
    branch = 1;
    terminal_error = Error{ErrorCode::incomplete};
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
    pending_program_trace_.finish(2);
    pending_tools_trace_.finish(2);
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
      for (auto &continuation : engine.workflow_continuations_)
        continuation.parked.finish(1);
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
      pending_program_trace_.finish(0, nullptr, trace.activation(0, program_revision_));
    if (activate_tools)
      pending_tools_trace_.finish(0, nullptr, trace.activation(1, tools_revision_));
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
