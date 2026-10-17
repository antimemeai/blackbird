# Comparing physical terminal edge policies

The operator wants a handful of physical-viewport renderers compared on speed and
stability. This unit measures the candidate implementations in the actual
ChatPainter/ChatPresenter packet path, rather than comparing unrelated TUI
applications or relabelling a visual repair as a performance improvement. Root
owns candidate machinery; the separate full-physical Microsoft terminal lane owns
visual correctness. This plan precedes native timing or a remote build.

## Question and selection rule

All candidates own every physical cell. A candidate must first produce the
specified cells, attributes and cursor without unwanted scrolling, leave terminal
modes usable, preserve an in-flight immutable packet, and continue servicing input
under output pressure. Failed correctness cases disqualify that implementation
from a performance recommendation; their bytes and timing attempts remain.

Among qualified policies, compare actual packet bytes, preparation CPU and wall
time, terminal-frame observation, and input service during a blocked terminal.
Do not collapse these into a score. Prefer a policy that meets the physical screen
contract simply, without a material cost in the workloads that matter. A smaller
packet is useful but does not by itself establish better responsiveness.

## Native fixture interface proposed to the owner

One development-only fixture links the same owned renderer and presenter as the
application. Select the edge policy at runtime, keeping native build, scene
construction, frame order, input processing and output transport identical. The
fixture has two modes:

- A finite batch prepares/commits a declared sequence of the same grids. Emit raw
  monotonic and thread CPU timestamps before/after each measured batch plus
  iteration/frame count and exact summed packet bytes. Calculate differences and
  summaries in the external report. Scene construction is outside the preparation
  interval. Use actual painter commit semantics; do not substitute a dummy dirty
  detector or measure an allocating setup loop as painting.
- A PTY episode reads fixed commands from stdin and drives actual nonblocking
  ChatPresenter output. A separate inherited report FD emits fixed native records
  for ready, input acquired, frame prepared, frame committed and exit. No timing
  text is inserted into the terminal packet. Native records identify stage and
  emit raw timestamps/counts; the parent derives durations. The parent reads
  actual CSI ?2026 l packet ends from the PTY and records its own monotonic clock.

Suggested command line: `--edge-policy NAME --mode batch|pty --cols N --rows N`.
The final API may be smaller if the owner's fixture already supplies equivalent
facts. Stage IDs must make packet boundaries unambiguous when a newer frame is
coalesced behind a pending packet. Do not claim that native write completion is a
physical display paint: parent PTY observations include scheduling and drain time,
and the Microsoft VT backends provide the independent cell/style oracle.

Report-FD writes must be bounded and independent of the intentionally blocked
terminal. Its finite records fit a pipe; the parent drains it throughout. Tests
must not add a blocking report channel to the application's production loop.
No production producer-side duration calculation, formatting, histogram or metric
transform is introduced.

## Equal workloads

Each trial uses the same deterministic grids and mutation order for every policy.
Use 110x35 and 80x24 initial geometries. Include a 101x30→32x12→101x30 sequence
for the reported final-column fault, and 1x1/2x2 in correctness rather than pretending
such tiny layouts are representative timing workloads. The owner and terminal
oracle lane must agree on the exact scene manifest before the first measurement.

| Workload | Concrete fault or cost addressed |
| --- | --- |
| Sparse editing | Change a short interior token, a last-column glyph, and cursor only in successive frames. Exposes ordinary delta overhead and delayed wrap after an edge write. |
| Full stream | Replace every row with deterministic printable text across successive frames. Shows bandwidth/CPU cost of full repaints and bottom-right handling. |
| Blank styled tails | Turn an occupied right edge into a blank tail in each available Ink style, including a nondefault background if the owned palette provides one. Distinguishes erase semantics from printed spaces. |
| Unicode at the edge | Place a width-two glyph ending in the last cell, replace it with narrow cells, and attach combining marks. Checks footprint repair and avoids calling byte count a character count. |
| Repeated resize | Alternate the declared 101x30 and32x12 states, preserving seeded final-column cells before the narrowing. Tests recovery and geometry adoption rather than cold process startup. |
| Backpressure and input | Fill the PTY until actual flush returns pending, keep a packet in flight, send finite input/cancel plus two geometry updates, then release reads. Checks that input is acquired before the blocked packet finishes and that only the final pending geometry follows its completion. |

Backpressure is established by the actual pending output/transport state, not an
invented sleep threshold. The parent withholds terminal reads for a fixed120ms
measurement interval only after the fixture reports pending output. It continues
draining the report FD and sends the same input command. Record actual input
acquisition and cancellation acknowledgment before release. The deadline is an
explicit fixture timeout (2s after input), not a purported usability guarantee.
After release, capture the committed old packet and final resized frame. Failure
to create pressure is a fixture failure, not a fast candidate result. A stream
large enough to exceed the PTY capacity is common to every candidate; total
emitted/prepared bytes remain in the records.

Cancel in this fixture means servicing the fixture's actual native input action
and ceasing its future generated frames. It does not claim cancellation of a
provider, child process, or a running user's session. Application-level routing
remains covered by the existing actual-main tests; do not replace it with this
narrow renderer harness.

## Qualification before timing

The full-physical oracle lane checks exact cells/styles/cursor on Ghostty and
Alacritty, including the rightmost/bottom-right cells, scroll preservation, resize,
Unicode, mode restoration and fragmented packets. Native owning tests check
packet completion, abandonment and input progress under a sustained nonconsumer.
A candidate with a failed check is listed as failed, with no implied qualified
performance win. Qualified build paths remain unchanged during observation.

The new external observer receives one direct independent review of endpoint and
lifetime behavior before timing. It does not need a second certification system.
Endpoint checks are direct predicates over actual report events and terminal
frame boundaries, not a fresh hand-written VT renderer duplicating the physical
oracle. Preserve every original terminal byte stream and the fixture record bytes.

## Remote build and run

Use Neuroses, the existing operator-selected ordinary Linux host. Read-only
inspection on2026-10-16 confirms Linux6.8.0-146, UbuntuClang18.1.3, CMake3.28.3,
Ninja1.11.1. Use the established linux-clang18/libstdc++13 profile, C++20 Release,
BLACKBIRD_DEBUG=OFF, instrumentation=none and the qualified Lua5.4.8 archive if
CMake requires it. This source scope includes only the current native/test
inputs and pinned owned development helpers; source archives remain intact.

Create a unique persistent campaign directory after root explicitly leases the
candidate source and authorizes the build. Record Git base, complete native
source manifest, content/path/mode identity, compiler/configure/build commands,
Lua/helper identities and exact fixture binary hash. Do not reuse a mutable old
campaign build directory or relocate binaries with embedded helper paths. Prior
Lua and command-center campaigns remain untouched. No AFL commands, inspection,
polling or stop occur in this unit.

After all owning native/actual-terminal checks pass and root confirms no owned
remote build/check overlaps, run one serial round-robin campaign. Initial count:
30 trials per qualified policy and geometry. Alternate policies within each trial
index; alternate geometries deterministically. Do not run one entire candidate
then another. Batch and PTY phases are distinct, clearly named measurements.
All trials, initial executions, tails and failures stay in the report. No warmup
removal, repeat-to-improve, outlier removal or changed deadline after a failure.
If a concrete fixture defect requires a correction, preserve the original failed
attempt and identify the exact corrected source; do not silently replace it.

Record host identity/load before and after, owned timing interval and runtime
profile. These establish context, not global host idleness or causal attribution:
Neuroses is shared. No owned build/check/timing overlap is allowed. Do not inspect
or govern unrelated work or assert that AFL or any other user's task was idle.

## Reporting

Retain raw per-trial frame counts, packet bytes, raw clocks, command-write times,
input-acquisition events, committed-frame events, final status and exit code.
Publish p50,p95,max for each policy/scenario/geometry with all sample counts and
failures; p99 at30 trials is the maximum and adds no independent tail information.
Show CPU/preparation, terminal-observer latency and packet bytes separately.
Any loss, protocol confusion, forced exit or mode/cell failure remains an explicit
stability result, not a missing sample excluded from the denominator.

A recommendation names the actual supported profiles and failed/qualified cases.
It does not turn a small timing difference on a shared host into a causal claim,
a universal terminal guarantee, or a claim that every future TUI view is fast.
Exact final delivered source-to-measured-source differences must be stated if a
later test-only correction occurs. Root publishes the one campaign's report/data,
updates the performance tranche ledger and issue disposition after the source
gate. No further performance panel is added merely to certify these records.

## Concrete fixture contract agreed before implementation

Root's fixture is `terminal_edge_fixture --strategy addressed|erase|corner|full
--events-fd N`. Startup emits sentinel scene sequence0; raw stdin commands are
`s` sentinel, `b` default blank tails, `c` colored blank tails, `u` Unicode edge,
`o` overwrite wide footprint, `r` reread actual ioctl geometry/recover, `q` finish
and exit. Event lines on the separate inherited descriptor are `INPUT seq byte`,
`PREP seq wallBeforeNs wallAfterNs cpuBeforeNs cpuAfterNs packetBytes`, `PENDING
seq` once after an incomplete flush, and `READY seq columns rows` only after
actual presenter completion. PENDING is not described as a syscall EAGAIN event.
The finite event stream is retained verbatim. The external collector records
parent monotonic timestamps at command writes and at complete event/frame reads.

The batch invocation is `--batch SCENARIO --iterations N --cols C --rows R`,
with scenarios sparse,stream,blank,style,unicode,resize. Exact raw batch record
spelling is coordinated with the owner before execution. Initial200 iterations
per batch gives a bounded work sample without invoking a long-lived autonomous
load. Thirty process trials per policy/geometry/scenario stay round robin and
retain all first runs.

The collector will use the existing blocked-terminal technique: retain the slave
output descriptor, fill its nonblocking output queue down to one-byte refusal,
then send a scene mutation. Wait for actual PENDING, continue withholding reads,
and send finite additional input plus two resize commands. INPUT events must
arrive before release. Parent filler bytes remain in the original transcript but
are counted separately from native packet bytes. No new large artificial native
packet or production control route is required to manufacture pressure. After the
fixed hold, drain and wait for the current geometry READY, then clean q exit.

Final owner coordination: `--batch all` emits six `BATCH scenario iterations
frames wallBeforeNs wallAfterNs cpuBeforeNs cpuAfterNs packetBytes` rows in one
process, with iterations=frames. This gives240 batch processes and240 PTY
processes for four qualified policies, two geometries and30 trials. The agreed
PTY sequence is startup s0,b1,c2,u3,o4,s5,r6(32x12),r7(original), then blocked
c8,b9,r10(32x12),r11(original),q12. Commands9–11 are acquired while8 is in flight;
only the final desired state11 follows its completed immutable packet. The report
compares end-to-end parent PTY boundaries by actual prepared-packet order; it
does not equate unrelated descriptor delivery order with native write order.

Quit12 is sent and INPUT12 observed while output is still withheld, after the
latest resize11 has been acquired. It requests fixture shutdown without replacing
the in-flight packet or losing its latest intended frame. Release then requires
READY11 and clean normal exit. This directly observes native quit acquisition
under pressure, rather than sending quit only after the interesting blockage ends.

Source-reading correction before execution: TerminalMode cleanup emits a defensive
CSI2026l without beginning a new frame. The collector therefore pairs complete
CSI2026h→l brackets and ignores an unmatched lifecycle close; it does not count
every literal close as a committed scene. Packet order, not event descriptor read
order, pairs PREP records with these bracket ends. This is finite framing, not a
second terminal screen model.

The batch interval includes the declared deterministic grid mutations and actual
prepare+commit calls. Its CPU/wall values describe that complete scenario per
frame, not isolated painter-only CPU. Initial base allocation remains outside.
The PTY PREP interval separately measures actual prepare plus any empty-delta
heartbeat, with scene construction outside. The report keeps those populations
separate and makes no deduction from common mutation overhead to edge-policy cost.

## Actual qualification dispositions

The corner prototype is disqualified by the independent Ghostty2x2 combining-mark
case; do not tune it or rank it on performance in this comparison. Addressed,
erase and full are the three eligible policies. Their Linux full physical oracle
passes43planes per policy/backend on Ghostty and Alacritty (258total), plus the
four native byte-contract cases and existing chat_view case pass5/5. Native
corner bytes passing does not contradict its actual terminal compatibility
failure. The matched round therefore has180batch and180PTYprocesses.

The first44-sample Linux attempt exposed the observer's contradictory second
child.poll early break. Its actual source correction, root same-unit review and
12-process actual Linux development qualification are recorded in
MEASUREMENT_RESULTS.md. The wait retains its actual5s deadline and explicit
zero-exit check; no timing result is accepted by widening an allowance. Every
failed original remains. The final corrected observer is an explicit overlay on
the unchanged compiled native tree, separate from the later CMake test-registration
change selecting the three surviving physical policies. No native rebuild or
physical-oracle repetition follows from this observer-only correction.

This unit compares candidate renderer policies. The live application still uses
its existing cropped extent/default addressed policy; it is not the integration
repair of arw. A later selected-policy application change will receive the usual
unchanged historical app-before/after observer round. Do not claim historical app
performance from this fixture experiment.
