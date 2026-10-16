# Running command jobs

2026-10-09. Operator-selected work, ahead of steering and trajectories. This
design follows the primary-platform study, eight harness source/test studies and
the current Blackbird integration map in
`papers/2026-10-09-command-jobs/`. No reference code or dependency is adopted.

## Purpose

A command can release its foreground wait, keep running while Blackbird does
other work, and return to the foreground as the same command. Interactive commands
have a private PTY whose input and output can be attached to the operator terminal.
The process, capture and launch identity survive every attachment change.
Backgrounding does not stop, cancel, relaunch or extend a command's deadline.

This unit supplies process-lifetime custody across turns. Active or unsettled jobs
block native replacement and session switching. Normal shutdown stops and reaps
owned children before destroying their engine/UI. Harness death loses custody;
reopen retains the existing unresolved-command fence and never adopts a saved PID
or replays a launch. Cross-process custody requires an independent custodian and
is outside this unit. Uncontained descendants and remote effects remain explicit.

## Ownership and identities

The native engine owns one job registry throughout its lifetime. A job is admitted
through the existing decision/invocation/attempt batch before dispatch. Its job ID
is its original launch attempt, not a PID. The registry entry exists before its
worker can spawn. One worker owns spawn, descriptors, raw collection, input and
child wait/cleanup. Returning from a wait never destroys that owner.

Workers publish bounded immutable byte/event queues. The UI mailbox holds64 requests, with four slots reserved for background/stop; blocked input stays queued without repeatedly admitting the same control. The engine execution owner
alone drains them into the existing audit. UI control and inspection use a locked
native projection/mailbox; the UI never mutates Lua, context or RetainedState.
There is no second transcript or production JSON persistence/IPC.

The original `exec` attempt remains running after a background result. Provider
function-call output closes the *tool wait*, with `job_id`, state and output_ref,
without claiming command completion. Later foreground/input/signal/read operations
have their own admitted attempts, linked to that same job. Exactly one terminal
observation settles the launch after actual child/stream/cleanup observations and
successful result retention. A completed wait returns the command's observed exit
status; an incomplete wait has no exit_code. Existing synchronous exec result and
timeout behavior remain usable when no yield is selected.

The reserved terminal observation contains bounded native metadata itself: root
termination kind/code/signal, captured/retained extents, capture completeness,
stop/error cause and output_ref. It does not require an ordinary-capacity original
result source. Normal tool-result originals remain ordinary captures. Failure of
that capture cannot fabricate a locator; failed job capture stops owned jobs and
settles bounded unknown observations where possible, otherwise admission stays
fenced. Validate the terminal representation against payload/batch limits before
spawn, not merely aggregate reserve bytes. Per-job terminal debt is8192bytes and
16records; all aggregate additions reject overflow.

The engine owns one aggregate settlement reservation while a workflow or job is
outstanding. Every refresh includes workflow cancellation, synchronous operation
depth and per-job bounded terminal allowance. Foreground return cannot release
that allowance. Output consumes ordinary capacity; retention failure stops owned
work and preserves unknown/fenced outcome rather than dropping originals silently.
Sticky job retention failures survive unrelated turn resets.

## Independent state

Process evidence comprises starting, running, observed stopped/continued, observed
leader exit, stream closure, cleanup, and retained terminal result. Attachment and
waiter state are separate. Empty output does not mean EOF. EOF does not mean exit.
Leader exit does not mean inherited writers closed. Terminal output is sealed only
after the collector's declared stream closure or an explicit incomplete-drain error.

Wait budgets use monotonic time and are independent of the launch-relative command
lifetime. Repeated foreground waits cannot renew the lifetime deadline. Completion
and background/control admission are resolved against an exact job/wait identity
under the registry lock. A request for a finishing job cannot detach or signal a
later job. Completed jobs remain inspectable until explicit archive or resident
admission pressure. Capacity never kills a live job to evict it.

First defaults are eight live jobs and32 resident job records, configured explicitly
through the job configuration interface while no jobs are live. Each job permits
16MiB captured output, 1MiB pending capture and 1MiB live-view tail (shared immutable
chunks), 64KiB pending input, 16MiB total retained input identity payload, and4096 retained control identities. Input request payloads share their immutable native bytes; the worker does not keep another string copy. Public reads are
at most64KiB. Full queues apply bounded backpressure; lifetime/output exhaustion
requests stop with an explicit incomplete/unknown result. Stop/background remain
available when input/control capacity is exhausted. At the resident bound, new
admission automatically retires the oldest eligible record in original admission
order. Eligibility requires a finished worker, retained terminal result, no
pending captured bytes or native custody, no unfinished delivery, no foreground
waiter, and no queued mailbox request targeting that exact job. Explicit archive
uses the same predicate. If every record is protected, admission refuses before
worker publication. The live-job limit is checked first and never retires a
completed record merely to refuse the launch anyway.

Retirement joins the finished worker and releases its native projection, live
tail and input/control identity history outside the registry mutex. It emits a
raw ownership transition and preserves the existing retained terminal result and
original output. Retired jobs leave native discovery/read/control; their
`output_ref` remains readable through `read_process_output`. A stale control
fails lookup before effects. Original attempt admission owns launch identity;
resident IDs and raw PIDs are never recycled as new command identities. A
durably settled unknown result may retire after native custody ends; unretained
capture or a pending child cannot. Sticky capture failure independently fences
new work. No unbounded native tombstone history is introduced.

These are declared resource choices, not process identity or provider limitations.

## Shared interfaces

`exec` gains explicit `yield_ms` and `pty`; its existing command/argv, lifetime
timeout, task binding and output presentation budget remain. A zero yield starts
in the background. Omitting yield preserves foreground waiting. PTY jobs retain
input; ordinary pipe commands retain the existing closed-stdin behavior.

Native model/Lua/operator controls provide job discovery/read, foreground wait,
background release, input, resize and explicit stop/signals. Output reads have
independent absolute byte cursors and distinguish caught-up from sealed. Full
originals remain available through the existing output_ref. Input and signal
requests carry delivery identity; duplicate requests cannot repeat writes/signals,
and an identity reused with different contents conflicts. Input queues have a
declared byte bound; resize uses the latest requested dimensions.
Input acknowledgement identifies queued versus kernel-written bytes, never
application consumption. PTY EOF means sending its configured VEOF character;
it is not a pipe half-close. Raw input's audit admission precedes actual writes.
One delivery record retains its exact request and advancing written offset for
the entire resident job lifetime. A duplicate observes that same delivery; it
never creates another write. Failure after a written prefix reports partial/unknown.

`/exec JSON` launches without a provider round trip; `/jobs` inspects immediately;
`/bg [JOB]` and Ctrl-B release the exact active wait through the native mailbox;
`/fg JOB` waits for that existing job. A selected foreground PTY receives keyboard
input while its output uses Blackbird's existing escaped text presentation. Ctrl-B
returns to the composer, preserving its draft; resize reaches the same private PTY.
Ctrl-C in this input mode goes through the private terminal driver; explicit job
stop remains separate. The operator tty stays raw/no-ISIG under the established
TUI mode and its foreground process group is never given away by the harness.

The TUI opens the shared native command-results inspector with `/jobs` or Ctrl-O.
Ctrl-O preserves the existing draft/cursor. Its read-only resident observation is
allocation-free and nonblocking, bounded to eight catalog rows plus one selected
detail,4096 retained bytes and16 original source chunks. Missing resident history
is explicit; this path does not scan the retained journal. Recording pressure
keeps the last accepted page visibly stale. Selection/navigation never starts,
foregrounds, signals or stops a command. Plain-mode `/jobs` and explicit process
reads retain their existing structured output.

This is interactive text foregrounding, not an arbitrary full-screen terminal
emulator. ANSI/control bytes remain exact in originals, without replaying raw
suffixes, alternate-screen state or terminal queries into the operator terminal.
Full-screen emulation/transparent attachment is not selected by this command-
lifetime unit. Foreground shares the collector, not a second read descriptor.
Bounded live chunks support text observation with exact gap offsets; the omitted
prefix remains retrievable from originals. Received and audited extents are
distinct, and public retained reads do not advance beyond committed bytes.
Asynchronous completion does not pop the synchronous UI stack.
Task runtime activity stays associated with the launch. No job outcome changes
the model-authored task status automatically.

There is one engine foreground waiter and one operator input attachment. Their
lease includes exact job and monotonically increasing wait epoch. Omitted `/bg`
captures that lease at submission; delayed requests cannot bind to a newer waiter.
Busy foreground selection is allowed only for that same job; other selection is
refused until it releases. Independent read cursors do not require attachment.
Bracketed-paste delimiters are handled incrementally: Ctrl-B inside a paste is
input, outside it is the local detach chord. Ctrl-B then `/quit` exits input mode
and stops the harness. A stopped job is never implicitly continued by foreground.

| Action | Command and workflow behavior |
| --- | --- |
| Background / wait budget expires | Release selected wait; job continues under original deadline |
| Turn ends after yielded exec | Job remains owned and observable |
| Cancel ordinary foreground exec | Stop that command and cancel workflow; earlier background jobs continue |
| Cancel later foreground wait | Release wait and cancel that workflow; existing job continues |
| Ctrl-C during PTY text input | Admit/write byte03 to private terminal; no harness cancellation flag |
| Explicit job stop | Sticky exact-job request; report accepted separately from observed termination |
| Command lifetime/output bound | Stop job; report timeout/incomplete capture and external uncertainty |
| Quit/error shutdown | Stop/join/reap all owned jobs before UI/engine destruction |

## Platform mechanics

Owned spawning serializes non-atomic descriptor creation with all owned spawns,
uses close-on-exec endpoints, and resets inherited job-control signal state.
The shared lock covers legacy Child, new jobs and editor spawn, and non-atomic
pipe/temporary descriptor creation including terminal wakeup endpoints. It is
released before I/O, waits or helper readiness. All endpoints are normalized above
stdio before file actions, including when the harness began with closed stdio.
Pipes use isolated process groups. A small owned exec helper establishes a PTY
session/controlling terminal before exec, avoiding unsafe post-fork allocation in
the threaded harness. Its spawn omits SETPGROUP: setsid must run before it becomes
a group leader. A close-on-exec bootstrap pipe reports session readiness and setup/
exec failure. Before readiness, stop targets the owned PID only, not a guessed
group. Helper failure is an observed command failure, not a new launch attempt.
Direct child wait uses WNOWAIT to pin identity until group cleanup;
no group signal follows reap. Terminal input targets the private PTY's actual
foreground group through its terminal driver. Group cleanup makes no containment
claim about programs that escape their group/session.

Stop sends TERM, then KILL after200ms if necessary, including direct root PID
signaling while custody remains pinned. After stop, drain lasts at most500ms;
forced closure is incomplete capture, never successful EOF. Normal leader exit
waits for stream closure until the remaining original lifetime deadline, not a
renewed clock. No group-empty probe certifies containment. Preserve native exit
versus signal kind (normal exit143 differs from SIGTERM), consume stop/continue
events without reaping exit, and use only the owned PID's wait status.

Linux PTY-master EIO becomes channel closure only after helper readiness; setup
failure/unrelated descriptor EIO remains failure. Mac zero-byte EOF is channel
closure. Neither establishes root exit. These paths are qualified separately.
Leader observation and stream draining proceed independently, including stopped
children and descendant-held writers. Collector backpressure bounds memory;
owner pumping during cooperative provider/tool waits, Lua execution and idle UI
keeps capture moving. Uncooperative blocking code can delay consumption, so workers
must remain stoppable under full queues. Pumping is bounded, reentrancy guarded,
never edits live provider context, and never throws across a Lua hook boundary.
Each pump entry is an owner-safe point outside retained transactions; a guard
prevents recursive pumping. Pending UI controls enter the same native admission
path before granting worker effects. Pump failure requests all job stops and sets
a sticky error. Provider/tool ticks surface that error; the Lua hook catches it
into Runtime failure and raises its Lua error after C++ automatic owners unwind.
Shutdown requests stops independently of audit health and drains before joining.

## Direct verification

Specification-derived cases establish one launch/same PID across many waits and
attachments; exact binary byte conservation and independent cursors; unchanged
lifetime deadlines; independent leader exit/EOF; ordered and deduplicated input;
foreground versus stop; capacity refusal before effects; late control isolation;
retention-failure fences; restart/switch refusal; shutdown reaping; and crash with
zero replay. A small independent event model attacks transition schedules. Real
native child fixtures and actual terminal PTYs attack OS assumptions separately.

Required static analysis uses the complete owned compilation database and existing
unsuppressed rules. Changed paths run under debug/release, ASan/UBSan and qualified
TSan; Linux runs the actual native/PTY cases. A real local command trial demonstrates
background work, unrelated work, input/resize and return to the same command. No
provider call or generic performance claim is required. Mutation remains fleet-only.

Design and plan receive adversarial challenge before coding. Implementation has
one remediation layer and one recheck/fixes under the same declared allowance.
No third assurance layer or adoption of an external terminal/process framework.

## Operator input pressure

The operator adapter retains at most4KiB pending input plus six framing bytes for
the selected job. It keeps keyboard controls available under queue pressure,
explicitly reports excess unadmitted bytes, and preserves pending bytes with their
original job through detach. Background/stop have reserved mailbox admission.
Input payload identity history is bounded independently of currently pending bytes.

Actual Darwin26.6 SDK probes showed exit-only waitid returning STOP and a continued
event with an anomalous reported PID. Exit classification requires an exit kind
and the owned PID; nonterminal observation uses the specific P_PID selector and
STOP/CONT kind. The primary empirical source and full fields are in platform.md.

## Admission refusal versus capture failure

The registry reports its actual live/record guard through call-local native
CommandStartStatus; generic raw/audit capacity is not reclassified from absence
of a job. A registry refusal returns a retained normal tool result with
`error=capacity`, `resource=live_commands|command_records`, actual `count` and
`limit`, the audit `attempt` locator and `effect_outcome=not_started`. It has no
process `output_ref`, because this command never launched. The attempt settles
as failure, the activity says refused/not started, and the model receives this
result rather than a generic turn-stopped placeholder. It can inspect current
jobs and choose further work. This does not automatically retry any command.

Failure to retain the refusal result or terminal observation preserves the
capture stop/fence. Real post-start capture failure, cancellation and timeout
retain their previous unknown-effect and exact-child cleanup contracts. A
registry limit never implies provider context exhaustion or user interruption.
