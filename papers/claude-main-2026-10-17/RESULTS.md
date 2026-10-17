# Native Claude main-loop results — 2026-10-17

The native Messages adapter and retained-engine integration are ready for root's
combined source gate. This unit made no live provider requests and changed no
operator credentials. Root owns the separate OAuth reference alignment and live
qualification. See [PLAN](PLAN.md) and the one independent
[SOURCE_REVIEW](SOURCE_REVIEW.md).

## Delivered behavior

`--provider anthropic --model NAME` selects `AnthropicCodingProvider` through the
ordinary session factory. The adapter uses existing owned account selection,
request-group revision binding, refresh, HTTP, cancellation, raw/original capture
and Lua coding workflow. It does not invoke the Claude CLI bridge. An explicit
model remains required when changing providers.

The boundary translates chronological native messages, function schemas,
function calls and linked results into Messages. Initial instructions remain
system content; chronological developer/system entries become explicitly labeled
user guidance at their original position. Unsupported options/content and foreign
opaque reasoning are refused. Known invalid model/effort pairs are refused before
session settings are saved and at the public encoder: Sonnet4.6/Opus4.6 retain
`max` support and reject `xhigh`; Opus4.5 rejects `xhigh`/`max`. Earlier manual
thinking families use the documented owned effort-to-budget mapping in source;
current adaptive families use native `output_config.effort`.

Thinking, signatures and redacted data remain complete native Values, tagged with
their original assistant message ID. Text and tool projections retain their native
block too; changes that disagree with those originals fail before dispatch.
Equivalent native text represented as a string still exports the original block.
Successor seeds accept native thinking and reject malformed IDs/signatures/data.
Responses export refuses native Anthropic reasoning instead of relabeling it as
OpenAI encrypted content. Ordinary managed context already copies native Values;
no internal JSON storage or JSON-mediated Lua transfer was introduced.

The native SSE accumulator requires complete indexed block and message lifecycles,
object tool input, unique call IDs, consistent stop reasons and a clean whole tail.
Incomplete or failed streams never publish tool calls to the default workflow.
Text previews use native events while audit/original capture keeps the exact
upstream SSE. Current-block owned string builders append fragments and move the
completed text/signature into native blocks once, avoiding accumulated-prefix
copies. Operational task/budget state is retained at its original chronological
position for this provider so signed continuations replay the same input prefix.

## Direct RED and GREEN

The initial actual-executable RED is saved at
`context/claude-main-2026-10-17/native-red.log`: owned fixture auth succeeded, then
the main route refused with `Main coding requires a Responses provider.`

Review findings received direct missed-refusal REDs before their fixes:
`parity-red.log`, `cancel-red.log`, and `effort-red.log`. Each exited1 because the
old candidate accepted the forbidden action. Those checks now pass in the native
adapter test, including a held synthetic auth file lock with an already-cancelled
provider callback and a bounded unlock thread; no network is involved.

Final focused Debug build passed. The following10 CTests passed,0 failed,
26.50 seconds total:

- `anthropic_provider`, `anthropic_native`;
- `context`, `context_delta`, `successor_seed`, `json`;
- `responses_provider`, `registered_responses_native`;
- `coding`, `coding_stream_capture`.

The actual native executable episode uses a synthetic owned API key and injected
curl. It checks one local exec effect, signed/redacted thinking and tool-result
continuation, text previews, cache/read/write usage accounting, exact previous
Messages prefixes with retained task state, forced session rebuild/reopen,
refused invalid interactive effort without persisting it, malformed/truncated/
unsupported streams before local effects, SIGINT cancellation, exact native SSE
in retained originals and absence of the fixture key from outputs/session files.
The pure adapter suite additionally checks parallel tool IDs/results, byte-sized
preview chunk boundaries1–19, selected-account revision fencing, incompatible
reasoning refusal and all concrete review repairs. Unknown usage stays unknown.

Build/check logs are under ignored `context/claude-main-2026-10-17/`:
`resumed-build.log`, `green.log`, and `tidy-green.log`. Relevant diff whitespace
checks passed. Targeted clang-tidy for the provider and new/changed test units
finished clean after fixing two source diagnostics. The swappable Value arguments
became a named projection aggregate. An analyzer path that took both the text and
thinking branches after a move became an explicit `else if`; no suppression was
added. The successor malformed-type test now constructs valid JSON and reaches
seed validation, preserving its no-context/no-audit-write refusal oracle.

## Remaining ownership

The independent review's five findings are resolved in this unit. Root performs
the combined full source gate and publishes/integrates the candidate. No second
independent review, broad audit, TUI performance run or AFL interaction was added.
Real account authorization and end-to-end live Messages validation remain with
root; the offline evidence above does not claim either has succeeded.

## Combined C++23 qualification and host activation

Root incorporated master `c38a7de` and published qualified source `9e9dad5` on
`work/claude-main-install-cpp23-2026-10-17`. The installed full source hook passed
263/263 Mac Debug tests in500.49s and all owned C++/Lua checks. No new suppression
was added. The first combined suite's two raw-fixture failures, unchanged serial
rechecks and final CTest scheduling correction are recorded in JOURNAL.md and
`arconaut-9h5`; their assertions and deadlines remain unchanged.

Neuroses Clang23.1.4/libc++23.1.4 scoped Release and ASan+UBSan each passed10/10
in2.41s and6.71s respectively. Captures: `context/linux/run-i0mb2n67/checks.log`
and `context/claude-main-2026-10-17/source-hook-cpp23-final.log`.

The host installer built and activated generation `generation-z8hyFGNP` from
`9e9dad5`, including the native adapter. The managed command passed help and auth
catalog checks from `/tmp`. Owned auth status still contains only Paper; neither
live Anthropic login nor a real Messages exchange is qualified. Native issue
`arconaut-12q` and auth issue `arconaut-uaa.5` retain that follow-through. This
candidate publication and host installation initially left the branch unmerged.
The operator subsequently requested integration: master was fast-forwarded from
`c38a7de` to `acfe0f9` and pushed. The source is identical to qualified `9e9dad5`;
later changes are documentation and issue records. Settled source checks were
carried through this fast-forward under the repository's explicit rule.
