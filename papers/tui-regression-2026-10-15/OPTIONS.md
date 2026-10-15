# TUI regression testing under Blackbird's C++/Lua stack

2026-10-15. Options assessment requested after the doctrine/component/view program.
Source inspection and primary-tool documentation, not a new execution/benchmark
campaign. No testing dependency is adopted. Pinned acquired originals and restoration
commands are in [SOURCES.tsv](SOURCES.tsv).

## Recommendation

Own a small deterministic scenario harness around the proposed native component
host. Combine semantic transition checks and styled cell/cursor expectations with
current real-PTY checks. Evaluate an external emulator tool if it saves meaningful
work; retain a small actual Ghostty/phux compatibility set for physical rendering,
keyboard negotiation and graphics. These attack different fault classes rather
than certify one another. Record/replay and model exploration help discover and
explain failures; expected outcomes still need direct oracles.

This is a proposed test architecture, not a selected dependency or a completed
harness. Its design slots into [component Unit1–2](../../docs/TUI_IMPLEMENTATION_PLAN.md).
The current test inventory is materially useful and should be reused.

## Current machinery

- `tests/terminal_test.cpp` exercises native composer state, multiline editing,
  history, paste, palette selection and command help without an actual provider.
- `tests/chat_view_test.cpp` checks typed display, incremental/cold formatting,
  no unnecessary reflow, selected Unicode/wide-cell behavior, control escaping,
  bounded display derivation and partial-write byte preservation through a pipe.
- `scripts/terminal-screen-oracle.py` independently interprets the emitted VT
  subset and compares delta versus full frame glyphs/styles/cursor, including
  fragmented feeds and selected cancellation cases. It is deliberately not a
  complete terminal emulator. Its width heuristic, sparse geometry and ignored
  graphics do not qualify general Unicode, terminal scrolling or Kitty images.
- Real launcher PTY fixtures cover paste, resize, local commands during active
  work, actual pure-Lua/process cancellation, unsent draft survival, terminal mode
  restoration and reopen. Related fixtures cover tiny viewports, sessions/drafts,
  tasks, participants and command jobs. Native custody/raw tests check further
  descriptor/terminal/editor/original-capture contracts.
- CMake registers these with CTest. Mac/Linux profiles and sanitizer tooling exist;
  several integrated CLI/PTY fixtures run serially because competing filesystem/
  scheduling pressure can obscure their intended assertions.

No test was rerun to answer the options question; these are source-grounded
coverage descriptions, not a fresh passing result. A full/delta comparison detects
differences between paths; it cannot detect a layout mistake common to both.
Add independently specified expectations for the new component behavior.

## Options by the claim they can check

| Option | Direct use in this stack | Strength | Limit/cost |
| --- | --- | --- | --- |
| Native semantic/component scenarios | Feed typed input, owner publications, async completions and reloads; assert exact focus/selection/draft, target identity and accepted invocation | Fast, deterministic; matches C++/Lua ownership boundaries | Requires small clock/owner/queue seams; internal state alone does not establish actual terminal output |
| Styled cell/cursor snapshots | Render a known bounded scene at known geometry; compare text, colors/attributes and cursor, with selected exact reference checks | Cheap visual regression over forms, lists, density, narrow layouts and error states | A baseline preserves prior behavior, including mistakes; not a font/GPU/image/physical-key oracle |
| Real PTY black-box scenarios | Run actual binary, send fragmented bytes/paste/resize/signals, inspect screen plus actual fixture effects | Covers launch, raw modes, queues, cancellation, restoration and process boundaries | More scheduling variability; PTY alone is not an emulator or a desktop input event |
| Independent emulator interpretation | Feed output into a different terminal core; query cells/modes and emitted replies | Catches VT interpretation, scroll/erase/wrap, protocol differences and some width assumptions | Engine/profile/version compatibility; its renderer can differ from a GUI terminal |
| Actual GUI terminal captures | Run selected episodes in configured Ghostty, direct and through phux; inspect pixels and genuine negotiated input | Needed for fonts, images, clipping, cursor appearance and real deployment interaction | Platform/font/scaling variation; expensive to keep broad; pin profiles and keep focused |
| Deterministic scheduling and generated sequences | Advance virtual time and completion order, block output, fail composition, change generations, unplug virtual device | Direct regression for hangs, stale targets, fairness and lost/duplicated actions | Simulated schedules need real boundary cases too; simulated time does not measure host latency |
| Record/replay and agent exploration | Retain input, observation transitions and captures; reduce found failures to fixed scenarios | Useful failure discovery, triage and human/model reproduction | Replay input alone does not fix external responses/time; a model's opinion or no-crash outcome is not a behavioral oracle |

## A practical owned scenario interface

Use the same native host and Lua composition API as production. Inject the small
boundaries that already need explicit owners: clock, terminal transport, view input,
async result delivery and action sink. A scenario supplies fixed observations and
checks exact intended behavior. A declarative C++ or Lua surface is sufficient;
there is no requirement to introduce a test DSL/compiler or test framework.

For example: select job A; queue an input event; publish reordered list B; resolve
the event; assert A or the specified refusal was dispatched, never a neighboring
job. Separately assert the styled collection and focus appearance. Another case
builds a scene from draft revision N, types through N+2, then publishes; text must
remain N+2. These are different claims from merely snapshotting an initial view.

Useful scenarios include composition failure/loop with continued local control,
stale async responses after route close/reopen, invocation admitted with missing
acknowledgment then reload, literal paste versus deliberate POWERWORD submission,
context base conflict with retained draft, start-turn old/new pinned generations,
and controller hold/disconnect/reconnect without resurrecting the old gesture.
The future domain owners remain responsible for actual effect/activation outcomes.

Use narrow, ordinary, wide and tiny geometries that exercise real layout choices;
light/dark and reduced-motion/graphics-fallback profiles where supported. Fixed
clock, scripted responses, deterministic IDs/source revisions and an explicit
terminal-width profile make snapshots reproducible. Do not blanket-mask changed
text/styles or remove focus marks merely to make a diff pass. Show expected/actual
cell/style/cursor differences. Review baseline changes against intended behavior.
Raw ANSI byte goldens are usually too tied to renderer optimization; semantically
equivalent output encodings should compare as cells/modes for visual tests.

## External tool candidates

### Microsoft tui-test: strongest external candidate to evaluate

[Current repository](https://github.com/microsoft/tui-test), retained at
`3de5098c11fb5a2d026fe2adf2149a132b4d705a`. It provides CLI/Python/Rust interfaces,
text/style/link locators, keyboard/mouse/resize controls, cell/cursor inspection,
snapshots, recordings and failure artifacts. Current documentation lists Alacritty,
Ghostty, Rio and xterm.js backends. Its retained `terminal/ghostty/core.rs` directly translates Ghostty VT state
into neutral cells/cursor; Cargo dependencies also include terminal engines
and raster/font tooling; named backends and generated images do not establish the
behavior of a particular GUI application's font/GPU/window stack.

Concrete benefit: richer independent emulator interpretation, useful diffs and
structured agent-readable failure traces without growing our small VT subset into
a full terminal. Python development bindings or a pinned executable fit the
existing development-only tooling; no Rust or Python enters the product runtime.
Costs: another evolving tool/dependency graph, backend feature/version differences,
custom protocol/input qualification and adapting assertions to our meaningful
operation outcomes. It cannot know a selected domain ID or effective hook generation
unless the scenario checks an actual semantic/effect observation too.

A useful qualification comparison would run the current palette/tiny-geometry
cases plus one blocked-output/control scenario, then inspect cell/style/cursor,
process cleanup and mode behavior. This report does not run or adopt that pilot.
Do not interpret `wait idle` as completed work or available control; quiet output
can also be a hang.

### Neovim's Screen pattern: excellent reference for what we own

[Screen implementation](https://github.com/neovim/neovim/blob/3068a8f652c8e83aac0696b4a513f369b5bbbd99/test/functional/ui/screen.lua)
and terminal fixtures are already acquired in the prior performance corpus. It
uses readable screen expectations with attribute identities and eventual state
matching, with useful asynchronous debugging. This is a design reference, not a
drop-in Blackbird library: its UI/RPC harness is tied to Neovim.

### VHS: useful recordings and visual examples

[VHS](https://github.com/charmbracelet/vhs), retained at
`24fa2254a9806091e6ee6a980e9f3bcfe0a9ba53`, offers scripted tapes, screenshots,
recordings and text/ASCII golden output. Its README requires ttyd and ffmpeg.
Useful for reproducible visual walkthroughs and selected golden examples, with
more external tooling than our current PTY approach. Tape completion alone does
not establish correct target dispatch, cancellation or responsiveness. Its Go
implementation would remain external development tooling if adopted.

### Tuistory and tuibot: exploration/operation alternatives

[Tuistory](https://github.com/remorses/tuistory), retained at
`84511af3de5fcd1fefde2522459a24b3d35f99b1`, offers named sessions, input, snapshots,
waits and human attach. It is useful agent-operation machinery, with an additional
JavaScript ecosystem and overlap with supported phux/current PTY drivers. Its
wait-for-quiet mode cannot establish completion. It is lower priority for this
regression need than strengthening our existing drivers.

[Tuibot](https://github.com/tui-testing/tuibot), retained at
`992922067be7e7d02d08357ceb5b09ae5db10017`, combines random, model-guided and scripted
exploration, replayable run directories and optional coverage using Docker/shell-use.
Potentially useful for finding surprising sequences; heavier than a focused native
regression suite. Any discovered failure needs a concrete oracle and stable case.
A coverage increase or an agent's screenshot judgment is not a correctness result.

These alternatives are studies, not blanket library recommendations. No framework
adoption is required to start useful component regressions under the owned stack.

## Responsiveness and presentation over time

Test progress and control during actual competition: continuous output, blocked
stdout, large catalogs/history, slow consumer, composition errors/loops and resize
storms. Assert no lost draft events, correct targets, finite queue behavior,
available cancellation and eventual latest coherent frame after draining output.
A blocked terminal cannot display a new frame; continued input/control processing
is a separate promise from immediate visible feedback in that condition.

Use virtual schedules to reproduce ordering failures. For physical host latency,
measure actual input acceptance→local state/action receipt→output completion under
a named workload/profile, report tails and recovery, and choose thresholds from
the intended action and measured baseline. Output completion is not photon time.
Structural regressions such as one keystroke reformatting hidden history or an idle
view repainting forever are useful direct work/byte assertions alongside latency.
Any raw measurement emission keeps the existing producer/recorder/tertiary split.

Controller semantics can be generated at the adapter boundary. Actual USB/MIDI
latency, switch behavior and mechanical haptic feel need a selected device and
physical observations; synthetic events cannot establish those claims.

## Proposed rollout

1. Preserve current CTest/PTY checks during extraction; retain known ESC repair
   under its existing owning issue.
2. Add semantic scenarios and styled cell/cursor expectations as native components
   become real, with deterministic scheduling for the identified ownership faults.
3. Discuss and qualify an external emulator tool only if its benefit justifies
   the development dependency; Microsoft tui-test is the leading candidate here.
4. Keep a small actual Ghostty/phux profile set for graphics, fonts, key negotiation
   and physical composition. Capture useful failures as traces/frames for inspection.
5. Add generated event schedules and optional model exploration where they attack
   actual remaining failure classes; reduce discoveries to meaningful regressions.

No new fleet, mutation, paid-model or GUI automation campaign is started by this
assessment. Existing engineering/hardening bounds apply; testing supports delivery
rather than becoming a second terminal product.

Source verification: all10 retained original byte-length/SHA-256 identities and
ignore checks pass; local document links resolve. No new extraction, execution
or runtime regression result is implied. Staged documentation whitespace passes.
