#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/raw_caller.hpp"
#include "blackbird/raw_reader.hpp"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <unistd.h>

using namespace blackbird;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error{message};
}
template <class T> T identity(unsigned char byte) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{byte};
  return unwrap(T::from_bytes(bytes));
}
template <class T> T field_bytes(ByteView &bytes) {
  check(bytes.size() >= sizeof(T), "short native body");
  T result{};
  std::memcpy(&result, bytes.data(), sizeof(result));
  bytes = bytes.subspan(sizeof(result));
  return result;
}
class NoProvider final : public CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw std::runtime_error{"unexpected conversation"};
  }
};
constexpr std::string_view reply =
    "{\"model\":\"jev-1.13.0\",\"answers\":{\"q\":{\"type\":\"noul\",\"noul\":0.75}},"
    "\"usage\":{\"input_tokens\":3,\"output_tokens\":1}}\n200";
constexpr std::string_view encoded = "{\"model\":\"jev-1.13.0\",\"state\":\"fixture\","
                                     "\"questions\":{\"q\":{\"instructions\":"
                                     "\"Does it work?\",\"type\":\"noul\"}}}";
int fake() {
  const std::string input{std::istreambuf_iterator<char>{std::cin}, {}};
  if (input.find("Authorization: Bearer raw-decision-private-sentinel") ==
          std::string::npos ||
      input.find("fixture") == std::string::npos)
    return 19;
  if (const auto *marker = std::getenv("BB_DECISION_RAW_MARKER")) {
    std::ofstream dispatch{marker};
    dispatch << "actual dispatch";
  }
  const auto *mode_value = std::getenv("BB_DECISION_RAW_MODE");
  const std::string_view mode = mode_value ? mode_value : "normal";
  std::string bytes{reply};
  if (mode == "http")
    bytes = "{\"error\":\"overload\"}\n529";
  if (mode == "tail")
    bytes = "not-a-tail";
  if (mode == "malformed")
    bytes = std::string{"B\0\xff\n200", 7};
  if (mode == "semantic")
    bytes = "{\"model\":\"jev-1.13.0\",\"answers\":{\"q\":{\"type\":\"noul\",\"noul\":"
            "2}}}\n200";
  if (mode == "overflow")
    bytes = std::string(64, 'X');
  if (mode == "transport")
    bytes = std::string{"transport\0\xff", 11};
  if (mode == "cancel")
    bytes = std::string{"before-cancel\0\xff", 15};
  const auto n = ::write(STDOUT_FILENO, bytes.data(), bytes.size());
  if (n != static_cast<ssize_t>(bytes.size()))
    return 20;
  if (mode == "transport")
    return 56;
  if (mode == "cancel")
    for (;;)
      ::pause();
  return 0;
}

struct CapturedRecord {
  IdentityBytes lane;
  std::uint32_t site;
  std::uint64_t ordinal;
  std::vector<std::byte> body;
};
using Ref = std::pair<IdentityBytes, std::uint64_t>;
Ref read_ref(ByteView &bytes) {
  check(bytes.size() >= 24, "short caller reference");
  IdentityBytes lane{};
  std::copy_n(bytes.begin(), lane.size(), lane.begin());
  bytes = bytes.subspan(lane.size());
  const auto ordinal = field_bytes<std::uint64_t>(bytes);
  check(ordinal != 0 && lane != IdentityBytes{}, "invented caller reference");
  return {lane, ordinal};
}
std::vector<CapturedRecord> preserved(const std::filesystem::path &session) {
  const auto runs = unwrap(discover_raw_runs(session.string()));
  check(runs.size() == 1, "closed raw prefix");
  const auto &run = runs.front();
  const auto &snapshot = run.snapshot;
  if (!snapshot)
    throw std::runtime_error{"closed raw prefix"};
  check(snapshot->closed, "closed raw prefix");
  auto reader = unwrap(RawRunReader::open(session.string() + "/raw/" + run.name));
  std::vector<std::byte> scratch(16 * 1024 * 1024);
  std::vector<CapturedRecord> rows;
  while (const auto record = unwrap(reader->read(scratch))) {
    ByteView body{record->raw};
    const auto size = field_bytes<std::uint32_t>(body);
    const auto site = field_bytes<std::uint32_t>(body);
    const auto ordinal = field_bytes<std::uint64_t>(body);
    check(size == record->raw.size(), "preserved envelope extent");
    rows.push_back({record->origin.lane, site, ordinal, {body.begin(), body.end()}});
  }
  return rows;
}
void graph(const std::vector<CapturedRecord> &rows, std::string_view mode,
           RawOperationBranch outer_branch, std::size_t expected_calls = 1) {
  std::map<Ref, const CapturedRecord *> index;
  std::map<Ref, Ref> child_call;
  std::map<Ref, std::string> wire;
  std::map<Ref, std::uint64_t> cleanup, end;
  std::map<Ref, std::array<unsigned, 3>> released;
  std::map<std::pair<Ref, std::uint32_t>, unsigned> events;
  std::size_t calls = 0;
  for (const auto &row : rows)
    check(index.emplace(Ref{row.lane, row.ordinal}, &row).second,
          "duplicate raw identity");
  const auto parent_site = [&](Ref ref, std::uint32_t site) {
    check(index.contains(ref) && index.at(ref)->site == site,
          "causal raw owner/site mismatch");
  };
  for (const auto &row : rows) {
    ByteView bytes{row.body};
    const Ref own{row.lane, row.ordinal};
    if (row.site == 3600) {
      const auto parent = read_ref(bytes);
      check(index.contains(parent) &&
                (index.at(parent)->site == 9200 || index.at(parent)->site == 2400),
            "decision unrelated parent");
      check(field_bytes<std::size_t>(bytes) > 0 &&
                field_bytes<std::size_t>(bytes) > 0 && bytes.empty(),
            "decision limits layout");
      ++calls;
    }
    if (row.site == 1400) {
      const auto caller = read_ref(bytes);
      parent_site(caller, 3600);
      child_call.emplace(own, caller);
      check(!field_bytes<bool>(bytes) && !field_bytes<bool>(bytes) &&
                field_bytes<bool>(bytes) && !field_bytes<bool>(bytes) && bytes.empty(),
            "private child capture policy");
    }
  }
  for (const auto &row : rows) {
    ByteView bytes{row.body};
    const Ref own{row.lane, row.ordinal};
    if (row.site == 1404) {
      const auto child = read_ref(bytes);
      parent_site(child, 1400);
      const auto channel = field_bytes<std::uint8_t>(bytes);
      const auto requested = field_bytes<std::size_t>(bytes);
      const auto n = field_bytes<ssize_t>(bytes);
      (void)field_bytes<int>(bytes);
      check(channel == 1 && requested == 8192 &&
                (n < 0 || static_cast<std::size_t>(n) <= requested),
            "native actual read fields");
      if (n > 0) {
        check(bytes.size() == static_cast<std::size_t>(n), "native prefix length");
        wire[child].append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
      } else
        check(bytes.empty(), "invented failed read bytes");
    }
    if (row.site >= 1407 && row.site <= 1409) {
      const auto child = read_ref(bytes);
      parent_site(child, 1400);
      ++released[child][row.site - 1407];
      if (row.site == 1409) {
        check(field_bytes<std::uint8_t>(bytes) == 1 && bytes.empty(),
              "native owner cleanup variant");
        cleanup[child] = row.ordinal;
      }
    }
    if (row.site == 3603) {
      const auto caller = read_ref(bytes);
      parent_site(caller, 3600);
      const auto child = read_ref(bytes);
      check(child_call.at(child) == caller, "returned child drifted");
      const auto exit = field_bytes<int>(bytes);
      check(bytes.empty() && exit == (mode == "transport" ? 56 : 0),
            "actual native exit changed");
      ++events[{caller, 3603}];
    }
    if (row.site == 3604 || row.site == 3606 || row.site == 3608 || row.site == 3610) {
      const auto caller = read_ref(bytes);
      parent_site(caller, 3600);
      ++events[{caller, row.site}];
      if (row.site == 3604 || row.site == 3610)
        check(bytes.empty(), "plain owner request width");
      if (row.site == 3608) {
        check(field_bytes<std::size_t>(bytes) > 0 && bytes.empty(),
              "actual parser request size");
      }
      if (row.site == 3606) {
        const auto channel = field_bytes<std::uint8_t>(bytes);
        check(channel == 1 || channel == 2, "observer channel");
        check(field_bytes<bool>(bytes), "actual observer child omitted");
        const auto child = read_ref(bytes);
        check(child_call.at(child) == caller, "observer child drifted");
        const auto n = field_bytes<std::size_t>(bytes);
        check(n > 0 && field_bytes<bool>(bytes) && bytes.empty(),
              "actual observer fields");
      }
    }
    if (row.site == 3601 || row.site == 3605 || row.site == 3607 || row.site == 3609 ||
        row.site == 3611) {
      const auto request = read_ref(bytes);
      parent_site(request, row.site - 1);
      const auto branch = field_bytes<std::uint8_t>(bytes);
      (void)field_bytes<std::uint8_t>(bytes);
      const bool failed = field_bytes<bool>(bytes);
      std::optional<Error> error;
      if (failed)
        error = Error{field_bytes<ErrorCode>(bytes), field_bytes<std::int64_t>(bytes)};
      if (row.site == 3601) {
        check(branch == static_cast<std::uint8_t>(outer_branch),
              "actual outer terminal branch");
        end[request] = row.ordinal;
      }
      if (row.site == 3605) {
        if (mode == "tail") {
          check(branch == 3 && error && error->code == ErrorCode::corrupt &&
                    row.body.size() + 16 == 52,
                "HTTP malformed native outcome");
        } else {
          check(branch == 1 && !error &&
                    field_bytes<int>(bytes) == (mode == "http" ? 529 : 200) &&
                    row.body.size() + 16 == 47,
                "HTTP native status missing");
        }
      }
      if (row.site == 3609) {
        check(branch == 1, "JSON Result fabricated throw");
        const bool ok = field_bytes<bool>(bytes);
        check(ok == (mode != "malformed"), "JSON native Result flag");
        if (!ok)
          check(error && error->code == ErrorCode::corrupt &&
                    row.body.size() + 16 == 53,
                "maximum failed-Result terminal missing");
        else
          check(!error && row.body.size() + 16 == 44,
                "successful Result terminal width");
      }
      if (row.site == 3611) {
        check(branch == (mode == "semantic" ? 3 : 1) && failed == (mode == "semantic"),
              "actual validator terminal");
      }
      if (row.site == 3607) {
        ByteView source{index.at(request)->body};
        (void)read_ref(source);
        const auto channel = field_bytes<std::uint8_t>(source);
        const bool callback_failed =
            channel == 1 && (outer_branch == RawOperationBranch::allocation ||
                             outer_branch == RawOperationBranch::length ||
                             outer_branch == RawOperationBranch::standard ||
                             outer_branch == RawOperationBranch::nonstandard ||
                             mode == "observer_error");
        check(branch == static_cast<std::uint8_t>(callback_failed
                                                      ? outer_branch
                                                      : RawOperationBranch::returned),
              "actual callback terminal branch");
      }
      check(bytes.empty(), "terminal native width/trailing bytes");
      ++events[{request, row.site}];
    }
  }
  for (const auto &row : rows) {
    if (row.site == 3600 || row.site == 3604 || row.site == 3606 || row.site == 3608 ||
        row.site == 3610)
      check(events[{Ref{row.lane, row.ordinal}, row.site + 1}] == 1,
            "actual request has no unique outcome");
  }
  check(calls == expected_calls && child_call.size() == expected_calls &&
            end.size() == expected_calls,
        "missing actual caller/child/outcome");
  for (const auto &[child, caller] : child_call) {
    check(released[child] == std::array<unsigned, 3>{1, 1, 1},
          "native child not released exactly once");
    check(child.first == caller.first && cleanup.at(child) < end.at(caller),
          "terminal precedes actual child release");
    check(wire.contains(child), "native child stdout absent");
    const bool post_collect = outer_branch == RawOperationBranch::returned ||
                              mode == "http" || mode == "tail" || mode == "malformed" ||
                              mode == "semantic" || mode == "transport";
    check(events[{caller, 3603}] == (post_collect ? 1 : 0),
          "invented/missing native returned exit");
    const bool http = post_collect && mode != "transport";
    check(events[{caller, 3604}] == (http ? 1 : 0),
          "unreached/missing HTTP acquisition");
    const bool json = http && mode != "http" && mode != "tail";
    check(events[{caller, 3608}] == (json ? 1 : 0),
          "unreached/missing JSON acquisition");
    check(events[{caller, 3610}] == (json && mode != "malformed" ? 1 : 0),
          "unreached/missing validation");
  }
}
struct FaultCase {
  const char *name;
  const char *mode;
  RawOperationBranch branch;
  std::optional<Error> error;
  std::string expected;
};
void faults(const std::filesystem::path &directory, const std::string &executable) {
  const std::array cases{
      FaultCase{"http", "http", RawOperationBranch::error,
                Error{ErrorCode::external_unknown, 529},
                "{\"error\":\"overload\"}\n529"},
      FaultCase{"tail", "tail", RawOperationBranch::error, Error{ErrorCode::corrupt},
                "not-a-tail"},
      FaultCase{"malformed", "malformed", RawOperationBranch::error,
                Error{ErrorCode::corrupt}, std::string{"B\0\xff\n200", 7}},
      FaultCase{"semantic", "semantic", RawOperationBranch::error,
                Error{ErrorCode::corrupt},
                "{\"model\":\"jev-1.13.0\",\"answers\":{\"q\":{\"type\":\"noul\","
                "\"noul\":2}}}\n200"},
      FaultCase{"overflow", "overflow", RawOperationBranch::error,
                Error{ErrorCode::capacity}, std::string(64, 'X')},
      FaultCase{"transport", "transport", RawOperationBranch::error,
                Error{ErrorCode::provider_transport, 56},
                std::string{"transport\0\xff", 11}},
      FaultCase{"cancel", "cancel", RawOperationBranch::error,
                Error{ErrorCode::interrupted}, std::string{"before-cancel\0\xff", 15}},
      FaultCase{"observer_error", "normal", RawOperationBranch::error,
                Error{ErrorCode::io, 777}, std::string{reply}},
      FaultCase{"allocation",
                "normal",
                RawOperationBranch::allocation,
                {},
                std::string{reply}},
      FaultCase{"length", "normal", RawOperationBranch::length, {}, std::string{reply}},
      FaultCase{
          "standard", "normal", RawOperationBranch::standard, {}, std::string{reply}},
      FaultCase{"nonstandard",
                "normal",
                RawOperationBranch::nonstandard,
                {},
                std::string{reply}}};
  const auto request =
      unwrap(parse_json("{\"state\":\"fixture\",\"questions\":{\"q\":{\"instructions\":"
                        "\"Does it work?\",\"type\":\"noul\"}}}"));
  for (const auto &test : cases) {
    const auto session = directory / test.name;
    std::filesystem::create_directory(session);
    std::filesystem::permissions(session, std::filesystem::perms::owner_all);
    check(::setenv("BB_DECISION_RAW_MODE", test.mode, 1) == 0, "fixture mode");
    {
      auto runtime = unwrap(
          RawRuntime::open(session.string(), identity<EnvironmentId>(71),
                           {{32 * 1024 * 1024, 256 * 1024, 128 * 1024}, 128 * 1024}));
      auto handle = unwrap(runtime->prepare_thread());
      RawThreadBinding binding{handle};
      constexpr std::string_view label = "actual selected decision fixture";
      const std::array parts{std::as_bytes(std::span{label.data(), label.size()})};
      const RawEventRef parent{raw_lane_epoch(), unwrap(raw_emit(9200, parts))};
      const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
      DecisionModelConfig config;
      config.curl_executable = executable;
      config.request_limit = 4096;
      config.response_limit = std::string_view{test.mode} == "overflow" ? 16 : 8192;
      DecisionModels adapter{config};
      bool reached = false;
      adapter.cancelled = [&] {
        return reached && std::string_view{test.mode} == "cancel";
      };
      adapter.observer = [&](std::string_view source, std::string_view) {
        if (source != "decision_model.response")
          return;
        reached = true;
        const std::string_view name = test.name;
        if (name == "observer_error")
          throw Error{ErrorCode::io, 777};
        if (name == "allocation")
          throw std::bad_alloc{};
        if (name == "length")
          throw std::length_error{"fixture"};
        if (name == "standard")
          throw std::runtime_error{"fixture"};
        if (name == "nonstandard")
          throw 42;
      };
      bool failed = false;
      try {
        (void)adapter.evaluate(request, &context);
      } catch (const Error &error) {
        check(test.error && error.code == test.error->code &&
                  error.detail == test.error->detail,
              "wrong actual decision native error");
        failed = true;
      } catch (const std::bad_alloc &) {
        check(test.branch == RawOperationBranch::allocation, "wrong allocation");
        failed = true;
      } catch (const std::length_error &) {
        check(test.branch == RawOperationBranch::length, "wrong length");
        failed = true;
      } catch (const std::exception &) {
        check(test.branch == RawOperationBranch::standard, "wrong standard");
        failed = true;
      } catch (int value) {
        check(value == 42 && test.branch == RawOperationBranch::nonstandard,
              "wrong nonstandard");
        failed = true;
      }
      check(failed, "fault unexpectedly returned");
    }
    const auto runs = unwrap(discover_raw_runs(session.string()));
    check(runs.size() == 1, "fault run closure");
    const auto &run = runs.front();
    const auto &snapshot = run.snapshot;
    if (!snapshot)
      throw std::runtime_error{"fault run closure"};
    check(snapshot->closed, "fault run closure");
    auto reader = unwrap(RawRunReader::open(session.string() + "/raw/" + run.name));
    std::vector<std::byte> scratch(1024 * 1024);
    std::string wire, body;
    unsigned terminals = 0;
    bool real_branch = false;
    while (const auto record = unwrap(reader->read(scratch))) {
      ByteView bytes{record->raw};
      (void)field_bytes<std::uint32_t>(bytes);
      const auto site = field_bytes<std::uint32_t>(bytes);
      (void)field_bytes<std::uint64_t>(bytes);
      if (site == 1404) {
        bytes = bytes.subspan(24);
        const auto channel = field_bytes<std::uint8_t>(bytes);
        (void)field_bytes<std::size_t>(bytes);
        const auto n = field_bytes<ssize_t>(bytes);
        (void)field_bytes<int>(bytes);
        if (channel == 1 && n > 0) {
          check(bytes.size() == static_cast<std::size_t>(n), "fault native extent");
          wire.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        }
      }
      if (site == 3602) {
        bytes = bytes.subspan(24);
        const auto n = field_bytes<std::size_t>(bytes);
        check(bytes.size() == n, "fault original extent");
        body.assign(reinterpret_cast<const char *>(bytes.data()), n);
      }
      if (site == 3601) {
        bytes = bytes.subspan(24);
        const auto branch = field_bytes<std::uint8_t>(bytes);
        (void)field_bytes<std::uint8_t>(bytes);
        const bool error_present = field_bytes<bool>(bytes);
        check(branch == static_cast<std::uint8_t>(test.branch) &&
                  error_present == test.error.has_value(),
              "wrong preserved throw branch");
        if (error_present) {
          const auto &expected = test.error;
          if (!expected)
            throw std::runtime_error{"wrong preserved error"};
          const auto &error = *expected;
          check(field_bytes<ErrorCode>(bytes) == error.code &&
                    field_bytes<std::int64_t>(bytes) == error.detail,
                "wrong preserved error");
        }
        check(bytes.empty(), "fault terminal trailing bytes");
        real_branch = true;
        ++terminals;
      }
      const std::string_view raw{reinterpret_cast<const char *>(record->raw.data()),
                                 record->raw.size()};
      check(raw.find("raw-decision-private-sentinel") == std::string_view::npos,
            "private input captured");
    }
    graph(preserved(session), std::string_view{test.name}, test.branch);
    check(real_branch && terminals == 1 && wire == test.expected && body == encoded,
          "fault source/original/terminal was lost or transformed");
  }
  (void)::unsetenv("BB_DECISION_RAW_MODE");
}

void nested_observer(const std::filesystem::path &directory,
                     const std::string &executable) {
  const auto session = directory / "nested";
  std::filesystem::create_directory(session);
  std::filesystem::permissions(session, std::filesystem::perms::owner_all);
  const auto request =
      unwrap(parse_json("{\"state\":\"fixture\",\"questions\":{\"q\":{\"instructions\":"
                        "\"Does it work?\",\"type\":\"noul\"}}}"));
  {
    auto runtime = unwrap(
        RawRuntime::open(session.string(), identity<EnvironmentId>(61),
                         {{32 * 1024 * 1024, 256 * 1024, 128 * 1024}, 128 * 1024}));
    auto handle = unwrap(runtime->prepare_thread());
    RawThreadBinding binding{handle};
    constexpr std::string_view label = "nested native decision fixture";
    const std::array fields{std::as_bytes(std::span{label.data(), label.size()})};
    const RawAdapterContext context{
        nullptr, RawEventRef{raw_lane_epoch(), unwrap(raw_emit(9200, fields))}, nullptr,
        nullptr};
    DecisionModelConfig config;
    config.curl_executable = executable;
    DecisionModels outer{config}, inner{config};
    unsigned nested = 0;
    inner.observer = [](std::string_view, std::string_view) {};
    outer.observer = [&](std::string_view source, std::string_view) {
      if (source == "decision_model.response") {
        ++nested;
        check(string_field(inner.evaluate(request, &context), "status") == "ok",
              "nested actual decision failed");
      }
    };
    check(string_field(outer.evaluate(request, &context), "status") == "ok" &&
              nested == 1,
          "outer/nested actual results");
  }
  const auto rows = preserved(session);
  graph(rows, "normal", RawOperationBranch::returned, 2);
  std::map<Ref, std::string> originals, wires;
  std::map<Ref, Ref> children;
  std::vector<Ref> calls;
  std::map<Ref, std::uint64_t> callback_ends, caller_ends;
  for (const auto &row : rows) {
    ByteView b{row.body};
    if (row.site == 3600)
      calls.emplace_back(row.lane, row.ordinal);
    if (row.site == 1400)
      children.emplace(Ref{row.lane, row.ordinal}, read_ref(b));
    if (row.site == 3602) {
      const auto caller = read_ref(b);
      const auto n = field_bytes<std::size_t>(b);
      check(n == b.size(), "nested acquired original extent");
      originals[caller].assign(reinterpret_cast<const char *>(b.data()), n);
    }
    if (row.site == 3601)
      caller_ends.emplace(read_ref(b), row.ordinal);
  }
  for (const auto &row : rows) {
    ByteView b{row.body};
    if (row.site == 1404) {
      const auto child = read_ref(b);
      const auto channel = field_bytes<std::uint8_t>(b);
      (void)field_bytes<std::size_t>(b);
      const auto n = field_bytes<ssize_t>(b);
      (void)field_bytes<int>(b);
      if (channel == 1 && n > 0)
        wires[children.at(child)].append(reinterpret_cast<const char *>(b.data()),
                                         static_cast<std::size_t>(n));
    }
    if (row.site == 3606) {
      const auto caller = read_ref(b);
      const auto channel = field_bytes<std::uint8_t>(b);
      if (channel == 1) {
        const Ref callback{row.lane, row.ordinal};
        for (const auto &end : rows)
          if (end.site == 3607) {
            ByteView body{end.body};
            if (read_ref(body) == callback)
              callback_ends[caller] = end.ordinal;
          }
      }
    }
  }
  check(calls.size() == 2 && originals.size() == 2 && wires.size() == 2,
        "nested sources conflated");
  std::sort(calls.begin(), calls.end(),
            [](const Ref &a, const Ref &b) { return a.second < b.second; });
  check(calls[0].first == calls[1].first &&
            caller_ends.at(calls[1]) < callback_ends.at(calls[0]) &&
            callback_ends.at(calls[0]) < caller_ends.at(calls[0]),
        "nested cleanup/observer return causality");
  for (const auto &call : calls)
    check(originals.at(call) == encoded && wires.at(call) == reply,
          "nested originals changed/misattributed");
}

std::vector<CapturedRecord> memory_rows(RawChannel &channel, IdentityBytes lane) {
  std::vector<CapturedRecord> rows;
  for (auto partition :
       {RawPartition::data, RawPartition::control, RawPartition::failure}) {
    auto read = unwrap(channel.read(partition, 0, 32 * 1024 * 1024));
    ByteView bytes{read.bytes};
    while (!bytes.empty()) {
      const auto size = field_bytes<std::uint32_t>(bytes);
      const auto site = field_bytes<std::uint32_t>(bytes);
      const auto ordinal = field_bytes<std::uint64_t>(bytes);
      check(size >= 16 && size - 16 <= bytes.size(), "native channel frame");
      rows.push_back(
          {lane,
           site,
           ordinal,
           {bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size - 16)}});
      bytes = bytes.subspan(size - 16);
    }
  }
  return rows;
}
void admission_and_stop(const std::filesystem::path &directory,
                        const std::string &executable) {
  const auto request =
      unwrap(parse_json("{\"state\":\"fixture\",\"questions\":{\"q\":{\"instructions\":"
                        "\"Does it work?\",\"type\":\"noul\"}}}"));
  const std::array<std::string_view, 5> cases{"short", "credential", "default", "full",
                                              "sticky"};
  for (const auto name : cases) {
    const auto lane = identity<EnvironmentId>(81).bytes();
    const auto shm = "/bb-dcut-" + std::to_string(::getpid()) + "-" + std::string{name};
    auto channel = unwrap(RawChannel::create(
        shm, lane, {name == "short" ? 4143UL : 32UL * 1024 * 1024, 128 * 1024, 4096}));
    RawProducer producer{channel};
    const auto marker = directory / ("dispatch-" + std::string{name});
    check(::setenv("BB_DECISION_RAW_MARKER", marker.c_str(), 1) == 0,
          "dispatch marker");
    check(::setenv("BB_DECISION_RAW_MODE", "normal", 1) == 0, "normal cut mode");
    check(::setenv("TYPESAFE_API_KEY", "raw-decision-private-sentinel", 1) == 0,
          "cut credential");
    check(::unsetenv("jev") == 0, "remove fallback credential");
    DecisionModelConfig config;
    config.curl_executable = executable;
    config.request_limit = 4096;
    config.credential_file = directory / "no-such-credential";
    if (name == "credential")
      check(::unsetenv("TYPESAFE_API_KEY") == 0, "missing credential");
    std::array<int, 2> stop{-1, -1};
    if (name == "sticky")
      check(::pipe(stop.data()) == 0, "actual stop pipe");
    {
      RawThreadBinding binding{producer, lane};
      RawWaitScope wait{{{stop[0], -1}, {}}};
      constexpr std::string_view label = "actual admission fixture";
      const std::array parts{std::as_bytes(std::span{label.data(), label.size()})};
      const RawEventRef parent{lane, unwrap(raw_emit(9200, parts))};
      const RawAdapterContext context{nullptr, parent, nullptr, nullptr};
      DecisionModels adapter{config};
      std::optional<RawControlToken> filler;
      adapter.observer = [&](std::string_view source, std::string_view) {
        if (source != "decision_model.response")
          return;
        if (name == "full") {
          const auto available = unwrap(producer.available(RawPartition::control));
          filler.emplace(
              unwrap(producer.hold_capacity(RawPartition::control, available)));
          check(unwrap(producer.available(RawPartition::control)) == 0,
                "ordinary control not full");
          throw Error{ErrorCode::io, 777};
        }
        if (name == "sticky") {
          const char byte = 1;
          check(::write(stop[1], &byte, 1) == 1, "actual sticky stop");
          throw Error{ErrorCode::io, 777};
        }
      };
      bool failed = false;
      try {
        const auto result =
            adapter.evaluate(request, name == "default" ? nullptr : &context);
        check(name == "default" && string_field(result, "status") == "ok" &&
                  !result.find("elapsed_us"),
              "unexpected cut return");
      } catch (const Error &error) {
        const Error wanted{name == "short"        ? ErrorCode::capacity
                           : name == "credential" ? ErrorCode::unsupported
                                                  : ErrorCode::io,
                           name == "full" || name == "sticky" ? 777 : 0};
        check(error.code == wanted.code && error.detail == wanted.detail,
              "wrong actual preeffect/stop error");
        failed = true;
      }
      check(failed == (name != "default"), "missing actual cut");
    }
    const auto rows = memory_rows(channel, lane);
    unsigned begins = 0, children = 0, originals = 0, terminals = 0;
    for (const auto &row : rows) {
      if (row.site == 3600)
        ++begins;
      if (row.site == 1400)
        ++children;
      if (row.site == 3602)
        ++originals;
      if (row.site == 3601) {
        ByteView body{row.body};
        (void)read_ref(body);
        check(field_bytes<std::uint8_t>(body) == 3, "cut actual Error branch");
        const auto stage = field_bytes<std::uint8_t>(body);
        check(stage == (name == "short"        ? 2
                        : name == "credential" ? 3
                                               : 6),
              "cut crossed actual native boundary");
        ++terminals;
      }
    }
    if (name == "short" || name == "credential")
      check(begins == 1 && children == 0 &&
                originals == (name == "credential" ? 1 : 0) && terminals == 1 &&
                !std::filesystem::exists(marker),
            "refusal dispatched/lost original");
    else if (name == "default")
      check(begins == 0 && children == 0 && originals == 0 && terminals == 0 &&
                std::filesystem::exists(marker),
            "default adapter secretly selected");
    else {
      check(std::filesystem::exists(marker), "paid stop cut never dispatched");
      graph(rows, "observer_error", RawOperationBranch::error);
    }
    if (stop[0] >= 0) {
      (void)::close(stop[0]);
      (void)::close(stop[1]);
    }
    unwrap(RawChannel::unlink(shm));
  }
  (void)::unsetenv("BB_DECISION_RAW_MODE");
  (void)::unsetenv("BB_DECISION_RAW_MARKER");
  check(::setenv("TYPESAFE_API_KEY", "raw-decision-private-sentinel", 1) == 0,
        "restore test credential");
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 1 && std::string_view{argv[1]} == "--disable")
    return fake();
  try {
    const auto directory = std::filesystem::temp_directory_path() /
                           ("blackbird-decision-raw-" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
    const auto session = directory / "session";
    std::filesystem::create_directory(session);
    std::filesystem::permissions(session, std::filesystem::perms::owner_all);
    const auto environment = identity<EnvironmentId>(61);
    const JournalHeader header{
        environment, identity<AuditStreamId>(62), 3, {32768, 131072}, std::nullopt};
    {
      auto root = unwrap(
          RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                    NativeJournalDirectory::open(session.string()))),
                                "audit", header, {64 * 1024 * 1024, 10000}));
      AuditLog log{*root};
      ContextStore context{log};
      NoProvider provider;
      DecisionModelConfig config;
      config.curl_executable = std::filesystem::canonical(argv[0]);
      check(::setenv("TYPESAFE_API_KEY", "raw-decision-private-sentinel", 1) == 0,
            "test credential environment");
      CodingEngine engine{log, context, provider, "test", config};
      auto runtime = unwrap(
          RawRuntime::open(session.string(), environment,
                           {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
      auto handle = unwrap(runtime->prepare_thread());
      RawThreadBinding binding{handle};
      const auto request = unwrap(
          parse_json("{\"state\":\"fixture\",\"questions\":{\"q\":{\"type\":\"noul\","
                     "\"instructions\":\"Does it work?\"}}}"));
      const auto result = engine.operator_call("decision_model", request);
      check(string_field(result, "status") == "ok", "decision result changed");
    }
    const auto runs = unwrap(discover_raw_runs(session.string()));
    check(runs.size() == 1, "raw run did not close");
    const auto &run = runs.front();
    check(!run.error, "raw run did not close");
    const auto &snapshot = run.snapshot;
    if (!snapshot)
      throw std::runtime_error{"raw run did not close"};
    check(snapshot->closed, "raw run did not close");
    auto reader = unwrap(RawRunReader::open(session.string() + "/raw/" + run.name));
    std::vector<std::byte> scratch(16 * 1024 * 1024);
    std::string original, request;
    unsigned begins = 0, terminals = 0;
    while (const auto record = unwrap(reader->read(scratch))) {
      ByteView bytes{record->raw};
      const auto size = field_bytes<std::uint32_t>(bytes);
      const auto site = field_bytes<std::uint32_t>(bytes);
      (void)field_bytes<std::uint64_t>(bytes);
      check(size == record->raw.size(), "bad raw envelope");
      if (site == 1404) {
        check(bytes.size() >= 24, "missing native child parent");
        bytes = bytes.subspan(24);
        const auto channel = field_bytes<std::uint8_t>(bytes);
        (void)field_bytes<std::size_t>(bytes);
        const auto n = field_bytes<ssize_t>(bytes);
        (void)field_bytes<int>(bytes);
        if (n > 0 && channel == 1) {
          check(bytes.size() == static_cast<std::size_t>(n), "raw acquired extent");
          original.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        }
      } else if (site == 3600) {
        ++begins;
      } else if (site == 3601) {
        ++terminals;
      } else if (site == 3602) {
        check(bytes.size() >= 24, "missing encoded request parent");
        bytes = bytes.subspan(24);
        const auto n = field_bytes<std::size_t>(bytes);
        check(bytes.size() == n, "encoded request extent");
        request.assign(reinterpret_cast<const char *>(bytes.data()), n);
      }
      const std::string_view raw{reinterpret_cast<const char *>(record->raw.data()),
                                 record->raw.size()};
      check(raw.find("raw-decision-private-sentinel") == std::string_view::npos,
            "credential emitted into raw log");
    }
    check(original == reply, "actual decision stdout missing from preserved raw log");
    check(request == encoded, "original decision request bytes changed/missing");
    check(begins == 1 && terminals == 1, "decision request/outcome missing");
    graph(preserved(session), "normal", RawOperationBranch::returned);
    faults(directory, std::filesystem::canonical(argv[0]).string());
    admission_and_stop(directory, std::filesystem::canonical(argv[0]).string());
    nested_observer(directory, std::filesystem::canonical(argv[0]).string());
    std::filesystem::remove_all(directory);
    std::cout << "decision raw bytes passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
