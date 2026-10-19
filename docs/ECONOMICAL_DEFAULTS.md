# Economical default observations

Operator2026-10-15: implement the next ordinary-workflow economy improvements,
following the [harness comparison](../papers/tokenomics-2026-10-15-harness-comparison.md).
This unit reduces avoidable prompt material without selecting another model,
lowering reasoning effort, enabling compaction or claiming measured cost savings.

## Behavior and implementation plan

The default `programs/turn.lua` supplies `output_max_bytes=8192` and
`output_head_bytes=2048` to `exec` when the caller omits a budget. This keeps up to
2 KiB of opening context and 6 KiB of final diagnostics: half the former raw
payload, with three times as much room for errors/summaries as setup. Sizes are
byte budgets, not token estimates or claims of measured provider savings.
For un-ranged `read_file`, the default stays 16384 bytes, head-first. An explicit
range bypasses that file default. Explicit limits (including zero and larger
limits) remain authoritative. An explicit exec budget keeps head-first behavior
unless `output_head_bytes` requests a split; it must not exceed the budget.
Custom Lua callers keep full-read defaults, subject to existing native capacity.

When exec output exceeds a split budget, `output` contains the head, an explicit
`[... bytes [START,END) omitted ...]` marker, and the tail. For binary excerpts it
is `{head: <text-or-hex>, tail: <text-or-hex>}` instead. `byte_start`/`byte_end`
describe the head; `tail_byte_start`/`tail_byte_end` describe the tail. All offsets
are absolute, zero-based, half-open raw-byte offsets. `returned_bytes` excludes
the display marker and encoding overhead; `output_bytes` counts the full original.
`omitted_range` gives the exact gap. `next_read={tool:"read_process_output",
arguments:{output_ref,byte_start,byte_end}}` supplies an exact next page (up to
8192 raw bytes) inside that gap. A zero budget returns no bytes but still offers
a progressing next read. No anonymous placeholder or effect replay is involved.

Recovery uses `read_process_output` with the same canonical `range` shape as
`read_file`: one-based inclusive lines or zero-based half-open bytes. Legacy
`byte_start`/`byte_end` remain supported; never mix selector families. Line reads
retain LF and do not invent a line after trailing LF. Optional `output_max_bytes`
pages long selections; actual offsets and `next_read` are returned. This keeps
one retained-process recovery owner and reuses the model's existing line-range
concept, rather than adding file identities or disk fallback for command output.

Exec and recovery share the same retained original reader. A process-local index
stores source locators (not output bodies), scans only new committed metadata,
and rebuilds lazily after reopen. Each read assembles the selected command's
complete original before projection, up to the existing 16 MiB command capacity;
this does not claim a streaming/memory optimization. The command job's rolling
UI cache is not mistaken for the complete original. Originals and request/effect
linkage remain unchanged. No ageing, archiving or automatic compaction is added.

Native `read_file` accepts `output_max_bytes`, applies it after range selection,
and reports total file bytes, selected bytes, returned bytes and omitted selected
bytes when a limit is supplied. Budget cuts retreat to character boundaries and prefer a newline within 256
bytes (without discarding more than half the prefix). Explicit byte slices stay
exact, including hex when a caller intentionally selects part of a character.
Truly binary observations remain hex. Results carry half-open absolute
`byte_start`/`byte_end`; omitted counts use the actual cut, not the requested cap.
Capture the complete original before projecting its model-facing result.

An omitted file prefix carries `next_range`, an exact bounded byte page that can
be passed back with its `snapshot`; this continues from the actual cut, not from
the nominal cap. Hints themselves use character/nearby-line boundaries. A zero
budget offers a 16384-byte next page; tiny nonzero hints allow at least four bytes
so a character larger than the initial budget does not trap recovery in an empty
page. The initial observation still obeys the explicit budget.

The CodingEngine returns a `snapshot` source reference for a successful disk read.
Its shape is `{journal: <32 hex digits>, sequence: <positive exact integer>}`.
`read_file` accepts exactly one of `path` and `snapshot`; the latter reads the
retained original directly by journal/sequence, with the same ranges and optional
limit, and never opens the old path. Preserve snapshot identity in the result.
This avoids scanning the transcript and works after the file changes or disappears
and after session reopen. Unknown/malformed references refuse, without disk fallback.
Standalone LocalTools has no retained store and refuses snapshot requests.
For a supplied limit, `file_bytes` is the full original length; `selected_bytes`
is the requested range length after EOF clipping; `returned_bytes` is the actual
character/line-safe prefix length at most the budget; `omitted_bytes` is selected minus returned. The
budget counts raw bytes, before any hex or external JSON encoding. Invalid-range
guidance for a snapshot read preserves the supplied snapshot in its example.

Default instructions direct targeted reads, recovery when observations omit useful
facts, and Lua composition of mechanical operations. They preserve complete tool
groups and explicit model/effort choices. No automatically selected judge, semantic
filter, result reuse, paid keepalive, scheduler or automatic context pruning.

## 2026-10-19 verification

The exec UTF-8 seam oracle fails before q4n. The native default-workflow oracle
fails before 47k (missing trailing diagnostic), and its enlarged 1200058-byte
build also exposes the rolling-cache head loss on q4n-only code. The fixed
workflow returns [0,2048) and [1193914,1200058), then the deterministic provider
fetches line 3 exactly at [600013,600033). Reopen recovers the entire build after
its disk source is deleted; the one-byte effect marker remains X. This is a
mechanism check, not a claim about live model quality or provider cost.

The seam and continuation tests fail against their old implementations. File and
process hints now end on the same safe boundaries as observations. Seven affected
Debug tests pass (28.10 s), and build/release/blackbird builds successfully. q4n's
locked source hook passes all 265 Debug tests and owned lint; the 47k source
commit must pass the same hook. Full evidence and bounded recheck notes:
[papers/sol-observations-2026-10-19/PLAN.md](../papers/sol-observations-2026-10-19/PLAN.md).
No ageing, TUI product change, installed activation, push or tracker operation is
part of this unit.

## Historical direct checks and delivery (2026-10-15)

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

## Historical delivery observations (2026-10-15)

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
