# Shared native view navigation

2026-10-17. Independent source study and proposed next implementation slice,
read against `cbb206b629d936c13c9636b0a7bac999fa68e3e9`. The operator authorizes
repairing command-run list paging, then continuing shared shell/navigation and
named controls. This note supplies that second slice; its source is not yet
implemented or qualified by this report.

## Purpose and useful episode

An operator leaves a partly edited prompt, opens the list of command runs,
compares output, checks current-session work, and returns to precisely the same
prompt. They can discover these views without memorizing three unrelated keys.
Navigation stays native when Lua composition fails or terminal output stalls.

Build one small shared navigation owner and a typed view picker across the four
existing working surfaces. This is the first application shell component. It
provides named presentation actions for later binding work without waiting for
the complete programmable input/device system.

## Meaning of names

| Name | Concrete meaning |
| --- | --- |
| Command run | One command execution, including its process state, output and controls. Existing native `CommandJobs` types and wire fields may retain their names in this slice. |
| View | A working surface: Conversation, Command runs, Command center, or Result comparison. |
| Route | A native value identifying which view is active. It grants no execution authority. |
| Action | A named intention accepted by an owner. This slice's route actions only change local presentation. |
| Binding | A mapping from an input event in a scope to an action. This slice supplies existing/default aliases; configurable gesture policy remains later work. |
| List page / output page | Respectively a bounded group of command-run entries and a bounded byte window from one run. Keep their controls/help separate. |

Use plain view labels in the picker. Existing route captions may remain
`COMMAND RESULTS`, `COMMAND CENTER`, and `RETAINED RESULT LAB` to preserve
understood presentation baselines. Avoid importing implementation container
names into help. The earlier paging repair must describe extra command runs as
additional list pages, not a capability or execution limit.

## Source grounding and chosen boundaries

- [Terminal interface](../../include/blackbird/terminal.hpp) currently combines
  `InputAction`, `InputResult`, `ComposerChoice`, editor state and palette state.
  Add typed route payloads without renumbering existing `InputAction` values:
  captured raw events retain their current interpretation.
- [Terminal loop](../../src/terminal.cpp), `TerminalUI::run`, maintains three
  independent adapter open flags and repeats close-other/toggle/first-page
  dispatch for keys and submitted slash commands. Centralize the decision about
  the active view and route every entry point through it. Retain the existing
  adapters as owners of their selections, pages, detail offsets and Lua state.
- `Composer::choices` supplies a useful native picker shell, but palette Enter
  currently produces `choose_command`; the terminal saves/replaces the prompt.
  A route choice must produce a typed local action instead. `/commands` and
  session choices keep their existing explicit command-loading behavior.
- `Composer::feed_impl` currently processes inspecting arrows/function keys
  before ordinary palette handling. A modal view picker must consume its own
  navigation first. Otherwise hidden collection movement or F8 interrupt can
  occur behind the picker.
- [Terminal custody](../../src/terminal_custody.cpp), `TerminalCustody::enter`,
  disables `ISIG`, `IEXTEN` and `IXON`. An unused Ctrl-\\ byte (`0x1c`) can
  therefore open the picker in ordinary Blackbird input without becoming
  SIGQUIT. The private-PTY input branch precedes Composer and must keep sending
  that byte to its selected child. Existing Ctrl-B retains its meaning there.
- [Inspector](../../include/blackbird/command_inspector.hpp),
  [center](../../include/blackbird/command_center.hpp) and
  [comparison](../../include/blackbird/tui_instrument.hpp) retain local state
  across toggles. `first_page` explicitly resets some paging state. Distinguish
  resume from reset in the coordinator; ordinary picker navigation must resume.

The [theory](../../docs/INTERACTION_THEORY.md), especially stable meaning and
continuity, explains the benefit. The [doctrine](../../docs/TUI_DOCTRINE.md)
requires native control, literal paste, stable identities and preserved drafts.
The [component contract](../../docs/TUI_COMPONENTS.md) separates scoped input,
named actions and domain authority. The [coordination design](../../docs/TUI_COORDINATION_VIEWS.md)
requires persistent drafts and meaningful route focus. The existing
[input source study](../tui-2026-10-15/input/REPORT.md) grounds device-independent
action identity and protocol limits; the [component source study](../tui-program-2026-10-15/components/REPORT.md)
grounds keyed state and explicit focus settlement in acquired Ito references.
Those studies do not select a production library or qualify a new key protocol.

## Concrete implementation scope

1. Add `TuiRoute` and a finite owned descriptor table with stable names
   `view.conversation`, `view.command-runs`, `view.command-center`, and
   `view.result-comparison`, human labels, descriptions and current availability.
   Named values carry no closures, arbitrary string commands or domain targets.
2. Add `/views` plus Ctrl-\\ as picker entry points. They open the same native
   picker. Ctrl-\\ preserves draft bytes, cursor, editor history, prompt queue
   and invoking route/focus. `/views` is a deliberately submitted local command,
   never a provider request.
3. Extend palette choices with an optional typed route action and append a
   dedicated input result action. Enter on that choice applies navigation
   directly. It does not load a slash command, save a duplicate recovered draft,
   enqueue a prompt or send a model turn. Existing generic command choices keep
   their behavior. Search, arrows and dismissal work in the picker itself.
   Append the new action after all existing enum entries. Preserve its stable
   action name in the existing raw input-action capture at site4010 so a typed
   route selection is recoverable without treating a label as dispatch authority.
4. Coordinate activation in one native helper. Existing Ctrl-O/Ctrl-X/Ctrl-]
   remain toggle aliases; existing `/jobs`, `/center`, `/instrument` preserve
   their explicit first-page entry behavior. Picker selection resumes the chosen
   view without calling `first_page`. Selecting the already active view focuses
   it rather than toggling it closed. Conversation selection restores the editor.
   Add a narrow native `focus_view()` adapter method where needed to regain
   collection/detail control without resetting pages, selection or pins.
5. Before leaving the current view, prepare the requested adapter and verify it
   is available. Refusal keeps the current route, editor and selection intact
   and displays the concrete reason. Do not set the route enum and then discover
   that the selected adapter could not be created.
6. Use the common descriptors for picker labels and new navigation help. Expose
   current route/selection in the picker. Retain present layout, task pane,
   artwork, status and captions; this slice need not reserve another terminal row.
   Closing the picker restores its invoking route/focus, including detail focus
   where it existed. Ordinary Escape-to-draft behavior outside it remains.

One native owner decides the active route. Adapter open flags can remain
compatibility projections during this extraction but must not become a second
independent route authority. Navigation and preparation do no source scan,
provider call or synchronous Lua work. Existing async refresh remains outside
input handling and painting. No process lifecycle or action receipt changes.

## Direct REDs before source

Write expected behaviors, then run the unchanged executable on the new episode.
Old main must fail the missing picker/navigation expectation, not a helper import
or newly introduced function lookup. Keep those observations with this tranche.

Root reports the initial direct REDs: native `terminal_test` fails the missing
picker expectation at line45, and unchanged actual main times out awaiting
`Views` after Ctrl-\\, with its full trace retained. These reports establish the
missing entry point; the remaining behavioral claims below require their actual
GREEN oracles after source work.

| Fault class | Direct oracle |
| --- | --- |
| Navigation consumes draft | Actual main: type a Unicode/multiline prompt, move its cursor internally, open Ctrl-\\, select Command center, then Conversation; insert a marker and require exact final draft bytes/cursor. Provider/effect ledgers remain empty. |
| Picker selection becomes command submission | Select each available route through typed choices. Assert no recovered-draft duplication, queued prompt, command text in editor, model turn or external effect. Separately retain the existing `/commands` Enter-load-then-explicit-send test. |
| Hidden input leaks through modal | Open picker while a collection/detail is focused; arrows change only picker selection, typing changes only picker search, and F8 does not request interrupt of the underlying command. Paste containing Ctrl-\\/Ctrl-O/Ctrl-X stays literal. |
| Route preparation destroys useful state | Native coordinator tests refuse an unavailable route and require the old route/focus/selection unchanged. Select the already active route and require it remains open. |
| Navigation resets inspection | Actual main: inspect a later list page and later output window, switch to another view and back through picker, require the same actual selected ID, window and visible source. A comparison's A/B pins remain immutable. |
| Escape/cancel ambiguity | Dismiss picker and require invoking focus/draft restored. Keep existing Ctrl-C stop and Ctrl-Q quit semantics; native stop still reaches the actual owner with stdout blocked. |
| Child control interception | Private-PTY fixture echoes Ctrl-\\ as child input; no view picker appears. Ctrl-B still releases foreground ownership. |
| Layout/recovery | Microsoft Ghostty and Alacritty episode checks picker wide/narrow resize, cursor and full viewport; readable owned expectations cover picker labels, selected style, empty search and return. Tiny fallback still exposes existing essential controls. |

Do not manufacture an extra general input protocol suite for these route values.
Keep existing terminal/editor/raw-UI/custody checks relevant to touched boundaries.
Use actual owner IDs and effect ledgers where claiming retained identity or no
dispatch. Screen expectations establish presentation, not process authority.

## Integration, costs and exit

Finish command-run paging first; it changes comparison list behavior used by the
resume oracle. This shell slice has no dependency on the still-missing retained
CodingEngine operation locator, general hook/configuration owners or devices.
Do not combine those units into navigation work. Route descriptors are a finite
table; lookup/picker work is bounded by existing view count, never session history.
Retain one composer and existing adapter storage rather than duplicating drafts
or command observations into the shell.

Run affected native/raw/PTY cases, both Microsoft backends, reviewed readable
baselines and relevant sanitizers/Linux qualification, followed by the installed
full source gate. Perform one independent implementation review, fix findings,
and recheck the changed behavior within the existing two-layer bound. No review
of the review or unrelated audit. Track ordinary bugs in the existing queue.

Run the required matched Linux Release performance round against the preceding
paging tranche using the unchanged historical observer. A new view-picker
scenario establishes its own baseline and cannot claim a pre-feature speedup.
Preserve every attempted sample/failure and derive summaries outside production.
This work does not reopen parked resize-stage decomposition. AFL and user
artwork remain untouched. Source commit/push, main integration and running
executable activation retain their existing separate states.

Delivery means useful native navigation and named route actions across current
surfaces. Configurable binding definitions, enhanced keyboard decoding,
sequences/chords, binding explanation, device adapters, reusable forms and new
domain workbenches remain explicit next units. Carry this route/action component
into settings, Context and initial team surfaces rather than adding more
one-off toggle branches.
