# Completing nls.2

2026-10-11. Source inspected on master at `f99ccb2`; initial instrumentation
source is `56a965c`. This is the proposed remaining sequence, with design gaps
distinguished from implementation and verification work. Implementation of the
raw architecture is already approved. Evals remain deferred.

## What is ready

The [raw design](INSTRUMENTATION_RAW_DESIGN.md) settles process separation,
native envelopes, lane identity, publication/reclaim, armed wakeups, protected
terminal/failure credit, recorder framing, synchronization, and process-death
recovery. The older literature/reference studies and the single design review
support those choices. Neither a replacement framework nor another broad
research campaign is needed.

The channel, audit component, supervisor/recorder, native clock/region hooks,
immutable descriptions, and application startup/shutdown are implemented.
Startup, restore, retained append, command admission/worker, terminal phase, and
provider-owner hooks are integrated. Provider-owner observations are not adapter
wire-send or raw-read coverage. The installed source gate passed 89 debug cases,
all 125 owned C++ translation units and the owned Lua checks. The actual launcher
passed Linux debug, release and ASan/UBSan. Component and process-failure results
have narrower claims in the [implementation note](../papers/2026-10-11-raw-runtime-implementation.md).

The [census](../papers/2026-10-10-raw-instrumentation-surfaces.md) classifies 60
product owner surfaces and nine system domains. It names event families and
available fields. It is not 60 completed byte layouts or 60 completed owner
subplans. The [existing plan](INSTRUMENTATION_PLAN.md) provides the integration
order and direct checks; the remaining owner details still need to be written.

## Designs still needed before their source changes

The first completion checkpoint is implemented and published on master as `3561c72`:
cancel/deadline-aware admission and RPC ownership, bootstrap/metadata recovery,
read-only discovery/reader, and one actual provider/native-child vertical.
Its reviewed subplans and direct results are
[admission](../papers/2026-10-11-raw-admission-subplan.md),
[metadata](../papers/2026-10-11-raw-metadata-recovery.md),
[reader](../papers/2026-10-11-raw-reader-subplan.md), and
[provider](../papers/2026-10-11-provider-raw-vertical-subplan.md).
The installed gate passes93/93 debug cases, all132 selected owned C++ translation
units and owned Lua diagnostics. All16 affected cases also pass Mac release and
ASan/UBSan, and Linux debug/release/ASan-UBSan. These are conceptual units, not
completed census rows by implication. The next owner units remain explicit work.

| Boundary | Existing machinery / concrete gap | Required owner design |
| --- | --- | --- |
| Capacity waits and cleanup | `raw_wait_data`, `raw_claim_control` and supervisor RPC now carry sticky cancellation descriptors and absolute owner deadlines; interrupted replies retain their ownership. Context retirement honors the same control. | Wire actual UI/worker cancellation into owner scopes, prepay each finite terminal sequence, and qualify owner cleanup under full lanes. The admission capability alone does not complete those integrations. No periodic main-process timer. |
| Raw reads and no-wait regions | The immediate emitter and held control claims exist; a successful space check is not an owner-wide reservation. | Calculate each bounded burst before entering a product lock or reading bytes. Establish sole ownership across the check/read/publish interval; handle reentrancy and yields explicitly. Unknown-size reads become bounded actual reads, not post-read capacity refusals. Record requested and returned amounts, errors, EOF and partial results separately. |
| Capacity selection | Current defaults are 64MiB data, 256KiB control and 128KiB failure per producer, with 128KiB recorder failure space. These are implemented configuration, not a measured workload sizing result. | Measure source rates, finite bursts, terminal sequences and recorder stalls on the first real vertical. Choose capacities and physical frame/batch allowances together. Define oversized acquisition refusal before effects. No silent fallback to dropping, sampling or shortening records. |
| Native/Lua authored events | Native emission accepts a static site and byte spans. There is no shipped Lua raw-event bridge or authored-site description interface. | Define site identity scoped to immutable source generation, description publication before activation, permitted native/Lua scalar and byte types, exact layouts, invocation linkage, size admission and failure behavior. Copy existing representations; no generic Value/BBM serialization, dictionaries, callback registry or metric arithmetic in the writer. |
| Resource acquisition | Raw clock acquisition exists. The census proposes a separate sampler for OS observations. | Specify which native observations are obtainable externally, their method/units/incarnation, coherent owner-only snapshots, and acquisition-failure records. Define sampler lifecycle and its emission lane independently of Blackbird execution. Select sampling cadence/control outside the main process. Do not invent counters or label process-wide observations as per-child usage. |
| Reader discovery and audit enumeration | The native reader and `blackbird-raw-reader` discover preserved run/segment prefixes, retain exact descriptions, expose native origins/bytes and stable restart cursors. Native/profile/source checks pass, including concurrent published snapshots. | Normal semantic inspection can reference this separate interface; no reader acquires the writer lease or changes D, retention or admission. Qualify whole-path consumer contention with performance workloads. Evaluator/scoring design is outside this work. |
| Bootstrap and metadata recovery | The candidate defines activation, independent registration/header/closure copies, bounded prepaid mapping acquisitions, unresolved missing sources and known closed-name cleanup. | Finish integrated qualification and actual runtime append/sync/reclaim cuts. Preserve damaged originals and strict legacy interpretation; never infer an empty lane from missing metadata. |

For each semantic owner, write the exact sites, body widths/optional variants,
existing identity sources, observation/admission boundaries, held credit and
all actual outcomes. Name an independent expected event/byte list for success,
refusal, exception, cancellation, partial and unresolved work where applicable.
Recheck current source symbols: census line numbers describe the older baseline.
Review those new details and their plan before implementing them. Settled transport
choices do not need another review merely because another owner adopts them.

## Remaining sequence

These groups assign every product census row once. Shared native mechanisms emit
their own mechanical facts; each caller still emits its own semantic facts. A
group is a sequencing aid: each actual owner gets a complete written subplan and
its own direct checks, rather than one agent receiving unrelated leftovers.

| Order / owners | Product census rows | Work and direct completion check |
| --- | --- | --- |
| 1. Runtime admission, recording and recovery | System I; shared machinery for every row | Resolve cancellation-safe waits and bootstrap discovery first. Finish actual runtime append/sync/ack/catalog cuts, forward/reverse wake interleavings, two parked contexts, hot-lane fairness and failure-space exhaustion/replenishment. Compare independently generated bytes/identities, exact failure bodies and D at the relevant cuts. Audit-component fake-I/O results do not substitute for runtime failure propagation. |
| 2. Provider and native child boundaries | 33–40, 46–48, 51–52, 54; system D and adapter boundaries | First implement one actual provider request/wire-read/native-child vertical. Then cover both owned and compatibility provider paths, retry/auth, colleague/decision adapters, file operations, Beads and Git callers. Capture existing external request bodies before send, raw response chunks before parsing/preview, actual syscall/HTTP/child results and separate linked request/response records. Do not copy credential-bearing curl configuration as a metric body. Child fixtures supply exact binary stdout/stderr, partial-input and exit/control facts; malformed responses, limits, transport failures and cancellation must retain already observed originals. |
| 3. Command output and control | 10, 49–50; systems E/F | Cover pipe/PTY launch, output, stdin partial writes/refusal, resize, signal, foreground routing, stop/continue, EOF/reap and terminal preservation. Reserve reads and terminal control before acquisition. Real children provide byte/status oracles under full lanes, cancellation and foreground changes; cleanup remains possible while ordinary admission is blocked. Foreground wait completion is separate from process exit. |
| 4. Participants | 41–45; system G | Cover worker binding, configuration/admission, request/response, capture/owner drain, direction delivery, cancel/join/archive/shutdown. Test the actual capture limit and retention failures with original bytes still retained; never wait for the recorder under the owner mutex or acquire ContextStore from a worker. Direction identities come from actual transfers, without claiming a global logical clock. |
| 5. Context, tasks and storage | 24–32, 53; systems A/B/C | Cover context/CLM changes and reads, budgets, validation/unknown placeholders, checkpoints, tasks/activity, retained-state indexing/archive and physical storage. Use exact original references only where originals are retained. Force conflicts, allocation/I/O/sync failures and reopen. Replay emits replay work rather than a fresh historical effect. Storage instrumentation cannot recursively append through the failing storage it observes. |
| 6. Programs, workflows and authored events | 13–22, 56, 59; bridge parts of A | Cover turn/source pinning, VM/bridge lifecycle, definition/tool activation, callable workflows, candidates and backstop. Implement the reviewed raw native/Lua authored interface. Exercise generation changes, retained descriptions, accepted/refused bodies, Lua/native errors and invocation linkage. Authored raw values remain uninterpreted until disk preservation. |
| 7. Session and operator surfaces | 1–9, 11–12, 23, 55, 57–58, 60; system H | Complete launch/reopen/settings, input/composer/queues, UI persistence/messages/frame transport, presentation retirement, restart/handoff, complaint, station and explicit recovery. Real CLI/PTY/station/reopen fixtures compare owner outcomes with preserved events, including short writes and failures. Read-only audit inspection creates no new capture run. |
| 8. External samples and consumption | Acquisition part of I; reader interface | Implement the reviewed sampler and read-only discovery interface. A stopped, slow or killed reader cannot filter, mutate or acknowledge raw events. Compare preserved bytes/cursors with and without the reader and under reader disk contention. Demonstrate native resource-acquisition errors as records. Ops/evals consume the output; no eval definitions or executions here. |
| 9. Workload sizing and cost | Actual paths above | Measure 64/256/4096-byte bodies, natural bursts, wrap/full lanes, raw clocks, awake/idle recorder and cold/warm pages. Report producer p50/p99/max, allocations/copies/RMW/syscalls/wakes, preserved-prefix lag, sustained throughput and recorder CPU. Measure real owner paths as well as the primitive. Use the results to finish capacity choices; preliminary enqueue medians do not qualify end-to-end performance. |

The eight surface classes remain lifecycle, state/publication, data boundary,
queue/handoff, coherent owner snapshot, resource ownership, authored domain event
and acquisition/failure. The table covers all of them, not merely timed regions.
System A–I are representation/codec/bridge; physical storage; retention/recovery;
file/native child; command lifecycle; command flow/control; workers/queues;
terminal/UI; and clocks/acquisition/channel.

## Current owner work

The second completion checkpoint is published as `2bda565`. It implements partition claims and splits, nonblocking
cleanup credit, native recorder failure/fairness cuts, an independent OS
sampler, live recorder add/retire, command control/output, provider authorization,
native/Lua authored capture and task ownership. Each has its written subplan and
one completed code/oracle attack. Finding-driven fixes include command terminals
after returned-value construction, full command/authored/auth/task reference and
native-field oracles, and removal of the task commit's post-append deque move
allocation on Linux. All10 affected Mac release and ASan/UBSan cases pass. Linux debug/release
pass all seven selected owner cases; ASan passes those same owners after the
actual static-Lua wrapping and sized-delete fixture corrections. The installed
source gate then rejects four of99 debug cases. The borrowed timing-lane lifetime,
canonical private auth fixture path, auth source capacity and selected native
provider metadata oracle are corrected. Cancellation now uses caller-held
AttemptOpen/adapter-receipt/no-child terminal regions paid before admission;
the body retains its own interruptible admission. Actual postadmission and
live-child cancellation fixtures check distinct native outcomes and ordinary
reopen. The late quit path passes unchanged PTY and native checks, Mac release
and ASan/UBSan. Its final Linux changed-path checks pass2/2 in debug, release and
ASan/UBSan (run-h7iaz0wu:12.28/9.50/22.36s). The installed source gate passes
99/99 debug cases in127.26s, all144 selected owned C++ translation units and the owned
Lua checks. The formatting rejection was corrected before that complete gate.

The command unit also removes ordinary registration/retirement's recorder
restart and history replay. A real512KiB PTY workload and precise recorder-death
cut exercise the native descriptor handoff, held outcomes and recovery path.
Death recovery still reads preserved history; live add/retire does not.

The next whole-owner plans have completed their one plan attacks and resolved
the findings: [provider exchange](../papers/2026-10-11-provider-adapter-raw-subplan.md),
[participants](../papers/2026-10-11-participant-owner-raw-subplan.md),
[ContextStore](../papers/2026-10-11-context-owner-raw-subplan.md), and
[turn ownership](../papers/2026-10-11-turn-owner-raw-subplan.md).
Provider and participant sources share a pre-acquisition owned buffer contract;
an uncommitted callback failure preserves the initialized extent with unknown
used length. Participant cancellation uses a native socket shutdown and706
preheld owner-cleanup bytes per Run. Context and turn plans distinguish actual
context settlement from actual program acceptance before native activation.
The third completion checkpoint, `ab23278`, implements those owners plus linked audit-query and native
command-wake hooks. Their genuine baselines, ONE whole code/oracle attacks and
finding fixes are retained in the matching papers. Context changes also repair
pending custody and accepted-state activation after fallible history pinning.
Participant repair covers reentry, overlapping leases, failed shutdown and armed
orphan reclamation; supported profiles caught and fixed delayed native-field
lifetimes. Provider emission preserves its existing encoded request before
configuration/header allocation. Turn capture precedes accepted task presentation;
its real child policy admission refusal preserves pending/activation state.
The existing ordinary operation receipt still lacks a canceled prepaid bank;
[operation17](../papers/2026-10-11-operation-owner-raw-subplan.md) explicitly owns
that next integration. This checkpoint is published on master.

Mac changed-path release and ASan/UBSan checks pass. Linux debug checks pass,
and the broader release batch passes24/24 in15.56s. Its ASan/UBSan batch passes
23/24 in95.99s; command_jobs' native descriptor-exhaustion fixture exposes an LLVM
memory-probe failure under the depleted descriptor bound. The corrected fixture
retains a real forwarded native EMFILE and exact semantic no-child terminal;
its Mac release1/1:3.32s and ASan1/1:6.39s pass. Linux direct debug/release/ASan
also pass1/1 each in2.10/2.06/2.50s. The other23 Linux ASan checks remain settled.
The first mandatory source gate builds successfully and passes102/105 debug
cases. Its three fixture findings are corrected: provider timing uses actual
reviewed1320–1326 fields and sufficient borrowed codec capacity; authored
capture retains its intentional full-DATA refusal while supplying turn credit
and enough reader scratch for the preserved4MiB+1 original; participant PTY
checks reconstruct chat rows and parse the exact displayed external result.
Direct affected debug checks pass, narrow changed C++ static checks are clean,
and the PTY also passes Mac release/ASan. Linux's same PTY check exposes
mid-frame ID extraction; waiting for the actual renderer frame delimiter fixes
that existing oracle. Final affected Linux debug/release/ASan checks pass3/3
each in2.66/1.89/6.59s (run-4bme3mn8). The two borrowed engine fixtures also pass
Mac release2/2:2.06s and ASan2/2:10.72s. The corrected mandatory source gate
passes105/105 debug cases in125.85s, all155 selected owned C++ translation units,
and owned Lua checks before the `ab23278` commit and push. These findings
do not require production changes or another review panel.
The fourth completion segment implements
[operation17](../papers/2026-10-11-operation-owner-raw-subplan.md),
[physical storage](../papers/2026-10-11-product-storage-owner-raw-subplan.md), and
[semantic retained/index emission](../papers/2026-10-11-retained-index-owner-raw-subplan.md).
Operation emission preserves actual acquired result packets before projection and
records dispatch requests and returned/thrown outcomes separately. Native storage
preserves partial reads, original writes and actual errors; explicitly authorized
workers acquire resources, and a separate serialized producer emits prepaid
unlock/close records after cold readers outlive Root. Semantic hooks cover native
frames, acknowledged append metadata, identity/state changes, recovery, saved
slots, index/catalog access and owner teardown. Saved-state allocations occur
before publication; accepted disk/live selection changes require only nonthrowing
moves afterward. TerminalUI and Root share one main/worker launch/join transfer.

Real exec, generic adapter receipt/terminal, joined Job and turn rollback callers
hold finite raw banks before admission. Ordinary bodies keep their own admission.
Paid original capture does not promise later business result retention succeeds:
actual retention failure emits its truthful raw error and whole-operation outcome.
A capacity fallback terminal is bounded; it never invents an accepted result locator.

The composed OLD+NEW stop/exec core requires794488 CONTROL and138922517 DATA
bytes at its declared maximums, including retarget pairs and native frame headers.
Generic receipt adds76551 CONTROL; generic terminal adds242440 at its formal
maximum (318991 together). These are different simultaneous caller obligations: their full formal
sum is **not** claimed to fit1MiB. Actual session catalog limits reduce the native
probe bound, and real nested Lua→exec is directly exercised. Deeper ordinary
recursion may refuse before effects. The launcher selects256MiB DATA/1MiB CONTROL/
128KiB FAILURE per prepared producer; other runtime callers retain64MiB/256KiB/
128KiB defaults. These are intentional finite grants, not measured workload sizing
or an RSS claim. Raw recorder/reader storage and read-only inspection stay opt-out.

Each owner has its one code/oracle review and finding-driven direct repairs.
Operation OP1/OP2, physical P1–P4 and semantic S1–S4 are recorded in their
[operation](../papers/2026-10-11-operation-owner-raw-code-attack.md),
[storage](../papers/2026-10-13-product-storage-owner-raw-code-attack.md), and
[retained/index](../papers/2026-10-13-retained-index-owner-raw-code-attack.md)
reports. The semantic repairs deny unbudgeted public inspection, end terminal
loans before callbacks, validate issuer before reading mutable Root state, and
emit the direct checked-read writer state on actual Error. Actual launch/reopen, cold lifetime/concurrent cleanup, retarget refusal,
result/cancellation and positive native-read boundary tests qualify the stated
paths. Source ca6747b passes the installed source hook:110/110 Debug tests in146.29s,
all161 owned C++ translation units and owned Lua formatting/syntax/type/contract
checks. The20 affected owner cases qualify on Mac Release/ASan-UBSan and Linux
Debug/Release/ASan-UBSan. Finding-driven fixture rechecks are included: Linux
storage76.11s with a120s whole-group watchdog, optimized GNU projection caller,
retired-inode observation and ASan thread forwarding. Native fault deadlines and
byte/custody assertions remain intact. The seven older integration fixtures also
pass Mac Debug/Release/ASan-UBSan and Linux Release/ASan-UBSan; Linux7/7 completes
in2.68s/24.75s. Exact logs are in ignored context/current-segment-joint-snapshot
and context/linux. No further review panel is introduced.

Remaining program/workflow/bridge owners,
[file47–48](../papers/2026-10-11-file-owner-raw-subplan.md)/Beads/decision/Git callers, session/UI/station/backstop and measured workloads
remain open. Task hooks cover30–31; authored/VM hooks and authorization hooks
cover their stated subplans only. No completed subset closes the60-row census.

## Remaining-owner delivery, 2026-10-14

The operator authorized tool callers, programs/workflows and session/operator
surfaces through source and direct checks, stopping before qualification. This
segment uses ONE shared [emission mechanism](../papers/2026-10-13-raw-emission-consolidation.md):
RawEmission owns paid banks, request/publication/end; raw_publish copies native
parts; raw_call_run classifies actual returned/thrown alternatives. Surface
owners supply their actual fields and lifecycle. No callback registry, metric
formatter/encoder, producer per event, event transformer or timer is introduced.
Original business encoders/formatters retain their existing work; instrumentation
funds their output beforehand and preserves it before later work.

| Delivered owner | Concrete source and checks |
| --- | --- |
| File47–48, Beads51, decision/Git callers | Original native path/read/write/partial/error bytes and actual Child outcomes, paid resource cleanup, actual CWD growth, source grants and semantic results. [File](../papers/2026-10-13-file-owner-raw-code-attack.md), [Beads](../papers/2026-10-13-beads-owner-raw-code-attack.md), [decision](../papers/2026-10-13-decision-owner-raw-code-attack.md), [Git](../papers/2026-10-13-git-owner-raw-code-attack.md). |
| Programs/workflows13–22 and bridge | Activation, generation/source/callable lifetime, VM/native/Lua original boundaries and errors; retained issuer owns cleanup after cold lifetimes. [Program/workflow](../papers/2026-10-13-program-workflow-owner-raw-code-attack.md), [bridge](../papers/2026-10-13-bridge-module-owner-raw-code-attack.md). |
| Candidate56 | Actual file/Git/intent/observation/child custody and same business BBM outputs; timeout keeps its old unknown-outcome path while recording failures escape. Actual terminated argv and native canonical paths. [Candidate](../papers/2026-10-14-candidate-owner-raw-code-attack.md). |
| Session/input/UI1–9,11–12 | Settings/state/locators, actual native input, dispatch/refusal, messages/palette/frame/packet and cold retirement. Existing real CLI/PTY fixtures compare bytes, identities and no-effect refusal. [Session](../papers/2026-10-13-session-state-owner-raw-code-attack.md), [input](../papers/2026-10-13-main-input-owner-raw-code-attack.md), [UI](../papers/2026-10-13-session-ui-owner-raw-code-attack.md). |
| Restart/recovery23,58,60 | Real token/locators/state and structural recovery IDs/predicates/append cursors; actual new/resume argv reopen in process, restart exec has no fabricated return on success. Retained sole issuer survives Runtime destruction; foreign custody rejects without dropping its loan. [Restart/recovery](../papers/2026-10-14-restart-recovery-owner-raw-code-attack.md). |
| Complaint55, Station57 | Actual local detail/delivery/retention and station admission/control/workflow/publication, original bytes and failed/no-effect outcomes. [Complaint](../papers/2026-10-14-complaint-owner-raw-code-attack.md), [Station](../papers/2026-10-13-station-owner-raw-code-attack.md). |
| Backstop59, terminal H and plain output | Actual recovery choices/actions/independent artifact; native editor/mode/signal/resize and paid restoration; existing plain native pieces and safe formatter with original-before-format and real write outcomes. ONE [Backstop](../papers/2026-10-14-backstop-owner-raw-code-attack.md), [H](../papers/2026-10-14-terminal-custody-raw-code-attack.md) and [Plain](../papers/2026-10-14-plain-output-owner-raw-code-attack.md) code/oracle attack each; their finite fixes and direct native checks pass. |

Shared File.prefix7 reads the requested112 native bytes without whole-file size
rejection/trailing acquisition. Shared File.weakCanonical8 preserves the actual
native prefix/CWD and returned path rather than guessing a PATH_MAX output grant;
Restart and Session listing reference that already-preserved body. Request and
return remain separate events. Absent optional context opts out; present invalid
references reject at the shared caller and selected terminal/native boundaries.

The first integrated Debug gate built successfully and passed124/131 cases.
Its seven failures exposed three production defects: foreign C++ exceptions lost
their original type at the Lua bridge, exec task activity missed the native
subscriber, and canceled workers could not finish publication/joined ownership.
Fixture defects were a source-relative workflow path, a reused file acquisition
accumulator, and reading a run while the source VM still retained its issuer.
Each correction follows its existing owner plan and review. The bridge/workflow/
operation cases pass3/3 in59.44s; File/Backstop pass2/2 in42.51s. The terminal
correction passes all seven existing task/terminal/command PTY, wake, UI and
terminal-custody cases in59.19s. Exact original-error, byte, closure and genuine
completion-refusal assertions remain intact. The first failed gate is preserved in
context/remaining-owner-source-gate-first-tests-failed.log. The final installed
hook must pass before publication; its result will be recorded here.
The next integrated run builds completely and passes130/131 tests in200.32s.
The sole remaining storage fixture assumed cancellation must leave Root blocked;
its actual join/rearm path now restores Root. The existing completion selection
passes after requiring actual cancellation, joined restoration and exactly one
idle native acquisition. The genuine forced-refusal/debt case stays strict.
The development build driver now lets whole native builds finish, matching its
existing whole-suite/native-analysis behavior; utility limits and individual
fixture watchdogs remain unchanged.
A later run passes129/131 in201.05s, including storage. Its recovery marker
oracle can read an opened file before the shell writes completion bytes; the
same-deadline exact-byte wait correction passes the existing case. The Main
crash report identifies a throwing fixture observer racing recorder ACK inside
noexcept publication, after the prefix test had completed. The corrected
observer uses the same exact preserved control record and reports its own errors
at the fixture boundary. Named borrowed successor outputs fix the Main lint
finding without argument copies. Matching Main/recovery direct and static checks
pass; the final whole-source gate remains required before publication.
The next gate passes131/131 Debug tests in210.23s and reaches all194 selected
owned C++ translation units. Its three failing TUs have explicit optional-bank/
snapshot/expected-error guards and named borrowed Git path parameters corrected;
the same three targeted analyses now pass. No annotations or weaker assertions.
Final verification is complete:131/131 Debug tests pass in236.43s; all194
selected owned C++ translation units, C++ formatting and header coverage pass.
Lua formatting and syntax pass. LuaLS then found the tooling-only declaration
missing for the already implemented blackbird.emit(site,value). Its exact native
contract is now declared; targeted formatting/syntax, whole-workspace LuaLS
(zero diagnostics) and the existing Lua contract probe pass. The operator
explicitly directs no whole-gate rerun for that declaration. Native/test/C++
source is unchanged from the successful checks; the checkpoint uses that
operator-authorized hook override after completing only the remaining Lua leg.
Logs: context/remaining-owner-source-gate-lua-declaration-failed.log and
context/final-lua-declaration-check.log. No workload/profile qualification starts.

Source and direct owner checks are separate from qualification. Publication of this source checkpoint requires the installed full Debug
build/test/C++/Lua commit hook. The execution log is
context/remaining-owner-source-gate.log; the hook rejects source or index changes
during its check. Final native H and Plain checks pass2/2 and1/1 respectively,
and Backstop's ten direct native cases pass. These finish the named source work,
without starting workload/profile qualification. Linux-only File and Candidate long-CWD direct
branches remain unexecuted on this host; the attempted remote launch timed out
before remote work. No Linux/profile/workload result is claimed for this segment.

Standalone SystemA Value/radix/ByteBuffer allocator/deleter/lifetime redesign
remains outside this delivery and open in the census. Bridge and native numeric
acquisitions needed by the named owners are covered. Workload sizing/capacity,
producer/recorder costs and consumer-contention qualification remain for the
operator discussion. nls.2 remains in_progress; nls.9 and broader P0 nls.10
remain open. Pre-runtime help/auth/discovery/read-only inspection and diagnostics
after Runtime teardown remain explicit bootstrap/audit boundaries, not claimed
as captured live-session output. Evals, multiplayer, steering and further
trajectories remain deferred.

## Shared defects and boundaries

The participant capture limit, post-effect result refusal and provider original
expiry are tracked in P0 nls.10. Where an nls.2 hook depends on those originals,
its owner unit must fix that retention boundary and update nls.10. A locator to
discarded bytes cannot qualify the hook. This is a concrete overlap, not a claim
that instrumentation closes the entire audit-completeness issue or that its
whole audit review must precede every hook.

Lane ordinal order is not a logical clock. nls.9 remains separate. Host power
loss is excluded. A process dying before complete publication leaves an
acquisition unresolved; do not fabricate its outcome. Multiplayer, steering,
new trajectory work and particular evals remain deferred. The independent
startup_loading development-fixture investigation is arconaut-985.

## Closing the bead

For each owner: reviewed concrete subplan, direct red oracle, implementation,
green checks, one code/oracle attack and finding-driven fixes/recheck. Use the
existing owned lint/source gate and relevant Mac/Linux release/diagnostic checks.
Six build/analysis workers remain the approved maximum. Choose checks for actual
fault classes; no tests for tests, another assurance layer, arbitrary campaign
stopwatch or laptop mutation campaign. Text-only changes get document checks.

nls.2 closes when all current census owners have their specified emissions and
failure paths implemented, independent source-to-preserved-byte checks pass,
runtime failure/cleanup/consumer behavior matches the design, native/Lua authored
events and read-only consumption work, and real costs/capacities are measured.
The remaining sequence is not itself evidence that those conditions hold.
