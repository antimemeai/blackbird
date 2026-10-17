# Native action handoff

2026-10-16. This unit connects the already phase-checked Lua intent inbox to the
existing CommandJobs mailbox. It owns no signal implementation and never calls
control() as a query. The actual process owner retains reservations and delivery
facts. The bridge keeps only a bounded recent presentation cache.

Create one bridge for the actual environment/conversation, live CommandJobs owner
and inbox, before keyboard processing. A native worker, registered with raw
transport before dispatch, reads accepted entries in acceptance order. Validate
the exact scope, owner string, physical owner incarnation, canonical job ID and
interrupt verb. Observe the exact target; a missing or exited target refuses
without replacing it with another job. This last observation is advisory:
CommandJobs admission is still authoritative if the job exits concurrently.

Before dispatch, hold an immutable envelope and derive one request_id (<=128
bytes) from the entire native ActionKey. Submit exactly one process signal request
to CommandJobs::request, then acknowledge the inbox only after preserving either
mailbox admission or an explicit refusal/unknown. Never resend after an exception
or lost return. Subsequent reads query try_observe_delivery with that same key.
Queue consumption and syscall return are separate facts; neither says the target
application handled an interrupt. No semantic operation locator is fabricated.

Expose bounded try-lock snapshots of the last16 dispositions, including their
native envelope and delivery query/facts. Retiring a derived cache row never
authorizes repetition. Accepted inbox entries do not depend on view, source or
VM lifetime. Stop wakes actual raw waits, joins the native worker and retains
undispatched accepted entries in the independently owned inbox. No detached work.

Raw sites4095 (forward arguments plus original envelope/input link) and4096
(return branch linked to4095) belong to this unit. Capture precedes forwarding;
raw failure prevents new dispatch or leaves the original key explicitly unknown.
Existing process admission/enqueue/control/syscall records remain authoritative.
The worker may wait for raw capacity; keyboard, paint and Lua callbacks may not.

Permanent behavioral RED must compile against a refusing scaffold. Cases cover
scope/target rejection, exact one-mailbox handoff, same-token dedup after inbox
acknowledgment, distinct triggers, receipt query without resubmission, owner/view
retirement, exited target and cooperative stop. Use the real CommandJobs mailbox
and actual fixture process where a domain effect is needed. One independent
implementation/oracle challenge and fixes, then relevant profiles; no extra panel.

Files: include/blackbird/tui_action_bridge.hpp, src/tui_action_bridge.cpp,
tests/tui_action_bridge_test.cpp, BRIDGE_RESULTS.md. Root owns build registration
and the separate native scene/source adapter. No new dependency or external API.

## Concrete owner subplan

NativeActionBridge is the sole consumer for one scope/inbox. The runtime owns it
across all presentation reloads. BridgeScope holds actual typed environment and
conversation IDs. owner() returns their lowercase32-hex strings separated by `/`,
matching the command center. Targets require kind `command`, that exact owner,
physical CommandJobs incarnation and lowercase32-hex job key; verb `interrupt`.
Constructor bootstraps the actual physical incarnation before input begins.
The complete ActionKey maps to an81-byte request ID:
`bb-ui:<inbox32>:<owner16>:<trigger16>:<ordinal8>` (fixed lowercase hexadecimal).
No scene/source/view generation is substituted for the accepted native key.

create prepares a worker producer and a dedicated RawCommandWake::signal before
starting a thread. Only the worker performs raw/domain admission and mailbox
handoff. notify merely wakes its condition variable; it also checks the bounded
inbox periodically. Snapshot reads try-lock the bridge cache and copy at most16
fixed native receipts, without querying or dispatching. The worker refreshes
existing receipt queries and preserves last-good facts separately from busy/
unavailable query status. Cache retirement cannot re-enqueue a key.
The constructor emits actual raw_worker_dispatch before spawn; the bound worker
emits raw_worker_started under its stop-aware raw wait scope before processing
an inbox entry. Failed registration/dispatch/start never forwards an action.

The worker handles one accepted envelope through preservation and inbox
acknowledgment before advancing. Stop cancels actual raw waits and leaves every
unattempted envelope in the inbox. After a forward call, it first stores the
fixed receipt and finishes acknowledgment, even if stop was requested; inbox
methods hold only finite native copies under their mutex. This prevents a
previously attempted entry surviving shutdown solely because acknowledgment
briefly returned busy. No automatic retry of CommandJobs::request occurs.

Raw4095 is DATA: packed native ActionKey(inbox16,owneru64,triggeru64,ordinalu32),
Dependencies(environment16,conversation16,view-owneru64,view-incarnationu64,
source-generationu64,request-sequenceu64,scene-revisionu64,local-input-revisionu64,
query-revisionu64),acceptance-orderu64,input-originR, then counted native Text
target-owner,target-kind,target-key,target-incarnationu64,handler,verb,derived
request-id,forward-op=`signal`,forward-signal=`interrupt`. Each Text is size_t
length then exact bytes. No C++ struct padding is emitted. Raw4096 is CONTROL:
forwardR,BridgeDispositionu8,forward-calledbool,has-errorbool,ErrorCode,i64.
Error slots are zero and ignored when has-error is false. The worker prepays
4096 before4095/forwarding and uses an active RawCaptureFailureScope to separate
raw failure from an identical business error. A failed post-forward capture
retains the exact key as unknown and halts new dispatch; no second attempt.

Permanent RED first compiles the refusing create seam, then real Lua handlers
accept envelopes into the actual inbox and demand a real mailbox handoff. GREEN
cases cover exact handoff/receipt/dedup, scope/target/exit rejection, recent16
retirement without replay, raw original/return links and cooperative stop with
unattempted entries retained. Root performs the single implementation/oracle
challenge after GREEN, then selects scoped profiles. No shared Ninja without lease.

The reload/lost-return oracle deliberately discards presentation ownership and
reads the actual original-key receipt without resubmitting; it does not inject
an arbitrary OS acknowledgment loss or qualify durable recovery. No globally
exactly-once or post-restart reconciliation claim is made. RawWorkerDispatch
lineage is decoded against the actual forwarding worker producer independently.

## Hook busy fixture correction

The final hook provides actual RED: recent exits with native busy while251 other
cases pass. Preserve the exact pre-fix test/hash in ignored bridge evidence.
ActionInbox::new_trigger returns busy before incrementing its trigger counter;
try_read returns busy before copying and never accepts an envelope. The fixture
must wait for those explicit busy results, using its existing5-second polling
deadline. Non-busy failures retain their code/detail and identify the attempted
call. Read predicates treat busy as not-yet-observed rather than throwing or
nesting an additional deadline. Standalone reads use the same finite deadline.
No handler/outcome, effect admission or uncertain forward is repeated. Preserve
accepted_requests, all20 exact mailbox handoffs, order, dedup and recent16 facts.
Run only the existing owning5 cases and actual owning-test TU lint. The hook's
noncontextual message does not prove which of the two inbox call sites contended;
the correction follows both documented nonblocking contracts, not a new product
race claim. Root owns the additional test-only publication delta.
