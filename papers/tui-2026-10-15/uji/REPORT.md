# uji and Ito: programmable presentation without surrendering responsiveness

2026-10-15. Source study of the six public repositories returned by the
uji-labs organization listing during acquisition. Exact revisions and archive
identities are in [sources.tsv](sources.tsv), restoration in
[MANIFEST.md](MANIFEST.md), and captured public pages in
[documents.tsv](documents.tsv). This is a supplement to the Blackbird repository's
reference shelf. It changes no product machinery.

The strongest reference is the division between a small native host and views,
controls, composition and policy expressed in Lua. The interesting question for
Blackbird is how to give the operator and model that much expressive control
while keeping input, cancellation and terminal transport responsive when a view
or consumer misbehaves. No comparative benchmark was run here.

## What was acquired

All paths below are relative to `quarantine/tui-2026-10-15/uji/source/`.

| Repository | Material and study scope |
| --- | --- |
| `uji` | Rust kernel; Lua agent, UI and APIs; documentation; tests and benchmark scenarios. Main source reviewed for UI/task/reload boundaries. Root license GPL-3.0-or-later. |
| `ito` | Lua declarative views/state/layout/controls/window; Rust cell/image/input/terminal adapters; tests and browser workshop source. Root license MIT. |
| `uji-plugins` | Statusline views, themes, live fuzzy picker, plan-mode policy, Claude process adapter. Read statusline/picker and documented extension seams; provider adapter acquired for later BHI work. |
| `uji-skills` | Only README in this snapshot. It describes a skills layout; this is not an acquired collection of populated capability folders. |
| `homebrew-uji` | Formula and release/distribution documentation; retained for restoration context, not installed. |
| `copr` | Fedora packaging and build definitions; retained for provenance, not run. |

No root license file appeared in the four supporting repository snapshots. That
is an acquisition observation, not an inferred license. Bundled third-party files
can carry their own licenses. All six original ZIPs remain intact. Embedded
instructions, installers and hooks have no authority over this checkout.

## Mechanisms worth learning

Source anchors below identify the captured revisions through the manifest;
function names are more useful than moving upstream line numbers.

1. **Views are ordinary programmable composition.**
   `uji/lua/uji/core/ui/screen.lua` supplies greeting/transcript/composer/modal/
   activity slots to a Lua `Screen` view. `core/ui/render.lua` assembles toolbars,
   floating presentations, selection and notices. The normal composition has a
   plain-screen fallback if placement fails. For Blackbird, a Lua screen should
   compose owned semantic data rather than parse status prose into widgets.

2. **Read dependencies trigger invalidation.**
   `ito/lua/ito/runtime/state.lua` records reading nodes in weak-key tables.
   Changing `.value` notifies readers only when the stored value differs and
   invalidates each affected composition. `runtime/composition.lua` reuses a
   placed result when element/environment identities match and the node is
   neither stale nor invalid. This is a concrete mechanism, not a guarantee that
   arbitrary Lua tables or all layouts update incrementally. In-place mutation
   of a value without an observable write needs explicit semantics.

3. **Identity makes dense, changing lists usable.**
   `ito/lua/ito/controls/list.lua`, `item_id`, `identities`, `cursor` and `arrange`
   keep chosen/first item identities and reject duplicate or missing IDs.
   `rows` composes rows while the visible height permits. However `identities`
   still walks all items, and variable-height selection adjustment may recompute
   row placements. Lazy painting alone does not establish constant-cost updates.
   Blackbird needs stable task/job/message identity when a stream inserts rows
   above a selection or an encoder moves through an evolving list.

4. **Focus and control routing are explicit.**
   `ito/lua/ito/window/init.lua`, `settle`, `key`, `draw` and `panes` route keys to
   the focused control, then scoped handlers, and maintain scroll targets.
   A focus transition can cause a second draw before presentation. The tests
   `ito/tests/window_spec.lua` check pointer/keyboard routing and selection while
   scrolling. This is useful grounding for a unified keyboard/controller focus
   model; focus policy is more than a list of global shortcut strings.

5. **Actions and bindings have separate names.**
   `uji/lua/uji/core/ui/keymap.lua` stores six mode maps and resolves a canonical
   chord ID. `docs/src/api/action.md` and `keymap.md` expose named actions,
   callable handlers, commands and introspection. Lookup is one mode/chord map;
   this is not a general simultaneous-chord, multi-step sequence or analog-input
   recognizer. Named actions are the useful seam for multiple input devices.

6. **Live results are tied to the requesting generation.**
   `uji/lua/uji/core/ui/views/pick.lua`, `query_later`, increments a generation
   after its debounce and admits a `show` callback only when that generation
   remains current and the modal answer is unsettled. An older result can still
   arrive before the next debounce finishes. `uji-plugins/lua/telescope/init.lua` starts ripgrep via jobs and
   limits collected live-search results to 200. Preventing a stale result from
   replacing the current list is different from terminating obsolete work.
   Its file lister passes no collection limit. A Blackbird implementation should
   specify both cancellation and bounded result delivery, including partial
   results and a path to the rest; do not inherit 200 as a design constant.

7. **Semantic styling remains composable.**
   `uji-plugins/lua/statusline/init.lua`, the theme files, and
   `uji/docs/src/configuration/themes.md` provide useful material for composing
   model/effort/context/status segments. Empty segments do not need separators.
   `uji/docs/src/api/ui.md` documents toolbar placement and isolated diagnostics
   for failing toolbar items. This supports high information density through
   meaningful hierarchy and optional detail rather than filling every cell.

## Where a responsive-looking architecture can still stall

| Source boundary | Observed mechanism | What remains to establish for Blackbird |
| --- | --- | --- |
| `uji/.../core/ui/init.lua`, `frames`, `read`, `render` | Independent cooperative input/frame tasks; dirty promise; 1/60-second sleep after frame processing. | The sleep is a pacing constant, not a measured 60 FPS or an input-latency bound. A long render/callback still occupies its task's execution. |
| `ito/src/tty/real.rs`, `present`, `write` | Ratatui cell flush, backend flush and `write_all` execute synchronously. | Slow or stopped PTY consumption must not prevent dispatch of interrupt/control. Separate input acquisition alone does not ensure that. |
| `ito/src/tty/real.rs`, `open`; `input/reader.rs` | A reader thread sends through an unbounded channel. Its poll timeout is 100ms. | A ready descriptor can wake before that timeout; do not report 100ms as key latency. Floods need queue age/memory/fairness characterization. |
| `ito/src/tty/input/reader.rs`, `settle` | Terminal handoff uses a 120ms sleep; drop changes a running flag without joining the thread here. | Account for actual reader ownership and handoff completion, especially repeated suspend/resume. A sleep is not an acknowledgement. |
| `ito/src/tty/event.rs`, `key` | Key releases are discarded; repeat is retained; unsupported key codes return no event. `real.rs` requests escape disambiguation and alternate keys. | Hold-to-act, pedal release, rich physical-key identity and arbitrary chords need a richer event model and negotiated support. |
| `uji/crates/kernel/src/task.rs`, `start`, `timeout` | Lua async calls run through local cooperative tasks; cancellation aborts the future and timeout races it with a timer. | A synchronous non-yielding callback is not preempted by this race. No native or Lua UI callback should inherit an unsupported hard responsiveness claim. |

These are source-observed boundaries and proposed falsification targets, not
reproduced user-visible hang reports. In particular, Blackbird's current
nonblocking `ChatOutput` is already stronger than Ito's synchronous transport on
this specific axis. Preserve that property when experimenting with Lua views.

## Reload and BHI relevance

`uji/lua/uji/core/config.lua`, `reload`, refuses an active turn and reconstructs
resume arguments, carrying a composer draft into a VM restart. The native kernel
`crates/kernel/src/kernel.rs`, `drive` and `live`, recreates the Lua state while
reclaiming the terminal. The model-facing
`lua/uji/builtin/tools/reload_config.lua` schedules reload after a turn and reports
config-loading notices afterward; `crates/tests/lua/reload_spec.lua` checks that
feedback. The distinction between operator reload-now and model reload-after-turn
deserves study in the BHI pass.

This is a useful example of closing the model's edit/reload/observe loop. It is
not evidence for arbitrary native-code reload, preserving all live Lua stacks,
or Blackbird's own refit requirements. Its JSON carry/state conventions also do
not supersede Blackbird's owned native representation rule.

## Verification material and limits

`ito/tests/composition_spec.lua` directly checks preserved state, keyed
reordering, cache keys and invalidation. `window_spec.lua` checks focus and
selection behavior. Those are worthwhile oracle patterns to study, but these
upstream tests were not run here.

`uji/bench/scenario/scenarios.json` includes long history, large drafts/pastes,
per-character streams, long tool output, scrolling, and large files. `bench/steps.lua`
uses test support UI/agent/server machinery; `bench/runner.lua` batches samples,
takes medians, and gives torture cases cooperative timeouts. They are unusually
relevant workload ideas. They do not supply measurements on our host, physical
key-to-photon latency, slow real terminal writes, MIDI, or a preemptive watchdog
for non-yielding Lua. Nothing in this report establishes uji as faster or slower
than Blackbird overall.

For the next pass, compare small owned C++ primitives plus Lua views against the
same stress traces: slow terminal, stale live search, expensive view, key-release
loss, changing keyed list, tiny window, width disagreement and reload with an
unsent draft. The complete cross-lane agenda belongs in the parent synthesis.
