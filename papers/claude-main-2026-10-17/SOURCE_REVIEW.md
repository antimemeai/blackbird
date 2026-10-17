# Independent native Claude code review — 2026-10-17

One bounded review of the complete Messages adapter unit, conducted by the
install/update unit owner independently of the adapter implementer. Source reads
only: no compilation, tests, live requests, credentials, session activation or
unrelated audit. Findings were sent directly to the owner while reviewing; fixes
and direct rechecks remain with that owner and root. This report describes the
reviewed candidate, before those fixes.

Read the written plan; adapter header/implementation; pure and actual-native
checks; changes to coding request assembly, preview, JSON export, successor seed,
main routing and CMake. Also followed auth binding/request/cancellation and each
Responses exporter to its shared boundary. Current official
[effort documentation](https://platform.claude.com/docs/en/build-with-claude/effort)
and [thinking documentation](https://platform.claude.com/docs/en/build-with-claude/thinking)
were read for model compatibility and preserved-block constraints.

## Concrete findings

1. **P2 — retained text parity guard has representation/role bypasses.** In
   `export_anthropic_request`, array message content passes through `native_block`,
   but string content bypasses it. Take a native tagged assistant text item from
   `anthropic_completed_response`, retain its `anthropic_content` and message ID,
   then change its projected content from one output-text part to a different
   string: export silently sends the changed text. Editing its role can likewise
   project retained assistant text as user guidance. An empty part array drops the
   original block. This defeats the stated refusal of disagreement with retained
   native blocks and can invalidate signed history. Apply native type/role/group
   coherence and exactly-one-text-block checks across both content representations.
   Direct RED should mutate these concrete fields while retaining the original,
   and distinguish an equivalent string representation from changed text.

2. **P2 — known model/effort combinations are accepted but invalid on wire.** The
   generic effort check accepts `xhigh` for Sonnet/Opus4.6 and `xhigh`/`max` for
   Opus4.5, then sends those strings as `output_config.effort`. Current official
   effort docs list `max` for the4.6 families but exclude them from `xhigh`;4.5
   supports the three ordinary levels. Main route validation returns early for
   Anthropic, allowing invalid saved choices before export. Reject these known
   invalid pairs before saving session settings and at the public export boundary;
   keep valid newer-family levels and manual-budget mappings explicit. Direct
   assertions should check both refusals and valid4.6 `max`/new-family `xhigh`.

3. **P2 — binding does not receive cancellation.**
   `AnthropicCodingProvider::begin_request_group` constructs
   `ProviderAuth{config_.auth}` without copying the provider's `cancelled`
   callback. `Store` waits up to30 seconds for an external flock and checks only
   that config callback. Therefore cancellation can remain ineffective during
   account binding, before `respond` supplies its callback. Propagate cancellation
   at binding as the existing OpenAI adapter does. A subprocess holding the
   synthetic auth lock plus an already-cancelled callback is a direct no-network
   refusal oracle. The inherited global store mutex is outside this unit's scope.

4. **P2 — each text/thinking/signature delta recopies the whole prefix.**
   `MessageStream::event` uses `auto text = string_field(block, target)` and
   replaces the field after each append. For fragment lengths `d_i`, this copies
   the sum of all accumulated prefixes, rather than total fragment bytes. The
   whole-stream16MiB bound does not change this quadratic work pattern. Keep
   owned current-block text/signature builders and publish them when the block
   closes, or supply equivalent safe owned append machinery. This is a concrete
   copy/complexity finding; no unmeasured latency claim or benchmark campaign is
   proposed. Existing multi-fragment exact-output checks should remain unchanged.

5. **New-test defect — successor rejection case never reaches the validator.**
   The last new malformed seed case appends a second `type` key to a thinking
   block by replacing its signature. `parse_json` rejects duplicate keys, and
   `unwrap(parse_json(...))` occurs outside the `reject` helper, so this aborts the
   whole test rather than checking seed refusal. Change the original thinking
   block's type to text without creating duplicate keys, then retain the existing
   no-context/no-audit-write oracle.

## Other assessed boundaries

The adapter preserves upstream SSE instead of synthesizing Responses bytes.
Acceptance follows semantic message/block completion, complete object tool JSON,
stop-reason/tool consistency and whole-tail parse; recognized duplicate or malformed
terminal records fail before engine context acceptance and default-workflow tool
execution. Unsupported semantic block/delta kinds are refused. Forward envelope
events are intentionally ignored; previews remain presentation-only.

Thinking and redacted payloads remain native Values. The shared Responses export
rejects provider-tagged reasoning, and OpenAI, OpenRouter and registered Responses
all use that boundary. Text/call metadata is removed only at that external export.
Successor validation checks native reasoning IDs, signatures/data and forbids
simultaneous foreign encrypted content; its direct test oracle needs finding5.

Selected owned-account revision/protocol/base/kind are passed into each existing
ProviderAuth request, preserving selection fences. Transport/raw capture, original
bytes, retry ownership and normalized usage stay in existing paths. Cache creation
and cache read tokens are included in normalized input; final usage is accepted
only after complete response semantics. Auth OAuth wire changes belong to root's
separate auth seam review, rather than this review.

These source observations are bounded conclusions, not a substitute for the
owner's direct RED/GREEN, the combined source gate or separate live qualification.
No second independent review of the repaired unit is requested.
