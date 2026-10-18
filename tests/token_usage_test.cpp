#include "blackbird/json.hpp"
#include "blackbird/provider_auth.hpp"
#include "blackbird/provider_original.hpp"
#include "blackbird/token_usage.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <unistd.h>

namespace {
using namespace blackbird;
void check(bool value, int line) {
  if (!value) {
    std::fprintf(stderr, "token_usage:%d\n", line);
    std::abort();
  }
}
#define CHECK(...) check((__VA_ARGS__), __LINE__)
template <class T> T need(Result<T> result) {
  CHECK(result.has_value());
  return std::move(result).value();
}
void need(Result<void> result) { CHECK(result.has_value()); }
IdentityBytes identity(unsigned value) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{static_cast<unsigned char>(value)};
  return bytes;
}
template <class T> void append(std::vector<std::byte> &out, const T &value) {
  const auto bytes = std::as_bytes(std::span{&value, 1});
  out.insert(out.end(), bytes.begin(), bytes.end());
}
void text(std::vector<std::byte> &out, std::string_view value) {
  append(out, value.size());
  const auto bytes = std::as_bytes(std::span{value});
  out.insert(out.end(), bytes.begin(), bytes.end());
}
void ref(std::vector<std::byte> &out, std::uint64_t ordinal) {
  append(out, identity(3));
  append(out, ordinal);
}
// Independent minimal BBM2 fixture writer: literal tags and varints, no production
// packet encoder or interned-word table. Expected counters remain literal asserts.
void binary_integer(std::string &out, std::uint64_t value) {
  do {
    const auto low = static_cast<unsigned char>(value & 127U);
    value >>= 7;
    out += static_cast<char>(value ? low | 128U : low);
  } while (value);
}
void binary_text(std::string &out, std::string_view value) {
  out += char{6};
  binary_integer(out, value.size());
  out += value;
}
void binary_value(std::string &out, const Value &value) {
  std::visit(
      [&](const auto &native) {
        using T = std::decay_t<decltype(native)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>)
          out += char{0};
        else if constexpr (std::is_same_v<T, bool>)
          out += native ? char{2} : char{1};
        else if constexpr (std::is_same_v<T, std::string>)
          binary_text(out, native);
        else if constexpr (std::is_same_v<T, Value::Array>) {
          out += char{9};
          binary_integer(out, native.size());
          for (const auto &entry : native)
            binary_value(out, entry);
        } else if constexpr (std::is_same_v<T, Value::Object>) {
          out += char{10};
          binary_integer(out, native.size());
          for (const auto &[key, entry] : native) {
            binary_text(out, key);
            binary_value(out, entry);
          }
        } else {
          std::visit(
              [&](const auto &number) {
                using N = std::decay_t<decltype(number)>;
                if constexpr (std::is_same_v<N, Decimal>) {
                  out += char{5};
                  out += number.negative ? char{1} : char{0};
                  const auto exponent =
                      number.exponent < 0
                          ? static_cast<std::uint64_t>(-(number.exponent + 1)) * 2 + 1
                          : static_cast<std::uint64_t>(number.exponent) * 2;
                  binary_integer(out, exponent);
                  binary_integer(out, number.limbs.size());
                  for (const auto limb : number.limbs)
                    for (unsigned shift = 0; shift < 32; shift += 8)
                      out += static_cast<char>((limb >> shift) & 255U);
                } else if constexpr (std::is_same_v<N, double>) {
                  out += char{11};
                  const auto bits = std::bit_cast<std::uint64_t>(number);
                  for (unsigned shift = 0; shift < 64; shift += 8)
                    out += static_cast<char>((bits >> shift) & 255U);
                } else if constexpr (std::is_signed_v<N>) {
                  out += number < 0 ? char{4} : char{3};
                  binary_integer(
                      out, number < 0 ? static_cast<std::uint64_t>(-(number + 1)) + 1
                                      : static_cast<std::uint64_t>(number));
                } else {
                  out += char{3};
                  binary_integer(out, number);
                }
              },
              native.storage());
        }
      },
      value.value());
}
std::string packet_fixture(std::string_view literal) {
  std::string bytes{"BBM\2", 4};
  binary_value(bytes, need(parse_json(literal)));
  return bytes;
}
void write(const std::filesystem::path &path, ByteView bytes) {
  std::ofstream file{path, std::ios::binary | std::ios::trunc};
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  CHECK(file.good());
  file.close();
  std::filesystem::permissions(path, std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_write);
}
std::map<std::string, std::string> files(const std::filesystem::path &path) {
  std::map<std::string, std::string> result;
  for (const auto &entry : std::filesystem::recursive_directory_iterator{path}) {
    if (!entry.is_regular_file())
      continue;
    std::ifstream input{entry.path(), std::ios::binary};
    result.emplace(entry.path().string(),
                   std::string{std::istreambuf_iterator<char>{input}, {}});
  }
  return result;
}
struct Fixture {
  struct Phase {
    std::uint8_t kind;
    std::uint64_t parent = 0;
    unsigned attempt = 0;
  };
  struct Packet {
    std::uint64_t phase;
    std::uint8_t role;
    std::string_view value;
  };
  struct Http {
    std::uint64_t parent;
    unsigned attempt;
  };
  std::array<char, 64> name{};
  std::filesystem::path run;
  JournalHeader header{need(EnvironmentId::from_bytes(identity(1))),
                       need(AuditStreamId::from_bytes(identity(2))),
                       1,
                       {65536, 131072},
                       std::nullopt};
  std::unique_ptr<NativeJournalDirectory> directory;
  std::unique_ptr<RawAudit> writer;
  std::uint64_t ordinal = 0;
  std::array<std::uint64_t, 3> positions{};
  bool delay_child = false;
  std::vector<std::byte> delayed_raw;
  std::optional<RawOrigin> delayed_origin;
  Fixture() {
    const std::string pattern = "/tmp/blackbird-token-usage-XXXXXX";
    std::copy(pattern.begin(), pattern.end(), name.begin());
    CHECK(::mkdtemp(name.data()) != nullptr);
    run = std::filesystem::path{name.data()} / "raw" / "bbfixture";
    std::filesystem::create_directories(run);
    std::filesystem::permissions(run.parent_path(), std::filesystem::perms::owner_all);
    std::filesystem::permissions(run, std::filesystem::perms::owner_all);
    std::vector<std::byte> descriptions;
    constexpr std::array<char, 8> magic{'B', 'B', 'R', 'A', 'W', 'A', 'B', 'I'};
    append(descriptions, magic);
    // Independent literal qualified ABI widths, not the reader's encoder.
    constexpr std::array<std::uint32_t, 18> widths{2, 4, 8, 8, 4, 4, 8, 8, 1,
                                                   1, 4, 4, 1, 4, 8, 4, 4, 2};
    append(descriptions, widths);
    write(run / "descriptions", descriptions);
    directory = std::make_unique<NativeJournalDirectory>(
        need(NativeJournalDirectory::open(run.string())));
    writer =
        need(RawAudit::open(*directory, "events.0", header, 16, SyncStrength::data));
    publish();
  }
  ~Fixture() {
    writer.reset();
    directory.reset();
    std::filesystem::remove_all(name.data());
  }
  void publish() {
    flush_delayed();
    std::vector<std::byte> bytes;
    append(bytes, std::uint64_t{0});
    append(bytes, writer->cursor().journal.bytes());
    append(bytes, writer->cursor().sequence);
    append(bytes, writer->cursor().offset);
    write(run / "published", bytes);
  }
  std::uint64_t row(std::uint32_t site, std::vector<std::byte> body,
                    RawPartition partition = RawPartition::control) {
    std::vector<std::byte> raw;
    append(raw, static_cast<std::uint32_t>(body.size() + 16));
    append(raw, site);
    append(raw, ++ordinal);
    raw.insert(raw.end(), body.begin(), body.end());
    const auto index = static_cast<std::size_t>(partition) - 1;
    const RawOrigin origin{identity(3), partition, positions[index]};
    if (delay_child && (site == 1400 || site == 1310)) {
      CHECK(!delayed_origin);
      delayed_origin = origin;
      delayed_raw = raw;
    } else {
      if (partition == RawPartition::control)
        flush_delayed();
      const std::array records{RawAuditRecord{origin, raw}};
      need(writer->append(records));
    }
    positions[index] += raw.size();
    return ordinal;
  }
  void flush_delayed() {
    if (delayed_origin) {
      const std::array records{RawAuditRecord{*delayed_origin, delayed_raw}};
      need(writer->append(records));
      delayed_origin.reset();
      delayed_raw.clear();
    }
  }
  std::uint64_t phase(Phase specification) {
    const auto [kind, parent, attempt] = specification;
    std::vector<std::byte> body;
    append(body, kind);
    append(body, parent != 0);
    if (parent)
      ref(body, parent);
    append(body, attempt != 0);
    if (attempt) {
      append(body, identity(attempt));
      append(body, identity(attempt + 40));
      append(body, identity(attempt + 80));
    }
    return row(1320, std::move(body));
  }
  void packet(Packet specification) {
    const auto [phase_id, role, value] = specification;
    std::vector<std::byte> body;
    ref(body, phase_id);
    append(body, role);
    text(body, role == 11 ? std::string{value} : packet_fixture(value));
    (void)row(1322, std::move(body), RawPartition::data);
  }
  std::uint64_t http(Http specification) {
    const auto [parent, attempt] = specification;
    std::vector<std::byte> body;
    append(body, true);
    append(body, identity(attempt));
    append(body, identity(attempt + 40));
    append(body, identity(attempt + 80));
    ref(body, parent);
    append(body, int{30});
    // Odd attempts keep the layout of runs recorded before the stall timeout field.
    if (attempt % 2 == 0)
      append(body, int{120});
    const auto request = row(1300, std::move(body));
    body.clear();
    ref(body, parent);
    ref(body, request);
    (void)row(1324, std::move(body));
    body.clear();
    ref(body, request);
    text(body, "POST");
    text(body, "https://openrouter.ai/api/v1/responses");
    text(body, R"({"model":"vendor/coder"})");
    (void)row(1301, std::move(body), RawPartition::data);
    return request;
  }
  void acquired(std::uint64_t request, std::string_view value) {
    std::vector<std::byte> body;
    ref(body, request);
    append(body, std::uint8_t{1});
    append(body, value.size());
    append(body, true);
    append(body, value.size());
    const auto bytes = std::as_bytes(std::span{value});
    body.insert(body.end(), bytes.begin(), bytes.end());
    (void)row(1312, std::move(body), RawPartition::data);
  }
  void auth_for(std::uint64_t request) {
    std::vector<std::byte> body;
    append(body, std::uint8_t{14});
    append(body, false);
    append(body, std::uint8_t{0});
    const auto action = row(1600, std::move(body));
    body.clear();
    ref(body, action);
    append(body, std::uint8_t{0});
    text(body, "openrouter");
    text(body, "fixture-account");
    (void)row(1601, std::move(body), RawPartition::data);
    body.clear();
    ref(body, action);
    ref(body, request);
    (void)row(1612, std::move(body));
  }
};
void recorded_sources() {
  Fixture fixture;
  const auto first = fixture.phase({.kind = 3, .attempt = 10});
  const auto request = fixture.http({.parent = first, .attempt = 10});
  fixture.auth_for(request);
  constexpr std::string_view failed =
      R"({"id":"response-1","model":"vendor/coder","status":"incomplete","usage":{"input_tokens":100,"output_tokens":20,"input_tokens_details":{"cached_tokens":40,"cache_write_tokens":10},"output_tokens_details":{"reasoning_tokens":5},"cost":0.125}})";
  fixture.acquired(
      request, std::string{"data: {\"type\":\"response.incomplete\",\"response\":"} +
                   std::string{failed} + "}\n\n");
  fixture.packet({first, 4, failed}); // Same response observation, independent capture.
  std::vector<std::byte> terminal;
  ref(terminal, first);
  append(terminal, std::uint8_t{2});
  append(terminal, std::uint8_t{24});
  append(terminal, ErrorCode::incomplete);
  append(terminal, std::int64_t{0});
  (void)fixture.row(1321, std::move(terminal));
  const auto retry = fixture.phase({.kind = 3, .attempt = 11});
  const auto retry_request = fixture.http({.parent = retry, .attempt = 11});
  fixture.acquired(
      retry_request,
      R"({"id":"response-2","model":"vendor/coder","status":"failed","usage":{"input_tokens":50,"output_tokens":0,"input_tokens_details":{"cached_tokens":0,"cache_write_tokens":0},"output_tokens_details":{"reasoning_tokens":0},"cost":0}})");
  const auto missing = fixture.phase({.kind = 3, .attempt = 12});
  (void)fixture.http({.parent = missing, .attempt = 12});
  const auto colleague = fixture.phase({.kind = 12, .attempt = 13});
  fixture.packet(
      {colleague, 4,
       R"({"type":"result","is_error":true,"modelUsage":{"claude-fixture":{"inputTokens":7,"outputTokens":3,"cacheReadInputTokens":2,"cacheCreationInputTokens":1}},"usage":{"input_tokens":7,"output_tokens":3,"cache_read_input_tokens":2,"cache_creation_input_tokens":1}})"});
  fixture.packet(
      {colleague, 5,
       R"({"provider":"claude","requested_model":"claude-fixture","status":"failed","usage":{"input_tokens":7,"output_tokens":3,"cache_read_input_tokens":2,"cache_creation_input_tokens":1},"model_usage":{"claude-fixture":{"inputTokens":7,"outputTokens":3,"cacheReadInputTokens":2,"cacheCreationInputTokens":1}}})"});
  fixture.publish();
  const auto before = files(fixture.name.data());
  const auto report = need(read_token_usage(fixture.name.data()));
  CHECK(report.observations.size() == 5);
  CHECK(!report.complete_session_ledger);
  CHECK(report.runs.size() == 1 && report.runs[0].reached_preserved_end);
  const auto &one = report.observations[0];
  CHECK(one.attempt == identity(10));
  CHECK(one.account == "fixture-account" && one.provider == "openrouter");
  CHECK(one.terminal_branch == 2 && one.terminal_stage == 24);
  CHECK(one.counters.input == 100 && one.counters.output == 20);
  CHECK(one.counters.cache_read == 40 && one.counters.cache_write == 10);
  CHECK(one.counters.reasoning == 5);
  CHECK(one.response_status == "incomplete" && one.sources.size() >= 2);
  CHECK(one.observed_cost_usd && one.observed_cost_usd->text() == "0.125");
  CHECK(one.sources[0].event.ordinal != 0 && one.sources[0].body_offset > 16);
  auto original =
      need(RawRunReader::open(one.sources[0].run_directory, one.sources[0].cursor));
  std::vector<std::byte> buffer(65536);
  const auto read = need(original->read(buffer));
  if (!read)
    std::abort();
  const auto slice =
      read->raw.subspan(one.sources[0].body_offset, one.sources[0].bytes);
  const std::string expected =
      std::string{"data: {\"type\":\"response.incomplete\",\"response\":"} +
      std::string{failed} + "}\n\n";
  CHECK(std::string_view{reinterpret_cast<const char *>(slice.data()), slice.size()} ==
        expected);
  const auto totals = token_usage_subtotals(report);
  const auto main = std::find_if(totals.begin(), totals.end(), [](const auto &sum) {
    return sum.actor == UsageActor::main;
  });
  CHECK(main != totals.end());
  CHECK(main->observations == 3 && main->missing_usage == 1);
  CHECK(main->known.input == 150 && main->known.output == 20);
  CHECK(main->known.reasoning == 5);
  UsagePrice price{"2026-10-14", "openrouter", "vendor/coder", "", 2, 4, 0.5, 3};
  const auto estimate = estimate_token_usage(one, price);
  CHECK(estimate.usd && *estimate.usd > 0.000229 && *estimate.usd < 0.000231);
  CHECK(one.observed_cost_usd && one.observed_cost_usd->text() == "0.125");
  auto impossible = one;
  impossible.counters.cache_read = 101;
  CHECK(!estimate_token_usage(impossible, price).usd);
  auto invalid_date = price;
  invalid_date.date = "2026-02-31";
  CHECK(!estimate_token_usage(one, invalid_date).usd);
  TokenUsageLimits limits;
  limits.max_records = 1;
  const auto bounded = need(read_token_usage(fixture.name.data(), limits));
  CHECK(!bounded.runs[0].reached_preserved_end && !bounded.issues.empty());
  CHECK(files(fixture.name.data()) == before);
  const auto rendered = render_token_usage(report, std::span{&price, 1});
  CHECK(rendered.find("raw-prefix-only") != std::string::npos);
  CHECK(rendered.find("0.125") != std::string::npos);
}
void decision_and_gaps() {
  Fixture fixture;
  std::vector<std::byte> body;
  ref(body, 999); // Deliberately absent operation parent, not a guessed latest call.
  append(body, std::size_t{1024});
  append(body, std::size_t{1024});
  const auto decision = fixture.row(3600, std::move(body));
  body.clear();
  ref(body, decision);
  text(body, R"({"model":"jev-fixture"})");
  (void)fixture.row(3602, std::move(body), RawPartition::data);
  body.clear();
  ref(body, decision);
  for (unsigned i = 0; i < 4; ++i)
    append(body, i == 2);
  fixture.delay_child = true;
  const auto child = fixture.row(1400, std::move(body));
  const auto chunk = [&](std::string_view value) {
    std::vector<std::byte> bytes;
    ref(bytes, child);
    append(bytes, std::uint8_t{1});
    append(bytes, value.size());
    append(bytes, static_cast<ssize_t>(value.size()));
    append(bytes, int{0});
    const auto raw = std::as_bytes(std::span{value});
    bytes.insert(bytes.end(), raw.begin(), raw.end());
    (void)fixture.row(1404, std::move(bytes), RawPartition::data);
  };
  chunk(R"({"model":"jev-fixture","usage":{"input_tokens":9,)");
  chunk("\"output_tokens\":2}}\n503"); // HTTP failure still reports consumption.
  body.clear();
  ref(body, decision);
  append(body, std::uint8_t{3});
  append(body, std::uint8_t{8});
  append(body, true);
  append(body, ErrorCode::external_unknown);
  append(body, std::int64_t{503});
  (void)fixture.row(3601, std::move(body));
  const auto bad = fixture.phase({.kind = 3, .attempt = 20});
  fixture.acquired(fixture.http({.parent = bad, .attempt = 20}), "data: {broken\n\n");
  fixture.packet({999, 4, R"({"usage":{"input_tokens":1000,"output_tokens":1000}})"});
  fixture.publish();
  const auto before = files(fixture.name.data());
  const auto report = need(read_token_usage(fixture.name.data()));
  CHECK(report.observations.size() == 2);
  const auto &jev = report.observations[0];
  CHECK(jev.actor == UsageActor::decision && jev.provider == "jev");
  CHECK(jev.counters.input == 9 && jev.counters.output == 2);
  CHECK(jev.terminal_branch == 3 && jev.terminal_stage == 8);
  CHECK(!jev.attempt); // Parent gap cannot manufacture retained operation IDs.
  CHECK(jev.sources.size() == 2);
  CHECK(!report.issues.empty());
  CHECK(!report.observations[1].usage_present);
  CHECK(files(fixture.name.data()) == before);
}
void invalid_usage_and_local_refusal() {
  Fixture fixture;
  const auto chat = fixture.phase({.kind = 3, .attempt = 30});
  fixture.packet(
      {chat, 4,
       R"({"model":"chat-fixture","usage":{"prompt_tokens":12,"completion_tokens":4,"prompt_tokens_details":{"cached_tokens":8},"completion_tokens_details":{"reasoning_tokens":3},"new_counter":6}})"});
  const auto conflict = fixture.phase({.kind = 3, .attempt = 31});
  fixture.packet({conflict, 4, R"({"usage":{"input_tokens":7,"output_tokens":2}})"});
  fixture.packet({conflict, 5, R"({"usage":{"input_tokens":8,"output_tokens":2}})"});
  const auto fractional = fixture.phase({.kind = 3, .attempt = 32});
  fixture.packet(
      {fractional, 4, R"({"usage":{"input_tokens":1.5,"output_tokens":2}})"});
  const auto refusal = fixture.phase({.kind = 11, .attempt = 33});
  fixture.packet(
      {refusal, 5,
       R"({"provider":"claude","requested_model":"fixture","status":"refused","remote_disposition":"not_dispatched","usage":null})"});
  const auto anthropic = fixture.phase({.kind = 12, .attempt = 34});
  fixture.packet(
      {anthropic, 4,
       R"({"type":"message","model":"claude-fixture","usage":{"input_tokens":10,"output_tokens":2,"cache_read_input_tokens":20,"cache_creation_input_tokens":5,"cache_creation":{"ephemeral_5m_input_tokens":0,"ephemeral_1h_input_tokens":5}}})"});
  fixture.publish();
  const auto report = need(read_token_usage(fixture.name.data()));
  CHECK(report.observations.size() == 5);
  CHECK(report.observations[0].counters.input == 12);
  CHECK(report.observations[0].counters.output == 4);
  CHECK(report.observations[0].counters.reasoning == 3);
  CHECK(!report.observations[0].counters.cache_write);
  CHECK(std::any_of(
      report.observations[0].reported_fields.begin(),
      report.observations[0].reported_fields.end(), [](const auto &entry) {
        return entry.name == "usage.new_counter" && entry.value == Number{6};
      }));
  CHECK(report.observations[1].conflicting);
  CHECK(report.observations[2].malformed);
  CHECK(report.observations[3].dispatch == UsageDispatch::locally_refused);
  CHECK(!report.observations[3].usage_present);
  const auto &claude = report.observations[4];
  CHECK(claude.provider == "anthropic" && !claude.counters.input_includes_cache);
  CHECK(claude.counters.input == 10 && claude.counters.cache_read == 20);
  UsagePrice price{"2026-10-14", "anthropic", "claude-fixture", "", 2, 4, 0.5, 3};
  CHECK(!estimate_token_usage(claude, price).usd); // A 1h write needs its own rate.
  const auto totals = token_usage_subtotals(report);
  const auto main = std::find_if(totals.begin(), totals.end(), [](const auto &entry) {
    return entry.actor == UsageActor::main;
  });
  CHECK(main != totals.end() && main->known.input == 12 && main->invalid_usage == 2);
}
std::string terminal_frames(std::initializer_list<std::string_view> responses) {
  std::string bytes;
  for (const auto response : responses)
    bytes += std::string{"data: {\"type\":\"response.completed\",\"response\":"} +
             std::string{response} + "}\n\n";
  return bytes;
}
void review_regressions() {
  Fixture fixture;
  const auto custom = fixture.phase({.kind = 3, .attempt = 40});
  fixture.packet(
      {custom, 4,
       R"({"model":"custom","provider":"openrouter","usage":{"input_tokens":10,"output_tokens":2,"cost":7}})"});
  const auto bad_cost = fixture.phase({.kind = 3, .attempt = 41});
  fixture.packet(
      {bad_cost, 4,
       R"({"model":"custom","usage":{"input_tokens":10,"output_tokens":2,"cost":"unknown"}})"});
  const auto cli = fixture.phase({.kind = 12, .attempt = 42});
  fixture.packet(
      {cli, 4,
       R"({"type":"result","is_error":false,"total_cost_usd":0.25,"usage":{"input_tokens":3,"output_tokens":1}})"});
  constexpr std::string_view first =
      R"({"id":"r1","model":"vendor/coder","status":"completed","usage":{"input_tokens":100,"output_tokens":10,"input_tokens_details":{"cached_tokens":0,"cache_write_tokens":0}}})";
  constexpr std::string_view changed_counts =
      R"({"id":"r1","model":"vendor/coder","status":"completed","usage":{"input_tokens":7,"output_tokens":2,"input_tokens_details":{"cached_tokens":0,"cache_write_tokens":0}}})";
  constexpr std::string_view changed_id =
      R"({"id":"r2","model":"vendor/coder","status":"completed","usage":{"input_tokens":100,"output_tokens":10,"input_tokens_details":{"cached_tokens":0,"cache_write_tokens":0}}})";
  const auto conflicting = fixture.phase({.kind = 3, .attempt = 43});
  fixture.acquired(fixture.http({conflicting, 43}),
                   terminal_frames({first, changed_counts}));
  const auto identities = fixture.phase({.kind = 3, .attempt = 44});
  fixture.acquired(fixture.http({identities, 44}),
                   terminal_frames({first, changed_id}));
  const auto duplicate = fixture.phase({.kind = 3, .attempt = 45});
  fixture.acquired(fixture.http({duplicate, 45}), terminal_frames({first, first}));
  const std::string start =
      "data: "
      "{\"type\":\"message_start\",\"message\":{\"type\":\"message\",\"model\":"
      "\"partial\",\"usage\":{\"input_tokens\":10,\"output_tokens\":0,\"cache_read_"
      "input_tokens\":0,\"cache_creation_input_tokens\":0}}}\n\n"
      "data: {\"type\":\"message_delta\",\"usage\":{\"output_tokens\":3}}\n\n";
  const auto partial = fixture.phase({.kind = 3, .attempt = 46});
  fixture.acquired(fixture.http({partial, 46}),
                   start + "data: {\"type\":\"message_stop\"}\n\n");
  const auto cumulative = fixture.phase({.kind = 3, .attempt = 47});
  fixture.acquired(fixture.http({cumulative, 47}),
                   start + "data: "
                           "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":"
                           "\"end_turn\"},\"usage\":{\"output_tokens\":5}}\n\n"
                           "data: {\"type\":\"message_stop\"}\n\n");
  const auto tier = fixture.phase({.kind = 3, .attempt = 48});
  fixture.packet(
      {tier, 4,
       R"({"model":"tier","service_tier":"default","usage":{"input_tokens":5,"output_tokens":2,"input_tokens_details":{"cached_tokens":0,"cache_write_tokens":0}}})"});
  fixture.packet(
      {tier, 5,
       R"({"model":"tier","service_tier":"priority","usage":{"input_tokens":5,"output_tokens":2,"input_tokens_details":{"cached_tokens":0,"cache_write_tokens":0}}})"});
  fixture.publish();
  const auto report = need(read_token_usage(fixture.name.data()));
  CHECK(report.observations.size() == 9);
  CHECK(!report.observations[0].observed_cost_usd);
  CHECK(report.observations[0].reported_cost == Number{7});
  CHECK(!report.observations[1].malformed && report.observations[1].malformed_cost);
  CHECK(report.observations[1].counters.input == 10);
  CHECK(report.observations[2].observed_cost_usd == Number{"0.25"});
  CHECK(report.observations[3].conflicting && report.observations[4].conflicting);
  const auto &identical = report.observations[5];
  CHECK(!identical.conflicting && !identical.partial_usage);
  CHECK(identical.counters.input == 100 && identical.counters.output == 10);
  CHECK(report.observations[6].partial_usage &&
        report.observations[6].counters.output == 3);
  CHECK(!report.observations[7].partial_usage && !report.observations[7].conflicting);
  CHECK(report.observations[7].counters.output == 5);
  const auto &tier_conflict = report.observations[8];
  CHECK(tier_conflict.pricing_conflict && !tier_conflict.malformed);
  UsagePrice tier_price{"2026-10-14", "", "tier", "default", 2, 4, 0.5, 3};
  CHECK(!estimate_token_usage(tier_conflict, tier_price).usd);
  UsagePrice price{"2026-10-14", "openrouter", "vendor/coder", "", 2, 4, 0.5, 3};
  CHECK(!estimate_token_usage(report.observations[3], price).usd);
  const auto totals = token_usage_subtotals(report);
  const auto unknown =
      std::find_if(totals.begin(), totals.end(), [](const auto &entry) {
        return entry.actor == UsageActor::main && entry.provider.empty();
      });
  CHECK(unknown != totals.end() && unknown->known.input == 15 &&
        unknown->known.output == 4);
}
void standalone_role() {
  Fixture fixture;
  const auto valid = fixture.phase({.kind = 11});
  fixture.packet({valid, 11, R"({"provider":"openai","model":"fixture"})"});
  const auto malformed = fixture.phase({.kind = 11});
  fixture.packet({malformed, 11, "{not-json"});
  fixture.publish();
  const auto report = need(read_token_usage(fixture.name.data()));
  CHECK(report.observations.empty());
  CHECK(
      std::count_if(report.issues.begin(), report.issues.end(), [](const auto &issue) {
        return issue.reason == "malformed request original";
      }) == 1);
  CHECK(
      std::count_if(report.issues.begin(), report.issues.end(), [](const auto &issue) {
        return issue.reason == "unlinked or over-depth original excluded from totals" &&
               issue.source.has_value();
      }) == 2);
}
void actual_callback_producer() {
  Fixture fixture;
  const auto session = std::filesystem::path{fixture.name.data()} / "native";
  std::filesystem::create_directory(session);
  std::filesystem::permissions(session, std::filesystem::perms::owner_all);
  constexpr std::string_view response =
      R"({"id":"native","model":"vendor/native","status":"failed","usage":{"input_tokens":17,"output_tokens":3,"input_tokens_details":{"cached_tokens":2,"cache_write_tokens":1},"output_tokens_details":{"reasoning_tokens":1},"cost":0.125}})";
  {
    auto runtime = need(RawRuntime::open(session.string(),
                                         need(EnvironmentId::from_bytes(identity(90))),
                                         {{32 * 1024 * 1024, 128 * 1024, 4096}, 4096}));
    auto handle = need(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    for (unsigned ordinal = 0; ordinal < 2; ++ordinal) {
      RawProviderOwner owner{
          identity(91 + ordinal), identity(95 + ordinal), identity(99), {}};
      RawAdapterContext context{&owner, {}, nullptr, nullptr};
      RawProviderPhase attempt;
      attempt.start(3, &context);
      owner.dispatch = attempt.reference();
      context.parent = attempt.reference();
      ProviderHttpRequest request;
      request.url = "https://openrouter.ai/api/v1/responses";
      request.body = R"({"model":"vendor/native"})";
      ProviderAuthConfig config;
      config.http = [ordinal, response](const ProviderHttpRequest &,
                                        ProviderHttpResponseLease &body) {
        const auto output = body.prepare(response.size());
        std::copy(response.begin(), response.end(), output.begin());
        if (ordinal == 1)
          throw Error{ErrorCode::provider_transport, 77};
        body.commit(response.size());
        return 503;
      };
      try {
        const auto returned =
            provider_http(request, config, {}, &owner, true, nullptr, &context);
        CHECK(ordinal == 0 && returned.status == 503);
        const Error failed{ErrorCode::provider_transport, 503};
        attempt.finish(2, &failed);
      } catch (const Error &error) {
        CHECK(ordinal == 1 && error == Error{ErrorCode::provider_transport, 77});
        attempt.finish(2, &error);
      }
    }
  }
  const auto before = files(session);
  const auto report = need(read_token_usage(session.string()));
  CHECK(report.observations.size() == 2);
  const auto &returned = report.observations[0];
  CHECK(returned.dispatch == UsageDispatch::response_observed &&
        returned.http_status == 503);
  CHECK(returned.attempt == identity(91) && returned.counters.input == 17);
  CHECK(returned.counters.output == 3 && returned.terminal_branch == 2);
  CHECK(returned.observed_cost_usd == Number{"0.125"});
  CHECK(!report.observations[1].usage_present && !report.observations[1].http_status);
  CHECK(report.observations[1].dispatch != UsageDispatch::response_observed);
  CHECK(std::any_of(report.issues.begin(), report.issues.end(), [](const auto &issue) {
    return issue.reason == "unknown-used response extent is not a complete body";
  }));
  CHECK(files(session) == before);
}
} // namespace
int main() {
  recorded_sources();
  decision_and_gaps();
  invalid_usage_and_local_refusal();
  review_regressions();
  standalone_role();
  actual_callback_producer();
  return 0;
}
