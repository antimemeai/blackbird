# Native application host tools, version 1

2026-10-23. Proposed extension for coordination with Oxcart. This is the
Blackbird-side implementation contract; Oxcart must implement and opt into it
before these tools are available in a live browser. Existing protocol-1 hosts
that omit `host_tools` keep their current command/event vocabulary.

## Catalog and negotiation

`open` optionally carries `host_tools: {version: 1, catalog: [...]}`. A catalog
entry has `name`, `description`, `arguments` and `result`. Names use provider
function-name characters (ASCII letters/digits/underscore/hyphen), at most 64
bytes, and cannot collide with native or effective/pending Lua tools. Catalogs
are connection-local and immutable until close; restored tool descriptions never
restore a host callback. At most 32 tools and 64 KiB of encoded catalog are
accepted. These are control metadata limits, not reading/capture limits.

Successful `ready` echoes `host_tools: {version: 1, names: [...]}`. Unsupported
versions, malformed catalogs and collisions fail opening before any turn. Hosts
must check this echo before advertising a connected operation. Omitting the
extension emits no new host-tool events.

Schemas are native values: `{kind: "object", fields: [...]}` for closed objects;
each field has `name`, `required` (boolean), and `schema`. Other kinds are
`string`, `integer`, `number`, `boolean`, `null`, and `array` (with `element`
schema). Empty objects use `fields: []`. Nested schemas have at most 16 levels
and 256 nodes per tool. Unknown schema fields/kinds, duplicate names and invalid
UTF-8 reject. Arguments must be objects. Provider JSON schemas are produced only
when assembling the provider tool catalog; IPC and retention use BBM2.

Example implemented Oxcart operation (illustrative, host must supply its actual
catalog): `oxcart_state`, arguments `{kind: "object", fields: []}`, result
`{kind: "object", fields: [{name: "reading_path", required: true,
schema: {kind: "string"}}]}`.

## Calls, progress and results

Blackbird emits `host_call` with `id` (owning application turn), `call_id`
(unique native operation-attempt identity), `invocation_id`, `generation`,
`tool`, and native `arguments`. The retained admission/input precedes dispatch.
The host owns browser/document/visit checks using its advertised arguments;
Blackbird does not retarget an operation after navigation.

The host sends `host_result` with its own unique command `id`, `turn_id`,
`call_id`, `outcome` (`success`, `error`, `cancelled`, `unknown`) and `result`
(native value). Successful values must match the catalog's result schema. Other
outcomes carry the actual failure/cancellation/uncertainty details in `result`.
The ordinary tool result includes the outcome, result, call/turn identities and
retained packet source. A failed browser operation is not a successful effect;
transport interruption after dispatch remains unknown. No automatic tool retry.

Optional `host_progress` uses a separate command `id`, matching `turn_id` and
`call_id`, and `text`. Blackbird retains the packet and forwards a `status`
event on the owning turn with `call_id` and text. Progress is observation, not
completion. No progress acknowledgement is required.

Only the one outstanding call accepts a result. Wrong-turn, unknown, duplicate
and late results/progress are retained and rejected using `error` on the
incoming command's ID with `final_follows: false`; they cannot complete another
call. The host must keep command IDs distinct from turn IDs so rejection cannot
retire an active turn in existing host clients. Malformed or schema-invalid
results do not settle the outstanding call; the host may submit a corrected
result with a new command ID, or cancel/close.

During a call Blackbird keeps pumping commands and native command observations.
Cancel, steer, close or EOF interrupts waiting. Before final, a connected host
receives `host_cancel` with the old turn `id` and `call_id`; it requests local
host cancellation, never claims undo. The old call retires locally with unknown
effect disposition when no actual result was received. Steering retains old
final-before-new-turn ordering. A later host result is an old observation, not
permission to resume or repeat the effect. Host jobs may outlive a callback;
return a real job handle and expose status/cancel tools when implemented.

Large observations remain complete files with adjacent blobs. Return their
locators and let normal Blackbird file tools read selected ranges or whole files.
The existing 4 MiB frame limit does not limit harvested data. Preserve originals;
never substitute a capped inline payload for a complete retained reading.

## Delivery and direct checks

Worktree: `context/oxcart-host-tools-2026-10-23`, branch
`work/oxcart-host-tools-2026-10-23`, base master `a94d094`. No new dependency,
MCP client/server, Oxcart browser executor or TUI change is part of this unit.
Oxcart's separate owner implements its catalog, shared operation dispatcher and
host event/result binding; coordinate this document before native browser use.

Direct defects/checks: reject unnegotiated/invalid catalogs and collisions;
validate nested native arguments/results; expose advertised tools to a scripted
provider; dispatch once through retained operation admission; inspect the exact
artifact with ordinary file tools and continue the provider loop; retain original
BBM2 replies; reject stale/duplicate/malformed results; process progress, steering,
cancel, close and disconnect during waiting without deadlock; preserve unknown
outcomes without replay. New regression oracles run RED on the old implementation
before source fixes. One code review/fixes and one affected recheck/fixes, no third
assurance layer.

Resources: an isolated native Debug build, focused native/CLI tests and changed
sanitizer cases, followed by the required full source commit gate. No provider
credentials or real inference for internal cases; no browser fixture or fleet
mutation campaign. A real Oxcart turn follows when its adapter exists and its
ordinary operator-selected browser is available. Runtime/source pins and live
results are recorded separately. The operator says Dolt is unused; do not start,
initialize or repair it as part of this delivery.
