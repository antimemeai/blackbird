# Process delivery observation for the Lua SIGINT instrument

Study only, 2026-10-16. No product edits, builds, new raw records or tests run.
The source-commit hook owns tracked files/index. This ignored note supplies the
delivery-owner portion of PLAN.draft.md; root still issues the final owner plan.

## Smallest useful contract

Add one exact-key, fixed-output, try-lock observation to CommandJobs. Query names
the actual current runtime/session binding, canonical 32-hex job ID and nonempty
request ID of at most 128 bytes. The accepted native invocation envelope supplies
these; Lua never resolves a missing target from foreground/PID/neighbor selection.
The only action in this tranche is signal/interrupt with immutable arguments.

Return independent facts, not a single optimistic success enum:

* Query status: ready, busy, malformed, wrong/retired scope or unavailable.
* Residency: job missing; job resident but delivery key absent; reservation
  present (queued or selected); or delivery present. These are different facts.
* Actual delivery counters/flags, typed signal and worker-consumption status.
* Optional original admission/selection/control raw references; optional bounded
  syscall facts and optional actual retained operation references, as described
  below. An absent reference remains absent when capture is disabled/unavailable.

No catalogue is needed: the bridge already retains the keys it accepted. Perform
at most one jobs lookup and one reservations/deliveries lookup each under
try_to_lock, then copy fixed native fields. Existing limits are 256 resident jobs,
4096 delivery identities per job and 128 request bytes. Give delivery maps a
transparent comparator if required for allocation-free string_view lookup.
Do not inspect Value trees on query: cache the tiny op/signal tags when the owner
actually validates them. Existing validate_control whitelists names but does not
bound duplicate object-field count, so query-time Value::find is not a sound
fixed-work shortcut. No mailbox walk, delivery-history scan, journal scan,
allocation, wait, raw admission, drain, dispatch, cancellation or settlement.

## Source facts and honest labels

CommandJobs::request (src/command_jobs.cpp:1897) reserves a unique delivery's
history capacity before mailbox insertion. Permanent limits refuse synchronously;
transient PTY input pressure may wait. Exact duplicates share a reservation or
existing Delivery; changed arguments conflict. Signal reservations consume an
identity but zero input bytes. An exact duplicate can still enqueue another
mailbox entry; existing control dedup prevents a second worker delivery.

requests (:1986) constructs its whole output before selecting entries and marks
Reservation.selected before removing the mailbox entry. Reservation survives
selection and pins debt/retirement until control succeeds or explicitly refuses.
Selected does not prove CodingEngine called process: poll_commands
(src/coding.cpp:2624) selects a vector, then calls each request. A prior call can
throw, leaving later selected reservations intact. Observation must neither
silently relabel these as dispatched nor replay them to repair progress.

control (src/command_jobs.cpp:1669) installs Delivery and a signals queue entry.
Its matching-reservation release guard runs on both success and explicit failure.
A finished/exited target can therefore lose its reservation without acquiring a
Delivery. A ready query with no matching key is NOT proof of never-accepted,
cancelled or refused. Keep accepted-but-no-longer-reconcilable unknown unless the
bridge has the actual returned/refused owner outcome. Do not add tombstones or an
unbounded second history merely to turn this absence into a terminal state.

The worker (:800) sets failed from leader_exited, invokes signal_owned, sets done,
and pops the signal. Final cleanup (:1113) also marks unprocessed deliveries
done+failed. Consequently existing status=delivered means worker consumption,
not verified kernel acceptance or application interruption.

signal_owned (:524) can perform up to THREE native calls: process-group kill,
getpgid verification and leader kill fallback. It may send to both group and
leader. It currently discards their results except for choosing fallback. A
successful syscall does not prove the child handled SIGINT, and errno after
success is not meaningful. Present a consumed control as "dispatch consumed";
never infer "application interrupted" from command exit/terminal output.

## Actual provenance, without reconstructing it later

Existing raw sites (src/raw_command.cpp description) already carry:

| Boundary | Actual record |
| --- | --- |
| request/control call | 2013 exact text arguments; 2014 branch/result/error linked to 2013 |
| mailbox | 2015 enqueue linked to request; dequeue linked to that enqueue |
| signal native call | 2020 launch ref, delivery key, target, signal, method |
| native return | 2021 linked to 2020, rc and errno; getpgid additionally returned group |

Keep first accepted enqueue and selection refs on the existing reservation and
transfer them to Delivery at actual dispatch. Preserve the original controlling
2013 ref on Delivery; duplicate observations must not replace provenance with a
later retry. Existing ControlTrace::finish returns void, so its 2014 ref is not
currently retained natively. Query must not invent it from an ordinal.

For useful truthful SIGINT feedback, cache a fixed array of at most three native
call facts directly in the existing Delivery at signal_owned's existing calls:
method, target, signal, rc, errno, request/ref and return/ref. Keep call count and
an explicit no-attempt/consumed/abandoned distinction. Cleanup calls with empty
delivery key do not attach to an intentional interrupt. No new syscalls, new
capture calls, raw-capacity exemption or group-routing change is required.
If root chooses to omit this cache initially, the UI must stop at consumed/
unknown-kernel outcome; raw refs cannot be obtained through pure query today.

Semantic receipt is a separate missing seam. CodingEngine::call process
(:4830) returns CommandJobs::control's Value through generic operation. operation
retains operation.result (:3842 onward), but its local retain helper discards
SourceReference after emitting 2411 and returns only ApplicationRecordId. It
emits the actual attempt/terminal disposition and 2414 settlement record, then
returns a generic Value. The delivery owner sees none of those locators.
result_record is a fact index, not SourceReference. The queued process result is
the operation accepting control, not later signal completion.

If the instrument requires actual semantic links in this tranche, use one narrow
owner-issued handoff for the exact signal key: actual operation attempt/raw trace,
optional primary/fallback original reference, actual operation.result application
ID+SourceReference, and actual terminal record/disposition. Cache on the existing
delivery when present; expose the actual outcome to the already accepted bridge
envelope when control explicitly refused without installing a delivery. Cache
each fact only after its real transition; a retained result and a settled attempt
are separate optionals. A recording failure must not erase already observed
worker/native-call facts or imply safe replay. No journal lookup or change to the
generic operation-result Value schema is needed. Root must explicitly select this
small integration seam in the owner plan; pure projection alone cannot supply it.

## Lifetime and reconciliation

The native action bridge, not a Lua VM or Scene, retains the accepted immutable
target/arguments/key and last actual owner response. Compose/load/preview cannot
enqueue. Close/reload retires callbacks/presentation generations, not an accepted
delivery. New intentional trigger gets a new key; redelivery keeps the same key;
querying never redelivers. Changed arguments under the same key conflict.

The existing host already obtains shared_ptr<CommandJobs> from CodingEngine.
Keep the bridge under that runtime's lifetime, outside the replaceable Lua view.
Do not let a UI query release the final strong CommandJobs reference on the input
thread: its destructor joins workers. If readers must escape the runtime host,
use a nullable guarded retired endpoint rather than pinning workers through a
view. Last copied observation stays inspectable but cannot claim freshness after
owner/session retirement. Root supplies actual environment/conversation binding;
a scene generation is not a domain scope ID.

Job retirement deliberately removes completed delivery receipts once no pending
reservation/delivery/output/custody protects the job (:402). No job pin or longer
history is needed for this instrument. A receipt disappearing, lost response,
runtime shutdown or stale scope remains unknown/unavailable; never resolve a new
job from PID or automatically submit another signal. A deliberately new attempt
is a fresh action, presented as such.

## Decisive permanent RED cases for the eventual owner unit

1. Real jobs owner: request exact SIGINT key; query queued, select without calling
   control and query selected; snapshots preserve debt, mailbox, capture/frontiers
   and worker identity. With actual mutex occupied, query returns busy promptly.
2. Dispatch real signal to a fixture that counts handled signals. Lose the action
   acknowledgement and close/reload the view; query same key without dispatch,
   then deliberately redeliver same arguments and verify one worker delivery flow.
   Changed arguments conflict; two genuine trigger keys yield two delivery flows.
   Count native flows separately from potentially multiple group/leader calls.
3. Gate target exit between mailbox acceptance/selection and control/worker.
   Verify selected promise cannot retire early, explicit refusal is reported when
   actually returned, absence stays unknown otherwise, and no neighbor receives
   SIGINT. Query a retired job and stale runtime scope without any dispatch.
4. Exercise actual syscall cache using controlled kill/getpgid outcomes (e.g.
   ESRCH/EPERM and group verification fallback). Assert exact rc/error/method,
   no-attempt versus abandoned, and no claim of kernel success from done alone.
   Genuine RawRuntime decode independently checks cached 2020/2021 references and
   exact job launch/key/targets; capture-disabled refs stay absent.
5. If semantic handoff is selected: real CodingEngine mailbox process call links
   exact accepted key to actual retained result/attempt/settlement. Decode actual
   raw operation originals independently. Refuse result retention after admission;
   retain earlier dispatch facts and explicit recording unknown, with zero replay.

Existing mailbox tests already attack permanent ID/byte reservations, selected
debt/retirement, duplicate/conflict and transient unrelated/urgent progress.
Reuse their native seams rather than duplicate their whole suite. New tests attack
the observation/lifetime/provenance claims above. Behavioral unavailable-seam RED
precedes implementation; a missing declaration/compiler error is only setup.
