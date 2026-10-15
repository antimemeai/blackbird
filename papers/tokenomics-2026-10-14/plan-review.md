# Concrete tokenomics/OpenRouter plan review

2026-10-14. One independent review of
[`docs/TOKENOMICS_IMPLEMENTATION.md`](../../docs/TOKENOMICS_IMPLEMENTATION.md)
before relevant product coding. Scope: provider, cost and protocol correctness;
no execution, live calls, source changes or further campaign. Contract grounding:
[OpenRouter contract](openrouter-contract.md) and
[provider contracts](provider-contracts.md).

The sequence is coherent: shared conversion/binding and usage foundations precede
OpenRouter, and cache controls follow explicit route support. No new library,
credential migration or account probing is needed for deterministic adapter work.
Two missing policy decisions should be made concrete before the main adapter.

## Findings requiring plan additions

1. **Required tools must not silently disappear downstream.** Unit 3 says usable
   main compatibility but does not choose `provider.require_parameters` behavior.
   OpenRouter defaults this false; even tool preference can permit ignoring tools
   if no provider supports them. For required coding tools/controls, choose
   `require_parameters:true` or refuse the unsupported capability explicitly.
   Preserve caller-selected routing and do not silently weaken this on retry.
   Add a request oracle for the chosen setting and an unsupported-capability
   refusal; a successful response without tools does not demonstrate tool support.
   [Provider routing contract](https://openrouter.ai/docs/guides/routing/provider-selection#requiring-providers-to-support-all-parameters).

2. **Provider-default prompt caching must remain distinct from answer replay.**
   Unit 4 selects provider-default caching first, while the completion section
   rejects silent semantic answer caching. OpenRouter presets can enable response
   caching, including replayed tool-call responses, independently of prompt KV
   reuse. For normal main coding, explicitly send `X-OpenRouter-Cache:false` so a
   preset cannot silently select that behavior. Keep prefix policy default unless
   explicitly configured. Add an exact header oracle; retain returned cache-status
   headers without inferring provider identity from replayed metadata.
   [Response-cache contract](https://openrouter.ai/docs/guides/features/response-caching).

## Concrete requirements already intended, worth naming in direct oracles

- **History:** Unit 3's opaque-preservation/history validation must preserve
  assistant `id`, `status`, `phase` and full ordered reasoning items, including
  format/signature/content/encrypted payloads. The Chat `reasoning_details`
  representation is not Responses history. Keep namespace identity/custom tool
  definitions and calls, or reject an unsupported form explicitly. Structured
  tool outputs may use typed content arrays; arbitrary native objects still need
  external-boundary encoding. Retain model-switch incompatibilities rather than
  silently discarding reasoning to make the next request fit.
- **Terminal streams:** Explicitly test `response.failed`, `response.incomplete`,
  `response.error`, plain `error`, and failure under HTTP 200. Canonical
  `error_type` belongs on the response (nested under `response` in a failed-event
  wrapper); `server_error` alone cannot distinguish authentication from overload.
  A valid completed item is not an accepted completed response. Retain metadata
  chunks after completion and prevent malformed/truncated/conflicting terminal
  streams from advancing or dispatching tools. `[DONE]`/EOF alone is not success.
- **Identity/accounting:** Requested slug, response model, upstream provider,
  router attempt history, effective tier and non-secret account binding are
  different fields. Preserve `usage.cost` as the reported account charge; upstream
  inference cost is a breakdown, not an extra amount to add. Missing usage or
  routing metadata must remain missing, including on response-cache hits.
- **Cache policy:** Explicit GPT-6.1 Sol support needs the exact direct/OpenRouter
  model and route, recent intended content boundaries and marker conflict handling.
  Do not infer broader image/file marker support from the direct OpenAI schema.
  OpenRouter marker translation does not translate TTLs. A session routing ID is
  independent of prefix markers and does not guarantee a hit.

## Metadata header plumbing

`X-OpenRouter-Metadata:enabled` is appropriate for the OpenRouter Responses
adapter. Prefer typed per-request metadata/response-cache options translated only
for that origin/route. A generic transport extension can also be a small
allowlisted non-secret header set bound to the selected origin, with no credential
headers or replacement of auth-generated headers. This keeps `ProviderAuth`
protocol-neutral without allowing metadata plumbing to become an alternate
credential path. No additional auth-policy changes are requested.

Root received these findings directly. Fix these concrete plan omissions and
implement the named oracles in the owning units; this review does not request a
second plan tribunal, a review of this review or another source gate.
