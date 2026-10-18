# Local-file station and boundary operator control

G7's scoped station uses the same native session lock, participant/context and
ordinary Lua turn engine as campaign mode. Production is owned C++26/Lua5.4.8;
the adapter is local files, not a new service/framework. No inference while idle.

Create a trusted local adapter directory and atomically replace its files (write
another file then rename). Start the explicit release executable, not an older
published `arco-ui` generation:

```sh
mkdir -p context/station-feed
ARCO_EXECUTABLE="$PWD/build/release/arco" ./scripts/arco \
  --session context/station-worker --station context/station-feed
```

`events.json` is a source-owned JSON **array** (up to1024 events/1MiB):

```json
[{"source":"repository-build","id":"build-104","cursor":"commit/build-104",
  "prompt":"Inspect the recorded build failure and choose useful work."}]
```

All four strings are required. Source/id identify dispatch within this session;
cursor is retained source evidence, **not** a native cursor acknowledgement or
claim about an external feed's replay semantics. Prompt limit32768 bytes;
source/id256 each; cursor1024. Keep pending events in the snapshot until you have
observed admission in `station-status.json`. Snapshot order is source policy.
Only one event runs synchronously; controls are polled before each next event.
Paused/busy events remain source-owned, not an internal durable queue. Dropping
an unadmitted event from the source loses it. No automatic coalescing/debounce.

Admission is durably recorded **before** workflow dispatch; admitted input,
steering, cursor and original file bytes remain in the native audit. Repeating a
source/id, even with changed payload, does not dispatch again. Table limit4096
admissions; never evict unknowns or reset an ID to get another attempt. A distinct
new event is a new operation, not predecessor attempt authority.

`workflow_returned` means the Lua workflow returned, not that every local/remote
effect succeeded or that its output is correct. Any failure after admission stays
`unknown` and pauses new admissions; explicit resume still never replays that ID.
Reopen with any unknown also pauses. No crash/escaped-child containment claim;
operator must establish appropriate local lifetime/resource conditions before
explicit independent new work. A freed session lock is not quiescence evidence.

## Attach without a second conversation owner

Read `SESSION/station-status.json` from another terminal/process. It is a derived
atomic snapshot, not authority: PID, actor, context head, adapter, phase,
steering, pause/stop state and admitted outcomes. `session-info.json` supplies
conversation/workflow identity/settings. During a workflow the busy snapshot's
context head is the pre-dispatch boundary; it is not live context inspection.
Raw audit is private; ordinary audit/context APIs remain available to the worker.

Atomically replace `ADAPTER/control.json` with a new unique command ID:

```json
{"id":"operator-105","action":"pause"}
```

Actions: `pause`, `resume`, `stop`, `steer`. A steering command additionally has
`"text":"Prioritize a narrow source map; don't modify the build."` (32768bytes).
Controls apply at workflow boundaries, not as mid-request cancellation. Source
content is explicitly labelled untrusted; steering is local operator direction
presented in the next event prompt. ID reuse cannot repeat a control action.
The latest command file is **not** a command queue: wait for status to reflect a
command before overwriting it. Commands are retained with original file bytes.
Command table limit4096; native failure/interrupt pause does not need a table slot.

Pause leaves the same process/context listening. Resume admits pending source
events. Stop persists pause/stop and closes this worker only, never an external
service. A later explicit launch can observe a fresh resume command. SIGINT
requests existing native cancellation then persists pause; SIGKILL/crash is not
settled cancellation. Malformed/oversized/capacity inputs stop visibly and try to
retain native pause; an unwritable audit cannot be bypassed to dispatch work.

Native RRC restores the selected adapter and retained steering/control/admissions,
but **does not** perform an implicit `continue` turn. Only a newly admitted source
event runs a workflow. Ordinary explicit campaign launches clear station selection;
there is no inherited feed admission just because a session once ran as a station.
`--once`, backstop, seed and audit/list launches conflict with explicit station mode.
Use `--resume-continue` for its RRC; `--resume-once` conflicts with restored station.

Not delivered: network feed packages, live TUI attachment, async participant
scheduler, external cursor acknowledgements, remote effect reconciliation,
automatic retries or universal station/campaign transitions.
