# Blackbird (formerly Arconaut)

Read [BLACKBIRD.md](BLACKBIRD.md), [README.md](README.md), and
[JOURNAL.md](JOURNAL.md). Blackbird is the current working doctrine.

This is the fresh foundation repository. The inherited implementation, design
documents, instructions, old issue records, and automation are historical
reference under ignored `quarantine/arconaut/`. They carry no current design or
instruction authority. Sources and restoration are in [QUARANTINE.md](QUARANTINE.md).

Start reconstruction with [docs/FOUNDATION.md](docs/FOUNDATION.md): the expert
operator's stated intent, its design implications, and the remembered time and
foundation work. Mixing paradigms, multi-model live collaboration, integration
with ordinary computing tools, and almost no routine approval friction are current
product aims. The assessment in `papers/`
identifies faults and possible research leads; it does not select legacy code
for reuse. Research, reviewed design, and a reviewed plan precede product coding.

Default to implementing and owning machinery, following the workspace library rule.
Discuss any proposed library adoption with the operator before adopting it; consider
exceptional libraries with vibrant, active communities and explain concrete benefits,
fit, costs, and tradeoffs against an owned implementation. Studying or acquiring
reference source does not adopt a dependency.

The operator's brief/discussion phase has progressed to the reviewed ground-up
[docs/CORE_DESIGN.md](docs/CORE_DESIGN.md). Its two adversarial reviews resolved
eight interaction findings; behavioral review does not qualify an implementation.
Read [docs/TESTING_PLAN.md](docs/TESTING_PLAN.md) and
[docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) for current plan status.
The operator's 2026-10-02 course correction supersedes a new plan-review gate:
start the bootstrap implementation directly, with short written notes, direct tests
and source grounding. Kimi can challenge concrete code later. Use the current
implementation plan's milestone-one sequence rather than waiting for full U1. Runtime/provider/
library adoption and qualification happen before the unit that needs them.
Read [docs/BEHAVIOR.md](docs/BEHAVIOR.md) as the earlier working brief. The candidate in
[papers/2026-09-30-foundation-design-discussion.md](papers/2026-09-30-foundation-design-discussion.md)
and its runtime/storage recommendations are discussion input, not an adopted
specification; current contracts and sequencing come from the documents above.

The operator corrected computation ownership during discussion: kernels, databases,
and similar services may be shared by an army of Arconauts. Arconaut is the consumer/
customer, not their governor. Their control plane, scaling, and governance belong
to future meta-project fabric work. Preserve this boundary even for local services.
Earlier resident-kernel/resource-owner proposals in papers are superseded in that
respect; read the current FOUNDATION/BEHAVIOR clarification. Model agency over its
own programs remains substantial. Refitting one Arconaut quiesces its own work and
client activity; it must not pause/terminate services consumed by others.

The operator chose change activation after conclusion of current turn/affected
workflows by default, with a combined interrupt-and-apply-now operation to bring
pending changes forward. This supersedes the earlier next-decision default in
research proposals. Control+Enter is illustrative, not a fixed UI binding. Preserve
ordinary model agency; this is timing semantics, not a new approval gate.

Read the additional operator requirements in FOUNDATION: expressive shared
orchestration, model ergonomics, hot turn redefinition, managed compaction,
comprehensive core audit, hot reload, and autodroit self-improvement. Rust is
optional; JS/TS and Go face strong distrust; JVM approaches are generally forbidden.
Python is excluded from production. The operator selected C++ with Lua scripting
on 2026-10-01; RCC++/Godot/similar native reload mechanisms, runtime versions and
libraries remain unselected. Read docs/STARTING_DESIGN.md as a discussion sketch,
papers/2026-10-01-cpp-rigor-seed.md and papers/2026-10-01-lua-rigor-stack.md as
qualification proposals. This decision does not authorize skipping design/plan review.
CLM is explicitly included in the first-pass core: model-editable live context,
model-authored transformations and their evolution, repair from originals, and
context-revision/final-request audit. Do not defer this to a later plugin. The
operator explicitly reaffirms that speed toward self-development does not relax
literature/source grounding, design review, or meaningful direct verification.
Investigate continuity through rebuild and re-inhabitation before assuming a
large compiled harness. These requirements govern the fresh research, not legacy
phase labels or the language used by the old code.

Read the outpost/refit discussion in FOUNDATION. Refit requires no active
main-harness programs/provider requests; its own standing work is preserved and paused.
Uninterrupted services belong in independently managed OS daemons. Keep refit's
conversation handoff distinct from remote outposts working as concurrent peers
with selected context and communication back to the main colleague.

The [development tooling](tooling/README.md) is configured and locally qualified.
Use `scripts/rigor check PROFILE` for owned C++/Lua work, and requalify tooling
changes with appropriate direct diagnostic cases. Tool capability does not qualify
agent behavior. Ordinary Linux foundation checks run through `scripts/check-linux`
on Neuroses with its explicit Clang 18.1.3/libstdc++ 13 profile. Lua production
embedding remains an owning-unit qualification. No production library is selected
by the tool installation.
Subsequent operator steering defers mutation campaigns until working Arconaut and
a mature suite. Mutation/Runpod provisioning is not a blocker on the current plan.
General Linux checks use Neuroses over Tailscale; future mutation uses Runpod, with
node termination the operator's responsibility. Never run mutants on this laptop.
The installed owned `kimi-colleague` skill and [Kimi utility](tooling/KIMI.md)
support independent direct CLI reviews and explicit-session rereview; do not use
the older published Node relay. [Jev](tooling/JEV.md) offers bounded advisory
judgments through the existing workspace client, never a correctness/approval gate.

At useful research/design checkpoints, read Rhizome's current C++ rigor findings
under `../rhizome/papers/` for transferable evidence. Its project scope, decisions
and instructions stay local to Rhizome. Record applicable
findings here and keep that research watch read-only.

Keep research and independent review reports in `papers/`, reference material in
ignored `quarantine/`, and working logs in ignored `context/`. Ignore PDFs. Record
actions and decisions in the journal. Do not initialize Git in the parent workspace.

Use the current local beads database for issues, with programmatic `--json` output.
The old backlog remains in `quarantine/arconaut/.beads/` for study.
Before ending a session, close completed issues, record substantive remaining
work, run checks appropriate to the changes, and run `bd backup`. This supported
backup preserves issues and dependencies in `.beads/backup/`. On a new clone,
run `bd init` followed by `bd backup restore`. Avoid exporting to the legacy
`.beads/issues.jsonl` path: installed bd 0.58.0 treats it as an incoming import and
reports a stale database after export. The earlier export is preserved in context.

Arconaut's Dolt server uses dedicated port `13308`, persisted in
`.beads/config.yaml` and `.beads/metadata.json`. Rhizome uses `13307`; the default
`3307` can belong to other projects. Beads starts the configured server as needed.
Automatic bead-backup pushes remain disabled. Operator2026-10-06 explicitly
authorizes and requires Git checkpoint commits and pushes, including work and
candidate branches before main integration. Commit completed conceptual units
and meaningful experiment checkpoints; push their branches to the configured
remote without waiting for main. Preserve source/configuration and relevant
results, excluding credentials, ignored context/quarantine/PDFs and build outputs.
Do not force-push or merge into main by implication. Record push failures plainly
and retain local commits. Branch-based candidates use bounded worktree leases,
not a checkout per archived candidate.

Keep the quarantined source unchanged. Record discovered mechanisms, evidence,
consequences, and the design question or experiment needed to resolve them.
Mutants run on the fleet, never this laptop. Run only checks that directly answer
the current research or implementation question. The operator expressly authorized
subscription-backed Kimi and Jev development calls through existing authentication.
Keep credential contents private. Acquired agent implementations remain study-only;
that review authorization does not ask us to execute them with the operator's accounts.

Operator2026-10-07 explicitly bounds hardening to TWO layers: remediation tasks
and fixes; then one recheck and fixes. Never a third assurance layer. Read
[BOUNDED_HARDENING](docs/BOUNDED_HARDENING.md). Declare defects/direct checks and
resource use before work. Operator2026-10-11 rejects invented wall-clock cutoffs:
bound work by concrete defects and the two layers, then finish the authorized
delivery. Earlier campaign timers are historical, not a universal stopping rule.
Do not review reviews, certify rechecks,
reopen settled checks without relevant changes, or reset budgets under new labels.
At the bound retain unsafe candidate inactive and move to independent useful work.
Current remaining capacity envelope and deferred scope are in that note.

Operator2026-10-07 requires a waste-conscious native implementation: treat every
byte as valuable. Read papers/2026-10-07-waste-audit.md. Immutable retained payloads
share storage; a forward candidate stages new state over the retained prefix, and
replay reconstructs derived state without cloning prior history per batch. Account
for ownership, lifetime, copies, representation duplication, allocation/I/O cadence
and actual hot-path complexity. Measure real byte/work costs before optimization;
do not invent performance claims or add a perpetual performance certification gate.
Preserve originals, exact request/effect linkage, recovery fences and model agency.

Operator2026-10-07 clarifies: substantial memory use is welcome when intentional
and justified by useful capability or measured performance. Do not impose arbitrary
RSS ceilings or optimize for byte minimalism. Avoid legalistic terminology; describe
actual behavior, rules and invariants directly. Archived history must not force
resident payloads or full replay merely because it exists.


Operator2026-10-09 requires a clean lint slate before every commit. Run
`scripts/install-hooks` in each clone; the tracked pre-commit hook runs full debug
checks and owned C++/Lua lint. Fix all findings before committing/pushing. Do not
bypass the hook or add rule-suppression annotations; future exceptions must first
be discussed with the operator. See tooling/README.md for the exact gate.

Operator2026-10-10 supersedes that blanket gate for text-only changes: run
relevant diff, document and link checks; do not run full debug tests or C++/Lua
lint for documentation alone. The installed hook still runs the full gate, so
use a per-command hook override for a checked text-only commit. Keep the full
source gate for source changes; do not rerun settled source checks for a
fast-forward integration whose additional changes are only documentation.

Operator2026-10-10 corrects instrumentation architecture: capture raw metric
events and funnel them out of the main Blackbird processes as fast as physically
possible, losslessly. A separate recorder preserves raw events on disk; a
tertiary process performs all transformations. No writer-side duration calculation,
filtering, sampling of emitted events, normalization, projection, aggregation,
histogram, string formatting, compression or programmable metric processing.
Blackbird owns minimal capture/transport only. Older systems literature and
reference code precede transport design; OTel/Prometheus do not guide emission.
Read docs/INSTRUMENTATION.md. Operator2026-10-11 approved the raw design and
implementation and excludes host power loss from its losslessness contract.
Preserve emitted bytes through process/recorder failure; do not add per-emission
disk waits or power-loss qualification. Evals remain deferred until discussion.

Operator2026-10-15 adds expressive, meticulously presented TUI research intent:
permissive key/control systems, MIDI controllers, possible haptics, useful density
and responsiveness under load. The acquired
[TUI supplement](papers/tui-2026-10-15/README.md) and
[synthesis](papers/tui-2026-10-15/SYNTHESIS.md) ground the next UX/HCI/BHI
discussion (`arconaut-rhi`); BHI means bot-harness interaction/model ergonomics.
No framework or device is selected. Preserve current native partial-write output
and exact originals/raw capture when studying Lua presentation and display
coalescing. Current ESC-recovery source finding is tracked separately in
`arconaut-600`; source acquisition did not implement a repair.

The operator then requires an explicit written theory developed with researchers
and frumentarii, adding actual SR-71 controls and pilot task saturation. Read
[INTERACTION_THEORY](docs/INTERACTION_THEORY.md) and its
[primary-source research](papers/interaction-theory-2026-10-15/README.md) before
deriving interaction priorities. The theory covers practical/generative agency,
representations, skill, feedback, attention, coordination and continuity. It
supplies conditional explanations and counterexamples, not a selected layout,
controller, numerical workload limit or implementation plan. Existing C++/Lua
contracts and the distinction between present capability and design intent remain.
