# One whole retained/index owner code and oracle attack

2026-10-13. This is the semantic owner's ONE complete code/oracle review,
replacing the pending root review. Stable-method study preceded caller completion;
the final caller pass began after root released the whole source and Metadata
handed off strict/native green. No production or test edits, builds, provider
calls, dependency adoption or additional reviewers were used by this reviewer.
Source coordinates below refer to the reviewed candidate before these repairs.

The candidate requires the four corrections below. Existing native greens do
not exercise these triggers. Findings and direct cuts were handed to Metadata and
root during this same pass; their fixes and the affected direct recheck belong
to the existing bounded unit, not another review panel.

## S1: public inspection can consume a settlement bank

`src/retained_state.cpp:649–723` (`fact`) and `726–767`
(`FactHistory::page`) construct their request and the 65-byte3154 selection
using the current origin without rejecting `Input::no_new`.
`src/raw_retained.cpp:48–59` grants any requested semantic amount by splitting
an available loan; `no_new` rejects only the absence of a loan.

A selected stop scope with a resident fact therefore permits `root.fact(0)`.
The operation needs no physical read, returns the resident fact and emits3154
from the stop bank. A resident history page similarly invokes its read callback
and emits3154. The physical B=0 envelope does not prevent either case.

This contradicts the accepted subplan's M4 rule:3154 allowance is ZERO in
Context-stop, joined Job, receipt and exec settlement topologies;65X funds3118
existing-match observations. An extra public inspection must refuse before
acquisition/selection or have a separately admitted inspection envelope. A
large undifferentiated semantic bank is not that envelope.

Required repair: reject public inspection using these selected settlement loans
before splitting request/selection credit or invoking the reader. This includes
the demonstrated `no_new` case and the exec topology's zero inspection allowance.
Check the direct history
read closure as well, since the stop topology excludes that invocation. Keep
ordinary authorized inspection and internal lookup/existing-match selection
available. Do not increase the stop bank to absorb unauthorized inspection.

Direct cut: create a real selected Root with one resident accepted fact; admit
its actual stop bundle; activate the actual `no_new` scope. Invoke `fact` and
a resident `FactHistory::page`. Require capacity refusal, no reader invocation
for the page, and zero3154 within that scope. After scope restoration, ordinary
inspection must return the original RecordRef/event. The current fixture has
positive ordinary inspection and canceled Context stop, but no such negative
public-inspection cut.

## S2: terminal scope remains attached during later callbacks

`src/raw_retained.cpp:441–457` stores the full-exec third scope in `scope_`
and the generic terminal scope in `terminal_scope_`. They end in `leave()` or
the carrier destructor. In `src/coding.cpp:2397–2414`, `settle` activates this
scope; `2691–2712` performs primary and possible fallback settlement. The scope
remains attached during task emission and completion/output callbacks at
`2713–2758`. The earlier report's `EndExecScope` does not close a terminal
scope created after that report returned.

This is an active source defect, not merely retention of unused credit. The
selected journal's `RawStorageResource::scope_` still points at the old terminal
scope. `RawStorageLoanScope` rejects another attachment at
`src/raw_storage.cpp:883–920`. A one-shot `operation_completed` callback that
reenters an otherwise valid exec can admit its operation, then fail its own
prebody scope with `wrong_incarnation` before the child runs. Generic callback
reentry can instead perform its prebody Root work against the outer terminal's
semantic/physical bank and collide at its separate receipt attachment.

The accepted generic contract says body/reentrant work borrows none of the
terminal or receipt credit. The terminal bank authorizes the actual primary and
bounded fallback attempts, not later callback work.

Required repair: keep one terminal scope across primary plus possible fallback,
then end it before task emission or any completion/output callback. Preserve
exception-safe teardown if either attempt throws. Do not open a second terminal
scope for the fallback or silently lend the exhausted terminal bank to reentry.

Direct cut: on a real selected engine, install a one-shot completion callback
that invokes a normal nested exec or Lua call. Verify its independent effect or
result and live Root; compare native grant/scope records so the outer terminal
scope closes before the nested operation begins. Ordinary body-time nested
Lua-to-exec already passes; that fixture does not exercise completion-time
reentry.

## S3: scope copies mutable Root state before authority refusal

`RawRetainedScope` at `src/raw_retained.cpp:265–270` initializes
`archive_(root.archive_)` before checking
`semantic.permits_current(&root)`. A rejected foreign-thread caller can therefore
copy the mutable shared_ptr while the actual authorizing owner replaces the
catalog. Refusal comes after the unauthorized read and cannot prevent its race.

Required repair: initialize the catalog snapshot empty, check immutable
loan/root/current-issuer eligibility first, then copy the authorized Root's
catalog and inspect its query/index state. No producer, lock or new lifetime
mechanism is needed. `RawRetainedReadOrigin::snapshot` already follows the
correct guard-before-copy ordering at `src/raw_retained.cpp:128–140`.

The direct wrong-thread refusal cut must reach this scope constructor, not just
loan retirement. Source order must show that refusal precedes mutable Root
reads. Existing wrong-OS-thread physical retirement tests remain useful for
their separate cleanup fault class; they do not establish this constructor's
access ordering.

## S4: direct checked-read Error omits its owned writer state

Direct checked-packet failure at `src/journal_writer.cpp:954–973` changes owned
writer state to blocked. Its3165 emission at `981–988` emits that state only on
success. The site table and raw description distinguish direct writer-state
ownership from archive/lazy closures that do not own it. Metadata accepted this
literal source/layout defect in the same review. Preserve the direct owned state
on Error, with no acquired count when absent; archive/lazy closures still omit
the writer state they do not own. No state invention or extra read is required.

Direct cut: truncate the actual accepted native payload so the direct checked
read reaches native EOF. Require the business incomplete Error and blocked
writer state, exact physical acquired-prefix/EOF record, and3165's actual owned
blocked state plus Error, without an absent acquired vector's byte count. This
is a native-layout defect, not a claim of replay or lost physical Error bytes;
the physical read/return bytes remain present.

## Completed source examination

The review read the complete retained subplan and its M1–M6 attack, the current
doctrine/AGENTS/bounded-hardening rules, all semantic owner source and associated
headers, the literal raw description and the whole native retained fixture.
The final pass additionally traced actual Coding, CommandJobs, Terminal and main
callers and the corresponding physical scope/handoff methods and native oracles.

|Invariant|Actual implementation examined and conclusion before these fixes|
|---|---|
|Authoritative acknowledgment|`retained_state.cpp:1543–1636,1916–1959`: validates caller span cardinality before effects; only the outer acknowledged append fills caller metadata and sets its flag. Diagnostic children do not receive that span/flag. Publication uses reserved trivial metadata and swaps staged state.|
|Saved-state allocation|`retained_state.cpp:2920–3190`, `saved_state.cpp`: shared catalog and owned SavedState are staged before slot publication; selected-event/retirement credits are held before acknowledgment. No later owning allocation follows accepted slot publication.|
|Index/catalog ownership|Complete `recovery_index.cpp` and `archive_catalog.cpp`: actual immutable root/page/key/locator values; address-burn record prepaid before end mutation; actual split/entry/callback loops; no added metrics traversal or page re-encoding.|
|Replay/recovery|Complete retained replay, `journal_writer.cpp`, `journal_checkpoint.cpp`, `environment_head.cpp`, `retained_environment.cpp`: rollback preserves the accepted prefix, hints/derived slots do not create dispatch permission, native optional/error variants are distinct.|
|Cold-reader lifetime|`retained_state.cpp:773–947`, `journal_writer.cpp:994–1180`, `RawRetainedReadOrigin`: closures own the old file/catalog and shared immutable authority, not a live Root pointer. Origin eligibility precedes copying the mutable reader origin. Scope teardown restores old origins and clears a borrowed loan from a replaced catalog. S1/S3 identify the remaining boundary defects.|
|Ordered command admission|`coding.cpp:2135–2208`, `retained_state.cpp:1553–1564,1740–1754`, `raw_retained.cpp:343–432`: caller keeps exactly the actual three ACK descriptors, derives credits from existing encoded buffers before publication, and couples dispatch to the actual Admission attempt. No metrics-only encode or per-fact mirror.|
|Exec body/receipt LIFO|`coding.cpp:2310–2393,2441–2500`, `raw_retained.cpp:298–337`, `retained_state.cpp:2535–2631`: prebody attachment suspends before the real body and restores before receipt. The dispatch origin ends before its caller's outer scope. Resume/receipt refusal remains separate from the actual effect Result.|
|Generic receipt/terminal|Separate33887/F420 kind4 bank and107639/F8576 kind5 two-attempt bank are admitted before the ordered batch. Receipt scope is local to dispatch and ends before its enclosing origin. The terminal's primary/fallback share one scope; S2 concerns its late end.|
|Job terminal|`command_jobs.cpp:1089–1210,1821–1906`, `coding.cpp:1738–1751`, `raw_retained.cpp:483–570`: prelaunch child bank, actual worker join before borrowing, exact terminal ACK output, staged result before append, and acceptance flag handles an error after the append was accepted. Renew pays a new bank before another actual attempt. No original exec terminal is created merely because foreground waiting returned.|
|Turn stop|`coding.cpp:3957–4081`: one turn trace supplies existing Context/region banks; rollback borrows the current complete stop bundle for finish/view/linkage. Persistent aggregate protection survives while command debt remains. It is not a fresh canceled admission.|
|M6 main/worker transfer|`raw_retained.cpp:576–766`, `terminal.cpp:1296–1316,1497–1597,1734–1755`: partial preparation retains OLD; never-started failure releases on the actual child issuer; candidate installed before jthread; original MAIN bank released using issuing-thread proof; authority ends before complete; actual join precedes fresh MAIN bundle. Refusal retains joined-child debt and skips idle work/next worker until fresh admission succeeds.|
|Actual main selection|`main.cpp:211–230,256–258,300–306`: main alone configures256MiB DATA/1MiB CONTROL and supplies an explicit product-storage owner to native directory and Root. Audit inspection opts out. General defaults remain unchanged.|

The final whole legal-input formula uses native **frame** extents:
N=5+3*ceil((I+32)/16MiB)+ceil((D+32)/16MiB).
Both full legal payloads cross the native16MiB transfer boundary, giving N13;
the exact16MiB Invocation fixture with a small Decision gives N12. The corrected
core is794488 CONTROL/138922517 DATA. Separate generic holder banks and actual
existing holds can exceed a configured lane: admission must refuse before the
effect. Arbitrary recursive maximum workloads are not promised to fit1MiB.

## Direct oracle and execution evidence

This reviewer read source and execution logs; did not run builds or binaries.
Metadata handed off strict/native green in
`context/retained-index-study/quota-boundary-recheck-build.log` and
`quota-boundary-final-native.log` (successful empty-output native run), plus
`caller-tidy.log` and `caller-final-fixture-tidy.log`. Root reported physical
native/static/CLI green and supported-profile execution; that integration work
belongs to root, not an additional review claimed here.

`tests/raw_retained_test.cpp` is1463 lines in the reviewed candidate. Its direct
mechanisms are materially stronger than no-crash checks:

* Saved selection measures the real allocation leaf/stack after actual slot
  rename/directory acknowledgment. The preserved old red has disk8/live6, while
  the current fixture requires zero post-ACK allocation and cuts the actual
  prepublication SavedState allocation, preserving the old selected generation.
* Native append compares every emitted prepared frame with independently
  CRC-validated disk framing, exact opaque bytes, actual cursor and RecordRef;
  request/return references are matched by actual producer/ordinal.
* Diagnostic acceptance verifies accepted rejection children do not acknowledge
  the refused caller's proposal, and malformed span cardinality has no effect.
* Canceled Context runs real finish(false), view and stop linkage under held
  banks after native socket EOF refuses fresh admission. It requires one actual
  physical stop scope and accepted publication changes.
* Environment create/reopen is compared with independently decoded head bytes
  and the required recovery-pending state.
* Generic receipt's real body writes an independently read effect, fills ordinary
  CONTROL headroom and signals native EOF; paid receipt retains its actual
  interrupted Error while fresh admission remains interrupted.
* Lua projection's actual allocation stack forwards the acquired32768-byte Q
  result while filling CONTROL and signaling EOF. The raw original is exact;
  the actual2417 stage7 interrupted return is checked, with no invented business
  terminal or result locator.
* The4096-byte result/native2048-cap case preserves the exact primary raw result,
  bounded actual capacity Error and one honest unknown terminal/no fake locator.
* Independent disk Invocation payload is exactly16MiB; its three actual archived
  acquisitions each emit16MiB plus32-byte remainder, with native3033 B/N12.
* Plain exec, no-child refusal, large Lua locator and body-time nested Lua-to-exec
  verify independent child effects, live Root and actual2/3 exec,1 receipt,
  1 terminal and1 joined Job scopes per corresponding owner.

The physical fixture now supplies the previously missing retained cold
concurrent-final-drop/closure, actual retarget negative and actual Terminal
normal/canceled/failed-jthread paths, as well as the complete child early-entry
and joined-debt/fresh-resume checks (`storage_raw_test.cpp`:
`retained_cold_concurrent_cleanup`, `combined_transfer`, `terminal_product_owner`).
These are the same repaired physical oracles, not a second semantic cleanup test
system. They do not replace S1's public-inspection refusal or S2's completion-time
callback reentry checks.

Reference grounding was read directly: pinned LMDB commit/write_meta/pick_meta
and transaction-thread lifetime API, SQLite walIndexRecover and LevelDB
VersionSet::Recover under the paths recorded in `QUARANTINE.md`. They support
the distinction between flushed bytes, selected metadata, derived acceleration
and serialized owner lifetime. They were studied, not adopted or executed.

The review is complete. Resolve the concrete findings in this same unit and record
the affected direct results. No review of this report, new panel, broad assurance
campaign, old process adoption or command replay is proposed.


## Owner disposition and direct recheck

The same reviewed unit resolves S1–S4; no second review was added.

S1 rejects public fact, history page and direct history-reader inspection before
selection when a borrowed settlement loan is installed. Internal3118 lookup
remains funded. The direct cut creates its real history before cancellation and
selects an independently verified resident suffix fact, so neither a fresh
history grant nor an archived read can mask the defect. Under the actual stop
scope all three public inspections refuse without3154/3144/3148 or reader
invocation; the same ordinary fact/read succeeds after scope restoration.

S2 ends the one terminal scope after the actual primary and possible bounded
fallback, before task/user/output callbacks. The one-shot completed callback
reenters the existing turn's actual CodingEngine::call, the same member used by
Lua, through a fixture-only explicit member-pointer instantiation. A second
operator_call would instead start a top-level turn and legitimately refuse busy
while the first owns settlement protection; that initial oracle was corrected.
There is no production API or callback adapter. The child writes the independently
checked target, and outer3036/3037 precede nested2400 on the actual producer.

S3 proves the loan's immutable issuer/thread permission before copying Root's
mutable archive pointer or selecting its current resources. Scope restoration,
suspend and resume use the same proof. A real foreign thread temporarily bound
to the same handle invokes the actual constructor and receives wrong_incarnation
before any scope or Root selection. Legal joined and ordinary lifetime paths
remain covered by the existing whole physical fixture.

S4 emits the direct writer's actual state on both Result branches. Failure has
no acquired-vector count; archive/lazy closures still omit state they do not own.
The real cut preserves the file, truncates after the actual32-byte frame header
and one payload byte, observes the exact33-byte native prefix and EOF, checks
blocked/incomplete fields and restores the independently CRC-checked original.

Matching pre-repair headers and three linked archives are preserved under
ignored context/retained-index-study/review-before-fix with ABI notes and hashes.
The tightened S1/S4 and corrected S2 oracles each reproduce exit1 at the intended
assertion on those archives. Current strict Debug link passes; all three native
cases pass15.90s. Five changed TUs pass narrow clang-tidy and six changed
source/header files pass dry formatting. Exact logs: review-s1-red.log,
review-callback-old-red.log, review-s4-red.log,
review-callback-current-build.log, review-fixes-final-debug-native.log,
review-fixes-tidy.log and review-fixes-format.log in that ignored study directory.
Supported profiles and the mandatory source gate remain the root owner's
qualification, not an assertion made by these Debug results.


## Required source-gate integration fixture corrections

The mandatory source hook subsequently exposed obsolete admission/oracle
assumptions in four preexisting real fixtures. No new production defect or
review layer was inferred from them.

The turn policy and command/wake exec fixtures used4MiB/2MiB DATA lanes, which
refused operation17's16MiB original grant before their intended effect or
cancellation cut. Turn now has32MiB DATA; the two exec fixture lanes use the
ordinary64MiB/256KiB DATA/CONTROL sizes. Wake still publishes the exact2MiB
original, holds the rest of its whole lane's credit and parks real admission
with the recorder stopped. It closes each unused custody-pipe end, with the
actual fatal/native EOF/alias/original assertions unchanged.

Context's canceled/full-CONTROL cut now checks exactly three328-byte region
withdrawals using independent reviewed native layout276+52, not the production
credit helper or the obsolete fixed432 expectation. Command's existing synthetic
four-observation burst declares its three extra event observations, preserving
four432-byte bursts and the original1728 bank. Its two real canceled exec paths
independently retain four caller prepaid regions; the launched path still has
one actual joined-worker settlement, and the prelaunch refusal has none.

The policy-child test had deliberately expected the former unpaid receipt to
close the audit. It now verifies the repaired contract: actual child
interrupted/ECANCELED survives, Root stays live, and one independently retained
AdapterReceipt contains that exact failure. Its native child/parent validation,
policy activation and final held turn-terminal assertions remain unchanged.
Current strict fixture links and all four direct Debug cases pass8.16s with the
existing watchdogs. Logs are under ignored context/retained-index-study with
legacy-fixture-qualified-* names. Root owns the supported affected profiles and
mandatory source-hook retry.


Root completed supported-profile integration and the installed source hook in
source ca6747b:110/110 Debug cases in146.29s, all161 owned C++ translation units
and owned Lua checks. The existing owner/finding oracles pass affected Mac/Linux
Release and ASan-UBSan profiles. The required hook's seven obsolete integration
fixture assumptions are corrected and their direct/profile checks pass; native
byte, failure, custody and publication assertions remain strict. Full disposition
and remaining scope are in [the completion plan](../docs/INSTRUMENTATION_COMPLETION.md).
nls.2 remains open; this completes the present owner segment.
