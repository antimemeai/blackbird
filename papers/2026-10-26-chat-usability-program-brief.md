# blackbird chat usability, polish, and stop-embarassing-ourselves

Operator-requested next program, 2026-10-26. Preserve this exact title, including
its spelling, in the project Beads epic. Requested state: priority P1, open,
not started. Filing attempt with bd 1.3.1 is blocked by the legacy 0.58.0 Dolt-server
workspace; no issue ID has been created. This is the retained program/issue brief,
not a replacement issue tracker or a claim of successful filing.

## Sequencing

Finish and safely settle the presently in-flight continuity and S3a composition
units first, retaining their actual gates, findings, source and publication state.
Then this program is NEXT, ahead of automatic continuation into S3b, generic
backlog cleanup, provider/linker work or other new implementation units. Do not
interrupt the active workers, and do not wait for the entire old backlog. A
necessary specific blocker to settling an in-flight unit stays with that unit;
it is not permission to launch another general campaign first.

## Operator's actual experience

The chat UI is confusing. Escape does not actually interrupt the active turn.
Submission state was unclear enough that the operator sent four or five similar
messages to their model partner because they could not tell one was already sent.
Tool-call output poured into chat needs readable pretty printing and automatic
collapse, with a clear mechanism for inspection when warranted. These are examples
of a wider usability failure, not an exhaustive bug list or a request for isolated
cosmetic changes.

Treat this report as an actual operating failure. Inspect the exact installed and
running revision alongside source; old folding/input/navigation delivery reports
do not establish that the operator's present interface is usable. Reproduce the
reported experience before deciding whether a capability is absent, hidden,
incoherent, stale at runtime or broken.

## Required approach — whole program, not patchwork

Start with current reference implementation review and UX/HCI/design study, read
actual source and primary material, and acquire relevant reference source into
quarantine with exact provenance/restoration records. Reuse and challenge the
existing INTERACTION_THEORY, TUI_DOCTRINE, component/view designs, prior TUI studies,
corpus assessments and deferred findings rather than repeating acquisition blindly.
No framework/library adoption or copied third-party implementation by implication.

Map real operator tasks, visible states, feedback, control/focus routing and
failure/recovery paths. Identify the broader list with a systematic usability
walkthrough. Develop a coherent interaction model and a holistic design, include
clear behavioral contracts and concrete mockups/prototypes, pressure-test design
and delivery plan, then implement whole conceptual tranches under BLACKBIRD.md.
Do not implement the first obvious fix before the source/reference/design work.

## Initial design obligations

- Interruption: Escape must provide the expected usable turn-interrupt control,
  with clear scope, acknowledgement and observed stopping outcome. Resolve conflicts
  with composer editing, palettes/modals, selections and foreground PTY input.
  Preserve draft and native known/unknown effect semantics; cancellation is not undo.
- Message lifecycle: make draft, accepted/sent, queued, active, delivered/finished,
  failed and interrupted distinctions unmistakable as relevant to actual native
  state. Operator prompt and partner reply attribution must be clear. Prevent
  uncertainty-driven resubmission without silently dropping intentional repeated
  messages or inventing remote delivery/completion claims.
- Tool calls/results: format structured/native values and text for comprehension,
  not a serialization dump. Collapse tool activity/results by default while keeping
  identity, purpose, state and important outcome visible. Provide discoverable,
  efficient keyboard/mouse expansion and full/ranged original inspection with
  reliable focus/scroll/return behavior. Distinguish presentation truncation from
  full retained originals; do not rerun an effect merely to inspect output.
- Coherent chat interaction: study composer/focus, keyboard discoverability,
  send/newline behavior, queues/steering, working/waiting/error indicators, transcript
  hierarchy, scrolling/anchors, responsive layout and accessible legibility together.
  Identify further defects from actual use rather than claim this list is complete.

## Delivery and acceptance

The first deliverables are the source-grounded reference/UX study and reviewed
holistic design/plan, followed by implementation and actual-use qualification.
The design must tie each visible state/control to a real owner and outcome, and
explain the behavior under active inference, queued messages, long/failed tools,
cancellation, slow output, resize, route/focus changes and reopen.

Use appropriate semantic/native/physical PTY and readable-screen oracles, actual
operator-task walkthroughs, old-behavior REDs for reported regressions, preserved
originals and interruption correctness, required source diagnostics/host checks,
and matched TUI performance deltas where behavior changes. Unit tests alone do
not establish usability. Respect one remediation/review layer and one recheck/fix
layer; no recursive assurance, arbitrary mission/turn budgets or laptop mutants.
Source publication, master integration, installed runtime activation and user-
observed improvement are separate, explicit outcomes. Finish with an actual
usable chat workflow, not merely more test cases, screenshots or a theme.
