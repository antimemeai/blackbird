#include "blackbird/token_usage.hpp"
#include "blackbird/json.hpp"
#include "blackbird/packet.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>

namespace blackbird {
namespace {
struct Key {
  IdentityBytes lane{};
  std::uint64_t ordinal = 0;
  auto operator<=>(const Key &) const = default;
};
RawEventRef reference(Key key) { return {key.lane, key.ordinal}; }
class Fields {
public:
  explicit Fields(ByteView bytes) : bytes_(bytes) {}
  template <class T> T get() {
    if (bytes_.size() < sizeof(T))
      throw Error{ErrorCode::corrupt};
    T value;
    std::memcpy(&value, bytes_.data(), sizeof(T));
    bytes_ = bytes_.subspan(sizeof(T));
    return value;
  }
  bool boolean() {
    const auto value = get<std::uint8_t>();
    if (value > 1)
      throw Error{ErrorCode::corrupt};
    return value != 0;
  }
  Key key() { return {get<IdentityBytes>(), get<std::uint64_t>()}; }
  ByteView remaining() const { return bytes_; }
  std::string text() {
    const auto size = get<std::size_t>();
    if (size > bytes_.size())
      throw Error{ErrorCode::corrupt};
    std::string result{reinterpret_cast<const char *>(bytes_.data()), size};
    bytes_ = bytes_.subspan(size);
    return result;
  }
  void end() const {
    if (!bytes_.empty())
      throw Error{ErrorCode::corrupt};
  }

private:
  ByteView bytes_;
};
struct Node {
  std::uint32_t site = 0;
  std::uint8_t kind = 0;
  std::optional<Key> parent;
  std::optional<IdentityBytes> attempt, invocation, generation, group;
  std::optional<std::uint8_t> branch, stage;
  std::optional<int> http_status;
  std::optional<UsageSource> source;
  bool outbound = false, started = false, response = false;
};
struct Body {
  Key owner;
  std::string bytes;
  std::vector<UsageSource> sources;
  unsigned rank = 0;
  bool request = false;
  std::string url;
  bool native_packet = false;
};
struct DirectoryCloser {
  void operator()(DIR *directory) const noexcept { (void)::closedir(directory); }
};
template <class T> T need(Result<T> result) {
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
std::optional<std::uint64_t> integer(const Value *value) {
  if (!value || !std::holds_alternative<Number>(value->value()))
    return {};
  return std::visit(
      [](const auto &number) -> std::optional<std::uint64_t> {
        using T = std::decay_t<decltype(number)>;
        if constexpr (std::is_same_v<T, Decimal>) {
          if (std::all_of(number.limbs.begin(), number.limbs.end(),
                          [](auto limb) { return limb == 0; }))
            return 0;
          if (number.negative || number.exponent < 0 || number.exponent > 19)
            return {};
          std::uint64_t result = 0;
          for (auto limb = number.limbs.rbegin(); limb != number.limbs.rend(); ++limb) {
            if (result > (UINT64_MAX - *limb) / 1000000000)
              return {};
            result = result * 1000000000 + *limb;
          }
          for (std::int64_t exponent = 0; exponent < number.exponent; ++exponent) {
            if (result > UINT64_MAX / 10)
              return {};
            result *= 10;
          }
          return result;
        } else if constexpr (std::is_same_v<T, std::uint64_t>)
          return number;
        else if constexpr (std::is_same_v<T, std::int64_t>)
          return number >= 0 ? std::optional{static_cast<std::uint64_t>(number)}
                             : std::nullopt;
        else {
          if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
              static_cast<long double>(number) > static_cast<long double>(UINT64_MAX))
            return {};
          return static_cast<std::uint64_t>(number);
        }
      },
      value->number().storage());
}
std::string string(const Value *value) {
  return value && std::holds_alternative<std::string>(value->value()) ? value->string()
                                                                      : std::string{};
}
const Value *field(const Value *value, std::string_view name) {
  return value ? value->find(name) : nullptr;
}
bool object(const Value *value) {
  return value && std::holds_alternative<Value::Object>(value->value());
}
void unique(const Value &value) {
  if (const auto *members = std::get_if<Value::Object>(&value.value())) {
    std::set<std::string_view> names;
    for (const auto &[name, child] : *members) {
      if (!names.insert(name).second)
        throw Error{ErrorCode::corrupt};
      unique(child);
    }
  } else if (const auto *items = std::get_if<Value::Array>(&value.value()))
    for (const auto &item : *items)
      unique(item);
}
void numeric_fields(const Value &value, std::string prefix,
                    std::vector<UsageField> &fields) {
  if (fields.size() >= 1024)
    throw Error{ErrorCode::capacity};
  if (const auto *number = std::get_if<Number>(&value.value()))
    fields.push_back({std::move(prefix), *number});
  else if (const auto *members = std::get_if<Value::Object>(&value.value()))
    for (const auto &[name, child] : *members)
      numeric_fields(child, prefix.empty() ? name : prefix + "." + name, fields);
}
void merge_fields(std::vector<UsageField> &target,
                  const std::vector<UsageField> &next) {
  for (const auto &candidate : next)
    if (std::none_of(target.begin(), target.end(), [&](const auto &old) {
          return old.name == candidate.name && old.value == candidate.value;
        }))
      target.push_back(candidate);
}
void counter(const Value *value, std::optional<std::uint64_t> &target,
             UsageObservation &observation) {
  if (!value)
    return;
  const auto parsed = integer(value);
  if (!parsed)
    observation.malformed = true;
  else
    target = parsed;
}
enum class CostUnit { unqualified, usd };
void cost(const Value *value, UsageObservation &observation,
          CostUnit unit = CostUnit::unqualified) {
  if (!value || std::holds_alternative<std::nullptr_t>(value->value()))
    return;
  if (const auto *number = std::get_if<Number>(&value->value())) {
    const auto text = number->text();
    double amount = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), amount);
    if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
        std::isfinite(amount) && amount >= 0) {
      if (observation.reported_cost && *observation.reported_cost != *number)
        observation.conflicting_cost = true;
      if (!observation.reported_cost)
        observation.reported_cost = *number;
      observation.reported_cost_explicit_usd =
          observation.reported_cost_explicit_usd || unit == CostUnit::usd;
      return;
    }
  }
  observation.malformed_cost = true;
}
void parse_usage(const Value &usage, UsageObservation &observation, bool camel,
                 bool anthropic) {
  observation.usage_present = true;
  if (!object(&usage)) {
    observation.malformed = true;
    return;
  }
  numeric_fields(usage, "usage", observation.reported_fields);
  const bool chat = usage.find("prompt_tokens") || usage.find("completion_tokens");
  anthropic = anthropic || usage.find("cache_read_input_tokens") ||
              usage.find("cache_creation_input_tokens");
  auto &counts = observation.counters;
  counts.input_includes_cache = !anthropic;
  counter(usage.find(camel  ? "inputTokens"
                     : chat ? "prompt_tokens"
                            : "input_tokens"),
          counts.input, observation);
  counter(usage.find(camel  ? "outputTokens"
                     : chat ? "completion_tokens"
                            : "output_tokens"),
          counts.output, observation);
  if (camel || anthropic) {
    counter(usage.find(camel ? "cacheReadInputTokens" : "cache_read_input_tokens"),
            counts.cache_read, observation);
    counter(
        usage.find(camel ? "cacheCreationInputTokens" : "cache_creation_input_tokens"),
        counts.cache_write, observation);
  } else {
    const auto *input =
        usage.find(chat ? "prompt_tokens_details" : "input_tokens_details");
    const auto *output =
        usage.find(chat ? "completion_tokens_details" : "output_tokens_details");
    counter(field(input, "cached_tokens"), counts.cache_read, observation);
    counter(field(input, "cache_write_tokens"), counts.cache_write, observation);
    counter(field(output, "reasoning_tokens"), counts.reasoning, observation);
  }
  cost(usage.find("cost"), observation);
}
void parse_response(const Value &value, std::vector<UsageObservation> &scopes) {
  if (!object(&value))
    return;
  if (const auto *nested = value.find("response"); object(nested)) {
    parse_response(*nested, scopes);
    return;
  }
  if (const auto *nested = value.find("message"); object(nested)) {
    parse_response(*nested, scopes);
    return;
  }
  if (const auto *output = value.find("output");
      output && std::holds_alternative<std::string>(output->value()) &&
      value.find("exit_code")) {
    auto decoded = parse_json(output->string());
    if (decoded.has_value()) {
      unique(decoded.value());
      parse_response(decoded.value(), scopes);
    }
  }
  auto aggregate = std::find_if(scopes.begin(), scopes.end(), [](const auto &entry) {
    return entry.scope == UsageScope::aggregate;
  });
  if (aggregate == scopes.end()) {
    scopes.emplace_back();
    aggregate = std::prev(scopes.end());
  }
  const auto assign = [&](std::string_view name, std::string &target) {
    auto candidate = string(value.find(name));
    if (!candidate.empty())
      target = std::move(candidate);
  };
  assign("model", aggregate->actual_model);
  assign("actual_model", aggregate->actual_model);
  assign("requested_model", aggregate->requested_model);
  if (const auto *tier = value.find("service_tier"); tier && *tier != Value{}) {
    if (!std::holds_alternative<std::string>(tier->value()))
      aggregate->pricing_conflict = true;
    else {
      if (aggregate->service_tier_reported && aggregate->service_tier != tier->string())
        aggregate->pricing_conflict = true;
      aggregate->service_tier_reported = true;
      aggregate->service_tier = tier->string();
    }
  }
  assign("id", aggregate->response_id);
  assign("status", aggregate->response_status);
  assign("provider", aggregate->reported_provider);
  if (string(value.find("type")) == "message" && aggregate->reported_provider.empty())
    aggregate->reported_provider = "anthropic";
  if (string(value.find("remote_disposition")) == "not_dispatched")
    aggregate->dispatch = UsageDispatch::locally_refused;
  if (value.find("is_error"))
    cost(value.find("total_cost_usd"), *aggregate, CostUnit::usd);
  else
    cost(value.find("cost"), *aggregate);
  if (value.find("is_error") && aggregate->reported_provider.empty())
    aggregate->reported_provider = "claude";
  const auto *usage = value.find("usage");
  if (usage && !std::holds_alternative<std::nullptr_t>(usage->value()))
    parse_usage(*usage, *aggregate, false,
                value.find("is_error") || string(value.find("type")) == "message" ||
                    aggregate->reported_provider == "claude" ||
                    aggregate->reported_provider == "anthropic");
  const auto *models = value.find("modelUsage");
  if (!models)
    models = value.find("model_usage");
  if (models && !std::holds_alternative<std::nullptr_t>(models->value())) {
    if (!object(models)) {
      aggregate->malformed = true;
      return;
    }
    for (const auto &[model, entry] : models->object()) {
      auto alternate = std::find_if(scopes.begin(), scopes.end(), [&](const auto &old) {
        return old.scope == UsageScope::model_alternative && old.actual_model == model;
      });
      if (alternate == scopes.end()) {
        scopes.emplace_back();
        alternate = std::prev(scopes.end());
        alternate->scope = UsageScope::model_alternative;
        alternate->actual_model = model;
      }
      parse_usage(entry, *alternate, true, true);
    }
  }
}
class Scan {
public:
  Scan(TokenUsageReport &report, TokenUsageLimits limits, std::string run)
      : report_(report), limits_(limits), run_(std::move(run)) {}
  void issue(std::string reason, std::optional<UsageSource> source = {},
             std::optional<Error> error = {}) {
    report_.issues.push_back({run_, std::move(reason), std::move(source), error});
  }
  void record(const RawRunRead &read, RawRunCursor before) {
    Fields input{read.raw};
    if (input.get<std::uint32_t>() != read.raw.size())
      throw Error{ErrorCode::corrupt};
    const auto site = input.get<std::uint32_t>();
    const Key own{read.origin.lane, input.get<std::uint64_t>()};
    UsageSource source{run_, before, reference(own), 0, read.raw.size()};
    const auto define = [&]() -> Node & {
      auto &node = nodes_[own];
      if (node.site)
        throw Error{ErrorCode::conflict};
      node.site = site;
      node.source = source;
      return node;
    };
    if (site == 1320) {
      auto &node = define();
      node.kind = input.get<std::uint8_t>();
      if (input.boolean())
        node.parent = input.key();
      if (input.boolean()) {
        node.attempt = input.get<IdentityBytes>();
        node.invocation = input.get<IdentityBytes>();
        node.generation = input.get<IdentityBytes>();
      }
      input.end();
    } else if (site == 1300) {
      auto &node = define();
      if (input.boolean()) {
        node.attempt = input.get<IdentityBytes>();
        node.invocation = input.get<IdentityBytes>();
        node.generation = input.get<IdentityBytes>();
        (void)input.key();
      }
      (void)input.get<int>(); // request timeout
      if (!input.remaining().empty())
        (void)input.get<int>(); // stall timeout; absent from runs before 2026-10-18
      input.end();
    } else if (site == 3600 || site == 1400 || site == 1310) {
      auto &node = define();
      node.parent = input.key();
      if (site == 3600) {
        (void)input.get<std::size_t>();
        (void)input.get<std::size_t>();
      } else if (site == 1310)
        (void)input.get<std::size_t>();
      else
        for (unsigned i = 0; i < 4; ++i)
          (void)input.boolean();
      input.end();
    } else if (site == 2400) {
      auto &node = define();
      if (input.boolean())
        node.parent = input.key();
      node.generation = input.get<IdentityBytes>();
    } else if (site == 2403) {
      auto &node = nodes_[input.key()];
      const auto kind = input.get<std::uint8_t>();
      const auto id = input.get<IdentityBytes>();
      if (kind == 1)
        node.invocation = id;
      else if (kind == 2)
        node.attempt = id;
      else if (kind == 3)
        node.generation = id;
      input.end();
    } else if (site == 1600) {
      (void)input.get<std::uint8_t>();
      if (input.boolean())
        (void)input.key();
      const auto expected = input.get<std::uint8_t>();
      if (expected == 1) {
        (void)input.get<std::int64_t>();
        (void)input.get<std::int64_t>();
      } else if (expected > 2)
        throw Error{ErrorCode::corrupt};
      input.end();
      auth_[own].began = true;
    } else if (site == 1601 || site == 1602) {
      const auto action = input.key();
      auto &auth = auth_[action];
      if (site == 1601) {
        const auto kind = input.get<std::uint8_t>();
        if (kind == 0) {
          auth.provider = input.text();
          auth.account = input.text();
        } else if (kind == 1)
          auth.account = input.text();
        else
          throw Error{ErrorCode::unsupported};
      } else {
        auth.provider = input.text();
        auth.account = input.text();
        for (unsigned i = 0; i < 3; ++i)
          (void)input.text();
      }
      input.end();
      auth.source = source;
    } else if (site == 1612) {
      const auto auth = input.key();
      auth_links_.emplace_back(auth, input.key());
      input.end();
    } else if (site == 1324) {
      const auto parent = input.key();
      links_.emplace_back(parent, input.key());
      input.end();
    } else if (site == 1325) {
      const auto parent = input.key();
      nodes_[parent].group = input.get<IdentityBytes>();
      for (unsigned i = 0; i < 3; ++i)
        (void)input.get<unsigned>();
      input.end();
    } else if (site == 1321 || site == 1303 || site == 3601) {
      auto &node = nodes_[input.key()];
      node.branch = input.get<std::uint8_t>();
      node.stage = input.get<std::uint8_t>();
    } else if (site == 1302 || site == 1311) {
      const auto parent = input.key();
      if (site == 1302) {
        (void)input.key();
        nodes_[parent].http_status = input.get<int>();
        nodes_[parent].response = true;
        input.end();
      } else {
        (void)input.get<std::uint8_t>();
        if (input.boolean()) {
          nodes_[parent].http_status = input.get<int>();
          nodes_[parent].response = true;
        }
      }
    } else if (site == 1401) {
      auto &node = nodes_[input.key()];
      (void)input.get<std::uint8_t>();
      const auto result = input.get<int>();
      (void)input.get<int>();
      const auto pid = input.get<pid_t>();
      (void)input.get<pid_t>();
      input.end();
      node.started = result == 0 && pid > 0;
    } else if (site == 1301 || site == 3602) {
      const auto parent = input.key();
      Body body{parent, {}, {}, 0, true, {}};
      if (site == 1301) {
        (void)input.text();
        body.url = input.text();
        nodes_[parent].outbound = true;
      }
      body.bytes = input.text();
      input.end();
      source.body_offset = read.raw.size() - body.bytes.size();
      source.bytes = body.bytes.size();
      body.sources.push_back(std::move(source));
      save(std::move(body));
    } else if (site == 1312 || site == 1322) {
      const auto parent = input.key();
      unsigned rank = 0;
      bool request = false;
      bool native_packet = false;
      if (site == 1312) {
        const auto kind = input.get<std::uint8_t>();
        const auto requested = input.get<std::size_t>();
        if (!input.boolean()) {
          issue("unknown-used response extent is not a complete body", source);
          return;
        }
        const auto used = input.get<std::size_t>();
        if (kind != 1 || used > requested || used != input.remaining().size())
          throw Error{ErrorCode::corrupt};
        rank = 6;
      } else {
        const auto role = input.get<std::uint8_t>();
        native_packet = role >= 1 && role <= 5;
        const auto size = input.get<std::size_t>();
        if (size != input.remaining().size())
          throw Error{ErrorCode::corrupt};
        if (role == 1 || role == 2 || role == 11)
          request = true;
        else if (role == 3 || role == 4 || role == 5)
          rank = role == 4 ? 5 : role == 3 ? 4 : 2;
        else
          return;
      }
      const auto bytes = input.remaining();
      source.body_offset = read.raw.size() - bytes.size();
      source.bytes = bytes.size();
      save({parent,
            {reinterpret_cast<const char *>(bytes.data()), bytes.size()},
            {std::move(source)},
            rank,
            request,
            {},
            native_packet});
    } else if (site == 1404) {
      const auto child = input.key();
      const auto channel = input.get<std::uint8_t>();
      const auto requested = input.get<std::size_t>();
      const auto result = input.get<ssize_t>();
      (void)input.get<int>();
      if (result <= 0 || channel != 1)
        return;
      const auto length = static_cast<std::size_t>(result);
      if (length > requested || length != input.remaining().size())
        throw Error{ErrorCode::corrupt};
      const auto node = nodes_.find(child);
      // Data and control partitions are recorded independently. A child's
      // stdout may precede its begin record; classify it after scanning links.
      if (node != nodes_.end()) {
        const auto parent_key = node->second.parent;
        if (parent_key) {
          const auto parent = nodes_.find(*parent_key);
          if (parent != nodes_.end() && parent->second.site != 1300 &&
              parent->second.site != 3600 &&
              !(parent->second.site == 1320 && parent->second.kind == 12))
            return;
        }
      }
      credit(length);
      auto &body = streams_[child];
      body.owner = child;
      body.rank = 3;
      if (body.bytes.size() > limits_.max_body_bytes - length)
        throw Error{ErrorCode::capacity};
      body.bytes.append(reinterpret_cast<const char *>(input.remaining().data()),
                        length);
      source.body_offset = read.raw.size() - length;
      source.bytes = length;
      body.sources.push_back(std::move(source));
    }
  }
  void finish() {
    for (const auto &[parent, child] : links_) {
      auto at = nodes_.find(child);
      if (at != nodes_.end() && at->second.site == 1300) {
        if (at->second.parent && *at->second.parent != parent)
          issue("conflicting HTTP source parents", at->second.source);
        else
          at->second.parent = parent;
      }
    }
    for (const auto &body : bodies_) {
      if (body.rank != 2 || body.request || root(body.owner))
        continue;
      auto wrapper = decode_packet_string(body.bytes);
      if (wrapper.has_value() &&
          string(wrapper.value().find("remote_disposition")) == "not_dispatched") {
        try {
          unique(wrapper.value());
          refusal_roots_.insert(body.owner);
        } catch (const Error &error) {
          issue("ambiguous local-refusal wrapper", body.sources.front(), error);
        }
      }
    }
    std::map<Key, std::size_t> indices;
    for (const auto &[key, node] : nodes_) {
      const auto resolved = root(key);
      if (!resolved || *resolved != key)
        continue;
      if (report_.observations.size() >= limits_.max_observations) {
        issue("observation limit reached");
        break;
      }
      UsageObservation observation;
      observation.request = reference(key);
      observation.actor = node.site == 3600 ? UsageActor::decision
                          : node.kind == 3  ? UsageActor::main
                          : node.kind == 12 || refusal_roots_.contains(key)
                              ? UsageActor::colleague
                              : UsageActor::unknown;
      if (observation.actor == UsageActor::decision)
        observation.provider = "jev";
      observation.attempt = node.attempt;
      observation.invocation = node.invocation;
      observation.generation = node.generation;
      observation.terminal_branch = node.branch;
      observation.terminal_stage = node.stage;
      const auto source = node.source;
      if (source)
        observation.request_sources.push_back(*source);
      auto ancestor = node.parent;
      std::size_t depth = 0;
      while (ancestor && depth++ < limits_.max_causal_depth) {
        const auto found = nodes_.find(*ancestor);
        if (found == nodes_.end() || !found->second.site) {
          issue("missing causal parent", node.source);
          break;
        }
        if (!observation.retry_group)
          observation.retry_group = found->second.group;
        if (!observation.attempt)
          observation.attempt = found->second.attempt;
        if (!observation.invocation)
          observation.invocation = found->second.invocation;
        if (!observation.generation)
          observation.generation = found->second.generation;
        ancestor = found->second.parent;
      }
      indices.emplace(key, report_.observations.size());
      report_.observations.push_back(std::move(observation));
    }
    for (const auto &[key, node] : nodes_) {
      const auto resolved = root(key);
      const auto found = resolved ? indices.find(*resolved) : indices.end();
      if (found == indices.end())
        continue;
      auto &observation = report_.observations[found->second];
      if (observation.attempt && node.attempt && observation.attempt != node.attempt) {
        observation.conflicting = true;
        issue("conflicting retained attempt identities", node.source);
      }
      if (!observation.attempt)
        observation.attempt = node.attempt;
      if (!observation.invocation)
        observation.invocation = node.invocation;
      if (!observation.generation)
        observation.generation = node.generation;
      if (node.http_status) {
        if (observation.http_status && observation.http_status != node.http_status)
          observation.conflicting = true;
        observation.http_status = node.http_status;
      }
      if (node.response)
        observation.dispatch = UsageDispatch::response_observed;
      else if (node.started && observation.dispatch != UsageDispatch::response_observed)
        observation.dispatch = UsageDispatch::child_started;
      else if (node.outbound && observation.dispatch == UsageDispatch::unknown)
        observation.dispatch = UsageDispatch::outbound_captured;
    }
    for (const auto &[auth_key, application] : auth_links_) {
      const auto resolved = root(application);
      const auto index = resolved ? indices.find(*resolved) : indices.end();
      const auto auth = auth_.find(auth_key);
      if (index == indices.end() || auth == auth_.end() || !auth->second.began)
        continue;
      auto &observation = report_.observations[index->second];
      if (!observation.account.empty() && observation.account != auth->second.account)
        observation.conflicting = true;
      else
        observation.account = auth->second.account;
      if (!auth->second.provider.empty())
        observation.provider = auth->second.provider;
      const auto source = auth->second.source;
      if (source)
        observation.request_sources.push_back(*source);
    }
    for (auto &[unused, stream] : streams_) {
      (void)unused;
      const auto child = nodes_.find(stream.owner);
      if (child == nodes_.end())
        continue;
      const auto parent_key = child->second.parent;
      if (!parent_key)
        continue;
      const auto parent = nodes_.find(*parent_key);
      if (parent == nodes_.end() ||
          (parent->second.site != 1300 && parent->second.site != 3600 &&
           !(parent->second.site == 1320 && parent->second.kind == 12)))
        continue;
      bodies_.push_back(std::move(stream));
    }
    std::stable_sort(
        bodies_.begin(), bodies_.end(), [](const auto &left, const auto &right) {
          return left.request != right.request ? left.request : left.rank > right.rank;
        });
    std::map<std::pair<Key, std::string>, std::size_t> alternatives;
    for (const auto &body : bodies_) {
      const auto resolved = root(body.owner);
      const auto index = resolved ? indices.find(*resolved) : indices.end();
      if (index == indices.end()) {
        if (body.request) {
          UsageObservation unlinked;
          request_metadata(unlinked, body);
        }
        issue("unlinked or over-depth original excluded from totals",
              body.sources.front());
        continue;
      }
      if (body.request) {
        request_metadata(report_.observations[index->second], body);
        continue;
      }
      for (const auto &candidate : response(body)) {
        auto position = index->second;
        if (candidate.scope == UsageScope::model_alternative) {
          const auto key = std::pair{*resolved, candidate.actual_model};
          auto found = alternatives.find(key);
          if (found == alternatives.end()) {
            if (report_.observations.size() >= limits_.max_observations) {
              issue("observation limit reached", body.sources.front());
              continue;
            }
            auto next = report_.observations[index->second];
            next.scope = UsageScope::model_alternative;
            next.actual_model = candidate.actual_model;
            next.counters = {};
            next.reported_fields.clear();
            next.observed_cost_usd.reset();
            next.reported_cost.reset();
            next.reported_cost_explicit_usd = next.malformed_cost =
                next.conflicting_cost = false;
            next.sources.clear();
            next.usage_present = next.malformed = next.conflicting = false;
            next.partial_usage = false;
            position = report_.observations.size();
            report_.observations.push_back(std::move(next));
            found = alternatives.emplace(key, position).first;
          }
          position = found->second;
        }
        merge(report_.observations[position], candidate, body.sources);
      }
    }
  }

private:
  TokenUsageReport &report_;
  TokenUsageLimits limits_;
  std::string run_;
  std::map<Key, Node> nodes_;
  std::vector<std::pair<Key, Key>> links_;
  std::vector<Body> bodies_;
  std::map<Key, Body> streams_;
  struct Auth {
    bool began = false;
    std::string provider, account;
    std::optional<UsageSource> source;
  };
  std::map<Key, Auth> auth_;
  std::vector<std::pair<Key, Key>> auth_links_;
  std::set<Key> refusal_roots_;
  std::size_t retained_ = 0;
  void credit(std::size_t amount) {
    if (amount > limits_.max_body_bytes ||
        amount > limits_.max_retained_bytes - retained_)
      throw Error{ErrorCode::capacity};
    retained_ += amount;
  }
  void save(Body body) {
    credit(body.bytes.size());
    bodies_.push_back(std::move(body));
  }
  std::optional<Key> root(Key key) const {
    std::optional<Key> colleague, transport;
    std::size_t depth = 0;
    for (;;) {
      const auto found = nodes_.find(key);
      if (found == nodes_.end() || !found->second.site)
        return colleague ? colleague : transport;
      if (++depth > limits_.max_causal_depth)
        return {};
      const auto &node = found->second;
      if (refusal_roots_.contains(key))
        return key;
      if (node.site == 3600 || (node.site == 1320 && node.kind == 3))
        return key;
      if (node.site == 1320 && node.kind == 12)
        colleague = key;
      if (node.site == 1300)
        transport = key;
      if (!node.parent)
        return colleague ? colleague : transport;
      key = *node.parent;
    }
  }
  void request_metadata(UsageObservation &observation, const Body &body) {
    if (!body.url.empty()) {
      observation.route_url = body.url;
      if (!observation.provider.empty()) {
        // An acquired auth binding is stronger than URL recognition.
      } else if (body.url.starts_with("https://openrouter.ai/"))
        observation.provider = "openrouter";
      else if (body.url.starts_with("https://api.openai.com/") ||
               body.url.starts_with("https://chatgpt.com/"))
        observation.provider = "openai";
      else if (body.url.starts_with("https://api.anthropic.com/"))
        observation.provider = "anthropic";
    }
    auto parsed =
        body.native_packet ? decode_packet_string(body.bytes) : parse_json(body.bytes);
    if (!parsed.has_value()) {
      issue("malformed request original", body.sources.front(), parsed.error());
      return;
    }
    try {
      unique(parsed.value());
      const Value *request = &parsed.value();
      if (object(request->find("request")))
        request = request->find("request");
      auto model = string(request->find("model"));
      if (!model.empty())
        observation.requested_model = std::move(model);
      auto provider = string(request->find("provider"));
      if (!provider.empty() && observation.provider.empty())
        observation.provider = std::move(provider);
      observation.request_sources.insert(observation.request_sources.end(),
                                         body.sources.begin(), body.sources.end());
    } catch (const Error &error) {
      issue("ambiguous request original", body.sources.front(), error);
    }
  }
  std::vector<UsageObservation> response(const Body &body) {
    std::vector<UsageObservation> result;
    if (body.native_packet) {
      auto packet =
          decode_packet_string(body.bytes, {limits_.max_body_bytes, 500000, 128});
      if (!packet.has_value()) {
        issue("malformed native provider packet", body.sources.front(), packet.error());
        return result;
      }
      try {
        unique(packet.value());
        parse_response(packet.value(), result);
      } catch (const Error &error) {
        issue("ambiguous native provider packet", body.sources.front(), error);
        for (auto &scope : result)
          scope.malformed = true;
      }
      return result;
    }
    bool terminal_usage = false;
    bool terminal_seen = false;
    std::string_view bytes{body.bytes};
    const auto newline = bytes.rfind('\n');
    if (newline != std::string_view::npos && bytes.size() - newline == 4 &&
        std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(newline + 1),
                    bytes.end(), [](char c) { return c >= '0' && c <= '9'; }))
      bytes = bytes.substr(0, newline);
    const auto consume = [&](std::string_view data) {
      if (data == "[DONE]" || data.empty())
        return;
      auto parsed = parse_json(data, {limits_.max_body_bytes, 500000, 128});
      if (!parsed.has_value()) {
        issue("malformed response original/event", body.sources.front(),
              parsed.error());
        return;
      }
      try {
        unique(parsed.value());
        const auto type = string(parsed.value().find("type"));
        if (type.starts_with("response.") && type != "response.completed" &&
            type != "response.failed" && type != "response.incomplete" &&
            type != "response.done")
          return;
        const auto *delta = parsed.value().find("delta");
        const auto *stop = field(delta, "stop_reason");
        const bool terminal =
            type == "response.completed" || type == "response.failed" ||
            type == "response.incomplete" || type == "response.done" ||
            type == "message_stop" ||
            (type == "message_delta" && stop && *stop != Value{}) ||
            (parsed.value().find("choices") && object(parsed.value().find("usage")));
        std::vector<UsageObservation> frame;
        parse_response(parsed.value(), frame);
        bool complete = false;
        for (const auto &scope : frame)
          if (scope.scope == UsageScope::aggregate && scope.usage_present &&
              !scope.malformed && scope.counters.output) {
            if (scope.counters.input)
              complete = true;
            else if (type == "message_delta" && stop && *stop != Value{})
              complete = std::any_of(result.begin(), result.end(), [](const auto &old) {
                return old.scope == UsageScope::aggregate &&
                       old.counters.input.has_value();
              });
          }
        if (terminal_seen) {
          for (const auto &candidate : frame) {
            auto target =
                std::find_if(result.begin(), result.end(), [&](const auto &old) {
                  return old.scope == candidate.scope &&
                         (old.scope == UsageScope::aggregate ||
                          old.actual_model == candidate.actual_model);
                });
            if (target == result.end())
              result.push_back(candidate);
            else {
              if ((!terminal && candidate.usage_present) ||
                  (terminal && !target->response_status.empty() &&
                   !candidate.response_status.empty() &&
                   target->response_status != candidate.response_status))
                target->conflicting = true;
              merge(*target, candidate, {});
            }
          }
        } else
          parse_response(parsed.value(), result);
        terminal_seen = terminal_seen || terminal;
        terminal_usage = terminal_usage || (terminal && complete);
      } catch (const Error &error) {
        issue("ambiguous response original/event", body.sources.front(), error);
        for (auto &scope : result)
          scope.malformed = true;
      }
    };
    if (bytes.starts_with("data:") || bytes.starts_with("event:") ||
        bytes.starts_with(":") || bytes.find("\ndata:") != std::string_view::npos) {
      std::string event;
      while (!bytes.empty()) {
        const auto end = bytes.find('\n');
        auto line = bytes.substr(0, end);
        bytes =
            end == std::string_view::npos ? std::string_view{} : bytes.substr(end + 1);
        if (line.ends_with('\r'))
          line.remove_suffix(1);
        if (line.empty()) {
          consume(event);
          event.clear();
        } else if (line.starts_with("data:")) {
          line.remove_prefix(5);
          if (line.starts_with(' '))
            line.remove_prefix(1);
          if (!event.empty())
            event += '\n';
          event += line;
        }
      }
      consume(event);
      if (!terminal_usage && !result.empty()) {
        issue("stream has partial usage without a terminal usage scope",
              body.sources.front());
        for (auto &scope : result)
          scope.partial_usage = true;
      }
    } else
      consume(bytes);
    return result;
  }
  static void merge(UsageObservation &target, const UsageObservation &candidate,
                    const std::vector<UsageSource> &sources) {
    const auto text = [](std::string &old, const std::string &next) {
      if (old.empty())
        old = next;
    };
    if ((!target.response_id.empty() && !candidate.response_id.empty() &&
         target.response_id != candidate.response_id) ||
        (!target.actual_model.empty() && !candidate.actual_model.empty() &&
         target.actual_model != candidate.actual_model))
      target.conflicting = true;
    text(target.actual_model, candidate.actual_model);
    text(target.requested_model, candidate.requested_model);
    text(target.response_status, candidate.response_status);
    text(target.response_id, candidate.response_id);
    if (target.service_tier_reported && candidate.service_tier_reported &&
        target.service_tier != candidate.service_tier)
      target.pricing_conflict = true;
    if (!target.service_tier_reported && candidate.service_tier_reported) {
      target.service_tier = candidate.service_tier;
      target.service_tier_reported = true;
    }
    target.pricing_conflict = target.pricing_conflict || candidate.pricing_conflict;
    text(target.reported_provider, candidate.reported_provider);
    if (target.provider.empty())
      target.provider = candidate.reported_provider;
    if (candidate.dispatch == UsageDispatch::locally_refused &&
        target.dispatch == UsageDispatch::unknown)
      target.dispatch = candidate.dispatch;
    const auto count = [&](auto &old, const auto &next) {
      if (old && next && old != next)
        target.conflicting = true;
      if (!old)
        old = next;
    };
    if (candidate.usage_present) {
      if (target.usage_present && target.counters.input_includes_cache !=
                                      candidate.counters.input_includes_cache)
        target.conflicting = true;
      target.counters.input_includes_cache = candidate.counters.input_includes_cache;
      count(target.counters.input, candidate.counters.input);
      count(target.counters.output, candidate.counters.output);
      count(target.counters.cache_read, candidate.counters.cache_read);
      count(target.counters.cache_write, candidate.counters.cache_write);
      count(target.counters.reasoning, candidate.counters.reasoning);
    }
    if (target.reported_cost && candidate.reported_cost &&
        *target.reported_cost != *candidate.reported_cost)
      target.conflicting_cost = true;
    if (!target.reported_cost)
      target.reported_cost = candidate.reported_cost;
    target.reported_cost_explicit_usd =
        target.reported_cost_explicit_usd || candidate.reported_cost_explicit_usd;
    target.malformed_cost = target.malformed_cost || candidate.malformed_cost;
    target.conflicting_cost = target.conflicting_cost || candidate.conflicting_cost;
    if ((target.route_url.starts_with("https://openrouter.ai/") ||
         target.reported_cost_explicit_usd) &&
        !target.malformed_cost && !target.conflicting_cost)
      target.observed_cost_usd = target.reported_cost;
    else
      target.observed_cost_usd.reset();
    target.usage_present = target.usage_present || candidate.usage_present;
    target.conflicting = target.conflicting || candidate.conflicting;
    target.partial_usage = target.partial_usage || candidate.partial_usage;
    target.malformed = target.malformed || candidate.malformed;
    merge_fields(target.reported_fields, candidate.reported_fields);
    target.sources.insert(target.sources.end(), sources.begin(), sources.end());
  }
};
bool abi(ByteView bytes) {
  if (std::endian::native != std::endian::little || bytes.size() < 80 ||
      std::memcmp(bytes.data(), "BBRAWABI", 8) != 0)
    return false;
  Fields fields{bytes.subspan(8)};
  const std::array<std::uint32_t, 18> expected{2,
                                               sizeof(std::uint32_t),
                                               sizeof(std::uint64_t),
                                               sizeof(std::size_t),
                                               sizeof(clockid_t),
                                               sizeof(int),
                                               sizeof(time_t),
                                               sizeof(long),
                                               sizeof(bool),
                                               sizeof(ErrorCode),
                                               sizeof(RawRegionOutcome),
                                               sizeof(RawObservationKind),
                                               sizeof(RawRegionEnd),
                                               sizeof(RawPartition),
                                               sizeof(ssize_t),
                                               sizeof(pid_t),
                                               sizeof(uid_t),
                                               sizeof(short)};
  return std::all_of(expected.begin(), expected.end(),
                     [&](auto width) { return fields.get<std::uint32_t>() == width; });
}
std::string hexadecimal(ByteView bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (const auto byte : bytes) {
    const auto value = std::to_integer<unsigned>(byte);
    result += digits[value >> 4];
    result += digits[value & 15U];
  }
  return result;
}
const char *actor_name(UsageActor actor) {
  switch (actor) {
  case UsageActor::main:
    return "main";
  case UsageActor::colleague:
    return "colleague";
  case UsageActor::decision:
    return "decision";
  case UsageActor::unknown:
    return "unknown";
  }
  return "unknown";
}
void sum(std::optional<std::uint64_t> &target, std::optional<std::uint64_t> value,
         UsageSubtotal &subtotal) {
  if (!value)
    return;
  const auto old = target.value_or(0);
  if (*value > UINT64_MAX - old) {
    subtotal.overflow = true;
    return;
  }
  target = old + *value;
}
} // namespace

Result<TokenUsageReport> read_token_usage(std::string_view session,
                                          TokenUsageLimits limits) {
  try {
    if (!limits.max_runs || !limits.max_records || !limits.max_body_bytes ||
        limits.max_body_bytes > UINT32_MAX - 4096 || !limits.max_scan_bytes ||
        !limits.max_retained_bytes || !limits.max_observations ||
        !limits.max_causal_depth)
      throw Error{ErrorCode::invalid_range};
    TokenUsageReport report;
    auto session_directory = need(NativeJournalDirectory::open(session));
    auto child = session_directory.open_child_directory("raw");
    if (!child.has_value()) {
      report.issues.push_back(
          {std::string{session}, "raw coverage unavailable", {}, child.error()});
      return Result<TokenUsageReport>::success(std::move(report));
    }
    std::unique_ptr<DIR, DirectoryCloser> directory{::fdopendir(child.value())};
    if (!directory) {
      const auto error = errno;
      (void)::close(child.value());
      throw Error{ErrorCode::io, error};
    }
    std::vector<std::string> names;
    std::size_t entries = 0;
    for (;;) {
      errno = 0;
      const auto *entry = ::readdir(directory.get());
      if (!entry) {
        if (errno)
          throw Error{ErrorCode::io, errno};
        break;
      }
      if (entry->d_name[0] == '.')
        continue;
      if (entries++ >= limits.max_runs) {
        report.issues.push_back(
            {std::string{session}, "raw run discovery limit reached", {}, {}});
        break;
      }
      if (std::string_view{entry->d_name}.starts_with("bb"))
        names.emplace_back(entry->d_name);
    }
    std::sort(names.begin(), names.end());
    std::size_t records = 0, bytes = 0;
    for (const auto &name : names) {
      const auto path = std::string{session} + "/raw/" + name;
      auto native = NativeJournalDirectory::open_at(::dirfd(directory.get()), name);
      if (!native.has_value()) {
        report.issues.push_back({path, "raw run unavailable", {}, native.error()});
        continue;
      }
      auto opened = RawRunReader::open(std::move(native).value(), {}, limits.raw);
      if (!opened.has_value()) {
        report.issues.push_back(
            {path, "raw run prefix unavailable", {}, opened.error()});
        continue;
      }
      auto reader = std::move(opened).value();
      UsageRunCoverage coverage{path, reader->cursor(),          0,
                                0,    reader->snapshot().closed, false};
      if (!abi(reader->snapshot().descriptions)) {
        report.issues.push_back({path, "unsupported native raw ABI", {}, {}});
        report.runs.push_back(std::move(coverage));
        continue;
      }
      std::size_t largest = 0;
      for (const auto &segment : reader->snapshot().segments)
        largest = std::max(largest,
                           static_cast<std::size_t>(segment.header.limits.max_payload));
      std::vector<std::byte> output(std::min(largest, limits.max_body_bytes + 4096));
      Scan scan{report, limits, path};
      for (;;) {
        if (records >= limits.max_records || bytes >= limits.max_scan_bytes) {
          scan.issue("scan record/byte limit reached");
          break;
        }
        const auto before = reader->cursor();
        auto next = reader->read(output);
        if (!next.has_value()) {
          scan.issue("raw record unavailable", {}, next.error());
          break;
        }
        const auto &optional_record = next.value();
        if (!optional_record) {
          coverage.reached_preserved_end = true;
          break;
        }
        const auto &record = *optional_record;
        if (record.raw.size() > limits.max_scan_bytes - bytes) {
          scan.issue("scan byte limit reached");
          break;
        }
        ++records;
        ++coverage.records;
        bytes += record.raw.size();
        coverage.bytes += record.raw.size();
        coverage.scanned_through = record.next;
        try {
          scan.record(record, before);
        } catch (const Error &error) {
          scan.issue("raw source record unsupported/corrupt or body limit reached", {},
                     error);
          if (error.code == ErrorCode::capacity)
            break;
        }
      }
      scan.finish();
      report.runs.push_back(std::move(coverage));
    }
    if (names.empty())
      report.issues.push_back({std::string{session}, "no preserved raw runs", {}, {}});
    return Result<TokenUsageReport>::success(std::move(report));
  } catch (const Error &error) {
    return Result<TokenUsageReport>::failure(error);
  } catch (const std::bad_alloc &) {
    return Result<TokenUsageReport>::failure({ErrorCode::allocation});
  } catch (const std::length_error &) {
    return Result<TokenUsageReport>::failure({ErrorCode::capacity});
  }
}
std::vector<UsageSubtotal> token_usage_subtotals(const TokenUsageReport &report) {
  std::vector<UsageSubtotal> result;
  for (const auto &observation : report.observations) {
    if (observation.scope != UsageScope::aggregate)
      continue;
    auto found = std::find_if(result.begin(), result.end(), [&](const auto &subtotal) {
      return subtotal.actor == observation.actor &&
             subtotal.provider == observation.provider &&
             subtotal.known.input_includes_cache ==
                 observation.counters.input_includes_cache;
    });
    if (found == result.end()) {
      result.push_back({observation.actor, observation.provider, {}});
      found = std::prev(result.end());
      found->known.input_includes_cache = observation.counters.input_includes_cache;
    }
    ++found->observations;
    if (observation.dispatch == UsageDispatch::locally_refused)
      ++found->locally_refused;
    if (observation.partial_usage)
      ++found->partial_usage;
    if (!observation.usage_present)
      ++found->missing_usage;
    if (observation.malformed || observation.conflicting) {
      ++found->invalid_usage;
      continue;
    }
    const auto &counts = observation.counters;
    found->missing_input += counts.input ? 0U : 1U;
    found->missing_output += counts.output ? 0U : 1U;
    found->missing_cache_read += counts.cache_read ? 0U : 1U;
    found->missing_cache_write += counts.cache_write ? 0U : 1U;
    sum(found->known.input, counts.input, *found);
    sum(found->known.output, counts.output, *found);
    sum(found->known.cache_read, counts.cache_read, *found);
    sum(found->known.cache_write, counts.cache_write, *found);
    sum(found->known.reasoning, counts.reasoning, *found);
  }
  return result;
}
UsageEstimate estimate_token_usage(const UsageObservation &observation,
                                   const UsagePrice &price) {
  UsageEstimate result{price.date, {}, {}};
  const auto fail = [&](std::string reason) {
    result.unavailable_reason = std::move(reason);
    return result;
  };
  if (price.date.size() != 10)
    return fail("price date must be YYYY-MM-DD");
  const auto date_part = [](std::string_view part) {
    int value = 0;
    const auto begin = part.data(), end = begin + part.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end ? value : -1;
  };
  const std::string_view date{price.date};
  const auto year = date_part(date.substr(0, 4)), month = date_part(date.substr(5, 2)),
             day = date_part(date.substr(8, 2));
  if (price.date[4] != '-' || price.date[7] != '-' || year <= 0 || month <= 0 ||
      day <= 0 ||
      !std::chrono::year_month_day{std::chrono::year{year},
                                   std::chrono::month{static_cast<unsigned>(month)},
                                   std::chrono::day{static_cast<unsigned>(day)}}
           .ok())
    return fail("price date must be YYYY-MM-DD");
  if (observation.provider != price.provider || observation.actual_model.empty() ||
      observation.actual_model != price.model ||
      observation.service_tier != price.service_tier)
    return fail("price provider/model/service tier does not match observed response");
  if (observation.scope != UsageScope::aggregate || observation.malformed ||
      observation.conflicting || observation.pricing_conflict ||
      observation.partial_usage || !observation.usage_present)
    return fail("usage scope is unavailable or ambiguous");
  const auto &counts = observation.counters;
  if (!counts.input || !counts.output || !counts.cache_read || !counts.cache_write)
    return fail("input/output/cache counters are not all reported");
  if (counts.reasoning && *counts.reasoning > *counts.output)
    return fail("reasoning component exceeds total output");
  const auto read = *counts.cache_read, write = *counts.cache_write;
  auto ordinary = *counts.input;
  if (counts.input_includes_cache) {
    if (read > ordinary || write > ordinary - read)
      return fail("cache categories exceed total input");
    ordinary -= read + write;
  }
  if ((read && !price.cache_read_per_million) ||
      (write && !price.cache_write_per_million))
    return fail("cache price missing");
  for (const auto &reported : observation.reported_fields)
    if ((reported.name == "usage.cache_creation.ephemeral_5m_input_tokens" ||
         reported.name == "usage.cache_creation.ephemeral_1h_input_tokens") &&
        reported.value != Number{0})
      return fail("cache write TTL has no rate in this price input");
  const std::array rates{price.input_per_million, price.output_per_million,
                         price.cache_read_per_million.value_or(0),
                         price.cache_write_per_million.value_or(0)};
  if (!std::all_of(rates.begin(), rates.end(),
                   [](double value) { return std::isfinite(value) && value >= 0; }))
    return fail("price is negative or nonfinite");
  const auto total = static_cast<double>(ordinary) * rates[0] +
                     static_cast<double>(*counts.output) * rates[1] +
                     static_cast<double>(read) * rates[2] +
                     static_cast<double>(write) * rates[3];
  if (!std::isfinite(total))
    return fail("estimate overflow");
  result.usd = total / 1000000;
  return result;
}
std::string render_token_usage(const TokenUsageReport &report,
                               std::span<const UsagePrice> prices) {
  std::ostringstream out;
  out << "coverage: raw-prefix-only; complete_session_ledger=false\n";
  for (const auto &run : report.runs) {
    out << "run " << run.run_directory << " records=" << run.records
        << " bytes=" << run.bytes << " closed=" << run.closed
        << " preserved_end=" << run.reached_preserved_end;
    if (run.scanned_through)
      out << " segment=" << run.scanned_through->segment
          << " offset=" << run.scanned_through->audit.offset;
    out << '\n';
  }
  const auto number = [](std::optional<std::uint64_t> value) {
    return value ? std::to_string(*value) : std::string{"unknown"};
  };
  for (const auto &subtotal : token_usage_subtotals(report))
    out << "known_subtotal actor=" << actor_name(subtotal.actor)
        << " provider=" << (subtotal.provider.empty() ? "unknown" : subtotal.provider)
        << " input=" << number(subtotal.known.input)
        << " output=" << number(subtotal.known.output)
        << " cache_read=" << number(subtotal.known.cache_read)
        << " cache_write=" << number(subtotal.known.cache_write)
        << " reasoning_component=" << number(subtotal.known.reasoning)
        << " observations=" << subtotal.observations
        << " missing_usage=" << subtotal.missing_usage
        << " invalid_usage=" << subtotal.invalid_usage
        << " locally_refused=" << subtotal.locally_refused
        << " partial_usage=" << subtotal.partial_usage
        << " missing_input=" << subtotal.missing_input
        << " missing_output=" << subtotal.missing_output
        << " missing_cache_read=" << subtotal.missing_cache_read
        << " missing_cache_write=" << subtotal.missing_cache_write
        << " input_includes_cache=" << subtotal.known.input_includes_cache
        << " overflow=" << subtotal.overflow << '\n';
  for (const auto &entry : report.observations) {
    out << "observation actor=" << actor_name(entry.actor) << " scope="
        << (entry.scope == UsageScope::aggregate ? "aggregate" : "model_alternative")
        << " provider=" << (entry.provider.empty() ? "unknown" : entry.provider)
        << " model=" << (entry.actual_model.empty() ? "unknown" : entry.actual_model)
        << " account=" << (entry.account.empty() ? "unknown" : entry.account)
        << " request=" << hexadecimal(entry.request.lane) << ':'
        << entry.request.ordinal
        << " attempt=" << (entry.attempt ? hexadecimal(*entry.attempt) : "unknown")
        << " status=" << entry.response_status
        << " terminal_branch=" << number(entry.terminal_branch)
        << " terminal_stage=" << number(entry.terminal_stage)
        << " dispatch_stage=" << static_cast<unsigned>(entry.dispatch)
        << " usage=" << entry.usage_present << " malformed=" << entry.malformed
        << " partial_usage=" << entry.partial_usage
        << " conflicting=" << entry.conflicting;
    if (entry.http_status)
      out << " http_status=" << *entry.http_status;
    out << " pricing_conflict=" << entry.pricing_conflict
        << " malformed_cost=" << entry.malformed_cost
        << " conflicting_cost=" << entry.conflicting_cost;
    if (entry.reported_cost)
      out << " reported_cost=" << entry.reported_cost->text();
    if (entry.observed_cost_usd)
      out << " observed_cost_usd=" << entry.observed_cost_usd->text();
    out << '\n';
    for (const auto &item : entry.reported_fields)
      out << "  reported " << item.name << '=' << item.value.text() << '\n';
    const auto render_source = [&](std::string_view kind, const UsageSource &source) {
      out << "  " << kind << ' ' << source.run_directory
          << " segment=" << source.cursor.segment
          << " offset=" << source.cursor.audit.offset
          << " event=" << hexadecimal(source.event.lane) << ':' << source.event.ordinal
          << " body_offset=" << source.body_offset << " bytes=" << source.bytes << '\n';
    };
    for (const auto &source : entry.sources)
      render_source("source", source);
    for (const auto &source : entry.request_sources)
      render_source("request_source", source);
    for (const auto &price : prices) {
      const auto estimate = estimate_token_usage(entry, price);
      out << "  list_price_estimate date=" << estimate.date << " usd=";
      if (estimate.usd)
        out << *estimate.usd;
      else
        out << "unknown reason=" << estimate.unavailable_reason;
      out << '\n';
    }
  }
  for (const auto &issue : report.issues) {
    out << "coverage_issue " << issue.run_directory << ' ' << issue.reason;
    if (issue.error)
      out << " error=" << error_name(issue.error->code) << ':' << issue.error->detail;
    if (issue.source)
      out << " event=" << hexadecimal(issue.source->event.lane) << ':'
          << issue.source->event.ordinal << " segment=" << issue.source->cursor.segment
          << " offset=" << issue.source->cursor.audit.offset;
    out << '\n';
  }
  return out.str();
}
} // namespace blackbird
