# Context byte budget: operating guide

## Policy and units

The native default is **enabled**, with `trigger_bytes: 393216` (384 KiB) and
`target_bytes: 196608` (192 KiB), chosen from measured campaign requests
([REQUEST_SIZES](../papers/clm-a1v-2026-10-19/REQUEST_SIZES.md)), under revision
`builtin-default`. A session that never chose a policy follows the current
default on reopen; that includes one recorded under the pre-2026-10-19
`initial-disabled` revision. An explicitly chosen policy is kept exactly.

Every request also carries one `CONTEXT_SIZE input_bytes=N entries=M
trigger_bytes=T target_bytes=G` line, or `budget=off`, in its developer
operational state, so the model always knows its size. A provider that retains
request state in context (today only Anthropic) gets the line only with the
trigger guidance, so stale sizes accumulate only while the trigger is
reached (see `arconaut-2o9`). `state_context = "omit"`
drops it.
This documentation session instead has an enabled **9000/4500-byte** policy;
those are session choices, not defaults.

When enabled, `input_bytes >= trigger_bytes` invites the model to inspect its
working map and propose managed compaction. Measurement is **serialized input
UTF-8 JSON bytes**, not tokens, total request/wire bytes, or a provider context
limit. The trigger does not reject requests or force shrinking. The target is
advisory: retaining instructions and repairing omissions may require growth.
Compaction changes presentation; it performs **no physical audit reclamation**.

## Inspect and propose

Tool API inspection (no mutation):

```json
{}
```

Pass that object to `context_budget`. Lua equivalent:

```lua
local v = arco.call('context_budget', {})
```

The view contains `revision`, `effective`, `pending`, `pending_revision`,
`triggered`, `input_bytes`, `units`, and `capability`. `revision` identifies the
**effective policy**, not the context revision. Pending fields are null when
there is no staged policy.

Example proposal to `context_budget` (replace the revision placeholder with the
inspection result):

```json
{
  "proposal": {
    "base": "<effective policy revision>",
    "enabled": true,
    "trigger_bytes": 9000,
    "target_bytes": 4500,
    "reason": "Opt-in model-authored working-map compaction"
  }
}
```

Executable Lua equivalent:

```lua
local v = arco.call('context_budget', {})
local r = arco.call('context_budget', {proposal = {
  base = v.revision, enabled = true,
  trigger_bytes = 9000, target_bytes = 4500,
  reason = 'Opt-in model-authored working-map compaction'
}})
assert(r.staged)
-- Finish this workflow successfully; r.effective is still the old policy.
```

`base` is optional; omitting it binds the current effective policy revision.
A stale explicit base returns `staged: false` with
`reason: "stale-policy-revision"`. Supply all four policy fields: boolean
`enabled`, positive integer thresholds at most 67108864 bytes (64 MiB),
`target_bytes < trigger_bytes`, and a nonempty reason at most 4096 bytes.
Unknown policy fields are rejected. To disable, propose `enabled: false` while
still supplying valid thresholds and a reason.

## Activation and durability

A valid proposal returns `staged: true` and a `pending_revision`; it does not
immediately change `effective` or `revision`. Successful workflow completion
publishes it; failure or interruption cancels pending changes. Finish the
workflow after staging, then inspect on the next turn rather than claiming the
proposal is already effective.

When managed context and policy are both pending, native settlement prepares
the resulting context and publishes the context change and effective-policy
record in one durable boundary append when the managed proposal is accepted.
A managed settlement conflict can reject that context proposal while the policy
still publishes at a successful boundary; inspect the settlement outcome.
Fallible preparation precedes publication; post-commit display is best-effort notification. A notification
exception does not undo committed work or turn it into cancellation. Effective
policy is recovered from committed `context-budget-effective-v1` program
records on reopen.

## Model-authored compaction and repair

Use `context_view` (Lua: `arco.context()`) to identify expendable assistant notes.
The model authors the summary and selects IDs; native structural acceptance is
not a certificate of summary accuracy. Preserve live instructions and complete
tool call/result groups. Retain source locations, current fixes, uncertainty,
and original entry IDs for later repair.

Example Lua managed proposal, with a real notes ID substituted:

```lua
local r = arco.call('context_manage', {proposal = {
  mode = 'summarize', ids = {'<assistant notes entry ID>'},
  reason = 'Condense working notes', source = 'Implementation notes',
  summary = {role = 'assistant', content = '<source-grounded summary>'}
}})
assert(r.staged)
```

Omitting the managed proposal's `base` binds its invocation snapshot. An explicit
base is the **context** revision and can become stale as tool exchanges append.
Managed changes also wait for successful workflow completion.

Recover omitted details with bounded `context_inspect` pages, not an unbounded
read of all originals. The default page format is the native framed BBM2 stream
(`BBMS\1` header, then eight-byte little-endian lengths and packets), which a
model cannot read. Ask for `"format": "json"` to page JSON text instead:

```json
{
  "query": {
    "kind": "originals",
    "entry": "3de1dba2e9e6fb751b00000000000000.0",
    "format": "json",
    "offset": 4500,
    "limit": 4000
  }
}
```

Either format returns the page as hex in `bytes`, plus `total_bytes`, `next`,
`revision` and `context_revision` (`src/context.cpp`, `ContextStore::inspect`). A
JSON slice may not be a complete document, so decode the hex and join pages as
needed. For subsequent pages, bind the returned `revision`; on conflict, restart
pagination. Append a recovered fact to the
working map with `arco.append`, or use managed `restore` when the full original
is needed. Repair can exceed the advisory target; retained originals remain
available even after removal from presentation.

## Capability provenance

`capability.provider` comes from native adapter identity, with an explicit
unavailable explanation for unspecified adapters. `capability.model` reports
the effective request model in request guidance, including overrides;
standalone inspection/stats uses the engine default. Read the accompanying
`provider_source` and `model_source`, rather than inferring identity from prose.
`context_window_tokens` is null: there is no qualified model capability catalog,
as stated by `context_window_source`. Do not invent a token window from the byte
policy.

## Source anchors

- `src/coding.cpp`: `default_context_budget`, `validate_context_budget`,
  `CodingEngine::budget_view`, `context_budget`, constructor replay, and `turn`.
- `src/context.cpp`: `ContextStore::finish_workflow` and boundary publication.
- `include/blackbird/coding.hpp`: engine API/state.
- `tests/context_budget_test.cpp`: policy, trigger, repair, combined-boundary,
  notification, and reopen oracles (not executed for this documentation task).

The historical review in `context/giga-campaign/budget-review.out` predates the
atomic combined-publication and best-effort notification fix; its ordering
finding must not be presented as the current implementation.
