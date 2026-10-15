# Integrated interaction design challenge

2026-10-15. One bounded conceptual reading of `TUI_DOCTRINE.md`, `TUI_VIEWS.md`,
`TUI_IMPLEMENTATION_PLAN.md`, and `TUI_EXISTING_VIEWS.md`. No review of reviews,
product tests, or rereview of this lane's coordination research. Locations below
refer to this reading; subsequent edits may move line numbers.

The stack substantially preserves the theory: doctrine is explicitly normative,
layouts are proposed compositions, unknown capability is not silently invented,
models operate without screen coordinates, and the plan includes one authored
instrument that demonstrates newly possible work. Delegation does not require
continuous monitoring. No broad theoretical rewrite is warranted.

## P2 — Raw text protection conflicts with existing intentional submission grammar

**Location:** `docs/TUI_DOCTRINE.md`, section 7, final sentence of the
conflict/ambiguity paragraph (lines 130–131); `TUI_IMPLEMENTATION_PLAN.md`
Unit 1 lines 39–42 and `TUI_EXISTING_VIEWS.md` conversation/composer section.

The doctrine says text entry, paste and IME contents are not a macro language.
This correctly protects text insertion from being mistaken for shortcut events,
but the wording also prohibits the existing explicitly registered POWERWORDS
submission behavior. `docs/CALLABLE_WORKFLOWS.md`, Matching and presentation,
defines case-sensitive workflow tokens anywhere in submitted operator text,
including quoted/code text. That is an intentional current language, not a
decoder accident. The migration promises preserved current controls without
resolving whether the submission grammar survives. An implementer could remove
or reinterpret a delivered expressive capability while claiming compliance.

**Direct fix:** distinguish inserting text from deliberately submitting it.
Raw text/paste/IME must not resolve as keybindings; submitted text still follows
the effective, inspectable registered workflow grammar, including its existing
POWERWORDS semantics unless separately changed. Add that preservation to the
composer migration and one direct acceptance case: pasted/typed text never
executes while being entered, while explicit submission selects the same workflow
definition and exact suffix as the current grammar. Highlighting alone does not
select or execute it. This requires no new general test campaign.

## P2 — The existing Context/request/repair workbench has no delivery owner in the sequence

**Location:** `docs/TUI_EXISTING_VIEWS.md`, Context, requests and original repair,
lines 105–123; `docs/TUI_IMPLEMENTATION_PLAN.md`, Units 2–3 and Unit 6.

The view design gives a concrete existing-domain workbench for discovering what
the model sees, editing context, inspecting actual request inputs and recovering
originals. The implementation sequence migrates conversation, sessions, tasks,
settings and callable definitions, then names context only as a metrics family.
Metrics about context are not the Context edit/repair workbench. No unit explicitly
owns its source collection, request comparison, versioned edits and exact-original
repair interactions. This matters to the first-core CLM and self-development
intent, and leaves a requested existing view unsequenced despite the broad view
map. The native authority already exists; it does not depend on a future hook
or recipe catalog.

**Direct fix:** add the Context/request/original workbench as a named application
in Unit 3 or an explicit sibling unit after the shared document/collection/editor
parts exist. Keep its current Context authority and next-request data-edit
semantics distinct from deferred governing-definition activation. Direct
acceptance should cover a preserved edit draft on stale-base conflict, inspection
of the actual historical request, and retrieval of the exact retained original
after presentation/compaction. Context metrics should link into that workbench,
not become its substitute. No new Context store or scheduler is needed.

## Scope

Both findings are local design/sequence corrections. They do not require
restarting research, selecting a framework, imposing a new approval gate,
revisiting settled product tests, or converting the doctrine into a fixed
screen arrangement. No other substantive interaction finding was identified
in this bounded reading.
