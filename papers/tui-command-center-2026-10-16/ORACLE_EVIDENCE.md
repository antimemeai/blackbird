# Actual-main and readable-screen oracle evidence

The integrated PLAN precedes these fixtures. This owner changes only
scripts/check-tui-center, the existing singular tests/tui_snapshot_test.cpp,
exactly two new expected screen files, and its episode/evidence notes.
Root owns actual source/CMake/terminal/adapter integration and the independent
one-pass review. The actual-main fixture was RED before implementation; native
builds and the product implementation belong to root and the other owning units.

## Actual old-main behavioral RED

Executed before root implemented the center:

```sh
python3 scripts/check-tui-center build/debug/blackbird \
  --artifacts context/tui-command-center-2026-10-16/oracle/old-main
```

The actual executable was
530b831eb3748580aeeb13c2e3d729ef9e6394e4375eaa7f7b87bc600559cc99.
The pinned Ghostty driver launched it successfully and recorded the nonempty
DRAFT_CENTER_SENTRY draft with two cursor-left events. Ctrl-X24 was sent. The
permanent literal COMMAND CENTER expectation failed locator_no_match after its
existing four-second allowance. This was behavior, not driver/build/setup error;
owned session close succeeded. No fixture workflow was submitted before failure.

Exact log: `context/tui-command-center-2026-10-16/oracle/old-main-red.log`.
Identity, before-open state/cursor, command timeline and failure records remain
in `old-main/ghostty-11e588e0b4/`; the initial driver source copy is
`oracle/check-tui-center-red.py`. Later driver expansion preserves that first
nonempty-draft Ctrl-X oracle before any seed effects.

## Full fixture prepared after RED

The real-main fixture uses the existing local fake-Claude PATH transport. Two
actual participants return distinct answers and remain waiting; one is unbound.
A real command writes24 result rows, waits at a fixture release barrier and
exits7. A second explicit setup command writes the actual returned task/run/job
identities to a private fixture ledger. Its own returned job ID is emitted by
the seed workflow, whose source is not echoed as the submitted prompt. Owner
IDs/order, launch and request ledgers supply independent expectations.

The center must show declared blocked state/blocker, participant waiting/answers,
and actual exit7/output without inventing command/task binding. Mid-cursor draft,
nonzero result reading position, independent jobs-route anchor, narrow resize,
literal pasted controls and /center are exercised. Browsing cannot relaunch the
two setup jobs, call the fake provider again, join waiting participants or mark
the declared task done. Assertions target the literal detail rectangle rather
than any matching text in chat. The existing45-second body watchdog reserves
cleanup within the existing60-second CTest; fixture barriers poll briefly rather
than depending on multi-second sleeps. Cleanup preserves fixture ledgers/source
and command history with failure artifacts.

The two new readable cases use actual CommandCenter and a real TaskState prepared
with a blocked task under fixed valid environment/conversation identities.
Participants/commands are explicitly unavailable. Wide100x16 shows the actual
adapter; narrow32x12 focuses detail and makes the task callback busy, retaining
last-good content and the adapter notice. All previous16 expectations remain.
Both literal grids were then generated from the implemented adapter and inspected.
Wide100x16 shows Task t1, Declared blocked, the owner label, exact task revision,
blocker and four source lines within the settled split. Narrow32x12 shows retained
blocked task detail under an explicit stale notice; wrapped blocker content stays
inside the detail rectangle. Exactly these two expectations were adopted; all16
prior expectations remain unchanged. All18 styled comparisons pass.

Readable generation/comparison logs are `oracle/readable-generation.log` and
`oracle/readable-comparison.log`; literal generated grids remain in
`oracle/readable/`.

## Actual-main GREEN and one oracle challenge

The implemented actual executable SHA256 was
567727a915decb36757b956343eec704a0b7cae77d3164508cf8a7e543edbc52.
The complete Ghostty episode passed in `oracle/green/ghostty-7feeef48ce/`;
Alacritty passed in `oracle/green/alacritty-cc0a99408c/`. The corresponding
`ghostty-green.log` and `alacritty-green.log` preserve the commands' outcomes.
These are actual native launches with two separately recorded owner IDs,
participant answers, command exit7 and independent private fixture ledgers.

Root's single independent oracle challenge identified a real fixture race:
identifier-anywhere could mistake a still-visible bound participant's Task: t1
body for adoption of the task. The permanent select helper now takes the
fixture-declared kind and ID and waits for the exact literal detail title row,
Task t1 / Participant <actual ID> / Command <actual ID>, ignoring only leading
focus marker/padding. It does not consult a product selection field or compute
expectations from the adapter. Semantic body assertions follow that adoption.
Both complete episodes passed again with the strengthened oracle:
`oracle/challenge-green/ghostty-bc10af25a9/` and
`oracle/challenge-green/alacritty-11feec862b/`; logs are
`ghostty-challenge-green.log` and `alacritty-challenge-green.log`. Earlier runs
are retained as earlier evidence, not relabeled.

## Single integrated input/main/terminal review

After those GREEN episodes this owner inspected the root integration changes in
main.cpp, terminal.hpp and terminal.cpp, with the existing sole Composer,
foreground-PTY path, task publication and the command-center public calls in
context. The actual main supplies stable environment/conversation scope and a
reader whose owner outlives the UI loop. Foreground PTY processing remains ahead
of Composer. Paste handling precedes route controls. Only one inspection route
is open; switching closes the other without replacing its retained controller.
The one Composer retains byte cursor/draft. Refresh happens outside input and
paint, while task observation uses the actual UI mutex with try-lock and explicit
uninitialized/busy/stale publication; failed publication stays stale until a
successful snapshot.

One concrete finding was sent to root: toggle/first_page receive the current
InputResult source, but navigation and source-page calls do not. CommandCenter's
4089 then retains the opening Ctrl-X origin for a later Down/Ctrl-Right-induced
adoption, contrary to the plan's actual-input provenance claim. The proposed
small correction passes an optional raw input origin through act/page and updates
it when scheduling the refresh. Product edits and the permanent provenance test
belong to root/the adapter owner. No second review panel was requested.

Root final rebuilt-source counterpart: the registered Ghostty center passes in
20.51s inside the final9-case integration selection; unchanged18 readable cases
and four blocked-output episodes pass too. Root's forwarding fix uses actual
Composer result.source.record in center act/page; the native raw case independently
resolves its current Home4010 origin in4089. Full-source Alacritty recheck also passes (final-source/alacritty-0b9f4ba13f).
The normal installed hook remains integration-owner work.
