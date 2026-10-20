-- Profile `anthropic-messages-prefix-checked`: the default for native Anthropic
-- Messages (owner decision, 2026-10-20). Fable 5.1, Opus 5.5, Sonnet 5.5 and Haiku 5.5
-- run the preserved-thinking prefix check, which binds each thinking block to everything
-- before it, and any new or unknown family is assumed to as well: dropping reasoning
-- after a change is valid on every Anthropic model, keeping it is not valid here. Only
-- the older families listed in `anthropic-messages` keep reasoning at a cut. Rows marked
-- unobserved are built against mocks until an Anthropic account and probe rd7.3 exist
-- (plan §1.4).
return {
  schema = "blackbird.provider-profile/1",
  id = "anthropic-messages-prefix-checked",
  revision = 1,
  match = { provider = "anthropic", protocol = "messages" },
  sources = {
    preserved = {
      url = "https://platform.claude.com/docs/en/build-with-claude/preserved-thinking",
      read = "2026-10-19",
      title = "Preserved thinking",
    },
    thinking = {
      url = "https://platform.claude.com/docs/en/build-with-claude/thinking",
      read = "2026-10-19",
      title = "Extended thinking",
    },
    tool_calls = {
      url = "https://platform.claude.com/docs/en/agents-and-tools/tool-use/handle-tool-calls",
      read = "2026-10-19",
      title = "Handle tool calls",
    },
  },
  pairing = {
    validator = "anthropic-messages",
    basis = "cited:tool_calls",
  },
  reasoning = {
    at_cut = "drop_after_change",
    at_rebind = "keep_in_family",
    side_request_reasoning = "drop_all",
    basis = {
      at_cut = "unobserved:Preserved thinking prefix check; mock-only until rd7.3",
      at_rebind = "cited:preserved",
      side_request_reasoning = "unobserved:palette_only changes tools, so the step's copy drops all thinking (§13.1); mock-only until rd7.3",
    },
  },
  turn = {
    start = "user_message_without_results",
    close = "reply_request",
    open_turn = { "closed_only", "boundary" },
    boundary = {
      form = "reply_then_note",
      enabled = false,
      basis = "conservative:probe rd7.3 pending",
    },
    basis = {
      start = "unobserved:Extended thinking turn rule; mock-only until rd7.3",
      close = "unobserved:closing reply with tool_choice none; mock-only until rd7.3",
      open_turn = "conservative:closed_only until rd7.3; boundary listed, disabled until its probe",
    },
  },
  request_state = {
    placement = "retained_in_place",
    basis = "unobserved:matches today: retain_request_state() is true (include/blackbird/anthropic_provider.hpp:15), so the state item is appended to the context and to the end of the input (src/coding.cpp:4484-4493) and later requests carry it where it was first sent",
  },
  note = {
    role = "developer_as_user_text",
    placement = "after_tool_results",
    basis = "conservative:matches today: developer items render as user text after tool results (src/anthropic_provider.cpp:428-434)",
  },
  tool_change = { mode = "cut", basis = "cited:preserved" },
  archival = { mode = "allowed", basis = "cited:preserved" },
}
