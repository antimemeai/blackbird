# Tool output lifecycle in agent context: literature and engineering evidence (as of 2026-10-19)

Status legend used below: **[peer-reviewed]** = accepted at a journal or workshop, per the source page; **[preprint]** = arXiv only, no venue on the page; **[vendor claim]** = a company blog or doc about its own product, internal evaluation, no independent replication; **[practitioner]** = engineering writeup with no controlled evaluation.

No PDFs were downloaded. All sources were read as arXiv HTML or abstract pages, or as official blog and documentation pages.

## 1. JetBrains Research, "The Complexity Trap" (2025): observation masking vs LLM summarisation

### Takeaway
On SWE-bench Verified with SWE-agent, replacing observations older than 10 turns with a placeholder cuts cost about 50% against the raw agent. Its solve rate is statistically indistinguishable from LLM summarisation in 4 of 5 configurations, and it is usually cheaper. Summarisation's weak points are hidden: it lengthens trajectories, its summary calls miss the cache, and with a long-reasoning model it can lose accuracy. A hybrid (mask first, summarise late) is cheapest. The evidence covers SWE tasks with verbose observations only.

### Cited Findings
- Paper: Lindenbauer, Slinko, Felder, Bogomolov, Zharov (JetBrains Research), arXiv 2508.21433. v1 29 Aug 2025, v3 27 Oct 2025, described as the camera-ready for the 4th DL4C workshop at NeurIPS 2025 **[peer-reviewed, workshop]** — [arXiv abs](https://arxiv.org/abs/2508.21433)
- Abstract claim: observation masking "halves cost relative to the raw agent" while matching, and sometimes slightly exceeding, LLM-summary solve rates. The hybrid cuts cost a further 7% against masking and 11% against summary — [arXiv abs](https://arxiv.org/abs/2508.21433)
- **Methods.**
  - Observation masking (OM) replaces environment observations older than a rolling window of **M = 10** turns with a placeholder and keeps all reasoning and actions in full. M = 10 was tuned on SWE-agent.
  - LLM-Summary summarises **N = 21** turns at a time, keeps the **10** most recent turns verbatim and first triggers after 31 turns. It uses an OpenHands-style summary prompt at temperature 0.
  - The hybrid applies OM (window 10) early and defers summarisation (N = 43, tail 10). Its summariser sees the unmasked context.
  - Setup: SWE-agent, SWE-bench Verified (500 instances), turn limit 250, paired bootstrap B = 10,000. Models: Qwen3-32B (thinking and non-thinking), Qwen3-Coder 480B, Gemini 2.5 Flash (thinking and non-thinking).
  - [arXiv HTML v3](https://arxiv.org/html/2508.21433v3)
- **Results (solve rate %, then cost per instance, Raw / OM / Summary)** — [arXiv HTML v3](https://arxiv.org/html/2508.21433v3):
  - Qwen3-32B: 17.0 / 15.0 / 16.0; $1.12 / $0.55 (−50.9%) / $0.50 (−55.4%)
  - Qwen3-32B thinking: 23.0 / 24.6 / 24.8; $0.51 / $0.46 (−9.8%) / $0.51 (0%)
  - Qwen3-Coder 480B: 53.4 / 54.8 / 53.8; $1.29 / $0.61 (−52.7%) / $0.64 (−50.4%)
  - Gemini 2.5 Flash: 32.8 / 35.6 / 36.0; $0.41 / $0.18 (−56.1%) / $0.24 (−41.5%)
  - Gemini 2.5 Flash thinking: 40.4 / 36.4 / 31.4; $0.56 / $0.24 (−57.1%) / $0.25 (−55.4%). This is the one configuration where both strategies fall below Raw, and summary falls about 5 points below OM.
  - OM is cheapest in 4 of 5 configurations. Apart from Gemini-thinking, neither strategy significantly changes solve rate against Raw.
- **Observation tokens are about 84% of an average SWE-agent turn**, which is why masking observations alone captures most of the savings — [arXiv HTML v3](https://arxiv.org/html/2508.21433v3)
- **Failure mode 1: trajectory elongation from summaries.**
  - Gemini 2.5 Flash: mean 52 turns with Summary, 44 with OM, 50 Raw. Summary runs are about 15% longer than OM.
  - Qwen3-Coder 480B: Summary runs are about 15% longer than Raw and 13% longer than OM.
  - The authors suggest summaries signal the agent to keep going, which erodes the efficiency gain.
  - Masking is not immune: on Qwen3-32B, OM lengthened trajectories about 13% against Raw.
  - A critic-enhanced summariser (150 instances) did not raise solve rate and lengthened trajectories further.
  - [arXiv HTML v3](https://arxiv.org/html/2508.21433v3)
- **Failure mode 2: summary calls cost money and miss the cache.**
  - Summary calls are 0.65–7.20% of instance cost: Qwen3-Coder 7.20% ($0.0439), Gemini Flash 6.71%, Gemini Flash thinking 5.24%, Qwen3-32B 2.86%, Qwen3-32B thinking 0.65%.
  - They "rarely hit the cache". Subtracting them largely closes the efficiency gap between Summary and OM.
  - Running the hybrid with N = 21 instead of 43 hurt cost efficiency because of KV-cache inefficiency and summarisation overhead.
  - [arXiv HTML v3](https://arxiv.org/html/2508.21433v3)
- **Hybrid result (Qwen3-Coder 480B, SWE-bench Verified-50 subset):** +2.6 points solve rate over Raw, −7% cost against OM, −11% against Summary. The authors project savings of about $20 against OM and $35 against Summary over the full benchmark — [arXiv HTML v3](https://arxiv.org/html/2508.21433v3)
- **Transfer to OpenHands (Gemini 2.5 Flash, 50 instances).**
  - The SWE-agent window of 10 did not transfer and degraded performance.
  - Retuned to M = 58, OM matched Summary on cost and solve rate.
  - The authors attribute the difference to OpenHands retaining retry turns that SWE-agent elides. They call this initial evidence only.
  - [arXiv HTML v3](https://arxiv.org/html/2508.21433v3)
- **Stated limitations** — [arXiv HTML v3](https://arxiv.org/html/2508.21433v3):
  - SWE observations are verbose, which favours masking. Domains with terse tool output may differ.
  - Both triggers are fixed and non-adaptive. OM ignores staleness, for example a file that was later modified, and Summary ignores subgoal boundaries.
  - With short trajectories (Qwen3-32B thinking, median about 15 turns) summarisation never pays, though OM still cut cost about 10%.
  - Qwen3-32B costs are inflated because the Alibaba pricing used does not separate cache hits.

### Inferences
- The masking window is a scaffold-specific hyperparameter (10 in SWE-agent, 58 in OpenHands). It is not a universal constant, so any implementation needs a tunable or adaptive window.
- Masking keeps the prefix append-only between window shifts and needs no extra model call. That is probably the main reason it beats summarisation on cost, more than the token count itself.
- The Gemini-thinking result suggests long-reasoning models may depend more on old observations, or be more harmed by lossy summaries. That is a warning against assuming compaction is free for reasoning models.

### Gaps
- The exact placeholder text used by OM was not extracted. Per the paper, the SWE-agent original collapses old observations to a single line each.
- No result covers masking with a recoverable locator (for example "output stored at X") as opposed to a bare placeholder.
- The JetBrains Research blog post accompanying the paper was not fetched. Any extra claims in it are unverified here.

## 2. SWE-agent's "last N observations" ablation and OpenHands condenser evaluations

### Takeaway
The original SWE-agent ablation found that keeping only the last 5 observations in full, with older ones collapsed to one line, beat full history: 18.0% vs 15.0% resolved on a SWE-bench Lite subset with GPT-4 Turbo. The same paper shows interface-level shaping of tool output matters as much or more: a 100-line file viewer and summarised search results. OpenHands' condenser is a vendor result: about 2x lower per-turn cost at an equal solve rate (54% vs 53%) on a SWE-bench Verified subset.

### Cited Findings
- SWE-agent (Yang, Jimenez et al.), arXiv 2405.15793, v3 revised 11 Nov 2024 — [arXiv HTML](https://arxiv.org/html/2405.15793). The fetched page does not name a venue. It is commonly cited as NeurIPS 2024, but that was not confirmed from the fetched page.
- **Context management ablation (Table 3, SWE-bench Lite subset, GPT-4 Turbo):** "last 5 observations" (default) 18.0% resolved vs full history 15.0% (−3.0). Older observations are collapsed to a single line each; the exact wording is in Appendix C, Fig. 31, which was not extracted — [arXiv HTML](https://arxiv.org/html/2405.15793)
- **Other ablations on output shaping (same setup)** — [arXiv HTML](https://arxiv.org/html/2405.15793):
  - File viewer: 100 lines (default) 18.0 vs 30 lines 14.3 vs full file 12.7. Dumping the whole file cost 5.3 points.
  - Search: summarised results (default) 18.0 vs iterative 12.0 vs no search 15.7.
  - Editor: lint-gated edit 18.0 vs edit without linting 15.0 vs no edit 10.3.
  - No demonstration: 16.3.
- **OpenHands context condensation blog** (Calvin Smith, 9 Apr 2025) **[vendor claim]** — [OpenHands blog](https://openhands.dev/blog/openhands-context-condensensation-for-more-efficient-ai-agents):
  - Above a size threshold, the condenser summarises older events, covering user goals, progress, remaining work, critical files and failing tests. It keeps recent events intact.
  - It triggers only at threshold so prompt caching keeps working between condensations.
  - On a SWE-bench Verified subset, per-turn cost settles at "less than half" the baseline ("up to 2x" reduction). Solve rate is 54% vs 53% baseline.
  - The post claims the baseline scales quadratically and the condensed agent linearly, assuming events of similar size. No measured figure is given for this.
  - Caveats: subset only, and the agent takes extra turns to condense.
- An independent check: JetBrains found that in OpenHands, OM with a retuned window (58) matched the LLM-summary condenser on cost and solve rate (Gemini 2.5 Flash, 50 instances) — [arXiv HTML v3](https://arxiv.org/html/2508.21433v3)

### Inferences
- The SWE-agent result implies that elision can improve accuracy, not only cut cost, at least for GPT-4-Turbo-era models. This fits the context-rot evidence in section 5.
- Bounding what a tool emits in the first place (windowed viewers, summarised search) had larger measured effects than history elision. Output-at-source design is a first-class lever.

### Gaps
- No public, peer-reviewed OpenHands condenser evaluation with full SWE-bench Verified numbers and variance was found. The blog uses a subset with no confidence intervals.
- The SWE-agent ablations are on a small Lite subset with no reported variance. Later SWE-agent versions' history processors were not examined.

## 3. Manus "context engineering" lessons

### Takeaway
Manus's widely cited July 2025 post is practitioner guidance without controlled evaluations **[practitioner/vendor]**. Its main lessons:
- Optimise for KV-cache hit rate: keep a stable prefix, append only, serialise deterministically.
- Mask tool availability through logits rather than removing tools.
- Treat the file system as externalised memory, and make compression restorable by keeping a URL or path.
- Recite goals in a todo.md at the context tail.
- Keep errors in context.
- Avoid few-shot uniformity.

### Cited Findings
- Author Yichao "Peak" Ji, published 18 Jul 2025 — [Manus blog](https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus)
- **KV-cache** is called "the single most important metric" for a production agent — [Manus blog](https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus):
  - Average input:output ratio is about **100:1**.
  - With Claude Sonnet, cached input costs **$0.30/MTok** against **$3/MTok** uncached (10x).
  - A timestamp at the top of the system prompt can kill caching. Non-deterministic JSON key order silently breaks it.
  - Add explicit cache breakpoints where the provider requires them.
- **Mask, don't remove** — [Manus blog](https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus):
  - Adding or removing tools mid-task invalidates the cache, because tools sit near the front of the prefix.
  - Removal also confuses the model when earlier actions reference vanished tools.
  - Manus uses a state machine that masks token logits, with Auto / Required / Specified function-calling modes enforced via response prefill.
  - Tool names share prefixes (`browser_`, `shell_`) so whole groups can be constrained.
- **File system as context** — [Manus blog](https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus):
  - 128K+ windows are often insufficient, and performance degrades at length.
  - Compression must be **restorable**: drop a web page's content but keep its URL, or drop a document but keep its sandbox path.
- **Recitation** — [Manus blog](https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus): a typical task is about **50 tool calls**. Rewriting todo.md pushes the plan into the recent tail, where attention is strongest.
- **Keep errors in context** — [Manus blog](https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus): leaving failed actions and stack traces in context shifts the model away from repeating them. Hiding them removes the evidence the model needs to adapt.
- **Few-shot pitfall** — [Manus blog](https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus): repetitive context induces mimicry, for example drifting into a rhythm while reviewing 20 resumes. The remedy is controlled variation in serialisation and phrasing.

### Inferences
- The "keep errors" lesson pulls against naive elision policies that clear oldest-first regardless of content. A policy may need to protect failure observations, or keep a compact error record, when clearing bulk output.
- "Restorable compression" is the same idea as Anthropic's "lightweight identifiers" (section 4). Two independent vendors converge on keeping a locator when dropping content.

### Gaps
- Manus publishes no numbers on task success under its policies, no measured cache-hit rates and no ablations. All claims are experiential.

## 4. Anthropic: "Effective context engineering", context editing, memory tool, compaction

### Takeaway
Anthropic frames context as a finite "attention budget" degraded by context rot. It recommends:
- just-in-time retrieval via identifiers;
- compaction, maximising recall first, then precision;
- tool-result clearing as "the lightest form of compaction";
- structured notes;
- sub-agents returning 1–2k-token distillates.

The product numbers are internal and unreplicated **[vendor claim]**: context editing alone +29%, editing plus memory tool +39% on an internal agentic-search eval, and −84% tokens on a 100-turn web-search eval. The API docs make the cache interaction explicit: clearing invalidates the cached prefix, so `clear_at_least` exists to batch clearing.

### Cited Findings
- "Effective context engineering for AI agents", Anthropic Engineering, 29 Sep 2025 **[vendor guidance]** — [Anthropic](https://www.anthropic.com/engineering/effective-context-engineering-for-ai-agents):
  - Context rot occurs across all models: recall degrades as tokens grow.
  - Just-in-time loading via file paths, stored queries and links, as in Claude Code's use of `head` and `tail` on large data. This is slower than precomputed retrieval, so it is often paired with up-front files like CLAUDE.md.
  - Compaction in Claude Code keeps architectural decisions, unresolved bugs and implementation details, and continues with the **5 most recently accessed files**. Advice: tune the compaction prompt for recall first, then precision.
  - Tool-result clearing is called the "safest, lightest touch" form of compaction.
  - Structured note-taking: in the Claude Pokémon example, the agent tracked progress across 1,234+ steps through context resets.
  - Sub-agents may each use tens of thousands of tokens but return a condensed 1,000–2,000-token summary.
- Context management announcement, 29 Sep 2025, launched with Sonnet 4.5 **[vendor claim]** — [Claude blog](https://claude.com/blog/context-management):
  - Context editing clears "stale tool calls and results" near the token limit.
  - The memory tool is a client-side file directory with create, read, update and delete operations that persists across conversations.
  - Numbers: memory + context editing **+39%** over baseline on an internal agentic-search eval; context editing alone **+29%**; on a **100-turn web search** eval, context editing let agents finish otherwise-failing workflows while cutting token consumption **84%**. No further methodology is published.
- Context editing API docs (current as of Oct 2026) — [Claude docs](https://platform.claude.com/docs/en/build-with-claude/context-editing):
  - Beta header `context-management-2025-06-27`.
  - Clearing runs server-side, so the client keeps the full history.
  - `clear_tool_uses_20250919` defaults: trigger 100,000 input tokens; keep the 3 most recent tool use/result pairs; clear oldest first; replace each cleared result with placeholder text. Options: `clear_tool_inputs`, `exclude_tools` (never cleared), and `clear_at_least` (skip the edit unless it removes at least that many tokens).
  - **Caching:** tool-result clearing invalidates cached prefixes and forces a cache write. `clear_at_least` is the stated tool for making invalidation worthwhile.
  - Thinking-block clearing (`clear_thinking_20251015`) trades cache hits against window space.
  - Server-side compaction (`compact_20260112`) is now recommended as the primary strategy, with context editing for finer control. SDK client-side `compaction_control` is deprecated or removed.
  - With the memory tool, Claude gets an automatic warning near the clearing threshold so it can save essentials before results are cleared.
  - Responses report `applied_edits` (cleared tool uses, cleared input tokens), and `count_tokens` previews pre- and post-clearing sizes.

### Inferences
- Anthropic's default (clear oldest, keep 3 tool pairs, trigger at 100k) is structurally the same as JetBrains/SWE-agent observation masking, implemented server-side with a cache-aware batching knob. The 84% figure and the JetBrains ~50% figure are consistent in direction but not comparable: one is internal web search, the other SWE-bench.
- The pre-clearing memory warning is the Anthropic version of "restorable compression": the agent is invited to extract what it needs before elision, instead of having a locator to re-fetch.

### Gaps
- Exact placeholder wording, the internal eval's task set and model, and variance are unpublished.
- No published Anthropic numbers for server-side compaction (`compact_20260112`) quality were found.

## 5. ACON, ACE, MemGPT/Letta, context rot, lost in the middle, and 2025–2026 papers

### Takeaway
Learned or optimised compression can beat heuristics.
- ACON reports 26–54% peak-token reduction with accuracy kept or improved, and shows FIFO and retrieval baselines degrade badly on harder AppWorld tasks.
- Small trained pruners (SWE-Pruner, Squeez, CoACT) cut 23–92% of observation tokens with success held near baseline.

The 2026 literature increasingly shows that end-task success hides costs: more re-retrieval, blocked actions, run-to-run instability and non-linear "trajectory amplification". ACE shows that full LLM rewrites of accumulated context can collapse it (18,282 to 122 tokens, accuracy below baseline). MemGPT established OS-style paging with explicit memory-pressure signals.

### Cited Findings
**ACON** — Kang et al. (Microsoft/KAIST), arXiv 2510.00615, v3 dated 1 Jun 2026 on the page. The keywords mention ICML, but acceptance was not confirmed **[preprint, venue unconfirmed]** — [arXiv HTML](https://arxiv.org/html/2510.00615); code at [github.com/microsoft/acon](https://github.com/microsoft/acon)
- Method:
  - An LLM compresses both history and the latest observation, only above a length threshold.
  - The natural-language compression guideline is optimised from contrastive failures: tasks that succeed uncompressed but fail compressed. A utility step targets success, and an optional compression step targets brevity.
  - The compressor is distilled into Qwen3-14B, Qwen3-8B and Phi-4 with LoRA.
- Results:
  - 26–54% peak-token reduction, and distilled compressors keep over 95% of teacher performance.
  - Small agents improve by up to 46% (AppWorld 32%, OfficeBench 20%, 8-objective QA 46%). Qwen3-14B on AppWorld goes from 25.6% to 33.9%.
  - gpt-4.1 on AppWorld (history compression): no compression 56.0 accuracy at 9.93k peak tokens; ACON 56.5 at 7.33k.
  - gpt-4.1 on OfficeBench: 76.84 uncompressed vs 74.74 ACON, with peak tokens 7.27k to 4.93k, a small accuracy loss.
- **Baselines:** on AppWorld, FIFO (keep recent) scores **45.8** and retrieval **27.4** against 56.0 uncompressed. Both degrade badly on medium and hard tasks.
- Failure mode noted: over-compression drops subtle facts needed for final decisions, especially on OfficeBench and QA.

**ACE (Agentic Context Engineering)** — Zhang et al. (SambaNova, UC Berkeley, Stanford), arXiv 2510.04618, v3 29 Mar 2026, no venue on the page **[preprint]** — [arXiv HTML](https://arxiv.org/html/2510.04618)
- Scope: this is about evolving instruction/strategy "playbooks", not tool-output elision. It is relevant because of how it rewrites context.
- Method: Generator → Reflector → Curator, which emits itemised **delta** bullets merged by non-LLM logic, plus grow-and-refine with embedding de-duplication. There are no full rewrites.
- Failure modes named:
  - **Brevity bias.**
  - **Context collapse**: a full LLM rewrite shrank an 18,282-token context at 66.7 accuracy to **122 tokens at 57.1**, below the 63.7 no-context baseline (AppWorld).
- Results:
  - AppWorld +10.6% average, with ReAct+ACE at 59.4 vs 42.4 for ReAct. The top-ranked IBM CUGA (GPT-4.1) scores 60.3, while ACE uses DeepSeek-V3.1.
  - Finance +8.6%.
  - Adaptation latency −82.3% and rollouts −75.1% vs GEPA (offline AppWorld). Latency −91.5% and token cost −83.6% vs Dynamic Cheatsheet (online FiNER).
- Limitation: it needs reliable feedback signals, and degrades without them.

**MemGPT (now Letta)** — Packer, Wooders, Lin, Fang, Patil, Stoica, Gonzalez, arXiv 2310.08560, v1 12 Oct 2023, v2 12 Feb 2024 **[preprint]** — [arXiv HTML v2](https://arxiv.org/html/2310.08560v2)
- Architecture:
  - Main context holds read-only system instructions, a fixed-size working context editable only by function calls, and a FIFO message queue headed by a recursive summary of evicted messages.
  - Recall storage keeps all messages. Archival storage is vector-searchable with **paginated** results.
  - At about **70%** of the window, a memory-pressure warning is injected. At about 100%, about 50% of the queue is evicted into the recursive summary.
  - A heartbeat flag chains calls.
- Deep memory retrieval (accuracy): GPT-4 32.1% → 92.5%; GPT-4 Turbo 35.3% → 93.4%; GPT-3.5 38.7% → 66.9%.
- Nested KV: GPT-3.5 baseline at 0% by 1 nesting level, GPT-4 and GPT-4 Turbo at 0% by 3 levels, MemGPT+GPT-4 unaffected.
- Limitations:
  - The embedding retriever often ranks the gold document outside the top dozen, and the agent sometimes stops paging early.
  - Weak function-calling (GPT-3.5) degrades it badly.
  - Document QA used only 50 questions.

**Context rot (Chroma)** — Hong, Troynikov, Huber, 14 Jul 2025, technical report **[vendor research, not peer-reviewed]** — [Chroma](https://www.trychroma.com/research/context-rot)
- 18 LLMs tested. Performance degraded with input length in every experiment.
- Low needle-question similarity accelerates degradation. Even one distractor hurts, and four hurt more.
- Shuffled haystacks beat coherent ones, which the authors call counterintuitive. Needle position had no notable effect in their setup.
- LongMemEval: focused prompts (about 300 tokens) scored significantly above full ones (about 113k tokens) on 306 prompts.

**Lost in the Middle** — Liu et al., arXiv 2307.03172, TACL **[peer-reviewed]** — [arXiv abs](https://arxiv.org/abs/2307.03172)
- Performance is highest when relevant information is at the beginning or end of the context and degrades significantly in the middle, even for long-context models.
- Note the tension with Chroma's report of no notable position effect in its needle setup.

**2025–2026 trained or structural compression papers** (all **[preprint]**):
- **Context-Folding** (Sun et al., arXiv 2510.11967, 13 Oct 2025): the agent branches into a sub-trajectory and folds it into a short outcome summary, trained with FoldGRPO (RL with process rewards). It matches or beats ReAct with about **10x smaller active context** and "clearly outperforms" summarisation-based management on Deep Research and SWE tasks — [arXiv abs](https://arxiv.org/abs/2510.11967)
- **AgentFold** (Ye et al., Alibaba Tongyi, arXiv 2510.24699, 28 Oct 2025): per-step folding, with granular condensation plus deep consolidation of finished sub-tasks. The 30B-A3B SFT model scores 36.2% BrowseComp and 47.3% BrowseComp-ZH — [arXiv abs](https://arxiv.org/abs/2510.24699)
- **SWE-Pruner** (Wang et al., arXiv 2601.16746, v1 23 Jan 2026, v4 7 May 2026): the agent states a goal ("focus on error handling") and a 0.6B neural skimmer keeps relevant lines while preserving syntax. It cuts 23–54% of tokens on SWE-bench Verified with success "improved", and reaches up to 14.84x compression on LongCodeQA — [arXiv abs](https://arxiv.org/abs/2601.16746)
- **Squeez** (Kovács, arXiv 2604.04979, 4 Apr 2026): task-conditioned tool-output pruning that returns the smallest **verbatim** evidence block for a query. Qwen 3.5 2B with LoRA reaches 0.86 recall and 0.80 F1 while removing 92% of input tokens, 11 recall points above zero-shot Qwen 3.5 35B-A3B, on a 618-example curated test set. Downstream task success is not reported in the abstract — [arXiv abs](https://arxiv.org/abs/2604.04979)
- **CoACT** (Chen et al., arXiv 2607.02911, 3 Jul 2026): trains the compressor with a **next-action-preservation** reward (the compressed observation must yield the same next action) plus a length reward. It cuts total tokens **33.0%** on SWE-bench Verified across 3 models, with effectiveness "close to" uncompressed — [arXiv abs](https://arxiv.org/abs/2607.02911)
- **TRACE / execution instability** (Min et al., arXiv 2608.06503, v1 6 Aug 2026, v2 14 Oct 2026, "preliminary empirical study"): repeated compaction on AppWorld weakens the influence of recent interactions and increases blocked actions, repeated exploration and run-to-run inconsistency. It proposes boundary-local evaluation (paired continuations from the same state at each compaction event) and verifier-guided prompt optimisation. The abstract gives no effect sizes — [arXiv abs](https://arxiv.org/abs/2608.06503)
- **"What does context compression cost an agent?"** (Liu, arXiv 2608.16370, 17 Aug 2026) — [arXiv abs](https://arxiv.org/abs/2608.16370):
  - Setup: a deterministic 24-turn planning environment plus ALFWorld, 3 models.
  - Retrieval calls rose in all 6 model-regime comparisons (5 of 6 significant after Holm correction), while completion changes at 5x compression were not significant in any cell.
  - GPT-5.5: completion 80% → 85% (n.s.) while retrieval calls rose **21.0 → 63.9** (p = .002). DeepSeek lost completion only at 10x.
  - Replacing retained state with irrelevant content raised retrieval 57% with no completion change.
  - Random selection performed comparably to an offline hindsight oracle.
  - In ALFWorld, sliding compression caused no retrieval surge, so the effect is environment-dependent.
- **ICLR (Interaction-aware Compression for Long-horizon Reasoning)** (Wang et al., arXiv 2609.29875, 24 Sep 2026; "ICLR" is the method's name, not the venue) — [arXiv abs](https://arxiv.org/abs/2609.29875):
  - A training-free method that compresses only **reasoning** blocks, ranked by frozen proxy entropy, and keeps actions, tool calls and observations intact.
  - On 260 WorkBuddyBench tasks, reward rose 0.699 → 0.718 and input / output / cache-read tokens fell 25.5% / 14.4% / 33.3%.
  - Reports **"trajectory amplification"**: local deletion causes non-linear changes in total compute by altering later interactions.
  - Finds historical reasoning becomes more replaceable once the derived state has been externalised into code, files, tool outputs or environment feedback.

### Inferences
- There are two opposite but compatible strategies in the evidence. One elides observations and keeps reasoning and actions (JetBrains OM, SWE-agent, Anthropic clearing). The other elides reasoning and keeps observations (ICLR 2609.29875). Both work in their settings. What they share is that **actions/tool calls stay** and whatever is dropped can be re-derived from external state.
- ACE's collapse result and JetBrains' summary-elongation result point the same way: free-form LLM rewrites of accumulated context are the riskiest operation. Deltas, verbatim extracts (Squeez) or placeholders are safer.

### Gaps
- ACON's and ACE's venue status could not be confirmed. SWE-Pruner, Squeez, CoACT, TRACE and 2608.16370 have abstract-level numbers only, and their full papers were not read.
- Letta's 2024–2026 engineering blog evaluations (for example sleep-time compute or memory benchmarks) were not fetched.
- No tool-result *relevance ranking* paper beyond Squeez, SWE-Pruner and CoACT was found within budget.

## 6. Cross-cutting: what preserves success while cutting tokens; when elision hurts; locators; caching

### Takeaway
The most robustly supported low-risk policy is rolling-window clearing of old **observations**, keeping actions and reasoning, with the window tuned per scaffold. It gives about 50% cost cuts at equal solve rate on SWE-bench (JetBrains), 3 points better accuracy than full history in early SWE-agent, and an 84% vendor-claimed token cut (Anthropic). Shaping tool output at the source (bounded viewers, summarised search) matters as much.

Elision hurts when the agent must re-acquire dropped state. That cost shows up as extra retrieval and turns, not lower completion. It also hurts long-reasoning models, and when LLM rewrites lose detail or collapse. Restorable locators (URL or path) and pre-clear memory saves are the convergent vendor mitigation, but no controlled study isolating the locator's value was found. Caching makes the trigger cadence matter: batch clears, keep the prefix stable and append-only, and remember that summary calls miss the cache.

### Cited Findings
- Masking of old observations matches summarisation at about half the cost, observations are about 84% of turn tokens, and summaries lengthen runs by about 15% — [JetBrains, arXiv 2508.21433v3](https://arxiv.org/html/2508.21433v3)
- Keeping the last 5 observations beats full history, 18.0 vs 15.0, and full-file viewing is worse than a 100-line window, 12.7 vs 18.0 — [SWE-agent, arXiv 2405.15793](https://arxiv.org/html/2405.15793)
- Naive heuristics are not always safe: FIFO scores 45.8 and retrieval 27.4 against 56.0 uncompressed on AppWorld — [ACON](https://arxiv.org/html/2510.00615). In AppWorld-style multi-app tasks, older observations carry needed facts (API docs, IDs), unlike SWE logs.
- **Elision hurts through re-acquisition:**
  - At 5x compression, completion is unchanged but retrieval calls nearly triple (21.0 → 63.9 for GPT-5.5) — [arXiv 2608.16370](https://arxiv.org/abs/2608.16370)
  - Repeated compaction increases blocked actions, repeated exploration and run-to-run inconsistency — [arXiv 2608.06503](https://arxiv.org/abs/2608.06503)
  - Deletions cause non-linear "trajectory amplification" — [arXiv 2609.29875](https://arxiv.org/abs/2609.29875)
- **Elision hurts reasoning models:** on Gemini 2.5 Flash thinking, solve rate fell from 40.4 (Raw) to 36.4 (OM) and 31.4 (Summary) — [JetBrains](https://arxiv.org/html/2508.21433v3)
- **Rewrites collapse context:** 18,282 → 122 tokens, accuracy 66.7 → 57.1 — [ACE](https://arxiv.org/html/2510.04618)
- **Staleness is unhandled:** OM ignores whether an observation is stale (for example, the file was modified later) — [JetBrains](https://arxiv.org/html/2508.21433v3)
- **Locators and recoverability:**
  - "Compression must be restorable": keep the URL or path — [Manus](https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus)
  - Keep lightweight identifiers and load content just in time — [Anthropic engineering](https://www.anthropic.com/engineering/effective-context-engineering-for-ai-agents)
  - A memory warning before clearing lets the agent save essentials; the client keeps the full history — [Claude docs](https://platform.claude.com/docs/en/build-with-claude/context-editing)
  - Recall storage keeps every evicted message, and retrieval is paginated — [MemGPT](https://arxiv.org/html/2310.08560v2)
- **Prompt caching:**
  - 10x cached vs uncached price and a 100:1 input:output ratio, so a stable append-only prefix dominates cost — [Manus](https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus)
  - Clearing invalidates the cache, so use `clear_at_least` to batch — [Claude docs](https://platform.claude.com/docs/en/build-with-claude/context-editing)
  - Threshold-only triggering preserves caching between condensations — [OpenHands blog](https://openhands.dev/blog/openhands-context-condensensation-for-more-efficient-ai-agents)
  - Summary calls rarely hit the cache, and a smaller summarisation chunk (N = 21 vs 43) hurt efficiency through KV-cache loss — [JetBrains](https://arxiv.org/html/2508.21433v3)
- **Errors:** keep failed actions and stack traces in context — [Manus](https://manus.im/blog/Context-Engineering-for-AI-Agents-Lessons-from-Building-Manus). A FIFO policy does not do this by default.
- **Presentation matters beyond presence:** even one distractor hurts; focused 300-token prompts beat 113k full prompts — [Chroma](https://www.trychroma.com/research/context-rot). Relevant content in the middle of a long context is under-used — [Lost in the Middle](https://arxiv.org/abs/2307.03172)

### Inferences
- A defensible default from this evidence:
  1. Bound tool output at the source.
  2. Mask old observations in batched windows, keeping actions, a one-line header and a **locator** to the retained original.
  3. Protect error observations and explicitly pinned outputs.
  4. Defer any LLM summary to late in long runs (the hybrid), as deltas or verbatim extracts rather than free rewrites.
- Evaluate a policy by more than solve rate: count retrievals and re-reads, extra turns, blocked or repeated actions, run-to-run variance, and cache-read vs cache-write tokens. Several 2026 papers show completion metrics hide these costs.
- Because 2608.16370 finds random selection about as good as a hindsight oracle, sophisticated relevance ranking of *what* to drop may matter less than whether dropped content can be cheaply recovered. This is an inference from one small study, not established.

### Gaps
- **No controlled study was found comparing a bare placeholder with a placeholder plus re-fetchable locator** for elided tool output. This is the most direct missing evidence for recoverability design.
- No published measurement isolates cache-hit rate under masking vs summarisation, beyond JetBrains' qualitative note and cost tables.
- Evidence is concentrated on SWE-bench, AppWorld and web search. Interactive and multi-agent settings are largely unstudied.
- Most 2026 results are single-version arXiv preprints, several single-author, with no peer review yet.
