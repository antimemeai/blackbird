# The terminal is another participant in responsiveness

2026-10-15. Supplemental source study of libvaxis and Ghostty, pinned in
[sources.tsv](sources.tsv), restorable through [MANIFEST.md](MANIFEST.md).
Both archives are retained intact and both root licenses are MIT. These are
references, not adopted dependencies or implementations run during this study.

libvaxis was selected for its explicit capability negotiation, event ownership
and terminal protocol handling. Ghostty was selected because it is already a
relevant terminal for Blackbird and exposes the other end of output processing,
Unicode widths and presentation. Neither a project's language nor its README's
speed description establishes performance on our workload.

## libvaxis: bounded queues also need a way to stop

Paths are relative to
`quarantine/tui-2026-10-15/substrate/source/libvaxis/`.

- `src/Loop.zig` declares a 512-element queue, reads input in a separate task and
  exposes blocking and nonblocking event operations. `postEvent` waits for room;
  `tryPostEvent` can report failure to enqueue. A producer waiting behind a full
  queue still needs cancellation. A shared queue full of motion can also delay
  a later control key unless an owner defines a distinct delivery rule.
- `src/queue.zig`, `close`, preserves buffered items, records the first close
  reason, rejects new writes and broadcasts to both consumer and producer
  waiters. `Loop.stop` closes the queue before canceling the input task. The
  source includes tests named `stop interrupts a reader posting to a full queue`
  and `close wakes all blocked writers and readers`. These directly address a
  real shutdown failure class. They were studied, not run.
- `Loop.winsizeCallback` may fail to enqueue a resize when capacity is exhausted;
  its comment calls this coalescing. That is not by itself proof that the last
  physical dimensions will eventually be presented. For Blackbird, a sticky
  latest-size slot plus a wake notification is a candidate worth testing.
- `Loop._ttyRun` treats an isolated ESC according to whether continuation bytes
  are immediately ready. That favors immediate ESC handling but cannot know
  whether a delayed byte will complete a legacy Alt sequence. This is a concrete
  tradeoff to compare with negotiated keyboard events and an explicit legacy
  timeout; no recognizer can recover information the transport never encoded.
- `src/Vaxis.zig`, `queryTerminalSend`, `queryTerminal` and
  `enableDetectedFeatures`, separate query transmission, waiting and activation.
  Width method, keyboard extensions and rendering features depend on responses
  and quirks. `src/Loop.zig` contains tests for cursor-query/capability-probe
  interactions and expiry. Blackbird should recognize replies separately from
  operator input and must not make unanswered queries an indefinite startup wait.
- `Vaxis.render` uses cell state and selected width capabilities. The same source
  is useful for explicit-width and image feature study. Its writer contract still
  needs examination before any proposed use with a slow PTY; a modern protocol
  feature list does not establish a nonblocking transport.

## Ghostty: a completed write is not a displayed frame

Paths are relative to
`quarantine/tui-2026-10-15/substrate/source/ghostty/`.

- `src/terminal/modes.zig` identifies synchronized output (2026) and grapheme
  cluster handling (2027). `src/renderer/generic.zig`, `updateFrame`, skips a
  rendering update while synchronized output is active.
  `src/termio/stream_handler.zig` handles the mode and
  `src/termio/Thread.zig`, `startSynchronizedOutput`, supplies timer handling;
  `src/termio/Termio.zig`, `resetSynchronizedOutput`, resets it. Therefore a
  pretty atomic frame and an available input path are separate claims. A lost
  synchronization terminator can look like an application hang even if its
  process continues consuming keys.
- `src/termio/Termio.zig` initializes grapheme width policy from configuration.
  `src/terminal/Screen.zig` explicitly manages multi-codepoint grapheme storage,
  styles and hyperlinks. The application's locale `wcwidth` result can disagree
  with the terminal's configured grapheme behavior. That matters for cursor
  placement, selection, clipping, wide glyph overwrites and stable dense columns.
- `test/fuzz-libghostty/` contains separate OSC, byte parser and full stream fuzz
  harnesses and retained seed corpora. The stream harness exercises scalar and
  slice paths. Study those as different fault classes: byte parsing, allocating
  payloads and stateful rendering. Their presence is not evidence of a particular
  reliability level, and no campaign ran here.

## Experiments to carry into design

1. Stop reading a PTY while streaming output, inject a control action through the
   remaining input path, and distinguish input acquisition, action dispatch,
   effect cancellation, packet drain and actual display. A test that resumes the
   reader before checking control has missed the important interval.
2. Split every supported input/reply sequence at every byte boundary; add delayed
   legacy ESC continuations, malformed/incomplete sequences and interleaved probe
   replies. Check recovered text and action order against a declared grammar.
3. Close a full queue with a waiting producer and waiting consumer, then exercise
   repeated stop/restart. Check actual completion and retained event disposition.
4. Change physical dimensions while output is stalled; after drain, compare the
   final visible size with the latest observed dimensions. Exercise zero and tiny
   viewports and a resize during a partially written packet.
5. Compare declared terminal profiles on combining marks, emoji sequences,
   variation selectors, ambiguous-width characters and right-edge wide glyphs.
   Use original strings and expected cell/selection behavior, not just snapshots
   that share the implementation's width routine.

Blackbird already has an owned single-writer, partial-write-aware output path.
These references sharpen the questions around that machinery; they do not call
for replacing it or importing an emulator wholesale.
