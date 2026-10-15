# OpenRouter/cache code and oracle review

2026-10-14. One substantive static review of the newly written OpenRouter and
cache unit before integration. Read the adapter/header, request-cache code,
external JSON conversion, main/session provider-selection deltas, coding request
assembly, default Lua guards, direct C++ cases and native fake-curl script.
Grounding: [execution plan](../../docs/TOKENOMICS_IMPLEMENTATION.md),
[OpenRouter contract](openrouter-contract.md), and [provider contracts](provider-contracts.md).
No builds/tests, live calls, product edits, Git/Beads mutation or delegation.
Locations below refer to the reviewed source before remediation.

## Findings

### 1. P1 — Native result arrays are guessed to be external protocol content

`src/json.cpp:465`–`477` exempts a tool result from JSON conversion whenever it
resembles an input-text/image/file array. The native result
`[{type="input_text",text="alpha"}]` is ordinary representable tool data, but now
becomes protocol content rather than JSON text describing that array. Image/file
shapes and cache marker properties can similarly acquire protocol meaning. This
changes the model's input and confuses owned native values with external types.

**Correction:** ordinary native arguments/results always use the established
boundary conversion. Deliberate provider content arrays need an explicit,
validated opt-in that is removed at export. Cache's own string-to-input-text
wrapping happens after ordinary conversion and does not require shape guessing.

**Direct oracle:** compare the exact native result above with the external
string; parse that string and recover the entire original array unchanged. Add
ordinary image/file-shaped records and marker-shaped properties. Separately
exercise the explicit protocol-content path, confirming the local discriminator
does not leak and the submitted native request remains unchanged.

### 2. P2 — Marker conflict scanning treats arbitrary native data as policy

`src/request_cache.cpp:16`–`23`, invoked at `79`–`81`, recursively searches the
whole input for keys named `cache_control` or `prompt_cache_breakpoint`. Native
function arguments or results may legitimately contain those keys. They would
be quoted by correct external conversion, but `stable_prefix` and even
`disabled` reject the request as a policy conflict before export.

**Correction:** inspect only actual protocol control locations, distinguishing
ordinary native arguments/results from deliberately supplied external content.
Keep refusal for genuine authored markers or request-wide conflicting controls.

**Direct oracle:** a completed call/result pair whose ordinary result is
`{cache_control="source data",prompt_cache_breakpoint="source data"}` succeeds
under stable and disabled modes; its external JSON text retains both fields.
The same key at a legal content-block marker position still produces the intended
conflict. Include an ordinary function-argument occurrence independently.

### 3. P1 — A completed envelope can dispatch an incomplete tool

`src/openrouter.cpp:138`–`164` delegates terminal validation to
`completed_response` in `src/openai.cpp:550`. That parser checks response status
and output-array presence, but not client-tool item status or contradictions
with a non-null response error. `programs/turn.lua` dispatches a recognized
function call without checking its status. Consequently a
`response.completed`/`status="completed"` envelope containing valid JSON
`write_file` arguments and `status="incomplete"` or `"in_progress"` on the call
can perform the effect. A completed call inside an envelope carrying a non-null
error is another contradictory accepted shape.

**Correction:** require a structurally consistent successful terminal response
and complete supported tool items before the default loop dispatches any effect.
Do not use valid JSON arguments or the event name as completion evidence.
Preserve originals on refusal.

**Direct oracle:** extend the real native fake-curl case with each incomplete/
in-progress call status and with a non-null response error/canonical failure
type under a completed event. Use otherwise valid write-file arguments. Require
failed acceptance and no output file, including when a completed envelope is
reconstructed from final-item events. Retain the existing normal continuation,
failed/incomplete, malformed-tail and unsupported-tool cases.

### 4. P2 — Authentication fixtures repeat noncanonical error identifiers

`src/openrouter.cpp:23` recognizes `authentication_error`/`permission_error`;
`tests/openrouter_test.cpp` and `scripts/check-openrouter` use the same invented
authentication value. The current contract uses canonical `authentication` and
`permission_denied` (and separately documents credit failures). A real
`response.failed` containing `error_type="authentication"` therefore becomes
`external_unknown`, so the tests do not establish the claimed typed distinction.
These failures currently remain terminal; this finding does not claim an auth
retry or credential leak.

**Correction:** map the actual canonical vocabulary deliberately; keep ambiguous
native `server_error` terminal without guessing that it is transient.

**Direct oracle:** use independent documented canonical values in both the C++
failure fixture and native launcher fixture. Confirm `authentication` maps to
the chosen auth outcome, a different canonical overload/server value retains its
own terminal classification, and an unknown future value is not accepted or
retried as success. Exercise both response-wrapped failure and plain error shapes.

## Bound and retained strengths

The explicit persisted OpenRouter selection, request-group account binding,
`require_parameters:true`, scoped metadata/response-cache headers, trailing
metadata capture, native state omission/assembly and opaque field retention are
appropriate mechanisms. The reviewed code preserves full input history and
does not switch to another billing route. Existing tests directly attack several
important failures, but the four constructions above are missing and expose
actual defects, rather than asking for broader test volume.

Root received and accepted these findings during the review and owns fixes and
their direct checks. Root's planned finding-driven recheck and integrated source
gate close this unit; this report requests no additional review layer. The native
owner's concurrently discovered retained-codec repair is outside this review's
findings and was not retested here.
