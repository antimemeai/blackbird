# Request-route foundation implementation

2026-10-14. Owned unit `arconaut-1ud` on shared tokenomics/OpenRouter branch.
Root owns provider selection, OpenRouter implementation, cache/layout assembly,
CMake registrations, publication and issue updates. This unit changes OpenAI
request/auth ownership, shared request-group lifecycle, retry classification and
direct route tests. No live provider calls or executable activation.

## Defects and purpose

The [inventory](native-inventory.md) and [native design review](design-review-native.md)
identify native tool fields bypassing external conversion on owned auth, owned
HTTP429 bypassing retry, and account re-resolution between attempts. Correct these
before interpreting route-specific cache policy or usage. Native retained values
and exact emitted originals remain separate. Provider-auth transport stays generic.

## Ownership and implementation

1. Add no-op `CodingProvider` request-group begin/end hooks. Begin receives the
   final assembled native request and raw assembly context. Engine owns one
   group scope around preparation, transport retries and final acceptance;
   end is noexcept and runs on all exits. Refuse nested request use before
   touching an existing group. Begin failures remain in the raw assembly phase.
2. OpenAI snapshots a non-secret owned auth binding or compatibility account/route
   for the group. Dispatch and retries require that same binding; refreshing an
   access token within the same account remains allowed. A changed account,
   credential replacement revision, kind or endpoint/protocol conflicts before
   inference submission. Normal owned OAuth refresh preserves revision.
3. Apply `export_provider_request` at the OpenAI caller boundary for owned as well
   as compatibility requests. Generic `ProviderAuth::request` receives external
   values and retains the exact actual JSON body. Existing tool-free colleagues
   already construct external-shaped input; no unrelated colleague changes.
4. Retry only positive HTTP429 provider-rate-limit status in addition to current
   transient conditions. Refresh failure/backoff is local authentication work,
   distinct from an inference HTTP429 and must not enter model-request retry.

## Direct evidence and bounds

First extend existing direct cases while old production code is in place and
observe the actual assertions fail: owned wire tool field types and typed429
retry count. Then add lifecycle/binding cases with the implementation. Check exact
request reuse, one accepted continuation, distinct attempt metadata, auth/cancel
terminal behavior, same-account token refresh, account/revision/route switch
refusal and cleanup after preparation failure. Use existing injected HTTP and
fake Codex/curl fixtures; no account/network activity.

Run only affected Debug targets/cases during this unit; coordinate Ninja use with
root. Root later owns the integrated source gate. No mutation campaign, new
runtime/library or perf/token-saving claim. Hardening has two layers: these
implementation/remediation cases and fixes, then one independent code review with
finding-driven recheck/fixes. No third assurance layer. Record actual executions
and remaining limits below before returning the unit.

## Results

Behavioral old-source reds executed before production edits: the new actual
owned fake-curl continuation failed `openai_auth_test` at its expected wire-type
assertion (then line203); `coding_test stream-capture` failed in `retry_tests`
because `provider_rate_limit(429)` received one attempt rather than three. A first
fixture build caught a missing test include, corrected before those behavioral
runs. No live endpoint was used.

Implemented metadata-only `openai_request_binding`, per-attempt expected-binding
checks, owned boundary conversion, positive inference-HTTP429 retry and negative
refresh-backoff status. OpenAI preparation does not launch a second Codex process;
ordinary per-attempt compatibility authentication still uses the existing CLI.
New binding traces link to preparation/auth resolution and own their credits
rather than consuming an older fixed parent budget.

The engine holds one provider group across all attempts and acceptance, releases
it on preparation/transport/acceptance failure, and refuses nested ownership.
Queued workflow draining remains before ownership, allowing those workflows to
make their own requests. Context origin is pinned with selected input; preparation
that changes the head conflicts before dispatch. Root inserts cache-policy
assembly after group begin and before native final-request capture.

At root's request, generic auth has two disabled-by-default OpenRouter application
header choices: routing metadata and full-response-cache bypass. They apply only
to OpenRouter application requests, not OAuth exchanges or other providers.
Direct fixtures cover all four combinations against OpenRouter/OpenAI.

The first green run passed `openai_auth`, `provider_auth`, `provider_auth_raw`
and `provider_exchange_raw`, including actual preparation-refusal capture, while
`coding` still failed its typed429 assertion. A debugger inspection found the
actual rate-limit fault received one call and became another error before retry.
The retained-event codec still bounded errors at `provider_transport`, excluding
the newer `provider_auth` and `provider_rate_limit` codes. This was six related
encoder/decoder validators, not another retry-classifier defect.

An independent explicit rejected-record byte fixture linked against the old Debug
foundation failed decoding the valid authentication code before that source
correction (`context/tokenomics-retained-codec-red.log`). Extend all six validators
through the final current error code; exact cases cover rejected records, adapter
receipts, recovery problems and continued rejection of unknown255. A direct
compiler invocation initially lacked the repository SDK flag; repeat with the
actual configured Xcode sysroot built the diagnostic fixture, then produced that
behavioral red. This compile setup failure is not counted as a product red.

Finding-driven green checks finish with `coding` and `retained_events` 2/2 passed
in14.55s (`context/tokenomics-route-codec-green.log`). The direct coding
stream-capture mode also passed, including raw-active owned429/retry, account
switch, same-account credential replacement, actual token renewal and local
refresh429 refusal. Lifecycle cases cover preparation throw/cleanup, nested
provider ownership and changed context during preparation. New successful fixture
scenarios needed distinct call IDs in their shared journal; reusing the old fixed
`accepted` ID correctly caused a protocol conflict, and the fixture was corrected.

Every Ninja invocation reported a premature dependency-cache ending and rebuilt
most translation units. After the libraries rebuilt, the final test-only
diagnostic correction was compiled/linked with the current CMake-generated
commands to avoid another unchanged library rebuild. Root owns the mandatory
integrated source gate; this unit neither repairs tooling nor bypasses that gate.
All6 directly relevant cases have passing runs across the initial4 and corrected2;
the initial5-case run and its failure remain in `context/tokenomics-route-green.log`.
No unchanged4-case rerun is claimed after the narrow codec finding.

The one bounded independent code review and any finding-driven recheck remain
with root before publication. No live entitlement, token saving, API bill,
deployment, mutation or running-executable claim follows from these local cases.
