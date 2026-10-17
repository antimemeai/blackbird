# Native Claude in the main coding loop

2026-10-17. Unit owner: native Messages adapter and retained-engine integration.

## Purpose and scope

`--provider anthropic --model NAME` must use owned Anthropic account selection and
run the same retained Lua coding workflow, native tools, audit, cancellation and
session reopen as Responses routes. The existing direct colleague path and Claude
CLI bridge do not implement the main coding loop. Reuse `ProviderAuth`; add no
library or second credential store. Explicit model selection remains required
when changing providers. This unit does not initiate login or make live requests.

## Grounding

Read local `CodingEngine::request`, `ResponsePreview`, native JSON boundary
adapters, `ContextStore::valid_entries`, session/main routing, owned auth request
group binding, and colleague Messages export. Current official source consulted:

- [Create Message](https://platform.claude.com/docs/en/api/messages/create):
  Messages uses `messages`, `system`, `max_tokens`, client `tools` and streaming.
- [Streaming](https://platform.claude.com/docs/en/build-with-claude/streaming):
  message start, indexed content start/delta/stop, cumulative usage deltas, final
  message stop; tool input arrives as JSON fragments. A complete transport is
  insufficient without complete semantic block/message boundaries.
- [Thinking](https://platform.claude.com/docs/en/build-with-claude/thinking):
  adaptive and manual modes differ by model family. Complete thinking and
  redacted blocks must be replayed in original order with their tool use; opaque
   signatures remain unchanged. They are not OpenAI encrypted reasoning.
- [Tool calls](https://platform.claude.com/docs/en/agents-and-tools/tool-use/handle-tool-calls):
  tool results are user content immediately following their assistant tool group.
- [Effort](https://platform.claude.com/docs/en/build-with-claude/effort): native
  effort is output_config.effort on current models; do not treat it as a numeric
  thinking budget.

The SDKs are reference concepts only. Existing owned curl/raw-capture machinery
remains the transport. Existing OAuth identity headers/system prefix are reused.

## Design

1. Translate retained native protocol entries at the external boundary. User,
   assistant text, complete tool calls/results preserve order. Chronological
   developer guidance becomes explicitly labeled user content; original initial
   instructions are top-level system. Consecutive blocks from one original
   assistant message retain its message ID and order. Reject unsupported content,
   orphan/duplicate/incomplete tool groups and foreign opaque reasoning before
   dispatch. Preserve authored schemas without inventing required arguments.
2. Preserve native thinking/redacted blocks as native Values on tagged reasoning
   entries, never JSON persistence or fake encrypted_content. Ordinary managed
   context already preserves native entry Values. Successor-seed validation
   additionally accepts exactly this native shape. Responses export refuses these entries.
   Managed context remains model authored; native adapters do not silently
   rewrite invalidated signed history after a destructive edit.
3. A bounded SSE accumulator checks message and indexed block lifecycle, preserves
   signatures/data exactly, validates complete JSON object tool inputs and only
   publishes a response after message_stop. Failed/incomplete/malformed streams
   cannot dispatch local tools. Unknown standalone future events are ignored;
   unknown semantic blocks/deltas are refused rather than partially accepted.
4. Extend existing presentation-only preview to recognize native Messages text
   events, keeping exact upstream SSE in raw/original capture. Do not synthesize
   fake Responses stream bytes. Preview never publishes context or executes tools.
5. Bind selected owned account once per request group, pass cancellation/deadline
   and RawAdapterContext through existing ProviderAuth, refuse changed selection.
   Normalize token usage into existing engine fields while retaining full upstream
   bytes. Cache read and cache creation input tokens count toward total input.
6. Retain request-only operational state for this provider before request assembly
   closes, so subsequent signed thinking sees the same chronological input state.
   Preserve ordinary Responses behavior. Main factory/route validation explicitly
   supports built-in anthropic Messages; other Messages registrations remain
   unsupported until separately owned.

## Direct checks and bound

Write a native-main executable test first: injected curl fixtures run the actual
launcher, auth key storage, Lua workflow, exact once-only file effect, signed and
redacted thinking + parallel tool IDs, tool-result continuation, previews, usage,
reopen and changed-account refusal. Negative semantic streams must fail before any
local effect. Pure adapter tests attack chunk boundaries, block lifecycle, tail
corruption, argument completion and cross-provider export. Exercise cancellation
without requests to a real service and inspect synthetic session originals for
exact stream bytes and absence of fixture key.

RED must fail because the Messages route is currently refused. GREEN runs only
new direct cases plus relevant existing Responses/context/coding checks. Root
owns one independent source review and combined full source gate. No broad
assurance churn, performance rerun (no TUI visual change), AFL interaction or
mutation campaign is part of this unit.
