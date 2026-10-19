# Fold tool output in the conversation

Unit 2 of the 2026-10-19 little-bugs work, on `work/text-bugs-2026-10-19`. Bead
`arconaut-rhi.8`. Operator 2026-10-19: do not auto-expand every line of every tool
call. The chosen form is the header, the first 3 output lines, an elision row and
the last 3 lines. Ctrl-O stays the way to see everything.

## What happens today

- `CodingEngine` streams command output to `TerminalUI::process_output`
  (`src/coding.cpp:2675`, `:5523`). It arrives as `ChatKind::tool` text merged
  into the last tool block, which is the `▸ exec …` status block
  (`src/terminal.cpp`, status and process handlers).
- `ChatView` keeps the complete text of every block (`src/chat_view.cpp:228`) and
  renders every line.
- The display window is 64 KiB or 2,048 lines (`trim`, `:168`). One large command
  output therefore evicts the oldest blocks: the operator's earlier prompts and the
  model's replies leave the transcript.
- Command output is retained losslessly by the command owner and audit. The Ctrl-O
  command-result inspector (`52f9103`) reads those originals. `ChatView` is a
  display, not the retention of record.

## What changes

1. **Fold on append.** For `ChatKind::tool` blocks only, `ChatView::append` keeps:
   - the head: the block's first line (the header) plus 3 output lines;
   - the tail: the last 3 complete lines plus any unfinished line;
   - a hidden-line count.

   When the block exceeds the head plus the tail, the middle bytes are erased.
   `bytes_`, `newlines_` and the block's line count shrink by what was removed.
   - One raw `4032` retirement is emitted with a new reason `5` (folded). Its
     offset is the original stream offset of the erased range; its length is the
     byte count.
   - The display never holds more than about 8 lines of one tool block, so large
     output no longer evicts the conversation.
   - The original bytes are already copied by raw `4030` before any display change,
     as they are for clipping.
2. **Render the elision.** `rows()` renders a folded block from its first row:
   label, head lines, the row `┆ N more lines · Ctrl-O inspects` (muted), then the
   tail. A fold restarts that block's rendering from its first row instead of its
   committed offset. The block is always the last one, so the work stays constant
   per append: no O(n²) re-render.
3. **Thresholds.** A block folds only once at least 2 lines would be hidden, so
   the elision row never replaces a single line. Other kinds (user, assistant,
   notice, error, summary) are unchanged.

Not in scope:
- an in-transcript "expand all" (the ChatView no longer holds the middle; Ctrl-O
  is the expansion, as the operator's choice states);
- per-block expansion or selection;
- folding Lua `print` output that is not tool text;
- what the model receives (`arconaut-47k`).

## Direct checks

RED first:

- `tests/chat_view_test.cpp`:
  - a tool block of 40 lines renders the header, lines 1–3, `┆ 34 more lines`,
    and lines 38–40 (exact rows);
  - streaming 40 lines one chunk at a time gives the same rows, with
    `size()` bounded by head plus tail rather than total;
  - an unfinished last line shows in the tail;
  - 7 lines stay unfolded;
  - an assistant block of 40 lines is unchanged;
  - after a 10,000-line tool output, an earlier user block is still rendered (it
    is evicted today).
- `tests/ui_raw_test.cpp`: a folded append emits `4032` with reason 5 and the exact
  original offset and length, and `4030` still carries the complete text.
- `scripts/check-terminal-pty`, on the actual executable: a 40-line exec shows
  `▸`, lines 1–3, `┆ 35 more lines`, then 39, 40 and `[exit 0]`, and Ctrl-O
  shows the complete output.
- Readable baselines change only where a shown tool block exceeds the threshold,
  and each changed expectation is updated deliberately.

Then the installed full source hook, one independent review, fixes, and one
recheck.

## Review and recheck

The independent review found no fold arithmetic or rendering defect. It found
these presentation and record problems, all fixed:

- **Real exec output showed one tail line.** The `\n[exit N]\n` marker added an
  empty line. Markers now drop their leading newline at line start, so the tail
  is `39, 40, [exit 0]`.
- **Background output lost its attribution once folded.** New
  `ChatView::tool_start` and `tool_output` give `[command J]` output its own
  block (it merges only while that block is newest) and re-open the foreground
  header after other blocks.
- **Code-fence state broke across the hidden middle.** Fold now records fence
  parity and applies it at the elision.
- **The raw offset coordinates were undocumented and under-tested.** They are the
  block's display stream: clipped bytes are excluded and earlier folds are still
  counted. `ui_raw_test` checks two streamed folds and the complete 4030 text.
- **The raw event budget did not cover the fold event.** The append reserves one
  more range.
- **The trim reset was claimed but missing.** The earlier edit had failed; it is
  now in place at both front cuts.

Every new oracle for changed behaviour was observed failing on the old code: the
fold and attribution PTY cases on generation-IW1976SM, and the fence case with
the parity line disabled.

The bounded recheck found three defects in those fixes, all fixed. No third
layer follows.

- **Queued process output joined across streams.** `TerminalUI::post` joined
  consecutive process messages, so markers and attribution depended on timing.
  The new `terminal_process_join` joins only within one stream: plain foreground
  chunks, or chunks of one background job without its repeated marker. Markers
  never join onto other text.
- **A finished command's header stayed active.** `tool_end()` clears it when the
  operation completes, so post-completion lines like `[exec: capacity]` no longer
  reopen the previous command's header.
- **4030 now records the rewritten display text.** This is documented: the posted
  original stays in the queue's raw 4000 record and in command retention.

Both new oracles were first run against the previous behaviour (an always-join
`terminal_process_join` and a no-op `tool_end`) and failed there.
