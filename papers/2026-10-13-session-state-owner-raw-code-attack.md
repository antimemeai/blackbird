# Session settings, identity and discovery: one code/oracle attack

2026-10-13. This is the unit's ONE independent code/oracle review. Read the
accepted Session subplan and its sole plan review, complete session/raw_session
sources and headers, RawCaptureFailureScope and the final raw API failure marks,
RawCallerTrace's confined failure checks, the new file absolute6 source/helper
and native absolute fixture cases, the complete session_raw fixture, and Main's
explicit startup/dispatch origins. No production/test edits, builds, new panel,
transport rereview or workload qualification ran for this review. Concurrent
program fixture work is a separate owning unit.

Read current BLACKBIRD/AGENTS and BOUNDED_HARDENING. Existing source-grounded
Session and file study remains the literature/reference input; this review adds
no library or broad acquisition. Published-source Session red is preserved in
context/session-state-owner/baseline.log. Root's actual matched direct result is
context/session-state-owner/final-direct.log: session_raw passed. That existing
green does not establish the untested fault classes below.

## SS-C1 — final terminal publication can fail after the last capture check

At the reviewed include/blackbird/raw_session.hpp:38–54, run checks
require_capture INSIDE the body handed to RawCallerTrace::run. That correctly
turns a swallowed nested instrumentation error into the owning Error outcome.
However, RawCallerTrace::run subsequently calls its noexcept finish. The actual
raw_emit or notification failure in that paid terminal marks failure_ through
raw_fence AFTER the body check. Finish's destructor only propagates and closes
the failure scope; it never throws. The method therefore still returns success.
For listing, src/session.cpp:363–370 publishes4225 inside the body and assigns
its completion out-ref after run returns, even if the following4221 terminal
failed. Main can then accept that completion as UI input.

Keep the inside-body check and ALSO require capture success after trace.run has
emitted its terminal, before returning or exposing the completion ref. Apply to
both void and returned variants. The latter check must occur before Finish
closes the synchronous failure scope. Preserve actual business errors and
already-reached native bodies; do not fabricate a second terminal after a
failed actual terminal. Root accepted this source finding during this review.

Direct oracle: within the existing owner fixture, forward a real selected
producer publication and fail its actual final terminal/notification path after
the body has succeeded. Require the actual recording Error to escape the
synchronous call, listing completion to remain absent, and no accepted UI/next
business work. A body-source refusal is a different boundary and cannot replace
this cut. An existing recorder/native-failure hook may be used; do not add a
production injection seam or a second recorder certification system.

## SS-C2 — the native fixture counts several outcomes without checking their fields

The current tests/session_raw_test.cpp:192–195 parses only the parent of4211,
4219,4221,4243. A returned outcome substituted for the invalid save's actual
invalid_range Error, a wrong reached stage, omitted Error bytes, or wrong
filesystem predicate/error/end data all still satisfy ends[parent]==1.
Likewise lines215–225 discard failed stat errno and successful stat mtime; the
lstat check accepts any regular-file mode. Lines228–234 discard the completed
row count. The fixture never checks actual4213 locator bytes,4215 assignment,
4216 equality,4224 row/index/metadata linkage, or4226's actual absolute body
reference. Consequently several declared native fields could be corrupted or
omitted while its final presence booleans remain green. This is an oracle
finding, not a claim that production currently corrupts those fields.

Tighten the SAME fixture's existing native parser and independent expectations:
check branch/stage/Error presence and exact actual invalid_range/capacity where
specified; returned filesystem error/predicate/end variants only when reached;
compare mode/mtime/errno to independently acquired native outcomes for the
actual fixture paths; compare4225 count with each returned listing; require
4213's existing locator,4215 assignment tags and4216 duplicate predicate; and
resolve4224 metadata request plus4226 to actual role7/path bytes. Expected bytes
must remain independent of the raw emitter/description/production serializer.
No new test-of-test layer or broad filesystem campaign is requested.

## SS-C3 — declared pre-publication settings refusal is absent

The full-DATA refusal cases at tests/session_raw_test.cpp:262–345 correctly cut
snapshot native read and post-read metadata fields. They preserve the real
file/mtime and forbid replacement and listing completion. Neither reaches
SessionStore::save input admission BEFORE the authoritative program append.
The subplan explicitly promises one-byte-short original admission forbidding
that semantic effect, but the existing save cases are success/duplicate/business
invalidity only. Add one finite actual selected grant-minus-one cut before the
first named settings original, with the small CONTROL request still funded.
Assert capacity, no4213/4215, unchanged settings and exact audit fact count, no
replacement, and the owning Error outcome. This directly answers the remaining
Session effect-admission claim without expanding the unit.

Do not fabricate the plan's generic postappend return-allocation scenario:
AuditLog formats its locator before append, save returns void and moves its
already-owned settings after publication, and identity returns a fixed native
triple. Any such claim must identify a genuinely reached allocating boundary;
the present source does not expose the originally suggested postappend locator
formatting boundary. State that limitation plainly rather than manufacture it.

## Source conformance and boundaries

Leaf math is49+N (E16+R24+kind1+size8); locator48+N; identity88;
assignment41; predicate42; row72; completion48; absolute link64.
raw_session_stat pays41 plus max(48,44+native method field width), preserves
actual rc/errno immediately, and reads only mode_t for lstat success or time_t
for stat success. No failed struct field or padding is read. Iterator-open and
increment fund the next path before its acquisition and emit the end predicate
only if the actual error-code is clear. Existing short circuits, sort, weak
canonical operation and ordinary damaged/missing fallback remain explicit.

The absolute6 integration (src/raw_file.cpp:476–527,1021–1052) funds actual
absolute-copy extent, or grows native getcwd, preserves each original output,
then funds49+actual cwd length+separator+relative operand before joining.
role7 returns the actual path body; Session4226 names it explicitly. Native
file cases cover real >8192 cwd/ERANGE growth, removed cwd/native error, and
exact/minus-one copy/join bounds. No PATH_MAX cap is imposed on acquired cwd.
Weak canonical remains the existing business API; quote funding accounts for
five bytes per apostrophe and two delimiters. This is review of the confined
Session integration, not reopening settled file/transport ownership.

RawCaptureFailureScope stores native optional<Error> only in a selected
synchronous thread scope, preserves its first actual failure, propagates on
finish, and never changes RawWaitControl ABI or exports a borrowed pointer.
Final claim/wait/emit/fence errors mark it; finite try probes remain unmarked.
RawCaller overflow/field checks now mark actual refusal before throwing.
Session metadata catches inspect the actual origin before damaged fallback,
and snapshot catches inspect it before writing replacement. Main startup and
worker command calls copy immutable real refs; --sessions before runtime stays
opted out. Root's concurrently edited Main input/restart work is outside this
Session code review.

Resolve SS-C1–SS-C3 with the finite direct checks above under this SAME review.
No second code panel, review of the recheck, new host-profile campaign or
qualification is requested. Owner fixes/recheck and root delivery remain theirs.

## Owner disposition and finite direct result

SS-C1 repaired in the synchronous helper: require capture both inside the body
and after the paid terminal, before exposing success/out-ref. Native fixture
forwards the actual producer send, arms the existing channel and closes its
real socket at the final4211/4221 notification. Both void-save and returned
listing throw the actual io/EBADF; listing exposes no completion. Save's already
acknowledged semantic record/settings remain, checked against the actual retained
program rather than a guessed total fact increment (native issuer facts also
exist). No second terminal or production injection API was added.

SS-C2 expands the same raw parser: exact native branch/stage/Error widths,
invalid_range/corrupt failures, filesystem result variants, actual independently
observed lstat mode/stat mtime, actual listing row counts, record-identity locator,
settings assignments/equality, row/metadata and absolute role7/helper linkage.
SS-C3 uses actual DATA49+name.length−1, with the small owner CONTROL funded:
capacity precedes the first borrowed field, no authoritative append, locator,
assignment or replacement, exact unchanged settings/fact count, one owning Error.
Mac matching session_raw is GREEN in
`context/session-state-owner/finite-direct.log`; final narrow static checks in
`finite-static-final.log` are clean. No rereview, extra panel or qualification.
