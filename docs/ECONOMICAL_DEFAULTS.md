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
Since 2026-10-19 (`arconaut-o17`) these are native defaults for every program,
not only the default workflow: a custom Lua or built-in workflow call that names
no `output_max_bytes` gets the same shaped `exec`/`read_file` observation, and so
do `process op=foreground` and an unranged `read_process_output` (8192 bytes,
2048 head plus 6144 tail; `output_head_bytes` alone keeps that tail after a larger
head). Explicit ranges and limits stay exact. The operator's own `/exec`,
`/process` and `/fg` calls are not model observations and stay complete; calls a
`/lua` program or workflow makes inside them are shaped like any program's. Each
operation's decision record lists the keys Blackbird supplied in
`native_defaults`.

Since T1 (`arconaut-rd7.9`, design CONTEXT_LIFECYCLE §9) a result is shaped once,
where it becomes visible to the model, after its whole result is retained as its
original:
- the output of the model's own function call, when a program dispatches it. A
  relay declares the call it answers, `blackbird.call(name, args, {call_id =
  item.call_id})`, as `programs/turn.lua`, `ultracode`, `useful_turn.lua` and
  `economy_pilot.lua` do; the engine accepts a call_id it recorded from this turn's
  responses that no output has answered yet, and refuses an unknown or answered one
  with `reason` `call_id_unknown` or `call_id_answered` (the call is not run).
  An undeclared call counts as the model's only when its name and arguments equal an
  undispatched model call, and only until the turn's first declaration; otherwise
  its whole result enters context;
- a drained workflow continuation, inside its developer item;
- host page observations from the application connection.

Calls a program makes inside its own execution (`blackbird.call` at any nesting,
inside `lua`, a workflow or a Lua-defined tool) return whole results; o17's defaults
above still apply to `exec`, `process`, `read_process_output` and `read_file`.
Content a program adds with `blackbird.append` is its own authored context and is
not shaped. The operator's outermost call, including an operator workflow
invocation, stays exact.

The tools shaped this way are every tool except `exec`, `process`,
`read_process_output` and `read_file` (their own shapes above) and `write_file`,
`edit_file`, `context_inspect`, `audit_inspect`, `trajectory_read`,
`variables_read`, `tasks_read` and `git_observe` (they bound their own output). A
result's text is the string itself when it is a UTF-8 string, otherwise its JSON,
which is what the provider receives for a structured output. Within its budget, or
when the cut would not be shorter than that text, the result returns unchanged,
structure included. Over it, the result becomes one string: the first 4096 bytes of
that text, the line `[... bytes [H,T) of N omitted; read_process_output with
result_ref R and range {mode: bytes, start: H, end: T} pages them, 8192 bytes each
with next_read ...]`, and the last 12288 bytes. Cuts keep UTF-8 characters and JSON
escape sequences whole and prefer a nearby newline. A result with no text form
within the provider limits (96 MiB, 500000 JSON nodes) becomes an error result
`{error: "result_text_unavailable", reason, result_ref, detail}`; its original stays
retained.

Budgets count bytes of that text, at most 16777216: `output_max_bytes` alone keeps
a head of that many bytes, `output_head_bytes` with it splits head and tail, and
`output_head_bytes` alone keeps a tail of 16384 minus the head for a head under 4096,
else 12288 (the budget is the larger of 16384 and head + 12288); a head above the
budget is `invalid_range`. A result whose cut would not be shorter than its text
comes back whole. Native tools take these keys on the model's call and
never pass them to the tool itself; inside a program they change nothing.
`colleague`, `participant_read`, `participant_await`, `participant_join`,
`workflow_invoke` and `lua` list them in their schemas. A Lua-defined tool's
arguments are its own: its shape is `definition.shape = {output_max_bytes,
output_head_bytes}` from `tool_define`, the default otherwise.

`read_process_output` with `result_ref` (instead of `output_ref`) pages the
original's text with the same ranges, 8192 bytes per page from the range's start by
default, more only with an explicit `output_max_bytes` (at most 16777216);
`output_head_bytes` alone keeps a 6144-byte tail after it, as an `output_ref` read
does. `next_read` pages within the requested range only, so following the marker
stops where the tail the model holds begins. An empty `result_ref` is
`invalid_range`; a result with no text form has no pages and its read is refused as
`unsupported` with a message. The decoded text is kept for the turn, so paging does
not decode the original again. A value that is itself one result shaped in this turn,
passed through whole as another result or a continuation, is not cut again; its
settlement records `pass_through` with the inner result_ref. Mixed with other content
and over budget, it is cut normally, and the outer original holds the inner marker. Each shaped operation's settlement (for host observations, their
original's metadata) records the shape applied
(`max_bytes`, `head_bytes`, `tail_bytes`, `text_bytes`, and `whole` or `omitted`
with `result_ref`, `failed` for an error result, or `error`) and the model's
`call_id`; `native_defaults` still names the keys Blackbird supplied. A shaped
failure (an error key or a nonzero `exit_code` before shaping) also records
`failed: true` in its original's metadata; the `failed` selector finds that original
by the marker's result_ref with one indexed lookup, so a shaped failure still
matches and no history is scanned.

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
