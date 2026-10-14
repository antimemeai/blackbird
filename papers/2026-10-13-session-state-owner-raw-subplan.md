# Session settings, identity and discovery raw owner

2026-10-13. One conceptual unit: session.cpp's authoritative settings/identity,
derived discovery metadata and read-only discovery presentation. Main input and
restart/successor are separate following units. Stop before workload qualification.

Read live session.hpp/cpp, context.hpp/cpp AuditLog publication API, existing
session_store_test and saved-state/recovery fixtures, reviewed raw design/census,
old Unix emission/transport studies, and the file owner's native acquisition
study. Existing filesystem/BBM methods remain business operations. No dependency,
second metric encoder, Value visitor, string formatter, duration or clock is added.
Default context=nullptr opts direct helpers out; Main explicitly passes its
actual startup or dispatch Ref. Parent is immutable across worker/queue lifetimes.
Read-only --sessions before a runtime exists stays read-only and creates no run.

## Sites, representation and admission

E16, R24, native fields without padding. RawCallerTrace pays request plus maximum
52-byte Error terminal, with explicit extra widths for returned variants.

SS1 actual failure origin: a selected synchronous Session/Main call owns an
explicit RawCaptureFailureScope with optional native Error. Shared raw APIs mark
only FINAL claim/wait/emit/fence errors into that typed per-thread scope; transient
backpressure and raw_try_claim probes do not mark. Nested scopes propagate the
actual first failure on destruction to their enclosing synchronous owner. Existing
RawWaitControl ABI remains unchanged: its copies can escape into RawSampler, so
no stack failure pointer is added there. No process-wide failure flag, callback
registry, syscall interception, parsing, timer or metric transformation. Business
catch blocks inspect this actual scope and rethrow capture refusal before damaged/
replacement fallbacks. Missing/unreadable/oversized business reads keep old recovery.
Selected method success checks its actual scoped capture failure after paid end
emission; no accepted UI completion is supplied for failed recording. Independent
ordinary/native failure cases and actual raw-grant refusal distinguish both paths.
 Whole
session4210/4211 request: parentR,u8 action (restore1,save2,identity3,snapshot4),
journal16,u64 sequence,u64 end. Whole listing4220/4221 request: parentR.
Stage is actual reached boundary, not an inferred success. Ordinary admission
fails before the next acquisition; every completed prior source remains recorded.
No waits occur under a UI mutex; session/discovery are single-owner methods.

4212/4222 leaves: ownerR,u8 kind,size_t N,existing bytes (E+R+1+8+N=49+N).
Named kinds: name1,model2,effort3,workflow4,path5,serialized6,locator7,
status8,quoted9,resume_command10. Prepays49+known source bound BEFORE the
existing copy/encoder/path acquisition; captures its completed string immediately
before subsequent fallible work. Borrowed input leaves are copied before validation
or by-value acquisition; change save's by-value parameter to const reference,
then acquire its owned copy after capture. This is deliberate repair of an input
acquisition gap, not a new business setting or a writer projection.

4213 publication: ownerR,size_t N,actual existing returned textual locator bytes.
AuditLog's existing no-id overload returns a textual locator, not native
ApplicationRecordId; do not decode or manufacture a native ID. Prepay48+32 before
that existing return acquisition and copy before assignment. Underlying Context/
retained/storage owners already capture publication/source; session emits its own
actual locator and cursor, not another business encoder.4214 identity return:
ownerR,actor16,conversation16,workflow16, paid88 before existing identity recovery/
issue/record path.4215 setting/state assignment: ownerR,u8 action, paid41 before
log append/copy; emits only after actual assignment.4216 actual predicate:
ownerR,u8 kind,bool returned, paid42 before reached equality/validation predicate.
Missing end never means accepted state. Restore captures each selected retained
settings field before its existing assignment; links constructor request cursor.
Existing retained settings packet originals remain retained by their owner.

Snapshot metadata validates then prepays49+512KiB before the SAME existing
encode_packet_string, captures exactly that business output (kind6) immediately,
then explicit file context points to its actual owner. File helpers emit native
read/write originals/failures; unchanged metadata stays unchanged and no write
is invented. Existing read errors still cause replacement from authoritative
settings; those failed reads are distinct from the overall successful snapshot.

Listing captures requested path before absolute/normalization. Existing absolute,
lexical normalization, canonical/stable/quoted/resume-command business strings
are captured after their actual acquisition with preheld source-derived bounds:
absolute/normalized <= requested length+PATH_MAX; canonical <= actual path length
+PATH_MAX; iterator entry <= root length+1+NAME_MAX; quoted <=5*stable length+2;
resume_command <= literal prefix length+quoted length. Checked uint64 arithmetic,
UINT32 envelope bounds. No temporary result disappears if the next copy throws.
Existing weakly_canonical remains its actual std::filesystem operation under a
confined typed wrapper; it is not replaced by file canonical. Exists uses the
selected native helper, with its throwing overload recreated where actually used:
real error_code becomes the same filesystem_error, error_code overload OR checks
retain their original short circuits. No new canonicalization
for instrumentation. Named parsed settings are captured after the existing
file original/decode and before copies into the displayed Value. Status string
constants are not synthesized for metrics; existing actual status is copied.

4240/4241 native stat/lstat pair: requestR,u8 method,int descriptor absent (path
already retained as named leaf); return requestR,int rc,[int saved errno on negative | mode_t mode for
lstat success | time_t mtime for stat success]. Pay request41 plus
max(48,44+sizeof(method-specific native field)) before syscall; capture rc/errno and successful native fields before predicates.
No reading undefined failed struct fields or whole struct padding.

4242/4243 filesystem pair: request parentR,u8 method (symlink1,directory2,
iterator_open3,iterator_increment4,entry_directory5); terminal standard52 plus
native returned bool and int error-code value (maximum57). Iterator operations
carry actual error-code value and only the reached end predicate; no iterator-end
claim after an error. Actual entry path leaf grant is held before open/increment
that acquires that entry. Preserve existing short circuit order and sorting.
4224 row publication: listingR,size_t row position, paid48 before Value creation/
push. Emit after push; successful earlier rows remain sources if a later fails.
4225 completion:listingR,size_t row count, paid48 before final Value construction;
out-parameter supplies this actual Ref to UI kind1. If output publication fails,
out-ref stays absent, not a synthetic success. UI does not walk/encode the Value.

## Implementation and direct oracles

New raw_session.hpp/cpp holds typed fixed-field mechanics/description, reuses
reviewed RawCallerTrace. Confined session.hpp/cpp integration and Main call-site
context/UI-origin wiring. Root owns CMake/runtime registration. No restart source
in this unit. Existing default direct APIs and session-store cases stay compatible.

Published-source red: actual SessionStore save/identity/snapshot/listing under a
real private RawRuntime produces native semantic effect but lacks4210/4212/4225.
Independent literal fixture fields plus exact actual saved business bytes/native
stat outcomes are expected raw bodies; no expected encoder generated from emitter.
Green direct checks: save/change/duplicate/invalid/oversized, new/recovered identity,
unchanged metadata mtime, missing/damaged/symlink/unreadable discovery, exact paths/
quoted command, open/error/increment results, constructor reopen and selected vs
unselected API. One-byte-short source admission forbids semantic append/native
read/write; postappend return allocation failure preserves actual publication and
unchanged/changed state accurately. Use actual allocation/native error cuts already
available in owner fixtures where relevant, no broad consumer/performance campaign.
One independent whole code/oracle review; finite fixes and direct recheck only.

SS1–SS4 resolved in this same plan: actual failure origin above; exact leaf49
and apostrophe5 bounds; weak canonical and throwing/error-code exists preserved;
method-specific native stat outputs. AuditLog existing textual locator is formatted
BEFORE append, so no fabricated postappend formatter fault is claimed. Identity
triple is emitted before subsequent hex/Value work;4225 after final Value success.

Concrete implementation details after source grounding: snapshot has no AuditLog
cursor argument, so its distinct4217 request carries parentR,action4 and actual
identity triple (89-byte request).4211 links that actual request. Metadata parse/
validation gets4218/4219, so ordinary caught decode/allocation failure is recorded
separately before listing publishes `damaged`.4224 adds actual metadataRequestR
(72-byte row grant) rather than guessing a parsed-field source. Native locator4213
uses its declared48+32 grant and bytes without a kind member. Restore4215 mutation
tags1base/3name are emitted immediately after their actual assignments, before
subsequent name copy/validation. Listing completion out-ref is supplied only after
its actual paid final end also succeeds.

Absolute-path correction: pinned current_path/getcwd source and earlier file
review explicitly permit output beyond PATH_MAX. Do not fund std::absolute with
that guessed cap. Reuse the file owner's native growing getcwd in distinct
absolute6/role7 helper. At actual cwd return, fund49+cwd CStringlength−1+1+input
length BEFORE existing path join, emit actual joined path immediately and return
its real preserved-body Ref. Absolute-input copy uses actual input extent. The
Session owner captures requested input before that helper; lexical normalization
then funds actual returned absolute native length. Weakly_canonical remains its
actual API on this absolute input, bound existing canonical prefix PATH_MAX plus
actual suffix length; source proof from selected absolute canonical operation
and existing reference explains that bound, not a cap on getcwd. This confined
new file method belongs to session integration review; settled file owner is not
rereviewed or relabeled as another assurance pass.

Failure context closes and propagates at actual owner run return/error, not at
a later C++ object destruction after row processing. Catch checks metadata's
actual local capture error before any damaged classification. Session4226 links
actual file absolute role7 body; original-range offsets are logical payload
offsets, not raw record offsets. New session native fixture forwards real open/
close and fills the actual producer DATA grant before selected metadata read or
parsed-field capture. Its oracle requires zero write-helper requests/completions,
unchanged native file and mtime, and actual failed owner terminal.

Checked raw-claim preparation (frame-width/field-capacity/credit overflow) also
marks the actual scoped capture failure before throwing; it must not look like
business validation. RawCaller layouts and actual native adapter outcomes remain
unchanged. The Session/Main integration review covers this confined origin tag.
