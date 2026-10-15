# Registered Responses providers and Paper Instruments

The operator requests an isolated worktree and enough configuration to run the
main coding harness with Paper Instruments. Worktree: blackbird-paper-provider;
branch: work/paper-provider-2026-10-15, based on d987f6f. The original checkout's
active TUI changes remain there. No new library or runtime.

## Source contract

Read 2026-10-15: https://docs.paperinstruments.com/docs/ultramarine/responses
and https://docs.paperinstruments.com/docs/ultramarine/chat-completions .
Paper offers https://api.paperinstruments.com/v1/responses and model
ultramarine-1-flash; supports streamed stateless full-history requests and local
function tools. Effort is low/high/max. Developer messages and encrypted reasoning
are unsupported. Plaintext reasoning may be replayed. Function results are strings.
A completed terminal response is required before executing a tool.

## Design and implementation

Keep owned ProviderAuth registrations, account pinning and native external
encoding. Extend the existing registration's protocol choice with
responses-portable: Responses using system instead of developer messages,
plaintext reasoning, and low/high/max effort. This is explicit configuration,
not hostname sniffing or an automatic credential/model fallback. Standard
responses preserves OpenAI fields. Chat/Messages registrations remain colleagues
only. The main selector accepts registered Responses IDs; OpenAI and OpenRouter
keep their existing specialized adapters. Custom providers do not claim support
for the explicit OpenAI/OpenRouter cache policies.

Use a new small RegisteredResponsesCodingProvider with the existing ProviderAuth
transport and completed_response parser. Normalize only the configured portable
request view at the external boundary; retain native context and exact wire
originals through the existing capture. Replace developer roles with system,
omit the encrypted-reasoning include request, and refuse encrypted history rather
than silently deleting it. Preserve plaintext reasoning and call/result identities.
Validate terminal error and completed tool statuses/unique call IDs before dispatch.
Keep account binding pinned across retries; do not follow redirects. No static
key, .env content, or credential enters tracked files, argv or reviewer prompts.

Allow effort max throughout session/program validation, persist the actual custom
provider and effort through reopen/restart/new, and keep OpenRouter's model shape
rule. Validate provider IDs with the same owned identifier alphabet and length.
Add BLACKBIRD_RESPONSES_CURL as the explicit local transport injection for native
protocol checks; normal runs use system curl.

## Direct checks and bound

One remediation layer: meaningful red/green export/transport tests and a native
fake-curl loop requiring exact tool arguments/results and preserved plaintext
reasoning; account-change refusal, invalid provider/protocol, effort max and
session reopen, incomplete/failed/malformed responses must not execute tools.
One independent code challenge and fixes constitute the recheck. Run the required
source gate before committing. No third assurance campaign or unrelated TUI repair.
Build an isolated optimized executable and run a small live Paper tool read plus
follow-up through the actual harness. Read only paper_api_key from workspace .env
without sourcing it; feed auth privately or pass it in the child environment.
Retain logs under ignored context and report actual API restrictions/failures.
Publish the candidate branch; do not merge or activate another checkout.
