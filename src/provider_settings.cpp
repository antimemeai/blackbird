#include "blackbird/provider_settings.hpp"
#include <algorithm>

namespace blackbird {
namespace {
// Every entry was read against the vendor document on this date. Excerpts are in
// papers/provider-settings-2026-10-18/; docs/PROVIDER_SETTINGS.md explains drift.
constexpr std::string_view read = "2026-10-18";
using E = Effort;

// OpenAI Responses API (owned API key or OAuth account).
constexpr DocSource openai_create{
    "https://developers.openai.com/api/reference/resources/responses/methods/create",
    read};
constexpr DocSource openai_reasoning{
    "https://developers.openai.com/api/docs/guides/reasoning", read};
constexpr DocSource openai_latest{
    "https://developers.openai.com/api/docs/guides/latest-model", read};
constexpr std::array openai_efforts{
    EffortSupport{E::none, "no reasoning; latency-critical tasks"},
    EffortSupport{E::minimal, "least reasoning where the model supports it"},
    EffortSupport{E::low, "efficient reasoning with a modest latency increase"},
    EffortSupport{E::medium, "balanced; the documented default for current models"},
    EffortSupport{E::high, "hard reasoning, complex debugging, deep planning"},
    EffortSupport{E::xhigh, "deep research and long agentic runs"},
    EffortSupport{E::max, "maximum reasoning for the most complex tasks"}};
constexpr std::array openai_astra_excluded{E::none};
constexpr std::array openai_sol_excluded{E::none, E::minimal};
constexpr std::array openai_model_efforts{
    ModelEffortRule{"gpt-6-astra", openai_astra_excluded, openai_reasoning},
    ModelEffortRule{"gpt-6.1-sol", openai_sol_excluded, openai_latest}};
constexpr std::array<std::string_view, 1> openai_unsupported{
    "temperature, top_p and top_logprobs when reasoning effort is not none (GPT-6)"};
constexpr std::array openai_sources{openai_create, openai_reasoning, openai_latest};

// Codex ChatGPT backend (Codex CLI ChatGPT login). No public wire reference; the
// Codex documents describe the levels its models advertise.
constexpr DocSource codex_models{"https://developers.openai.com/codex/models", read};
constexpr DocSource codex_config{"https://developers.openai.com/codex/config-reference",
                                 read};
constexpr std::array codex_efforts{
    EffortSupport{E::low, "Light in the apps; quick, well-scoped tasks"},
    EffortSupport{E::medium, "balances speed and depth"},
    EffortSupport{E::high, "difficult multi-step work"},
    EffortSupport{E::xhigh, "Extra High; difficult multi-step work"},
    EffortSupport{E::max, "more time to reason about a single task; plan dependent"}};
constexpr std::array<std::string_view, 1> codex_unsupported{
    "ultra: Codex runs it as client-side subagents, which Blackbird does not run"};
constexpr std::array codex_sources{codex_models, codex_config};

// Anthropic Messages.
constexpr DocSource anthropic_effort{
    "https://platform.claude.com/docs/en/build-with-claude/effort", read};
constexpr DocSource anthropic_models{
    "https://platform.claude.com/docs/en/about-claude/models/overview", read};
constexpr std::array anthropic_efforts{
    EffortSupport{E::low, "most efficient; significant token savings"},
    EffortSupport{E::medium,
                  "moderate token savings; default on Opus 5.5 and Haiku 5.5"},
    EffortSupport{E::high, "as many tokens as needed; default on most models"},
    EffortSupport{E::xhigh, "extended capability for long-horizon work"},
    EffortSupport{E::max, "no constraint on token spending"}};
constexpr std::array anthropic_no_xhigh{E::xhigh};
constexpr std::array anthropic_no_xhigh_max{E::xhigh, E::max};
constexpr std::array anthropic_model_efforts{
    ModelEffortRule{"claude-mythos-preview", anthropic_no_xhigh, anthropic_effort},
    ModelEffortRule{"claude-opus-4-6", anthropic_no_xhigh, anthropic_effort},
    ModelEffortRule{"claude-sonnet-4-6", anthropic_no_xhigh, anthropic_effort},
    ModelEffortRule{"claude-opus-4-5", anthropic_no_xhigh_max, anthropic_effort}};
constexpr std::array<std::string_view, 1> anthropic_unsupported{
    "thinking disabled on Opus 5.5 (400 at every effort)"};
constexpr std::array anthropic_sources{anthropic_effort, anthropic_models};

// OpenRouter Responses.
constexpr DocSource openrouter_reasoning{
    "https://openrouter.ai/docs/guides/best-practices/reasoning-tokens", read};
constexpr DocSource openrouter_responses{
    "https://openrouter.ai/docs/api/reference/responses/overview", read};
constexpr std::array openrouter_efforts{
    EffortSupport{E::none, "disables reasoning entirely"},
    EffortSupport{E::minimal, "about 10% of max_tokens for reasoning"},
    EffortSupport{E::low, "about 20% of max_tokens for reasoning"},
    EffortSupport{E::medium, "about 50% of max_tokens for reasoning"},
    EffortSupport{E::high, "about 80% of max_tokens for reasoning"},
    EffortSupport{E::xhigh, "same allocation as max (about 95%)"},
    EffortSupport{E::max, "about 95% of max_tokens for reasoning"}};
constexpr std::array<std::string_view, 2> openrouter_unsupported{
    "store: true", "non-null previous_response_id"};
constexpr std::array openrouter_sources{openrouter_reasoning, openrouter_responses};

// Paper Ultramarine Responses.
constexpr DocSource paper_responses{
    "https://docs.paperinstruments.com/docs/ultramarine/responses", read};
constexpr DocSource paper_sampling{
    "https://docs.paperinstruments.com/docs/ultramarine/default-sampling-config", read};
constexpr std::array paper_efforts{
    EffortSupport{E::low, "fewer reasoning tokens"},
    EffortSupport{E::high, "fewer reasoning tokens than max"},
    EffortSupport{E::max, "default and recommended"}};
constexpr std::array<std::string_view, 1> paper_models{"ultramarine-1-flash"};
constexpr std::array<std::string_view, 8> paper_unsupported{
    "top_k",
    "non-empty include (encrypted reasoning)",
    "background: true",
    "conversation",
    "automatic truncation",
    "non-empty stream_options",
    "developer messages",
    "images, audio, files and item references"};
constexpr std::array paper_sources{paper_responses, paper_sampling};

// MiMo Responses: pay-as-you-go and the Token Plan clusters.
constexpr DocSource mimo_responses{
    "https://mimo.mi.com/static/docs/api/chat/responses.md", read};
constexpr DocSource mimo_plan{
    "https://mimo.mi.com/static/docs/quick-start/faq/token-plan/Plans&Pricing.md",
    read};
constexpr DocSource mimo_plan_access{
    "https://mimo.mi.com/static/docs/tokenplan/Token%20Plan/quick-access.md", read};
constexpr std::array mimo_efforts{
    EffortSupport{E::none, "thinking off"},
    EffortSupport{E::minimal, "mapped to low: thinking on"},
    EffortSupport{E::low, "thinking on"},
    EffortSupport{E::medium, "thinking on"},
    EffortSupport{E::high, "thinking on"},
    EffortSupport{E::xhigh, "mapped to high: thinking on"},
    EffortSupport{E::max, "mapped to high: thinking on"},
    EffortSupport{E::ultra, "mapped to high: thinking on"}};
constexpr std::string_view mimo_equivalence =
    "MiMo documents none as thinking off and every other level as the same "
    "thinking on; custom effort tuning is unsupported";
constexpr std::array<std::string_view, 5> mimo_models{
    "mimo-v2.6-flash", "mimo-v2.6-pro", "mimo-v2.6-pro-ultraspeed", "mimo-v2.5-pro",
    "mimo-v2.5"};
constexpr std::array<std::string_view, 4> mimo_plan_models{
    "mimo-v2.6-pro", "mimo-v2.6-flash", "mimo-v2.5-pro", "mimo-v2.5"};
constexpr std::array mimo_plan_restricted{ModelRestriction{
    "mimo-v2.6-pro-ultraspeed",
    "listed in the Responses reference but not in the Token Plan model list; the "
    "Singapore cluster answered HTTP 400 'Not supported model "
    "mimo-v2.6-pro-ultraspeed' on 2026-10-17",
    mimo_plan}};
constexpr std::array<std::string_view, 4> mimo_unsupported{
    "background", "previous_response_id", "context_management",
    "temperature and top_p in thinking mode (forced to 1.0 and 0.95)"};
constexpr OutputCeiling mimo_output{
    131072, true,
    "default and maximum for mimo-v2.6 models and mimo-v2.5-pro; mimo-v2.5 "
    "defaults to 32768"};
constexpr std::array mimo_sources{mimo_responses};
constexpr std::array mimo_plan_sources{mimo_responses, mimo_plan, mimo_plan_access};

constexpr EndpointSettings mimo_plan_cluster(std::string_view name,
                                             std::string_view base_url) {
  return {.name = name,
          .protocol = "responses",
          .base_url = base_url,
          .efforts = mimo_efforts,
          .default_effort = "thinking on",
          .equivalence = mimo_equivalence,
          .models = mimo_plan_models,
          .model_source = mimo_plan,
          .restricted = mimo_plan_restricted,
          .output = mimo_output,
          .unsupported_parameters = mimo_unsupported,
          .sources = mimo_plan_sources};
}

constexpr std::array table{
    EndpointSettings{
        .name = "OpenAI Responses API",
        .protocol = "responses",
        .base_url = "https://api.openai.com/v1",
        .efforts = openai_efforts,
        .default_effort =
            "model dependent; medium for GPT-5.5, GPT-5.6 and GPT-6 models",
        .model_efforts = openai_model_efforts,
        .output = {0, true, "model dependent; max_output_tokens counts reasoning"},
        .unsupported_parameters = openai_unsupported,
        .sources = openai_sources},
    EndpointSettings{.name = "OpenAI Codex ChatGPT backend",
                     .protocol = "responses",
                     .base_url = codex_chatgpt_base_url,
                     .efforts = codex_efforts,
                     .default_effort = "the client's default for the model and plan",
                     .output = {0, true, "not documented for the backend"},
                     .unsupported_parameters = codex_unsupported,
                     .sources = codex_sources},
    EndpointSettings{.name = "Anthropic Messages",
                     .protocol = "messages",
                     .base_url = "https://api.anthropic.com/v1",
                     .efforts = anthropic_efforts,
                     .default_effort =
                         "high; medium on Claude Opus 5.5 and Claude Haiku 5.5",
                     .model_efforts = anthropic_model_efforts,
                     .output = {128000, true,
                                "current models; max_tokens bounds thinking plus "
                                "text; Blackbird sends 16384 by default"},
                     .unsupported_parameters = anthropic_unsupported,
                     .sources = anthropic_sources},
    EndpointSettings{
        .name = "OpenRouter Responses",
        .protocol = "responses",
        .base_url = "https://openrouter.ai/api/v1",
        .efforts = openrouter_efforts,
        .default_effort = "per model (reasoning.default_effort in GET /models)",
        .output = {0, true, "per routed model; effort sets the reasoning share"},
        .unsupported_parameters = openrouter_unsupported,
        .sources = openrouter_sources},
    EndpointSettings{
        .name = "Paper Ultramarine Responses",
        .protocol = "responses-portable",
        .base_url = "https://api.paperinstruments.com/v1",
        .efforts = paper_efforts,
        .default_effort = "max",
        .equivalence = "reasoning cannot be disabled",
        .models = paper_models,
        .model_source = paper_responses,
        .output = {0, true, "omitted max_output_tokens uses the model's ceiling"},
        .unsupported_parameters = paper_unsupported,
        .sources = paper_sources},
    EndpointSettings{.name = "MiMo Responses (pay as you go)",
                     .protocol = "responses",
                     .base_url = "https://api.xiaomimimo.com/v1",
                     .efforts = mimo_efforts,
                     .default_effort = "thinking on",
                     .equivalence = mimo_equivalence,
                     .models = mimo_models,
                     .model_source = mimo_responses,
                     .output = mimo_output,
                     .unsupported_parameters = mimo_unsupported,
                     .sources = mimo_sources},
    mimo_plan_cluster("MiMo Token Plan (China)",
                      "https://token-plan-cn.xiaomimimo.com/v1"),
    mimo_plan_cluster("MiMo Token Plan (Singapore)",
                      "https://token-plan-sgp.xiaomimimo.com/v1"),
    mimo_plan_cluster("MiMo Token Plan (Europe)",
                      "https://token-plan-ams.xiaomimimo.com/v1")};

// Fallback for registered endpoints the table does not describe: the wire rule
// Blackbird applied before the table existed.
constexpr std::array responses_fallback{E::low, E::medium, E::high, E::xhigh, E::max};
constexpr std::array portable_fallback{E::low, E::high, E::max};

std::string join(std::span<const Effort> efforts) {
  std::string out;
  for (const auto effort : efforts) {
    if (!out.empty())
      out += ", ";
    out += effort_name(effort);
  }
  return out;
}
std::string join(std::span<const std::string_view> names) {
  std::string out;
  for (const auto name : names) {
    if (!out.empty())
      out += ", ";
    out += name;
  }
  return out;
}
std::string cite(const DocSource &source) {
  return " Source: " + std::string{source.url} + " (read " + std::string{source.read} +
         "; docs can drift, see docs/PROVIDER_SETTINGS.md).";
}
} // namespace

std::optional<Effort> parse_effort(std::string_view text) noexcept {
  for (const auto effort : all_efforts)
    if (effort_name(effort) == text)
      return effort;
  return std::nullopt;
}
std::string_view effort_name(Effort effort) noexcept {
  switch (effort) {
  case Effort::none:
    return "none";
  case Effort::minimal:
    return "minimal";
  case Effort::low:
    return "low";
  case Effort::medium:
    return "medium";
  case Effort::high:
    return "high";
  case Effort::xhigh:
    return "xhigh";
  case Effort::max:
    return "max";
  case Effort::ultra:
    return "ultra";
  }
  return {};
}
std::span<const EndpointSettings> provider_settings() noexcept { return table; }
const EndpointSettings *endpoint_settings(std::string_view protocol,
                                          std::string_view base_url) noexcept {
  for (const auto &endpoint : table)
    if (endpoint.protocol == protocol && endpoint.base_url == base_url)
      return &endpoint;
  return nullptr;
}
std::span<const Effort> protocol_efforts(std::string_view protocol) noexcept {
  if (protocol == "responses")
    return responses_fallback;
  if (protocol == "responses-portable")
    return portable_fallback;
  return {};
}
std::vector<Effort> accepted_efforts(const EndpointSettings &endpoint,
                                     std::string_view model) {
  std::vector<Effort> out;
  for (const auto &support : endpoint.efforts) {
    const bool excluded =
        std::ranges::any_of(endpoint.model_efforts, [&](const ModelEffortRule &rule) {
          return model.starts_with(rule.model_prefix) &&
                 std::ranges::find(rule.unsupported, support.effort) !=
                     rule.unsupported.end();
        });
    if (!excluded)
      out.push_back(support.effort);
  }
  return out;
}
std::optional<std::string> refuse_launch(const LaunchChoice &choice) {
  const auto [protocol, base_url, model, effort] = choice;
  const auto *endpoint = endpoint_settings(protocol, base_url);
  const auto chosen = parse_effort(effort);
  if (endpoint == nullptr) {
    const auto rule = protocol_efforts(protocol);
    if (chosen && std::ranges::find(rule, *chosen) != rule.end())
      return std::nullopt;
    return "Effort '" + std::string{effort} + "' is not accepted for " +
           std::string{protocol} + " endpoint " + std::string{base_url} +
           ", which the provider settings table does not describe. The " +
           std::string{protocol} + " wire rule accepts: " + join(rule) + ".";
  }
  const std::string where =
      std::string{endpoint->name} + " (" + std::string{endpoint->base_url} + ")";
  for (const auto &restriction : endpoint->restricted)
    if (restriction.model == model)
      return "Model '" + std::string{model} + "' is not available on " + where + ": " +
             std::string{restriction.reason} +
             ". Documented models: " + join(endpoint->models) + "." +
             cite(restriction.source);
  if (!endpoint->models.empty() &&
      std::ranges::find(endpoint->models, model) == endpoint->models.end())
    return "Model '" + std::string{model} + "' is not documented for " + where +
           ". Documented models: " + join(endpoint->models) + "." +
           cite(endpoint->model_source);
  const auto accepted = accepted_efforts(*endpoint, model);
  if (chosen && std::ranges::find(accepted, *chosen) != accepted.end())
    return std::nullopt;
  std::string refusal =
      "Effort '" + std::string{effort} + "' is not documented for " + where +
      (accepted.size() < endpoint->efforts.size() ? " with model " + std::string{model}
                                                  : std::string{}) +
      ". Accepted: " + join(accepted) + ".";
  if (!endpoint->equivalence.empty())
    refusal += " Note: " + std::string{endpoint->equivalence} + ".";
  return refusal + cite(endpoint->sources.front());
}
} // namespace blackbird
