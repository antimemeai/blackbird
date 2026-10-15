#pragma once

#include "blackbird/raw_provider.hpp"
#include "blackbird/raw_reader.hpp"
#include "blackbird/value.hpp"

#include <string>

namespace blackbird {

enum class UsageActor : std::uint8_t { main, colleague, decision, unknown };
enum class UsageScope : std::uint8_t { aggregate, model_alternative };
enum class UsageDispatch : std::uint8_t {
  unknown,
  outbound_captured,
  child_started,
  response_observed,
  locally_refused
};
struct UsageSource {
  std::string run_directory;
  RawRunCursor cursor;
  RawEventRef event;
  std::size_t body_offset = 0;
  std::size_t bytes = 0;
};
struct UsageField {
  std::string name;
  Number value;
};
struct UsageCounters {
  std::optional<std::uint64_t> input, output, cache_read, cache_write, reasoning;
  // Anthropic's input excludes cache categories; Responses/Chat input includes them.
  bool input_includes_cache = true;
  bool operator==(const UsageCounters &) const = default;
};
struct UsageObservation {
  UsageActor actor = UsageActor::unknown;
  UsageScope scope = UsageScope::aggregate;
  RawEventRef request{};
  std::optional<IdentityBytes> attempt, invocation, generation, retry_group;
  std::string provider, requested_model, actual_model, service_tier, response_id;
  std::string reported_provider, route_url;
  // Not inferred from a route/model; empty until an actual non-secret binding exists.
  std::string account;
  std::string response_status;
  // Native terminal branch and stage remain distinct from external response status.
  std::optional<std::uint8_t> terminal_branch, terminal_stage;
  std::optional<int> http_status;
  UsageDispatch dispatch = UsageDispatch::unknown;
  UsageCounters counters;
  std::vector<UsageField> reported_fields;
  std::optional<Number> observed_cost_usd;
  std::optional<Number> reported_cost;
  bool reported_cost_explicit_usd = false, malformed_cost = false,
       conflicting_cost = false;
  bool service_tier_reported = false, pricing_conflict = false;
  std::vector<UsageSource> sources, request_sources;
  bool usage_present = false;
  bool partial_usage = false;
  bool malformed = false;
  bool conflicting = false;
};
struct UsageCoverageIssue {
  std::string run_directory, reason;
  std::optional<UsageSource> source;
  std::optional<Error> error;
};
struct UsageRunCoverage {
  std::string run_directory;
  std::optional<RawRunCursor> scanned_through;
  std::uint64_t records = 0, bytes = 0;
  bool closed = false, reached_preserved_end = false;
};
struct TokenUsageReport {
  std::vector<UsageObservation> observations;
  std::vector<UsageCoverageIssue> issues;
  std::vector<UsageRunCoverage> runs;
  // Always false: a preserved raw scan cannot establish complete semantic history.
  bool complete_session_ledger = false;
};
struct TokenUsageLimits {
  RawReaderLimits raw;
  std::size_t max_runs = 1024;
  std::size_t max_records = 1000000;
  std::size_t max_scan_bytes = 256 * 1024 * 1024;
  std::size_t max_body_bytes = 16 * 1024 * 1024;
  std::size_t max_retained_bytes = 64 * 1024 * 1024;
  std::size_t max_observations = 100000;
  std::size_t max_causal_depth = 256;
};
// Read-only pinned raw prefixes. Missing/corrupt runs remain coverage issues.
Result<TokenUsageReport> read_token_usage(std::string_view session_directory,
                                          TokenUsageLimits limits = {});

struct UsageSubtotal {
  UsageActor actor;
  std::string provider;
  UsageCounters known;
  std::uint64_t observations = 0, missing_usage = 0, invalid_usage = 0;
  std::uint64_t locally_refused = 0;
  std::uint64_t partial_usage = 0;
  std::uint64_t missing_input = 0, missing_output = 0, missing_cache_read = 0,
                missing_cache_write = 0;
  bool overflow = false;
};
// Model-alternative scopes are not added to aggregate scope.
std::vector<UsageSubtotal> token_usage_subtotals(const TokenUsageReport &report);

struct UsagePrice {
  std::string date, provider, model, service_tier;
  double input_per_million = 0, output_per_million = 0;
  std::optional<double> cache_read_per_million, cache_write_per_million;
};
struct UsageEstimate {
  std::string date;
  std::optional<double> usd;
  std::string unavailable_reason;
};
UsageEstimate estimate_token_usage(const UsageObservation &observation,
                                   const UsagePrice &price);
std::string render_token_usage(const TokenUsageReport &report,
                               std::span<const UsagePrice> prices = {});

} // namespace blackbird
