# Economical default observations

Operator2026-10-15: implement the next ordinary-workflow economy improvements,
following the [harness comparison](../papers/tokenomics-2026-10-15-harness-comparison.md).
This unit reduces avoidable prompt material without selecting another model,
lowering reasoning effort, enabling compaction or claiming measured cost savings.

## Behavior and implementation plan

The default `programs/turn.lua` supplies `output_max_bytes=16384` to `exec` when
the caller omits a limit. For `read_file`, supply the same limit only when neither
a limit nor any canonical/legacy range is supplied. Explicit choices, including
zero, remain authoritative. Custom Lua calls keep the existing full-read default.
The 16 KiB value is an overridable observation default, not a token or memory cap.

Native `read_file` accepts `output_max_bytes`, applies it after range selection,
and reports total file bytes, selected bytes, returned bytes and omitted selected
bytes when a limit is supplied. Preserve binary and split-UTF-8 slices as hex.
Capture the complete original before projecting its model-facing result.

The CodingEngine returns a `snapshot` source reference for a successful disk read.
Its shape is `{journal: <32 hex digits>, sequence: <positive exact integer>}`.
`read_file` accepts exactly one of `path` and `snapshot`; the latter reads the
retained original directly by journal/sequence, with the same ranges and optional
limit, and never opens the old path. Preserve snapshot identity in the result.
This avoids scanning the transcript and works after the file changes or disappears
and after session reopen. Unknown/malformed references refuse, without disk fallback.
Standalone LocalTools has no retained store and refuses snapshot requests.
For a supplied limit, `file_bytes` is the full original length; `selected_bytes`
is the requested range length after EOF clipping; `returned_bytes` is the smaller
of selected length and budget; `omitted_bytes` is selected minus returned. The
budget counts raw bytes, before any hex or external JSON encoding. Invalid-range
guidance for a snapshot read preserves the supplied snapshot in its example.

Default instructions direct targeted reads, recovery when observations omit useful
facts, and Lua composition of mechanical operations. They preserve complete tool
groups and explicit model/effort choices. No automatically selected judge, semantic
filter, result reuse, paid keepalive, scheduler or automatic context pruning.

## Direct checks and delivery

Before implementation, add tests that fail on the published source: exact bounded
file slices/counts with complete captured bytes; default native workflow command/
file observations; explicit range/limit and full custom Lua behavior; recovery of
an omitted middle fact from its immutable snapshot after mutation/unlink and
reopen; binary/zero/invalid limits and malformed references. Assert useful recovered
content and one actual command effect, rather than checking instruction wording.

One independent design/plan review before source changes; one code/oracle review
of this conceptual unit and finding-driven correction within the same review lane.
No review of reviews or certification campaign. Run the required source commit hook
once on the final candidate and affected optimized/sanitizer/Linux tests. Record
actual results, close only delivered work, back up issues, commit and push the work
branch. Main integration and executable activation are separate from this change.

## Delivery observations

The actual native default loop produces two provider requests and ten linked tool
results. A 40000-byte file/command observation defaults to16384 returned bytes;
explicit24000/30000-byte file selections and20000-byte command observations remain
intact, zero limits remain empty, and a custom Lua read remains complete. The
omitted middle constraint is recovered exactly through its same snapshot after
replacement/deletion/reopen. One command marker is appended once. These are
deterministic mechanism checks, not a live token/cost or general quality result.

Independent [design/code-oracle review](../papers/economical-defaults-2026-10-15-review.md)
and finding-driven corrections are complete. Direct Debug tools/economical_defaults
pass2/2 (2.83s). Affected macOS Release coding/tools/economical_defaults pass3/3
(16.71s); ASan/UBSan pass3/3 (27.21s). Linux Debug/Release/ASan pass3/3 each
(6.29s/3.43s/17.04s). The first installed full Debug suite passes137/137 (221.81s),
then analysis finds an avoidable Value parameter copy in the new fixture helper.
Change that parameter to const reference; its direct clang-tidy check passes.
Stop the superseded analysis and run the required final commit gate after the
correction. That gate passes136/137; `operation_raw` hits its existing120.15s
watchdog after printing `provider variant 3` (total387.33s). The previously tracked
intermittent test is `arconaut-7qn`; no root cause or feature association is
established. The earlier timeout in that issue stopped at a different phase.
No host-profile or independent-review campaign is repeated for the fixture
signature correction, and no further whole-suite rerun is launched for a pass.

The source candidate was held uncommitted after that hook failure. The remaining
owned analysis completes directly
with exit0: all204 actual C++ translation units and reached headers, C++ formatting,
owned Lua syntax/format, fresh LuaLS (`Diagnosis completed, no problems found`)
and contract probe pass. Log: `context/economical-defaults-analysis-complete.log`.
The same owned `lint()` function uses the exact-source build database, with no
test rerun.

The operator then requests a twenty-run study, repair of its concrete findings,
only affected tests, and merge. The
[study](../papers/2026-10-15-operation-raw-20-run-study.md) completes19passes,
1deadline assertion failure and0timeouts. The
[repair](../papers/2026-10-15-recorder-stop-repair.md), source `e73b379`, makes
stop notifications persist across the recorder's flag-check/poll boundary and
removes the deadline test's assumption that blocking starts within20ms. A native
cut fails old code and passes both graceful shutdown and explicit recovery with
exact originals preserved. Debug raw_admission/raw_runtime/raw_recorder_cuts/
operation_raw pass4/4 in22.47s; focused formatting/static analysis also passes.
This does not retrospectively establish the earlier timeout's cause.

Integrated on master under the operator's explicit merge instruction, using the
completed checks without another full-suite run. arconaut-ns1 and arconaut-7qn
are closed for the delivered changes. No running executable is activated by the
source merge.
