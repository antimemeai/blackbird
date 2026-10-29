# Lua5.5.1 for Oxcart's current Blackbird runtime

The operator explicitly permits deliberate pinned-dependency upgrades and a
Blackbird upgrade. Worktree work/oxcart-lua-2026-10-29 starts at master a94d094;
the active checkout and its work remain untouched. The native host-tool line
0a7e297 is two commits ahead of that master, with no intervening master changes;
advance this isolated branch to it before applying the Lua unit so Oxcart retains
its application tools. This is a runtime migration,
not a TUI feature tranche or a dependency installation request.

Read the official [5.5 migration section](https://www.lua.org/manual/5.5/manual.html#8)
and [version policy](https://www.lua.org/versions.html): clients must recompile
across minor versions. Read installed5.5.1 lua.h/lauxlib.h and owned VM/runtime
calls. Blackbird uses luaL_newstate for coding, two-argument lua_newstate in TUI
presentation and its authored-allocation fault test, and lua_resume for renderer
coroutines. It does not use the changed GC tuning controls or lua_dump. Owned
Lua files have no ordinary identifier named global. Existing coroutine, close,
raw authored-error and allocation tests remain the direct behavioral oracles.

Lua5.5 adds an explicit seed to lua_newstate. Use luaL_makeseed(nullptr), exactly
as documented for luaL_newstate, without replacing custom allocators or changing
their failure semantics. Nil errors now become strings in Lua itself; preserve
actual authored data and inspect any affected expectations instead of guessing
that the old shape survives. The installed library's luaconf.h enables its
default GLOBAL compatibility option; no application compatibility shim is added
and no all-compatibility-options-disabled run is claimed.

Plan: first retain the unchanged source's configure rejection of the installed
5.5.1 headers/library. Change the CMake gate to exact5.5.1, update both custom
constructor calls and the pinned development lua/luac paths. Keep the existing
LuaLS/StyLua5.4-compatible syntax checks for the unchanged owned language subset;
do not claim they model all5.5 syntax. Build and run the native suites, including
coding, authored errors/allocation, raw modules, TUI coroutine/presentation and
application/host tools. Inspect failures against the actual migration contract.
Check owned source lint and document the actual supported runtime/tooling.

Oxcart packages an isolated exported source tree and its private matching Lua
dylib. Record source/library/header hashes; preserve the earlier5.4 runtimes.
Actual native page capture, host-tool continuation and extraction/preview on
the resulting pinned combination close the integration loop. No production
source from references is copied, no Homebrew symlink is changed, and no model
session or active checkout is replaced.

The first full suite ran265 cases:256 passed, eight lacked the already-adopted
Microsoft terminal driver in this isolated worktree, and bridge_module_raw
rejected an actual LUA_TNONE measurement on __gc's absent second argument.
The diagnostic identifies site4825/reason3/type-1. The production producer
correctly emitted lua_type/lua_topointer without conversion. The test now
requires a missing type to occur only on that close reason with null pointer;
ordinary value types and supported object pointers keep their existing checks.
No production raw bytes or values are normalized. The direct5.5 API manual's
acceptable-index semantics explain the absent value. Install the same pinned
development driver in this worktree, then rerun those concrete failures.

All nine explicitly named cases pass after that restoration. The final full
266-case host-tool source gate found a separate PTY oracle waiting for repaint
bytes after a no-op composer clear, despite BG_3 already being visible in its
retained screen. The test now evaluates that actual state and drains any pending
frame before unchanged attribution checks. A targeted native PTY run passes;
no terminal behavior, fixed timeout or required output assertion is weakened.
