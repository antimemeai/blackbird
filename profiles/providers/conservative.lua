-- Profile `conservative`: every route without a more specific profile, and the base
-- layer under every shipped profile. Data only: read natively, never executed
-- (docs/CONTEXT_LIFECYCLE.md §15). A basis detail names the code site whose behaviour
-- the value reproduces, or why the value is the safe choice.
return {
  schema = "blackbird.provider-profile/1",
  id = "conservative",
  revision = 1,
  match = {},
  pairing = {
    validator = "codec",
    basis = "conservative:no validator exists besides each codec's own export checks",
  },
  reasoning = {
    at_cut = "keep",
    at_rebind = "drop_foreign",
    side_request_reasoning = "keep",
    basis = {
      at_cut = "conservative:§15.4; no cut exists today",
      at_rebind = "conservative:the §6.3 rebind rule, new with S3a; today no adapter accepts foreign reasoning, it throws instead (src/json.cpp:457-459, src/anthropic_provider.cpp:412-419, src/responses_provider.cpp:44-46)",
      side_request_reasoning = "conservative:a side request repeats the main request",
    },
  },
  turn = {
    start = "user_message_without_results",
    close = "none",
    open_turn = { "closed_only" },
    boundary = {
      form = "user_note",
      enabled = false,
      basis = "conservative:no boundary form is probed on an unknown route",
    },
    basis = "conservative:§15.4; settle only at a closed turn",
  },
  request_state = {
    placement = "request_local",
    basis = "conservative:matches today: retain_request_state() is false (include/blackbird/coding.hpp:37), so the state item goes at the end of each request's input and is not kept in context (src/coding.cpp:4484-4493)",
  },
  note = {
    role = "developer",
    placement = "after_tool_results",
    basis = "conservative:Blackbird's canonical role: the state is a developer item at the end of the input (src/coding.cpp:4480-4493); portable Responses routes send that item as system (src/responses_provider.cpp:47-48)",
  },
  tool_restriction = {
    mode = "palette_only",
    basis = "conservative:§24 owner decision; allowed_tools waits for its probe",
  },
  tool_change = { mode = "cut", basis = "conservative:§6.4 shipped default" },
  archival = { mode = "allowed", basis = "conservative:cuts wait for a closed turn" },
  edit_acceptance = { in_place = "unknown", basis = "conservative:not probed" },
  cache = {
    capability = "none",
    basis = "conservative:no cache controls; today's model-gated explicit policy (src/request_cache.cpp:87-93) moves in P2/K1",
  },
  retry = {
    http = { 408, 429, 500, 502, 503, 504 },
    transport = { 5, 6, 7, 16, 18, 28, 52, 55, 56, 92, 95, 96 },
    rate_limit_requires_retry_after = false,
    basis = "conservative:matches today: the engine's retry set; a 429 is retried with or without retry-after (src/coding.cpp:4726-4736)",
  },
}
