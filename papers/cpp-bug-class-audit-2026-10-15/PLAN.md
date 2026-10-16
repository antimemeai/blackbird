# C++ bug-class survey and prevention study

2026-10-15 local. Operator explicitly requests C++ staff engineer subagents to
search the codebase for other instances of the repaired bug classes and study
literature/reference implementations for guards, checks, lint, annotations and
other ways to narrow their possibility space. Baseline095d7d1 includes product
repair65e5877. This is a new source survey, not a repeat assurance pass for that
repair. No main integration, activation or production dependency is selected.

The theory is that these failures cross ownership or representation boundaries:
resident versus historical capacity; admitted versus started effects; bytes and
scalars versus physical cells; saved PID versus currently owned child. A guard is
useful when it makes that boundary explicit and rejects an invalid transition.
Generic memory/race diagnostics do not establish semantic validity of a refusal,
a terminal cell position or a cleanup action. Prefer types/owner APIs/compiler
checks where they encode the actual invariant, and direct behavioral oracles for
the rest. Runtime checks must fail visibly without fabricating certainty.

Four conceptual owners survey bounded lifetime/reclamation, effect/error
semantics, terminal representations/oracles, and process identity/cleanup. Three
explicitly requested staff subagents own the first three; root owns process
identity and installed compiler/tooling mechanisms, then consolidates findings.
Each report gives source lines, reachable trigger, effect, confidence, decisive
check and prevention cost/blind spots. Separate confirmed defects, plausible
hazards and intentional bounded behavior. A negative search is scoped evidence,
not a claim that the codebase is bug-free.

Read current owned code and relevant primary literature/reference source. Retain
acquired originals in ignored quarantine with tracked exact source manifests;
archived instructions stay historical. Reference study does not adopt libraries.
Use existing compiler profiles, no new broad suite, repeated performance round,
mutation campaign, provider call, original-session recovery or AFL inspection.
Small scratch probes answer concrete disputed claims; preserve exact source,
command and result in context. User artwork stays untouched.

Root challenges each new finding once against actual source and its claimed
trigger, correcting false positives in the report. Do not review reviews or run
new panels. Queue ordinary confirmed findings, escalate an egregious reachable
fault. Deliver research reports, source acquisition manifest, prioritized bug
queue and practical prevention design/implementation sequence. User requested
an audit and prevention study; production repairs/tool adoption remain distinct
from this survey's findings. Record bounded recommendations rather than silently
rewriting multiple subsystems during discovery.
