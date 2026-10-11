# Raw hooks and bodies: complete current source census

2026-10-10 source census and proposed event families. Source baseline
`e75d57c4bb0beebba0698b3e12b590e9a6de057b`, unchanged during this study. The earlier
[product census](2026-10-10-instrumentation-product-surfaces.md) covered all46 C++
units and7 shipped Lua programs. Its60 source/owner rows are retained below;
its metric-registry/aggregation/callback architecture is superseded. The
[system census](2026-10-10-instrumentation-system-surfaces.md) supplies the nine
mechanical domains detailed below. This is the emission schema, not a list of
consumer metric names.

2026-10-11 status: the operator approved the raw architecture and implementation;
the first transport/recorder and initial hook unit is on master. This census
retains its study baseline, including the boundary defects below. Remaining
exact owner layouts, admission credit and oracles are in the
[completion plan](../docs/INSTRUMENTATION_COMPLETION.md); these rows do not claim
implemented coverage. The old LocalSpan mechanism described below is replaced.

## Classes, attachment and record forms

| Class | Exact hook/emission form | Interpretation after preservation |
| --- | --- | --- |
| L lifecycle/phase | Distinct begin/end records, actual owner identity or begin-record reference; each raw clock sample/domain copied separately | Interval, rate, incomplete activity |
| S state/publication | Request/stage/commit/activate/refuse as distinct sites; actual old/new revision/state or original locators | Transition populations, residence |
| D data boundary | Actual prepare/send/read/decode/import/present boundary; request and response are distinct; returned amount/status and original reference | Flow, byte rate, usage interpretation |
| Q queue/handoff | Enqueue/dequeue/deliver/withdraw/duplicate/refuse with the real transfer identity and owner epoch | Queue residence and occupancy |
| P coherent owner snapshot | Copy already-maintained state at an existing safe owner boundary; no callback, history walk, or new accumulator | Gauge/history; known freshness |
| R ownership/resource | Acquire/release/resize/publication with actual owner/resource incarnation and known requested/observed amount | Population and memory/work accounting |
| C authored domain event | Fixed native/Lua raw-event site; caller-supplied scalar/byte body, source generation and invocation | Program-specific meaning selected by consumer |
| F acquisition/failure | Attempt/return-error/status-unavailable with raw returned status and exact source boundary; independent transport failure path | Failure diagnosis, uncertainty |

Every fallible row includes F: errors/refusals/cancellation/unknown/partial work
are recorded at their actual boundary. A missing end is never fabricated as a
successful completion. The same semantic owner emits for native and Lua callers;
bridge observations describe bridge work separately. Tertiary subscribers do
not run in the owner process.

Each listed dot-separated family names separate static site descriptions.
Bodies are site-specific layouts, not dictionaries with a universal parent tree.
Use native16-byte identities where the owner already has `Id::bytes()`. Where
an existing owner uses a textual identity, copy those exact length/bytes; do not
parse/format it on the writer. Revisions/ordinals/lengths are native integers of
recorded width; statuses are the owner's actual enum/integer or raw string;
quantities keep their native type/unit. Byte tails have explicit lengths. A
source locator copies stream, record/range and owner generation already known.
No extra serialization, tree traversal, deduplication, unit conversion or clock
subtraction is performed to populate a body.

All fields below mean **already acquired/maintained fields**, not instructions
to compute a new aggregate. When a convenient quantity is absent, emit its
original/source reference or boundary records and derive it outside Blackbird.
Schema layouts and valid optional variants are fixed in the durable build/site
description before activation. They include acquisition-failure variants so a
failed measurement is distinguishable from a real zero value.

## Product owner sites (60)

| # | Owner and current source sites | Classes | Separate events and raw body fields |
| --- | --- | --- | --- |
| 1 | Native launch and session open: `src/main.cpp:60,211–363` | L/S | launch.request/open.return/ready; session identity, open/recovery mode, directory/stream identity, returned error |
| 2 | CLI/plain/station/TUI source selection: `src/main.cpp:173–209,903–1005` | S/D | input.accept/dispatch; source kind, retained input identity, actual raw length, selected interface |
| 3 | Composer submit/cancel/quit: `src/terminal.cpp:1012,2110–2123` | D/S | composer.submit/cancel/quit; input identity, foreground epoch, request kind, actual acceptance/refusal |
| 4 | Pending prompt queue: `src/terminal.cpp:1662–1679,2240–2258` | Q/P | prompt.enqueue/dequeue/withdraw/refuse; existing prompt identity, queue epoch, owner state, retained draft reference |
| 5 | UI state persistence: `src/terminal.cpp:1298–1324` | L/S | ui-save.begin/return; state revision, file identity, write/sync/rename stage, returned native result |
| 6 | Operator command classification and dispatch: `src/main.cpp:507–818`; `src/coding.cpp:2487,2505` | D/L | command.selected/dispatch/return; invocation, selected command/workflow identity, origin, branch/result |
| 7 | Model/effort/workflow/session settings: `src/main.cpp:673–816`; `src/session.cpp:194` | S | setting.request/stage/effective/refuse; setting identity, existing old/new revision/value bytes, scope |
| 8 | UI message post/drain: `src/terminal.cpp:1140,1552` | Q/D | ui-message.enqueue/dequeue; message identity/type, actual payload length, queue owner/epoch |
| 9 | Layout, paint and terminal transport: `src/chat_view.cpp:383,489,510,516`; `src/terminal.cpp:1695` | L/D/P | frame.begin/constructed/send/return; frame/terminal epoch, existing dimensions, requested/returned byte amount, EAGAIN/error |
| 10 | PTY keyboard routing: `src/terminal.cpp:1351–1408,1630–1654` | D/Q | keyboard.route/send/return; job identity, foreground epoch, actual input bytes/source, requested/accepted/refused amount |
| 11 | Presentation history trimming: `src/chat_view.cpp:207`; `src/terminal.cpp:609` | S/P | presentation.retire; row/source identities, actual retired ranges, already-owned size values |
| 12 | Session identity/settings/reopen: `src/session.cpp:182–205`; `src/main.cpp:218–363` | L/S/P | session.attach/recover/detach; session/process identity, selected saved root, recovery mode/result, unresolved references |
| 13 | Turn lifecycle: `src/coding.cpp:2525–2688` | L/S | turn.request/admit/start/end; invocation, source generation, existing context revision, actual settlement/error/unknown |
| 14 | Effective program/source generation: `src/coding.cpp:2584–2587` | S/D | source.pin/return; generation, immutable source reference, existing raw length, retention error |
| 15 | Lua VM creation/loading/evaluation/teardown: `src/coding.cpp:289,547–603` | L/D/P | vm.allocate/load/call/destroy; runtime instance, definition generation, native/Lua return/error, already-known allocation size |
| 16 | Lua-to-native bridge: `src/coding.cpp:421–542` | D/L | bridge.begin/decoded/transfer/returned; invocation, operation kind, source/result locator, existing byte/node quantities |
| 17 | Native operation admission/dispatch/outcome: `src/coding.cpp:1289–1638` | L/S | operation.request/refuse/admit/open/observe/retain/publish; exact attempt/invocation, effect kind, observed disposition/error, source/result locator |
| 18 | Workflow selection and deferral: `src/coding.cpp:2083–2113`; `src/workflows.cpp:124` | S/Q | workflow.select/enqueue/dequeue/withdraw; workflow/generation, invocation, queue transfer identity, disposition |
| 19 | Callable workflow execution: `src/coding.cpp:1640–1694` | L | workflow.begin/end/refuse; workflow/generation, invocation, begin reference, returned disposition/error |
| 20 | Program/module registry updates: `src/coding.cpp:728–816` | S/D | definition.validate/stage/activate/cancel/import; definition/generation, existing source reference, native result/cache state |
| 21 | Lua tool definitions/invocation: `src/coding.cpp:834–879,2117–2123` | S/L | tool.define/activate/invoke/return; tool/generation, invocation, source/schema reference, actual result/refusal |
| 22 | Default loop semantic phases: `programs/turn.lua:2–24` | C | program.mark; caller-supplied site and raw scalar/byte payload, executing generation/invocation (no subscriber callback) |
| 23 | Restart scheduling and handoff: `src/coding.cpp:2125`; `src/session.cpp:206–249`; `src/main.cpp:827,1148` | S/Q/L | restart.request/schedule/handoff; current/successor incarnation, retained token, custody/refusal, handoff result |
| 24 | Context append/edit/restore: `src/context.cpp:578,628,670` | S/D | context.request/stage/commit/refuse; old/new revision, item/source identities, exact changed ranges and existing raw lengths |
| 25 | Managed select/archive/summarize/restore: `src/context.cpp:881,970` | S/L | clm.select/archive/summary/restore; operation, definition/source/context revisions, input/output references, actual stage/result |
| 26 | Working presentation/original/history inspection: `src/context.cpp:408,427,689,1095` | L/D/P | context-read.begin/return; revision/query/page/source reference, actually returned rows/bytes, error |
| 27 | Context budget policy and advisory trigger: `src/coding.cpp:881–935,1811–1839` | S/P | budget.stage/effective/observe/advisory; policy/revision, already-assembled request byte length, configured threshold, actual branch |
| 28 | Protocol validation and stop placeholders: `src/coding.cpp:243,2167–2202,2669`; `src/context.cpp:786` | S/D | protocol.validate/refuse/placeholder; call/attempt identity, validation result, repaired slot references, actual unknown disposition |
| 29 | Context checkpoint and compact reopen: `src/context.cpp:363,400`; `src/main.cpp:235–260` | L/S/D | checkpoint.begin/publish/refuse/restore; selected revision/root/cursor, actual mode, existing encoded size, native result |
| 30 | Task edit batch: `src/tasks.cpp:582–620`; `TaskState::prepare`, `src/tasks.cpp:112` | S | task-edit.request/duplicate/conflict/commit; receipt/task identities, old/new revision, changed IDs/states, exact batch source |
| 31 | Task state reads/projection: `src/tasks.cpp:504`; `include/blackbird/tasks.hpp:70` | P/D | task-read.return/snapshot; task revision, returned projection/reference, existing maintained populations, actual error |
| 32 | Task activity binding: `src/coding.cpp:1292,1358–1382,2026`; `src/terminal.cpp:1203` | L/S | task.bind/activity; task identity, actual attempt/job/turn identity, owner-written activity state |
| 33 | Main request assembly: `src/coding.cpp:1696–1854` | L/D | request.prepare/refuse; request/group/attempt, context/tool/config revisions, original request reference and actual encoded length |
| 34 | Request group / retry policy: `src/coding.cpp:1855–1970` | S/Q/L | retry.select/wait.begin/wait.end/refuse; request group, attempt ordinal, prior response/error reference, configured wait, raw clock readings |
| 35 | Provider authorization/binding/refresh: `src/openai.cpp:123`; `src/provider_auth.cpp:476,721–814` | L/S | auth.resolve/refresh/return; binding/provider revision, action stage, actual disposition/error (no secret material acquired by this hook) |
| 36 | Provider outgoing request and transport: `src/openai.cpp:182–253`; `src/coding.cpp:1876` | D/L | request.send.begin/return/transport; request/attempt, original reference, raw syscall/HTTP status, actual sent amount |
| 37 | Provider raw stream / decode / preview: `src/coding.cpp:1894–1930`; `src/openai.cpp:255,297`; `src/stream_capture.hpp` | D/L | stream.read/decode/preview; request/response/stream identity, raw bounded chunk or original locator, returned byte amount, parser/item/presentation stage |
| 38 | Provider response receipt and acceptance: `src/coding.cpp:1931,1971–1999` | D/S | response.receive/import/publish/refuse; response/request/attempt, original report/model/usage bytes or locator, raw validity, context revision/result |
| 39 | Direct colleague preparation/admission: `src/colleague.cpp:148,267` | L/D | colleague.prepare/admit/send/refuse; request/attempt, profile/context revision, exact original request locator, local transport result |
| 40 | Direct colleague upstream response and wrapper result: `src/colleague.cpp:290–360,364` | D/S | colleague.response/wrapper.return; upstream response/request, original/report locator, CLI native status, parse/wrapper disposition |
| 41 | Participant start/configure/launch: `src/participants.cpp:316,329–390` | L/S/Q | participant.configure/admit/register/launch; run/request/attempt, config revision, exact admission locator, thread launch result |
| 42 | Participant request execution and worker state: `src/participants.cpp:104–219` | L/S/D | participant.request.begin/response/end/state; run/request/attempt, observed upstream response reference, actual worker state/error |
| 43 | Participant capture and owner drain: `src/participants.cpp:114–125,263–314` | Q/D | capture.receive/publish/preserve/refuse; run/request/source identity, raw bounded bytes/locator, actual stage/error, source observation vs drain boundary |
| 44 | Direction submit/deduplicate/queue/deliver: `src/participants.cpp:425–488,169–197` | Q/S | direction.request/duplicate/refuse/enqueue/dequeue/send/withdraw; delivery/run/request identities, original message locator, actual branch |
| 45 | Participant await/join/cancel/archive/shutdown: `src/participants.cpp:490–619` | L/Q/S/P | participant.cancel/wait/join/seal/archive/shutdown; run identity, request/ack boundary, actual terminal and raw begin/end readings |
| 46 | Auth account management / sign-in: `src/provider_auth.cpp:494,504,515,552,581,610,655`; `src/provider_oauth.cpp` | S/L | account.change/select/logout/device.begin/poll/return; binding/config revision, actual action/status/error, existing nonsecret provider identity |
| 47 | File read/range/presentation: `src/tools.cpp:179–188` | L/D | file-read.begin/read/present/return; file/source identity, requested range, actual syscall byte amount, returned slice/source locator |
| 48 | File write/edit: `src/tools.cpp:189–217` | L/S/D | file-edit.observe/validate/propose/write/sync/rename/return; attempt/file/source, exact old/proposed locators, raw native result/conflict |
| 49 | Command start/control/output/final disposition: `src/coding.cpp:2005–2052,1006–1127`; `src/command_jobs.cpp` | L/Q/D/S | command.admit/spawn/ready/control/output/terminal/preserve; job/attempt, foreground epoch, raw control/output/exit state and exact chunk locator |
| 50 | Retained command output retrieval: `src/coding.cpp:2129–2166` | L/D | output-read.begin/return; job/source/range/cursor, actually read/returned bytes, actual error |
| 51 | Beads read/write adapter: `src/beads.cpp:35,91`; `src/coding.cpp:2064` | L/D/S | beads.prepare/spawn/read/return; request/attempt, operation kind, raw stream locators/native status, mutation-possible disposition |
| 52 | Decision-model request/response: `src/decision_models.cpp:171,237,305`; `src/coding.cpp:2054` | L/D | decision.prepare/send/response/validate/return; request/attempt, source/report locator, raw returned model/usage fields, actual stage/error |
| 53 | Audit/context/trajectory/variable reads: `src/coding.cpp:2279–2294`; `src/audit.cpp` | L/D | query.begin/return; query/source/revision/page/cursor, actual returned rows/bytes, actual read error |
| 54 | Read-only Git observation: `src/coding.cpp:2297`; `src/observations.cpp` | L/D | git.request/read/return; invocation, original stdout/stderr/source locator, child native status, observed revision |
| 55 | Complaint local record and advisory delivery: `src/coding.cpp:2213–2278` | S/Q/D | complaint.retain/deliver/bead.return; complaint/request identity, local source reference, independent delivery and bead disposition |
| 56 | Source candidate/checkpoint/run/activation: `src/candidate.cpp:198,255–268,372–432,479` | S/L/Q | candidate.archive/lease/checkpoint/run/activate; candidate/source generation, exact checkpoint, lease/attempt, actual transition/refusal |
| 57 | Station control and event intake: `src/station.cpp:65,83,91,105`; `src/main.cpp:907–973` | S/Q/L | station.receive/validate/admit/dispatch/control/observe; event/feed/invocation, receipt/dedup identity, actual pause/control state/result |
| 58 | Explicit session recovery: `src/coding.cpp:1130–1266`; `src/main.cpp:260` | S/L | recovery.select/validate/commit/refuse; unresolved attempt/source, selected explicit mode, actual retained acknowledgement/fence |
| 59 | Backstop lineage/assessment/pivot/action: `src/backstop.cpp:57–288` | L/S/Q | backstop.claim/assess/pivot/action/observe/pause; lineage/claim/attempt/source, actual branch, artifact predicate result and artifact locator |
| 60 | Successor seeding / session switch: `src/context.cpp:452,558`; `src/main.cpp:698–728` | S/Q | successor.seed/switch/handoff; old/new session and source revisions, custody check, retained handoff, actual launch result |

## System domains and exact mechanical sites

Each grouped family below is a distinct site/body. High-frequency details are
not automatically global interceptors; hook owned operations at their actual
boundary. Once a site is included, every occurrence/failure is emitted. There
is no runtime sampling/filter decision on the writer.

| Domain / classes | Source owners | Separate sites and raw fields |
| --- | --- | --- |
| A. Representation/codec/bridge; L/D/R/F | `value.hpp:44–115`, `shared_sequence.hpp:23–27,70–83`, `foundation.cpp:82`, `packet.cpp:140,347–409`, `coding.cpp:123` and bridge helpers | `storage.allocate/return/share/detach/release`: owner/resource identity, requested known size, copied source range, returned allocation error; `radix.node/leaf.create/release`: kind, owner, known node size; `codec.begin/return`: direction/format, actual input/output size, already-maintained validation node/depth values and error; `bridge.begin/return`: actual source/result reference and native/Lua status. No shadow allocation totals or extra traversal. |
| B. Physical storage; L/D/S/F | `journal_storage.cpp:31,83,95,116,119,224`, `journal_writer.cpp:263–397` | `read/write.begin/return`: file incarnation, offset, requested size, actual signed syscall return, errno on failure; `sync.begin/return`: actual strength/platform method and result; `lease.acquire/release/return`: owner/file identity; `batch.admit/write/commit/preserve/refuse`: actual stream/sequence range, payload/frame/commit lengths already prepared, state/error; `writer.state`: old/new live/blocked/poisoned/recovery state. Storage hooks do not append recursively through the storage they observe. |
| C. Retention/index/archive/recovery; L/S/D/P/F | `retained_state.cpp:937,1115–1245,2038–2175`, `context.cpp:363,400`, `recovery_index.cpp:56,115,157,177,292,308`, `archive_catalog.cpp:151,194,231,259`, `environment_head.cpp:160`, `saved_state.cpp:191`, `journal_checkpoint.cpp:164` | `settlement.reserve/refresh/refuse`: attempt and existing allowance values; `replay.batch.accept/reject`: original batch/fact/source references; `checkpoint/catalog/index/root.begin/publish/refuse`: exact root/cursor/generation and actual syscall result; `lookup.read/return`: key/source/range, actual page/byte return, resident/archive selection; `restore.select/reconcile/return`: chosen mode and unresolved references; `resident.retire`: actual retired source/fact range. Existing counts may be copied; no replay-generated fresh executions or new work counters. |
| D. File/native child; L/D/R/F | `tools.cpp:8,24,179–217`, `native_process.hpp:59,307–335`, `process_lifetime.hpp:9` | `file.acquire/read/write/sync/rename/return`: file/attempt, exact range, raw requested/returned amount and error; `child.spawn/ready/send/read/eof/signal/reap`: owned child incarnation/group, channel, raw read bytes or original locator, native return/wait status; `poll.begin/return`: raw endpoints and status; `custody.acquire/release`: actual owned-child/uncontained state. No process-global child-resource delta mislabeled as per-child usage. |
| E. Command lifecycle; L/S/R/F | `command_jobs.cpp:222–263,311,577,594,619,653,932,972,986`, `command_job_runner.cpp` | `job.admit/spawn/bootstrap/ready`: job/attempt, pipe/PTY selection, native return; `leader.exit/stop/continue/reap/output.eof`: raw wait/PTY status and actual boundary; `deadline/stop.request/term/kill/forced-close`: actual requested control, disposition/error; `terminal.observe/preserve`: job/attempt and original terminal locator. Foreground wait end is separate from process exit. |
| F. Command flow/control; D/Q/S/F | collector/control/drain in `command_jobs.cpp`, owner wrappers `coding.cpp:1006–1127,2005–2052` | `output.read/enqueue/drain/preserve`: raw chunk/known offset, requested/returned bytes, channel/source and job; `stdin.enqueue/write/withdraw/refuse`: delivery/job identity, exact input reference, returned partial bytes/error; `resize.request/ioctl/return`: raw rows/cols and return; `signal.request/return`: target incarnation, requested signal, syscall result; `foreground.acquire/release`: job and routing epoch. Queue/current values are copied only if already owned. |
| G. Workers/queues/cancellation; Q/L/S/R/F | `participants.cpp:59–69,104–219,263–314,425–619`, spawn lock in `native_process.hpp`/`process_lifetime.hpp` | `worker.launch/start/end/join`: run/request/incarnation and actual result; `capture.receive/preserve/fail`: source range/raw bytes, retained cursor and failure; `inbox.enqueue/dequeue/withdraw/duplicate/refuse`: delivery/run identity and actual branch; `cancel.request/observe`: exact owner/request; `lock.wait/acquire/release`: only explicitly chosen owned synchronization boundaries, raw clock endpoints and lock identity. Prepay credit before locks; never wait on recorder under them. |
| H. Terminal/UI custody; R/Q/L/D/F | `terminal.cpp` terminal guards, worker/message/input/resize sites; `chat_view.cpp:383–516` | `terminal.acquire/raw-mode/restore/release`: terminal incarnation and native return; `ui-worker.start/join`: incarnation and actual result; `message.enqueue/drain`: message type/source/range; `frame.construct/send/return`: actual byte return/frame identity; `resize.observe/route`: raw dimensions, terminal/routing epoch. No invented operator-attention or seen-by-user metric. |
| I. Raw clocks/acquisition/channel; L/D/R/F | `foundation.hpp:255–304`, existing `local_timing.hpp/.cpp`, `observations.hpp/.cpp`, proposed raw lane/recorder | `clock.acquire/failed`: native method/domain, raw returned values/error; `sample.acquire/failed`: unchanged acquired native value/unit and method; `lane.register/close`: epoch/name/layout/capacity; `ingest.failed/write.failed/sync.failed`: original identity/range, raw error and progress in independent failure space. Recorder frames/commit cursor represent routine recording; no recursive self-metrics. |

## Resource samples, authored events and sensitive sources

Acquire OS resource samples in a separate instrumentation sampler where possible,
not by periodic callbacks in Blackbird. The sampler emits acquired raw values and
acquisition failures through the same recorder boundary. Ops/evals reading those
values are still consumers. Owner-only state is emitted at existing coherent
owner boundaries; a newly added timer/control loop in the main process is not
part of this proposal.

Lua exposes a narrow raw scalar/byte event call at an authored static site, not
registered counters, histograms, begin/end computation or subscriber callbacks.
It copies the argument's original type/value and executing source generation.
If a dynamic/custom value needs serialization not already present, that is a
separate reviewed authored-source interface; it cannot quietly use generic
`Value`/BBM encoding in the core writer. Product Lua execution already has its
own semantics; this facility does not make metric interpretation native.

Authorization hooks acquire only action/binding/status facts, not credentials.
Raw preservation does not mean inspecting secrets merely to emit them. Original
provider request/response bytes belong to their existing audited-source paths;
when included as raw evidence they are neither sanitized nor converted by the
metric writer. A locator is valid only once the original retention contract is
sound. Provider expiry, participant rejection and post-effect refusal are known
audit-completeness defects; emitting a locator cannot repair a missing original.

## Boundary changes required before integration

`LocalSpan` currently subtracts clocks, converts units, locks an8192-entry buffer,
and drops after it fills; shutdown emits an unsynced stream. Replace its active
instrumentation path with separate raw begin/end records, not an adapter that
serializes the aggregate. Its disabled-build behavior is useful historical
evidence, not the new completeness contract.

Participant capture currently throws at1024 events/8MiB; `drain` retains the
front only after a later owner call. Command collector output can mark capture
incomplete at its output limit. `StreamCapture` buffers64KiB and is explicitly
diagnostic. These are not qualified lossless original paths. Reserve/publish at
the actual observation boundary, backpressure reads before capacity is exceeded,
and retain until preservation. Keep cleanup/cancellation independent.

Decision-model observation currently records request after send; place distinct
prepare/send/response records at the actual boundaries. Main provider request and
response already have distinct audit records: test all native/worker/adaptor
paths rather than claiming that separation is universally absent.

Multiplayer, remote outposts, general feeds, unrestricted live plugin reload,
and future concurrency have no implemented owner here. They are prospective
sites and are not claimed as covered. New owners must supply the same applicable
raw families and reviewed causal identities. Shared services own their internal
metrics; Blackbird emits its client-side facts and reported service observations.
