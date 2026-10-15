# Cache-aware context and orchestration mechanisms

2026-10-14. Harness-mechanisms lane for [this campaign](CAMPAIGN.md), starting
from pushed master d74f248. This is source/literature research and a set of
design hypotheses, not implementation, a performance result or an eval campaign.
No model/provider calls, acquired programs, installers, dependency adoption,
tests, Git/Beads changes or delegation were performed by this lane.

The strongest near-term opportunity is to make the final provider request stable
where its meaning is stable, and make large observations recoverable through
selected views. Preserve editable context and exact originals. Scheduling and
result reuse can then exploit real identities rather than treating every kind
of cache as interchangeable.

## Different reuse operations answer different questions

| Operation | What is reused | What still happens | Governing limit |
| --- | --- | --- | --- |
| Provider prompt/KV reuse | Previously computed state for an eligible input prefix | A new model generation, submitted input and ordinary effect admission | Actual model/account/endpoint cache contract, matching prefix and cache residency |
| Local request construction reuse | Immutable input fragments or their encoded representation | Provider submission and generation | Exact native values, representation version and lifetime; does not establish a provider hit |
| Durable recorded completion | An observation already settled under an invocation | Recovery consumes that recorded observation | Original invocation/attempt, effect uncertainty and replay rules |
| Artifact/result reuse | A previous producer's eligible output | A new demand selects and verifies the old output | Complete declared inputs/environment, freshness, intact output and current lineage |
| In-flight coalescing | One admitted producer shared by explicitly compatible consumers | The producer runs once and each consumer retains its dependency | Permission to join, cancellation ownership and complete equivalence |
| Semantic answer reuse | A past answer judged relevant to another question | New generation may be skipped | A separate approximate product policy; it cannot silently supply any of the above guarantees |

This distinction follows the current proposed
[workflow design](../../docs/CONCURRENT_DURABLE_WORKFLOWS.md), especially
“Identities and state,” “Artifacts and incremental demand” and “Participants and
context lineage.” That design is not an implemented durable runtime. Cache hits
must never settle an unknown external effect or turn a prior answer into newly
verified truth.

## Existing source shelf: mechanisms actually read

The following immutable snapshots were already present. Existing manifests retain
their archives, hashes, extraction/restoration details and omitted metadata:
[agent acquisitions](../2026-09-30-discovered-agent-acquisition.json) and
[CLM acquisition](../2026-10-01-context-language-models-acquisition.json).
No new implementation archive was acquired or changed.

| Reference and pin | Read source, relative to its quarantine root | Supported observation and consequence |
| --- | --- | --- |
| Aider `5dc9490bb35f9729ef2c95d00a19ccd30c26339c` | `aider/coders/chat_chunks.py:6–65`; `aider/coders/base_coder.py:1003–1031,1226–1397` | Ordered chunks place system/examples/read-only material ahead of more mutable material and add explicit cache boundaries. Background summarization publishes only if its source messages still match. Keepalive sends real one-token requests on a timer: it incurs provider work, rather than making retention free. |
| Same Aider pin | `aider/repomap.py:103–169,230–261,580–674` | A ranked repository map fits a token budget. Map-result keys and refresh policies are different from tag caching by file modification time. Useful discovery representation; neither is evidence that a current source tree is exactly equivalent or that the map contains every task-critical fact. |
| gptme `1f8d73638a25253fd0e1824c3d0a72ec93007582` | `gptme/tools/autocompact/engine.py:62–140`; `resume.py:167–222,274–333` | Reduced tool observations point to separately saved output because rewriting the conversation invalidates old byte ranges. Concurrent changes cause a summarizer result to be discarded. Blackbird's immutable original identities and context revisions supply a stronger basis than mutable-file ranges or file-size/mtime snapshots. |
| smolagents `c30b115286e000e98711fae5e85993547b73d826` | `src/smolagents/local_python_executor.py:1638–1663`; `agents.py:1720–1765` | Executable code can retain values while printing a selected observation. This path truncates captured prints and rendered return values; retaining an action output object does not establish a comprehensive durable raw audit. Study the computation/presentation separation, not truncation as the preservation policy. |
| CLM `18dc11115f50f261233c5bba7937834491e307e8` | `clm/clm_harness/context_env/env.py:147–170,182–214`; `suffix_cache_reuse/README.md:1–88` | The editable mirror and command boundary are concrete. This command path shortens output before history. Suffix Cache Reuse deliberately preserves states computed against an older prefix after an edit, including special handling for hybrid recurrent layers. That is approximate server inference, not ordinary exact client-side caching. |

Public pinned trees: [Aider](https://github.com/Aider-AI/aider/tree/5dc9490bb35f9729ef2c95d00a19ccd30c26339c),
[gptme](https://github.com/gptme/gptme/tree/1f8d73638a25253fd0e1824c3d0a72ec93007582),
[smolagents](https://github.com/huggingface/smolagents/tree/c30b115286e000e98711fae5e85993547b73d826),
[CLM](https://github.com/facebookresearch/context-language-models/tree/18dc11115f50f261233c5bba7937834491e307e8).
These snapshots establish inspected mechanisms at their recorded revisions,
not claims about today's uninspected upstream releases. Historical instructions
inside them have no workspace authority.

## Primary literature and its actual boundary

**Prefix scheduling.** SGLang §3 and appendix D expose shared prompt forks,
radix-tree prefix reuse and ordering by matched prefix length. The offline
optimality statement assumes sufficient cache capacity and a known request
batch; online output lengths disturb that model. The paper explicitly recognizes
starvation under greedy scheduling. Its prompt code movement also explicitly
does not strictly preserve the computation. Thus identical-prefix preservation
and prompt reordering require different quality claims. This study supports
examining prefix locality, not adopting the serving runtime or its headline
speedups. [SGLang, v2](https://arxiv.org/html/2312.07104v2).

**Tool waits and continuity.** Continuum §§3–4 and its limitations model cache
retention using tool-completion probability, reconstruction cost, queueing delay
and GPU-memory opportunity cost. A fast reload alone does not remove a returning
program's queue delay. Historical latency distributions and turn progress can
be misleading under changed workloads. The mechanism belongs to a server that
owns KV residency and admission; a hosted-API client cannot implement it merely
by assigning a session key. [Continuum, v6](https://arxiv.org/html/2511.02230v6).

**Concurrency working sets.** EfficientAgent §§2–5 connects reuse to whether cached
prefixes survive the other agents' intervening work; a fast host/GPU transfer
cannot rescue a prefix already evicted from the host tier. Its dependency-preserving
replay fixes request/output tokens and recorded tool delays to isolate serving
policies, while live task runs address a different claim. This is a recent
preprint, not reproduced here. It supplies a useful warning against assuming
that more parallel agents always improve completion time or cache reuse.
[EfficientAgent, v1](https://arxiv.org/html/2609.33762v1).

**Retrieval.** RepoCoder §2 iterates retrieval using a previous generated completion
to improve the next query. Its limitations and appendix D show nonmonotonic
quality: subsequent iterations can break earlier successes, misleading examples
can produce wrong APIs, and exact-match scoring can reject functional code.
The studied task is repository completion, not whole coding-agent maintenance.
This supports task-directed source discovery with functional checks, not a
universal retrieval budget or a claim that iterative retrieval saves tokens.
[RepoCoder, EMNLP 2023](https://aclanthology.org/2023.emnlp-main.151.pdf).

**Code actions.** CodeAct unifies tool composition and control flow in executable
Python and compares it with constrained action interfaces across API tasks and
models. Its abstract establishes a capability result; it does not establish that
Lua orchestration has the same token savings on Blackbird coding workflows. The
transferable idea is writing loops, branches and transformations once instead of
requesting a fresh model decision for each mechanical step.
[CodeAct](https://arxiv.org/abs/2402.01030).

**Context quality.** Lost in the Middle studies position sensitivity in
multi-document QA and key/value retrieval. Its findings challenge the assumption
that everything in a long prompt remains equally usable; they do not establish
that deleting the middle of a coding trajectory preserves constraints or quality.
[Lost in the Middle](https://aclanthology.org/2024.tacl-1.9/).

**Editable context.** CLM §3, §4.1 and appendix C distinguish context transformations
from size reduction and account for the reusable prefix when estimating compute.
An early edit can cause surviving suffix text to be computed again. Its programmable
surface supports repairs that restore information as well as reductions. The
existing [Blackbird source reconsideration](../2026-10-06-compaction-source-consequences.md)
already translates this into source recovery, exact facts, stale-publication
checks and final-request audit. The paper's FLOP model is not a hosted-provider
invoice. [Context Language Models, v1](https://arxiv.org/html/2609.37725v1).

Read scope is bounded to these mechanisms and the source sections above. Paper
HTML/PDFs were inspected on their primary sites; no new local reference artifact
was ingested. The CodeAct HTML version attempted was unavailable, so its abstract
and the already-acquired executable-code source were used without claiming a
fresh full-paper reread. Rhizome's read-only research-watch path
`../rhizome/papers/` is absent; no transferable findings are invented.

## Candidate native mechanisms

These are proposed directions inferred for Blackbird; the provider-contract and
native-inventory lanes determine exact endpoint eligibility and existing seams.
None supplies a measured saving yet.

1. **Stable assembly with visible causes of change.** C++ should retain immutable
   typed fragments and their source/representation identity; Lua should select
   and transform them. Encode only at the provider boundary. Keep equivalent
   instruction/tool fragments in a deterministic order, and avoid injecting a
   changing timestamp, queue status or incidental identifier into an otherwise
   stable leading block. An actually changed instruction takes effect under the
   existing activation rule even if it loses cache reuse. Never reorder roles,
   signed reasoning, tool obligations or prompt text merely to improve a cache
   graph. Reusing native bytes and obtaining a provider KV hit remain separately
   observed facts.

2. **Versioned working views over preserved observations.** Give a model exact
   bounded range/search/projection operations over retained original output.
   A view should identify its source, revision, selected ranges/transformation
   and omitted extent. Return useful diagnostics and recovery handles instead of
   putting every stdout byte into every subsequent prompt. Capture the complete
   observation first. A continuation can fetch an omitted central line or grow
   context. A simple head/tail slice is only one authored view; it is inadequate
   for arbitrary compiler logs, protocol transcripts or source requirements.
   Selection and projection have native CPU/I/O costs and may require extra
   tool/model turns.

3. **Deliberate context epochs.** A request pins a context revision. A planned
   edit publishes against its expected base; a late summary or tool result
   cannot overwrite a newer branch. Keep append-only growth when it remains
   useful; offer model-controlled repair/retrieval/compaction when it improves
   the task. Inspect the first changed fragment and the remaining suffix when
   comparing alternatives. Cache locality can inform the choice but cannot
   forbid needed repairs or dictate a universal compaction threshold. Explicit
   import preserves peer-context ancestry without copying every peer transcript.

4. **Code-mode computation with selected observations.** Lua can compose
   independent reads, run a mechanical iteration, compare exact values, retain
   intermediate artifacts and emit the specific evidence needed for the next
   model decision. Reuse the native execution/effect owner rather than creating
   an unaudited interpreter path. Durable recorded-command segments must preserve
   each actual command identity and completion; a shorter visible transcript
   cannot conceal executed effects. Keep observation-dependent decisions with
   the model when the authored program does not encode them. A tool-discovery
   index plus selected exact documentation is a plausible alternative to
   sending every tool's full schema; changed discovery/selection can itself
   cost calls and disturb prefixes, so compare it directly.

5. **Fair scheduling that may consider locality.** Independent ready tasks can
   carry their endpoint/model/profile, context branch and immutable request-base
   identity. A future scheduler may prefer compatible work only within its real
   readiness, grants and fairness rules. Parked tools must release physical
   executor workers, as the workflow design requires. Do not delay a necessary
   successor, duplicate work, manufacture a warmup request or starve another
   model merely to increase hits. Hosted backend placement/residency is outside
   Blackbird's ownership; local predicted reuse is not observed residency.

6. **Eligible artifact reuse before another expensive producer.** Connect source
   discovery, tool projections and build/review artifacts to the planned demand
   graph. Content digests locate candidates; actual input/environment equality,
   freshness and output validity decide eligibility. Reusing a pure projection
   is easier to justify than skipping an external read, model call or mutation.
   Expose fresh execution, eligible reuse and permitted coalescing explicitly.
   Current late-output eligibility and effect-reconciliation rules remain in
   force after invalidation. This is the workflow design's result-reuse lane,
   not another name for provider caching.

## Costs and quality must remain separate

For an append-only conversation with initial input S and d new input tokens per
turn, n submissions contain `n*S + d*n*(n-1)/2` input tokens, ignoring generated
text and protocol overhead. This arithmetic describes submitted input, not
recomputed tokens, retained-memory copies or billable cost. Generated text
becomes subsequent input too. A warm prefix can reduce prefill work while
leaving network bytes, cache-read billing and attention during decoding.

Use each actual endpoint's units, overlapping-field rules and prices from the
provider lane. A generic comparison is:

`total = uncached_input_cost + cache_write_cost + cache_read_cost + output_cost
         + extra_provider/tool/storage/transport_cost`.

A compaction candidate has an immediate transformation/generation cost, newly
missed suffix cost and possible retrieval/repair cost; its benefit accrues over
future useful turns. A warmup candidate adds real requests and can only pay if
the expected future avoided misses exceed all of its charges. Code-mode batching
can save decision turns but add generated program tokens, repair work and wasted
speculation. Broader prefixes can improve matching while supplying irrelevant
context. More forks can improve coverage while consuming more output tokens,
tool work and cache capacity. No single token count settles these choices.

The tertiary consumer should derive prefix-change classifications, usage
arithmetic, per-task cost and latency distributions from retained raw events.
Blackbird producers capture exact request/attempt/context linkage and original
provider usage; the recorder preserves them. Do not add formatted summaries,
hit-rate calculations or inferred usage to the producer/recorder. Provider
observations can be absent or ambiguous; missing cache fields are not measured
zeroes. Dollar accounting under a paid API does not infer subscription allowance
or quota savings.

## Direct comparisons to settle the hypotheses

These define useful future checks, not an approved live benchmark or request to
run one. First establish the actual supported provider route and raw observation
coverage. Hold task, source/environment, model/effort and stopping condition
constant where the claim requires it; preserve failed attempts and all costs.

| Question | Direct comparison and oracle | Costs/limits that must be reported |
| --- | --- | --- |
| Did stable assembly help without changing meaning? | Same native request values assembled repeatedly; compare exact final boundary representation and the first changed fragment. Any prompt-order/content change is a distinct behavioral variant. A later provider comparison needs actual cache usage and cold/warm conditions. | Construction/encoding/copies, submitted bytes/tokens, observed usage, first-token and completion latency; stable bytes alone do not prove a cache hit. |
| Can a view replace repeated full output usefully? | Full observation versus selected view with exact drill-down. Predeclare task-critical facts including a central fact outside head/tail; require actual recovery and the same task outcome. | Projection/read work, extra calls, omitted facts, repair cost and output tokens. Do not score source preservation as task competence. |
| When does compaction pay? | Same continuation with append-only context, a selected edit and edit plus retrieval. Check independently declared constraints, actual functional work and final requests across revisions. | Transformation request, prefix invalidation, later reads, native storage and all continuation cost; summarize quality failures explicitly. |
| Does Lua composition save useful work? | Mechanical tool sequence versus authored program over the same inputs. Check actual outputs/effects and failure/cancellation settlement, not just a smaller transcript. | Model decisions, code tokens, tool invocations, retries, interpreter/native overhead and wasted branches. |
| Does locality scheduling improve the workflow? | Dependency-preserving request replay can isolate queue/cache behavior; separate live task continuation checks adaptive quality. Include dissimilar prefixes and long tool waits. | Entire workflow completion, ready-to-service delays/fair progress, concurrency, backend cache observations and total work; replay cannot establish adaptive task success. |
| Is result reuse valid? | Repeat a declared pure demand, then independently change source/environment/schema/freshness or corrupt/remove output. Correct selection or recomputation must follow actual eligibility. | Lookup/verification/retention cost, avoided producer work, provenance and current-publication eligibility; never rerun uncertain effects as a cache miss. |

Native representation and preservation checks can use exact expected bytes and
retained identities. Coding quality needs actual functional/specification
oracles appropriate to the task; a model's self-rating, prompt length or unchanged
patch text is insufficient. The historical
[token-efficiency synthesis](../history/llm_token_efficiency_comprehensive_synthesis.md)
is a lead list, not evidence that its reported cross-paper speedups multiply or
transfer to this model, account route and workload.

The first useful design unit should therefore expose stable final-request
identity and honest raw usage linkage. Recoverable authored views and Lua
composition can follow through the existing context/effect owners. Scheduling
and incremental result reuse belong with the proposed concurrent/durable runtime;
server KV algorithms remain independently governed service research unless an
actual supported service contract makes them available.
