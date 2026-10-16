# Sustainable command residency

2026-10-15. Owning lane: arconaut-zh2 command registry lifetime. Proposal for
review before implementation; no new dependency or reference code adopted.

## Observation and theory

The operator's session admitted32 commands and refused command33 and34 before
worker launch. The defaults allow8 live jobs and32 resident records. Ordinary
foreground execution retains every completed record; only explicit `archive`
removes one. The registry therefore behaves as a lifetime command quota even
when all earlier processes were reaped and their results/output retained.

A command's native process owner, recent inspection projection and durable
originals have different lifetimes. Keeping the owner until actual custody and
capture settlement is necessary. Keeping every recent projection forever is
unnecessary and unsustainable. Automatic retirement may release only the latter;
it must never stop a process or forget an unretained outcome to admit another.

## Primary source study

Read the acquired revisions directly, without executing or modifying them:

- Codex08e2b58b07b8a423d6577b66fb7756e980b53dbf,
  `quarantine/codex/codex-rs/core/src/unified_exec/process_manager.rs`,
  `prune_processes_if_needed` and `process_id_to_prune_from_meta`1723–1794.
  The bounded map excludes interaction-locked processes from pruning, prefers
  older exited records outside its recent protected set, then permits older
  live removal. Its pointer/interaction-lock protection is useful. Live pruning
  is incompatible with Blackbird's process-custody contract.
- oh-my-pi8b25ad4a05625dde65df41d057756b4815f4837c,
  `quarantine/oh-my-pi/packages/coding-agent/src/async/job-manager.ts`,
  `releaseForegroundJob`475–483 and `#discardForegroundJob`810–821.
  It distinguishes foreground result consumption from process settlement and
  discards a foreground row only after settlement. That separation is useful;
  its special undisclosed foreground ID recycling is not Blackbird's identity
  policy.
- Blackbird `docs/COMMAND_JOBS.md`, `CommandJobs::{start,wait,control,settlement}`,
  and `CodingEngine`'s `read_process_output` path. Explicit archive already
  removes resident control/dedup histories while durable output remains available
  by original attempt/output_ref. Archive excludes active waiters, mailbox
  targets, pending output and native custody. The direct admission owner supplies
  original attempt identity; this registry is not an independent replay ledger.

Reference source/revisions/restoration are already catalogued in
`papers/2026-10-09-command-jobs/harnesses.md` and its acquisition manifests.
The existing sources suffice; no additional acquisition is needed.

## Proposed policy

Keep the8 live /32 resident defaults. Before refusing resident capacity during a
new launch, retire one oldest eligible completed record. Eligibility requires
finished worker, retained terminal observation, no pending captured bytes, no
owned/pending native custody, no unfinished input/signal delivery, no active
foreground wait and no queued mailbox request targeting that job. The terminal observation remains exactly the one
already retained; retirement emits a raw ownership transition. Native retirement
does not append/rewrite process results, delete originals, emit another terminal
observation, or signal any process.

Order is original admission order, using a monotonic native ordinal rather than
lexicographic job ID. A protected record may be skipped. Refuse capacity if all
records are protected or the live-job limit is already reached. Check the live
limit before retirement so an unsuccessful live-capacity admission does not
needlessly discard a recent projection. Preserve recent32 records when pressure
is absent. Explicit archive uses the same eligibility predicate.

A retired job leaves `process` discovery/read/control. `read_process_output` with
its existing output_ref still reads exact retained originals. A control against
that original ID fails lookup; it cannot reach another job. Request dedup history
lives only as long as its resident job, as before with explicit archive. Original
launch attempts remain unique through RetainedState admission; the native registry
must not grow an unbounded second tombstone ledger. Direct CommandJobs callers
must supply fresh admitted attempt IDs, as they already must after explicit
archive. This contract must be visible in the header and command design.

Whether the retained outcome says observed or unknown is not alone evidence of
native custody. A stopped but fully reaped, durably settled command can retire;
a pending child or unretained terminal cannot. Capture-refused records with pending
bytes stay protected. Existing sticky engine capture failure continues to fence
new work independently.

## Plan and direct oracles

1. Review this policy with the root owner, especially recent discovery semantics
   and the admitted-ID boundary. Root owns truthful pre-launch failure semantics.
2. Add a direct real-child case executing at least40 sequential foreground
   commands with the unchanged32-record bound. Check every known exit/output,
   exactly one launch per command and bounded residency; old source must fail
   at command33. Add a retained-engine check that the retired first output_ref
   remains readable and original terminal observation count stays one.
3. Under a small configured resident bound, protect a live job, a finished but
   unretained terminal/capture, and a mailbox-targeted settled job. Admission
   must refuse without changing the protected jobs. Drain/settle the appropriate
   item, then admit a fresh ID and verify exact ownership/output/late-control
   behavior. Test IDs deliberately out of lexical order to expose wrong pruning.
4. Implement a shared eligibility predicate and ordered candidate selection;
   destruction/join must occur outside the registry mutex. Preserve raw credit
   accounting and emit an ownership transition using the admitted launch link.
5. Run focused native registry + engine cases; root integrates broader affected
   rigor, performance comparison, Linux/sanitizer profiles and independent review.

Two-layer remediation: one implementation/fix layer, then one independent
recheck/fix pass. Findings that require more scope return to the explicit design;
no serial assurance layers, new persistence format, or live-job eviction.

## Reviewed admission API

The root owner accepted the policy and narrow native `CommandStartStatus` output
parameter. It reports live/resident counts and limits, an explicit live-limit or
resident-limit refusal set only at the actual registry guard, and whether worker
construction succeeded. Raw/audit/preparation failures do not acquire a synthetic
registry-refusal reason. Root uses this distinction to repair model/human failure
semantics without changing the global Error representation. The status is local
to that one call; no engine-global or nested-operation flag is introduced.

## Implementation and direct results

Implemented the reviewed admission-pressure policy in `CommandJobs`. Explicit
archive and automatic retirement share the same lifecycle predicate. A native
admission ordinal selects the oldest eligible record; lexicographic identity is
irrelevant. The worker is explicitly joined outside the registry lock before the
retired owner is released: the worker publishes `finished` before its lambda has
fully returned, so relying only on the Job destructor could let its last captured
shared pointer destroy and join itself. Raw2045 operation4 records automatic
settled retirement, prepaid before the registry erases the old entry.

The root owner built and ran the new native and engine cases against old source.
Both failed with capacity at ordinary command33. Raw logs are
`context/ultramarine-repair-2026-10-15/*retention-red*.log`.

The targeted Mac debug native retention case and the complete existing native
`command_jobs` executable pass after implementation. Logs are
`context/ultramarine-repair-2026-10-15/retention-green.log` and
`command-jobs-green.log`. The cases establish40 ordinary foreground children,
exact independent output/exit/launch markers, the unchanged32-resident bound,
admission-order retirement under reverse-sorted IDs, retired-control refusal,
protection of actual live custody/unretained capture/mailbox targets, and exact
live/resident refusal counts before launch. A live-limit refusal preserves old
completed projections even when the resident map is full; a later admitted call
resets the status and launches once. Full native shutdown also retains the
existing independent ECHILD reap oracle.

The engine case additionally checks that the first automatically retired
projection's output_ref still returns the same complete presentation and byte
count, with one retained terminal result and exact original binary bytes. Its
post-change run and broader shared checks belong to root integration; no source
commit, full gate, fleet mutation or AFL activity was performed by this lane.

## Layer2 profile correction: separate retention episodes

The Mac TSan run exposed the test registration mistake: appending40 instrumented
child startups to each existing lifecycle suite exhausted its existing30/45second
case deadline. The new retention workload now runs only through `--retention`
on the native and engine test executables. Default invocation preserves the
original lifecycle scenarios and deadlines. Root registers independent
`command_job_retention` and `command_job_retention_engine` CTest cases with60second
budgets for their new cold instrumented-child workload. The retention assertions
and40-command scenario remain intact; no existing deadline is widened. Root runs
the corrected selected profile once as the declared recheck/fix layer.
