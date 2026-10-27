# Original consequential design review

## Disposition: fix the consequential design gaps before deriving the plan

The receipts/current-work/linked-evidence model is coherent, and the design correctly separates proposed owner publications from existing mechanisms. It also preserves important uncertainty rather than making cancellation, missing acknowledgment or a retired job look settled.

However, several whole-episode contracts remain underdetermined. **Findings 1–3 are design blockers**: different implementations could satisfy the present prose while producing materially different execution or activation behavior. Findings 4–6 are consequential fixes to incorporate in this first layer.

This is a review of the supplied design artifacts—not a reopening of the accepted study, source verification, installed-runtime assessment or usability qualification. No tools were used. No actual provider/model/effort selection evidence is available from this response interface.

### 1. BLOCKER — Stop does not specify its ordering against automatic queue launch

**Exact locations:** CONTRACTS C2, “Stop holds all runnable next-turn items”; C3, “main turn captured at input acquisition” and delayed handling cannot stop t13; OWNERS C3, “Capture exact active turn token and queue-hold transition”; SCREENS A–B.

**Consequential episode:** ESC is acquired against t12 as t12 completes. The terminal proceeds to launch queued m32 as t13 before processing the stop transition. Capturing t12 prevents an incorrect cancellation of t13, but does not itself prevent the unwanted next turn from starting. The operator can receive an honest t12 completion-race message while the work they meant to hold has already launched.

The prose specifies the desired outcomes but not their common ordering point.

**Concrete remedy:** Define a native serialized stop-admission transition relative to queue launch:

- capture the turn target and close automatic next-turn dispatch together;
- identify which acquisition/admission boundary establishes this ordering;
- preserve a launched-before-stop result when launch actually won;
- never silently substitute t13 as the cancellation target.

Add a storyboard for **completion + queued launch + delayed stop handling**, including the honest outcome if the next turn had already launched. This needs an owner contract, not stronger preemption or changed storage policy.

### 2. BLOCKER — “Send next,” “Resume,” and correction application have incompatible execution meanings

**Exact locations:** CONTRACTS C2, especially “only explicitly labeled Send next admits execution,” “explicitly Resume returns it to runnable position,” and “Send next selects a held item”; SCREENS A, `[Apply correction; keep held] [Resume next turn] [Back]`.

**Consequential episode:** The operator changes m32’s text and chooses Resume. Does this apply the edited bytes, resume the prior immutable revision, or refuse until Apply? Meanwhile, does Send next release one held item, prioritize it ahead of runnable work, or dispatch it immediately when idle? These differences determine which instruction actually runs.

The screen exposes two actions without establishing the state each consumes. “Only Send next admits execution” also conflicts with Resume making an item automatically runnable.

**Concrete remedy:** Publish a small queue transition table with exact ID/revision guards:

- editable buffer versus committed held revision;
- Apply’s result;
- behavior of Resume with unapplied changes;
- release-one versus release-all;
- position relative to other runnable items;
- behavior when idle, busy, or awaiting a required answer.

Choose one vocabulary across contracts and screens. For example, distinguish **Apply—keep held**, **Release selected to next-turn queue**, and an explicitly defined priority action if one is actually wanted. Failed transitions must preserve both the edit and the committed prior revision.

### 3. BLOCKER — Native definition editing lacks a transaction owner and activation boundary

**Exact locations:** CONTRACTS C8, “submit complete guarded proposal” and “During active work … eligible successful boundary”; OWNERS C8, “guarded native editor submission adapter”; SCREENS F, effective g8 / pending g9 / base-conflicted editor g9.

**Consequential episode:** While t22 runs, the operator submits a native edit, and t22’s Lua independently stages another proposal. Which workflow owns the operator proposal? Does t22’s failure cancel the operator’s edit? Which proposal occupies the pending slot? Can an operator proposal become eligible at the boundary of work that never authored it?

Existing model/Lua staging semantics do not answer how a new asynchronous native editor participates. The screen also uses g9 for both a pending proposal and a conflicting edit, obscuring whether these are the same candidate.

**Concrete remedy:** Specify, separately for program configuration and tool definitions:

- edit-buffer identity and base;
- submission identity and actual execution owner;
- whether busy submission queues a later operation or joins a specifically identified transaction;
- pending-slot contention/refusal;
- whose successful boundary activates it;
- cancellation and publication results.

A queued independent edit operation is one possible coherent design; attaching it to current work requires an explicit proposed owner contract. Do not imply general apply-now behavior. Give conflicting buffer and accepted pending candidate distinct identities in screen F.

### 4. CONSEQUENTIAL — Typed collapse leaves an unbounded authored-output route through the same conversation

**Exact locations:** CONTRACTS C4, “Lua `display`/print stays explicitly authored display”; C5 layout and reading; README, principal conversation model; SCREENS C.

**Consequential episode:** A workflow prints a large serialized result or repeatedly publishes authored display. Tool cards are perfectly compact, yet the partner reply and consequential error are still buried. Classifying output honestly as authored display does not solve its reading cost.

The design explicitly closes the generic-tool projection gap but leaves the alternative route without a presentation contract.

**Concrete remedy:** Keep authored output semantically distinct, but define its bounded presentation behavior: attributable authored-output blocks, finite default preview, update/replacement versus append semantics where supported, and retained-original availability or honest unavailability. Ordinary assistant prose must remain distinct too.

Add one concrete screen containing **compact generic calls + a large authored display + the final partner reply**. This is a projection/reading requirement, not permission to discard originals or reclassify print as tool execution.

### 5. CONSEQUENTIAL — A pinned reading experience has no bounded-resource or admission contract

**Exact locations:** CONTRACTS C5, “freeze selected preview/page content,” return stack, selection preservation and “No history cloning per frame”; C4, asynchronous retained reads; OWNERS C5, pinned source/query/view revisions.

**Consequential episode:** The operator opens several originals, leaves nested return locations, and reads while streams grow and sources retire. The design promises frozen pages and preserved selections but does not say what remains resident, how many pins survive, or what happens when pin/read capacity is exhausted. “No history cloning per frame” prevents one implementation mistake but does not bound cumulative retained UI state.

**Concrete remedy:** Define a bounded pin/return model:

- frozen pages retain bounded selected content, not entire live streams;
- return frames retain locators and navigation state, not arbitrary page copies;
- state the depth/capacity behavior and visible eviction/refusal rule;
- distinguish a still-visible pinned page from an original that can no longer be fetched;
- reserve native draft/stop/Back service independently of detail-read admission.

No arbitrary performance target is needed here. The missing decision is what the interface promises under resource pressure.

### 6. CONSEQUENTIAL — Effectful pointer actions lack a presented-scene identity contract

**Exact locations:** CONTRACTS C4, pointer selection/toggle and explicit actions; C5, live insertion and partial-write packets; OWNERS, “Human gestures supply IDs/args to actual owners” and per-invocation identity.

**Consequential episode:** Output is blocked or a resize/live insertion changes layout. The operator clicks a visible row’s Withdraw, Join or Stop control. Mapping the coordinate against the newest logical scene can select a different object from the one physically presented. Checking the resulting ID at dispatch prevents a stale-ID mutation but does not establish that it was the ID the operator clicked.

**Concrete remedy:** Specify native hit testing against an identified presented scene, with stable target/action/revision captured into the gesture envelope. During partially emitted or otherwise incoherent presentation, define whether effectful pointer activation is refused or restricted to an established stable region. Never reinterpret an old coordinate using a new list.

Add a concise screen/interaction transition showing a stale click safely refused while preserving keyboard navigation and the draft. This complements—not replaces—owner-side identity validation.

## Evidence unavailable; not additional findings

The supplied material does **not** establish:

- exact current-source feasibility of the proposed adapters or complete publication coverage;
- installed-source parity or executable provenance;
- effective ESC decoding and PTY fallback across actual terminal profiles;
- safe provider/remote cancellation behavior;
- sustained reading, copy/export, accessibility, or expert-operator task success;
- the usability of these authored layouts as physical terminal screens.

Those are explicitly later obligations, not reasons to reopen the accepted study or require new assurance for this prose.

## First-layer settlement recommendation

Fix the six items in the design and retain the original request, answer and disposition. **Derive the delivery plan only after those fixes.** The later single review should be the promised **final focused design-findings recheck plus consequential plan review**, not authorization for another review or a certifying recheck after its fixes.

No implementation, sibling merge, storage-policy change, weakened oracle or additional approval ceremony is recommended here.
