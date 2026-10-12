# Physical product-storage acquisition and retention

2026-10-11. Study/subplan only: no production source or compiler run. Ignored
baseline fixture source is prepared but unexecuted. Root reserves3000–3099 and
owns the ONE whole plan attack before
source release. This unit closes the native physical part of census53/systemB
and the mechanical part of systemC, including nls.10 diagnostic partial reads.
The existing query owner describes its logical operation; it cannot describe a
positive native prefix which its callee destroys before returning an error.

## Source and reference study

Read journal_storage.hpp/cpp and diagnostics.cpp completely; journal_writer
create/open/append/read_exact/write_exact/scan, journal_checkpoint publication,
saved_state load/publication, recovery_index page read/write, archive_catalog
publication, environment_head selection and retained_environment forwarding.
Read RetainedState's replay/cold application readers and main's actual bootstrap
and restoration. Consulted diagnostics/scratch/journal/saved-state/retained and
recovery fixtures for independent business outcomes. There is no native_storage
source file: the real native boundary is journal_storage plus diagnostics.

Sources already acquired, retained intact:

* Linux man-pages6.19 archive in quarantine/raw-recorder-2026-10-10, read2,
  pread2, readdir3, close2, write2 and fsync2. Read unextracted pages with tar -xOf;
  did not rewrite or unpack the archive. Its manifest records source/hash/restore.
  Positive read returns are actual bytes, zero is EOF, errno is meaningful on
  failure. Short I/O is legal. readdir's d_off is opaque and d_name/record bounds
  are platform-specific. close is not retried after error on our supported
  platforms; successful close does not prove synchronization.
* Acquired Darwin read2 (Berkeley ancestry), Apple fsync/fcntl material, current
  native SDK stat/dirent declarations. pread does not alter the descriptor offset;
  Mac full strength selects F_FULLFSYNC while the other method is fsync.
* quarantine/native-storage-2026-10-11/linux-v6.12-locks.c: sys_flock's actual
  native command and shared-open-description ownership; XNU kern_descrip.c:
  sys_flock's LOCK_UN branch invokes VNOP_ADVLOCK without F_WAIT, and descriptor
  close/drop removes the owned description. Both source pins/hashes are in
  papers/2026-10-11-native-storage-acquisitions.json. Neither guarantees that every
  filesystem can never fail unlock; do not infer released ownership from a call.
* SQLite os_unix.c robust_close, seekAndRead, unixRead, unixWrite, full_fsync and
  test_syscall.c. Learn actual syscall forwarding/fault cuts and one-shot close.
  Its accumulator can discard earlier positive bytes on a later failed read;
  its full-fsync fallback is different from our existing product method. Adopt
  neither as a metric transform or new fallback. No SQLite dependency.
* Acquired Reno kern_ktrace.c ktrsyscall/ktrsysret/ktrgenio separates entry, return
  and I/O copying, but ktrgenio omits error outcomes and synchronously writes a
  vnode. Those are explicit counterexamples to Blackbird's required transport.

No literature campaign, dependency, clock/cadence, global interceptor, registry,
path dictionary, packet encoder, metric aggregation or producer/recorder parser.

## Whole invariant and scope

For an opted-in physical product owner, admit before acquiring syscall arguments,
calling native code or receiving its result. Preserve distinct request/return
records and every acquired positive byte prefix before another call, allocation,
validation, decode, callback, error mapping, close or release can destroy it.
Ordinary admission can fail before acquisition. Once admitted, its return and
mandatory resource cleanup are paid; they do not wait for more CONTROL.

|Physical owner|Actual calls and product consumer|
|---|---|
|Session directory|open/openat, fstat, geteuid, directory sync and close; product RetainedState/RetainedEnvironment receive the owned directory|
|Journal file|openat, fstat, pread/pwrite, fstat extent, flock acquire/release, fsync/F_FULLFSYNC and close; FramedJournal source/semantic frames and retained cold readers|
|Derived files|Same file calls inherited through directory: scan checkpoint, saved-state slots, recovery-index pages, archive catalog, environment authority/head and publication scratch|
|Publication namespace|fstatat source/destination, existing uid observations, renameat, directory sync; exact operands before business protection/selection|
|Scratch enumeration|openat("."), fdopendir, readdir, fstatat/openat/flock/fstat/pread8/unlinkat, closedir and candidate close; existing bounded128/16 business pass unchanged|
|Diagnostic directory|mkdirat, openat, fstat/uid, close; lazy owned DiagnosticStore|
|Diagnostic file|openat, fstat, native write/read loop, file close and failed-write unlink; existing65536-byte/30-day diagnostic policy|
|Diagnostic expiry|Persistent DIR stream, native readdir/stat/unlink/closedir; capture name before expiration parsing/private-file filtering|

All native calls in these selected source files are accounted for, including
validation/stat failures and ignored native close/unlink outcomes. Memory/custom
JournalDirectory adapters are not kernel calls and do not emit invented syscalls.
Their business semantics remain covered by their caller owners.

Retained indexing is a **separate next conceptual unit**: key construction,
index-page selection/publication, root acceptance, batch cursor/state changes,
replay reconciliation, archive/resident retirement and selected accelerators.
The mechanical native calls below those paths are included here; claiming their
semantic success from fsync/rename/native reads would be false. Producer-ring
publication and recorder recovery/bootstrap/reader storage are also separate.
Tools/auth/external files do not silently opt in through this product API.

## Explicit opt-in and resource ownership

Add raw_storage.hpp/cpp and immutable descriptions. A setup-only
RawProductStorageOwner contains the actual RawThreadHandle, optional actual
caller parentR and its admitted setupR. Explicitly pass it to new optional native
open/open_at overloads; absence means no storage event and no lane lookup or
claim. Existing APIs/default callers remain opt-out, including RawRuntime,
RawAudit, recorder descendants, external raw-reader discovery and bootstrap.
Do not select capture from ambient raw_enabled alone.

Main creates the raw runtime/binding first, then opens the product directory with
this owner immediately before actual RetainedState create/open. Its existing
unobserved startup header read needed to establish EnvironmentId stays explicitly
bootstrap; do not fabricate an observed read for it. Inspect/maintenance modes
which intentionally have no raw runtime remain opt-out. Selected live session
replacement must create its own explicit product owner after the actual new raw
binding, rather than inheriting an expired old-run producer. Coordinate main with
its current owner; no ContextStore implementation change.

Resources take their incarnation from the actual successful open requestR, not
fd number, path lookup, inode guesses or synthesized counters. The native result
and fstat fields retain the actual descriptor and physical observations. Failed
open has its actual attempted requestR but owns no descriptor; invalid paths
have an API refusal and no invented syscall. Files/diagnostic stores/DIR streams
inherit explicit product ownership only through this opted-in directory. File
ownership outlives FramedJournal when existing cold readers retain its shared
JournalFile; cleanup and producer handles travel with that actual object. Native cleanup
uses the actual creating actionR as its lifecycle parent; it does not claim that
a separate public API action occurred when a destructor invoked the native call.

Each owned native descriptor has a move-only cleanup bank and retained setup
handle, acquired before the open effect. Moves transfer these alongside the fd,
never consume or duplicate them. Replacement closes the old resource with its
own bank before adopting the new one. Claim members die before their handle.
Every failure after open (restriction check, allocation, fdopendir) uses that
already held cleanup. fdopendir success transfers the same descriptor-close
responsibility to the DIR owner; failure leaves it with the fd guard. closedir
success/failure consumes the DIR close once, never separately double-closes it.

The current product owners execute sequentially on one owner thread. Require the
same actual creating handle for ordinary operations; mismatched/concurrent owner
use refuses before acquisition, without silently binding another lane or adding
a mutex/wait. After a joined sequential owner ends its binding, destructor-only
cleanup may borrow its retained handle exclusively as the existing authored
owner does. The explicit preadmitted joined-command scope below is the selected
exception: it preserves file creation identity and uses the actual job issuer
after join. It never transfers creating-owner claims. Cross-thread cold-reader acquisition needs its own reviewed owner
handoff and is not claimed by this API; the current launcher does not do it.

open_child_directory returns a naked descriptor used by raw discovery. Keep that
existing seam opt-out rather than pretending the directory can observe a later
caller-owned close. A future product caller must use an owned directory return
with cleanup responsibility; no metric publication can discharge unknown caller
ownership. This exception is explicit, not an implicit partially observed fd.

## Native layouts and exact credit rules

No padded C++ struct dump or JSON. E16 is existing raw envelope; R24 is actual
lane16/ordinalu64; native64-bit size_t/ssize_t/off_t/pointer8, int4, bool/u8=1.
Setup descriptions specify dev_t/ino_t/mode_t/nlink_t and native directory field
widths, platform and version. One field is copied as its native bytes. Failure
errno appears only when the native return specifies failure; success/EOF do not
invent errno0 as an observed value.

Common call request Q = E16 + API-actionR24 + resourceR24 + methodu8 =65 before
method arguments. Return prefix T = E16 + actual syscall-requestR24 =40. An
optional errno is present:bool followed by int iff present. Native int return
max49; ssize_t return max53. Resource-less bootstrap is not encoded with a fake
resourceR: its admitted setup/open API action is the real resource proposal.

|Sites|Actual native fields after Q / after T|Credit incl E|
|---|---|---|
|3000/3001 owner setup/return|optional callerR, owner kindu8 / setupR, outcomeu8, optional Error(code1/detail8)|setup18 or42; return50 max|
|3002/3003 API action/terminal|ownerR, actionu8, resource-present:bool,[resourceR], original API scalar arguments; names/body in3004 / actionR, branchu8, optional Error|base42 or66 + actual scalar widths; terminal50 max|
|3004 actual API operand|actionR, operandu8, size_t length, exact original bytes|49+length DATA; before validation/cstring creation|
|3005/3006 open/openat|parentfd:int, flags:int, mode-present:bool,[mode_t], CString size_t+original bytes / actual returnedfd:int, errno optional|request82+mode_width+name length; return49 CONTROL|
|3007/3008 pread/read|fd:int, count:size_t, positioned:bool,[offset:off_t] / actual ssize_t, errno optional; positive appends exact count bytes|request78 or86 CONTROL; return53 CONTROL, positive49+count DATA|
|3009/3010 pwrite/write|same native args + exact input bytes / actual ssize_t, errno optional|request78/86+count DATA; return53 CONTROL|
|3011/3012 fstat/fstatat|fd:int,[flags:int,CString] / actual int, errno optional, success-only native stat fields below|fstat request69; fstatat81+name; return max85 on selected Linux,73 Mac|
|3013/3014 fsync/fcntl-fullsync|fd:int, actual methodu8,[native F_FULLFSYNC command:int], actual requested SyncStrengthu8 / int, errno optional|71 fsync,75 fullsync; return49|
|3015/3016 flock|fd:int, exact native operation:int / int, errno optional|73 +49 =122 per actual attempt|
|3017/3018 close/closedir|resource kindu8, native fd:int OR DIR*:8 / int, errno optional|70/74 +49 =119/123|
|3019/3020 renameat|fromfd:int,tofd:int, two CString lengths:size_t and exact bytes / int, errno optional|89+both names DATA; return49|
|3021/3022 unlinkat/mkdirat|fd:int, native flags:int OR mode_t, CString / int, errno optional|77+arg_width+name DATA; return49|
|3023/3024 fdopendir|actual fd:int / actual DIR* native8, errno optional|69+53; no fabricated stream on failure|
|3025/3026 readdir|actual DIR*:8 / nonnull:bool, errno-present:bool,[errno:int], success-only native fields and exact name incl NUL|request73; failure46/EOF42; success69+name Linux,71+name Mac DATA|
|3027/3028 geteuid|no argument / actual native uid_t|65+44 selected profiles|
|3029 resource END|resourceR, closure-outcomeu8|41; after actual closure return; failed close remains failed, not an inferred absence|
|3030/3031 reserved|No duplicated retained/index semantic publication|—|
|3032 prepaid pwrite of an already published input slice|same positioned native arguments as3009, actual prior inputR and payload offset:size_t / return remains3010|118 CONTROL; no second input copy; actual native count specifies the referenced range|
|3033/3034 physical loan admission|setup ownerR, optional actual callerR, kindu8, actual protected bytes:u64, batch/top-level/find bounds:size_t each, validated catalog maximum:u64 / requestR, branchu8,[Error on failure]|request106 max, return50 max;156 held before acquiring these arguments|
|3035/3036 physical loan activation|loanR, actual selected journal resourceR, archive-present:bool,[actual selected archive resourceR] / requestR, branchu8,[Error on failure]|request89 max, return50 max; paid by that loan before sticky cancellation|
|3037 loan-scope END /3038 loan-owner END|actual scopeR OR loanR, outcomeu8,[original Error on failure]|50 max each; held from admission, no fresh shutdown claim|

3006 open success returns the actual openfd; only subsequent success validation
makes an API-created resource usable. In syscall Q, resourceR for an open is its
already admitted parent resource; the API actionR identifies the future open.
Top-level open uses actual owner setupR as its proposal, never a zeroR.

Stat fields are exactly st_dev,st_ino,st_mode,st_uid,st_nlink,st_size, which the
owned code already consumes. Native sums are28 Mac and40 Linux, so success T +
int4 +errno-present1 +fields =73/85. Do not read struct fields after failed stat,
serialize padding or insert a derived private/regular flag. Existing predicates
run after the raw return. Every existing geteuid call is its own native pair;
do not issue an extra geteuid solely to populate metrics.

readdir success fields: Linux native ino8/off8/reclen2/type1 and name-length8;
Mac ino8/seekoff8/reclen2/namlen2/type1 and name-length8. Names include their actual
terminating NUL. Mac uses the native namlen; Linux uses bounded name acquisition
within the actual returned record, before matching/expiration parsing. Neither
interprets d_off/seekoff as a file position. Reserve the native maximum bounded
name extent before readdir; malformed native entry is preserved within that
admitted extent and returns a product error without searching beyond it. No
uninitialized record padding is read. DIR pointer values are native observations,
not global handles or dereferenced producer-side storage references.

API-action3002 scalar suffixes are explicit, not an opaque normalized operation:

|Actual public action|Scalars after the42/66 base; originals in3004|Fixed3002 maximum|
|---|---|---|
|open/open_at|parent-present:bool,[actual parentfd:int]; original path/component|43/47 unresourced|
|create_exclusive/open_existing|original component; actual FileAccess:u8 only for open_existing|66/67|
|read_at/write_at|original API offset:u64, original requested length:size_t|82|
|extent/lock_writer/release_writer|No additional scalar argument|66|
|synchronize/synchronize_directory|actual SyncStrength:u8|67|
|replace_file|Original from/to components; no invented target identity|66|
|store_diagnostic|actual now:u64, input native length:size_t; original name in3004, input in native3009|82|
|read_diagnostic/expire_diagnostics|actual now:u64; supplied name only for read|74|
|reclaim_publication_scratch|actual max_scan/max_remove:size_t; original journal component|82|

3004 copies API operands before validation; native open/stat/rename/unlink
requests subsequently copy their actual prepared C-string bytes including NUL,
with their actual native argument lengths. This second physical argument record
is not a new business encoding or a claimed semantic source. Both stages are
necessary to distinguish invalid original bytes from a native call which never
occurred. Diagnostic store's input needs no extra3004 body copy: its action
length links to actual3009 native input chunks; the already-owned caller body
is not erased if admission refuses before native write.

An API action holds its terminal50 plus exact fixed/native request-return pair
for each call before acquiring that call. Variable path/operand DATA is granted
before reading its bytes. An operation with multiple calls does not fund an
unbounded retry loop up front: each next ordinary call admits before acquisition;
failure propagates and existing callers retain their pending/error state. No
transformation/callback/allocation separates native call and protected return.

Positive reads hold DATA49+native_count and CONTROL53 before the syscall; zero
or negative returns use CONTROL, positive returns use DATA, with the unused bank
released. Writes hold DATA78/86+native_count and CONTROL53, publish the exact
already prepared input request before the native write, then its native return.
Do not call it committed/durable. Read/write zero calls still record their actual
native return. Raw ring grants larger than capacity refuse before acquisition.

To support current saved roots up to64MiB and journal payloads32MiB without
requiring a single larger-than-lane grant, an opted-in NativeJournalFile issues
at most16MiB in one native read_at/write_at call. It returns the actual legal
short result to existing read_exact/write_exact/read_all/write_all loops. Record
the original API requested size in3002 and actual native count in3007/3009;
never claim a truncated native read was a full API read. This is an explicit
owned I/O bound, not a metric byte transform or sampling policy. Unobserved
storage retains its existing native call size. Test the32MiB+ and64MiB consumers;
approval of this bound is part of this plan, not an implementation assumption.

Cleanup bank per descriptor =119 actual close pair +41 END =160. DIR cleanup
bank =123 closedir pair +41 END =164. Retain these from before acquisition;
opening a stream moves ownership rather than holding both banks indefinitely.
Reserve164 for the stream's intermediate raw descriptor before its openat;
raw-fd failure spends160 of that same bank, fdopendir success changes its kind
and later closedir spends164. No claim merging or new cleanup grant after open.
Before the LOCK_EX effect, prehold ONE actual unlock pair122 plus its actual
release API-action66 and terminal50 =238; close160 remains independent. There
is no unlock retry loop, including EINTR. A failed native return stays failed;
it cannot establish released ownership. Leave the descriptor and surviving cold
readers intact until their existing final ownership permits actual close.

Track real lease state alongside that bank: unheld, held, unlock_failed. Successful
native LOCK_EX establishes held. Failed LOCK_EX from unheld leaves unheld; failed
LOCK_EX from a previous held/unlock_failed owner preserves that prior state.
Successful LOCK_UN establishes unheld; failed LOCK_UN establishes unlock_failed
and consumes its ONE held pair. Store the exact last native failure for that
unresolved state; do not replace it with a synthesized released outcome.

release_writer is noexcept and may be destructor-driven. With held/unlock_failed
state and a funded bank it issues one actual LOCK_UN and publishes that exact
return. With unheld state it has no owned lease to release. With an already spent
bank it issues no second native call, no raw wait, no invented success or newly
funded per-call publication. The previous failed return remains the observed
unresolved lease outcome; later final actual close records its own distinct
native result. This is a native acquisition boundary, not a claim that every
unadmitted public void call is audited (broader nls.10 API entry coverage is
separate). Repeated automatic cleanup cannot regain credit by waiting.

An ordinary explicit lock_writer call is the existing safe reacquisition seam:
while sole ownership/handle/lifecycle still admits work and cancellation permits,
its admission obtains the real LOCK_EX request/return and a new238 release bank
before invoking native code. If an unspent release bank already protects the
held lease, keep that bank rather than adding a duplicate; otherwise replenish
before acquisition. On failed LOCK_EX, discard the new release bank only when
state was unheld. A previously held/unlock_failed lease remains owned and may
retain that admitted new bank for one later actual release attempt. On success,
state becomes held. Success is the actual observed LOCK_EX result, not evidence
that an earlier failed unlock succeeded or that a distinct inode was acquired.
No resourceR changes: the same actual open description remains owned. If ordinary
admission fails, neither reacquisition nor another release attempt occurs.

After lifecycle ownership closes ordinary admission, there is no replenishment.
Final close still uses its independent160 bank. Do not call lock_writer from a
destructor, add a periodic retry worker/timer, close early, or invalidate a real
cold reader. This resolves root's S1 within its ONE plan attack; the arbitrary
eight-attempt proposal is removed rather than qualified as a new retry policy.

Owner setup/action/publication maxima are summed from these native fields using
sizeof constants; descriptive table widths are not magic numeric source tokens.
Before open: terminal50 +open-return49 +fstat request69/return85 +actualuid
pair109 +cleanup160 =522 CONTROL at the Linux maximum,510 Mac, in addition
to its exact API-action and native open request/name grants. A successful open
transfers cleanup160; failed validation spends it, records failure and terminates.
Before lock: ordinary pair122 +held unlock238; resource already owns close160.
Before stream openat reserve cleanup164 instead of160, then fdopendir's
ordinary122 before invocation; failure uses160, success transfers the same164
bank to DIR ownership. Persistent stream does not retain an enumeration-history-sized bank.

## Cancellation, failures and recursion

Close, one prepaid writer unlock, resource END and the explicit settlement loan
below consume their own held credits under sticky cancellation/full ordinary
CONTROL. Ordinary native stat,
read, write, sync, rename, unlink and enumeration honor cancellation before
acquisition. No lock is acquired around a raw wait; callers must not enter the
API under a product mutex. The existing long-lived filesystem writer lease is
not a product mutex or recorder dependency: raw recorder uses separate storage.

The existing turn and Context contract already requires postcancel native writes:
finish_workflow(false), stop.linkage and possible issuer refills run from held
10544/9198 semantic/region banks. Those banks do not fund physical storage capture.
Fresh native grants would break an existing successful full-CONTROL stop. S2
therefore requires a distinct physical bank obtained before the protected effect,
not a generic cancellation bypass or a transfer from a semantic bank. Without
that bank, a postcancel call refuses before native acquisition, propagates the
actual admission Error and preserves pending semantic debt. Context no_new
continues to reject unpaid source/save and optional checkpoint acquisition.

Snapshot errno immediately at native return, before raw publication/close can
change it, and restore it where existing native business logic consumes errno.
Preserve native return before mapping ErrorCode. Keep primary read/write/stat
error separate from later close/unlink failure; close failure cannot replace an
earlier prefix or errno. Successful diagnostic close remains optional business
behavior, but its actual failed return emits without a false read failure/success
claim. Fix EOF's current stale-errno error detail to honest incomplete/io0 rather
than copying unrelated errno; retain the true zero native return separately.

Raw transport publishes acquired bytes or emits its existing ingest-failure
outcome/fence. No retry of a published event; no claimed captured bytes after a
failed publication. Recorder/source storage remains opt-out even with inherited
raw binding. Generic global interposition is only the native test seam, never a
production hook. No physical syscall observer callback or ContextStore on worker.

## S2: preadmitted physical settlement

This section resolves the actual integration defect found in root's ONE plan
attack. It is an admission/lifetime rule for a finite product obligation, not a
new retry worker. Raw storage capture must not turn a normal, already protected
stop append into a fresh canceled wait.

### Existing acquisition sequence

FramedJournal::append encodes the batch once, checks file extent, calls write_exact
and synchronizes once before publishing its prepared cursor/metadata. write_exact
currently continues after positive short writes and interrupted errors; its loop
has no finite successful-write guarantee. RetainedState::append_impl calls
journal_->usage twice before that append, and each usage calls the real native
extent even though the caller only uses indexed_records. Thus one ordinary
successful retained batch needs three actual extent calls, its full-write native
calls and one synchronization. Do not replace those observed calls with cached
metric values. Capture the existing encoded frame bytes, never encode frames a
second time for metrics.

submit_impl checks existing identity before append; apply checks it again. A
checkpointed live root may use ArchiveCatalog::find even for newly issued IDs
which correctly miss. Catalog entries are96 bytes. A count bounded by the
existing validated UINT32_MAX permits at most32 binary probes plus one final
entry, hence33 normal full96-byte reads per find. Each entry helper can already
continue after seven EINTRs and up to96 positive one-byte returns;103 native
attempts can be required by that existing retry behavior. The physical loan
funds the normal full-read sequence; extra attempts spend remaining paid read
slots and can honestly fail before acquisition when those slots are exhausted.
It does not promise success under every legal short-return sequence.

The stop/rejection application events and issuer reservation events have no
dependencies or invocation admission input. Actual fresh IDs miss, so their
negative catalog finds do not read a semantic payload. A corrupt catalog match
or a different event which selects a payload is not permission for an unpaid
historical read. Preserve the acquired catalog bytes and propagate refusal before
that additional syscall. General selected payload/index reads belong to the
separate retained/index owner, coordinated below.

### Explicit bank and selected native owners

Add a move-only RawStorageSettlementLoan with separately held CONTROL and DATA,
retained actual issuing producer handle, actual parentR, and separate remaining
banks for extent, positioned write, synchronize and bounded catalog entry reads.
Each loan emits only on its actual issuer; no credit moves across lanes. Its
explicit selected-root target retains the file-creating owner separately. The
main turn uses its creating producer; a joined job uses the preadmitted issuer
described below. Its argument includes the actual existing protected byte
capacity F and the finite native operation topology, with checked arithmetic.
No imaginary new business packet, metric transform or guessed average length
enters that calculation. If a raw partition cannot hold the complete protected
bank, admission fails before the effect. Resource-close/unlock credits remain
independent and survive a refused loan.

A narrow loan scope binds this bank to the actual selected FramedJournal file
and, where present, the current ArchiveCatalog file. RetainedState supplies those
owned objects through an explicit physical scope seam. Selection follows the
actual live journal and checkpoint root, not path lookup, descriptor equality or
ambient raw_enabled. The scope checks the selected root/creating owner, actual
loan issuer and exclusive sequential borrower before any acquisition. It unbinds before the loan/handle
dies. Nested scopes cannot silently replace a live different bank. No thread-local
global registry, implicit MaintenanceScope privilege or ordinary cancellation
exception. Custom/memory JournalFiles forward their existing business calls and
emit no invented native observations.

Before reading the loan's caller-supplied argument fields, obtain its156 metadata
bank:3033 request106 and3034 return50. Publish the actual request, then validate
the bounds/owner and acquire the purpose-specific banks. A refused DATA/CONTROL
grant records its actual Error with the already held3034; release any partially
acquired banks. It cannot leave a successful loan or a native call behind. These
arguments are the actual protected-capacity/topology inputs, not emitted derived
J or predicted syscall counts. Return success only once all banks and the retained
handle are owned. Native-ABI descriptions preserve size_t/u64 widths.

The present stop obligation has one activation scope. Before succeeding3034,
also hold189 for3035/3036/scopeEND and50 for loan-owner END.3035 names the exact
selected native resourceRs on activation; failed activation still uses3036's held
failure return and does no syscall. Successful activation ends with its own held
scopeEND, then the loan-owner END runs before releasing the remaining banks and
handle. Scope outcomes use the actual propagated Error/owner state; no successful
append is inferred from releasing credits. A future finite caller with several
separate activation sections must declare/pay that actual number before effect,
not acquire new activation credit after cancellation. There is no per-call scope
replenishment hidden in native code.

RawTurnTrace owns the stop loan beside its existing semantic/region banks. Before
begin_workflow/effects, turn obtains the actual cancellation_budget and existing
protected-settlement capacity, prepares the matching physical loan, and only then
publishes the accepted protection. protect_workflow prepares a replacement loan
before applying a larger protected effect, using the actual revised capacity.
Retain the old loan until both replacement admission and business protection
succeed; failure leaves the old protected rollback usable. Release the old loan
after installing the replacement. This may temporarily hold both real banks;
there is no fabricated capacity or late cancellation renewal. The existing raw
parent and the actual input/identity generation remain unchanged.

Rollback activates that explicit scope before finish_workflow(false) and
stop.linkage, including actual issuer refill appends. It borrows the physical
loan and its original handle until those calls finish. Context's semantic
no_new still prohibits source/save: only the selected native extent/write/sync
and bounded catalog calls consume this loan. An optional checkpoint cannot
borrow it for create/rename/full-scan/diagnostic work. A live ordinary operation
outside this scope admits normally and observes cancellation normally.

### Normal write allowance and already published input

Let L be the approved16MiB maximum native transfer. A prepared encoded batch of
B bytes needs N=ceil(B/L) full native writes. Before its first native write, split
exactly N write-call slots from the loan; each slot holds303 CONTROL bytes. A
slot is consumed once, including its unused remainder, so a failed/short call
cannot recycle itself into an unbounded retry. This is paid native acquisition
space, not a producer-side emitted counter. If splitting fails, no write occurs.

The first input slice emits3009 with its exact native input bytes (86+count DATA),
then calls pwrite and emits3010. Its CONTROL is API82 + terminal50 + return53
=185; the slot's extra118 is released. A stack-owned positioned-write state
retains that actual inputR, actual payload offset86, input extent and the existing
prepared batch borrow through write_exact. It does not allocate/cache/copy those
bytes again. For a later permitted retry of the same published slice,3032 writes
the actual native arguments and prior inputR/payload offset;3010 records the
actual native return. Its CONTROL is API82 + terminal50 + request118 + return53
=303. The count/offset reference only bytes already published. When that slice
is complete, the next distinct16MiB range emits its own actual3009 original.

Expose only the narrow JournalFile positioned-write continuation needed by
FramedJournal::write_exact; a default implementation forwards existing write_at
for custom files. Native code caps a retry to the remaining previously published
range, preserving legal short I/O and real business offset progression. Ordinary
unobserved callers retain their existing native behavior. This is input linkage,
not a frame decoder, chunk coalescer or conversion of source bytes.

A positive short or EINTR can consume another already prepaid slot, if one
exists. After N slots, unfinished output returns capacity/refusal before another
native syscall. Existing AppendAttempt then poisons the journal, and the retained
prepared proposal remains pending as its existing failure path requires. It does
not publish an accepted cursor, pretend synchronization or discard a written
prefix. A successful full native sequence uses N slots and its held sync, even
under sticky cancellation and a full ordinary CONTROL partition. Native EIO,
zero or sync failure remains the actual failure independently of available bank.

Catalog reads use one preheld271 CONTROL slot (API82 + terminal50 + request86 +
negative return53) and DATA145 (positive return49 + entry96) per actual native
call. A positive short preserves its exact prefix immediately and the next call
uses another paid slot. It never reacquires a canceled ordinary credit or claims
that a short entry is a complete catalog record.

### Exact stop bank

For the present turn/Context stop topology: at most two application records
(workflow rejection and stop.linkage), each possibly preceded by one actual
issuer refill. Four top-level submissions can use the existing four-region
rejection envelope each: K=16 retained append regions, O=4 top-level submissions.
Do not fund an arbitrary callback body or arbitrary repeated caller entries.
The mandatory bank is bounded by this exact existing owner scope.

Each batch's encoded bytes are charged to the actual protected F. Across at most
K nonempty batches the normal write-call bound is J=ceil(F/L)+K-1. Per-call
native grants still use the actual prepared B/count. Surplus held slots do not
allow a batch to retry beyond its own N split. Ordinary valid stop batches use
three extent calls each. An invalid top-level proposal may take two pre-rejection
usage calls plus one retain_rejection usage call before its diagnostic inner
batches; reserve those3O additional extent slots too. No branch silently runs a
native extent after consuming that bank.

|Held physical purpose|CONTROL|DATA|
|---|---|---|
|Extent, including pre-rejection usage|270(3K+3O) Linux;258(3K+3O) Mac|0|
|At most K synchronizations|237K Linux;241K Mac|0|
|Normal positioned writes, per-batch finite quota|303J|F+86J|
|Eight negative catalog finds, P probes each|271(8P)|145(8P)|

Here P=0 with no archive; otherwise use bit_width(A)+1, capped at33, where A is
the existing validated catalog maximum (min(UINT32_MAX, checked journal record
capacity*6)). Use the maximum that the current product capacity permits because
an ordinary checkpoint can change the selected catalog before rollback. The
scope selects that actual current owned catalog on activation. No full catalog
scan or extra native read is done merely to size the loan. For K16/O4 the fixed
non-write CONTROL is19992 Linux or19336 Mac; add303J and2168P. DATA is F+86J+1160P.
Loan metadata/lifecycle adds395 CONTROL (156 admission +189 activation +50 owner
END), independently of the purpose-specific call banks. A refused initial loan
needs only its already admitted request/return; it does not invent successful
activation or an acquired native resource. Thus the full successful present stop
reservation is20387+303J+2168P Linux or19731+303J+2168P Mac CONTROL, plus the
DATA bound above.
Checked overflow, loan-issuer mismatch or insufficient ring space refuses loan
admission before effects. Width formulas are implemented with native sizeof,
not the table's literals as unexplained source constants.

Successful appends retire the product credit and publish the real cursor as
before. Loan exhaustion before a native call is an honest failure, not a promise
of semantic cleanup. After a partial native write the journal is poisoned and
the proposal remains unresolved; after a prewrite extent/read refusal no write
is invented. Close does not retroactively accept an append or prove durability.

### Joined command issuer and native file creator

The actual CommandJobs::start prepares Job.raw_context and its claims under a
pre-thread binding. CommandJobs::settlement obtains the job under mutex, releases
that mutex, joins the actual worker and binds the retained job context for
semantic2035/2036 and held1728 append regions. Its Coding closure then calls the
main root's submit_settlement. The worker never calls ContextStore/RetainedState.
The native journal/catalog was created by the main context. Requiring its creator
to equal this legitimate joined emitter would break existing mandatory append.

RawCapacityClaim retains its actual RawProducer pointer and partition; moving
it does not change issuer. The file keeps its unchanged openR, setupR, creating
handle and independent cleanup/unlock banks. The job loan keeps its own issuing
handle and claims. Requests emitted on the job lane link the actual main-created
resourceR; these are real cross-lane references, not synthesized file identities.
Do not bind the main producer inside settlement to conceal this difference or
move a main bank to the job.

Coding prepares an explicit target from the actual root instance/creating owner
and the semantic owner's finite terminal dependency budget before exec launch.
Pass that admission to CommandJobs::start. Absent admission preserves standalone
opt-out. start verifies the actual bound parent equals the target's creating
handle, then derives Job.raw_context using its existing raw_prepare_thread. That
call derives a lane from the actual bound Runtime::State. Do not substitute an
arbitrary independently prepared context. In the existing job pre-thread binding,
issue its physical loan beside its semantic bank before worker construction or
child effects. The target retains the creating handle/setupR; its actual root
must outlive the jobs, matching the current engine/session ownership order.

The job-kind loan starts pending_join. Merely binding that producer cannot
activate the native scope while the worker lives. CommandJobs marks it joined
only after actual worker.join returns; a join error leaves the loan unusable.
The worker never consumes or destroys this owner-settlement bank. The direct
parent-kind loan remains usable under its actual sole parent owner. These are
explicit selected owner lifecycle states, not inferred TLS permission or a
worker-side joinable observation.

After join/binding, the existing business append closure receives the exact job
loan explicitly. Coding/RetainedState constructs its selected physical scope
around submit_settlement. Validate exact root, issuer, creating owner and current
journal/catalog native objects before any acquisition; refuse a conflicting
active borrower. Native objects retain a lexical borrowed scope pointer, restored
before loan/handle release. No implicit current-producer takeover, mutex, wait,
registry, metric subscription or second business serialization. The parent turn
can retain its own separate loan, but cannot substitute it for this job issuer.

Before launch, a preparation refusal emits its actual held3034 on the job issuer
and releases any acquired bank under that binding. A successful never-started
loan abandoned after construction failure emits held END under an explicit
binding to that same never-started issuer before claims/handle release. Honor
the actual thread construction outcome; do not borrow a concurrently running
worker producer or infer absence from a worker's joinable check.

CommandJobs::shutdown joins/stops while preserving pending terminal debt for a
later drain. Keep that job loan rather than prematurely releasing its bank or
claiming semantic success. A successful settlement closes its scope then loan
inside the actual joined binding. Failed/unattempted terminal debt retains honest
pending ownership; eventual CommandJobs destruction joins first, then explicitly
closes its loan with pending/error END on its issuer before claims/handle die.
An ordinary explicit retry can replace its spent loan on that same joined issuer
at a safe pre-effect boundary. Sticky cancellation cannot renew it. Repeated
shutdown cannot manufacture a new physical terminal bank; partial native write
still poisons the root and leaves the actual proposal unresolved.

No layout/bank change follows from this seam:3033 ownerR names the creating
setupR, optional callerR is the actual launch/semantic parent and the envelope
names the real loan issuer.3035 already names the actual current journal/catalog
resourceRs. Ordinary file operations still require creator equality; the
explicit loan scope requires selected-root authority and exact issuer equality.
Final file/cold-reader cleanup keeps its original creating handle and bank.

### Existing other mandatory owners and activation dependency

Whole-main opt-in also reaches the current held operation open/receipt/terminal
paths, command submit_settlement, Lua error diagnostics and capacity.stop.
Coding's operation owns its existing5184 region bank before authoritative
admission; command terminal owns1858 and protected8192/16 business capacity per
debt. Those are already present mandatory paths. Their semantic checks can select
positive archived admission/observation/invocation payloads. Actual first command
terminal can use six archive finds and three positive packet reads (admission,
recursive invocation and open); its latest observation is absent because Coding
currently emits only terminal observations. A generic retained caller with a
previous observation can read a fourth packet and has a separate envelope.
Receipt can use three finds
and one positive open read. The retained owner is deriving exact stored packet
sizes/provenance and diagnostic topology for the prelaunch allowance, rather
than inventing a blanket worst-case payload grant. A turn stop loan
covering negative fresh-ID finds cannot claim to fund those reads.

These selected catalog payload calls read the actual32-byte frame header plus
its recorded payload. Do not add a112-byte root read which belongs to the
distinct lazy archive_reader/payload_reader closures; if those closures actually
run, that owner must admit their real root/header/frame calls separately.

The retained/index owner is assigned the semantic submit/apply/dependency/archive
selection envelope. Coordinate one explicit physical loan contract with that
owner: exact selected file/range, finite topology and actual existing protected
capacity issued before the operation/child/effect, borrowed through mandatory
settlement, no cross-lane transfer or canceled fresh waits. That owner supplies
the necessary positive payload-read allowance and caller propagation; this
physical unit supplies its native methods and preacquisition bank enforcement.
Do not introduce a duplicate retained lookup service or second packet encoder.

This is a real main-activation dependency. The physical source/helper and the
actual turn/Context native stop fixture may qualify first, but main's entire
product directory cannot turn capture on until all these existing mandatory
callers have that propagation or their explicitly corrected failure contract.
Root must resolve that dependency before claiming census53 integration complete.
The earlier statement that postcancel writes were a hypothetical future need
is withdrawn. No production storage source has been changed during this attack.

## Actual diagnostic partial-prefix fix

Current read_diagnostic allocates result(st_size), reads into its suffix and on
the next n<=0 closes and returns Error, destroying the result. Audit query's
lookup sees only that Error. New paid native read helper publishes each positive
prefix immediately, then permits another read. A later EIO, EOF, EINTR/cancel,
close failure or business validation/decode failure cannot erase that prefix.
The product Result remains a failure when its requested complete file was not
obtained. Do not return a partial response as complete, add a second custody
service, promote expiring diagnostic disk into permanent retention, or claim
that this also repairs every earlier provider diagnostic-only audit policy.

Capture native fstat failure before its current EPERM protection mapping and
capture ignored close/unlink errors. The original error remains identifiable
even if product code preserves its historical protection Error; no synthetic
success fields on that failed stat. Later diagnostic expiry still operates on
diagnostic files, not the raw preserved run.

## Independent genuine red and candidate oracles

Use ONE native storage fixture with literal layouts and actual OS forwarding.
Mac native RTLD_NEXT/interposition and Linux linker --wrap/real calls as existing
qualified fixtures; no duplicate symbol escape, production callback or fake
native successes. Native fixture state stores exact acquisition returns/bytes
outside the production emitter; only selected actual product fds are faulted.

1. **Existing-API baseline diagnostic loss:** create real private65536-byte
   diagnostic file with binary/NUL/non-UTF8 literal pattern. Native first read
   forwards to the real OS for4096bytes and stores its actual returned bytes in
   test-owned shared memory; second selected read returns -1/EIO; selected close
   performs real close then returns -1/EINTR in a separate variant. Existing
   read_diagnostic returns actual failure, native prefix was acquired, raw run
   contains no preserved matching positive physical read. Preserve genuine red
   source/log/run before adding API or implementation. Do not reconstruct expected
   bytes by invoking the candidate decoder or merely require an event count.
2. **Missing explicit owner baseline:** a direct source using the proposed owner
   overload fails compilation against the genuine prechange header. Preserve it;
   keep the existing-API behavioral red above independent of missing symbols.
3. **Prefix-before-next-read cut:** native second-read entry blocks on a caller
   test barrier before returning failure; parent stops actual Rec/Sup beforehand,
   inspects actual producer P>A and independently compares raw pending native
   read's exact requestR/count/bytes against the forwarded native capture while
   owner is alive. Kill exactly the owner/holders; reopen via real orphan recovery
   and require one preserved original, actual request link and no complete-result
   claim. Test-owned process cleanup always runs on failure; no stopped orphans.
4. **Native transcript:** real create/open restrictions (symlink/FIFO/private
   mode), stat and uid, nonzero-offset pread/pwrite, actual short and zero returns,
   EIO/EBADF/EINTR, Mac fullsync/Linux fsync, flock contention/unlock, native close.
   Compare exact original input/positive output and every selected actual native
   request/return with forwarding capture, ordered by lane ordinal in tertiary
   fixture, not physical DATA/CONTROL append order. Success-only stat fields are
   compared individually; failure bodies cannot include uninitialized metadata.
5. **Whole physical consumers:** real journal append/source/reopen; allowed saved
   roots and32MiB journal payloads through bounded native transfers; a separate
   real64MiB native file and saved-slot envelope exercise the loader's physical
   maximum, with actual codec validity kept distinct (each context/program
   encoder already has16MiB ValueLimits, so do not invent a valid64MiB packet); actual16384-byte index
   pages; checkpoint/catalog/head publication rename/sync failures preserve old
   selected business root and actual reached native returns. Use existing codecs
   once, compare actual files/native bytes independently. This does not infer
   semantic-index capture from mechanical records.
6. **Enumeration/retention:** real scratch numeric names + live locked candidate,
   real journal signature, symlink/nonprivate objects and128/16 limits; diagnostic
   expired/retained/nonmatching names. Compare actual acquired names before
   validation, ignored ENOENT/error returns, exact unlink operands/results,
   persistent stream ownership and one final closedir. No synthetic name counters.
7. **Strong preclaim cuts:** actual full DATA/CONTROL + sticky cancellation before
   native read/write/stat/next retry → unchanged test syscall invocation count,
   original refusal/pending debt. Under the same pressure, preheld close/unlock
   pairs and END still emit actual native outcomes without waiting. Inject one real forwarded/native-failure
   unlock outcome (including EINTR); require exactly one actual call, the original
   errno and unresolved state, then repeat automatic void release and require zero
   additional native calls. A separately admitted real lock_writer reattempt can
   replenish ONE later unlock, while failed/sticky ordinary admission cannot.
   Verify final close waits for the surviving cold reader's actual lifetime. Acquisition-to-publication allocation trap
   rejects every allocation, not just a selected buffer size.
8. **Lifetime/recursion:** resource move/replacement, failed open validation and
   allocation, fdopendir failure/success, surviving real cold reader after writer
   release, later joined sequential cleanup under new binding. RawRuntime setup,
   recorder append/recovery and read-only raw-reader discovery with binding
   present emit zero3000–3099 calls unless an explicit product owner was passed;
   simultaneous product append still emits its exact transcript. Verify native
   fd ownership by real operation/EBADF and actual closedir calls, not path labels.
9. **Actual protected stop:** extend the existing real Context full-CONTROL/sticky
   stop fixture. Before effects, prepare the physical loan from its actual
   cancellation_budget/protected capacity and opt in its real journal. Preserve
   a real checkpoint/catalog and exhaust its issuer reservoir. Stop actual
   Rec/Sup and fill ordinary CONTROL as the existing cut does. Actual
   finish(false), stop.linkage and required issuer refills must still succeed
   with normal native full calls. Compare forwarding-captured extent/write/sync/
   entry-read originals, literal raw fields/parents and actual accepted stop and
   issuer events/file bytes. A refused initial loan prevents the effect; a refused
   replacement leaves the original protected rollback usable. Loan activation
   and both ENDs use their own held native fields after sticky cancellation;
   no fresh CONTROL/DATA admission occurs in that mandatory section.
10. **Finite native allowance:** forward a real first pwrite positive short from
    a batch with one paid full-write slot. Require no second selected syscall,
    preserved exact input/actual return, unchanged accepted cursor, poisoned
    journal and actual pending proposal. A larger batch permits its real
    ceil(B/16MiB) calls; compare3032's inputR/payload range with the original3009
    and actual forwarding pointer/offset/count bytes, without duplicate source
    DATA for that range. Normal larger full writes complete. Actual EINTR or
    positive-short catalog read consumes paid slots; exhaustion refuses before
    the next syscall and preserves the first prefix before catalog/product
    failure. Test a real fresh-ID archive miss; a corrupt matching identity
    cannot acquire unpaid semantic payloads. A loan cannot fund optional save,
    checkpoint create/rename, diagnostics, index rebuild or recorder storage.
    Reopen the real product and raw run independently after partial failure.
11. **Actual joined issuer:** create the journal on the main producer, launch a
    real CommandJobs child with its own prelaunch physical loan, then under
    actual job CONTROL pressure/sticky cancellation join and submit its terminal
    through the existing settlement closure. Literal native records must emit
    on the job lane, link the unchanged main-created resourceR and agree with
    actual accepted product bytes/cursor. Activating on the wrong producer or
    before join refuses before a selected native call; neither main nor job
    claims silently transfer. Test actual launch failure/abandonment and
    shutdown-with-terminal-debt, preserving the correct issuer's held END and
    later drain bank. Retain a real cold reader beyond the joined scope and
    require one final close on the original creating lifecycle/bank. This cut
    remains blocked on the semantic owner's exact positive-read allowance;
    standalone/default commands do not imply whole-main activation.

The ignored existing-API baseline source is
context/storage-diagnostic-prefix-red.cpp, prepared but not compiled/run. It
selects the actual65536-byte diagnostic inode, forwards its first4096-byte read,
copies those actual bytes into independent test storage and fails the next read
with EIO. It requires the real product failure and one exact preserved positive
raw prefix. This establishes genuine absent capture on the prechange source; the
candidate uses the explicit owner overload. Old opt-out APIs are not expected to
start capturing merely because a raw binding is present. Preserve that genuine
source/run/log before the candidate migration.

Targeted native Mac debug and changed-file tidy after implementation; root ONE
whole code/oracle attack, finding fix/recheck, then supported Linux debug/release/
ASan and Mac profiles under root integration. No duplicate assurance panel or
full suite during this study. Root registers helper/descriptions/CMake; coordinate
main with Metadata and leave Context/retained semantic-index windows untouched.
