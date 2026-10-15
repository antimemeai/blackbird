# Arco terminal redesign

Historical October7 proposal. The [TUI doctrine](TUI_DOCTRINE.md),
[component design](TUI_COMPONENTS.md), [current/future views](TUI_VIEWS.md) and
[delivery plan](TUI_IMPLEMENTATION_PLAN.md) now govern the expanded program.
Retain the craft/source ideas below as reference; old mascot/sidebar exclusions,
fixed animation/layout targets, renderer diagnoses and campaign cutoffs do not
override current intent or implemented native output behavior.

Discussion plan, 2026-10-07. Grounded in the
[local harness study](../papers/2026-10-07-terminal-ux-study.md).
The operator wants a gorgeous, animated, information-dense terminal and good
basic slash commands now. Advanced orchestration/control surfaces follow later.
This plan is not an implementation completion claim.

The [chat presentation proposal](CHAT_PRESENTATION_PROPOSAL.md) develops message
composition and the renderer in detail after the operator's updated screenshot.
Its source study covers C renderer damage tracking/storage/output as well as agent
presentation. The inline slash menu and initial borders are implemented; the rich
conversation renderer remains proposed.

## Visual direction

Make it feel like a finely made instrument: luminous accents, clear hierarchy,
compact operational information, confident spacing, and a little theater when
something happens. Vivid is the default. No mascot, SaaS mockup, dashboard grid,
or giant persistent banner. The conversation occupies most of the window.

Proposed palette: warm white main text, restrained slate secondary text,
electric cyan focus and Arco accent, lilac code/reasoning accent, amber pending
activity, mint successful completion, coral failure. Use terminal background
by default; a light-terminal variant changes contrast, not information hierarchy.
Offer truecolor, 256-color, ANSI and plain fallbacks. Meaning has text/symbols
as well as color; muted text must remain readable.

One compact top line shows project, session label, effective model and effort.
Long paths move into `/session`. Below it, readable chat blocks with a fine
accent edge distinguish operator, Arco, code, command results, and tool groups.
Render basic Markdown deliberately: headings, emphasis, lists, links, code fences
and readable tables. Preserve exact source text for copy/inspection. Code gets
syntax accents for C++, Lua, shell and JSON first; plain styling is the fallback.
Streaming incomplete fences remain legible; never delay text for an animation.

Tools show a compact identity, target, state, elapsed time and bounded preview.
Successful routine tool output can fold down; errors remain visibly identified.
Expansion shows retained output with exact omitted counts where known. Raw
request bytes/provider JSON move to inspection, while failures stay discoverable.
Do not fabricate percentage complete, prices, reasoning, or token usage.

Composer grows from one line to at most six (also bounded by physical height),
with a subtle focus border and visible cursor. Its lower line gives a few
contextual keys, not a wall of shortcuts. A compact status strip carries actual
activity, elapsed time, queue count, reported usage and context information where
available. At narrow widths drop secondary fields before damaging text/cursor.
No permanent sidebar in this pass.

Spatial sketch; colors, emphasis and motion are not representable here:

```text
 ARCONAUT   arconaut / ergonomics             sol 6.1 · medium
 ───────────────────────────────────────────────────────────
 YOU
 Make the command menu feel excellent.

 ARCO
 I'll make discovery fast and keep the draft intact.

 ▸ read_file  src/terminal.cpp                 ✓  3ms
 ▸ shell      release terminal checks          ⠹  1.2s

   /model       Choose model                 Session
   /effort      Choose reasoning effort      Session
 › /commands    Browse all commands           Chat
   ↑↓ choose    Tab fill    Enter run/fill    Esc dismiss
 ╭─────────────────────────────────────────────────────────╮
 │ › /                                                     │
 ╰─────────────────────────────────────────────────────────╯
 ⠹ checking terminal input · 8s        queue 0    / commands
```

The actual command list orders exact/prefix matches before description matches;
the sketch illustrates hierarchy, not a prescribed filtered order.

## Motion belongs in this pass

An 80–100ms small activity indicator and slow highlight sweep make live work
obvious. On completion, the active indicator settles into a success/failure mark
with a brief accent fade. Menu focus changes immediately; a short color transition
can follow without delaying selection. A tiny bounded welcome flourish is optional
after the working screen looks excellent. No animation of the reading position,
typewriter delays, fake progress bars, or redraw timer on a settled idle screen.

Use a monotonic clock shared by effects, with deadlines only while an effect is
active. Animation defaults on; motion can be disabled independently of the rich
theme. Static fallback preserves the same information. Suspend motion when the
TUI hands the terminal to an editor. Keep effects to dirty rows/cells; thousands
of retained lines must not be rewrapped at spinner frequency.

## Slash interaction

Typing `/` as the first character of a fresh single-line draft opens an inline
menu above the composer. Continued typing filters names, aliases and descriptions;
exact then prefix matches lead. Show one canonical row per command, name,
description, category and applicable timing (`now` or `after turn`). Keep selection
visible in a maximum eight-row viewport that shrinks with the terminal.

Up/Down select while the menu is open. Tab fills the selected name and closes
the name menu, adding a space when arguments are accepted. Enter executes a
selected argument-free command; for a command requiring arguments it fills the
name and moves into argument entry. A completely typed valid command submits
normally. Navigation never submits. Escape dismisses without changing the draft
and stays dismissed until another real edit. Ctrl-C retains its existing stop
behavior; the inline menu must not swallow cancellation.

Recall, restored drafts, bracketed paste and programmatic draft insertion must not
spontaneously take over arrow keys. URLs and slashes inside prose are not command
triggers in this pass. A second slash/path separator closes name completion;
absolute paths and unknown slash text remain editable. Unknown commands get a
local error/suggestions, never a silent replacement. Completion replaces only
the active token, preserving arguments and unrelated text. Ctrl-P/N retain explicit
history access. The existing Ctrl-Space/Ctrl-T searchable full palette remains
available; its Enter-to-fill contract can remain distinct and clearly labelled.

Argument completion is command-specific: `/effort` offers accepted levels,
`/model` offers configured available models with current selection marked,
`/workflow` offers local workflows, `/draft` offers recovered drafts. No live
provider fetch or model request on each keystroke. JSON/Lua commands get a syntax
hint/example instead of a misleading generic picker.

One command registry must describe aliases, syntax, argument completion,
availability and timing, supplying help/menu/dispatch lookup. Runtime handlers
retain authority over effects; the UI does not invent a second configuration or
workflow queue. Plain terminal mode continues to work from the same definitions.

## Basic command behavior to finish

| Commands | First-pass work |
| --- | --- |
| `/help`, `/keys`, `/commands` | Concise, categorized discoverable help; searchable full palette; honest key hints. |
| `/model [NAME]`, `/effort [LEVEL]`, `/workflow [FILE]` | Bare forms open pickers/show current value; argument forms stage changes at the existing safe boundary. Mark pending versus effective settings. |
| `/session`, `/sessions`, `/stats` | Readable compact local result blocks rather than diagnostic dumps; do not imply `/sessions` already switches sessions. |
| `/queue`, `/cancel` | Clear pending count/list; cancel identifies what it stops/clears. Keep immediate TUI behavior. |
| `/clear` | Explicitly display-only; do not erase model context. |
| `/edit`, `/drafts`, `/draft N` | Preserve existing editor/recovered-draft behavior; make draft choice discoverable. |
| `/quit`, `/exit` | One canonical row, alias search; prompt worker stop and draft persistence. |
| `/restart [NOTE]` | Optional note with sensible default; show honest restart status and require existing quiescent refit behavior. Does not build the executable by itself. |
| `/context`, `/originals`, `/compact`, `/inspect`, `/restore`, `/lua` | Keep real existing capabilities, improve syntax/help and result presentation. No advanced control redesign in this pass. |

Add `/theme` and a motion setting only alongside functioning presentation
configuration; do not advertise unfinished commands. Existing terminal keybindings
remain the starting point. Turn-boundary changes are not approval dialogs.

## Implementation sequence

1. **Visual shell and motion.** Semantic styles, compact header/status, responsive
   composer geometry, focus/selection treatment and actual animated activity.
   Show a local native terminal preview using representative fixtures before
   proceeding. This is the first tangible improvement, not a final polish task.
2. **Inline slash menu and basic commands.** Shared metadata, `/` discovery,
   arrow/filter/selection behavior, argument completions and bare forms above.
   Keep busy timing and composer/paste/history semantics intact.
3. **Conversation rendering.** Typed presentation events from runtime producers,
   message blocks, Markdown/code treatment, compact expandable tool groups,
   readable errors and stable scroll/follow-tail behavior.
4. **Tune the whole experience.** Inspect actual local sessions in wide/narrow,
   dark/light and phux terminals; adjust contrast, density and animation. Measure
   output bytes/CPU under streaming and long retained history; remove waste.

Each is one coherent implementation unit with a written sub-plan, direct tests,
review/fix and one bounded recheck. Hardening stops after two layers, capped at
25 minutes; new worthwhile findings become backlog. Land/push each accepted unit
on main and rebuild locally. No campaign restart or multiplayer launch implied.

## Evidence and performance

Use direct composer tests for menu selection/filtering/alias/paste/history and
exact dispatch counts. Use the existing PTY harness to verify rendered cells,
cursor location and command behavior under resize, active output and interruption.
Small physical sizes must remain within actual bounds rather than clamping the
terminal upward. Include CJK/combining characters and escape/control bytes.

Animation tests advance a fake clock and assert visible phase/state transitions,
settled idle silence, and unchanged conversation wrap count on spinner ticks.
Scrolling tests assert the visible message anchor survives streaming/collapse;
follow-tail and unseen output are explicit. Real terminal inspection judges
appearance; snapshots alone cannot establish that it looks good.

Cache completed block layout by width/style generation. Reflow changed blocks,
not full history per token or animation tick. Keep bounded display projections
with references to retained originals, no duplicate durable transcript. Changed
rows/cells drive terminal writes; batching coalesces bursts without delaying
input. Record measured time/bytes, never claim efficiency from architecture alone.

Advanced task drawers, fleet dashboards, multiplayer rooms, programmable control
surfaces and session graph navigation remain subsequent design work.
