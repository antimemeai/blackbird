# Tool output in the model request

Bears on `arconaut-47k` (keep full tool output out of the request) and
`arconaut-rhi.8` (fold tool calls in the TUI). The colleague's rhi.8 unit is
[TOOL_FOLD.md](../text-bugs-2026-10-19/TOOL_FOLD.md): the header, 3 head lines, an
elision row and 3 tail lines, with Ctrl-O for the rest. This note is about what the
**model** sees.

Reports: [set A](reports/tool-output-A.md) covers Codex, Claude Code 2.1.286 (the
bundle plus current docs, labeled separately), Goose, Crush, Kimi CLI and Hermes.
[Set B](reports/tool-output-B.md) covers OpenCode, Pi, Oh My Pi, Gemini CLI/Qwen,
OpenHands and mini-SWE-agent/SWE-agent. All were read from the 2026-09-30
snapshots. OpenHands could not be studied: the snapshot is frontend only (see
[gaps](gaps.md)).

## Blackbird today (master)

- `programs/turn.lua` supplies `output_max_bytes=16384` to `exec` and to un-ranged
  `read_file`. The model may raise it, lower it or set it to zero.
- Both tools keep a **head-only prefix**. The result carries `output_bytes` and
  `omitted_bytes` (`src/tools.cpp:386`, `:208`).
- The complete original is captured before projection, into the session's
  retained audit. It survives reopen and never expires.
- Recovery is through dedicated tools. `exec` returns `output_ref`;
  `read_process_output{output_ref, byte_start, byte_end}` reads exact retained
  bytes. `read_file{snapshot, range}` reads the exact captured file after it
  changes, is deleted, or the session reopens. No replay.
- Nothing removes old tool output from later requests automatically. It stays
  until the model compacts, archives or summarizes it through managed context.
  Those operations keep originals and allow restore.

## How the corpus does it

| | Shell cut | Full copy | Way back | Ageing |
| --- | --- | --- | --- | --- |
| Codex | head + tail, ~10k "tokens" (bytes ÷ 4) | **none** | re-run | auto-compact at 90% |
| Claude Code | first 30 KB; success: 2 KB preview + file | session `tool-results/` | ordinary Read/Grep on the path | microcompact: file + stub with path |
| OpenCode | tail, 2000 lines / 50 KiB | shared dir, 7 days | Read with offset/limit (hinted) | opt-in prune; placeholder with **no path** |
| Pi | tail, 2000 lines / 50 KiB | `$TMPDIR` log, never cleaned | ordinary read | none (summary compaction only) |
| Oh My Pi | head 20 KB + tail 20 KB above 50 KB, all tools | per-session artifacts | `artifact://N:a-b` paged read | superseded reads and empty results elided; **destructive** rewrite |
| Gemini CLI | first 20% + last 80% of 40k chars | per-session dir, 30 days | read_file (lines capped at 2000 chars) | always-on masking with preview + path |
| Goose | last 50 lines / 10 KB | temp files, 8 recycled slots | shell head/tail | off by default |
| Crush | head + tail, 30k cells | `.crush/shell-output/`, 7 days | `view` on the path | whole-session summary |
| Hermes | 40% head / 60% tail, 50k chars | cache dir, 1–7 days | read_file / search | old output cleared at 50–75% |
| Kimi CLI | head, 50k chars | **none** | re-run | compact at 85% |
| mini-SWE-agent | head 5k + tail 5k chars, in a warning | **none** (trajectory only) | re-run | none |

## Where Blackbird stands

**Retention is the strongest in the set.** Every reference that keeps a full copy
uses a filesystem path, and most of those paths expire or get recycled: 24 hours to
7 days, 8 recycled slots in Goose, cleanup by a sibling process in Claude Code. The
conversation keeps naming paths that no longer hold the bytes. Three references
keep nothing and must re-run the command. Blackbird's originals are exact,
session-owned and permanent, and recovery never replays an effect. Only Oh My Pi's
per-session artifacts come close, and its own ageing step destroys originals.

**The cut is the weakest part.**

1. **Head-only for commands is the wrong end.** Compiler errors, test summaries,
   tracebacks and exit messages arrive last. Every reference that thought about
   shell output keeps the tail: tail only (OpenCode, Pi, Goose) or head plus tail
   weighted to the tail (Gemini 20/80, Hermes 40/60, Codex, Crush, Oh My Pi,
   mini-SWE). Blackbird keeps the first 16 KiB of a failing build and omits the
   error. Reads are different: a head-first, paged read is right for files, and
   everyone does it.
2. **A cut through a UTF-8 character hex-encodes the whole result.** This is from
   source reading and has not yet been reproduced live. `display_bytes`
   (`src/tools.cpp:106`) hex-encodes the entire value whenever `valid_utf8` fails,
   and `valid_utf8` (`src/value.cpp:187`) rejects a truncated trailing sequence.
   The default cut is a raw byte offset (`src/tools.cpp:386`;
   `src/command_jobs.cpp:514` through `process_output_presentation`). So a cut that
   lands inside a multibyte character turns 16 KiB of readable output into about
   32 KiB of hex. Ordinary triggers include GCC's curly quotes, box-drawing test
   output and non-ASCII paths. A `read_process_output` range that starts
   mid-character behaves the same. Five references have the same class of byte-cut
   defect (Codex, Claude Code, Kimi, Hermes, Crush), but none of them hex-encodes
   the result. Filed as `arconaut-q4n`.
3. **The result doesn't say how to continue.** The model learns about
   `read_process_output` from the tool description, and the result carries counts
   but no next range. The best references put the continuation in the observation
   itself: Oh My Pi's `artifact://N:a-b`, and OpenCode's "use Read with
   offset/limit". Blackbird's own `process` read already returns `next_offset`;
   the exec observation does not.
4. **16 KiB is moderate, not small.** References sit around 30–50 KB. 47k asks for
   a smaller default. The evidence says the shape matters more than the size:
   head plus tail, a stated total, and a continuation handle.

**Recovery through dedicated tools versus ordinary tools.** Most references save to
a path so that the model recovers with the read and grep it already knows.
Blackbird uses dedicated tools with byte ranges. That is more exact and avoids the
expiry problems, but it costs the model a concept, and byte ranges don't match how
models think about logs. `read_file` already offers line ranges;
`read_process_output` does not. Two cheap ways to close the gap: give
`read_process_output` line mode, or let `read_file` accept an `output_ref`, so one
read tool covers files, snapshots and command output.

**Ageing is where CLM should beat everyone, and doesn't yet act.** The references
age old output in three ways:
- Claude Code microcompact and Gemini masking: save the output, leave a stub with
  a path, protect the newest N tokens, and act only when the saving is worth it.
- Oh My Pi: replace superseded reads of the same file and results with nothing
  useful in them. Clever, but it rewrites originals.
- OpenCode and Qwen idle clearing: a placeholder with no way back.

Blackbird already has the right primitives: managed `archive` with exact `restore`,
retained originals, and context-revision audit. A model-free transformation could
archive tool results older than N turns, or superseded reads of the same path. It
would leave a one-line stub carrying `output_ref`/`snapshot`, and it would be
strictly better than every reference, because nothing is lost and the stub is
always valid. Prompt caching is the constraint every reference respects. Change the
prefix rarely, in batches, and only when the saving is large. This belongs with
`arconaut-a1v`.

**Things Blackbird already gets right that the references trip on:** recorded
context revisions and exact requests, so a resumed session rebuilds identical
requests (only Claude Code does this deliberately). Originals are captured before
any lossy step, whereas Hermes truncates before removing secrets. No recycled or
expiring handles. No silent cuts: counts are always present when a budget applies.
Stdout and stderr are combined in order, as they are everywhere.

## Candidate shape for 47k

Drawn from the above; this is input to the 47k design, not a decision.

- `exec`: head plus tail weighted to the tail, for example 2 KiB of head and 6 KiB
  of tail by default, cut on character (preferably line) boundaries. Report
  `output_bytes`, the omitted byte and line ranges, and `output_ref` with the exact
  next range, inside the observation.
- `read_file`: keep head-first paging. Cut on character boundaries, and put the
  next range in the result.
- One read path for recovery: line ranges for `read_process_output`, or
  `read_file{output_ref}`.
- Later, with a1v: a model-free archive transformation for aged and superseded
  tool results, stubs carrying their handles, batched for the cache.
