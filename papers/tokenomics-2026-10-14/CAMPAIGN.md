# Tokenomics frumentarii

Operator, 2026-10-14: token caching and broader tokenomics next; run frumentarii
after the merge. Archive work and its inherited repairs/design are merged and
pushed on master d74f248. Research begins on work/tokenomics-research-2026-10-14,
issue c8h. The archive/source gate passed 132 Debug cases and owned C++/Lua lint;
there is no reason to repeat it for these research reports.

Three independent conceptual lanes:

1. Native request/context/usage inventory: trace actual request construction,
   supported provider/account routes, stable/changing prefixes, schemas, tool
   results, compaction, retries and exact usage capture. Name code locations and
   existing evidence; identify what is not measured.
2. Provider contracts: fresh official caching, usage and pricing documentation
   for actual supported paths. Separate OpenAI model-family behavior, ordinary
   paid API versus subscription routes, Anthropic and other material providers.
   State dated facts, unknown entitlement/endpoint behavior and cost equations.
3. Harness mechanisms: literature and source study for stable-prefix scheduling,
   tool/context representation, retrieval/compaction, code-mode orchestration,
   caching and token/quality tradeoffs. Identify concrete mechanisms and realistic
   direct comparisons, not a dependency or a universal savings claim.

Root integrates findings into a small native design and ordered implementation
proposal. Preserve original capture and model control. Fewer supplied tokens,
more cache hits, lower billable cost, lower latency and retained task quality are
different claims. Counter arithmetic belongs in code/tertiary consumers; do not
restore transformed metrics inside producers or recorder. Cache-write prices and
provider/account capabilities are not assumed universal. No silent semantic
response reuse or truncation of required inputs to make a token graph look good.

Reports go here. Each lane owns its report and acquisition notes only. References
remain study-only; keep source archives intact, stripped extracted Git metadata,
exact revisions/digests and restoration commands if acquiring implementations.
Local private observations stay in ignored context; never send them to web search.
No libraries, live provider/account requests, eval selection/campaign, product
implementation, source/performance testing, new deployment or executable activation
is selected by this research pass. Root records the journal/issues and publication.
No arbitrary campaign clock or new third assurance layer.
