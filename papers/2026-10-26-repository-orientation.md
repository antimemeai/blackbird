# Blackbird repository orientation — 2026-10-26

Read-only source/history assessment followed by this documentation checkpoint.
No product code, issue backup, worktree, branch integration, installed runtime,
provider account or live session was changed. No tests or model-service calls
were run. Qualification statements below describe retained reports, not reruns.

## Repository state

Fetched origin. Local and origin master are `a94d094`. The active checkout is
`work/text-bugs-2026-10-19` at `b057700`, four master-only commits and one branch-only
journal commit apart. There are 30 local heads, 56 origin heads (excluding the
symbolic HEAD), and 17 attached worktrees. All 16 auxiliary worktrees are clean.
Four local heads are ancestors of master, including master itself.

The active checkout already had six modified `.beads/backup/` files and untracked
Oxcart handoff, artwork and corpus assessment material. Preserve them. No stashes
were listed. Most remote-only heads are historical ancestors; the nonancestor
`work/claude-main-install-2026-10-17` is an older plan superseded by the later
implementation, not missing Claude support.

`docs/CURRENT_STATE.md`'s October 19 front line and `docs/BRANCH_INVENTORY.md`'s
October 15 current integration section are historical, not an accurate current
branch map. Several handoffs still describe staged work that is now committed.
October 26 archival checkpoint commits explicitly say they have not passed the
full source gate. Clean worktrees do not mean finished work.

## How the running system fits together

- `src/main.cpp` owns journal directory, retained state, audit, context, settings,
  provider and CodingEngine. The engine borrows audit/context/provider and owns
  command jobs, tasks, participants, observations and programmable workflow state.
- Terminal, plain CLI, station and `--application` converge on that engine.
  `src/application.cpp` adds framed command/event transport, not another agent.
- `src/coding.cpp` retains the turn's program source, creates a fresh Lua VM,
  runs `programs/turn.lua`, and publishes staged changes at a successful boundary.
  The ordinary Lua loop runs sequential provider/tool rounds. Separate native
  participants and `blackbird-workflow` provide other concurrency mechanisms;
  they are not a general concurrent replacement for this turn loop.
- Providers include OpenAI, OpenRouter, native Anthropic and registered Responses.
  JSON/SSE belong to the external provider boundary; owned Value/BBM2 formats
  carry internal state and IPC. Transport uses curl subprocesses.
- Tools dispatch through retained admission/outcome recording. CommandJobs owns
  process/PTY custody; LocalTools handles file operations. Model context is a
  revisioned presentation separate from immutable retained originals.
- The framed semantic audit and derived saved-state/archive indexes support
  reopen. Uncertain effects do not become safe replay merely because a process
  restarted. Context compaction does not reclaim physical audit capacity.
- Raw instrumentation uses a separate supervisor/recorder and tertiary readers;
  it is distinct from semantic audit settlement.
- Rebuild activation is restart/resume/continue, not in-process native hot-loading.
  `scripts/blackbird` may launch the managed install instead of checkout source;
  use an explicit BLACKBIRD_EXECUTABLE when testing a particular build.

Build/check entry points are `CMakePresets.json`, `CMakeLists.txt`,
`scripts/rigor`, and the native/CLI/PTY cases under tests and scripts. Master
selects C++23 and Lua 5.4.8. The currently selected managed generation is
`~/.local/lib/blackbird/releases/generation-6QI9PQx9`; this assessment did not
establish that generation's source identity or execute it.

## Branch dispositions

| Work stream | Actual state |
| --- | --- |
| startup-generation, cache-affinity, sol-observations | Already ancestors of master. Startup/effort question, mouse/Shift-Enter, folded tool output, UTF-8 observation boundaries, compact observations and cache affinity are not unfinished branch implementation. |
| text-bugs (`b057700`) | Product changes already on master; only the October 23 orientation journal commit is unique. |
| clm-a1v (`81fe09f`) | Research, reviewed design/plan and earlier implementation included in rd7, not master. |
| clm-rd7 (`d38fd3b`) | 40 commits beyond master; includes a1v plus clean-stop/SIGTERM, profiles, cut persistence, unit classes, ultracode dedup and result shaping. Retained qualification exists. Not master-integrated. |
| clm-rd7-s3a (`9df54fe`) | One checkpoint over rd7: cut composition library, no production callers by design. Remediation exists, but final recheck/full source gate remain incomplete. |
| clm-rd7-8zr (`5b1ff1d`) | Sibling checkpoint over rd7: test/tooling load and liveness work, not new provider semantics. Still has unresolved product/ASan findings. |
| cpp26 (`90c875c`), campaign-defects (`1d96832`) | Diverged from master at `f4bd931`. Campaign integrates C++26, j1l and vv6 only. Combined delivery incomplete. |
| defect-pyg-3nx (`496ff29`) | Recorded checked repair: retriable stall versus final total ceiling, stale capture-marker scope. Not yet in campaign integration or master. |
| defect-j1l (`651e0be`), defect-vv6 (`2610af9`) | Recorded checked process-tree containment and explicit copy/deprecated diagnostics. In campaign branch, not master. |
| defect-3o2 (`ff96171`), defect-5st (`35b8e0b`), defect-e5g (`141ba12`) | October 26 archival checkpoints, not fully source-gated: command/checkpoint volume, provider settings, linker pinning respectively. |
| sol-slash (`aeff90a`) | Only first unit implemented: completion ambiguity, status/clear aliases. Archival/full-gate incomplete; units 2–4 not delivered. |
| oxcart-host-tools (`6eecd9b`) | Native catalog/call/result bridge implemented at `0a7e297`, checked and pushed, not master-integrated. Live browser/model/MCP round trip still unqualified. |
| corpus-assessments (`9fa7e2a`) | Comparative research and attribution convention, not product changes. Composer synthesis and question-driven acquisition remain. |
| glm-bugbash (`738ef0b`) | Explicitly on ice. |

Most named clm-rd7 unit branches are already ancestors of `work/clm-rd7`; they
are not independent outstanding merges. S3a and 8zr are exceptions. Their diffs
overlap in CMakeLists.txt and tests/clean_stop_test.cpp.

## CLM continuation and concrete defects

Read `context/clm-rd7/docs/CONTEXT_LIFECYCLE_PLAN.md` and
`context/clm-rd7/docs/CONTEXT_LIFECYCLE.md`. The remaining serial path is S3a
completion → S3b timing/pending → Q1 signals → B request builder → V1 inspection
→ E1 escalation → A1 archival step → R1 overflow recovery → R0 final signal
pass, followed by live checks/pilot/trials. Provider probes, TUI work and remaining
provider-rule/cache migration have their own dependencies.

The old integration proposal names `7ee04da`, through P1b; it does not authorize
merging today's larger `d38fd3b` by implication. 5st is explicitly to be reworked
onto the accepted P1 base before later provider migration. S3b changes downgrade
semantics, so session-format compatibility requires deliberate attention.

`context/clm-rd7-notes/8zr.md` records a reproduced SIGTERM/restart livelock:
FramedJournal::write_exact retries interrupted while cancellation remains latched.
Do not classify it as host flakiness. A Linux ASan command_jobs busy failure and
bimodal TUI exit latency remain unresolved. Observed shared-host disk starvation
is a separate cause of load-sensitive failures.

The campaign handoff is `context/campaign-defects/docs/CAMPAIGN_DEFECTS_HANDOFF.md`.
Its integration lacks pyg/3nx, 3o2, 5st and e5g. The pyg/3nx CMake conflict needs
both raw-cleanup/stall cases and the j1l test wrapper, not one side discarded.
Mac TSan, missing raw terminal records and merged qualification remain unfinished.
The 9.5 GB incident was primarily repeated checkpoint tracing, not 9.5 GB of
command stdout. Broader audit completeness and full provider-error retention
remain important backlog concerns.

## Oxcart status

The untracked OXCART-HANDOFF describes the earlier absent callback lane. Read the
newer branch's docs/APPLICATION_HOST_TOOLS.md and
papers/2026-10-23-oxcart-host-tools-results.md instead. Native BBM2 catalog
negotiation, correlated results/progress and cancellation during waits exist.
Recorded internal scripted-provider checks and final Mac source gate passed;
Linux checks were blocked by SSH timeout. This is not a live browser/model result.

Retained runtime SOURCE_COMMIT and SOURCE_TREE were read directly:
`0a7e2979d7b6e298b71faa208bb5588a40f304f8` /
`19912c459ef51c92bd148d9a16385005aa6e7636`, under
context/oxcart-host-tools-qualified-runtime-2026-10-23. Live panel binding, browser
state/capture turn and outward MCP remain shared integration work.

## Issue storage and boundaries

bd onboard and bd ready refuse this workspace: current bd detects a legacy
0.58.0 Dolt-server workspace and requires explicit migration. No migration,
initialization or repair was attempted. Later Oxcart branch instructions state
that the operator says Dolt is unused; do not revive it as an incidental task.

The existing backup has 257 records: 163 closed, 85 open, 8 in progress and
1 blocked. These are snapshot statuses, not authoritative live completion:
several implemented rd7 units still appear open/in progress. Do not infer a new
work queue from those statuses alone. No issue mutation or backup was possible
or attempted after the incompatibility was established.

This assessment supports choosing a continuation, not a blanket merge, branch
cleanup, runtime update, assurance campaign or provider-policy refresh.
