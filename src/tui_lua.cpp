#include "blackbird/tui_lua.hpp"
#include "blackbird/display_text.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <lua.hpp>
#include <mutex>
#include <thread>
#include <type_traits>
#include <variant>

namespace blackbird::tui::lua {
namespace {
struct InboxIdentity {
  IdentityBytes bytes;
  std::uint64_t owner;
};
std::uint64_t fresh_owner() {
  static std::atomic<std::uint64_t> next{1};
  auto value = next.load(std::memory_order_relaxed);
  for (;;) {
    if (!value || value == UINT64_MAX)
      throw Error{ErrorCode::overflow};
    if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
      return value;
  }
}
template <std::size_t N> bool valid(const Text<N> &value, bool empty = false) noexcept {
  return value.size <= N && (empty || value.size != 0);
}
bool valid(const FixedTarget &value) noexcept {
  return valid(value.owner) && valid(value.kind) && valid(value.key) &&
         value.incarnation;
}
bool valid(const Dependencies &value) noexcept {
  return value.view.owner && value.view.incarnation && value.source_generation &&
         value.request_sequence;
}
bool valid(const PresentationInput &value) noexcept {
  if (!valid(value.stamp) || value.row_count > value.rows.size())
    return false;
  for (std::size_t n = 0; n < value.row_count; ++n)
    if (!valid(value.rows[n].key) || !valid(value.rows[n].label, true) ||
        !valid(value.rows[n].target))
      return false;
  for (std::size_t n = 0; n < value.detail.size(); ++n)
    if (value.detail_size[n] > value.detail[n].size() ||
        !valid(value.detail_titles[n], true))
      return false;
  return true;
}
template <std::size_t N> bool same(const Text<N> &a, const Text<N> &b) noexcept {
  return a.view() == b.view();
}
bool same(const FixedTarget &a, const FixedTarget &b) noexcept {
  return same(a.owner, b.owner) && same(a.kind, b.kind) && same(a.key, b.key) &&
         a.incarnation == b.incarnation;
}
bool same(const ActionEnvelope &a, const ActionEnvelope &b) noexcept {
  return a.key == b.key && a.stamp == b.stamp &&
         a.input_origin.lane == b.input_origin.lane &&
         a.input_origin.ordinal == b.input_origin.ordinal && same(a.target, b.target) &&
         same(a.handler, b.handler) && same(a.verb, b.verb);
}
template <std::size_t N> void copy(Text<N> &target, std::string_view text) noexcept {
  target.size = std::min(text.size(), N);
  std::copy_n(text.data(), target.size, target.bytes.data());
}
TargetRef native(const FixedTarget &target) {
  return {std::string{target.owner.view()}, std::string{target.kind.view()},
          std::string{target.key.view()}, target.incarnation};
}
} // namespace
struct Trigger::State {
  std::weak_ptr<const InboxIdentity> owner;
  ActionKey key;
  std::array<std::optional<ActionEnvelope>, 8> accepted{};
};
struct ActionInbox::State {
  explicit State(std::shared_ptr<const InboxIdentity> identity)
      : owner(std::move(identity)) {}
  std::shared_ptr<const InboxIdentity> owner;
  mutable std::mutex mutex;
  std::uint64_t next_trigger = 0, next_order = 0;
  std::array<std::optional<ActionEnvelope>, 8> pending{};
};
ActionKey Trigger::key(std::uint32_t ordinal) const noexcept {
  if (!state_)
    return {};
  auto result = state_->key;
  result.ordinal = ordinal;
  return result;
}
ActionInbox::ActionInbox(std::unique_ptr<State> state) : state_(std::move(state)) {}
ActionInbox::~ActionInbox() = default;
Result<std::shared_ptr<ActionInbox>> ActionInbox::create(IdentityBytes incarnation) {
  if (std::all_of(incarnation.begin(), incarnation.end(),
                  [](std::byte value) { return value == std::byte{0}; }))
    return Result<std::shared_ptr<ActionInbox>>::failure({ErrorCode::invalid_identity});
  try {
    auto identity = std::make_shared<const InboxIdentity>(
        InboxIdentity{incarnation, fresh_owner()});
    return Result<std::shared_ptr<ActionInbox>>::success(std::shared_ptr<ActionInbox>{
        new ActionInbox(std::make_unique<State>(std::move(identity)))});
  } catch (const std::bad_alloc &) {
    return Result<std::shared_ptr<ActionInbox>>::failure({ErrorCode::allocation});
  } catch (const Error &e) {
    return Result<std::shared_ptr<ActionInbox>>::failure(e);
  }
}
Result<Trigger> ActionInbox::new_trigger() noexcept {
  try {
    auto token = std::make_shared<Trigger::State>();
    const std::unique_lock lock{state_->mutex, std::try_to_lock};
    if (!lock.owns_lock())
      return Result<Trigger>::failure({ErrorCode::busy});
    if (state_->next_trigger == UINT64_MAX)
      return Result<Trigger>::failure({ErrorCode::overflow});
    token->owner = state_->owner;
    token->key = {state_->owner->bytes, state_->owner->owner, ++state_->next_trigger,
                  0};
    return Result<Trigger>::success(Trigger{std::move(token)});
  } catch (const std::bad_alloc &) {
    return Result<Trigger>::failure({ErrorCode::allocation});
  } catch (...) {
    return Result<Trigger>::failure({ErrorCode::external_unknown});
  }
}
bool ActionInbox::owns(const Trigger &trigger) const noexcept {
  return trigger.state_ && trigger.state_->owner.lock() == state_->owner;
}
Result<void> ActionInbox::accept(const ActionEnvelope &envelope,
                                 const Trigger &trigger) noexcept {
  if (!owns(trigger) || envelope.key != trigger.key(envelope.key.ordinal) ||
      envelope.key.ordinal >= 8)
    return Result<void>::failure({ErrorCode::invalid_identity});
  try {
    const std::unique_lock lock{state_->mutex, std::try_to_lock};
    if (!lock.owns_lock())
      return Result<void>::failure({ErrorCode::busy});
    auto &history = trigger.state_->accepted[envelope.key.ordinal];
    if (history)
      return same(*history, envelope) ? Result<void>::success()
                                      : Result<void>::failure({ErrorCode::conflict});
    const auto free = std::find_if(state_->pending.begin(), state_->pending.end(),
                                   [](const auto &entry) { return !entry; });
    if (free == state_->pending.end())
      return Result<void>::failure({ErrorCode::capacity});
    if (state_->next_order == UINT64_MAX)
      return Result<void>::failure({ErrorCode::overflow});
    auto accepted = envelope;
    accepted.acceptance_order = ++state_->next_order;
    history = accepted;
    *free = accepted;
    return Result<void>::success();
  } catch (...) {
    return Result<void>::failure({ErrorCode::external_unknown});
  }
}
Result<ActionPage> ActionInbox::try_read() const noexcept {
  try {
    const std::unique_lock lock{state_->mutex, std::try_to_lock};
    if (!lock.owns_lock())
      return Result<ActionPage>::failure({ErrorCode::busy});
    std::array<const ActionEnvelope *, 8> ordered{};
    std::size_t count = 0;
    for (const auto &entry : state_->pending)
      if (entry)
        ordered[count++] = &*entry;
    std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(count),
              [](const auto *a, const auto *b) {
                return a->acceptance_order < b->acceptance_order;
              });
    ActionPage page;
    page.size = count;
    for (std::size_t n = 0; n < count; ++n)
      page.entries[n] = *ordered[n];
    return Result<ActionPage>::success(page);
  } catch (...) {
    return Result<ActionPage>::failure({ErrorCode::external_unknown});
  }
}
Result<void> ActionInbox::acknowledge(ActionKey key) noexcept {
  try {
    const std::unique_lock lock{state_->mutex, std::try_to_lock};
    if (!lock.owns_lock())
      return Result<void>::failure({ErrorCode::busy});
    for (auto &entry : state_->pending)
      if (entry && entry->key == key) {
        entry.reset();
        return Result<void>::success();
      }
    return Result<void>::failure({ErrorCode::invalid_identity});
  } catch (...) {
    return Result<void>::failure({ErrorCode::external_unknown});
  }
}
Result<std::shared_ptr<const Script>> Script::create(std::string source) {
  if (source.size() > source_limit)
    return Result<std::shared_ptr<const Script>>::failure({ErrorCode::capacity});
  try {
    return Result<std::shared_ptr<const Script>>::success(
        std::shared_ptr<const Script>{new Script(std::move(source))});
  } catch (const std::bad_alloc &) {
    return Result<std::shared_ptr<const Script>>::failure({ErrorCode::allocation});
  }
}
namespace detail {
enum class Phase { module, compose, preview, handler };
enum class Api : lua_Integer {
  rows,
  key,
  label,
  detail,
  collection,
  item,
  inspector,
  request,
  find,
  detail_title
};
extern "C" {
static void *presentation_allocate(void *const ud, void *ptr, std::size_t osize,
                                   std::size_t nsize) noexcept;
}
struct Invocation {
  const ComposeRequest *composition = nullptr;
  const HandlerRequest *handler = nullptr;
  ActionInbox &inbox;
  Options options;
  const std::atomic_bool &stop;
  const std::atomic<std::uint64_t> &current_epoch;
  std::uint64_t epoch;
  Outcome outcome;
  SceneCandidate candidate;
  Phase phase = Phase::module;
  lua_State *state = nullptr;
  std::size_t heap = 0, items = 0, bytes = 0, rows = 0, detail_bytes = 0;
  std::uint64_t executed = 0;
  bool allocation_failed = false, native_failed = false;
  Completion interrupted = Completion::candidate;
  Error failure{ErrorCode::external_unknown};
  std::uint32_t ordinal = 0;
  Invocation(const ComposeRequest &request, ActionInbox &actions, Options limits,
             const std::atomic_bool &stopping, const std::atomic<std::uint64_t> &latest,
             std::uint64_t generation)
      : composition(&request), inbox(actions), options(limits), stop(stopping),
        current_epoch(latest), epoch(generation), outcome(request.input.stamp) {
    init();
  }
  Invocation(const HandlerRequest &request, ActionInbox &actions, Options limits,
             const std::atomic_bool &stopping, const std::atomic<std::uint64_t> &latest,
             std::uint64_t generation)
      : handler(&request), inbox(actions), options(limits), stop(stopping),
        current_epoch(latest), epoch(generation), outcome(request.stamp) {
    init();
  }
  void init() {
    candidate.view = outcome.stamp.view;
    candidate.base_revision = outcome.stamp.scene_revision;
  }
  ~Invocation() {
    if (state)
      lua_close(state);
  }
  const Script &script() const noexcept {
    return composition ? *composition->script : *handler->script;
  }
  void failed(Error error) noexcept {
    if (!native_failed) {
      native_failed = true;
      failure = error;
    }
  }
  static Invocation &self(lua_State *L) noexcept {
    return **static_cast<Invocation **>(lua_getextraspace(L));
  }
  static void hook(lua_State *L, lua_Debug *) {
    auto &call = self(L);
    if (call.stop.load(std::memory_order_acquire))
      call.interrupted = Completion::stopped;
    else if (call.composition &&
             call.current_epoch.load(std::memory_order_acquire) != call.epoch)
      call.interrupted = Completion::retired;
    else {
      call.executed += 1000;
      if (call.executed >= call.options.instructions)
        call.interrupted = Completion::instruction_budget;
    }
    if (call.interrupted != Completion::candidate)
      (void)lua_yield(L, 0);
  }
  static bool string(lua_State *L, int index, std::string_view &value,
                     std::size_t maximum, bool empty = true) noexcept {
    if (lua_type(L, index) != LUA_TSTRING)
      return false;
    std::size_t size = 0;
    const char *data = lua_tolstring(L, index, &size);
    if (size > maximum || (!empty && !size))
      return false;
    value = {data, size};
    return true;
  }
  struct IndexRange {
    int argument;
    std::size_t maximum;
  };
  static bool index(lua_State *L, IndexRange range, std::size_t &value) noexcept {
    const auto at = range.argument;
    const auto maximum = range.maximum;
    if (!lua_isinteger(L, at))
      return false;
    const auto number = lua_tointeger(L, at);
    if (number < 1 || static_cast<lua_Unsigned>(number) > maximum)
      return false;
    value = static_cast<std::size_t>(number - 1);
    return true;
  }
  void charge(std::size_t count, std::size_t text) {
    if (count > scene_limits.items - items || text > scene_limits.bytes - bytes)
      throw Error{ErrorCode::capacity};
    items += count;
    bytes += text;
  }
  void node(std::string_view key) {
    if (candidate.nodes.size() >= scene_limits.nodes)
      throw Error{ErrorCode::capacity};
    for (const auto &entry : candidate.nodes)
      if (entry.key == key)
        throw Error{ErrorCode::invalid_identity};
  }
  static std::string label(std::string_view text) {
    std::string safe;
    const auto lines = terminal_lines(text, std::numeric_limits<std::size_t>::max());
    for (const auto &line : lines) {
      if (!safe.empty())
        safe += ' ';
      safe += line;
    }
    return safe;
  }
  struct Returned {
    enum class Kind { none, integer, text, boolean } kind = Kind::none;
    lua_Integer integer = 0;
    std::string_view text;
    bool boolean = false;
  };
  Returned call(lua_State *L, Api api) {
    if (native_failed)
      return {};
    if (stop.load(std::memory_order_acquire)) {
      interrupted = Completion::stopped;
      throw Error{ErrorCode::interrupted};
    }
    if (composition && current_epoch.load(std::memory_order_acquire) != epoch) {
      interrupted = Completion::retired;
      throw Error{ErrorCode::interrupted};
    }
    const auto argc = lua_gettop(L);
    Returned returned;
    const auto invalid = [] { throw Error{ErrorCode::invalid_range}; };
    const auto key_arg = [&](int at, std::size_t cap = 64) {
      std::string_view value;
      if (!string(L, at, value, cap, false))
        invalid();
      return value;
    };
    if (api == Api::rows) {
      if (argc != 0)
        invalid();
      returned.kind = Returned::Kind::integer;
      returned.integer =
          composition ? static_cast<lua_Integer>(composition->input.row_count) : 0;
      return returned;
    }
    if (api == Api::key || api == Api::label || api == Api::detail ||
        api == Api::detail_title) {
      std::size_t at = 0;
      const auto count = (api == Api::detail || api == Api::detail_title) ? 2
                         : composition ? composition->input.row_count
                                       : 0;
      if (argc != 1 || !composition || !index(L, {1, count}, at))
        invalid();
      returned.kind = Returned::Kind::text;
      if (api == Api::key)
        returned.text = composition->input.rows[at].key.view();
      else if (api == Api::label)
        returned.text = composition->input.rows[at].label.view();
      else if (api == Api::detail_title)
        returned.text = composition->input.detail_titles[at].view();
      else
        returned.text = {
            reinterpret_cast<const char *>(composition->input.detail[at].data()),
            composition->input.detail_size[at]};
      return returned;
    }
    if (api == Api::collection) {
      std::string_view title;
      if (argc != 2 || !string(L, 2, title, 512))
        invalid();
      const auto key = key_arg(1);
      node(key);
      auto safe = label(title);
      charge(0, 2 * key.size() + safe.size());
      candidate.nodes.push_back({std::string{key}, {}, NodeKind::collection, true});
      candidate.collections.push_back({std::string{key}, {}, std::move(safe), {}});
      returned.kind = Returned::Kind::integer;
      returned.integer = static_cast<lua_Integer>(candidate.collections.size());
      return returned;
    }
    if (api == Api::item) {
      std::size_t handle = 0, at = 0;
      std::string_view value;
      if (argc != 4 || !composition ||
          !index(L, {1, candidate.collections.size()}, handle) ||
          !string(L, 3, value, 512) || !index(L, {4, composition->input.row_count}, at))
        invalid();
      const auto key = key_arg(2, 128);
      auto &collection = candidate.collections[handle];
      if (rows >= 8)
        throw Error{ErrorCode::capacity};
      for (const auto &entry : collection.items)
        if (entry.key == key)
          throw Error{ErrorCode::invalid_identity};
      auto safe = label(value);
      auto target = native(composition->input.rows[at].target);
      charge(1, key.size() + safe.size() + target.owner.size() + target.kind.size() +
                    target.key.size());
      collection.items.push_back(
          {std::string{key}, std::move(safe), std::move(target)});
      ++rows;
      return returned;
    }
    if (api == Api::inspector) {
      std::string_view title, value;
      if (argc != 3 || !string(L, 2, title, 512) || !string(L, 3, value, 16384))
        invalid();
      if (value.size() > 16384 - detail_bytes)
        throw Error{ErrorCode::capacity};
      const auto key = key_arg(1);
      node(key);
      auto safe = label(title);
      Inspector inspector{std::string{key}, std::move(safe), {}, {}, {}, 0};
      const auto lines = terminal_lines(value, std::numeric_limits<std::size_t>::max());
      charge(0, 2 * key.size() + inspector.title.size());
      for (const auto &line : lines) {
        charge(2, line.size());
        inspector.lines.push_back({{line, Ink::normal}});
      }
      detail_bytes += value.size();
      candidate.nodes.push_back({std::string{key}, {}, NodeKind::inspector, true});
      candidate.inspectors.push_back(std::move(inspector));
      return returned;
    }
    if (api == Api::request) {
      if (phase != Phase::handler || !handler)
        throw Error{ErrorCode::unsupported};
      const auto verb = key_arg(1);
      if (argc != 1)
        invalid();
      if (ordinal >= 8)
        throw Error{ErrorCode::capacity};
      ActionEnvelope envelope{
          handler->trigger.key(ordinal), handler->stamp, 0, handler->input_origin,
          handler->frozen_target,        handler->name,  {}};
      copy(envelope.verb, verb);
      const auto accepted = inbox.accept(envelope, handler->trigger);
      if (!accepted.has_value())
        throw accepted.error();
      ++ordinal;
      ++outcome.accepted_requests;
      returned.kind = Returned::Kind::boolean;
      returned.boolean = true;
      return returned;
    }
    if (api == Api::find) {
      std::string_view haystack, needle;
      if (argc != 2 || !string(L, 1, haystack, 16384) || !string(L, 2, needle, 4096))
        invalid();
      const auto found = haystack.find(needle);
      returned.kind = Returned::Kind::integer;
      returned.integer =
          found == std::string_view::npos ? 0 : static_cast<lua_Integer>(found + 1);
      return returned;
    }
    invalid();
    return returned;
  }
  static int api(lua_State *L) {
    auto &invocation = self(L);
    Returned returned;
    {
      try {
        returned =
            invocation.call(L, static_cast<Api>(lua_tointeger(L, lua_upvalueindex(1))));
      } catch (const std::bad_alloc &) {
        invocation.failed({ErrorCode::allocation});
      } catch (const Error &error) {
        invocation.failed(error);
      } catch (...) {
        invocation.failed({ErrorCode::external_unknown});
      }
    }
    if (invocation.native_failed) {
      lua_pushlightuserdata(L, &invocation);
      return lua_error(L);
    }
    switch (returned.kind) {
    case Returned::Kind::none:
      return 0;
    case Returned::Kind::integer:
      lua_pushinteger(L, returned.integer);
      return 1;
    case Returned::Kind::text:
      lua_pushlstring(L, returned.text.data(), returned.text.size());
      return 1;
    case Returned::Kind::boolean:
      lua_pushboolean(L, returned.boolean);
      return 1;
    }
    return 0;
  }
  static int setup(lua_State *L) {
    luaL_requiref(L, "_G", luaopen_base, 1);
    lua_pop(L, 1);
    constexpr const char *removed[] = {
        "_VERSION",     "dofile",         "loadfile", "load",   "getmetatable",
        "setmetatable", "collectgarbage", "print",    "warn",   "xpcall",
        "rawset",       "rawget",         "rawequal", "rawlen", "tostring"};
    for (const auto *name : removed) {
      lua_pushnil(L);
      lua_setglobal(L, name);
    }
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    // Strip the original table in place: strings retain it through __index.
    constexpr const char *strings[] = {"dump",  "find", "format",   "gmatch", "gsub",
                                       "match", "pack", "packsize", "rep",    "unpack"};
    for (const auto *name : strings) {
      lua_pushnil(L);
      lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
    lua_newtable(L);
    constexpr const char *names[] = {
        "rows", "key",       "label",   "detail", "collection",
        "item", "inspector", "request", "find",   "detail_title"};
    for (std::size_t n = 0; n < std::size(names); ++n) {
      lua_pushinteger(L, static_cast<lua_Integer>(n));
      lua_pushcclosure(L, api, 1);
      lua_setfield(L, -2, names[n]);
    }
    lua_setglobal(L, "ui");
    auto &invocation = self(L);
    lua_State *thread = lua_newthread(L);
    *static_cast<Invocation **>(lua_getextraspace(thread)) = &invocation;
    lua_sethook(thread, hook, LUA_MASKCOUNT, 1000);
    return 1; // Parent stack roots coroutine for the whole protected call.
  }
  static int entry(lua_State *L) {
    auto &invocation = self(L);
    const auto source = invocation.script().source();
    int status = luaL_loadbufferx(L, source.data(), source.size(), "presentation", "t");
    if (status != LUA_OK)
      return lua_error(L);
    lua_callk(L, 0, 1, 0, loaded);
    return loaded(L, LUA_OK, 0);
  }
  static int loaded(lua_State *L, int, lua_KContext) {
    auto &invocation = self(L);
    if (lua_type(L, -1) != LUA_TTABLE || lua_getmetatable(L, -1)) {
      invocation.failed({ErrorCode::invalid_range});
      lua_pushlightuserdata(L, &invocation);
      return lua_error(L);
    }
    const int table = lua_absindex(L, -1);
    std::size_t count = 0;
    bool compose = false;
    lua_pushnil(L);
    while (lua_next(L, table)) {
      std::string_view name;
      if (++count > 9 || !string(L, -2, name, 64, false) ||
          lua_type(L, -1) != LUA_TFUNCTION) {
        invocation.failed({ErrorCode::invalid_range});
        lua_pushlightuserdata(L, &invocation);
        return lua_error(L);
      }
      if (name == "compose")
        compose = true;
      else {
        if (invocation.outcome.handler_count == invocation.outcome.handlers.size()) {
          invocation.failed({ErrorCode::capacity});
          lua_pushlightuserdata(L, &invocation);
          return lua_error(L);
        }
        copy(invocation.outcome.handlers[invocation.outcome.handler_count++], name);
      }
      lua_pop(L, 1);
    }
    std::sort(invocation.outcome.handlers.begin(),
              invocation.outcome.handlers.begin() +
                  static_cast<std::ptrdiff_t>(invocation.outcome.handler_count),
              [](const auto &a, const auto &b) { return a.view() < b.view(); });
    if (!compose) {
      invocation.failed({ErrorCode::invalid_range});
      lua_pushlightuserdata(L, &invocation);
      return lua_error(L);
    }
    if (invocation.native_failed) {
      lua_pushlightuserdata(L, &invocation);
      return lua_error(L);
    }
    if (invocation.handler) {
      invocation.phase = Phase::handler;
      const auto name = invocation.handler->name.view();
      lua_pushlstring(L, name.data(), name.size());
    } else {
      invocation.phase =
          invocation.composition->preview ? Phase::preview : Phase::compose;
      lua_pushliteral(L, "compose");
    }
    lua_rawget(L, table);
    if (lua_type(L, -1) != LUA_TFUNCTION) {
      invocation.failed({ErrorCode::invalid_identity});
      lua_pushlightuserdata(L, &invocation);
      return lua_error(L);
    }
    lua_callk(L, 0, 0, 0, finished);
    return 0;
  }
  static int finished(lua_State *, int, lua_KContext) { return 0; }
  Outcome run() {
    if (stop.load(std::memory_order_acquire)) {
      outcome.status = Completion::stopped;
      return std::move(outcome);
    }
    state = lua_newstate(presentation_allocate, this, luaL_makeseed(nullptr));
    if (!state) {
      outcome.status = Completion::allocation;
      outcome.native_error = {ErrorCode::allocation};
      return std::move(outcome);
    }
    *static_cast<Invocation **>(lua_getextraspace(state)) = this;
    lua_pushcfunction(state, setup);
    int status = lua_pcall(state, 0, 1, 0);
    if (status == LUA_OK) {
      lua_State *thread = lua_tothread(state, -1);
      lua_pushcfunction(thread, entry);
      int count = 0;
      status = lua_resume(thread, nullptr, 0, &count);
      if (status != LUA_OK && status != LUA_YIELD &&
          lua_type(thread, -1) == LUA_TSTRING) {
        std::size_t size = 0;
        const char *error = lua_tolstring(thread, -1, &size);
        copy(outcome.diagnostic, {error, size});
      }
    }
    if (allocation_failed || status == LUA_ERRMEM ||
        (native_failed && failure.code == ErrorCode::allocation)) {
      outcome.status = Completion::allocation;
      outcome.native_error = {ErrorCode::allocation};
    } else if (interrupted != Completion::candidate) {
      outcome.status = interrupted;
      outcome.native_error = {ErrorCode::interrupted};
    } else if (native_failed) {
      outcome.status = Completion::native_failure;
      outcome.native_error = failure;
    } else if (status != LUA_OK) {
      outcome.status = Completion::script_error;
      outcome.native_error = {ErrorCode::external_unknown, status};
    } else if (stop.load(std::memory_order_acquire))
      outcome.status = Completion::stopped;
    else if (composition && current_epoch.load(std::memory_order_acquire) != epoch)
      outcome.status = Completion::retired;
    else if (handler)
      outcome.status = Completion::handler_finished;
    else {
      outcome.status = Completion::candidate;
      outcome.candidate.emplace(std::move(candidate));
    }
    // All script execution returned; no user finalizer path exists in the profile.
    lua_close(state);
    state = nullptr;
    return std::move(outcome);
  }
};

extern "C" {
static void *presentation_allocate(void *const ud, void *ptr, std::size_t osize,
                                   std::size_t nsize) noexcept {

  auto &self = *static_cast<Invocation *>(ud);
  if (!ptr)
    osize = 0;
  if (!nsize) {
    std::free(ptr);
    self.heap -= osize;
    return nullptr;
  }
  if (osize > self.heap || nsize > self.options.heap_bytes - (self.heap - osize)) {
    self.allocation_failed = true;
    return nullptr;
  }
  auto *next = std::realloc(ptr, nsize);
  if (!next) {
    self.allocation_failed = true;
    return nullptr;
  }
  self.heap = self.heap - osize + nsize;
  return next;
}
}
static_assert(std::is_convertible_v<decltype(&presentation_allocate), lua_Alloc>);

} // namespace detail
struct PresentationOwner::State {
  State(std::shared_ptr<ActionInbox> actions, Options limits)
      : inbox(std::move(actions)), options(limits) {}
  ~State() {
    stop.store(true, std::memory_order_release);
    notify();
    if (worker.joinable())
      worker.join();
  }
  std::shared_ptr<ActionInbox> inbox;
  Options options;
  std::mutex mutex;
  std::atomic_flag wake = ATOMIC_FLAG_INIT;
  std::thread worker;
  std::atomic_bool stop = false, stopped = false;
  std::atomic<std::uint64_t> epoch = 0;
  std::optional<ComposeRequest> composition;
  std::array<std::optional<HandlerRequest>, 2> handlers{};
  std::size_t head = 0, count = 0;
  std::optional<Outcome> ready;
  bool last_handler = false;
  void notify() noexcept {
    (void)wake.test_and_set(std::memory_order_release);
    wake.notify_one();
  }
  void work() noexcept {
    for (;;) {
      // Clear before checking persistent state: notifications cannot fall in a
      // predicate-check/CV-wait gap, including the lock-free stop notification.
      wake.clear(std::memory_order_release);
      std::optional<std::variant<ComposeRequest, HandlerRequest>> job;
      std::uint64_t generation = 0;
      {
        const std::lock_guard lock{mutex};
        if (stop.load(std::memory_order_acquire))
          break;
        if (!ready) {
          generation = epoch.load(std::memory_order_relaxed);
          if (count && (!composition || !last_handler)) {
            auto &pending = handlers[head];
            if (!pending)
              std::terminate(); // Private ring count and slots must agree.
            job.emplace(std::in_place_type<HandlerRequest>, std::move(*pending));
            pending.reset();
            head = (head + 1) % handlers.size();
            --count;
            last_handler = true;
          } else if (composition) {
            job.emplace(std::in_place_type<ComposeRequest>, std::move(*composition));
            composition.reset();
            last_handler = false;
          }
        }
      }
      if (!job) {
        wake.wait(false, std::memory_order_acquire);
        continue;
      }
      auto result = std::visit(
          [&](const auto &request) {
            detail::Invocation invocation{request, *inbox, options,
                                          stop,    epoch,  generation};
            return invocation.run();
          },
          *job);
      {
        const std::lock_guard lock{mutex};
        ready.emplace(std::move(result));
      }
    }
    stopped.store(true, std::memory_order_release);
  }
};
PresentationOwner::PresentationOwner(std::unique_ptr<State> state)
    : state_(std::move(state)) {}
PresentationOwner::~PresentationOwner() = default;
Result<std::unique_ptr<PresentationOwner>>
PresentationOwner::create(std::shared_ptr<ActionInbox> inbox, Options options) {
  if (!inbox || !options.heap_bytes || options.heap_bytes > heap_limit ||
      options.instructions < 1000 || options.instructions > instruction_limit ||
      options.instructions % 1000)
    return Result<std::unique_ptr<PresentationOwner>>::failure(
        {ErrorCode::invalid_range});
  try {
    auto state = std::make_unique<State>(std::move(inbox), options);
    auto *owner = state.get();
    state->worker = std::thread{[owner] { owner->work(); }};
    return Result<std::unique_ptr<PresentationOwner>>::success(
        std::unique_ptr<PresentationOwner>{new PresentationOwner(std::move(state))});
  } catch (const std::bad_alloc &) {
    return Result<std::unique_ptr<PresentationOwner>>::failure({ErrorCode::allocation});
  } catch (...) {
    return Result<std::unique_ptr<PresentationOwner>>::failure(
        {ErrorCode::external_unknown});
  }
}
SubmitStatus PresentationOwner::try_compose(ComposeRequest request) noexcept {
  if (!request.script || !valid(request.input))
    return SubmitStatus::invalid;
  if (state_->stop.load(std::memory_order_acquire))
    return SubmitStatus::stopped;
  try {
    const std::unique_lock lock{state_->mutex, std::try_to_lock};
    if (!lock.owns_lock())
      return SubmitStatus::busy;
    if (state_->stop.load(std::memory_order_acquire))
      return SubmitStatus::stopped;
    const auto epoch = state_->epoch.load(std::memory_order_relaxed);
    if (epoch == UINT64_MAX)
      return SubmitStatus::invalid;
    state_->epoch.store(epoch + 1, std::memory_order_release);
    state_->composition = std::move(request);
    state_->notify();
    return SubmitStatus::accepted;
  } catch (...) {
    return SubmitStatus::invalid;
  }
}
SubmitStatus PresentationOwner::try_handle(HandlerRequest request) noexcept {
  if (!request.script || !valid(request.stamp) || !valid(request.name) ||
      !valid(request.frozen_target) || !state_->inbox->owns(request.trigger))
    return SubmitStatus::invalid;
  if (state_->stop.load(std::memory_order_acquire))
    return SubmitStatus::stopped;
  try {
    const std::unique_lock lock{state_->mutex, std::try_to_lock};
    if (!lock.owns_lock())
      return SubmitStatus::busy;
    if (state_->stop.load(std::memory_order_acquire))
      return SubmitStatus::stopped;
    if (state_->count == state_->handlers.size())
      return SubmitStatus::full;
    state_->handlers[(state_->head + state_->count) % state_->handlers.size()].emplace(
        std::move(request));
    ++state_->count;
    state_->notify();
    return SubmitStatus::accepted;
  } catch (...) {
    return SubmitStatus::invalid;
  }
}
PollStatus PresentationOwner::try_take(Outcome &outcome) noexcept {
  try {
    const std::unique_lock lock{state_->mutex, std::try_to_lock};
    if (!lock.owns_lock())
      return PollStatus::busy;
    if (state_->ready) {
      outcome = std::move(*state_->ready);
      state_->ready.reset();
      state_->notify();
      return PollStatus::ready;
    }
    if (!state_->stopped.load(std::memory_order_acquire))
      return PollStatus::empty;
    if (state_->count) {
      const auto &pending = state_->handlers[state_->head];
      if (!pending)
        std::terminate(); // Same guarded private ring invariant as worker removal.
      outcome = Outcome{pending->stamp};
      outcome.status = Completion::stopped;
      state_->handlers[state_->head].reset();
      state_->head = (state_->head + 1) % state_->handlers.size();
      --state_->count;
      return PollStatus::ready;
    }
    if (state_->composition) {
      outcome = Outcome{state_->composition->input.stamp};
      outcome.status = Completion::stopped;
      state_->composition.reset();
      return PollStatus::ready;
    }
    return PollStatus::stopped;
  } catch (...) {
    return PollStatus::busy;
  }
}
void PresentationOwner::request_stop() noexcept {
  state_->stop.store(true, std::memory_order_release);
  state_->notify();
}
bool PresentationOwner::quiescent() const noexcept {
  return state_->stopped.load(std::memory_order_acquire);
}
Result<void> publish(const Outcome &outcome, const Dependencies &current,
                     SceneHost &host) {
  if (outcome.stamp != current)
    return Result<void>::failure({ErrorCode::conflict});
  if (outcome.status != Completion::candidate || !outcome.candidate)
    return Result<void>::failure({ErrorCode::incomplete});
  return host.publish(*outcome.candidate);
}
} // namespace blackbird::tui::lua
