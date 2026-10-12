# File47–48 whole owner — proposed design for ONE plan attack

2026-10-11. Root owns native LocalTools read/write/edit and lexical descriptor/
temporary-file lifetime. Sites3400–3499 reserved. This document replaces earlier
partial candidates as the proposed design; its explicit claims below must pass
ONE whole plan attack before implementation. No production code written.
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
existing representations. Capture native write body ONCE as soon as the actual proposed string exists,
BEFORE the existing file.proposed observer/capture callback or temporary-file
effect. Pass its typed RawFileInput {actualbodyR,length} explicitly to helper;
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
|Reached tool arguments3402|existing acquired path/count/range/old/replacement as borrowed native fields; no visitor over Value for metrics; unacquired missing fields omitted|
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
record envelopes carry native acquisition clock as existing transport already does;
no second timing/duration calculation. Positive read data prefix is one body3407
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
its existing40symlink limit and actual reached decision flow. Native stat first
checks the absolute target (ENOENT/ENOTDIR existsfalse, other errno preserved);
process existing filesystem::path components into a required business pending
deque. Dot requires actual directory stat, dotdot uses existing parent path,
normal component appends then actual lstat determines symlink. A symlink read
uses existing lstat st_size+1 or128 whenzero, and actual readlink. On full buffer
preserve returned bytes before doubling; existing GCC allows up to8192 then
reports ENAMETOOLONG on another full return. Readlink short bytes publish BEFORE
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
of caller allocation. Cleanup references that actual name body rather than copying
it anew or obtaining fresh DATA after failure. Cleanup remains on actual issuing
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

|Native pair|Literal request/return allowance including E|
|---|---|
|open|Q+flagsint4+modepresence1+[mode2/4]+CStringlength8+bytes=80/82+bytes DATA, return49CONTROL|
|read|Q+fdint4+requestedsize8=77CONTROL, return53CONTROL, positive body49+8192 maxDATA|
|stat/lstat|Q+CStringlength8+bytes=73+bytesDATA; return55Mac/57LinuxCONTROL|
|getcwd|Q+capacitysize8=73CONTROL; return46CONTROL; body49+providedcapacityDATA|
|realpath|Q+CStringlength8+bytes+outputcapacitysize8=81+bytesDATA; return46CONTROL; body49+PATH_MAXDATA|
|mkstemp|Q+mutabletemplatesize8+bytes=73+bytesDATA; return49CONTROL; outputbody49+templatecapacityDATA|
|fchmod|Q+fdint4+mode2/4=71/73CONTROL; return49CONTROL|
|write input reference|Q+fdint4+remainingcount8+actualinputR24+actualinputoffset8=109CONTROL; return53CONTROL; no second body|
|fsync|Q+fdint4=69CONTROL; return49CONTROL|
|close|Q+fdint4=69CONTROL; return49CONTROL; resourceEND41 =>159 held before open|
|rename|Q+twonativeCStringlength8+bothbytes=81+bothbytesDATA; return49CONTROL|
|cleanup unlink input reference|Q+actualmkstempoutputR24+nativeCStringlength8=97CONTROL; return49CONTROL =>146 held beforemkstemp|
|readlink|Q+CStringlength8+pathbytes+buffercapacity8=81+pathbytesDATA; return53CONTROL; positivebody49+capacityDATA|

Successful output known-length bodies may emit fewer bytes than held max, unused
credit releases. getcwd/realpath failure body uses known initialized extent with
unknown usedlength; the common3407 header gains nativeusedpresencebool1 and
usedlengthsize8 ONLY when known. Thus change body prefix49 to50/58 for these two
CString-output methods and mkstemp unknownfailure variant where necessary;
ordinary positive read/readlink remains actualcount-known49. Exact field layouts
are immutable native variants, not a normalized-body encoder.

Tool fixed41request+53terminal=94; helper49request+59terminal=108 maximum,
actual native length/error fields in terminal; existing reached native callback
pair41+50=91 acquired before each observer. File close159 and temporary unlink146
are independent of ordinary call credit; no finite aggregate budgets promise
all read/write/fallback iterations. Whole plan attack must challenge these
native field sums before source.

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
Proposed complete field/surface design now ready for ONE whole plan attack;
no production source/genuine owner red or native candidate qualification claimed.
The native resolver success-bound and fallback preservation are explicit attack
claims; if either fails, repair this design before source.
