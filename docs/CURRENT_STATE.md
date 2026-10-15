# Current Blackbird stock

Tokenomics/OpenRouter is integrated on **master**,2026-10-15, by fast-forward
through `4aa8f43` (qualified source `56b3f62`). The
[using guide](TOKENOMICS.md) covers explicit OpenRouter main coding, retries bound
to the selected account, prompt-cache policy, changing-state layout and the
separate raw usage reader. The [execution record](TOKENOMICS_IMPLEMENTATION.md)
and [qualification record](../papers/tokenomics-2026-10-14/qualification.md) record
136/136 Debug, full owned C++/Lua analysis and affected Mac/Linux optimized/
sanitizer passes. The operator authorizes one live round then merge: the native
launch refuses with provider_auth because no OpenRouter credential is configured;
no tool effect occurs, reopen preserves settings, usage remains unknown.
Live continuation/cache behavior remains arconaut-438; intermittent baseline
operation-capture timeout remains arconaut-7qn. Running-process activation is
separate from source integration.

New design wave,2026-10-14: concurrent and durable workflows. The
[proposed design](CONCURRENT_DURABLE_WORKFLOWS.md),
[delivery sequence](WORKFLOW_DELIVERY.md) and
[fresh research/review synthesis](../papers/workflows-2026-10-14/SYNTHESIS.md)
are complete for discussion on work/workflow-design-2026-10-14, descended from
the qualification/report branch. No workflow scheduler implementation, library
adoption, main integration or running-executable activation is implied. Existing
bugs and required tuning join the next implementation closeout; the source and
performance stock below remains the prior watch's actual state.

Latest watch: **2026-10-14**. Read [the handoff](HANDOFF.md) for exact source/
branch state, qualification results, open defects and next discussion. The
[performance notes](../papers/2026-10-14-performance-tuning-notes.md) track five
new investigation beads. Named owner source is integrated on master; subsequent
qualification repairs and measured reports are pushed on work/nls2-qualification-state
and are not merged into master. Missing actual File dispatch context remains
arconaut-4by; nls.2 stays in progress. No optimization starts during handoff.
The broad inventory below records the earlier October 10 stock; the handoff and
linked completion/repair/performance records supply the subsequent changes.

The default main line is `master`. On October10 the operator directed integration
of the pending branches. Master fast-forwards through `command-jobs` at `2ac0635`,
including the `trajectories` and `provider-auth` histories, plus the doctrine
symlink and current documentation. No source conflict or history rewrite is
needed. Candidate refs and five detached historical worktrees remain preserved.
[Earlier dispositions and this integration](BRANCH_INVENTORY.md).

The authorized collaboration campaign has delivered named session picking,
shared operator/model/Lua colleague calls and bounded native participants. A live
OpenAI colleague found a real tiny-viewport defect and Blackbird's main model
edited it; physical PTY checks, compaction/original repair and actual launcher
restart preserved the task/result/context. The current Release/OFF launcher is
updated; the running operator process is left alone. [Campaign results](../papers/2026-10-08-collaboration-campaign.md).
The initial Claude request timed out at90seconds. An operator-authorized new
attempt completed in337.331s with a360-second limit; its source triage and confirmed fixes are integrated on master.
The separate Giga stays paused.

## Running command jobs

Master owns running command identity and collection across
background/foreground waits and later turns. Shared operator/model/Lua controls
provide stable job IDs, bounded reads, ordered/deduplicated input, explicit signals,
resize and stop. Native pipes and private PTYs keep exact originals; launch-relative
deadlines and original exec settlement survive detachment. PTY attachment is a
text interface with escaped output, not a full-screen terminal emulator. Active or
unsettled commands block restart/session switching; shutdown joins owned jobs and
crash recovery never adopts an old PID or replays an old command.

The two reopened failing oracles have causal fixes. Final changed-case rechecks
pass on Mac release/ASAN/TSAN and Linux debug/release/ASAN, and source checkpoint
`6fa7c38` passes full debug84/84 and all owned C++/Lua lint.
[Results](../papers/2026-10-09-command-jobs/qualification.md) distinguish actual
passes from historical failures. The source is integrated on master; this merge
does not activate a replacement in the running operator executable. Steering and additional
trajectory implementation remain deferred. [Design](COMMAND_JOBS.md) and
[operator controls](USING_BLACKBIRD.md) describe the current limits.

## AFK bug fixes and raw instrumentation

On master, Claude reported errors without a success-only result field
remain known failures; allocation after effect-capable dispatch stays UNKNOWN.
Explicit `--recover-unknown-effects` validates supported native file/command and
Lua/workflow attempts as a whole and records UNKNOWN without replay/PID adoption.
Durable old command custody remains visible and fences restart/backstop across
compact reopen. Historical bootstrap payloads remain unsupported and the exact
original operator session/error is unidentified;05n stays open for that residual.

The old metadata implementation and direct/profile tests are preserved as an
unapplied [draft overlay](../papers/2026-10-09-operational-instrumentation-draft.patch).
Operator2026-10-10 rejected its design independently of the late lint finding:
instrumentation means hooks/surfaces affording system/product metric emission;
operations and evals consume them. Comprehensive source/reference/literature study
located the source surfaces. The operator subsequently rejected producer-side
transforms and the OTel/Prometheus architectural basis. The
[approved design](INSTRUMENTATION.md) specifies retained named-memory raw byte
lanes, durable recorder acknowledgement, bidirectional wakeups, protected terminal
and failure credit, a raw audit segment family and tertiary transforms. It includes
the complete raw source census and direct implementation/test plan, older C/source
studies, one design review with corrections, and small Mac/Linux producer/crash
probes. The operator approved implementation on2026-10-11 and excludes host power
loss. The delivered source includes named-memory transport, separate
supervisor/recorder, raw disk preservation/recovery, provider and command native
boundaries, participants, Context, turn/task/audit-query owners, independent
sampling and native/Lua authored events. The operation/storage/retained segment
adds original result preservation, physical I/O and retained/index emission,
prepaid terminal/cleanup banks and explicit main/worker transfer. Actual launcher
capture/reopen checks native storage and Root lifecycle; inspection creates no
capture run. The [completion plan](INSTRUMENTATION_COMPLETION.md) records owner
checks and remaining census/workload measurements. Source ca6747b passes110
Debug cases, all161 owned C++ translation units and owned Lua checks, with
affected Mac/Linux Release and ASan-UBSan qualification. nls.2 remains open; logical-clock
nls.9 and P0 audit completeness nls.10 remain separate. No evaluator was selected
or run. Existing operator executable replacement, multiplayer, steering and
further trajectory views remain separate.
[Disposition and evidence](NLS_AFK_SUBPLAN.md).

## What is on master

| Area | Current behavior | Material limit |
| --- | --- | --- |
| Native harness | C++20/Lua, terminal composer, command palette and external editor; retained sessions; `/new`, bounded retained names, searchable `/sessions` and `/resume`; independent task/draft/context restoration; restart/resume/continue. | Owned OpenAI auth available; installed Codex remains a compatibility route. |
| Context | CLM edits, managed compaction, archived original recovery and context budget policy. | Read selector now has one lines/bytes mode; malformed calls return correction guidance. Historical Lua ranges stay compatible. |
| Recovery | Cooperative backstop, bounded provider recovery, workflow repair, retained unknown effects and explicit native unknown-effect recovery without replay. | Arbitrary exec and escaped/remote effects prevent automatic claims of containment; general crash recovery unfinished. |
| Command jobs | Shared operator/model/Lua background/foreground waits, stable job IDs, bounded output, ordered input, signals, resize and stop; native pipes/private PTYs. | Text PTY interface; active/unsettled commands block restart/session switching; crash recovery never adopts old PIDs. |
| Trajectories/native formats | Shared bounded audit queries, timed variable observations and point correlation; owned native values and BBM2 internal persistence/IPC. | Clock synchronization unknown; observation gaps; no legacy format reader; richer views remain deferred. |
| Programmability | Retained dynamic tool definitions, named modules, staged model/effort defaults and effective/pending inspection. | Callable registry/configuration staging is delivered; combined atomic interrupt/apply and live native reload remain deferred. |
| Orchestration | Native bounded process-lifetime participants, configured concurrency, observe/await/join, addressed direction/dedup, cancellation requests and task badges; existing synchronous Lua composition remains. | Context-only workers; no arbitrary heap/child resurrection or cross-session participant custody. Unknown remote effects stay unknown. |
| Station | Durable-before-dispatch local source admission, duplicates, boundary inspect/steer/pause/resume/stop. | Local-file source; unknown effects remain unknown; no live TUI attachment or crash-containment claim. |
| Integrations/HUD | Opt-in trusted Lua packages; public GitHub releases and thematic text feed used in real work. | Feed display/context/action are separate; no broad integration catalog or graphical HUD. |
| Multiplexing | Supported external phux launcher, actual TUI turn, detach/reattach and RRC checked. | Shell run probe timed out; native lifecycle projection is backlog. Relay TLS does not establish application E2EE. |
| Decision models | Native Jev model tool, Lua `blackbird.decide`, `/decision JSON`; batched Choice/Score/Noul, lazy credentials, audited requests/results. | Jev only; workflows compose judgments and explicit retries. |
| Callable workflows | Retained Lua definitions, configurable slash prefix/aliases and bare invocation, operator POWERWORDS and terminal colors; model/Lua discovery/invocation. | Sequential same-turn execution; no parallel or durable scheduler. |
| Local efficiency | Saved current-state and suffix recovery for settled single-segment sessions; paged checked archive queries and original repair; checkpoint renewal prunes resident history. Earlier wrap-up heavy TUI first-frame p90 93.355ms in100runs; not a new task-pane-generation measurement. | Unresolved/station/pending-restart/multisegment states fall back to full recovery; catalog disk renewal still follows total key count. See [delivery](../papers/2026-10-08-saved-state-delivery.md) and [measurements](../papers/2026-10-08-startup-300-wrapup.md). |
| Colleagues | `blackbird-colleague` CLI/library, selected-context OpenAI/Claude requests, explicit captures/results and fake-transport tests. `arco-colleague` remains compatible. Common model tool, Lua and operator calls plus native concurrent workers. | Main coding loop remains OpenAI; workers are context-only. OpenAI live review/edit demonstration worked; Claude source review completed on the authorized360-second attempt. |
| Tasks | Durable session tasks/subtasks, typed model/Lua read/edit, below-sprite right pane, blockers/counts/version guards and explicit runtime bindings. | One owner/session; no cross-session synchronization, dependency scheduler or subsubtasks. See [tasks](TASKS.md). |
| Presentation | MIT, Patrick Beam copyright, GitHub About, supported phux link; BLACKBIRD wordmark and supplied SR-71 startup/status image. | Ghostty/Kitty image support; terminal fallback. |

## Remaining design and delivery

Common colleague and participant machinery is delivered, including a live Claude
source review. Its confirmed findings are repaired on master. The earlier timed-out request
remains unknown; the later answer does not resolve that earlier effect.
Owned direct Kimi/MiMo/Grok colleagues are implemented; remote tool-using
colleagues remain work.

G9 is a security discussion only. Its original OpenSSL/TLS proposal is not a
selected multiplayer architecture. Transport, room/message security, peer identity,
membership, enrollment/revocation, artifact/job authority and reconnect semantics
need design together. Frumentarii delivered primary literature/refimpl studies; read the
[source-grounded synthesis](../papers/networking-2026-10-07/SYNTHESIS.md). No networking dependency adopted.

[Branch inventory](BRANCH_INVENTORY.md) records the retired local/remote heads
and preserved detached historical checkouts. Published source history is unchanged.

## Remaining work

Saved current-state/suffix recovery is delivered for the supported settled,
single-segment population. The earlier recovery continuation notes describe
historical incomplete work; current behavior and limits are in the
[delivery report](../papers/2026-10-08-saved-state-delivery.md).

The requested performance wrap-up is integrated and published. Native request
inputs use invocation references; context values and history snapshots share owned
storage; context edits avoid duplicate candidate entries. Maintenance streams catalog
renewal, resumes scratch cleanup, and bounds compact resident tails when publication
fails. `/checkpoint` retries maintenance. The coding test failure is resolved.

Ordinary interactive launches now select the current Release/OFF `blackbird-ui`,
matching `blackbird` and the compatibility aliases. Program source and activation
share a durable batch; unchanged discovery metadata skips rewriting. Local phase
attribution and a finite [reopen matrix](SESSION_REOPEN.md) are delivered. No
under100ms readiness result is implied; final launcher outliers are retained.
See [delivery and actual limits](../papers/2026-10-08-wrapup-delivery.md).

New provider diagnostics expire after30days and live in removable files. Reopen/
capture cleanup is bounded; `--expire-diagnostics` cleans idle sessions explicitly.
Older diagnostics embedded in finite audits remain there. Additional stream cost
measurement was excluded by the operator. Catalog disk renewal still follows total
key count; arbitrary historical compatibility and linked-session reopening are not
implemented by this launcher.

Scoped Mac debug/release, ASan/UBSan and Linux debug checks cover the changed native,
retained-state and real PTY paths. The tiny-window repair clears stale rows and
uses physical cursor bounds;20x8/9x90 cases pass. Other work: historical recovery complaint follow-through, remote tool-using outposts, station attachment,
multiplayer and standalone login. The historical Giga remains paused. The current
collaboration campaign is delivered. Operator prioritizes owned provider login
and credential lifecycle beyond Codex and Claude CLI authentication.

## Owned provider authentication

Master includes an owned provider registry/credential store,
private operator auth CLI, Kimi/Grok device login, OpenAI signed browser login,
Anthropic PKCE/copy-code and generic key providers. Direct peer transports cover
the priority providers plus custom Responses/chat/Messages services. Main coding
remains OpenAI and can use explicitly selected owned credentials. Existing
Codex/Claude compatibility paths remain; operator account consent and remote
logout revocation are follow-through. Scoped Mac debug/release and Linux debug
checks passed; the late ambient-key guard has its direct Mac release check.
See USING_BLACKBIRD for commands and limits. Operator authorized integration on2026-10-09; provider-auth fast-forwarded
onto master and was pushed at59539d3. The operator's running process is untouched. Next campaign is the
requested instrumentation/evaluation/audit/trace/trajectory/Entire-like work.

## Trajectory campaign first slice

Master includes a shared committed metadata timeline with
readable `/trace`, model `trajectory_read` and Lua calls. It bounds both examined
facts and returned rows, pins historical prefixes and follows causal IDs to
existing originals. Mac debug/release and Linux debug direct checks passed.
It also includes UTC/monotonic operation timing and bounded central
variable observations: effective request instructions and explicit read-only Git
HEAD/history. `/variables` queries samples; `/correlate` plots work and sample
points; `/git-observe` acquires Git samples. Operator, model and Lua use shared
dispatch. Clock synchronization is unknown; samples leave gaps between them.
The instruction representation is an observation proxy, with no doctrine registry
or editing/activation API. Reported usage/model/parent-link audit review, integrated
evaluation runs, measured clock alignment and richer inspect UI remain open.
See [design and usage](TRAJECTORIES.md).


Master writes trajectory log/program metadata and native operation
continuations in owned BBM2 binary packets. Native values and typed numeric
components replace internal JSON throughout context, saved state, sidecars and
owned IPC. Lua transfers arguments/results directly. JSON remains external
protocol interchange and explicitly selected export. No legacy format reader.
See [TRAJECTORIES](TRAJECTORIES.md) for the wire layout and file names.
