# Command-run list paging results

2026-10-17. The native result instrument now pages through command runs using
its existing observer continuation IDs. Collection focus chooses run-list pages;
detail focus chooses output windows. Reads stay bounded to eight rows, 4096 bytes
and 16 source spans. This changes display and selection reach, not execution
limits. User-facing adapter text says command run; existing owner/API identities
remain unchanged.

The adapter keeps separate requested, observed, input and adopted-scene cursors.
Forward navigation consumes only a continuation belonging to the displayed page.
Pin and interrupt require matching ready observation and displayed-page identity;
a new page cannot reuse the old page's selected target/window while Lua is pending.
Backward and first-page recovery do not wait for Lua. Immutable A/B bytes and
source references remain independent of list/output navigation. The new
`focus_view()` restores shell route focus without closing/reopening the instrument
or changing collection/detail focus.

## Direct RED and GREEN

Before product changes, the new `tui_instrument_test paging` scenario executed
17 actual native commands (16 finite printf runs and one live child), then failed
with exit 1 at:

> forward list paging must expose command runs nine through sixteen

Saved original: `context/tui-navigation-2026-10-17/paging-red.log`.

After implementation, the seven instrument modes pass in the dedicated
`build/paging` Debug directory: semantic, source, stale, mapping, recovery, raw,
paging. Final run: 7/7, 1.22 seconds. Configure/build/test logs are under the same
ignored context directory (`paging-configure.log`, `paging-green-build.log`,
`paging-green-tests.log`).

The paging oracle checks literal ordered identities on pages of 8, 8 and 1 rows,
authored `row-...` presentation keys resolving exact native targets, rapid repeated
forward navigation, forward/back exhaustion, first-page reset during composition,
correct selected output bytes, both immutable pins, an actual second output window,
and rejection of stale pin/interrupt actions. A failed next-page Lua composition
retains the exact last-good scene while native focus and backward recovery work.
Existing fixed DATA/CONTROL capture-pressure tests now also exercise native focus.
The prior mapped-action test explicitly focuses detail and emits more than 4096
bytes, making its stale-window assertion concern a real second output page.

## Bounds and remaining qualification

Navigation retains 33 optional cursors, matching the existing native inspector's
bounded resident-view navigation. This is recent owner-resident work, not retained
history lookup. A/B pin windows remain bounded snapshots. A temporarily busy or
unavailable owner leaves the last scene visible and refuses actions needing a
fresh matching observation; local focus, backward/reset and source recovery remain.

This report claims native adapter behavior only. The root owns one independent
source review, shared shell integration, readable-screen and Microsoft episodes,
sanitisers, matched tranche performance and final source qualification. No live
provider/session activation, dependency adoption, mainline merge, artwork work or
AFL inspection occurred in this unit.
