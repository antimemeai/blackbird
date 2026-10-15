# Integrated BHI design challenge

2026-10-15. One bounded conceptual challenge of `TUI_DOCTRINE.md`,
`TUI_VIEWS.md`, `TUI_COMPONENTS.md`, `TUI_IMPLEMENTATION_PLAN.md` and
`TUI_METRICS_VIEWS.md`. Checked their referenced existing-workflow contract where
needed. No product changes, tests or review of reviews.

## BHI-1 — P2: specify the boundary between literal input and submitted workflow selection

**Location:** `docs/TUI_DOCTRINE.md:130`, with preservation obligations in
`docs/TUI_IMPLEMENTATION_PLAN.md:39` and the existing
`docs/CALLABLE_WORKFLOWS.md` Matching and presentation contract.

**Defect:** the doctrine says text-entry/paste/IME contents are not a macro
language, without limiting that rule to native input handling before deliberate
submission. Blackbird currently supports explicit bare command selection and
configured POWERWORD tokens anywhere in submitted operator text, including
quoted/code text. Those are retained workflow-selection semantics, not terminal
key decoding. The proposed input extraction is required to preserve current
controls, but the absolute doctrine sentence can also be read as abolishing this
accepted expressive path. An implementer cannot derive one unambiguous behavior
for a pasted prompt containing a configured POWERWORD followed by explicit Send.

**Direct fix:** say that committed text, paste and IME input enter the native
draft literally and never become key/action events while being decoded or
edited; explicit submission still invokes the declared command/workflow grammar,
including current configured bare names and POWERWORD behavior. Any intended
change to that submitted grammar should be an explicit product/design change,
not a side effect of extracting input controls. This preserves both the input
separation and the already-authorized expression mechanism.

**Direct acceptance:** pasting a registered token edits the draft without
dispatch; explicitly submitting the draft selects the same effective workflow
as the current documented matcher, with original text and trigger retained.
Native input handling does not interpret pasted escape bytes as command keys.

No other substantive BHI defect found in this pass. The integrated documents
separate presentation from runtime activation, preserve model/Lua semantic
operations without screen control, identify unsupported backends, and add no
routine approval gate.
