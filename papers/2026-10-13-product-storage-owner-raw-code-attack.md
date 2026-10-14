# Physical product-storage owner: ONE complete code/oracle attack

2026-10-13. This replaces the pending root physical code/oracle attack; it is not
another review layer. Read the complete physical subplan and its accepted plan
attack, raw_storage.hpp/cpp, journal_storage.hpp/cpp, diagnostics.cpp, physical
FramedJournal write/cold-reader seams, Terminal authority transfer, actual retained
maintenance pause/restore and transfer lifecycle. Read all twenty-one native
groups and their interposition/byte-reader support in storage_raw_test.cpp.
Current BLACKBIRD/AGENTS/two-layer instructions apply. No production/test edits,
builds, runtime execution or new agents. Findings describe the source inspected
before their repairs, not assurance of a subsequently changed candidate.

Coordinate physical byte/reader findings directly with raw_reader; coordinate
actual cold-history coverage with raw_metadata_recovery. Reader confirmed the same
four source findings; Metadata confirmed the cold final-drop coverage limit. This is one whole
unit report, not a remaining-census or Beads review. Whole-main opt-in stays held
for the already identified semantic caller/positive-read/capacity composition.

## Required source repairs and direct oracles

### P1. Diagnostic input is emitted twice against the accepted byte contract

`src/diagnostics.cpp:189–190` emits name and the whole input as3004 operands.
`RawStorageNative::write` then emits the actual same native input in3009.
The accepted subplan's native layout section expressly says diagnostic input
needs no extra3004 body: the existing action length and native3009 slices supply
the actual representation. Current source duplicates up to65536 bytes plus49
framing, adds an unnecessary DATA grant before validation, and can refuse a
diagnostic even though the agreed native-input/cleanup grants would fit.

Repair: remove the duplicate body path. Preserve the actual input length in
3002, reached exact native3009 inputs and original name before validation. Do
not replace it with another input encoder, digest or content registry.

Direct oracle: actual store with independently authored binary input, exact
3009 bytes/count and file bytes, plus zero3004 operand2 body. Current
diagnostic_prefix/enumeration groups only assert read preservation/expiry and
would accept the extra full body. An exact-capacity case should ensure that the
removed duplicate no longer consumes the native write's required headroom.

### P2. Failed-write unlink admission can replace the primary caller error

`src/diagnostics.cpp:208–226` stores the actual write error, closes the file and
calls RawStorageNative::unlink with ordinary fresh admission. That call constructs
RawStorageCall and claims CONTROL/DATA before its native request. If capacity or
sticky cancellation refuses there, the thrown Error escapes to the outer
capture wrapper and replaces the existing write Error. A write `-1/EIO` can
therefore become caller `capacity` or `interrupted`, and the failed diagnostic
can remain in the namespace. The original native EIO record still exists, but
the agreed preservation of the primary product error does not.

The old code ignored the unlink's native result and returned the write Error;
this added admission failure must not silently change that precedence. The
physical plan explicitly requires primary read/write error to remain distinct
from later close/unlink failure. Close has paid cleanup; this failed-write unlink
does not. Its original pathname must also remain alive through paid cleanup.

Repair this finite obligation inside the same owner: prepay one typed diagnostic
failure-unlink request/return and exact CString body before file creation/first
write, on its actual issuer. Its native fields are already owned: return49 CONTROL,
request81+CString length DATA. Success releases unused claims and emits no fake
unlink. Failure consumes the prepared pair once and returns the original write
Error regardless of the native unlink result. Retain the unlink's actual native
error separately; no new producer, retry timer or registry. An acquisition-time
failure before successful creation must not unlink a different existing file.

Direct oracle: forwarding selected write EIO (and zero-return/io0), then make
ordinary CONTROL/DATA unavailable and cancellation sticky before cleanup. Require
the unchanged caller EIO/io0, one real close, one paid unlink of the actual newly
created diagnostic, exact3021/3022 operands/outcome and no fresh grant. A separate
native unlink failure preserves that native errno while caller remains the first
write Error. Current prefix red faults diagnostic read, not this write/unlink
precedence path; none of the twenty-one groups reaches it.

Reader subsequently ran genuine P1/P2 reds against the preserved matching current
headers/libraries before source repairs. `context/storage-owner-findings-baseline/
duplicate.log` reports successful business store, one exact3009 input and one
duplicate3004 operand2, exit1. `cleanup.log` uses actual external descriptor close
then forwards the same native write arguments: kernel EBADF9 is preserved in
3010, sticky cancellation becomes ready before return, but store returns
interrupted/detail89 and invokes zero actual unlinks, exit1. The actual observed
primary error is EBADF, not synthesized EIO; it directly establishes the same
precedence/admission defect. Baseline source is `red.cpp` in that directory;
Reader retained native fixtures ending T3Wqui/qxHizx. These are attributed direct
results from the repairing owner, not executions by this reviewer or results of
a repaired candidate.

### P3. A legitimately replenished unlock_failed bank cannot be spent

`src/raw_storage.cpp:500–513`: prepare_release replenishes an exhausted bank;
acquired_writer(false) preserves the previous unlock_failed state and the new
bank. release_writer then accepts only Lease::held and returns without issuing
LOCK_UN for funded unlock_failed. This contradicts the accepted S1 state machine.

Concrete sequence: successful LOCK_EX; LOCK_UN returns EINTR and spends its bank;
an ordinarily admitted lock_writer prepays a new238 bank but its actual LOCK_EX
returns EINTR; subsequent release_writer has a real unresolved lease and a funded
bank yet issues zero native release calls. Final close eventually releases the
description, but the permitted paid release has been skipped.

Repair: allow held or unlock_failed when funded; reject unheld and spent banks.
Retain one actual call and exact errno/outcome. Do not regain credit on automatic
release, add retries or equate a failed release with unlocked ownership.

Direct oracle: extend the actual native flock fault cut with that failed admitted
reacquisition followed by exactly one paid release. Check native invocation count,
original resource Ref, real other-description contention before successful release,
and zero extra calls on subsequent automatic release. Current single_unlock_failure
only checks failed unlock → repeated spent release → final close. It never tests
the newly funded unlock_failed branch.

### P4. readdir names use the wrong native extent and lack the promised failure path

`src/raw_storage.cpp:1488–1526` prepays `71+sizeof(dirent::d_name)` and scans to
that same compile-time array end. It does not use native d_reclen for the record
extent, does not frame Mac's native d_namlen and does not return the promised
product error when the admitted entry has no valid bounded terminator.
The business callers later perform unbounded char_traits::length/classification
on the returned d_name (`journal_storage.cpp` scratch pass and diagnostics purge).

This is a legal Linux native case, not solely malformed test data. The already
pinned man-pages6.19 readdir.3 explicitly warns against sizeof(d_name) and states
that CIFS/Windows SMB names can exceed255; d_reclen can exceed glibc's struct
size. The source's256-byte scan can emit a truncated prefix without NUL and then
let business code use the full unpreserved returned name. For a malformed short
record, scanning the declared array rather than the actual record extent also
violates the accepted no-search-beyond-returned-record contract.

Repair: derive and prepay the native maximum from the actual record-length field
type and member offset, not sizeof(d_name). After readdir, frame the actual name
within the returned extent; retain Mac native namlen and validate its extent/NUL
consistency. Preserve the bounded acquired malformed entry then return a real
product corrupt/error before business strlen/parsing. Do not interpret d_off or
convert the name. Keep exact successful/error/EOF variants and ignored error
returns distinct. Update explicit DATA maxima and ABI description if required.

Direct oracle: forwarding readdir wrapper supplies a valid allocated native
record with a >255-byte Linux name and d_reclen exceeding struct size; compare
the literal unchanged name through NUL and native length fields. Separate short
record/no-NUL and inconsistent Mac namlen cuts must reach product error before
business classification and close the stream once. Ordinary real native entries
still forward unchanged. Current enumeration_and_retention has only short valid
names and cannot find this truncation or invalid-extent behavior. The manual is
in the intact archive `quarantine/raw-recorder-2026-10-10/man-pages-6.19.tar.xz`,
member `man-pages-6.19/man/man3/readdir.3`; no new literature acquisition is needed.

## Exact oracle gaps within the accepted lifetime/retarget claim

**Concurrent cold lifetime.** The accepted plan's source-grounded additions3–5
require actual FactHistory and replayed ImmutableBytes surviving Root/Context,
two simultaneous final-drop threads, old/new catalog last drops across actual
TUI/main while UI producer publishes, and final cleanup END after all resource
ENDs. The fixture currently does a main-created shared JournalFile drop on one
worker, a worker-created shared JournalFile drop on one cold thread
(authority_and_cold_cleanup), and a payload_reader after FramedJournal reset
(actual_journal_consumer). These are useful and different from those promised
ownership paths. There is no simultaneous final drop in this file and no actual
FactHistory/replayed ImmutableBytes final ownership case. Metadata confirms its
fixture only reads pinned history on the sole authorizing thread.

Complete the direct lifetime oracle already in the plan, with real closures and
independent native close/END observations under a barrier. Include an unused
failed-open bank release racing another final resource drop, since the mutex
must serialize held_ mutations as well as native publication. Check writer
unlock before surviving read and native EBADF only after final release. The
reviewed cleanup wrappers do serialize move/reset/native emission, retain state
outside the mutex, and release the final raw handle after unlocking; no source
race was established by this static pass. The missing oracle is not a claimed
observed race or a request for a new broad concurrency campaign.

**Retarget negative cut.** maintenance_catalog_retarget checks zero3007 on the
new refused resource after the3048 ordinal, but after restore returns capacity
the driver only sets scope.error and exits. It attempts no subsequent acquisition
through the real retained submission. Zero later reads is therefore vacuous as
an oracle of stop-before-acquisition. Its positive replacement/read test and
literal linked3049 Error are substantive. RetainedState's actual maintenance
caller currently sets retarget_error and returns rejected before continuation;
no production fall-through was established here. Move the negative oracle to
that real caller, make it otherwise reach the catalog read, and require the
actual refusal/no native call. Do not infer a production bug from the earlier
fixture's legitimate pre-refusal checkpoint reads.

**Other unexecuted variants.** The accepted native transcript includes failed
close/closedir/fdopendir and restriction/allocation guards. Current interposers
fault read, pwrite, flock, fstat and worker launch, not those close/stream outcomes;
the existing allocation trap only forbids allocations between acquired prefix
and publication/next call. Treat such guard variants as limits, not established
results. The direct P2/P4 fixes already require relevant close/stream lifetime
cuts; finish only the affected planned variants rather than rerunning unrelated
owners. Positive journal B/N quotas are not exercised by the physical-only
groups (their loan budgets use B=N=0); semantic caller integration must supply
that actual positive-read oracle before whole-main opt-in.

## Twenty-one groups read and what their direct assertions establish

These are static assessments of their source assertions. I did not execute them
or convert the earlier reported green run into stronger claims.

|Group|Actual mechanism/assertion inspected|
|---|---|
|1 diagnostic_prefix|real selected4096 read then EIO; all allocations refused between acquisition and next read; exact one3008 prefix in closed run|
|2 opt_out_and_native_originals|default no selection; actual nonzero-offset input/output; exact unlock/close creator links on cleanup lane|
|3 authority_and_cold_cleanup|fork exclusion; unrelated lane refusal; one real worker authority scope; converse shared-file drops and one cold thread|
|4 held_physical_stop|stopped actual Rec/Sup; full ordinary CONTROL/DATA + sticky stop; held extent/write/sync and paid close still publish|
|5 finite_native_failures|selected short/EINTR pwrite then pre-syscall finite refusal; actual kernel prefix and native error bytes|
|6 single_unlock_failure|one failed unlock, no spent automatic retry, actual lease contention until final close|
|7 write_reference_and_scope_quota|original16MiB body once; exact3032 prior-input Ref/offset/count; exec3 scopes and rejected fourth|
|8 actual_journal_consumer(true)|real short batch tail, poisoned journal, unchanged accepted cursor and one prepared original|
|9 actual_journal_consumer(false)|real32MiB payload through three bounded native inputs; exact cold payload after writer drop|
|10 exact_stat_fields|forwarded native stat fields memberwise; failed EBADF body contains no success-only fields|
|11 saved_limit_and_index_page|real invalid64MiB saved slot fully acquired before codec rejection; actual16384-byte page bytes against disk|
|12 enumeration_and_retention|numeric scratch/live lease/signature/symlink selection; short native names, expiry unlink and stream close counts|
|13 acquired_prefix_death|real native prefix pending P>A with stopped holders; kill/recovery retains exact published body once|
|14 wrong_thread_bank_death|live issuer rebound on wrong OS thread exits74 before bank END/destruction; prior input preserved|
|15 combined_transfer(false)|actual never-started rollback; entered child then OLDmain retirement; successful join restores fresh primary; Root END before native close|
|16 combined_transfer(true)|sticky joined admission refusal retains child debt; later explicit fresh transfer restores primary|
|17 cleanup_grant_exhaustion|actual1024 cleanup capacity admits six resources; seventh refuses before open; existing banks close once|
|18 maintenance_catalog_retarget|actual new catalog identity/count and paid positive read; literal oversized return; negative no-call driver gap above|
|19 terminal_product_owner(normal)|actual PTY/TUI worker/1030 link; primary idle only after join; paid settlement/Root END|
|20 terminal_product_owner(cancellation)|actual Ctrl-C worker; refused joined owner leaves debt and skips idle|
|21 terminal_product_owner(launch_failure)|actual pthread_create EAGAIN; no perform entry, OLDmain bundle preserved and cleanup releases|

## Reviewed authority/admission/byte lifetime conclusions

Resource incarnation is the successful native request Ref; executing issuer is
the explicit authority, not creator lane or fd number. Ordinary call checks
actual process/thread/handle; joined loans additionally check real issuer/root/
scope state. Pending handoff excludes origin reads; entered worker restores
authority before Terminal's actual join permits primary idle. Fork inherits TLS
addresses but PID checks refuse access/cleanup. Wrong-thread live loan retirement
exits before claim destruction; legitimate original-parent retirement after
child entry has separate immutable custody proof.

Cleanup bank creation/reset/move and native close/unlock emission use the one
dedicated state's mutex and handle. State survives resource/Root references;
the final handle can drain only outside its mutex. Raw descriptor→DIR transfer
retains one cleanup bank; native failed-close outcome does not claim successful
kernel release. Restrictions/stat fail guards own their fd before later business
allocation. Native return fields borrow stable locals through synchronous emit;
failed stat has no success fields, positive read publishes before limit/mapping/
another syscall, write input publishes before effect and continuation references
the existing frame borrow rather than copying the same slice again.

Loans spend finite native slots, including short/EINTR, rather than manufacture
success. Per-batch begin_write limits exact retry topology; poisoned tail and
unchanged accepted cursor are directly checked. Native output bytes and original
errno remain distinct from the business Result and eventual durability. Raw
recorder/bootstrap/default APIs remain opt-out; open_child_directory is used by
raw_reader discovery and is the explicit naked-fd exception. No writer duration,
packet transformation or new native callback was found in these storage paths.
The diagnostic duplicate in P1 is the concrete exception to representation
ownership, not grounds to create a generic byte-dedup service.

Apply P1–P4 and the affected existing lifetime/retarget oracles, then use the
already authorized single recheck/fixes and supported-profile integration.
Keep main activation held until semantic caller propagation and actual
simultaneous per-issuer capacity pass. No additional panel, review of this
report, compiler run by this reviewer or unrelated assurance layer is requested.

## Finding-driven direct recheck, 2026-10-13

P1-P4 are fixed; each authentic matching-header/archive baseline fails on its actual native cut. Diagnostic input appears once, as3009; preheld failure unlink preserves the actual write error and records its independent native unlink outcome. A newly replenished unlock_failed bank releases after failed reacquisition, while spent automatic cleanup never retries. Directory bytes follow the acquired native record extent and bounded NUL/name fields, preserving malformed originals before business corrupt. Mac namlen and Linux longer-than255 entries have native variants.
The kernel-error cut externally closes the selected descriptor then forwards
the unchanged nonzero write to actual EBADF. The zero-return variants forward
a zero-length write as deliberate fault injection; they do not claim the kernel
returned zero for the original nonzero request.

The expanded Mac fixture has30 groups (29 on Linux): original21, affected diagnostic failures/stream errors/native directory variants, plus the actual retained cold/concurrent cleanup case. Debug whole native is green. Real FactHistory and full-replay ImmutableBytes survive Root/Context death; actual native writer lease releases before lawful primary cold reads, which retain literal original bytes without caching. Dedicated dev/inode matched close forwarding observes one successful close followed by EBADF for each retained incarnation. A MAIN-created old archive closes on the authorized Terminal-style worker under a native barrier while MAIN publishes; a worker-created archive survives join and replacement on MAIN. Two independent threads drop actual final closures concurrently with an unused bank from a native ENOENT open. Actual stopped Rec/Sup, full MAIN CONTROL and EOF cancellation do not prevent paid cleanup. All observed native close pairs/resource ENDs share the serialized cleanup issuer; cleanup owner END follows resource ENDs. Actual TerminalUI normal/cancellation/failed-launch groups independently exercise the real product handoff.

The retarget negative now runs through actual RetainedState::append in a supported reopened compact session. Its real maintenance checkpoint grows the catalog beyond the loan bound; actual capacity refusal precedes candidate apply/native continuation. The cursor remains unchanged, exact3048/3049 preserve the new count/incarnation and error, and no later3007 reaches the refused new resource. The earlier no-later-call driver is replaced, not certified by a second oracle.

Root narrow static checks pass for MAIN, raw_storage, diagnostics, journal_storage/writer/checkpoint, local_timing and the fixture. Current actual launcher startup/native owner/factory/end, reopen and inspection oracle is green. Affected supported profiles and mandatory source gate finish this segment; no second code/oracle review is required.

Supported Linux qualification corrected two native-observer identity mistakes.
Linux /proc observation reused an externally lost fd number, so diagnostic close
matching now uses the live dev/inode or actual lost-fd cleanup issuer. The
preserved failed cold case shows archive0 openR433/fd14/dev64512/inode3437367
closed at cleanup ordinal43; a later saved-state temporary opens as different
R739/fd15 with that retired inode. The observer now stops following an incarnation
after successful close/EBADF. Per-openR raw close pair/resource END counts remain
separate direct assertions. No production race or lost close was established.


Root completed supported-profile integration and the installed source hook in
source ca6747b:110/110 Debug cases in146.29s, all161 owned C++ translation units
and owned Lua checks. The existing owner/finding oracles pass affected Mac/Linux
Release and ASan-UBSan profiles. The required hook's seven obsolete integration
fixture assumptions are corrected and their direct/profile checks pass; native
byte, failure, custody and publication assertions remain strict. Full disposition
and remaining scope are in [the completion plan](../docs/INSTRUMENTATION_COMPLETION.md).
nls.2 remains open; this completes the present owner segment.


2026-10-14 integration disposition: storage_raw alone fails the remaining-owner
mandatory gate (130/131 overall). Actual selected Terminal cancellation reaches
Turn cancelled and restored idle, but its fixture exit waits for raw_owner_blocked
and times out. Study of terminal_product_owner(cancellation), WorkerCustody.join
and the completed UI cancellation subplan shows this historical oracle assumed
sticky wake admission after join; the new actual paid rearm deliberately removes
that stale incarnation before restoring Root. Plan: require actual Ctrl-C flag,
one real perform, one idle native acquisition after actual join, lawful restored
Root and unchanged original/settlement/close linkage. Exit on that idle event for
normal and cancellation; keep failed-launch and combined_transfer(true)'s genuine
forced-admission-refusal/debt oracles unchanged. No production change, relaxed
watchdog, second code-review panel or new fixture campaign. Recheck the existing
storage_raw completion selection, actual fixture static/dry-format, then the
required full installed source hook.


Finite storage oracle recheck passes the existing completion selection with
real normal/Ctrl-C/failed-launch Terminal handoffs, retained cold cleanup,
forced joined-admission refusal and restoration. Actual cancellation remains
required; Main performs exactly one idle native acquisition after restored
joined ownership. All original settlement, close, issuer and raw linkage checks
remain. Matching strict target build, storage fixture clang-tidy, formatter and
diff checks pass (context/storage-terminal-rearm-oracle-{build,native,tidy}.log).
No production source changes. The mandatory full source hook follows.
