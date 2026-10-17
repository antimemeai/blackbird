# Use Blackbird

From this repository, run `./scripts/blackbird` (the active installed generation,
or the checkout's release executable when no managed installation exists). In an interactive terminal it opens the
TUI. Piped input and `--once` use the plain interface; `--plain` selects it explicitly.
The default OpenAI route can use your existing Codex ChatGPT sign-in; Codex supplies
authentication only. Blackbird runs the requests, tools, context and workflows.
Owned accounts and explicit OpenRouter main coding are described in
[provider routing, prompt caching and usage](TOKENOMICS.md).

```sh
./scripts/blackbird
```

Without `--session`, each launch starts a fresh conversation in a private
`~/.local/state/blackbird/session-XXXXXX` directory and prints its path.
Use `--session DIRECTORY` to reopen a conversation or choose its location;
`--list-sessions` lists saved conversations and explicit resume commands.
Recovery, inspection, continuation and diagnostic expiry require `--session`.
The working directory is the tool working
directory. Only one process may hold a session. `--once 'prompt'` runs one turn.
`--provider ID` selects OpenAI, OpenRouter or a registered Responses route; switching
providers requires an explicit `--model NAME`. `--effort low|medium|high|xhigh|max` selects reasoning
(initial default `medium`). If that effort is unsupported by the chosen route,
the interactive harness opens and asks which supported level you intended.
No request runs until you answer; quitting preserves the unanswered question for
reopen. Use Up/Down and Enter to answer; Escape keeps the draft. The plain command
loop prints the question and accepts `/effort LEVEL`. One-shot/application calls
need a supported `--effort` because they have no later answering step.
Provider, model, effort and absolute workflow path persist with
the session; explicit launch options override and save them. Actor/conversation/
workflow identities also survive restart. Lua request options can override model
request defaults for a particular call. `/session` shows configuration and identities;
`/workflow FILE` validates and saves a different turn program.

Explicit `--recover-unknown-effects` can acknowledge abandoned supported native
file/command/Lua/workflow attempts after validating their linkage. It records
UNKNOWN without replay or old PID adoption. Opened commands keep a durable
custody warning and fence restart/backstop; a fresh independent session remains
available. See [reopen behavior](SESSION_REOPEN.md) for unsupported cases.

## Find and resume sessions

`./scripts/blackbird --list-sessions [ROOT]` lists direct child session directories
(default the selected session parent, Blackbird or legacy Arconaut) and ROOT itself if it is a session. `/sessions`
lists the current session's parent. Results have stable absolute paths, Unix-second
last activity from audit mtime (null with explicit unavailable status if stat fails),
saved provider/model/effort/workflow and a shell-quoted
explicit `./scripts/blackbird --session 'PATH'` resume command. Ordinary reopen does not
consume a pending RRC intent; use `--resume-continue` explicitly when appropriate.

Listing reads bounded `session-info.bbm` configuration snapshots and stats audits;
it never locks, opens for recovery, creates directories or changes session files.
Snapshots are derived from the owning process's audited settings, not authority.
Legacy sessions have unavailable configuration until normally opened; damaged or
missing snapshots remain listed with explicit status. Snapshot-save failures warn,
so configuration may be stale then; audit stays authoritative. A root name beginning
with `--` should be spelled `./--NAME` or as an absolute path. Listing is local,
nonrecursive, skips symlink children and rejects nonregular metadata files, and
does not maintain a global session catalog.

## Terminal interaction

The conversation, live activity/timing and composer stay visible. Assistant text
streams as it arrives; commands, file paths, process output, exit codes and tool
errors appear during work. Previous user/assistant context is shown on reopening.

Turn and active native tool timers are distinct (`Turn Ns · Tool Ns`); native
completion shows elapsed milliseconds after its retained terminal observation.
Turn end is labeled completed, cancelled or failed. A completed tool is not shown
as still running during later pure-Lua work. Cancellation still records uncertain
outcomes where appropriate; it never invents successful completion.

While PgUp-scrolled, new output keeps the same-width viewport anchored; `Up N`
shows distance from the live bottom. PgDn to bottom resumes following output.
Resize clamps the retained viewport and recomputes composer/cursor cell widths.
Approximately 1 MiB of display is retained even for giant no-newline output, with
an explicit trim notice; full originals remain audited and A2 retrieval still works.

- Enter sends; Alt-Enter (or Ctrl-J) adds a line. Clipboard paste stays multiline
  until you send it. Left/right, Home/End, Backspace/Delete and Ctrl-A/E/U edit.
- Up/down recall persisted submitted-prompt history. PgUp/PgDn scroll the view.
- Ctrl-\ opens the local view picker; `/views` opens the same picker. Choose
  Conversation, Command runs, Command center or Result comparison to switch
  immediately while preserving the draft, cursor and each view's reading state.
  Search/arrow keys belong to the picker; Escape returns to the invoking focus.
- Ctrl-O toggles the command-results inspector without editing the draft or its
  cursor. In inspection focus, arrows/Home/End/PgUp/PgDn navigate; Tab changes
  collection/detail focus. Escape or typing returns to editing. Ctrl-Left/Right
  pages the focused command-run list or retained output. Pasted controls stay literal.
- Type and submit during a turn to queue the next prompt. Commands queue too, so
  configuration changes apply after the current turn. `/jobs` and `/bg` are immediate
  local command controls, including while inference is busy.
- Ctrl-Q exits while preserving draft and queued work as recoverable drafts.
- Ctrl-C stops the active turn and clears the queue, preserving the draft you are
  editing. Ctrl-C also preserves the draft when idle; Ctrl-U clears it. `/quit`, `/exit` or Ctrl-D on an empty draft
  exits; an active turn is stopped first. Terminal settings restore on exit.

Commands: `/help`, `/model NAME`, `/effort LEVEL`, `/context`, `/stats`, `/originals`,
`/restore ENTRY`, `/lua CODE`, `/clear` (view only), `/drafts`, `/draft N`, `/quit`, `/exit`, `/session`, `/sessions`, `/workflow FILE`, `/restart NOTE`. Unknown slash commands report a local error without a model request.
In plain mode `/paste` collects a block until `/send`.
`--audit-last` inspects recent original records without a provider request.

A command run is one execution with process state, output and controls. Command
runs have stable IDs (called `job_id` in the existing tool protocol). `/exec {"command":"sleep 30","yield_ms":0}`
starts one command and releases its wait immediately. In the TUI, `/jobs` opens
the resident command-results inspector; plain mode keeps its structured list.
The inspector identifies actual retained/received ranges and keeps its last good
page visibly stale when the owner or recording capacity is unavailable.
The authored Result comparison view also pages its command-run list in groups of
up to eight: focus the list with Left, then use Ctrl-Left/Right. With detail focus,
those controls page the selected run's output for the next pin. F6/F7 pin A/B;
changing list pages leaves those snapshots intact. This page size does not limit
command execution. The view reads recent resident runs; it is not an archive browser.
`/fg ID` waits on that same command; `/bg` or Ctrl-B releases the foreground wait.
Backgrounding keeps the process and collector running. Output and the original
exec attempt retain their identity across later turns. `/process` accepts a JSON
object with `job_id` and an `op`: `read`, `foreground`, `background`, `input`,
`resize`, `signal`, `stop`, or `archive`. Model tools and `blackbird.process` Lua
helpers use the same controls. Input, resize and signal requests carry a stable
`request_id`; repeating a request returns its receipt without repeating its effect.

Use `"pty":true` for interactive text commands. While attached, keyboard input,
Ctrl-C and resize go to that private terminal; Ctrl-B restores the composer without
stopping the command. Split UTF-8 and bracketed paste retain exact input bytes.
Output is escaped for display and retained exactly in originals; this interface
does not emulate a full-screen terminal. Bounded input pressure reports refused
bytes and keeps detachment available. `stop` performs owned cleanup. Deadlines
remain relative to launch, across every background/foreground cycle. Actual exit,
output closure and capture completeness are reported separately. Active or
unsettled commands block restart and session switching. Quitting stops and joins
owned jobs; reopening after a crash preserves unknown custody and never replays
an old command or adopts a saved PID. See [the design](COMMAND_JOBS.md) for limits.

This first TUI uses ANSI terminals with bracketed paste and at least 12 columns
by 10 rows. Editing uses UTF-8 codepoints; terminal wcwidth determines wrapping.
Complex emoji/grapheme editing is later work. Drafts are limited to 1 MiB, screen
history to approximately 1 MiB and prompt history to 128 entries. These presentation
limits do not trim retained context or original audit bytes. Queued prompts remain
UI drafts until their turn begins. Composer, cursor and submitted-prompt history
persist in replaceable `ui-state.bbm` within the private locked session directory;
this is UI state, not provider context. History and queued/recovered draft shelves
also have aggregate 1 MiB limits and at most 128 entries. On reopen, prior queued
prompts become **recovered drafts**, never runnable work. `/drafts` lists them;
`/draft N` loads one into the empty composer, and a separate Enter submits it.
Use Ctrl-U first if retaining a current draft elsewhere is intended. Automatic RRC
continuation runs first; restored drafts stay available behind it. A prompt is
removed from pending UI state before dispatch, so it cannot be replayed on reopen.
Invalid/unreadable UI state reports a warning without repairing audit. Atomic
replacement preserves the preceding saved state on write failure; a visible warning
keeps unsaved drafts in RAM and prevents new work dispatch until a save succeeds.
Recovered cursor offsets inside UTF-8 continuation bytes move back to a boundary.
No power-loss
or arbitrary-crash exact-keystroke durability is claimed.

## Opt-in cooperative backstop

`--backstop MISSION_JSON_FILE --once 'PROMPT'` supplies a small author-declared
mission packet for recovery after that workflow fails. The file contains an object
with a nonempty `mission` string; optional fields retain steer/resource observations.
`--workflow FILE` can select the failed source workflow. This is same-lifetime native
custody, not authority inferred from a parent exit, old lease, or reopened session.
Explicit cancellation stays paused; arbitrary exec makes containment unavailable.
The fresh `SESSION/backstop/audit` preserves declared lineage and old unknowns,
without inheriting attempt admissions. Two independent assessment/pivot attempts and
a two-minute deadline bound recovery. Selected actions use native file/context APIs,
not exec/restart or additional provider calls. Old-session mutations (including
aliases) are refused. Success observes a declared changed file artifact; it does not
settle remote effects or certify semantic usefulness. See
[BACKSTOP_OPERATING](BACKSTOP_OPERATING.md) for examples and limits.

## Lua programs and context

`programs/turn.lua` runs the request/tool loop. Edit it to change the next turn or
select another with `--workflow FILE`. Each turn reads and retains its source and
creates a fresh Lua VM. Globals are ephemeral; use context, files or services for
persistent state. The model has a `lua` tool and the same APIs:

- `blackbird.request(options)` assembles from live context, with optional model,
  instructions, input, tools, reasoning and other overrides. This transport
  currently requires `store=false` and `stream=true`.
- `blackbird.call(name, arguments)` runs a tool with native arguments. Decode external JSON explicitly at its boundary.
  `read_file`, `write_file`, `edit_file` access files. `exec` accepts `command` or
  `argv` and a timeout in seconds. Nonempty argv takes precedence; empty argv uses
  the command. File/process effects run without command-approval prompts.
  The default coding workflow supplies a 16384-byte observation limit for `exec`
  and un-ranged `read_file` calls unless `output_max_bytes` is explicit. An
  explicit file range bypasses that workflow default. This bounds returned raw
  bytes, not tokens, capture, memory or external encoding. Custom Lua callers keep
  full-read defaults; zero or larger explicit limits are accepted.
  `read_file` accepts one optional `range` object: `{mode="lines", start=1,
  ["end"]=20}` (one-based inclusive) or `{mode="bytes", start=0, ["end"]=256}`
  (zero-based half-open). Omit `range` for the whole file. Only one mode is possible;
  unknown range keys and mixed selectors fail with correction guidance. Historical
  Lua calls using one `line_start`/`line_end` or `byte_start`/`byte_end` pair remain
  supported, but those fields are absent from the advertised model schema.
  Missing start means first byte/line; missing end means EOF. Beyond-EOF ranges
  clip or return empty; reversed, negative, noninteger ranges and line zero fail.
  LF is retained, CR is ordinary data, and a trailing LF creates no extra line.
  Full original file bytes remain audited; invalid UTF-8 slices return hex.
  Reads still have the existing 16 MiB full-file capacity, even for small ranges.
  A native session read returns `snapshot={journal=...,sequence=...}`. For repair,
  call `read_file` with that `snapshot` instead of `path`, plus the desired range.
  It reads the exact captured original after file changes/deletion and session
  reopen; it never opens the current path or scans the transcript. Exactly one of
  path/snapshot is required. Standalone LocalTools has no snapshot store.
  Optional `output_max_bytes` limits a file's selected range. `file_bytes` is the
  full original size, `selected_bytes` the clipped range size, `returned_bytes`
  the returned prefix size, and `omitted_bytes` the selected bytes not returned.
  These four fields appear when a limit is supplied. Original capture is complete.
  `exec` accepts `output_max_bytes` to bound the raw prefix bytes in its
  model-facing result (JSON encoding overhead is not included). With a budget,
  `output_bytes` and `omitted_bytes` describe the full and omitted counts. Normal
  `exec` results include `output_ref`; use
  `blackbird.call("read_process_output", {output_ref=REF, byte_start=N, byte_end=M})`
  to retrieve original combined stdout/stderr from retained audit without replay.
  Ranges use the same byte semantics as file reads, and survive reopen. Original
  process chunks and terminal streaming are not clipped by this model budget.
  Timeout/cancellation still interrupt the turn; partial chunks and their attempt
  references remain in audit for explicit later inspection/retrieval. The existing
  16 MiB process collection capacity remains; this is not a memory/stream budget.
- `blackbird.context()` returns `{base, entries}`; each entry has stable `id` and `item`.
  Transform the presentation and use `blackbird.edit(candidate)`. Stale bases and invalid
  IDs produce retained rejections. Edits/removals preserve originals.
- `blackbird.stats()` (also `/stats` and `context_stats`) reports entry count,
  per-entry item bytes, serialized input-array bytes and full view bytes. All are
  native binary packet byte counts, **not tokens**. `last_request_bytes` measures the
  native packet size after request overrides; authentication headers are not
  part of it. Request size is shown before provider transport; actual returned
  token usage is shown after response. `usage` contains only numeric input/output/
  total counts and cached/reasoning details, never arbitrary extension strings.
  Last request/usage are scoped to this process; null means unavailable. No token
  estimates or model-limit guesses are made.
- `blackbird.originals()` lists originals; `blackbird.restore(id)` repairs an item.
  `blackbird.append(items)` adds synthetic items with fresh original IDs.
- `blackbird.json.decode/encode` import/export JSON at external boundaries; native values preserve null, arrays and exact numeric values.
  Use `blackbird.binary.encode/decode` for BBM2 persistence, and `blackbird.format` for text.
  Use `blackbird.array({})` for a new empty array.
- `blackbird.present(item)` displays a completed assistant message without duplicating
  matching streamed text. `blackbird.display(text)` and `print(...)` retain explicit
  display output. Streaming preview is derived from already-retained raw bytes.

Base/coroutine/table/string/math/utf8 Lua facilities are available. Host I/O goes
through `blackbird.call`; raw io/os/package/debug and file loaders are absent. Evaluate
modules with `load` on audited `read_file` content. This keeps effects in the audit;
it is not a hostile-code sandbox or an approval system.

Preserve Responses call/result linkage when editing context. Opaque items stay
originals even after presentation edits. Final requests, raw provider streams,
input/results, file prior/proposed bytes, process chunks, context revisions and
program source are retained. Credentials are excluded from the audit interface.
If final text differs from the preview, the UI marks the correction and shows
the authoritative final text. Deltas without item IDs wait for the final item.
Incomplete streamed calls never dispatch. Ctrl-C closes interrupted effects with an
unknown outcome and supplies interruption results for pending calls so later turns
can continue. It never automatically repeats a side effect. Pure Lua loops stop
through the instruction hook too.

## Live Lua-defined tools

`tool_define` (or `blackbird.define_tool(definition)`) stages a session tool schema and
Lua function body; `tool_registry` inspects effective versus pending definitions.
Successful workflow boundaries publish them. Later default requests expose the
tools and `blackbird.call` dispatches them through retained operation/native-effect
paths. Invalid definitions or failed workflows preserve effective configuration.
See [LUA_TOOLS](LUA_TOOLS.md) for the supported scalar-object schema, exact bounds,
source/body example, persistence and unknown-effect limits.

## Callable workflows and decision models

[Callable workflows](CALLABLE_WORKFLOWS.md) register retained Lua definitions,
configurable slash aliases/prefix, bare invocations and colored POWERWORDS.
`/ultracode TASK` is available by default in new sessions. Inspect the effective
registry with `/lua return blackbird.call('workflow_registry', {})`.

[Native decision models](DECISION_MODELS.md) expose Jev through `decision_model`,
Lua `blackbird.decide(...)`, and `/decision JSON`. The launcher lazily locates the
workspace key file; direct binary installs use environment credentials or
BLACKBIRD_JEV_ENV_FILE. No Python runtime or startup credential read is involved.

## Restart/resume/continue (RRC)

Build `cmake --build build/release --target blackbird`, then use `/restart NOTE` or let
the model call `restart` with `{note="changes, checks, next steps"}`. Launch through
`./scripts/blackbird`: it waits for exit code 75, then starts the replacement executable
with the same session. Settings, identities and context survive; the new process
injects `continue` with the note and runs the saved workflow. The original prompt
is not submitted again. `ARCO_EXECUTABLE` selects a different executable when needed.

A model restart request is deferred until the current tool batch and turn finish.
`blackbird.restarting()` lets custom Lua programs return at that boundary; another provider
request after scheduling restart is refused. Failed/interrupted turns cancel their
restart request. Pending call/result linkage or unfinished admitted effects blocks
restart. There are no current standing programs to pause; consumed services remain
outside this process's lifecycle.

After a launcher failure, reopen with `./scripts/blackbird --session DIRECTORY
--resume-continue` to consume a pending intent. Ordinary reopen does not run it.
`--resume-once` runs the continuation and exits, useful for controlled exercises.
Injection is deduplicated across the append/consume crash gap. The intent is
consumed before requesting a provider: a subsequent crash or failed request requires
explicit operator continuation rather than an automatic retry. Terminal drafts and history survive RRC; queued prompts restore only as drafts
requiring explicit submission after continuation, never automatic execution.
Native hot replacement and outposts remain separate later capabilities.

### Explicit failed-workflow linkage repair (B4)

A custom Lua error after a call is appended but before its result can leave
context linkage incomplete. The next provider request refuses that presentation;
ordinary failure does not automatically repair it or replay effects. Interruption
and capacity stops already supply their native stop placeholders.

After inspecting the retained failed program, operation results and unknowns, use
an explicit recovery workflow:

```text
/lua local r=blackbird.call('context_repair',{base=blackbird.context().base}); print(blackbird.json.encode(r))
/workflow programs/turn.lua
<your next useful request, with instructions not to replay uncertain effects>
```

`context_repair` requires the current context `base` (strict CAS). It adds one
`workflow_result_missing` placeholder per still-unanswered call that existed at
this workflow's start, with `effect_outcome: unknown` and `replayed: false`.
Even a tool that completed locally can have a missing presentation result; the
placeholder does not replace its retained actual result or establish its outcome.
Originals, existing outputs and source/audit paths remain intact. Repeating repair
on complete context adds nothing. Duplicate calls, orphan outputs, stale base,
new calls introduced by the active workflow and live owned native children are
refused. Repair is audited through ordinary native operation/context records.

This is **presentation repair**, not backstop recovery, effect reconciliation,
process containment or a new attempt authority. General exec may have escaped
children; remote effects may remain unknown. No native lifetime reset, automatic
request, retry, restart or unpause occurs. Explicit cancellation stays paused until
the operator submits work. Reopen still refuses unresolved non-provider admissions;
this tool does not bypass that recovery gate. A new explicit workflow can use
this repair path on retained incomplete context after an ordinary allowed reopen.

On reopening, unfinished provider requests are recorded as unknown without replay
or acceptance of late response data. Context and raw streams remain retained.
Unfinished command/file operations still block ordinary resumption; keep their audit
for diagnosis. Full recovery/custody remains unfinished. Multi-model concurrency, external complaint
storage and replacing the Codex auth scaffold are also subsequent work. Scoped
Mac/Linux persistence and RRC checks pass; broader integrated qualification remains
pending. The earlier vertical-slice demonstration
is mechanism evidence; readiness for daily self-development depends on operator use.

### Explicit managed compaction (B1)

`/compact JSON` accepts `{base,mode,ids,reason,source}`; mode is `archive`, `select`,
`summarize` or `restore`. Summary also requires `summary:{role:"assistant",content:...}`.
Use `/context` for entry IDs/revision. Complete call/result intervals are atomic;
all live user/system/developer instructions are mandatory. Restore merges exact
originals in capture order and repairs selected edited/reordered presentations.
Archive only changes presentation. Summaries have fresh IDs and source ancestry;
structural acceptance cannot establish fact accuracy. No automatic thresholds.

Lua uses `blackbird.manage(proposal)` and `blackbird.inspect(query)`. Model tools are
`context_manage {proposal}` and `context_inspect {query}`. A supplied base is strict
CAS. Model tool proposals may omit base to explicitly bind the invocation snapshot
(the concrete resolved proposal is audited), avoiding the provider-call append gap.
Managed requests during a workflow stage until successful completion, retaining the
current tool exchange. Failed/interrupted workflows cancel. One pending request;
o overlapping last-writer-wins. Generic CLM edit/append remain available.

`/inspect {"kind":"index"}` returns bounded hex-encoded BBM2 stream bytes. Add `"format":"json"` for a JSON export.
Kinds: `index`, `history`, `originals`; originals may specify `entry`. Optional
`offset` and `limit` (default4096, max65536) page exact source bytes without dumping
old history into a request. Decode hex locally; ranges may split UTF-8 codepoints.
Audit/originals grow even when presentation shrinks. No automatic effect replay.

A tool deadline returns `timed_out:true`, `effect_outcome:"unknown"` and an
`output_ref` for retained partial output. The workflow continues so the model can
inspect that output and choose a new attempt with a larger timeout, split the work,
or take another approach. Blackbird closes/reaps the local child group before returning;
already-performed external effects remain possible and are not replayed automatically.
Operator cancellation still stops the turn. Provider transport failures remain
separate from this tool-result continuation.

## Local audit explorer and advisory complaints

Model tools `audit_inspect({query:...})` and `rageshake({...})` are also available
from Lua as `blackbird.call("audit_inspect", {query=...})` and
`blackbird.call("rageshake", {...})`. No command approvals or immediate repair obligation.

Start with `{cursor:0,count:32}`; retain returned `end` for later pages, advance
`cursor` to returned `next`. Each row names its committed fact index, physical
journal/sequence, causal IDs and up to64 source dependency references. Kind numbers
are RetainedKind in include/blackbird/retained_events.hpp. Decision → invocation →
attempt IDs link operation input/context/program to observed disposition; admission
is not success. Application channel1 captures original labels/metadata, channel2
context revisions, channel3 program activation. Separate occurrences stay separate.

For exact retained bytes use `{record:INDEX,offset:0,limit:4096}` for event payload,
or add `source:0` to read that dependency's immutable original. Returned hex can
split UTF8/binary arbitrarily; concatenate/decode bytes, never rerun the effect.
`total_bytes`/`next` expose EOF. Count1..64; byte limit1..65536. Metadata is small
and selected; context packets are not serialized by index listing. Detail source
reads may copy one existing document (ordinary document bound); there is no global
full-history reconstruction or second output store. Prefix `end` remains usable
while new facts append. Fact indices are local to this journal lineage, not portable
between unrelated sessions. Only committed facts appear; provisional/crash evidence
is not silently asserted to have settled.

Complaint example:

```lua
local result = blackbird.call("rageshake", {
  observation="Inspection required a repeated full-history scan",
  references={attempt="EXACT_ATTEMPT", record=123, hypothesis="optional suspicion"}
})
return result
```

Observation/references are model assertions, not a correctness verdict. The native
capture adds context revision, effective program generation, model/effort,
actor/conversation/workflow, reporting attempt and audit watermark. `complaint.detail`
is retained before bead delivery; `complaint.delivery` records the result. Beads
receives only constant advisory text and local identity locators, not private raw
provider history or the observation. Delivery failure/timeout remains local; inspect
the actual exec output reference before any explicit new delivery attempt. Never
automatically replay an unknown side effect. An external DB sink is unconfigured;
a separately configured consumer can inspect these records without Blackbird provisioning
or governing a database. Keep raw audit/context ignored, not in commits or pushes.

### Explicit capacity successor (declared lineage)

`blackbird --session NEW_DIRECTORY --seed-session SEED.json --plain` initializes a
fresh audit with selected context; `/quit` exits without a provider request.
Do not combine seeding with inspection, discovery or restart-resume switches.
Existing audits are refused, not overwritten. Input is bounded to1MiB/4096 entries.
Example shape (replace IDs/observations with actual source locators):

```json
{"version":1,"source":{"session":"/absolute/source/session",
 "environment":"11000000000000000000000000000000",
 "journal":"22000000000000000000000000000000",
 "prefix_sequence":17,"prefix_end":8192,
 "context_revision":"33000000000000000000000000000000",
 "status":"unsettled","reason":"explicit capacity successor; no settlement established"},
 "entries":[{"id":"33000000000000000000000000000000.0",
 "item":{"role":"developer","content":"Retain current instructions and working state"}}]}
```

The author selects context and is responsible for including governing instructions
and honest source observations/unknowns. Native validation checks manifest shapes,
current text-message/self-contained encrypted-reasoning/tool-pair schemas, complete
ordered call/output pairs and unique original-entry locators. Non-text modalities
are not supported by this seed schema. Seeding does not read or verify the source,
claim settlement, dispatch imported calls, or inherit admissions. New session and
entry identities are fresh. One context packet atomically retains new originals
and declared lineage; its `lineage.source_entries` order maps to new `originals`.
Use bounded context history inspection to recover that packet. Old paths/journals/
entry IDs remain old locators; original bytes have not been copied wholesale or
moved. A failed write can leave allocator reservation facts but cannot publish
selected context without lineage. Inspect such a destination; do not silently retry
seeding into it. Automatic handoff and bounded old-entry resolution without full
parent replay remain separately queued; explicit declared continuation is supported.

### Bounded workflow capacity stop

Each coding turn protects byte/record credits for bounded diagnostics, outstanding
nested attempts (maximum depth16), pending managed-proposal cancellation, and call
linkage. Before context/staging publication, the prospective stop packets must fit
the unchanged physical payload/batch limits as well as remaining journal capacity.
Oversized obligations are refused rather than raising caps. A capacity refusal is
sticky for the turn, including Lua `pcall`: no fresh effects or provider retries.

Provider/process capture refusal stops the live adapter/child, retains earlier
committed fragments, records unknown effect outcome and received-but-unretained byte
counts, cancels pending management, and closes open calls with stop outputs. Bounded
Lua error capture reports omitted bytes. This is not a promise that rejected bytes
were retained, that no dispatched effect occurred, or that physical write/sync
failures can be settled in an unavailable audit. Success outputs remain ordinary
bounded writes; post-dispatch output overflow uses a bounded unknown terminal.
Context compaction does not reclaim physical audit bytes. Act on early headroom
warnings and explicitly select a successor; no automatic physical rollover exists.

### Retained named modules and request configuration

`program_config({})` inspects effective and pending program snapshots/revisions,
resolved request defaults and launch fallbacks, tool registry, context policy, and
the current governing workflow generation (its source is retained as
`program.source`). `program_config({proposal=..., base=REV})` stages a **complete**
snapshot; omitted base binds the current effective revision. Example in Lua:

```lua
local view = blackbird.call("program_config", {})
local source = blackbird.call("read_file", {path="programs/maintenance.lua"}).content
local pending = blackbird.call("program_config", {
  base=view.revision,
  proposal={modules=blackbird.array({{name="maintenance", source=source}}),
            model="", effort=""}
})
assert(pending.staged)
-- This workflow still sees the old snapshot. Import on a later workflow:
-- local maintenance = blackbird.module("maintenance")
```

Empty model/effort use current launch defaults; nonempty defaults affect later
requests after publication. Effort accepts low/medium/high/xhigh/max. Request-local
`blackbird.request` options still override these defaults. Model strings do **not**
select a different provider/adapter, authorize dependencies, or establish provider
capability; unsupported models fail through the existing adapter.

A candidate contains at most32 uniquely named modules, each at most16KiB source,
and at most64KiB serialized configuration. Text-only syntax validation executes
no candidate top-level code. Publication shares the durable successful-workflow
boundary with context/tool/policy changes; workflow failure, cancellation or failed
settlement preserves the effective snapshot and discards pending changes. Invalid
proposals do not replace a prior valid pending proposal. Complete proposals can
remove modules; retained history is not erased. Reopen restores published sources.

`blackbird.module(name)` obtains source through the audited `module_source` tool,
evaluates it on first import in that workflow, and caches its non-nil returned
value. Late imports resolve the same effective snapshot, never pending/live-file
bytes. Each module has a private assignment environment inheriting the ordinary
Lua globals; shared tables and `_G` are **not hostile-code isolation**. Cycles
fail; failed evaluation clears its loading marker. Evaluation can perform ordinary
audited effects: an error does not roll them back and an explicit retry can repeat
them. Use declarative top levels and put effects in returned functions.

This slice does not stage a governing workflow selector or implement an atomic
interrupt-and-apply UI. `--workflow`/`/workflow` still select the file read at turn
admission. Cancel stays cancelled; it does not publish pending configuration.
Native code still activates by build/restart/resume, not by module publication.

### Bounded synchronous orchestration (G6)

`programs/orchestration.lua` supplies a reusable named-module source. Publish it
through a complete `program_config` proposal, preserving other desired modules,
then use `blackbird.module("orchestration")` after the successful boundary. There is no
auto-installed module or inherited participant authority in a fresh session.

`new{owner=...,max_participants=N,max_attempts=M}` creates one workflow-local
synchronous scope. `run(id,callback)`, boolean `branch`, explicit `retry` and selected
`join{ids...}` compose ordinary native tool/colleague calls. IDs are unique per
scope, results/history are copied, and finite positive limits count participants
and callback attempts. Callbacks must bound their own tool/request counts and
native timeouts. A callback reports `status` and `remote_disposition`; these are
caller assertions grounded in actual native results, not containment evidence.
Exceptions/malformed results remain unknown. Only explicit `refused` plus
`not_dispatched` permits retry; there is no automatic retry/reconciliation/replay.

`pause(reason)` prevents subsequent callback dispatch and has no resume operation.
It does not interrupt an active native call or establish remote cancellation;
use the owning runtime's existing native cancellation paths for in-flight work.
Join is a snapshot, not a wait: all selected rows must be completed or it reports
unsettled. No parallel workers, detached children, persistent admissions, native
participant messaging or new provider pairing restrictions are introduced.

See [G6_ORCHESTRATION_MAP](G6_ORCHESTRATION_MAP.md) for API example and actual
context-only colleague usage, and [G6_NATIVE_OWNERSHIP_MAP](G6_NATIVE_OWNERSHIP_MAP.md)
for current stop/cancellation anchors. Direct checks: `lua tests/orchestration_test.lua`
with Lua5.4.8. G5's independent colleague candidate is not automatically integrated
by importing this module; select an actually configured adapter/call path explicitly.

### Operator executable during concurrent development

The checkout's `scripts/blackbird` follows the managed `~/.local/bin/blackbird`
installation when present. Without one, it uses `build/release/blackbird`.
`BLACKBIRD_EXECUTABLE=/absolute/path` selects a development executable explicitly;
the older `ARCO_EXECUTABLE` name also works. Interactive and unattended launches
follow the same selection. An old `blackbird-ui` sibling is never selected.

Run `./scripts/install-blackbird --update` to build and activate checked source.
A running installed harness, including `/restart`, stays within its selected
generation; a new invocation uses the update. Native self-development can select
a checked executable explicitly instead. Startup reports the session before
reopening history. See [installation](INSTALLING.md) for packaged workflow paths.

## Opt-in native station

`--station LOCAL_ADAPTER_DIRECTORY` admits identified file source events into the
same native participant/context/Lua workflow. Idle needs no inference. Inspect
`SESSION/station-status.bbm`; atomically replace source `events.json` and boundary
`control.json` for pause/resume/steer/stop. Repeated or unknown source/id never
redispatches. `workflow_returned` is not effect success.
See [Station operating interface](STATION.md) for exact bounds, RRC selection and
source-owned backlog semantics. No live TUI attach or crash containment claim.

Provider transport diagnostics expire after **30 days**. New blocks live in private
`diagnostics/` files beside the session audit; the audit stores small block references,
not another raw stream copy. Live display is immediate. Diagnostics are optional:
a process crash or storage error can lose them without replaying a request.
`audit_inspect` with `record` and `diagnostic:true` reads an unexpired block (the
usual offset/limit bounds apply); an expired/missing block returns `stale_handle`.
Expiration uses wall-clock Unix seconds, including its behavior after clock changes.

Reopen and new capture perform bounded expiry passes. For an idle session, run
`scripts/blackbird --session DIR --expire-diagnostics`; it reports removed files
and whether scanning completed. Repeat if incomplete. No background daemon is
installed. Legacy stream bytes already embedded in an older finite audit remain
there; this change does not rewrite those journals. New diagnostics no longer grow
that journal by raw transport volume.

`/checkpoint` explicitly saves current session state and retries failed maintenance.
`/stats` exposes the maintenance error and resident tail records. Compact sessions
attempt maintenance at128 resident records or8MiB of suffix; failed automatic
retries wait64 sequence steps. New admissions stop before the1024-record/32MiB
resident-tail budget, including reserved settlement room. Already-admitted work can
settle. Fix storage and retry `/checkpoint`; a failed derived publication does not
undo accepted context. The full journal's existing512MiB/200000-record capacity
remains independent. See [session reopen](SESSION_REOPEN.md) for fallback behavior.

### Choosing saved sessions

`/name NAME` retains a short session name (128 UTF-8 bytes); `/name` clears it.
`/sessions` opens a searchable picker in the TUI, or lists discovery metadata in
plain mode. Arrows choose, Enter loads `/resume DIRECTORY`, then Enter switches.
A draft displaced by selection is kept in `/drafts`. You can type `/resume
DIRECTORY` directly, including paths with spaces. It waits for current work to
conclude and does not send a model request. Destination model/effort/workflow,
context, tasks and drafts are restored independently. A failed open returns once
to the old session; uncertain operations are never silently retried. `/new` still
creates a fresh session and inherits only model, effort and workflow.

### Shared colleague requests

`/colleagues` discovers installed OpenAI/Codex and Claude transports. It makes no
account probe; installed does not mean authenticated or available. `/colleague
JSON`, model tool `colleague`, and `blackbird.colleague(request)` share one request:
`request_id`, `from`, `to`, `provider` (`openai` or `claude`), `model`, `task`,
`context` (explicit `{id,text}` entries), and `profile` (`name`, `provenance`,
`timeout_seconds`, `tools:"none"`, `requests:1`). Selected requests are bounded to
64 KiB. Inspect `status`, `remote_disposition`, actual model and usage separately.
Calls have no tools, hidden transcript, continuation or automatic retries. Local
interruption does not establish that remote work stopped. Admission, provider
bytes and results are retained with the actual operation attempt.

### Concurrent participants and direction

`/run {"request":COLLEAGUE_REQUEST,"task_id":"t1"}` starts a participant. Use
`/runs` for concise observed state, `/runs ID` for its bounded result, `/send
{"run_id":"ID","message_id":"direction-1","from":"operator","text":"..."}`
for direction, `/stop ID` to request cancellation, and `/join {"run_ids":["ID"]}`
to seal message admission, finish accepted directions and return in that order.
Tools `participant_start/read/send/cancel/await/join/archive/configure` and Lua
`blackbird.participants.start/read/send/cancel/await/join/archive/configure` share those
controls. Start's success means admission, not a completed remote request.

A first reply leaves the participant `waiting` for direction. Each accepted
message creates an explicit next request from the original selection plus the
prior answer available at acceptance. It cannot rewrite an in-flight request.
Each ID deduplicates exact repeats while this process lives; conflicting reuse is
rejected. Queued directions that cancellation/failure prevents are dropped,
without dispatch. Join stops accepting new directions. Interrupting a join stops
waiting; it does not cancel participants.

`/runs configure {"concurrency":4}` changes capacity while all runs are settled
(default2, range1..16). At most32 current runs; `/runs archive ID` frees a settled
slot while originals remain. Each inbox allows16 pending/64 lifetime messages,
2048-byte direction text. Selected requests stay64KiB. Workers buffer at most8MiB
and1024 capture packets for owner retention; pressure fails visibly, never grows
without bound. Current results are bounded projections; detailed originals remain
inspectable through `audit_inspect`. No diagnostic TTL is imposed on task/results.

Task badges show observed running/waiting/settled states. They never mark a task
done. `/new`, `/resume` and `/restart` require participants settled first. Exit
requests local cancellation and waits for worker return; it does not govern shared
services or establish remote cancellation. On process reopen unfinished runs are
`unknown`, with no worker resurrection, direction replay or automatic redispatch.

`/await {"run_id":"ID","timeout_ms":30000}` waits for accepted work without
closing the participant to more direction. A timeout or interrupted await is
separate from cancellation and does not stop the participant.

Reopening also settles an interrupted colleague/participant **control admission**
as unknown without replay. This covers a crash before its scheduling/result record
was completed; it does not establish that remote work stopped. Unresolved general
command/file custody keeps its existing refusal.

## Owned provider authentication

Owned provider authentication is integrated: `./scripts/blackbird auth` (or
`build/release/blackbird-auth`) is a separate operator interface. Use it outside
chat: keys and callback codes do not enter session prompts or operation inputs.

```sh
./scripts/blackbird auth providers
./scripts/blackbird auth status
./scripts/blackbird auth login kimi personal
./scripts/blackbird auth login grok personal
./scripts/blackbird auth login openai personal
./scripts/blackbird auth login anthropic personal
./scripts/blackbird auth key mimo work
./scripts/blackbird auth use openai personal
./scripts/blackbird auth logout openai personal
```

Kimi/Grok login prints a verification URL and code, then polls using the
provider's interval. OpenAI prints a browser URL and receives its callback on a
loopback listener; its signed ID token, issuer, audience, expiry, nonce and plan
scopes are checked before storing credentials. Reauthorization reuses the issued
client/account registration. Anthropic uses browser PKCE and a pasted code#state
with hidden input, following the older OpenCode/Pi flow. These flows do not read
Codex/Claude/Kimi credential caches. Browser consent is performed by the operator.
Provider endpoint/client behavior can change; deterministic checks are not live
subscription validation. No switch to separately billed access is automatic.

`key` reads hidden terminal input or stdin, never a command-line key argument.
This includes MiMo's dedicated Token Plan key or API key as selected by its issuer.
Do not paste a key into `/auth` or a model conversation. In chat `/auth`,
`/auth status`, `/auth providers` and model tool `provider_auth_status` expose
only labels, credential kind and observed local expiry/state, without a network
probe or refresh. `blackbird.call("provider_auth_status", "{}")` is the Lua path.

Owned credentials live in `~/.local/share/blackbird/auth/credentials.bbm`
(owner-only0600, directory0700); `BLACKBIRD_AUTH_HOME` selects an independent store.
Writes replace the complete record atomically. Thread/process locking serializes
rotating refresh, and login commits check account/selection revisions. Unknown
refresh outcomes stop automatic reuse and require reauthorization; transient
refresh failures keep credentials and enforce a cooldown. Local logout deletes
tokens while retaining the registration; remote revocation is not yet implemented.
An explicit logged-out/failed owned selection blocks environment/API fallback.
Without an owned selection, new direct providers can use their environment key.
OpenAI keeps the existing Codex path until an owned account/key is explicitly
selected; an ambient OPENAI_API_KEY does not silently change its billing route.

Registry IDs: kimi, mimo, openai, anthropic, grok, moonshot, openrouter. For other
services, configure an explicit HTTPS origin and one of the three wire formats:

```sh
./scripts/blackbird auth add-provider my-provider https://example.com/v1 chat MY_PROVIDER_KEY
./scripts/blackbird auth key my-provider work
```

Custom IDs cannot replace builtin origins. Supported protocols are `responses`,
`responses-portable`, `chat` (chat/completions), and `messages` (Anthropic-style).
The two Responses protocols support the main coding loop. Built-in `anthropic`
also supports it through its native Messages adapter; other registered Messages
providers remain context-only colleague routes. See
[Paper Instruments setup](TOKENOMICS.md#paper-instruments-and-registered-responses).
Credentials are sent
through curl's private stdin configuration, not its argv; no redirects or HTTP
downgrade. Installed `/usr/bin/openssl` supplies PKCE SHA256 and RSA signature
verification; no provider SDK/auth library is linked.

`/colleague` and participants accept these registry IDs and use direct transports.
Admissions retain protocol/origin/account/revision, never token material; an
account change before dispatch refuses that admission. Requested/observed models
and provider usage remain separate. HTTP401/403,429 and transport timeout have
distinct outcomes. The main coding loop supports OpenAI, explicit OpenRouter
Responses, registered Responses routes and built-in Anthropic Messages.
OpenAI can use its owned ChatGPT-plan login or key; existing Codex
authentication remains the compatibility path when neither is selected.
Provider `claude` remains the explicit Claude CLI
bridge, while `anthropic` uses owned direct authentication. Both support
context-only colleagues and participants. The `anthropic` route also supports
the main coding loop described below. Owned Anthropic OAuth has deterministic
login/refresh checks; the live token-exchange result is tracked separately from
those checks. See the [auth inventory](../papers/claude-auth-integration-2026-10-17/SOURCE_INVENTORY.md)
and [current primary-source notes](../papers/claude-auth-integration-2026-10-17/PRIMARY_SOURCES.md).
Source integration does not activate a running session or install a release.

### Native Claude main provider

Native Messages keeps Blackbird's own tool loop. Anthropic documents
[API authentication](https://platform.claude.com/docs/en/manage-claude/authentication)
for direct clients; its subscription `/login` and `setup-token` instructions apply
to Claude Code. The [Agent SDK guide](https://code.claude.com/docs/en/agent-sdk/overview#get-started)
requires prior approval for third-party products offering claude.ai login or
subscription rate limits. Blackbird's reference-derived subscription sign-in
below has not passed live validation; matching OpenCode does not establish
official support or successful service acceptance.

Sign in through the separate operator auth process, then select the provider and
model explicitly for a fresh conversation:

```sh
blackbird auth login anthropic personal
blackbird --provider anthropic --model claude-sonnet-4-6 --effort medium
```

An explicitly selected Anthropic API key also works with this route, using its
issuer's API billing. Blackbird does not switch a failed subscription login to
that route automatically. The installed Claude CLI bridge has its own account
selection and is independent of owned Anthropic sign-in.

Native Messages uses the retained Lua workflow and ordinary native tools. It
streams text, preserves complete signed/redacted thinking and tool-result
history across continuation/reopen, and refuses incomplete streams before tool
execution. Unsupported effort opens an interactive choice before any request;
valid answers are saved. The initial adapter supports text and function tools; unsupported semantic
content is refused. A context containing Anthropic's opaque thinking cannot be
sent to a Responses provider; start a fresh conversation for that change.

The owned sign-in uses browser consent plus a hidden pasted callback code. A
successful browser page alone does not mean credentials were saved: confirm
`Sign-in saved` in Terminal and an active account in `blackbird auth status`.
The observed [token-exchange 429](../papers/claude-auth-integration-2026-10-17/LIVE_RATE_LIMIT.md)
is a distinct live compatibility/service result. There is no automatic replay of
an authorization code after an unknown exchange outcome.

## Trajectory inspection

`/trace` prints a readable committed timeline. `/trace JSON` accepts the same
query as model tool `trajectory_read` and Lua:

```lua
local page = blackbird.call("trajectory_read", {query={cursor=0,count=32,scan=128}})
-- Keep page.end, then resume at page.next to read that same frozen prefix.
```

Query fields: `cursor` (fact index), `end` (exclusive pinned fact count), `count`
1..64 (default32), `scan`1..256 (default128), optional `attempt` (32 lowercase
hex characters of an existing attempt). Default unfiltered query begins within
the latest128 facts; attempt filtering starts at0. Returned events include
readable event names, causal IDs, observed outcomes and existing source locators.
Decision metadata exposes operation names when available. Filtering includes the
attempt's decision/invocation and linked captures, excluding sibling attempts.

`next` advances by examined facts, including nonmatches. Empty events does not
mean the search finished: continue until `caught_up` is true for the pinned end.
A fresh query without end observes a newer committed prefix. Inspection calls
are themselves audited; explicit finite pages prevent an automatic feed from
observing its own deliveries. This first surface has no watch or automatic replay.

Original payloads remain separate: use `audit_inspect` with the event's `record`
and optional `source`, `offset`, `limit` for exact hex bytes. Timeline metadata
does not establish success from admission or establish a missing settlement.
It does not yet expose timing/usage aggregates, evaluations or Git checkpoints.
See [campaign design](TRAJECTORIES.md) for the next units.
