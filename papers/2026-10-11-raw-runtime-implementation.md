# Raw instrumentation runtime — first implementation unit

2026-10-11. Operator-approved implementation, excluding host power loss. This is
a candidate branch; no main merge or running operator executable replacement.
Evals remain deferred. The complete surface census remains the implementation
backlog, not an assertion that the first hooks cover the system.

The native producer publishes fixed envelopes and existing native fields into
retained named shared memory. It does no metric transformation, formatting,
registry lookup, callback dispatch, allocation or disk I/O per emission. Separate
producer contexts have distinct data/control/failure partitions and reverse-wake
endpoints. A source region holds terminal credit before BEGIN. At a safe boundary
its owner can wait for space before acquiring another fact; actual emission is
an immediate bounded copy, publication and armed-notice exchange. Admission closes
on ingest failure; already held terminal credit remains usable.

`RawRuntime` starts before application threads. A single-threaded supervisor owns
registrations, both notifier endpoints, the parent-lifetime watch, recorder death
watch and named mappings. Its recorder child owns raw audit writes. These are two
support processes; no metrics framework or library dependency is adopted. A main
thread binds a registered context; worker registration happens before launch,
then the worker binds its own context. Forking the supervisor precedes application
threads; subsequent recorder forks occur only in the single-threaded supervisor.

The recorder copies a bounded complete ready prefix per partition, checks failure
and control before data, rotates its first lane each tour, appends unchanged raw
records with a source origin, and calls native `fsync`. Only successful append and
synchronization permit reclaim. No per-emission disk wait, device cache guarantee,
power-loss experiment or `F_FULLFSYNC` is required. Recorder CRC and framing are
integrity work, not metric transforms. Tertiary readers use preserved cursors and
cannot advance reclaim or affect emission.

Setup retains a versioned description file containing native widths, clock IDs,
source-generation hash, compiler, static sites, exact field layouts and enums.
The fixed binary lane registrations retain epoch, capacities, named memory and
closure. Registration/closure files use temporary-file publication plus directory
synchronization; torn append metadata cannot invalidate later registrations. A
catalogue lease excludes another active recorder. A segment predecessor and CRC
protected seed carry cumulative source preservation across a damaged-tail
successor. Original segments and bad tails remain intact. Quiescent retirement
drains P through D, seals a segment, marks closure and unlinks the mapping.

Every recorder attempt holds independent failure credit before acquiring or
writing its source. Site8000 retains original native error, stage, physical
progress, audit identity, source lane/partition/start/end and whether a source
range was acquired. Physical progress describes an acquired append/reclaim;
startup/open failure can have unknown physical progress and no acquired source.
A failed recorder leaves the originals pending. Automatic I/O recovery is bounded;
explicit recovery durably registers fresh independent failure capacity before
retrying, keeping the old failure lane. Main's own site8001 uses prepaid failure
space and closes ordinary admission. Supervisor RPCs watch an independent death
pipe whose writer is closed in recorder children.

On a later launch while the host stays up, a closed catalogue is skipped, an
active catalogue or live/ambiguous owner PID is left alone, and a dead-owner run
is reconstructed from retained lane registrations and named memory. Notices are
rebuilt rather than mistaken for death evidence. Missing named memory records its
native error without inventing an acquired source range. No old process is
adopted and no original effect is replayed.

## Direct evidence and limits

The channel test independently checks exact envelopes/bodies, release/acquire
publication, reclaim-before-reuse, held capacity, full partitions, armed wake
races, native-notice failure, invalid site and near-exhaustion admission. The raw
audit test checks independently assembled physical frames/CRC, all first/second
batch byte cuts, partial writes, failed synchronization, readable EIO refusal,
lost acknowledgement, predecessor seeds, conflicting origins and immutable
reader cursors. Journal encoding has exact destination/alias canaries.

The runtime test executes a300KiB event through actual named memory and disk,
recorder SIGKILL/restart, eight producer registrations/retirements, directory
unavailability and recovery with exactly one111-byte failure slot, and exact
native failure-body comparisons. It queues an actual RPC to a stopped supervisor
before killing it while its recorder survives. A separate fork fixture publishes
an event, kills producer/supervisor/recorder, waits for the actual catalogue lease to be released, then starts from retained
catalogue and named memory and checks the exact disk record. Hook tests exercise native
field preservation, clock failures, no allocations, terminal credit, ownership,
provider success/failure/retry and response-import rejection. Production clock
acquisition is a direct native function; the hook test supplies a fixed function
at static link time, and a separate test checks the real clock boundary/errno.

These checks do not constitute the complete planned crash-cut matrix or all
product coverage. Actual adapter wire-send/raw-stream and external-process sites
are not yet integrated; participant and remaining census classes are open.
Continuous-hot-lane fairness, arbitrary effect-lock/cancellation interleavings,
consumer isolation under active disk contention, and real runtime append/sync
fault cuts beyond the audit component remain direct verification work. An
incomplete bootstrap header can still prevent recovery and leaves originals
retained; no blanket bootstrap-fault guarantee is claimed. Logical clocks nls.9
and core audit completeness nls.10 remain separate open defects.

The single source attack preceded some later lifecycle integration. Its findings
and their focused fixes are recorded separately. Full-profile/build/lint results
and measured costs will be appended only after execution; primitive probe numbers
from the design unit are not represented as this implementation's performance.

Actual preliminary producer measurements are retained in
[raw producer cost](2026-10-11-raw-producer-cost.md): this loaded local pass
measured zero emission-loop allocations,83ns median64/256B bodies,292ns4096B,
and667ns for a region with both native clock endpoints. See its method and limits.

The original local sanitizer aggregate hit its30s limit with a redundant default
64MiB smoke case. Direct execution of that binary completed. The repeated smoke
was removed; the existing eight-worker case now checks each exact envelope, source
start, distinct lane identity and ordinal. This changes fixture cost and strengthens
its oracle; it does not claim the largest-capacity recorder setup is cheap.

Recovery publishes its preserved cursor even when no source remains pending and
no new append occurs. The direct restart case removes a publication for an empty
drained source, stops/kills the old recorder, and requires the successor itself
to restore the exact40-byte cursor. This closes reader visibility after recovered
preservation; it does not qualify every runtime append/sync fault cut.

The integrated gate exposed needless maximum-size scratch zeroing at every
recorder start:64MiB in the default configuration, despite reading an empty
segment. Audit scratch now uses owned uninitialized storage. Frame parsing fills
each exact header/payload prefix successfully before inspecting it; existing cut
and exact-byte oracles still apply. Capacity stays unchanged. The focused runtime
and tiny-terminal checks fell to1.07s/1.11s after this correction; those are fixture
execution times, not universal startup latency claims.

Initially automatic application activation was parked. The active integrated gate
exceeds its120s CTest limit and command-job foreground observation fails under
that run. Explicit development bindings continue exercising the runtime/hooks.
The saved main activation diff is ignored context, not a deployment instruction.
Resolving owner registration/restart/recording interaction with CLI/PTY deadlines
is open work; component success does not qualify that activation.

At that implementation bound, the inactive candidate also failed the full gate:
CLI/PTY deadlines failed and the88-case debug command exceeded120s. Automatic
capture was not established as the cause of those failures. Source was left
staged and uncommitted; full owned lint did not run because the gate stopped
at CTest. Targeted static/component/profile successes did not qualify activation.
No hook bypass, main merge, executable installation or source push occurred then.

Operator resumes delivery and explicitly authorizes main integration. The focused
inactive owner cases all pass; concurrent bulk storage work substantially delays
the broad suite. Correct the runner's whole-suite120s watchdog: CTest owns each
test's existing TIMEOUT or a120s default. Schedule deadline-sensitive UI/CLI
fixtures alone, retaining six-way parallelism for other cases and every existing
assertion/deadline. Restore application startup before retained-state work. The
actual launcher oracle checks preserved linked startup/ready and reopen/recovery
events, retained append, complete clean drain and inspection without new capture.
The full installed source gate passes all 89 debug cases (116.52s), all 125 actual
owned C++ translation units and owned Lua formatting/syntax/type/contract checks.
Three region-terminal optional accesses are guarded directly with has_value(),
rather than leaving the guard opaque behind enabled(); no suppression or behavior
change. Six independent native-analysis workers fit the actual host. Two large
translation units exceeded the generic120s wrapper; clang-tidy now completes the
whole source analysis rather than truncating it. Small-command and actual test
watchdogs remain. Launcher capture/reopen/inspection passes Linux debug, release
and ASan/UBSan. Source commit 56a965c is integrated on master. The complete
surface census and runtime/performance limits above remain open in nls.2;
these results qualify the delivered initial hooks, not the entire census.
