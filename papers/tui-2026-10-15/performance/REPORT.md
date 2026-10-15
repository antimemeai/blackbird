# Responsive native/Lua terminal interfaces: mechanisms and failure paths

2026-10-15. Supplement to the Blackbird repository's reference corpus. This lane acquired and read native rendering/event-loop implementations and Lua/native boundaries. It does not select dependencies, implement a design, benchmark contenders, or claim a fastest TUI. Acquired files are enumerated in [MANIFEST.md](MANIFEST.md); exact pins, hashes, restoration and omissions are in [MANIFEST.tsv](MANIFEST.tsv). The next UX/HCI/BHI discussion can use these mechanisms to ask sharper questions.

## What survives the source study

A responsive interface needs a short route from incoming control to visible acknowledgement even while other work produces output. Native code, more threads, and a nominal frame rate do not establish that route. The corpus exposes four distinct ways to lose it: doing too much before returning to input; blocking while writing a frame; retaining obsolete display events ahead of a cancellation; and waiting for terminal replies in routine drawing. Unicode layout adds correctness work which must fit that route rather than being omitted.

These are architectural inferences from the following specific implementations, not observed defects in current Blackbird. No acquired program was executed and no latency ranking was measured. Terms such as “fast” in project READMEs remain upstream claims.

The useful design separation to investigate is between exact application history, the current presentation model, queued control intent, and terminal bytes already partly emitted. An obsolete unstarted screen can often be replaced; an accepted control event, audit record, or half-written escape sequence cannot simply disappear. That distinction matters particularly for this repository's lossless raw instrumentation and retained originals: coalescing presentation must not silently coalesce those channels.

## Primary implementations and source anchors

### FTXUI: drain work, then draw; look at the actual pin

The new pin is `30f40a8f8d22fa7073982cabcb261dc93baa7ec6`, while the repository's previous corpus holds `d807cbca267a…`. Current machinery lives in `src/ftxui/component/app.cpp`, not the older documentation's `screen_interactive.cpp`. `RunOnce` fetches terminal events, posts buffered events, drains ready tasks with `RunUntilIdle`, and draws only if tasks executed. `RunOnceBlocking` uses a 16,666 μs polling sleep. The task runner executes callbacks to completion; the time budget cannot interrupt a long callback. `TerminalFlush` sends the accumulated output through `std::cout` and flushes it. These are concrete seams to compare with an owned wakeable input loop and output transport, not evidence of low tail latency. [Pinned App implementation](https://github.com/ArthurSonzogni/FTXUI/blob/30f40a8f8d22fa7073982cabcb261dc93baa7ec6/src/ftxui/component/app.cpp#L851), [task runner](https://github.com/ArthurSonzogni/FTXUI/blob/30f40a8f8d22fa7073982cabcb261dc93baa7ec6/src/ftxui/component/task_runner.cpp#L42).

`MultiReceiverBuffer` retains values until the slowest receiver advances or is removed. The application explicitly scopes its temporary setup receiver to avoid retaining every later event. This is an unusually relevant lifetime example for a harness with human and model subscribers. Its terminal parser tests and fuzz entry points offer study material for split input sequences. [Buffer](https://github.com/ArthurSonzogni/FTXUI/blob/30f40a8f8d22fa7073982cabcb261dc93baa7ec6/src/ftxui/component/multi_receiver_buffer.hpp), [setup receiver](https://github.com/ArthurSonzogni/FTXUI/blob/30f40a8f8d22fa7073982cabcb261dc93baa7ec6/src/ftxui/component/app.cpp#L1249).

### Notcurses: composition, rasterization, and writeout have different ownership

Reused the existing exact `b26048eebc74d5d254717d3332fa484718f9efe6` snapshot. `doc/HACKING.md` separates pile rendering from rasterization against terminal state. Different piles may render concurrently; rasterizations sharing the last frame and screen may not. Its writeout blocks, and damage buffers depend on the last-frame state advanced during rasterization. Consequently, they must be written in order. Its resize discussion also explains why resize arrival and writeout cannot form a perfectly synchronous transaction. [Rendering/rasterizing/writeout](https://github.com/dankamongmen/notcurses/blob/b26048eebc74d5d254717d3332fa484718f9efe6/doc/HACKING.md#L86).

The implication for a proposed nonblocking Blackbird renderer is precise: retain an immutable in-flight byte suffix and separately track the terminal state it establishes. Replacing a queued future frame requires recomputing from that state; dropping arbitrary deltas risks corruption. This is a proposed contract to design and test. The README's SIGWINCH warning is also useful: in a multithreaded program, signal-mask ownership can decide whether a blocking input poll wakes for resize. [Upstream environment notes](https://github.com/dankamongmen/notcurses/blob/b26048eebc74d5d254717d3332fa484718f9efe6/README.md).

### Turbo Vision: event-batch flushing plus damage tracking and deadlines

At `b5fc1e50dcc2da062d72f43eb3662894d25aaf30`, `THardwareInfo::readEvents` reads another bounded event batch only after the current queue empties. `waitForEvents` flushes before blocking when no events remain. `DisplayBuffer` tracks damaged row ranges, compares current cells with its flush buffer, and can defer output to a configured maximum FPS. `Platform::waitForEvents` includes the pending flush deadline in its wait timeout. This closes an easy hole: deferred output must eventually flush even when no more input arrives. [Hardware event path](https://github.com/magiblot/tvision/blob/b5fc1e50dcc2da062d72f43eb3662894d25aaf30/source/platform/hardware.cpp#L96), [display buffer](https://github.com/magiblot/tvision/blob/b5fc1e50dcc2da062d72f43eb3662894d25aaf30/source/platform/dispbuff.cpp#L111), [deadline integration](https://github.com/magiblot/tvision/blob/b5fc1e50dcc2da062d72f43eb3662894d25aaf30/source/platform/platform.cpp#L95).

For Blackbird, compare bounded event batches with a simple “drain everything first” policy under perpetual stream and controller traffic. A frame cap limits repaint cadence, not callback runtime or input latency. Preserve its original Borland notice and modern MIT/component notices; the corpus is reference-only.

### FINAL CUT: terminal byte costs and escape ambiguity are explicit

At `0ed8567d9438d093ee4bf9e33c013cbb37795b70`, `FApplication::processNextEvent` processes timers, input, queued events and terminal updates, then external user events. `FKeyboard` includes a configurable key timeout, initially 100,000 μs, and explicit handling of a lone Escape after that timeout. That timer is a parsing policy rather than an inevitable cost for all keys. [Application order](https://github.com/gansm/finalcut/blob/0ed8567d9438d093ee4bf9e33c013cbb37795b70/final/fapplication.cpp#L1636), [keyboard](https://github.com/gansm/finalcut/blob/0ed8567d9438d093ee4bf9e33c013cbb37795b70/final/input/fkeyboard.cpp#L49).

`FOptiMove` prices candidate terminal cursor operations using capability lengths/durations instead of assuming one universal cheapest cursor sequence. Its direct optimizer tests are useful comparative examples. For an owned renderer, separate CPU layout cost, bytes produced, system calls, terminal parser work, and acknowledgement latency before optimizing any one. Shortest encoded output need not minimize time in a modern terminal. [Cursor optimizer](https://github.com/gansm/finalcut/blob/0ed8567d9438d093ee4bf9e33c013cbb37795b70/final/output/tty/foptimove.cpp#L119).

### libtickit/libtermkey: separable byte streams and deadlines; inspect the transport

libtickit `e1a9c7d9798cc00e1c6b03eae1eb7f2c12547690` offers windows, clipping/render buffers, an event-loop abstraction, and terminal input/output through file descriptors or callbacks. Its mock terminal and renderbuffer tests make region operations inspectable without launching a real terminal. The direct-fd path in `tickit_term_flush` calls `write` and clears the output buffer without examining how many bytes it accepted; `write_str` likewise does not retry short writes. This observation specifically rules out assuming the default fd writer supplies a robust nonblocking output queue. A callback can instead hand owned bytes to an application transport. [Output path](https://github.com/leonerd/libtickit/blob/e1a9c7d9798cc00e1c6b03eae1eb7f2c12547690/src/term.c#L944), [mock terminal](https://github.com/leonerd/libtickit/blob/e1a9c7d9798cc00e1c6b03eae1eb7f2c12547690/src/mockterm.c).

Official libtermkey 0.22 was acquired as a hash-pinned release tarball. `termkey_waitkey` distinguishes no input from an incomplete sequence, waits using `poll` and `waittime`, and forces interpretation after the ambiguity deadline. The async API lets an application own those waits. Its upstream explicitly discourages new standalone use in favor of libtickit; this acquisition studies the parser, not recommends the deprecated library. [Canonical source and status](https://www.leonerd.org.uk/code/libtermkey/).

### Neovim: display queue overload is a cancellation problem

At `3068a8f652c8e83aac0696b4a513f369b5bbbd99`, `tui_flush` checks the TUI event queue against `TOO_MANY_EVENTS` and purges it on overload. Its comment names the failure: display events can accumulate faster than the terminal serves them, leaving even Ctrl-C waiting behind the queue. This is strong mechanism evidence for testing output floods; its “hours” remark is upstream motivation, not a timing measured here. [Overload recovery](https://github.com/neovim/neovim/blob/3068a8f652c8e83aac0696b4a513f369b5bbbd99/src/nvim/tui/tui.c#L1712).

`flush_buf` separates partial and final flushes, brackets supported synchronized output, writes through libuv, then runs its write loop to completion. An asynchronous primitive by itself therefore does not establish a nonblocking caller. Lua's fast callbacks restrict editor APIs; `vim.schedule` transfers editor work to the main queue. That preserves execution rules but does not make scheduled expensive callbacks cheap. [Flush](https://github.com/neovim/neovim/blob/3068a8f652c8e83aac0696b4a513f369b5bbbd99/src/nvim/tui/tui.c#L2580), [Lua callback rules](https://neovim.io/doc/user/lua/), [deferred APIs](https://neovim.io/doc/user/api/).

### vis: small native core, expressive synchronous Lua

At `10b630af09404cf2c887e2d7e6e24ecda9d05ea7`, the main loop integrates subprocess descriptors and terminal input with `pselect`, handles signals/resizes, draws, processes process readiness, then drains keys. Its key decoder uses a separate `poll` for an incomplete escape sequence. A parser wait on this path can therefore delay returning to the other descriptors. [Main loop](https://github.com/martanne/vis/blob/10b630af09404cf2c887e2d7e6e24ecda9d05ea7/vis.c#L1274), [escape handling](https://github.com/martanne/vis/blob/10b630af09404cf2c887e2d7e6e24ecda9d05ea7/ui-terminal.c#L657).

Lua exposes motions/text objects and ordered event handlers. `events.emit` calls them synchronously, stopping on a non-nil result. This is an expressive compact seam worth studying, with an explicit implication: a callback owns the event-loop thread until it returns. Moving all extensibility into Lua does not resolve responsiveness. [Lua event and composition API](https://github.com/martanne/vis/blob/10b630af09404cf2c887e2d7e6e24ecda9d05ea7/lua/vis.lua).

### Lite XL: Lua scheduling and render invalidation outside the terminal

Lite XL `234fa0998016f3da9eb9ceaf3c4bc79b93462276` is a graphical editor, acquired because its native renderer/Lua policy split answers a relevant question. `run_threads` resumes Lua coroutines and checks a frame budget between resumptions. A coroutine which does not yield can exceed it. `core.run` combines redraw pacing, event waiting, blink deadlines and coroutine wake times. [Lua scheduler](https://github.com/lite-xl/lite-xl/blob/234fa0998016f3da9eb9ceaf3c4bc79b93462276/data/core/init.lua#L981).

The C render cache records draw commands, hashes spatial cells, merges changed cells into dirty rectangles, and redraws those regions. This is a useful alternative to tracking every semantic mutation as damage. Its hash comparison is a performance mechanism with collision considerations; study alongside exact cell comparison rather than assuming interchangeable correctness. Transferable questions are the ownership/lifetime of command text and fonts, invalidation after style changes, and work spent hashing unchanged views. It is not a terminal-output solution. [Render cache](https://github.com/lite-xl/lite-xl/blob/234fa0998016f3da9eb9ceaf3c4bc79b93462276/src/rencache.c#L24).

### terminal.lua: avoid terminal-query round trips inside drawing

At `e74b42b18d1d771ffe0e7c6c3485136206fd45bc`, `terminal.input` documents cumulative query waits and recommends tracking state rather than querying it per widget. Its code preserves keyboard input encountered while awaiting a terminal answer in a separate buffer. The stated platform sleep timings are upstream observations, unmeasured here. [Input/query module](https://github.com/lunarmodules/terminal.lua/blob/e74b42b18d1d771ffe0e7c6c3485136206fd45bc/src/terminal/input/init.lua).

`output.write` concatenates values then flushes every call; copying a convenient per-widget usage pattern would create avoidable flush cadence. Its ambiguous-width detector probes once using cursor-position responses and supports an override. One ambiguous character probe is not general proof of all emoji/grapheme widths. These are useful Lua-facing API and capability-cache examples, not a qualified performance stack. [Output](https://github.com/lunarmodules/terminal.lua/blob/e74b42b18d1d771ffe0e7c6c3485136206fd45bc/src/terminal/output.lua#L63), [width probe](https://github.com/lunarmodules/terminal.lua/blob/e74b42b18d1d771ffe0e7c6c3485136206fd45bc/src/terminal/text/width.lua#L35).

### Chafa: give expensive visual quality work an explicit budget

Chafa `be73375f2049a373287b4dac9f2b41057d12bb2a` converts images into character/pixel terminal output. Its symbol renderer limits candidate exploration using `work_factor_int`, dispatches an AVX2 cell-error path when available, and batches rendering over workers. This provides concrete examples of adjustable visual work and ISA fallback. It does not establish an input-to-paint latency because conversion, byte generation, transport and display remain separate costs. [Symbol renderer](https://github.com/hpjansson/chafa/blob/be73375f2049a373287b4dac9f2b41057d12bb2a/chafa/internal/chafa-symbol-renderer.c#L142).

For Blackbird's detailed avatar, diagrams or image previews, investigate caching by actual source/geometry/style identity and preparing replacements off the control path. Keep an available prior image while preparing a new one. Whether symbols, Kitty graphics or another supported path is best must be decided against the actual terminal/multiplexer matrix; this source study selects none.

## Specifications that prevent attractive wrong answers

Four primary documents were downloaded intact; [documents.tsv](documents.tsv) records original URLs and byte hashes. Source-tree Neovim API/UI/Lua docs and Notcurses manuals supply additional offline documentation.

- POSIX `write` defines partial progress, errors and nonblocking behavior. An output component needs the unsent suffix, explicit EINTR/EAGAIN handling and an ordering contract. A successful short write is not completion. [POSIX.1-2017 write](https://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html).
- libuv's design distinguishes readiness-loop callbacks from thread-pool work and specifies loop thread-safety boundaries. An owned reactor should make equivalent thread/ownership boundaries explicit without adopting libuv. [Design overview](https://docs.libuv.org/en/stable/design.html).
- UAX29 revision 47 supplies extended grapheme boundaries and implementation guidance for data-driven/state-machine approaches. It also distinguishes perceived-character limits from byte bounds. It is a pinned Unicode 17 reference, not a claim to be the newest Unicode publication. [UAX29 revision47](https://www.unicode.org/reports/tr29/tr29-47.html).
- UAX11 revision44 says modern terminal width requires appropriate tailoring. Grapheme segmentation, terminal columns and byte offsets must remain distinct native concepts. Fitting text by summing code-point widths cannot be assumed correct for every cluster or emulator. [UAX11 revision44](https://www.unicode.org/reports/tr11/tr11-44.html).

Rhizome's read-only C++ rigor proposal at `projects_old/inactive-2026-10-07/rhizome/papers/cpp-rigor-stack.md` reinforces transferable ownership questions: checked dimensions, view invalidation, publication, thread transfer and actual whole-operation cost. Its project choices remain local; it supplies no terminal benchmark or additional adoption decision.

## Experiments to design after the UX/HCI/BHI pivot

These are proposed direct experiments, not tests run in this acquisition phase. First agree the interaction promises: what cancellation must acknowledge, what stale presentation may be replaced, what must survive, and which terminal/multiplexer configurations matter. Do not invent a global RSS/latency ceiling before choosing the product behavior.

| Question | Workload/fault injection | Direct observation or oracle |
| --- | --- | --- |
| Can display backpressure delay control? | PTY reader deliberately stalls while streamed tool output and repeated key/control events continue; then resumes | Exact control order and acknowledgement observations; resident queue bytes and age; terminal state equals the final native screen model after draining |
| Can work perpetually outrun input? | Continuous stream plus timer/animation plus controller-like bursts | Maximum observed intervals without servicing control, per-class oldest-event age, eventual final state; compare bounded batches against drain-until-idle |
| Can partial output corrupt the screen? | Force every possible short write and EAGAIN boundary around escape sequences and multibyte text | Retained unsent suffix emits exactly once in order; decoded terminal state equals an independently specified target grid |
| Does cancellation remain available during Lua work? | Long bounded computation, deliberate nonyielding callback, callback error, deferred work | Explicitly specified interruption/containment behavior; no claim that coroutine scheduling preempts arbitrary C/Lua callbacks |
| Do text layout and editing agree? | Combining runs, emoji/ZWJ/flags, Indic clusters, ambiguous-width text, huge unbroken lines, resize and scroll | UAX boundary cases for owned segmentation; separately compare reported cursor positions/visible cells on selected real terminals; declared fallback when they disagree |
| Are query protocols out of the drawing path? | Delay/drop/interleave capability replies with typed keys and paste | Keys retain order and bytes; timeout terminates; repaint does not synchronously query each component |
| Does expensive formatting scale with visible work? | Long retained conversation, streaming markdown/code blocks, deep history while scrolled away, style and width changes | Raw processing/allocation/byte counts tied to the actual mutation; unchanged hidden history need not relayout; exact visible output after targeted invalidation |
| Does deferred drawing eventually happen? | One update followed by complete input silence; sleeping timers; resize storm | Pending flush deadline wakes and produces the final view without a new key; no busy-spin implied by wake scheduling |
| Can a slow subscriber retain all UI history? | Human/model observers pause, detach and reconnect | Each subscription's retention/lifetime contract; exact application originals remain independent of discardable presentation cache |
| Does detail improve or obstruct work? | Identical operator/model tasks with image/animation/detail choices | Later UX/HCI/BHI lane chooses task success, navigation/error and comprehension measures; FPS alone cannot answer this |

For instrumentation, follow Blackbird's existing rule: emit raw events cheaply, preserve them through the separate recorder, and perform timing calculations/aggregation outside the main processes. The study should identify actual input, decode, dispatch, state-change, render-start, write-progress and acknowledgement observations without adding writer-side histograms or a perpetual benchmark gate. Terminal byte completion is only one point; visible display requires a separately justified observation method.

## Acquisition boundary and remaining breadth

This lane supplies 11 studied implementations (10 newly archived, Notcurses reused) and four standalone primary specifications. Other frumentarii own MIDI/haptics, extended keyboard protocols, control ergonomics and information density. No new issue about current Blackbird is asserted from reading another project. The assembled patterns justify the next investigation and a later owned C++/Lua design; they do not justify importing source or skipping that design.
