# Prevention design and repair sequence

This is the resulting design/plan from the requested class audit. Proposed types,
annotations, rules and behavioral changes below are not yet implemented or adopted.
The reports establish concrete missing invariants; installing more generic warnings
cannot supply those invariants. Existing source qualification remains unchanged.

## 1. Preserve actual owners before broad tooling work

Repair participant archive first. Business terminal, native worker return, raw
cleanup and semantic original retention are independent facts. Join closes the
worker's publication opportunity; only a subsequent successful debt drain/check
can authorize erase. If Retain fails or an unknown-used original remains, keep
its owner and visible debt. Normal capture nodes already have pool ownership;
per-run emergency nodes need the same survival guarantee. Never destroy an
unretained original to make a residency bound fit.

Make successful retirement consume a narrow owned token, conceptually:

```cpp
Result<JoinedRun> join_run(RunId);
Result<DrainedRun> retain_final_originals(JoinedRun&&);
Result<void> archive(DrainedRun&&);
```

Names illustrate boundaries, not a selected API. A move-only wrapper alone does
not prove the predicate: token construction must inspect the actual emergency,
queue, native cleanup and retention results after joining. Failure retains an
owner. Keep arbitrary payload destruction and joins outside the registry mutex.
Annotate those blocking interfaces as excluded from the mutex once an owned
annotated lock/guard exists. The compiler can reject an under-lock call; a direct
late-publication schedule must still establish the retention invariant.

The independent RocksDB study supplies a useful retire-under-lock/free-after-unlock
pattern; LevelDB is a counterexample to copying a deleter contract blindly.
Neither is a proposed dependency. See [registry study](REGISTRIES.md).

## 2. Child identity is custody, not an integer

Fix native Child's ECHILD transition and establish/reject the SIGCHLD parent
precondition before spawn. A child owner should be move-only with explicit
active/unreaped, reaped and ownership-unavailable states. A borrowed pid_t is
an observation and must not itself authorize signaling. Reap/lost custody
invalidates any numeric group signal authority. Preserve uncertainty if custody
is lost; neither a destructor return nor a decremented counter proves cleanup.

Standard types/private operations provide the main API boundary. A narrow
Clang consumed-state pilot can warn on modeled signal-after-reap calls, but it
cannot detect kernel auto-reaping without the real syscall transition. pidfds
are individual Linux process handles, not a cross-platform group solution.
The decisive regression sets ignored SIGCHLD in a private fixture, demonstrates
ECHILD, and observes zero later native group signals through a syscall seam.
Use the ordinary default-signal child as the successful counterpart. See
[process study](PROCESS.md), not a fake PID-reuse stress campaign.

## 3. Preserve effect knowledge and retention status independently

Replace operation-name/numeric-capacity heuristics incrementally with owner-issued
native results. An owner can issue `RejectedBeforePublish` only at its actual
validation/admission guard. `Applied` carries a nonallocating effect receipt;
`Uncertain` records what boundary was crossed and what remains unknown. Retaining
any of these observations is an independent operation that can fail and must
preserve the existing capture fence.

```cpp
using EffectObservation = std::variant<RejectedBeforePublish, Applied, Uncertain>;
struct AdapterObservation { EffectObservation effect; AdapterResult result; };
```

This is a conceptual shape, not a request to expose C++ internals in the UI or
serialize internal state as JSON. Each boundary's final external tool formatter
can express resource/count/limit/attempt and effect knowledge. Typed factories
belong to the resource/effect owner. Reinterpreting every capacity as safe refusal
or growing the current operation-name whitelist repeats the defect.

Migrate participant admission/send, configuration validation, and task candidate
limits first. Ordinary refusals should return normal retained function output
and permit inspection/correction of prior work. Failure to retain that output
must still block later effects. No automatic retry; accepted older jobs/messages
remain intact. Exhaustive variants or -Wswitch-enum reject forgotten new states;
no default-to-unknown or default-to-safe path should silently swallow an addition.
A wrong explicit mapping remains a behavioral error and needs the direct engine
checks from [EFFECTS.md](EFFECTS.md).

For write/edit, prepare the allocating success representation before replacement,
then retain the exact successful effect fact even if later presentation/capture
fails. An allocation error describes result construction, not necessarily the
filesystem state. The smallest repair for the observed allocation edge is not a
complete general post-effect protocol; keep that limitation visible.

## 4. A deferred request needs a real path to progress

Separate `Accepted`, `WaitForPendingDrain` and `RejectedPermanentBudget`. The
pending-input bound decreases as the child reads; retained request identities
and historical byte debt do not. Account for queued reservations as well as
already-dispatched work so admission cannot promise more than the bound.

Every wait result must name the actual progress condition or completion/refusal
path. Do not make accepted operator bytes disappear from the composer into an
entry that can never be selected. Preserve urgent stop/background capacity and
exact duplicate-request outcomes. Dropping receipts to free slots would permit
repeat writes/signals and changes the contract. This is a semantic owner check,
not a general compiler warning. [R2](REGISTRIES.md) gives the one-child saturation
regression and transient-pressure counterexample.

## 5. One bounded display map; separate byte and cell types

Own distinct byte offsets, codepoint/cluster positions and cell columns. Retained
source/edit offsets stay bytes. Derive wrapping, clipping, padding, rasterization
and caret coordinates from one selected-profile byte-to-cell map, including style
spans without treating SGR-only boundaries as new spacing cells. Only this owner
calls scalar/property/terminal width machinery. All display substitutions are
measured after replacement: emitting U+FFFD means reserving its footprint, not
that of the discarded wide cluster.

Handle arbitrary valid UTF-8 text, including invalid emoji combinations, unattached
marks and huge clusters. A renderer can omit, attach, or visibly replace a mark
according to a stated policy, but must emit the representation whose cell footprint
it reserves. Emoji sequence properties and terminal width capabilities are separate.
A byte-bound cap should give a bounded fallback whose actual width is known.

A narrow compiler-enforced Columns/ByteOffset API rejects unit substitutions;
a source/AST confinement rule can prevent a new ad hoc wcwidth loop elsewhere.
Neither can infer a correct physical width. Literal external-terminal coordinates
for the confirmed counterexamples, plus readable stable screen expectations and
partial-write/input checks, bear on those claims. Keep the oracle from deriving
its expected width from the same map under test. Mode2027/profile negotiation is
an explicit separate compatibility decision, not proof that all desktop Ghostty
instances share a bare-backend default. [Terminal study](TERMINAL.md).

## Implementation and qualification order

1. Participant emergency archive custody and native Child lost-custody signal
   authority: separate small owner changes with their decisive original/child
   oracles. These are the highest consequence findings, already queued P1.
2. Native effect/refusal observations through participant/config/task engine
   paths; file post-effect result preparation/receipt as its own small unit.
3. Mailbox permanent refusal, preserving duplicate and urgent-control semantics.
4. Shared display map/unit types, fixing grid malformed/fallback widths and
   composer coordinates together where the invariant crosses those paths.
5. Pilot capability annotations and narrow exhaustive-state/source-confinement
   rules on those now-explicit owners. Keep already-present nodiscard and normal
   analyzer/sanitizer gates; evaluate lifetimebound on actual borrowing APIs.

Research does not require a giant preparatory framework. Implement each complete
conceptual unit under a short written subplan, direct red/green, independent
concrete challenge once, and fixes within that same second layer. Use the existing
source gate for source changes, and run matched perf/deltas after terminal source
tranches as already required. No new timer, assurance panel, broad mutation/fuzz
campaign, producer-side metrics or library adoption is inserted by this plan.

[TOOLING.md](TOOLING.md) records which concrete diagnostics work on both profiles,
where the newer lifetime flag fails on Linux18, and what each mechanism misses.
The queue below remains the action owner; completing this research does not close
unimplemented product bugs.
