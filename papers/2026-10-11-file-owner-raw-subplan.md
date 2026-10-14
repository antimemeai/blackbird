# File47–48 whole owner — proposed design for ONE plan attack

2026-10-11. Root owns native LocalTools read/write/edit and lexical descriptor/
temporary-file lifetime. Sites3400–3499 reserved. This document replaces earlier
partial candidates as the proposed design; its explicit claims below must pass
ONE whole plan attack before implementation. Production follows the completed single plan attack after its findings are resolved below.
Operation17 records actual encoded operation packets and separate effect/result/
receipt failures. This unit records file business steps and actual native bytes;
it does not infer native success from an operation admission or returned Value.

## Source/reference grounding

Read tools.cpp helpers/LocalTools/file presentation/range and tools_test.cpp,
provider_original.hpp explicit RawAdapterContext, raw runtime claim/held lifetime.
Read pinned LLVM23.1.2 operations.cpp canonical/absolute/current_path, GCC13.3
fs_ops.cc equivalents including ENAMETOOLONG fallback, glibc2.39 canonicalize.c
whole function and Apple Libc FreeBSD realpath.c whole function; installed Darwin
getcwd/realpath manuals and SDK stat declaration, whole man-pages6.19 getcwd and
realpath members. Linuxv6.8 namei getname_flags/getname_kernel grounds full-native
pathname byte limits. Sources intact under quarantine/file-native-2026-10-11;
papers/2026-10-11-file-native-acquisitions.json holds exact URLs/member/source/hash.
Old Reno ktrace separates request/return/raw I/O but loses failed genio and writes
synchronously; adopt neither fault. Already-studied skalibs caller buffers/growth
are observation patterns only, no dependency or copied implementation.

Native study artifact file-path-native-study.cpp compiled independently against
actual qualified MacClang23.1.2 systemlibc++ and LinuxClang18/libstdc++13. Mac14
cases/Linux17 cases agree: absolute-input realpath(NULL), realpath(supplied
initialized PATH_MAX) and actual filesystem::canonical select same native path
or errno. Empty absolute differs: Mac selects cwd; Linux EINVAL. Mac native
canonical accepts file/ and file/. while Linux ENOTDIR. Loop/broken/symlink exact.
Actual getcwd succeeds at4865Mac/4857Linux, while canonical deep cwd and Linux
/proc/self/cwd return ENAMETOOLONG. No adapter replaces native platform semantics
with lexical normalization. These actual examples complement source, not a
universal success claim for unbounded filesystem states.

## Invariant and actual byte ownership

Explicit RawAdapterContext with actual operation parent selects file capture in
LocalTools::run. No raw_enabled-only opt-in for other helper callers, no nearest
TLS owner/context dictionary. Ordinary helper overloads remain opt-out. Selected
native helper receives the explicit actual file tool parent. Synchronous execution
retains actual issuing handle until all held claims are consumed/reset; no transfer
across worker lanes. File helpers return before the operation adapter callback ends.

Acquired bytes are emitted before limit checking, string/path allocation, raw
presentation, semantic observer, subsequent syscall or exception can destroy them.
Every actual native request has a distinct returned result/errno, and each reached
file owner has one actual success/error/exception terminal. Prefix capture does
not claim a successful complete read or a successful later semantic append.

Actual operation input BBM2 and native proposed file bytes are DISTINCT already
existing representations. Capture the borrowed write content argument, and
prepay the proposed-body claim before the existing copy or unique-position
replacement. Emit that completed native write body ONCE immediately afterward,
before any following exists/read/observer or temporary-file effect. Pass its typed RawFileInput {actualbodyR,length} explicitly to helper;
standalone selected helper publishes its own body when no prior owned input was
provided. Then each write references its real input R and existing remaining offset/
count. Do not re-encode operation input, build metric offset maps or copy native
body per short write. Input contiguous slices > transport record bound link their
actual body BEGIN/END, each slice published before first effect. No new projection,
filter, duration, path dictionary, registry, observer, aggregation or schema codec.

Existing before capture references the actual helper read ownerR and length;
its graph already owns every reached positive slice, so no second before body.
A narrow optional actual read-ownerR outparam accompanies the existing string
return, allocated/assigned before return and never synthesized on throw. File
proposed selection emits the typed input reference, never a second body copy.
No pointer-index dictionary or metric-only compare establishes these links: the
actual same lexical proposed string passes directly into helper.

Existing LocalTools capture copies remain existing business/audit paths; this
unit does not add another file content string solely for metrics. read_file grows
its already-required business string only AFTER publication of each native prefix.
The temporary name vector remains actual mkstemp mutable input. Raw native views
borrow its allocation; its existing postcall mutation is captured BEFORE path work.

## Actual file business and native surface classification

|Surface|Actual acquisition/failure and publication boundary|
|---|---|
|Tool entry3400 / return3401|method enum, actual parent R; holds terminal before file_range/string_field/captured_.clear; successful result has actual native shape, failures retain actual Error/exception branch|
|Reached tool arguments3402|existing acquired path/old/replacement/content as borrowed native fields; no visitor over Value for metrics; unacquired missing fields omitted|
|Existing range/selection3403|existing parsed start/end/mode or actual parse Error at business branch; raw presentation result handled by operation result, no duplicate formatting|
|Conflict/before/proposed3404|existing before/proposed string and chosen replacement position/count captured at required business branch; helper originals referenced where actual parent owns them; no guessed links|
|Semantic observer3405/3406|actual existing observer invocation/return/throw, paid before invoking arbitrary callback; outer result distinct from native effect|
|Helper API3410/3411|actual read/write method, existing limit/count and explicit parent; terminal distinguishes native complete result from later owner failure|
|Native open3420/3421|exact supplied C path operand, native flags/mode before call; descriptor/errno immediately after; resource incarnation is successful request R|
|Native read3422/3423|fd/resource R/requested8192, actual ssize_t/errno; positive bytes emitted before limit/append; EOF distinct from failed read|
|Native exists/stat3424/3425|actual path operand/native result/errno, successful stat members copied individually; absent ENOENT/ENOTDIR business exists false is not fabricated native success|
|Native getcwd3426/3427|actual initialized caller capacity; native returned pointer-presence/errno; success CString+NUL captured before filesystem::path construction; failed output preserved with known capacity/unknown used length|
|Native realpath3428/3429|actual ABSOLUTE input and initialized PATH_MAX output capacity; native pointer-presence/errno; successful CString+NUL or full initialized failure extent immediately before path copy|
|Native mkstemp3430/3431|actual mutable initialized template buffer extent, descriptor/errno and actual modified output immediately; no metric path allocation after return|
|Native fchmod3432/3433|fd/resource R/native mode, returned int/errno|
|Native write3434/3435|fd/resource R/native input R/existing remaining offset/actual requested remaining count; ssize_t/errno, no repeated body copy|
|Native fsync3436/3437|actual descriptor, returned int/errno; no fullsync fallback added|
|Native close3438/3439|exact owned resource R/fd, actual result/errno, once whether explicit or failure cleanup; no retry on EINTR|
|Native rename3440/3441|actual mkstemp output/source and resolved destination, returned int/errno; file effect and later result preparation distinct|
|Native unlink3442/3443|actual attempted temporary pathname and returned int/errno, even when original business catch ignores it|
|Native fallback lstat3445/3446|actual path/native int/errno and success native st_mode/st_size before business symlink decision|
|Native fallback readlink3447/3448|actual path/caller initialized capacity, native ssize_t/errno and exact positive bytes before resize/retry/path construction|
|Actual helper resource END3444|one per actual successful native open/mkstemp, paid before effect; no fake acquired resource on failed open|

Separate retained original body3407 stores observed already-produced byte view:
actual request R/role:u8/native length:size_t/unchanged bytes. Counts/native enum
widths are copied, no string-formatted fields or structure padding. Physical
record envelopes carry only length:u32/site:u32/ordinal:u64. There is no acquisition
clock or derived timing field in this owner. Positive read data prefix is one body3407
linked to actual3422 R and native returned count; no new content digest/decode.
All native results preserve errno before emission and restore it after bookkeeping.

## Native path behavior

Keep native realpath for Mac case/mount/platform behavior. Both current canonical
wrappers first make ABSOLUTE input. Linux source full-prefix readlink/faccessat plus
kernel full pathname bound gives successful selected canonical output <PATH_MAX;
the standalone relative getcwd-only glibc NULL-output bypass is not reached here.
Use caller supplied initialized PATH_MAX, paid DATA maximum BEFORE call. On success
scan NUL only for native framing and emit exactly acquired CString+NUL; on failure
publish the whole initialized supplied extent with used-length unknown. This scan
is required transport length work, not semantic transformation or a metric string.

absolute relative input still needs dynamic caller-buffer getcwd: begin native
buffer8192, initialize before acquisition, pay its full capacity+framing, on actual
ERANGE preserve returned extent/error BEFORE growing and admitting next actual
attempt. Do not impose PATH_MAX on getcwd/absolute nonexisting targets. Each grown
attempt must fit current actual producer capacity or refuse before another syscall.
Native relative empty platform behavior follows actual standard-library profile.
Retain existing business filesystem path joins/copies after capture; none is a
writer transformation. Success output storage dies only after publication.

Selected GCC canonical's native ENAMETOOLONG fallback
can attempt exists, symlink_status/read_symlink/component selection and final
exists; preserve reached failure class and actual acquisitions rather than treating
fallback as unreachable by handwaving. An owned business fallback may use paid
stat/lstat/readlink calls and existing path component work, but actual observed
outputs must publish before copied path allocation. Implement that existing business fallback with owned explicit calls, preserving
its literal decrement-after-read boundary (40th expansion refuses after its actual read), and actual reached decision flow. Native stat first
checks the absolute target (ENOENT/ENOTDIR existsfalse, other errno preserved);
process existing filesystem::path components into a required business pending
deque. Dot requires actual directory stat, dotdot uses existing parent path,
normal component appends then actual lstat determines symlink. A symlink read
uses existing lstat st_size+1 or128 whenzero, and actual readlink. On full buffer
preserve returned bytes before doubling; literal GCC checks currentcapacity>4096 on a full return, otherwise doubles.
Initial st_size+1 is arbitrary; no synthetic8192 cap is imposed. A fresh second
lstat precedes readlink, even after the first symlink-status lstat. Readlink short bytes publish BEFORE
resize/path construction. Existing relative/absolute link component insertion
and final native exists check stay business work. Native buffers/returned counts
are independent events; no metric canonicalization algorithm is added.
No full alternate libc path resolver is needed. Mac never selects this GCC
fallback. Fixed PATH_MAX first realpath may produce an extra failednative pair
on a boundary the NULL variant succeeds at; source proof and whole semantic
oracle must resolve any reachable such success before implementation.

## Admission, loops and cleanup

Each lexical owner prepays its fixed success/Error/exception terminal BEFORE
arguments. Each ordinary native attempt obtains request/return credit and full
possible output DATA BEFORE call; wait inherits raw_wait_control sticky endpoints.
No new clock/timer/deadline, cancellation polling loop or asynchronous task. The
helper never obtains a canceled semantic/no_new operation loan; operation17
suspends that bank around arbitrary adapter body. All native fields retain local
storage through the synchronous raw_emit (temporary ByteView lifetime lesson).

Before open/mkstemp, hold close request/return plus one actual resourceEND, actual
input/resource/descriptor maximum. Before mkstemp also hold one failure-unlink
request/return, and preserve complete initialized name in raw input independent
of caller allocation. Cleanup emits the actual unlink CString operand from
DATA held before mkstemp and, when available, references the earlier actual
mutable-name output body. It does not reconstruct the name or obtain fresh
DATA after failure. Cleanup remains on actual issuing
handle and consumes held credit without waits/cancellation. Claims reset before
handle drops; these resources do not outlive synchronous helper/adapter execution.
No extra dedicated cleanup producer needed for lexical-only file helpers.

Read attempts use actual8192 buffer; original read_file limit16MiB. Native positive
bytes publish even if the next limit check refuses. Failed/zero return consumes
only admitted call credit and releases unused DATA. Ordinary business read's late
I/O Error historically has detail0; preserve native errno in returned event and
keep caller mapping deliberate. Actual open failure preserves owned io/errno.
Selected implementation must preserve existing EINTR/business read behavior; its
oracle compares actual outcome, never asserts all legal short schedules complete.

Write requests preserve ACTUAL remaining.size rather than imposing a new syscall
slice size. Each short/EINTR return is captured; before next attempt pay again.
Canceled/full admission stops before that call; already emitted bytes and exact
temporary close/unlink remain paid. Native write0 keeps existing io/detail0.
Successful native close attempted once marks closed regardless of result, matching
current logic; failure catch does not double-close. Failed rename still unlinks;
successful rename transfers actual target publication and leaves no temporary
unlink claim to consume. Original observer can throw before effect, after before/
proposed originals preserved. Value allocation after rename remains outer failure
with actual native effect success and original proposed bytes already on disk.

Credits are literal E16+parentR24+actual selected argument member widths; request/
return pairs are preclaimed together, resources add3438/3439+3444 once, temporary
resources add3442/3443 once. Body DATA=E16+R24+role1+native length8+actualextent.
Budget code uses actual sizeof field sums/check overflow, no emitted computed
counts or guessed mean path/body size. Selected stat fields are actual st_mode (exists/is_directory/prior permissions)
and st_size (GCC read_symlink native buffer sizing), emitted ONLY on native
success; Darwin mode_t2/off_t8, Linux mode_t4/off_t8. No padding/reserved members
or stat predicate formatting. Existing raw-storage broad selected native fields
are a different owner; this file consumer does not read them for metrics.

The literal native pair layout follows physical-owner precedent: Q=E16+helperR24
+resourceR24+methodu8=65. Resource-less path acquisition uses the actual admitted
helper proposal R, never a fakefd/open success. Return T=E16+nativeRequestR24=40;
errno-presentbool1 with nativeint4 ONLY when returned value means failure.
Return nativeint4 max49/ssize_t8 max53/pointerpresencebool1 max46. Successful stat
T+int4+presence1+mode+size totals55Mac/57Linux; failedstat49. Names in native
request DATA have native supplied CString lengthsize_t8 and original bytes.

|Native pair|Authoritative maximum, E included|
|---|---|
|open|Q+flags4+modepresence=false1+CStringextent8+operand including first NUL =78+bytes DATA (selected read-only open has no mode operand); return49 CONTROL|
|read|Q+fd4+actual remaining8192-byte application-buffer capacity8=77 CONTROL; return53 CONTROL; positive original49+requested maximum DATA|
|stat/lstat|Q+CStringextent8+operand=73+bytes DATA; success return nativeint4/errnopresentfalse1/mode2or4/size8 =55Mac/57Linux CONTROL; failure49|
|getcwd|Q+capacity8=73 CONTROL; pointer-presence/errnopresence/[errno4] max46 CONTROL; output max58+capacity DATA|
|realpath|Q+CStringextent8+operand+outputcapacity8=81+bytes DATA; return46 CONTROL; output max58+PATH_MAX DATA|
|mkstemp|Q+mutable initialized templateextent8+all storage=73+bytes DATA; return49 CONTROL; output max58+templateextent DATA|
|fcntl F_SETFD|Q+fd4+command4+argument4=77 CONTROL; return49 CONTROL; held before mkstemp|
|fchmod|Q+fd4+mode2or4=71/73 CONTROL; return49 CONTROL|
|write input reference|Q+fd4+remainingcount8+inputR24+business cursor offset8=109 CONTROL; return53 CONTROL; no second file body|
|fsync|Q+fd4=69 CONTROL; return49 CONTROL|
|close|Q+fd4=69 CONTROL; resourceR24/request-present1/[actual requestR24]/result4/errno-present1/[errno4] return74 CONTROL; END41 =>184 held before acquisition|
|rename|Q+two CStringextents8 and both native operands=81+bytes DATA; return49 CONTROL|
|cleanup unlink|Q+outputRpresent1/[actual outputR24]+CStringextent8+actual mutated operand=max98+templateextent DATA; return74 CONTROL with actualresourceR/request-present/[actual requestR], both held before mkstemp|
|readlink|Q+CStringextent8+operand+buffercapacity8=81+bytes DATA; return53 CONTROL; positive original49+capacity DATA|

Normal original3407 body is actualrequestR24/role:u8/extent:size_t/bytes,
49+extent including envelope. CString-output variant for getcwd/realpath/mkstemp
adds used-known:bool and usedlength:size_t ONLY when known: prefix50 for failure,
58 for success. Successful output includes first NUL; failed output preserves the
entire initialized supplied storage with used length unknown. Mkstemp's initialized
mutable input includes its terminator and any suffix after embedded NUL. Its
success output is the actual first CString including NUL, never reconstructed.
These table maxima cover both branches before acquisition; unused credit releases.

C path operands include the first NUL and stop there. Tool argument capture
retains the whole already-owned business string separately, including an embedded-
NUL suffix. No new path rejection is added. Kernel-consumed path and initialized
supplied output extent are distinct records.

Successful open/mkstemp attaches fd/requestR and mutable-name borrow to a
preconstructed guard immediately, using nonthrowing assignments BEFORE returned
capture. Native close consumes fd ownership before capture; successful rename
consumes temp cleanup ownership before capture. Cleanup continues through failed
close capture and records actual unlink, without new admission or replacing the
original failure. The unlink request includes the actual mutated C operand from
its still-live vector and only an outputR that was actually acknowledged; it never
fabricates a Ref after output publication fails. Hold its DATA request and CONTROL
return before mkstemp. END is lexical ownership end, not kernel-close success.
Read open uses atomic O_CLOEXEC; mkstemp and its preheld F_SETFD(FD_CLOEXEC) run
under the existing process_spawn_mutex. No admission wait occurs under that mutex.
A normalization/capture failure closes the still-unprotected fd before unlock.

## Read semantics resolution from the completed plan attack

The owned native loop retains the existing8192-byte BUSINESS append batches,
not a guessed libc syscall size. Positive native prefixes publish before filling
that stack buffer further or entering the existing limit/append branch. Each
read's actual requested capacity is the remaining space, at most8192.

Pinned GCC13 basic_file_stdio::xsgetn retries EINTR; fstream::xsgetn loops positive
shorts and throws on other negative returns. Existing helper maps that stream
failure to io/detail0 and does not append the partial failing business batch.
The selected GNU loop preserves those outcomes, paying and recording each actual
EINTR retry before its next syscall. Pinned Darwin refill/fread returns partial
input on error; the SDK char-filebuf underflow interprets the next zero fread as
EOF without ferror. Selected Darwin therefore flushes the pending business batch
and ends on a negative read, including EINTR, while recording actual native
failure/errno. Native failure can coexist with business prefix success; no fake
complete native success is emitted. EOF/limit/append-allocation behavior keeps the
same existing business batches and exception class. New read sites capture the
owned adapter's syscalls, not libc's private internal acquisitions.

Acquired source/reference additions and hashes are in the existing manifest:
GCC13 basic_file_stdio.cc/fstream.tcc, pinned Apple refill.c/fread.c and the exact
installed SDK fstream. They are read references, not dependencies or shipped code.
The old native oracle uses actual shared-library fread dyld interposition on
Darwin (an owned fixture dylib), forwarded read on GNU, and an independent disk
inode/byte vector. Executable-local public overrides did not reach Darwin stdio;
those initial acquired0 attempts are explicitly not evidence of the defect.

## Direct native oracles and resource use

ONE existing-source baseline proves acquired file prefix lost on actual read limit/
allocation/EIO; actual native interposition forwards selected inode read and stores
native count/prefix, checks real caller Error and closed actual raw bytes. Another
real rename→acquired operation result allocation cut is already operation17's
whole oracle; reference it rather than invent a duplicate post-effect campaign.

Whole candidate fixture compares actual selected fd/input/output/native return
counts and literal disk raw bodies, original target/reopen bytes and mode/symlink
identity. Cases cover binary NUL/invalidUTF8, exact4K/8K/16MiB boundary, first and
later read Errors/limit/append allocation, real prefix before next refusal, raw
CTRL/DATA noeffect, stickycancel/heldcleanup, native shortwrite/EINTR/zero/EIO,
fsync/close/rename/unlink failure, observer exception before effect, exact edit
conflict unchangedtarget, deep cwd/proc and platform trailing/case aliases. Real
cleanup interposition checks once and preserves failedunlink result even ifignored.
All unselected native calls forward; no global production syscall interceptor.

One compiler worker when standalone baseline/candidate snapshot is permitted;
meaningful direct native checks then root-authorized independent whole code/oracle
attack, its fixes/direct recheck, affected Mac/Linux profiles, required sourcegate.
No laptopmutants, extra panel, timer, eval, dependency or installed executable change.
The original whole plan attack is complete; all six findings are resolved in
this authoritative table and the selected implementation. The independent
published-owner red and candidate native/static checks are recorded in the SAME
attack disposition. One whole code/oracle review remains before integration.
Mac direct checks do not qualify Linux-only fallback or workload performance.


2026-10-13 implementation boundary notes: native output3407 is attempted before
the associated return capture can throw. Both already-paid observations are
attempted, then ingestion failure propagates; failed original ingestion writes
the existing8001 event. Mandatory close/unlink returns name the actual acquired
resource and include request-presence/[acknowledged requestR], max74, never a
fabricated zero reference. Close holds184; unlink holds74 CONTROL and its
98+templateextent DATA request before mkstemp. Resource END follows descriptor
and temporary-name ownership consumption. Canonical(error_code) captures the
actual returned native error_code independently of its returned path; the
helper's maximum is103, while read/write/exists remain99.


Native Call releases unused return credit immediately after its actual native
return is captured; it never reserves another attempt without its own prior
admission. This makes the independently verified read simultaneous CONTROL peak
helper terminal50 + resource close/END184 + read request77/return53 =364.
The positive DATA maximum is8241 for the selected8192-byte request. Actual
maximum-minus-one refuses before that read, while the already acquired
resource still closes from its held bank. Tool request41/terminal50 admits91
before body entry;90 cannot enter. These are literal layouts, not a metric
writer count, aggregate or transform.


ONE code review F1 completion: writes capture the borrowed content argument
kind3, prehold49+content.size() before the existing copy, and immediately emit
the completed proposed body before exists/read/file.before. Edits use the actual
unique position and checked next.size()-old.size()+replacement.size() before the
SAME replace, then emit immediately. proposed() accepts that explicit preheld
claim; it never obtains a new grant after acquisition or duplicates the helper
input. The existing before/proposed observer order is unchanged. F2 corrects only
this cleanup description; native3442 already emits its actual operand correctly.

Backstop's confined method7 dependency reads an exact native prefix, not a whole
file with a small size limit. raw_file_read_prefix/read_file_prefix prepay the
same acquired descriptor cleanup before open, then each remaining native read
and unchanged positive bytes before string append. There is no fstat, trailing
read or second header decoder. A short source returns incomplete after preserving
its acquired bytes; ordinary native failures and EINTR retain their existing
separate outcomes. The optional owner out-reference names the actual paid3410
request even if a later acquisition fails, and remains empty for opt-out calls.

The real112-byte fixture checks one read from a2048-byte source, short7+EOF,
EIO, EINTR retry, positive112 then append allocation failure, exact raw method/
count/return fields, held close and opt-out behavior. Matching strict link,
direct prefix execution and narrow static analysis of raw_file/tools/fixture
pass (context/file-owner-unit/prefix-{build,native,static}.log). This extension
belongs to the reviewed Backstop dependency, not another whole File review.
The shared RawCallSites API change preserves all native bodies and grants.
