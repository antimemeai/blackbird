# Physical product-storage acquisition and retention

2026-10-11. Complete reviewed subplan; no production storage implementation.
Root reserves3000–3099. Its ONE whole plan attack accepted S1/S2, the same TUI
authority/cleanup finding and finite positive-read/scope quotas. The existing-API
prefix baseline qualifies published source ab23278 before implementation.
Whole-main activation remains held for semantic callers and composed capacities. This unit closes the native physical part of census53/systemB
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
RawProductStorageOwner contains the shared explicit root authority, retained
primary RawThreadHandle, optional actual caller parentR and admitted setupR.
Its dedicated CleanupState has a distinct retained producer and sole serialization. Explicitly pass it to new optional native
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
JournalFile; cleanup banks and their dedicated producer state travel with that actual object.
Native cleanup keeps the actual creating actionR as its lifecycle parent; it does not claim that
a separate public API action occurred when a destructor invoked the native call.

Each owned native descriptor has an opaque move-only cleanup bank and retained
dedicated CleanupState, acquired before the open effect. Its actual openR/setupR
remain immutable; the emitter and bank issuer for cleanup are explicitly separate. Moves transfer these alongside the fd,
never consume or duplicate them. Replacement closes the old resource with its
own bank before adopting the new one. Claim members die before their handle.
Every failure after open (restriction check, allocation, fdopendir) uses that
already held cleanup. fdopendir success transfers the same descriptor-close
responsibility to the DIR owner; failure leaves it with the fd guard. closedir
success/failure consumes the DIR close once, never separately double-closes it.

Ordinary operations require the explicit root-authorizing issuer and its actual
executing thread, not equality with the resource creator. The actual Main/TUI
perform/join/idle protocol serializes that authority. The joined-command loan
adds its explicit bounded post-join issuer, with unchanged file creationR and no
claim migration. Cleanup ALWAYS uses its one dedicated producer and private
nonwaiting serialization, including unused claim release and final cold-reader
destruction outside any root scope. The exact source/lifecycle/layout resolution
is integrated immediately below. Arbitrary cross-thread cold-reader acquisition
is not granted; final owned resource destruction is supported independently.

open_child_directory returns a naked descriptor used by raw discovery. Keep that
existing seam opt-out rather than pretending the directory can observe a later
caller-owned close. A future product caller must use an owned directory return
with cleanup responsibility; no metric publication can discharge unknown caller
ownership. This exception is explicit, not an implicit partially observed fd.

## SAME TUI authority and cleanup finding disposition

## What the current source actually owns

* main.cpp creates its instrumentation binding before the product Root. The
  main perform callback does Root/Context work outside CodingEngine::turn,
  including context commands and checkpoint/session operations. A1900-only
  permit therefore misses actual product operations.
* TerminalUI::run prepares a distinct raw context and actual RawWorkerDispatch
  in start, constructs a worker, binds that exact context, starts the existing
  command_perform1030 region, then calls perform. Completion joins the worker
  before clearing busy. Idle work runs only when !busy. Each later start joins
  its predecessor before preparing another worker.
* RetainedState::save_current_state replaces archive_ with a newly opened
  ArchiveCatalog. This can destroy a main-created catalog on the TUI worker
  while the main producer continues emitting UI events. The reverse also
  occurs: a worker-created catalog remains in Root/Context and is replaced or
  destroyed on main after join. Binding either original creator for final close
  is unsafe or wrong; the creating lane is not a cleanup serialization mechanism.
* RetainedState::history_snapshot captures shared archive_ plus a copied
  FramedJournal::archive_reader. The latter captures the shared JournalFile;
  neither closure requires RetainedState to survive. ContextStore::refresh_history
  replaces its FactHistory and can drop the last old archive or file reference.
  A public FactHistory can also outlive ContextStore and Root.
* FramedJournal::payload_reader separately captures shared JournalFile. Replay
  installs it inside ImmutableBytes::cold for ApplicationRecordEvent payloads.
  Copies of RetainedFact/ImmutableBytes share the reader. The last such copy can
  close its native file long after writer release and outside any perform scope.
* FramedJournal's destructor calls release_writer; it does not force final
  JournalFile destruction. ArchiveCatalog owns its file through unique_ptr.
  DiagnosticStore and NativeJournalDirectory each additionally own persistent
  DIR streams and descriptors. These all require the same explicit cleanup
  ownership, including failure guards and move replacement.

The RawCapacityClaim destructor and move assignment modify producer held_
directly. Serializing just emit/close while releasing a claim on another thread
does not serialize this producer. All claim mutations/releases must be included.

## Actual root authority, independent of resource creation

RawProductStorageOwner owns a shared native product authority created at its
existing setup. Its immutable setupR and each resource's actual successful openR
remain unchanged. It retains the primary main handle. The lexical authorizing
issuer is either that primary handle, the one explicitly admitted TUI child, or
an explicitly selected joined-job loan issuer for its bounded settlement.
Creation identity and current authorization are separate fields and concepts.

An ordinary opted-in native operation validates its actual authority object and
raw_current_thread against the current admitted issuer before admission or any
operand/native acquisition. It emits and claims on that current issuer. It does
not bind a creator, derive permission from raw_enabled, or treat an arbitrary
context from the same Runtime as authorized. Concurrent/reentrant handoff or
wrong-issuer use refuses before a syscall. The authority publishes the actual
admitted handle identity atomically; wrong-thread checks only compare that value
with their retained raw_current_thread().get(), never dereference a borrowed
scope pointer or race on a mutable shared_ptr. Also retain/check the actual
executing thread's identity: binding the right handle on an unrelated thread
does not confer permission. An opaque address of a per-thread identity marker
can serve this comparison without querying a new OS metric; it is not a permit,
is never emitted and is accepted only alongside the explicit native grant.
Disable the active permit before switching its issuer/thread identities. Actual
source serialization still comes from start/perform/join/idle, not from treating
these checks as a general concurrent Root API. Native file creation handles need
not remain alive just to preserve openR: no cleanup claim belongs to them after
this change.

TerminalUI's actual start passes its derived context and actual dispatch to the
explicit product owner's prepare_handoff, before publishing live worker state
or constructing the thread. Preparation validates the active authorizing parent
and stages a move-only grant. It temporarily binds only the not-yet-launched
derived child to preclaim its authority-entry/return/END bank. Actual parent
request/return, allocations and child credit refusal occur before worker effects.
Successful preparation marks the authority unavailable for ordinary parent
operations while that handoff is pending; it does not authorize a running child
just because its handle exists.

Use an explicit typed optional grant carrier at the selected TerminalUI perform
seam, and an optional product-owner argument at TerminalUI::run. Existing raw
worker dispatch/started records remain their actual records, singly emitted.
At the main perform entry, the grant opens a lexical authority scope using the
actual1030 BEGIN reference supplied by TerminalUI. That scope encloses the whole
perform body and its existing catch/backstop, including direct Root/Context work.
It ends before perform returns,1030 finishes, completion is posted and main joins.
No nested scope substitutes current generation or invents a dispatch reference.
Plain/one-shot/station paths retain their already authorized primary issuer;
absence of a product owner keeps the current terminal API behavior opt-out.

The grant exclusively owns its child claims. Entry and END consume them only
under the exact child binding. An actual never-started launch failure closes its
abandoned bank under an explicit not-yet-started child binding and restores the
parent permit. Successful worker scope exit publishes held END, closes its claims
on the worker and returns authority to the primary owner. Terminal's existing
join/busy protocol still excludes main Root work until that worker is joined.
There is no guessed join from binding disappearance, no worker-side joinable
test, no migration of a creator claim and no permission hidden in TLS.

Cold reader callbacks retain this authority object independently of Root. Their
ordinary reads still require the actual authorized issuer; a copied callback is
not a grant to read from any thread. Their destruction requires no ordinary
read/write permission and uses the independent cleanup mechanism below. Writer
lease state remains distinct from authorization and from read-only lifetime.

CommandJobs::start validates its parent against this active root-authorizing
issuer, which can be the actual TUI worker or main. It derives and prepays its
own job loan there before launch. The existing pending_join/actual join rule
and explicit root-selected settlement scope remain. Joined settlement does not
modify the ordinary root permit or borrow the cleanup producer for semantic
events. File openR/setupR remain the actual original facts; loan events and
native settlement events remain on their actual joined job issuer.

## ONE retained cleanup producer; no cleanup thread or queue

Create one shared CleanupState as part of opted-in product-owner setup, before
the first product fd/flock effect. It owns one separately prepared RawThreadHandle,
one private serialization mutex and one held cleanup-owner END. This is a lane,
mapping and endpoint, not an added OS thread/process. Resources retain this exact
state, so it survives Root, authority scopes, archive replacements and cold-reader
closures until the last resource and held cleanup bank are gone.

Every cleanup bank is issued on this producer before its corresponding native
effect. Caller ordinary admission runs first with its original cancellation
control. Then CleanupState briefly locks, explicitly binds its sole handle and
uses raw_try_claim_control to obtain the exact bank. That helper performs only
hold_capacity accounting; it has no RPC, polling, retry, allocation or sticky
cancellation check. If capacity/fence refuses, unwind the caller's admitted
attempt and refuse BEFORE open/LOCK_EX/fdopendir acquisition. Do not wait under
this mutex or try a second issuer. It cannot replenish cleanup after cancellation
merely because raw_try_claim_control bypasses cancellation: ordinary acquisition
admission is mandatory first. Mandatory settlement uses its prior loans and
cannot acquire new cleanup resources through this path.

This is a deliberately narrow exception to the generic hold_capacity-before-
owned-lock guidance: a dedicated producer's nonwaiting accounting must be
serialized, and raw_try_claim_control is the already owned nonwaiting primitive.
No ordinary claim_capacity/raw_wait_data/raw_prepare_thread/Runtime exchange or
producer drain may execute while this cleanup mutex is held. Ordinary native
operation admission and registration remain outside owned locks.

Opaque move-only cleanup-bank wrappers never expose RawCapacityClaim. Bank
creation, split, resource attachment, failed-acquisition release, unlock bank
replacement, emission, move-assignment release and final bank destruction all
execute through CleanupState's serialization. A resource move transfers the
opaque wrapper and its state owner; it does not reissue or merge its bank.
Different resource objects may be destroyed concurrently, so Root's sequential
perform protocol is not used as a substitute for this mutex.

Close/unlock/closedir enters that exact state, takes the mutex, explicitly binds
the cleanup handle, emits its already held native request, calls the actual
native function once, emits the actual return/END, and resets/releases the bank
before ending the binding and unlocking. No caller callback or arbitrary product
operation runs inside this section. Native close may have native OS latency;
there is no raw-capacity wait, timer, retry worker or metric subscriber. Failed
LOCK_UN retains the unresolved lease/error and spends its one bank as S1 already
requires. Failed close/closedir emits its actual failure and is not retried.

The wrapper destructor first moves its state owner to a stable local, releases
its claim under the state mutex/binding, then unlocks before dropping that local.
The final CleanupState destructor owns the sole remaining handle: it emits its
preheld END and destroys that claim inside an explicit cleanup binding, ends the
binding/unlocks, then releases the raw handle OUTSIDE the mutex. This ordering is
required because RawThreadContext::~RawThreadContext can drain/wait. No claim or
binding retaining this handle survives the final state release. Native fields
used by a resource destructor stay live through its held publication.

The final cleanup-owner END marks the actual end of this retained ownership,
not aggregate native cleanup success. Each failed close/unlock retains its own
original return/error event. Do not sum failures, maintain a success counter or
turn an earlier native error into a new acquisition result at this END.

This same wrapper must be used by every close guard in diagnostics.cpp and
journal_storage.cpp, both persistent DIR owners, failed-open restriction and
allocation branches, scratch candidate unlock/close, and replaced native
resources. A naked close credit hidden in a defaulted member destructor would
violate the contract even if the normal final close was correct. Opt-out cleanup
retains its existing business behavior without accessing this producer.

## Exact additional field layouts and credit placement

Existing E16/R24/native lane16/u8 conventions apply. Separate request and return
sites, native field copies, no serialized pointers and no emitted capacity-derived
metrics. New site slots within reserved3000–3099:

|Site|Actual fields after envelope|Maximum CONTROL|
|---|---|---|
|3040 handoff preparation request|product setupR, actual prepared child lane16, actual existing dispatchR, handoff kind:u8|81|
|3041 handoff preparation return|requestR, outcome:u8, failure Error(code:u8/detail:i64) when present|50|
|3042 authority entry request|product setupR, actual handoffR, actual command1030 BEGIN R|88|
|3043 authority entry return|requestR, outcome:u8, failure Error when present|50|
|3044 authority/abandoned-grant END|actual entryR or preparationR according to actual END kind, kind/outcome:u8, failure Error when present|50|
|3045 cleanup-producer setup request|product setupR|40|
|3046 cleanup-producer setup return|requestR, outcome:u8, actual lane16 on success OR native Error9 on failure|57|
|3047 final cleanup-owner END|actual successful cleanup setupR, outcome:u8, failure Error when present|50|

Preparation bank131 belongs to the current parent; child authority bank188 is
claimed on that exact child before launch. Failure/abandonment never emits a
fake entered scope. For3044 the one actual END kind identifies which reference
variant is present, rather than adding a synthetic origin ID. Failure Error
uses the existing outcome discriminator, with no success-only error bytes.

Cleanup setup requires97 CONTROL on the original setup parent, plus50 held on
the cleanup producer for its final END, in addition to the existing product
setup3000/3001 maximum92. Registration/mapping failure publishes the actual setup
failure before any product syscall. Root END waits for actual resources through
shared ownership; it is not emitted merely because perform or RetainedState ends.

Per-resource totals remain the already reviewed physical maxima, with their
issuers corrected explicitly:

* Before ordinary open, caller362 Linux/350 Mac pays terminal50, native-open
  return49, fstat request69/return85 Linux or73 Mac and actual uid pair109.
  The separate cleanup producer pays close160 before acquisition. Aggregate
  remains522/510, excluding the already specified exact API/open request grants.
* Stream intermediate fd pays cleanup164 on that lane; fdopendir's ordinary122
  stays on the current authorized caller. Failure spends fd-close160; success
  transfers the same164 responsibility to actual closedir, not a second bank.
* LOCK_EX request/return122 stays on the current ordinary issuer. Its one held
  release bank238 (actual release API66 + terminal50 + native pair122) belongs
  to cleanup. The same resource's independent close160 remains there too.
  A safely admitted lock_writer replenishment uses the same serialized,
  nonwaiting cleanup accounting before its effect; an unspent bank is retained.
* Resource END41 is already included in close160/DIR164. No extra cleanup-borrow
  request/return is inserted around each pair: its existing actual native request
  and return show the cleanup issuer and immutable resourceR directly.

Initial cleanup held floor50 plus160 per ordinary live fd,164 per live DIR and
238 per funded writer release is checked against the actual256KiB CONTROL
capacity. A grant refusal at that floor prevents the new resource; it does not
recycle live cleanup credit. Previously published cleanup bytes also occupy the
actual ring until acknowledged. Default RawRuntimeOptions registers64MiB DATA,
256KiB CONTROL and128KiB FAILURE for this lane:67,502,080 ring bytes plus the
native channel header/alignment. An explicitly larger main Runtime profile also
changes this child mapping because current child preparation uses that same
Runtime options; it cannot quietly keep a smaller cleanup capacity. The API currently cannot request a differently
sized child lane; do not quietly change the transport to avoid documenting that
cost. Cleanup emits only CONTROL; no extra payload copying, CPU worker or process
is created. This cost is one per opted-in product authority, not per fd or turn.


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
|3033/3034 physical loan admission|setup ownerR, optional actual callerR, kindu8, actual protected bytes:u64, batch/top-level/find bounds:size_t each, validated catalog maximum:u64, positive read bytes/call quotas:u64 each / requestR, branchu8,[Error on failure]|request122 max, return50 max;172 held before acquiring these arguments|
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
Before open: caller terminal50 +open-return49 +fstat request69/return85
Linux or73 Mac +actualuid pair109 =362 caller CONTROL Linux/350 Mac;
separate dedicated-cleanup160 preserves aggregate522/510, in addition to the
exact caller API-action and native open request/name grants. Cleanup bank
creation/release uses only the already specified serialized nonwaiting accounting. A successful open
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
API under a product mutex. Dedicated CleanupState uses only the explicitly
reviewed nonwaiting try-claim/emission/claim-release accounting while locked;
its registration and final handle drain remain outside that lock. The existing long-lived filesystem writer lease is
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
explicit selected-root target retains the root authority and immutable creating
setupR separately. The main turn uses its actual authorized main/TUI producer; a joined job uses the preadmitted issuer
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
ambient raw_enabled. The scope checks the selected root authority, actual
loan issuer and exclusive sequential borrower before any acquisition. It unbinds before the loan/handle
dies. Nested scopes cannot silently replace a live different bank. No thread-local
global registry, implicit MaintenanceScope privilege or ordinary cancellation
exception. Custom/memory JournalFiles forward their existing business calls and
emit no invented native observations.

Before reading the loan's caller-supplied argument fields, obtain its172 metadata
bank:3033 request122 and3034 return50. Publish the actual request, then validate
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
Loan metadata/lifecycle adds411 CONTROL (172 admission +189 activation +50 owner
END), independently of the purpose-specific call banks. A refused initial loan
needs only its already admitted request/return; it does not invent successful
activation or an acquired native resource. Thus the full successful present stop
reservation is20403+303J+2168P Linux or19747+303J+2168P Mac CONTROL, plus the
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
it does not change issuer. The file keeps its unchanged openR/setupR and independent cleanup/unlock banks
on the dedicated cleanup producer, regardless of its creator. The job loan keeps its own issuing
handle and claims. Requests emitted on the job lane link the actual main-created
resourceR; these are real cross-lane references, not synthesized file identities.
Do not bind the main producer inside settlement to conceal this difference or
move a main bank to the job.

Coding prepares an explicit target from the actual root instance/authority
and the semantic owner's finite terminal dependency budget before exec launch.
Pass that admission to CommandJobs::start. Absent admission preserves standalone
opt-out. start verifies the actual bound parent and executing thread are the target's
active root-authorizing issuer (main or TUI), then derives Job.raw_context using its existing raw_prepare_thread. That
call derives a lane from the actual bound Runtime::State. Do not substitute an
arbitrary independently prepared context. In the existing job pre-thread binding,
issue its physical loan beside its semantic bank before worker construction or
child effects. The target retains the shared root authority and immutable creating setupR; its actual root
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
around submit_settlement. Validate exact root, issuer, shared authority and current
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
resourceRs. Ordinary file operations require the actual current root authority issuer and
thread; the explicit loan scope requires selected-root authority and its exact
joined issuer. Final file/cold-reader cleanup uses its dedicated serialized
producer/bank and preserves the immutable original creating references.

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
Receipt succeeds with three finds and one positive open read; its logical
validation-failure envelope needs four finds. Initial Job terminal succeeds
with six finds and three positive frames, but its logical-failure path can need
a seventh absent-terminal lookup before rejection. Native EIO/corrupt exits
do not take that extra logical-rejection lookup. The retained owner is deriving exact stored packet
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

The semantic owner's current independently derived CONTROL costs are78536 for
Context stop,55741 for the initial joined Job terminal,33887 for receipt and188868
for full selected exec. These are study inputs, not a physical main capacity
selection. Context+exec alone totals267404, exceeding the default262144 CONTROL
bytes before any physical/existing turn/provider/command grants. Main activation
must derive each actual producer's maximum simultaneously live bank set and
select an explicitly reviewed capacity or refuse BEFORE effects. Adding costs
for different issuer lanes as though they share a ring would also be wrong.

Shared positive-read extension agreed with the semantic owner:
add original admission arguments B:u64 (positive native-read bytes) and N:u64
(positive native-read call quota). They select the actual journal through the
existing explicit scope; catalog-probe credit remains distinct. Each selected
positive/native-zero/error read spends one paid271 CONTROL call envelope, with
DATA budget B+49N. Request count is checked against remaining acquired-byte and
call credit BEFORE native acquisition. Legal short/EINTR may consume the finite
call quota; no call is fabricated or repeated after exhaustion. The actual
positive returned bytes, rather than original requested count, consume B; this
allows later admitted slices without reissuing unavailable bytes. The raw claim
still holds the full role budget before effects, so each acquired prefix can
publish immediately without another grant.

3033's metadata request grows106→122 for those16 actual API argument bytes;
3034 stays50, one-scope lifecycle395→411; the selected exec kind prepays789 for its actual
three possible scopes. The stop formula therefore becomes
CONTROL20403+303J+2168P Linux /19747+303J+2168P Mac, with N=B=0; selected
positive reads add271N CONTROL and B+49N DATA to that owner's applicable topology.
Actual caller packet/frame metadata supplies the quotas before effects; this is
capacity arithmetic over owned business facts, not a second encoder or an
emitted derived metric. Distinct lazy readers include their real112-byte header
only when invoked. Main positive activation remains held until acknowledged metadata, exact caller
propagation and each issuer's simultaneous capacity are resolved together.


### General physical formula and actual selected command quotas

For top-level submit limit O, retained batch limit K, catalog-find limit G,
validated maximum probe count P, actual protected write bytes F, original
positive-read admission quotas B/N and the selected kind's scope quota S, the
complete physical bank is:

* Linux CONTROL =222+189S+1047K+810O+303J+271(GP+N).
* Mac CONTROL =222+189S+1015K+774O+303J+271(GP+N).
* DATA =F+86J+145GP+B+49N.

Here J=ceil(F/16MiB)+K-1. The fixed terms are actual extent envelopes
270(3K+3O)/258(3K+3O), sync237K/241K and lifecycle222+189S. Each catalog probe
has its96-byte positive body plus49-byte prefix; its native call envelope271
is the same CONTROL maximum as an admitted positioned read. If archive is
absent, P=0; otherwise P=min(bit_width(A)+1,33) from actual validated capacity.
These are physical API admission calculations, never emitted derived metrics.

The semantic owner's selected first Job terminal K4/O1/G7 gives fixed
5409 Linux/5245 Mac plus303J+1897P+271N. Its actual acknowledged invocation
payload size I gives B=I+176 and N=2+ceil((32+I)/16MiB): real admission frame88,
open frame56 and invocation frame32+I. The initial successful terminal has no
previous observation. Its seventh-find allowance covers actual logical rejection.

Selected full exec K12/O3/G24/S3 gives fixed15783 Linux/15291 Mac plus303J+
6504P+271N. Its actual write bound F=1324 comes from Open24, Receipt36 and
terminal<=112 plus their three existing rejection envelopes384 each. Its
positive dependency frame union B=504+3I+D and N=5+3ceil((32+I)/16MiB)+
ceil((32+D)/16MiB) uses actual owned/acknowledged invocation and decision sizes,
not guessed average packets. These count selected normal native full transfers;
legal short/EINTR still consumes finite paid slots and can fail honestly.

The existing3033 kind selects the source-derived maximum S: stop1, joined
terminal1, selected exec3. Admission/owner END together cost172+50=222. Each
actual scope costs3035 request89+3036 return50+3037 END50=189. A selected exec
can activate for dispatch/Open, deactivate before adapter body, reactivate for
Receipt, then separately activate for a reached no-child terminal. Prepay789
lifecycle for that kind; stop/job keep411. Internal remaining scope credit is
checked before activation. An admitted scope uses its held return on validation
failure; exhausted scope quota refuses before new arguments/native acquisition,
with no fabricated fourth request/return or new cancellation grant. Unused scopes
are never emitted. Claims never move to another issuer or gain credit on reuse.
No new count field/event or duplicate semantic-loan telemetry is introduced.

Context stop includes semantic history_snapshot/archive_reader creation without
any physical payload read; it therefore keeps B=N=0. Those semantic costs do not
silently fund native root-header reads. Whole-main remains held until real caller
metadata and per-producer simultaneous admission capacities qualify these loans.

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
    require one final close on the dedicated cleanup lane, preserving the original
    creating lifecycleR and exact resource bank. This cut
    remains blocked on the semantic owner's exact positive-read allowance;
    standalone/default commands do not imply whole-main activation.

## Direct source-grounded oracle additions to the same whole fixture

1. The real old-API diagnostic-prefix red reproduced on published source
   ab23278 before storage implementation. No permission rewrite obscures its actual lost
   first4096 bytes before second-read EIO.
2. Prepare a real Terminal-style child from the primary binding and retain its
   actual dispatch/1030 region. On that worker execute opted-in checkpoint and
   direct Context work outside1900. Native calls must emit on this authorized
   child while preserving each actual resource openR. After actual join, main
   idle ordinary calls succeed again. An unrelated prepared lane and pending
   or overlapping handoff refuse before forwarded native invocation counts
   change. Failed launch releases the exact never-started child banks.
3. Open a real archive on main, preserve a history snapshot and create a new
   archive on the authorized TUI worker. Drop the old final catalog reference
   on that worker while main publishes real UI-lane records under a native
   fixture barrier. Literal final close/END belongs to cleanup, exactly once,
   retains the old main openR, and never uses UI producer claims. Preserve the
   new worker-created archive, join the worker, replace/drop it on main and
   compare the converse close/END against its actual worker openR.
4. Retain actual FactHistory and replayed ImmutableBytes cold payloads beyond
   Root/Context/FramedJournal destruction. Verify writer unlock first, a lawful
   surviving primary-issuer cold read before release, and no premature close.
   Last-drop real closures on two independent test threads simultaneously;
   forwarded actual closes and raw cleanup ordinals show exactly one final
   close per fd, sequentially on the same cleanup lane. The wrapper must release
   unused claims there too, not just close. Default cleanup-owner END is after
   every actual resource END and before lane retirement. Actual fd operations
   distinguish surviving lifetime from final EBADF; descriptor numbers alone
   are not used as incarnation identities.
5. Stop actual Rec/Sup and exhaust caller CONTROL with sticky cancellation only
   AFTER resource cleanup has been prepaid. Exercise both cross-issuer drops,
   unused failed-open bank release and final cold-reader drop; held cleanup
   native pairs/END still publish without fresh grants or drain while locked.
   A new ordinary resource/lock_writer cannot obtain cleanup capacity after
   refused admission. Separately exhaust the actual cleanup lane’s capacity
   before a new open: no new selected syscall, no fallback to another issuer,
   existing resource bank remains sufficient for its one actual close.
6. Launch an actual Job from the authorized TUI worker, not just main. Its
   prelaunch physical loan derives from that active parent. Actual post-join
   settlement native events use the joined Job issuer and unchanged resourceR;
   semantic/physical banks never use cleanup. This positive-read cut remains
   dependent on the semantic envelope, as already documented in S2.

The ignored existing-API baseline source is
context/storage-diagnostic-prefix-red.cpp, compiled and executed against
published source ab23278/debug foundation; authentic baseline exit1. It
selects the actual65536-byte diagnostic inode, forwards its first4096-byte read,
copies those actual bytes into independent test storage and fails the next read
with EIO. It requires the real product failure and one exact preserved positive
raw prefix. This establishes genuine absent capture on the prechange source; the
candidate uses the explicit owner overload. Old opt-out APIs are not expected to
start capturing merely because a raw binding is present. The actual first4096
bytes matched the real65536-byte input, the second native read returned EIO,
the product propagated that exact failure, and discovery/read verified a closed
raw run before the final missing-prefix assertion failed. Native fixture retained
at /tmp/blackbird-storage-prefix-red-wphbEq; build/log/binary under context/
storage-diagnostic-prefix-red*. Preserve these before candidate migration.

Targeted native Mac debug and changed-file tidy after implementation; root ONE
whole code/oracle attack, finding fix/recheck, then supported Linux debug/release/
ASan and Mac profiles under root integration. No duplicate assurance panel or
full suite during this study. Root registers helper/descriptions/CMake; coordinate
main with Metadata and leave Context/retained semantic-index windows untouched.
