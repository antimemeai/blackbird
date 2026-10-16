#include <lua.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
struct Owner {
  unsigned hook_calls = 0, resumes = 0;
  bool budget = false;
};
static void count_hook(lua_State *state, lua_Debug *) {
  auto *owner = *static_cast<Owner **>(lua_getextraspace(state));
  ++owner->hook_calls;
  if (owner->hook_calls >= 10) {
    owner->budget = true;
    (void)lua_yield(state, 0);
  }
}
static int install(lua_State *state) {
  luaL_requiref(state, "_G", luaopen_base, 1);
  lua_pop(state, 1);
  for (const auto *name : {"setmetatable", "getmetatable", "xpcall", "dofile",
                           "loadfile", "load", "collectgarbage", "print"}) {
    lua_pushnil(state);
    lua_setglobal(state, name);
  }
  return 0;
}
struct Case { const char *name, *source; int expected; };
int main() {
  std::printf("runtime=%s sizeof(lua_Integer)=%zu\n", LUA_RELEASE, sizeof(lua_Integer));
  constexpr std::array cases{
      Case{"plain-loop", "while true do end", LUA_YIELD},
      Case{"nested-pcall-loop", "while true do pcall(function() while true do end end) end", LUA_YIELD},
      Case{"nested-pcall-loop-in-pcall", "return pcall(function() while true do pcall(function() while true do end end) end end)", LUA_YIELD},
      Case{"finite-pcall", "local ok,value=pcall(function() local n=0; for i=1,20 do n=n+i end; return n end); assert(ok and value==210); return value", LUA_OK},
      Case{"source-error", "error('actual-source-error')", LUA_ERRRUN},
      Case{"no-finalizer-path", "assert(setmetatable==nil and getmetatable==nil and debug==nil and package==nil and coroutine==nil and xpcall==nil and load==nil and dofile==nil and loadfile==nil and collectgarbage==nil); return true", LUA_OK}};
  for (const auto &entry : cases) {
    lua_State *main_state = luaL_newstate();
    if (!main_state) return 91;
    lua_pushcfunction(main_state, install);
    if (lua_pcall(main_state, 0, 0, 0) != LUA_OK) return 92;
    lua_State *thread = lua_newthread(main_state);
    const int root = luaL_ref(main_state, LUA_REGISTRYINDEX);
    Owner owner;
    *static_cast<Owner **>(lua_getextraspace(thread)) = &owner;
    lua_sethook(thread, count_hook, LUA_MASKCOUNT, 1000);
    const int loaded = luaL_loadbufferx(thread, entry.source, std::strlen(entry.source), entry.name, "t");
    if (loaded != LUA_OK) return 93;
    int results = -1;
    ++owner.resumes;
    const int status = lua_resume(thread, nullptr, 0, &results);
    std::printf("case=%s load=%d status=%d resumes=%u hooks=%u budget=%d results=%d\n",
                entry.name, loaded, status, owner.resumes, owner.hook_calls, owner.budget, results);
    if (status != entry.expected || owner.resumes != 1 ||
        (status == LUA_YIELD && (!owner.budget || owner.hook_calls != 10 || results != 0))) return 94;
    const int reset = lua_resetthread(thread);
    std::printf("case=%s reset=%d\n", entry.name, reset);
    if (reset != LUA_OK && status != LUA_ERRRUN) return 95;
    luaL_unref(main_state, LUA_REGISTRYINDEX, root);
    lua_close(main_state);
  }
}
