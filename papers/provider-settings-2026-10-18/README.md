# Provider settings sources (read 2026-10-18)

Verbatim excerpts of the vendor documents behind Blackbird's provider settings table
(`src/provider_settings.cpp`, described in `docs/PROVIDER_SETTINGS.md`). Each file
names its URLs and the line ranges of the Markdown served on 2026-10-18.

| File | Provider endpoints |
| --- | --- |
| [openai.md](openai.md) | OpenAI Responses API (`api.openai.com/v1`), Codex ChatGPT backend |
| [anthropic.md](anthropic.md) | Anthropic Messages (`api.anthropic.com/v1`) |
| [openrouter.md](openrouter.md) | OpenRouter Responses (`openrouter.ai/api/v1`) |
| [paper.md](paper.md) | Paper Ultramarine Responses (`api.paperinstruments.com/v1`) |
| [mimo.md](mimo.md) | MiMo pay-as-you-go and Token Plan clusters, plus the observed 400 |

The Codex ChatGPT backend has no public wire reference; its entry rests on the Codex
model and configuration documents and on the quarantined Codex source
(`quarantine/codex/codex-rs/protocol/src/openai_models.rs`, `ReasoningEffort`).
