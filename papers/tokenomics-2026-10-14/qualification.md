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

| Profile | Scope | Result |
| --- | --- | --- |
| macOS Debug direct integration | Usage, OpenRouter, cache, native tool loop, tasks, settings, budget | 7/7,11.43s |
| macOS Release/OFF | Fourteen affected cases, including route/account/raw/retry and program policy | 14/14,23.83s |
| macOS ASan/UBSan | Same fourteen affected cases | 14/14,53.20s |
| Linux Debug | Same fourteen affected cases | 14/14,15.51s |
| Linux Release/OFF | Same fourteen affected cases | 14/14,7.31s |
| Linux Debug / Release | Updated native provider/model handoff fixture | 1/1,1.34s / 1/1,0.51s |
| Linux ASan/UBSan separate window | Fourteen affected cases plus updated native handoff | 15/15,50.06s |

The first full local gate builds successfully and passes135/136 tests in218.72s,
then stops at `main_input_native`: its handoff oracle hardcodes the old `/new`
argument count. Update it to assert the exact added provider/model pair, using an
explicit OpenRouter source session; the direct fixture passes. The fixture also passes local Release1/1 (2.19s) and ASan/UBSan1/1 (8.11s).
The full gate is rerun after this test correction. Lint did not run after that initial failed suite. Targeted analysis of the
modified native fixture then reports a preexisting compound optional access.
Cancel our in-progress gate before editing, bind/check the optional explicitly,
and retain the closed-snapshot assertion. Targeted analysis passes without a
suppression; restart the complete gate.

The combined remote job completes Debug and Release, then reaches the existing
600-second driver timeout in the last ASan case after13/14 pass. This does not
qualify the unfinished case. Finish ASan as a separate profile and check the
updated native handoff fixture on each supported profile. The first Linux Debug
snapshot separately reproduced the now-corrected U2 merge defect (13/14 pass);
that earlier snapshot is not a green qualification result.

## Limits

No live OpenRouter account/inference request was made. No measured latency,
cache-hit improvement, monetary saving, subscription allowance or general coding
quality is established. Deterministic protocol checks establish only their named
failure classes. Supported cache controls are limited to the documented exact
GPT-6.1 Sol public-key and OpenRouter routes; provider-default remains the default.
Live follow-through is tracked as `arconaut-438`.
The running operator executable/session is not replaced. Branch publication does
not imply a main merge.

The subsequent full local suite passes135/136 in297.58s and exposes an unrelated
old raw-overflow oracle: stderr is not guaranteed acquired before stdout. A
stderr poll can expire, then stdout's exact five bytes can arrive first and exceed
the one-byte limit. The original assertion required stderr's exact three bytes.
Correct only the fixture to accept either exact acquired stream with the other
empty; retain endpoint/reap/input/original checks and unchanged runtime behavior.
The direct test and required full gate cover this predicate correction; settled
product platform cases are not repeated for an equivalent test-only assertion.

The following full suite passes135/136 in187.20s, with both corrected native
fixtures green, and exposes task-pane PTY observation during an unfinished
synchronized frame. The existing screen oracle tracks cells as chunks arrive;
seeing draft text does not establish that its final cursor restoration arrived.
Track the renderer's actual ?2026h/l mode in the oracle and wait for frame
completion before evaluating this fixture's unchanged predicates. The direct
physical PTY recheck passes, including cursor and narrow fallback. No renderer,
geometry bound, time allowance or test assertion is relaxed.

The next full suite passes135/136 in188.94s, with the corrected task-pane, raw
capture and handoff cases green. `operation_raw` hits its120.09s watchdog before
the third diagnostic rejection case returns; previous full runs passed that case.
A single unchanged-source direct recheck passes1/1 in12.11s. Root cause remains
unestablished, tracked as `arconaut-7qn`. No concurrency/watchdog/semantic rule is
changed to obtain a pass; preserve the timeout alongside the final gate outcome.

The required full Debug suite passes136/136 in186.96s. Complete analysis of203
owned C++ units reports two findings: make the state insertion exit its field
loop after moving the string, and explicitly construct the independent Value
snapshot in the auth boundary test. Both corrections pass targeted analysis.
Four affected cases re-pass local Release4/4 (3.48s) and ASan/UBSan4/4 (7.53s);
scoped Linux optimized/sanitizer rechecks follow. The final source gate retains
all136 tests and all analysis, with four build workers to bound local contention.
