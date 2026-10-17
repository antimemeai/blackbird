# Current auth documentation and implementation boundary

Primary sources read 2026-10-17, alongside [the source inventory](SOURCE_INVENTORY.md).
This is documentation research, not a live account probe.

[Claude Code authentication](https://code.claude.com/docs/en/authentication)
documents claude.ai subscription browser login, Console login and cloud routes.
This supports the existing `claude` CLI bridge architecture: the installed CLI
owns its account interaction. It does not establish that Blackbird's independent
historical OAuth client is currently accepted. No installed CLI account was
inspected during this task.

[Claude API authentication](https://platform.claude.com/docs/en/manage-claude/authentication)
documents API keys, Workload Identity Federation and App Attest. Bearer is the
current key header; the existing `x-api-key` header is still supported. Keys
spanning workspaces require `anthropic-workspace-id`, whereas workspace-scoped
keys may omit it. Blackbird's current descriptor has no workspace selector;
this is a concrete future compatibility requirement for multi-workspace keys,
not evidence that all existing API-key requests are broken.

[ant CLI authentication](https://platform.claude.com/docs/en/cli-sdks-libraries/cli/authentication)
documents browser Console OAuth, named profiles and workspace-bound tokens.
That is a separate Console flow from the existing subscription-oriented
claude.ai client in Blackbird. Do not substitute one login or billing route for
the other, or copy the ant credential store into Blackbird.

Conclusion from code plus docs: account lifecycle machinery already exists;
there is no missing auth module to duplicate. Current native subscription
OAuth acceptance remains unqualified. Existing deterministic fixtures qualify
local behavior, not vendor acceptance. Main-model Claude requires Messages
streaming/conversation/tool-use integration, independently of login. These are
separate follow-through items; this request does not justify silently changing
the provider protocol or account/billing selection.
