> Unattributed proposal found untracked as `docs/MCP_CLIENT_SUBPLAN.md` (file time
> 2026-10-17 09:51) and filed here unchanged on 2026-10-19. Not adopted, reviewed or
> scheduled. Related scope: `arconaut-rhi.6` (versioned MCP tools/prompts/resources).

# MCP Client Subplan — Blackbird speaks MCP, depends on nothing

Status: proposal. The declared rule stands: no MCP dependency, no framework,
no SDK. This plan is about *capability*: Blackbird as a native MCP **client**
that treats MCP servers the way `beads` treats bd — an external world adapter,
admitted, audited, and bounded by house semantics.

## Why
The world is full of MCP servers (Linear, GitHub, Playwright, …) that encode
someone's integration work. Codex et al. use them freely. Blackbird without a
client is a colleague who can't hear half the room. With one, external state
(issues, PRs, tickets) flows into the same audited loop as local tool calls,
and Phase A adapters stop being one-off code.

## Precedents to reuse (do not clone)
- `beads.cpp` — external-executable adapter with explicit unknown-effect
  semantics; the disposition vocabulary to copy verbatim.
- `provider_oauth.cpp` — existing OAuth machinery; remote MCP servers (e.g.
  Linear) need OAuth 2.1 with resource indicators; reuse tokens/flow, add
  one audience/resource per server.
- `openai.cpp` HTTP/SSE paths — JSON-RPC over streamable HTTP rides the same
  transport discipline already shipped for providers.
- Admission/audit layer — every MCP tool call must become an admitted, journaled
  tool call like native tools; results obtainable by audit_ref; never implicit
  retry; UNKNOWN disposition retained (mirror exec's `effect_outcome`).

## Scope, in phases (each independently shippable)

### Phase 1 — skeleton client, deterministic transport
- `mcp.cpp`: MCP 2025-06-18 (JSON-RPC 2.0 over streamable HTTP/SSE) —
  `initialize`, `tools/list`, `tools/call`, `ping`. Nothing else.
- Config surface: `[mcp_servers.<name>] url = …` in the BB config; stdio
  servers explicitly out of scope here (later via native_process).
- Tool registration: dynamically discovered tools surface as
  `mcp_<server>_<tool>` with declared JSON-schema parameters; tool list is
  captured at workflow boundary — changes mid-session do not silently mutate
  the admitted surface (mirror program-config revision rules).
- Tested against a deterministic local stub server (fixtures + recorded
  conversations); no live account required (see NEXT_CAMPAIGN order-2 note).

### Phase 2 — auth + a real server
- OAuth flow for remote HTTP servers, reusing provider_oauth; token per server
  + resource parameter; refresh handling with the same observed-effort rules
  as provider refresh.
- Live integration target: Linear (`https://mcp.linear.app/mcp`) — exercises
  real OAuth, pagination, and boring-but-real usage from inside the session.
- Kill switch: per-server enable/disable; capability advertisement stays
  explicit (model sees only admitted servers).

### Phase 3 — hardening semantics
- Timeouts/reconnect: SSE stream drop is a *transport* event, not a call
  disposition; call effects reported with unknown-outcome rules (Phase A
  beads lesson: never convert an API exit into proof a side effect stopped).
- Result retention: MCP outputs journaled like process outputs; byte-bounded,
  audit-recoverable.
- Adverse conditions: slow server, malformed tool schema, mid-call server
  restart — each with explicit observed disposition, no silent retry.
- Non-goals (deferred, consciously): sampling, roots, resources, prompts,
  MCP-server-mode (serving our tools outward).

### Phase 4 — Lua/Lua-tool interop
- Lua workflows may call admitted MCP tools through the same guarded call
  surface used for blackbird.call; MCP tools become composable material for
  user-authored tools, which is where the namesake actually arrives.

## Discipline
One focused working session per phase (match NEXT_CAMPAIGN allowance style);
build → checks → restart-with-note; Phase 1 needs no network. The interim
curl/GraphQL bridge for Linear (consulting workspace) is scaffolding to be
retired the day Phase 2 binds, and doubles as a live differential test fixture.
