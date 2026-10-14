# Native concurrency and custody design attack

2026-10-14, layer-one independent review. Read all of `docs/CONCURRENT_DURABLE_WORKFLOWS.md` and the native source inventory. Findings concern the proposed design, not a claim that current product code implements it. No product changes, builds, tests or performance trials. This is the requested one attack; a later recheck should inspect only the remedies adopted for these findings.

Overall: the design preserves the right split between retained semantic ownership, executor custody, original capture and presentation. Its bounded index/queue and independent-progress mechanisms are credible. Six concrete gaps below need either a contract clarification or an owning integration requirement. None calls for another ledger, universal assurance layer or dependency adoption.

## N1 — P1: recovered unreleased dispatch contradicts current authority rules

**Section:** Durable decision and effect protocol, crash table row “After commit, before durable dispatch release,” and Existing machinery and integration boundary.

**Schedule:** A decision/invocation/attempt batch commits. Controller dies before AttemptOpenEvent. Reopen reconstructs the same attempt with recovered evidence. The proposed table permits dispatch once using a fresh owner release. Existing [RetainedState::dispatch](../../src/retained_state.cpp#L2565) returns `dispatched=false` for every recovered attempt, even if `opened=false`; [CustodyVerifier contract](../../include/blackbird/retained_state.hpp#L59) explicitly states successful recovery never grants an old attempt permission to dispatch anew. A new workflow reducer alone cannot perform the promised dispatch. Calling the ordinary fresh-call path instead makes a new logical invocation and loses stable-command identity; pretending evidence is live bypasses its authority fence.

**Minimal remedy:** Choose and specify the actual authorization transition. Either record a safe-undispatched successor attempt under the original logical invocation, or introduce a typed recovered-unreleased release authorization that is accepted only against a complete trustworthy retained prefix proving no release, current exclusive owner incarnation, preserved exact input and the still-current scope/revision. Do not simply convert recovered evidence to live. Document how this deliberately changes the older conservative contract and how uncertain/torn chains remain excluded. The crash row should state the chosen mechanism.

**Direct check:** Crash after decision commit before release; independently count zero previous effects, then one permitted dispatch under the recorded logical command. Crash after release must still yield zero automatic repeat effects.

## N2 — P1: releasing logical permits does not release a blocked worker thread

**Sections:** Execution owner and concurrency; Resource policy, fairness and workspace ownership; Direct behavioral oracles, Resource progress.

**Schedule:** Native pool size is one. A tool-using task occupies its worker, starts a child task and invokes synchronous `await_result`. The implementation releases the parent's runnable/provider permit as required, but the C++ worker remains blocked in that await. The child is eligible yet no physical worker is free. With pool size N, N such parents strand all children. A wait graph with ordinary permits released contains no resource cycle unless worker service capacity itself is represented. Current [Participants::await/join](../../src/participants.cpp#L1305) are blocking owner calls and cannot serve directly as pooled-worker waits.

**Minimal remedy:** Require all scheduler-owned waits to park an owned task continuation and return the physical worker to service, not merely release logical permits. No pool worker may synchronously await descendant pool work. If a trusted native adapter blocks without a resumable contract, account for its physical worker occupancy and reject/route descendant work rather than promising progress. Keep owner completion/stop service and mandatory executor cleanup independent of ordinary pool availability. Explicitly include this in the one-slot nested-fanout oracle.

This is an invariant, not a demand to settle the API name or exact pool implementation now. It preserves the no-thread-per-node aim while making its progress claim possible.

## N3 — P1: generation fences must reject old authority without losing old observations

**Sections:** Participants and context lineage, Cold revival; Program revisions, migration and interrupt and apply, step 4; Persistence, originals and failure capacity.

**Schedule:** A provider request is dispatched under epoch E. Local transport shuts down and records external outcome unknown. Its workflow is migrated or cold-recreated under E+1; no local callback is still executing at activation. A queued old completion, remote reconciliation response or supported transferred stream later reports exact result bytes for the old attempt. The design says stale callbacks cannot “dispatch or publish.” If a generic epoch guard rejects publication before capture/settlement routing, the old original and attempt observation disappear, contradicting complete originals and reconciliation. If the guard permits ordinary publication, old code can advance the successor's context/state incorrectly.

**Minimal remedy:** State separate authority classes. Old-generation code cannot issue fresh effects or advance the successor's business/context revision. A stable foundation observation endpoint must still accept and retain identifiable old-attempt originals, partial delivery/cleanup and reconciliation observations, using previously reserved capture/settlement resources. Such data attaches to its original attempt/branch and can enable current state only through an explicit current-generation decision/import. Unknown or invalid identity is itself an observable refusal, not a silently dropped callback. “Stale callbacks cannot publish” should be narrowed to active business/context publication.

**Direct check:** Queue an old-generation result before activation, deliver it after activation, then independently verify exact original bytes under the old attempt and zero old-code dispatch/current-context mutation.

## N4 — P1: detach needs an explicit session execution process and custody handoff

**Sections:** Execution owner and concurrency; Shared operator and model controls, Attach/detach; Existing machinery and integration boundary.

**Schedule:** Current terminal starts a turn worker and holds its [RawRetainedTransfer](../../src/terminal.cpp#L1750) through actual join. A standing workflow starts in that worker. Operator detaches/closes the terminal while it is still running. Letting terminal lexical cleanup run joins the worker and can trigger existing shutdown/stop behavior. Destroying or abandoning the transfer violates retained/raw-storage custody. Holding the entire terminal alive as an invisible execution owner fails the requirement that attachment is merely a client and introduces UI lifetime into scheduler survival. A second terminal that launches a new owner because the endpoint disappeared can duplicate session authority.

**Minimal remedy:** Name the initial physical ownership boundary: a session execution process/component outlives attached terminal clients and holds RetainedState, the raw-storage grant/settlement bundle, WorkflowRuntime and live executor registry. Terminal launch connects to that owner or creates it under an exclusive native session-owner check; detach only closes the client attachment. Establish standing owner custody before allowing detach. Transfer current terminal-held custody only through the existing exclusive actual-launch/join protocol or its equivalent; never share ordinary retained grants with worker executors. Endpoint disconnect, heartbeat expiry or vanished UI is not an owner takeover permit. Normal owner shutdown and owner crash remain distinct from client detach.

No surviving-process/refit claim is required for this remedy: the session owner may still lose its own children on its own crash. The essential point is that it lives independently of any particular UI and that initial owner acquisition/reconnect behavior has one authority rule.

**Direct check:** Close all attached client processes while a native command and durable wait remain, reattach from a new client, and independently count exactly one owner and launch. Separately crash the owner and exercise the declared conservative recovery path.

## N5 — P2: Lua quanta and asynchronous calls require a rooted suspension contract

**Sections:** Execution owner and concurrency, Runnable Lua segments; Two authoring surfaces; Native integration table.

**Schedule:** A configured instruction hook requests quantum suspension inside authored code entered through a native module/tool call using current `lua_pcall`; a command await would need to suspend through [Runtime::bridge](../../src/coding.cpp#L1539). Current code keeps native error/argument/trace frames and callback references on the synchronous C++ stack and executes the root with `lua_pcall`, not an owner-rooted `lua_resume` scheduler. A yield crossing a nonyieldable C boundary fails; retaining a stack-local continuation pointer instead leaves invalid native state at resume. Treating the Lua private yield transfer as ordinary caught failure can also corrupt task outcome. The design specifies quanta but does not name this owning boundary.

**Minimal remedy:** Add an integration requirement: every suspendable task coroutine is registry-rooted and resumed by its sole VM owner; native async bridges move arguments, errors, attempt/generation identity, cancellation linkage and result capture into a separately owned continuation object before yielding. Continuations enter stable foundation trampolines and validate task/VM incarnation; completion queues cannot call Lua directly. Use yieldable Lua continuation APIs where necessary, and declare genuinely nonyieldable trusted extension regions as visible quantum/cancellation limits. Closing a suspended VM must settle or explicitly transfer its associated native operations; a finalizer cannot fabricate settlement.

Exact binding spelling and allocation layout can remain implementation decisions. Selected Lua 5.4.8 qualification must attack yield/resume/error/allocation/cancel at the actual bridge; the historical Lua 5.5.1 research is not qualification of the selected runtime.

## N6 — P2: await-settlement must not implicitly close an observed foreign scope

**Section:** Structured concurrency and durable joins: “`await_result` does not implicitly close a scope; `await_settlement` does.”

**Schedule:** Peer A observes task B in a standing scope using cross-tree messaging. A calls await_settlement to wait for already running B. An implementation following the quoted rule closes B's scope and stops new child admissions, although A only owns a waiter. This defeats independently chosen programs and the design's separation of communication topology/publication authority/scope lineage. It also races with B's legitimate child expansion.

**Minimal remedy:** Make waiting for settlement observational. Closing/draining a scope is a separately admitted owner-authorized operation; an owning lexical convenience may combine close-and-await explicitly. If `await_settlement` is intended to be that convenience, require owned scope authority in its schema and provide a pure settlement wait for peers. Waiter cancellation must continue to affect only its observation by default.

## Items that are implementation choices, not additional defects

- Concrete weights, aging constants, inbox batch sizes, instruction quantum and pool sizes can be chosen with measured workloads; the design appropriately makes no numerical speed claim.
- Protected control/cleanup service plus pre-dispatch result capacity is the correct direction. Exact grants and reparented settlement-credit accounting belong to the scope/resource implementation, provided transfer cannot create ownerless debt.
- Current saved-state reductions must be extended for live workflow closure; the design explicitly identifies this. This is not a new review finding. Delivery must preserve complete attempts/waits/manifest references plus bounded suffix without loading all completed history.
- No global superstep is promised by the independent ready queue. This remains compatible with per-decision atomic batching and with a single synchronous durable writer; measured writer latency is a shared bottleneck, not a hidden claim of independent disk commit.
- The performance section correctly distinguishes mechanisms from claims, full execution from enqueue throughput, equal resources from equal time envelopes, and keeps tails. It requires objectives from actual baseline measurements. No invented performance target or performance result was found.
- Exact idempotency/reconciliation/cancellation support of particular provider or remote executors is deliberately unavailable until declared. That is honest capability discovery, not a reason to weaken uncertainty handling.
