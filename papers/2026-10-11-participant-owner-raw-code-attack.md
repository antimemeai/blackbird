# ONE participant code/oracle attack

Root reviewed the returned whole participant candidate after its actual three
focused debug cases passed and its owned changed translation units passed static
analysis. The provider-owned optional-access finding in the shared Coding unit
is separately assigned. Scope: Participants state/queue/native wake, typed raw
helpers, original primitive, default transport/immutable operation identities,
mandatory-first cleanup, and native/business fixtures. This is the one source
and oracle attack; fixes and direct rechecks follow without another panel.

## P1 — retention callbacks can reenter an unfinished owner action

The drain guard protects a pending capture front, but it has ended before start
or send invokes Retain. Public controls remain callable on this same owner thread
from that callback. Their validation happens before Retain; their staged map-node
insertions ignore the insertion result afterward.

Actual current-candidate red, using public Participants with raw inactive:
context/participant-reentrant-send-red.cpp compiled against its real libraries
and ran exit1. The first send's Retain callback sends the same message once.
Both sends return accepted; actual read reports queued=2 and receipts=1. One
accepted receipt now permits two worker deliveries, contrary to duplicate
acceptance. No sleep, source hook, synthetic queue or model was used.

Start has the related lifetime defect: reenter start with the same ID while the
outer Run is still staged. The nested call can insert that ID. Outer insert then
returns its rejected node, which the ignored result destroys; its saved pointer
is subsequently passed to snapshot/publication/worker construction. Reentrant
archive can similarly invalidate a Run borrowed across send Retain. These are
one missing owner-transaction boundary, not independent requests for a registry.

Resolve explicitly before those callbacks: refuse overlapping owner mutation
with the native busy result, or preserve/revalidate actual ownership and consume
every insertion result safely. Keep mandatory native cleanup available and
retain the already accepted semantic original. Do not hold the participant mutex
across callbacks. Add actual reentrant same-message and same-ID cuts; check no
second delivery, no freed Run use, no leaked endpoints and honest raw outcome.

## P2 — failed native shutdown still falls through to a potentially blocked join

Impl::stop publishes the actual shutdown result, closes activation regardless,
then mandatory cleanup joins. If shutdown fails while a worker is parked in raw
admission and a stopped pre-exec child holds an activation alias, closing this
descriptor leaves the kernel observer unready. The condition-variable notification
cannot wake that raw poll. Its wait control has the observer and no cancellation
callback. The held join can therefore never complete.

This is the same concrete native failure class resolved in the shared command
wake plan, which distinguishes actual successful shutdown from failed shutdown
and exits the owner after publishing failure when borrowers may exist. The
participant fixture currently only records successful native shutdowns; full
capacity plus a stopped alias does not test a failed shutdown.

Resolve a borrowed-owner failure policy before dispatch, without another wake
registry, retries, timer or callback. For potentially borrowed endpoints, publish
actual failed2123 then end the owner with the existing _exit(70) policy; independent
raw preservation and ordinary reopen retain uncertain effects. Before dispatch,
ordinary setup refusal and held endpoint cleanup remain available. The dispatch
publication flag must precede thread launch; checking jthread.joinable from a
worker is both too late at launch and the wrong ownership boundary.

Direct isolated cut: real parked admission, confirmed stopped inherited alias,
actual selected shutdown EBADF/native denial. Require failed native result and
owner exit70 before any join/false END. Parent cleans only its actual owned PIDs
and ordinary reopen reports unresolved work honestly. A resumed alias cannot
serve as the successful failure-path oracle.

## P3 — multiple prepared tickets can overwrite the single emergency original

Receiver::prepare refuses an occupied emergency slot but has no outstanding-ticket
ownership. The public capture interface and SourceLease permit two leases to be
prepared before either completes. The receiver's first overflow completion fills
the emergency node and stops acquisition; the second already prepared/acquired
completion replaces participant.emergency by move. That destroys the first node.
Later drain can retain only the second and can clear all reported semantic debt.
Raw preservation of the first body does not make that semantic ownership claim
true; with raw inactive there is no raw fallback either.

The plan assumes one physical source per worker and says a crossing acquired body
always has its own slot. Enforce that sequencing before acquisition rather than
assuming the caller already obeys it. A receiver-local active-ticket guard is
sufficient: refuse a second prepare while the first owns that slot, release it on
normal handoff or abandoned preparation, and keep the occupied emergency refusal.
Do not add another queue or process. Every already acquired original must still
transfer before business capacity is raised.

Direct cut: two actual source leases against the real participant receiver, with
capacity large enough that source admission itself is not the refusal. The second
prepare must fail before its acquisition; after an exact crossing first body or
unknown initialized first extent, the original/debt must remain owned. Include an
abandoned pre-acquisition ticket followed by an allowed fresh prepare.

## Checked

Finite banks, same-issuer split/release, shared source references, literal native
body checks, distinct request/result records, stable front across unlocked Retain,
unknown-used debt and no parse/retry, post-ack Original return, preallocated
direction/receipt storage, all queued-direction withdrawal, and mandatory cleanup
paid before publication were reviewed. The original nine-MiB loss and stop-after-
wait baseline reds are genuine; the current native green checks those selected
claims. No extra encoder/metric transform, synchronization service or dependency
is proposed. Above findings need their direct fixes; this report does not reopen
settled recorder/sampler/previous owner qualification or close the full census.

## Finding disposition and direct recheck

P1 is fixed. Actual owner Retain, Save, Publish and cancellation-predicate
callbacks refuse ordinary control reentry before argument acquisition; mandatory
shutdown remains available. Both staged map insertions consume the actual
insertion result. Start revalidates stopped/closed state before dispatch. Worker
publication does not touch the owner callback flag. The native fixture checks
same-ID start, same-message send and archive from Retain, read from Save and
configure from owner Publish: exactly five linked busy outcomes, one Run/socket
pair, one accepted receipt, one direction withdrawal and four successful native
closes. It also performs mandatory cleanup with actual failed shutdown inside
prelaunch Save; no worker is launched and the accepted original remains owned.
The genuine public-API P1 baseline remains in context/participant-reentrant-send-red.*.

P3 is fixed. Receiver owns a single pending ticket before source acquisition.
Preparation failure, abandonment and completed handoff release it. Before source
changes the actual overlapping-lease cut ran exit1: native A and B were preserved,
but only B reached semantic retention and debt cleared. After correction the
second acquisition never happens. Known A is retained byte-exact (9MiB); unknown
initialized A stays owned with semantic debt and no Retain. Abandoning a prepared
ticket permits a fresh preparation. Direct logs: context/participant-overlap-baseline-red.log
and context/participant-ticket-candidate.log. Bank171 and original layouts stay unchanged.

P2 is fixed. A borrower flag precedes native thread construction and is cleared
only after native join or failed construction. Actual failed2123 is published
before exit70 while borrowers may exist. Prelaunch cleanup uses the ordinary
held path. The genuine parked-worker/stopped-alias red reached blocked join after
forwarded native -1/EBADF (context/participant-shutdown-baseline-red.log). Parent
cleanup targets only its actual owned owner, alias, recorder and supervisor; it
observes actual catalogue-lease release before reopening.

The P2 recheck exposed a real recovery defect: orphan lanes have no wake endpoint,
but the parked producer left space armed. Acknowledgment advanced A then failed
raw_send_notice(-1), emitting8000 invalid_range/detail0/stage4 and leaving the
owner's failed2123 undrained. The preserved original red remains at
/tmp/blackbird-participants-ZHi0sq, with diagnostic log
context/participant-orphan-diagnostic.log. Before helper changes the direct native
armed/-1 reproduction also ran exit1 with P=A16. The narrow transferred fix is
written in [the quiescent reclaim subplan](2026-10-11-participant-quiescent-reclaim-subplan.md).
Only recover_orphans assigns a quiescent lane after the dead-owner/exclusive-lease
fence. Both recovery-prefix and post-publication acknowledgment share the same
event-boundary/cursor advancement primitive; only explicitly quiescent lanes omit
the obsolete wake. Ordinary invalid endpoint and boundary errors remain errors.
There are no retries or swallowed errors.

The original preserved red now reopens and closes successfully: one native shutdown
request, one failed return, no join/workerEND/ownerEND. The fresh death cut compares
the exact forwarded fd/how/result/errno and request linkage on disk, requires
exit70 and no fabricated END, and passes. The same native fixture checks ordinary
armed/-1 refusal plus quiescent exact/partial/past-P boundaries. Qualification
helpers/logs stay ignored under context; the original preserved directory stays
available. A fixture shared-memory name initially exceeded Darwin's native name
limit; using its actual mkdtemp suffix corrected setup without changing the oracle.

Final focused debug cases raw_channel, raw_runtime, colleague_engine, participants
and participants_raw pass5/5 in9.73s (native7.09s), log
context/participant-findings-final-green.log. The original-run direct green is
context/participant-orphan-reopen-green.log; the fresh death green is
context/participant-quiescent-shutdown-green.log. Participant source narrow static
passed after removing the old no-op check of the moved map node; final channel,
runtime and fixture final narrow static also pass exit0 with no diagnostics,
log context/participant-findings-final-tidy.log. Source and document diff checks
are clean. All participant and transferred recovery source is idle for root's
supported profiles/integrated gate. No additional panel, broad suite, commit,
merge or census-completion claim is made.

## Supported-profile field lifetime finding

Root's actual MacASan profile exposed stack-use-after-scope at Receiver::prepare
line526: f(request.label.size()) borrowed a temporary native length into a
ByteView array used in the next statement. Current request.maximum_bytes was
already a member; both receiver lengths now have stable locals. A focused same-
pattern source inspection found restore's two lengths and send's four lengths
in the same delayed arrays; those now also have stable locals. Inline event
initializer-list values are consumed within their full expression and need no
change. No public schema, bank or ownership policy changed.

Root's actual Release overlap oracle lost the abandoned/refused preparation
labels for the same expired length-view reason. The original independent body
oracle passes unchanged after the source fix; no fixture reduction or changed
expectation. Genuine profile reds remain context/nls-owner-release.log and
context/nls-owner-asan.log. Exact participants_raw rechecks PASS: debug7.23s,
Release0.90s, MacASan20.61s, individual context/participant-profile-lifetime-*-
green.log files. Narrow participants.cpp tidy exits0/no diagnostics, log
context/participant-profile-lifetime-tidy.log; diff check clean. Source idle, no
second review panel or full suite. Linux qualification remains root-owned.

## Integrated PTY oracle finding

The installed source gate's participants_pty case rejects a surviving participant
whose actual JSON result is present on the displayed cells: LOCAL_SELECT ends a
chat row and ED_REVIEW begins its immediately adjacent continuation. Existing
Screen.text inserts newlines between display rows. Requiring an unbroken marker
in that physical-row string confuses wrapping with a missing result.

Correct the existing PTY oracle to read contiguous chat-rail rows within the
actual header divider, excluding the independent task/sprite pane and resetting
at blank/label rows. Compare the exact marker and actual_model in that displayed
paragraph. Retain the independent fake child and all cancel/join/task/restart
checks. Strengthen its final actual CLI reopen check to parse the returned external
JSON and compare selected run_id, completed state, exact result text and actual
model, rather than loose substring presence. This fixes the existing semantic
and display oracle; no renderer/source change, generic screen parser change or
new panel. The displayed paragraph is parsed as the actual external JSON;
run_id must equal the selected beta, state must be completed, and result.text
and actual_model must exactly match the fake child's output. The selected-result
check rejects the earlier grouped join presentation, so stale join text cannot
satisfy the post-restart selected read. Current Mac debug/release/ASan direct
checks pass1/1 each in2.09/2.44/4.32s; logs are
context/participant-pty-wrap-oracle-{debug,release,asan}.log. Linux checks for
this and the two borrowed engine fixture findings follow; production rendering
and participant ownership are unchanged.

The same Linux finding recheck passes both engine cases but rejects join with
invalid_range. The displayed IDs were read from Screen cells after arbitrary
os.read chunks. Current Participants::join rejects an absent selected ID; it
does not reject an actual running or waiting Run. ChatPainter::prepare brackets
each real delta with ESC[?2026h/l, but Screen applies cells incrementally and the
old wait could observe a hybrid of preceding and partially replaced IDs.
The existing wait now tracks those real frame delimiters across read splits
and evaluates its predicate only outside the frame with no pending control.
It does not add a delay, retry join, change the renderer or infer completion
from elapsed time. The genuine Linux red is run-9t84003k/checks.log. Exact Mac
debug/release/ASan direct rechecks pass. Final affected Linux three-case
debug/release/ASan rechecks pass3/3 each in2.66/1.89/6.59s; actual PTY case
times1.58/1.46/2.57s. Logs run-4bme3mn8/checks.log and
context/participant-pty-complete-frame-{debug,release,asan}.log.
