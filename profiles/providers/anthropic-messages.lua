-- Profile `anthropic-messages`: native Anthropic Messages for the older families that
-- do not run the preserved-thinking prefix check (Mythos 5.1 and families before Fable
-- 5.1). Each `models` entry is a family: the name, or the name, `-` and any suffix such
-- as a snapshot date (src/anthropic_provider.cpp:486-492 identifies families by name the
-- same way). A family whose name is a `-`-prefix of a prefix-checked name (Fable 5,
-- Mythos 5, Opus 5, Sonnet 5 would capture 5.1 and 5.5 snapshots), and Sonnet 4 and
-- Opus 4 (whose bare names would capture every later 4.x), are deliberately not listed:
-- they take the prefix-checked default, which drops more thinking but is valid. Rows
-- marked unobserved are built against mocks until an Anthropic account and probe rd7.3
-- exist (plan §1.4).
return {
  schema = "blackbird.provider-profile/1",
  id = "anthropic-messages",
  revision = 1,
  match = {
    provider = "anthropic",
    protocol = "messages",
    models = {
      "claude-mythos-5-1",
      "claude-opus-4-8",
      "claude-opus-4-7",
      "claude-opus-4-6",
      "claude-opus-4-5",
      "claude-opus-4-1",
      "claude-sonnet-4-6",
      "claude-sonnet-4-5",
      "claude-haiku-4-5",
      "claude-3-7-sonnet",
    },
  },
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
    at_cut = "keep",
    at_rebind = "keep_in_family",
    side_request_reasoning = "drop_all",
    basis = {
      at_cut = "cited:preserved",
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
