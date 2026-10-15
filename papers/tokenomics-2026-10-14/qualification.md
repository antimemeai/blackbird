# Tokenomics and OpenRouter qualification

2026-10-14. Work branch `work/tokenomics-openrouter-2026-10-14`, based on pushed
research `ad4a640` and trace-archive master `d74f248`. This delivers the
[approved execution](../../docs/TOKENOMICS_IMPLEMENTATION.md), with no dependency,
live provider campaign, running-process replacement or main integration.

## Delivered behavior

The OpenAI route converts native arguments/results at the actual external
boundary and binds one request group to a non-secret route/account identity.
Typed HTTP429 retries preserve the assembled request; credential refresh for the
same account is allowed, account/key replacement is refused. Auth failures and
local refresh-backoff sentinels stay terminal. The newly exercised error variants
also required correcting the retained event codec's stale enum bounds.

Explicit persisted `--provider openrouter --model openai/gpt-6.1-sol` selects
stateless Responses coding through existing owned auth or the configured key.
Complete original responses, opaque reasoning, phase and routing metadata survive
continuation. Partial, failed, malformed, contradictory and duplicate-call output
cannot dispatch tools. Default Lua refuses custom/namespaced dispatch. Full
response caching is disabled independently of prompt-prefix policy.

Typed retained/request-local prompt policy supports provider default,
`stable_prefix` and `disabled`. Explicit controls require the supported actual
account/endpoint/model; conflicting authored controls refuse. Automatic task and
budget state is one ephemeral trailing developer message. Ordinary native tool
results always encode at the external boundary; deliberate protocol arrays use
an explicit discriminator. No marker, tool-shaped data or local option is guessed.

`blackbird-token-usage` separately reads bounded pinned raw prefixes. It preserves
attempt/source/scope identity, known counters, unknown/invalid/partial coverage,
account-route metadata and drill-down locators. Model-alternative scopes are not
added to aggregates. Actual supported USD reports remain separate from generic
unpriced costs and optional explicit dated estimates. Neither main nor recorder
performs this accounting. Raw-only coverage never claims a complete session bill.

## Direct mechanisms and review

- Old-source owned-wire/retry failures and independent retained-codec bytes are
  recorded in the [route implementation](route-implementation.md). Source/oracle
  inspection is the unit's [single independent review](route-code-review.md).
- Original layout checks fail before the assembly change. Exact request assertions
  cover instruction stability, role/order, selected input, pre-state budget units,
  native-data encoding, real marker conflicts, unsupported routes and retained
  activation/failure/override/reopen. Existing task/budget cases retain their
  semantic checks with the new request-local state location.
- The [OpenRouter/cache review](openrouter-code-review.md) finds array inference,
  marker false positives, contradictory/incomplete output and wrong auth names.
  All are accepted and corrected, then checked directly. No further code tribunal.
- The actual executable's injected fake curl requires exact outbound history,
  parameters and headers, appends one effect, resumes with the tool result,
  preserves opaque provider items and reopens without a request. Eight terminal/
  dispatch rejection classes plus duplicate call IDs assert zero effects. Its raw
  producer records are read by the real usage CLI: two requests report exactly
  input2400/output40/cache-read2000/cache-write200. This is synthetic protocol
  conformance, not a live provider/cache/billing result.
- Usage uses independently declared wire fixtures, actual native packet codecs
  and actual RawRuntime/provider-http callback paths. Its
  [single independent review](usage-code-review.md) finds cost qualification,
  terminal/tier conflicts, callback lineage and role11 format. The owner accepts
  all four and supplies finding-driven direct cases; see
  [implementation and outcomes](usage-implementation.md).

One early integrated build failed at an undereferenced provider unique_ptr. An
incorrect shell sequence subsequently ran stale/missing binaries; those results
are explicitly excluded. After correction and successful rebuild six selected
cases pass; context-budget's stale location expectation is updated and passes.
A new reopen cache fixture initially expected budget state below its own trigger;
using input above the trigger preserves the real oracle and passes. Static
analysis findings are fixed without annotations. No laptop mutants, review of
reviews, or third hardening layer.

## Final profile results

Source checkpoint **`56b3f62`** passes the installed full source hook and is pushed
to `origin/work/tokenomics-openrouter-2026-10-14`.

| Profile | Scope | Result |
| --- | --- | --- |
| macOS Debug full source gate | Complete suite | **136/136,180.47s** |
| macOS owned source analysis | 203 actual C++ translation units, reached headers, formatting; owned Lua formatting/syntax, fresh LuaLS and contract probe | **Pass** |
| macOS Release/OFF | Fourteen affected route/account/raw/retry/cache/settings/usage cases | 14/14,23.83s |
| macOS ASan/UBSan | Same fourteen cases | 14/14,53.20s |
| Linux Debug | Same fourteen cases | 14/14,15.51s |
| Linux Release/OFF | Same fourteen cases | 14/14,7.31s |
| macOS Release / ASan | Updated native provider/model handoff fixture | 1/1,2.19s / 1/1,8.11s |
| Linux Debug / Release | Updated native provider/model handoff fixture | 1/1,1.34s / 1/1,0.51s |
| Linux ASan/UBSan separate window | Fourteen affected cases plus native handoff | 15/15,50.06s |
| macOS Release / ASan final state/auth edits | Cache, tasks, budget and OpenAI boundary | 4/4,3.48s / 4/4,7.53s |
| Linux Release / ASan final state/auth edits | Same four cases | 4/4,1.25s / 4/4,2.90s |

The complete C++ pass initially reported exactly two diagnostics: a moved state
string in the request-field loop, and an implicit independent Value snapshot copy
in an auth test. Exit the loop after inserting the one state item and explicitly
construct the snapshot. Targeted analysis and the four affected profile cases
pass, then the complete installed source gate above passes. No suppression was
added. LuaLS reports no problems from its fresh report.

## Required-gate findings

The failed runs remain recorded in the journal and ignored working logs; only
the final passing source hook qualifies the checkpoint. Existing fixtures exposed
three concrete assumptions:

- `/new` hardcoded its old argument count. The corrected actual handoff fixture
  launches with OpenRouter and its namespaced model, asserts the exact retained
  provider/model operands and preserves raw selected/fallback linkage checks.
  A compound optional access is made explicit without changing its assertion.
- Raw overflow assumed stderr was always acquired first. A stderr poll can expire,
  then stdout can arrive first and exceed the one-byte limit. The oracle now
  permits exactly the original stdout-five/stderr-empty or stderr-three/stdout-empty
  outcomes, retaining the exact bytes, endpoints, input and reap checks.
- Task-pane PTY observation could return after draft cells but before a synchronized
  frame's final cursor restoration. The existing VT parser tracks the renderer's
  actual `?2026h/l` mode; the fixture waits for frame completion before applying
  its unchanged pane, cursor, narrow-layout and shutdown assertions.

A separate `operation_raw` run hit its120.09s watchdog before the third injected
native diagnostic rejection returned. Other full runs passed that case; one
unchanged-source direct recheck passes in12.11s, and both subsequent full suites
pass136/136 (186.96s before the lint edits,180.47s finally). Root cause is not
established. **`arconaut-7qn` remains open** with the exact phase/log references;
neither concurrency, watchdog nor semantic assertions were changed for a pass.

The initial Linux snapshot reproduced the usage merge conflict defect before
its final correction. A later combined remote job completed Debug and Release,
then reached its existing600s driver limit in the last ASan case after13 passes.
That incomplete result is not qualified; the separate15/15 ASan window above
finishes it. Temporary remote workspaces were cleaned up. No laptop mutation,
review of reviews, third review panel, or live evaluator campaign.

## Limits and publication

On2026-10-15 the operator directs one bounded live round then merge, without a
further certification campaign. Native auth status reports no OpenRouter accounts;
OPENROUTER_API_KEY and a matching variable in the known workspace env file are
absent. One native launch in `context/tokenomics-live-20261015T051616Z` refuses
with provider_auth before a tool effect. Read-only reopen preserves the explicit
OpenRouter/model selection; the native usage reader reports unknown consumption
and coverage gaps rather than zero. The prepared workflow permits at most two
submissions, no retries or warmup; credentials prevent service continuation from
being exercised. No actual provider/cache benefit is inferred from this refusal.
No measured latency, cache-hit improvement, monetary saving, subscription allowance
or general coding quality is established. Explicit cache controls cover the documented exact
GPT-6.1 Sol public-key and OpenRouter routes; provider-default remains the default.
Raw-prefix usage remains incomplete coverage, never a complete session bill.
Live follow-through is **`arconaut-438`**.

Implementation issues `arconaut-1ud`, `arconaut-wiq`, `arconaut-2aw` and
`arconaut-nan` are closed. The source push succeeded; a text-only result/issue
checkpoint records this outcome under the repository's documentation exception.
Master integration is authorized and fast-forwards from d74f248 through4aa8f43,
including the unchanged qualified source. This text-only integration record uses
the repository's existing documentation exception; settled source checks are not
repeated. Running-executable/session activation remains separate. Operator art,
historical worktrees, the work branch and other services retain their owners.
