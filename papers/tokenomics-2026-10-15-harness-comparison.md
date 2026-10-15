# Tokenomics relative to established harnesses

2026-10-15. Read-only comparison after master integration at 52d31ee. This is
a capability assessment, not a measured cost or quality ranking. No inference,
benchmark, credential acquisition, implementation or additional certification.

Blackbird now has competitive cache-policy foundations on its explicitly supported
routes: stable instruction/tool assembly, changing task/budget state at the end,
request-local policy, and original request/response retention. Provider-default
remains the default; explicit policy currently covers only the documented exact
GPT-6.1 Sol public-key/OpenRouter routes. Neither marker placement nor stable
local bytes proves a provider cache hit. See [delivered behavior](../docs/TOKENOMICS.md)
and [qualification](tokenomics-2026-10-14/qualification.md).

Current primary documentation establishes:

- [Claude Code caching](https://code.claude.com/docs/en/prompt-caching) includes
  distinct TTL controls for main and other requests, with billing-dependent
  defaults. [Cost guidance](https://code.claude.com/docs/en/costs) describes
  automatic caching and automatic context compaction.
- [Codex configuration](https://learn.chatgpt.com/docs/config-file/config-reference)
  includes a token-based automatic compaction threshold, a model context window,
  and a choice of total context or growth after the carried prefix for the
  threshold. [OpenAI caching](https://developers.openai.com/api/docs/guides/prompt-caching)
  is provider functionality; prefix changes and compaction can reduce reuse.
- [OpenCode configuration](https://opencode.ai/docs/config/) defaults automatic
  compaction on. Old-tool-output pruning is an additional opt-in control, with
  a token buffer reserved for compaction.
- [Aider caching](https://aider.chat/docs/usage/caching.html) organizes stable
  instructions, files and repository-map material for reuse, and offers optional
  keepalive pings. Blackbird deliberately introduces no paid keepalive traffic;
  this is a choice, not proof that either policy is cheaper on a given workload.

Blackbird's [context budget](../docs/CONTEXT_BUDGET.md) remains opt-in, measured in
bytes, advisory and model-directed. It is not a token-window catalog, automatic
token-based overflow controller or a spend limit. Recoverable originals and
managed context repair are useful controls, while automatic management and broad
explicit-policy coverage remain behind the documented capabilities above.

Raw usage inspection is a concrete strength: distinct attempts and scopes,
lineage-based deduplication, cache read/write and reasoning categories when
reported, actual charges separate from dated estimates, and explicit missing
coverage. This does not establish superiority over every competitor's accounting.
Lua composition can avoid model decisions for mechanical work, but no comparative
round-trip or cost saving has been measured.

Judgment: competitive foundations and detailed inspectability; less automatic
economy by default; no defensible cost-per-successful-task ranking yet. The single
OpenRouter launch stopped at missing credentials (arconaut-438), so it establishes
no actual cache hit or savings. Useful future observations should come from
ordinary authorized work, counting retries, helpers, cache writes, output and
repairs along with task success. Do not infer cheapness from smaller prompts,
native CPU efficiency, archival compression or cache-hit ratio alone.
