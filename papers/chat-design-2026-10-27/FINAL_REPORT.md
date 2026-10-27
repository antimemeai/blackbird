# Original SECOND/FINAL focused design recheck and plan review

## Final-layer disposition: BLOCKED pending two focused document fixes

The six original findings are **resolved on supplied prose**. The resulting interaction model is coherent, and the plan preserves the important delivery and evidence boundaries. Two consequential gaps remain in the derived plan/design integration: stop versus queued definition-operation dispatch, and fault-specific provider-stop obligations.

This is the **second and final review layer**. The owner should fix these items and record the disposition without another review or certifying recheck. No implementation is authorized by this response.

## Original findings: focused recheck

| Finding | Disposition and rationale |
|---|---|
| **1. Stop versus automatic queue launch** | **Resolved on supplied prose.** CONTRACTS C3 establishes decoded stop admission as the serialized turn-capture, dispatch-gate-close and queue-hold transition. It distinguishes raw arrival from admission, discloses launch-before-stop and prevents replacement-target cancellation. SCREENS H covers the consequential race. |
| **2. Correction/application/release ambiguity** | **Resolved on supplied prose.** CONTRACTS C2 separates private buffer from committed revision, defines Apply—keep held, refuses dirty release, specifies selected/all release, preserves acceptance order and explains idle/busy/question behavior. SCREENS A uses the selected vocabulary. |
| **3. Definition transaction ownership** | **Resolved on supplied prose.** CONTRACTS C8 assigns native edits independent operation identities and workflows, distinguishes program/tool owners, revalidates bases at dispatch and ties activation to the operation’s own successful boundary. SCREENS F separates buffer, operation and candidate identities. The remaining cross-contract dispatch issue below does not invalidate this transaction-owner fix. |
| **4. Authored-output flood** | **Resolved on supplied prose.** CONTRACTS C4 bounds attributable append previews, rejects guessed replacement semantics and distinguishes raw references from usable originals. SCREENS H includes generic output, authored flood and separately attributed final prose. |
| **5. Reading-resource bounds** | **Resolved on supplied prose.** CONTRACTS C5 bounds active/comparison pages and locator-only return frames, specifies refusal/replace behavior, preserves native controls and distinguishes pinned bytes from refetch availability. |
| **6. Presented-scene pointer identity** | **Resolved on supplied prose.** The pointer contract captures completed emitted geometry and stable target/action/domain identities, refuses effectful activation during partial/resize uncertainty and does not equate emitted bytes with physical paint. SCREENS H demonstrates safe refusal. |

All six original findings were valid; none needs to be reclassified as invalid.

## Remaining consequential blockers

### 1. Stop-gate coverage is undefined for the new queued definition workflows

**Exact locations**

- **CONTRACTS.md, C3**, stop-admission ordering and “No second queued turn can commit after the dispatch gate closes.”
- **CONTRACTS.md, C8**, bounded engine-command queue, dispatch after active work settles, and chat dispatch waiting behind an admitted definition operation.
- **PLAN.md, T1**, production stop/dispatch contract.
- **PLAN.md, T5**, independent definition-operation contract and staging/success/fail/stop race oracles.

**Why this is valid**

C3 closes automatic next-turn dispatch and holds queued chat intentions. C8 introduces another automatically dispatched workflow queue, but does not say whether that queue shares the stop fence.

Consequential episode:

1. t22 is active.
2. The operator submits u5, which is accepted but queued—not staged.
3. The operator stops t22.
4. t22 returns interrupted.
5. u5 becomes eligible for dispatch.

The supplied prose permits materially different outcomes: u5 remains held behind the stop fence, or u5 immediately launches a new main workflow and can activate a governing definition. “Independent transaction” and “t22 failure does not itself cancel queued u5/u6” do not decide this: preserving an operation is different from authorizing its dispatch after Stop.

This is a **cross-owner scheduling gap**, not a request to cancel participants, stop shared services, undo accepted work or reopen finding 3’s transaction ownership.

**Concrete fix**

Specify one policy across C3/C8 and T1/T5. A coherent minimal choice is:

- Decoded main-stop admission fences automatic dispatch of **both** queued chat turns and queued native definition workflows.
- Queued u5 remains accepted and recoverable, explicitly **held by stop**, not canceled or staged.
- An explicit release of u5 authorizes its dispatch; releasing a chat item does not silently release definition operations.
- Ordinary t22 failure without a stop event does not impose this hold.
- If u5 dispatch committed before stop admission, the stop captures the actual current workflow and reports that ordering; it never substitutes a remembered target.

Add direct oracles for both winning orders and show the held operation/release action in the instrument episode. If a different policy is selected, explicitly state its scope and visible outcome rather than leaving the two queues’ relationship implicit.

### 2. Provider wait/retry interruption lacks a fault-specific local obligation

**Exact locations**

- **PLAN.md, T1 — Old-behavior RED obligations**
- **PLAN.md, T1 — Direct owners/oracles**
- **PLAN.md, T1 — Physical/readable**
- **PLAN.md, T6**, paragraph separating safe provider fixtures from authorized live provider-stop observations

**Why this is valid**

T1 concretely specifies local Lua/child stop, decoder conflicts, output blockage and completion/launch races. Its fake-provider ledger proves request inclusion, but does not explicitly test stop during provider wait, retry wait or partial streaming.

T6 correctly preserves the live remote-stop evidence gap. However, “separate safe provider fixture from authorized live provider stop observations” is not a fault-specific local cancellation oracle. A source unit could satisfy the listed T1 stop obligations while leaving a retry timer dispatching a new attempt after cancellation, or losing the partial attempt’s original/disposition.

The accepted requirement makes provider wait/retry interruption a distinct case. This is not a demand to prove remote termination or perform live account probes.

**Concrete fix**

Add a bounded, safe fixture obligation to T1:

- Gate the actual main provider path separately at **response wait**, **retry wait before another attempt**, and **partial stream**.
- Record independently the request/attempt identities, local cancellation/return, retry-dispatch decision and retained partial originals.
- Assert that an admitted stop prevents a subsequently uncommitted retry attempt; preserve and report an attempt whose dispatch already won.
- Preserve prior partial output and actual effect/remote uncertainty; never infer remote stop from local transport closure.
- Under flowing output, assert readable requested-versus-observed state. Under EAGAIN, prove native control/result separately from the eventual coherent screen.
- Treat a currently passing subcase as preserved capability, not an invented old-behavior failure.

Keep live remote-stop observation separately scoped and explicitly unqualified until observed.

## Plan elements that do not warrant findings

The following are adequately addressed; objections demanding more would be invalid scope expansion:

- **Useful delivery and dependencies:** T1/T2 deliver meaningful episodes independently; T3/T4/T5 dependencies and partial-completion limits are explicit.
- **Owner authority:** proposed publications precede enabled claims; missing originals, causes and inclusion relations remain honestly unavailable.
- **Evidence levels:** semantic, source, physical PTY, readable-screen, fixture/live and expert-task evidence are distinguished.
- **Instrumentation:** OWNERS assigns minimal raw capture to producers, preservation to the recorder and derivation to a tertiary reader.
- **Performance:** the inactive-helper gap is explicit, with an identified equivalent-observer/fresh-baseline path and matched predecessor/candidate procedure.
- **Later gates:** source Debug/lint/direct/Release/Linux obligations belong to later source commissions and use the shared build slot. No gates need running for this prose review.
- **Integration and activation:** publication, master integration, installation, runtime activation and observed improvement remain separate.
- **Settlement boundaries:** nothing requires parked composition, continuity merge, full-sync diagnosis, storage-policy changes or a weakened oracle.

## Final settlement

**Retain the design/plan inactive as BLOCKED until the two document fixes above are made.** Then record the owner’s fixes and final-layer disposition and complete the authorized documentation handoff—**without another review request**.

The accepted study remains CLOSED. These artifacts do not establish implemented behavior, activated delivery or improved expert task performance.
