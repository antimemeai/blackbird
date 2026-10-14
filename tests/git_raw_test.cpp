#include "../src/native_process.hpp"
#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/raw_caller.hpp"
#include "blackbird/raw_reader.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>
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
std::string git(std::initializer_list<std::string_view> args) {
  Value::Array argv;
  argv.emplace_back("/usr/bin/git");
  for (const auto arg : args)
    argv.emplace_back(std::string{arg});
  LocalTools tools;
  const auto result = tools.run("exec", Value::object({{"argv", Value{argv}}}));
  check(field(result, "exit_code").number().text() == "0", "fixture Git setup failed");
  check(tools.captured().size() == 1, "missing independent native Git source");
  return tools.captured()[0].bytes;
}
struct NativeGit {
  int code;
  std::string output, error;
};
NativeGit native_git(const std::vector<std::string> &args) {
  detail::Child child;
  child.separate_error = true;
  child.start(args);
  child.close_input();
  int code = 0;
  auto output =
      child.collect(detail::Clock::now() + std::chrono::seconds{5}, 65536, &code);
  return {code, std::move(output), std::move(child.error_bytes)};
}
using Ref = std::pair<IdentityBytes, std::uint64_t>;
Ref reference(ByteView &bytes) {
  return {field_bytes<IdentityBytes>(bytes), field_bytes<std::uint64_t>(bytes)};
}
struct Row {
  Ref own;
  std::uint32_t site;
  std::vector<std::byte> body;
};
std::vector<Row> rows(const std::filesystem::path &session) {
  const auto runs = unwrap(discover_raw_runs(session.string()));
  check(runs.size() == 1, "Git failure recorder closure");
  const auto &run = runs.front();
  const auto &snapshot = run.snapshot;
  if (!snapshot)
    throw std::runtime_error{"Git failure recorder closure"};
  check(snapshot->closed, "Git failure recorder closure");
  auto reader = unwrap(RawRunReader::open((session / "raw" / run.name).string()));
  std::vector<std::byte> scratch(1024 * 1024);
  std::vector<Row> result;
  while (const auto record = unwrap(reader->read(scratch))) {
    ByteView b{record->raw};
    check(field_bytes<std::uint32_t>(b) == record->raw.size(), "Git failure envelope");
    const auto site = field_bytes<std::uint32_t>(b);
    const auto ordinal = field_bytes<std::uint64_t>(b);
    check(site != 8000 && site != 8001, "Git raw ingest failure");
    result.push_back({{record->origin.lane, ordinal}, site, {b.begin(), b.end()}});
  }
  return result;
}
struct GitFixturePaths {
  const std::filesystem::path &directory;
  const std::filesystem::path &repository;
};
void path_admission(GitFixturePaths paths, const JournalHeader &header) {
  const auto &directory = paths.directory;
  const auto &repository = paths.repository;
  const auto session = directory / "path-admission";
  std::filesystem::create_directory(session);
  std::filesystem::permissions(session, std::filesystem::perms::owner_all);
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                       NativeJournalDirectory::open(session.string()))),
                                   "audit", header, {64 * 1024 * 1024, 10000}));
  AuditLog log{*root};
  Observations observer{log, [] { return ObservationTime{}; }};
  const auto path = repository.string();
  const auto before = root->fact_count();
  const auto lane = identity<EnvironmentId>(91).bytes();
  const auto name = "/bb-gcut-" + std::to_string(::getpid());
  auto channel =
      unwrap(RawChannel::create(name, lane, {48 + path.size() - 1, 256 * 1024, 4096}));
  RawProducer producer{channel};
  {
    RawThreadBinding binding{producer, lane};
    const std::uint8_t literal = 1;
    const RawAdapterContext context{
        nullptr,
        RawEventRef{lane, unwrap(raw_emit(9200,
                                          std::array{std::as_bytes(
                                              std::span{&literal, std::size_t{1}})},
                                          RawPartition::control))},
        nullptr, nullptr};
    bool refused = false;
    try {
      (void)observer.git(Value::object({{"path", Value{path}}}), {}, &context);
    } catch (const Error &error) {
      refused = error == Error{ErrorCode::capacity};
    }
    check(refused && root->fact_count() == before,
          "one-byte-short path grant acquired business sample");
  }
  unsigned begin = 0, end = 0;
  for (auto partition :
       {RawPartition::data, RawPartition::control, RawPartition::failure}) {
    const auto output = unwrap(channel.read(partition, 0, 256 * 1024));
    ByteView bytes{output.bytes};
    while (!bytes.empty()) {
      const auto length = field_bytes<std::uint32_t>(bytes),
                 site = field_bytes<std::uint32_t>(bytes);
      (void)field_bytes<std::uint64_t>(bytes);
      check(length >= 16 && length - 16 <= bytes.size(), "native admission frame");
      ByteView b = bytes.first(length - 16);
      bytes = bytes.subspan(length - 16);
      check(site != 3702 && site != 3703 && site != 1400 && site != 8000 &&
                site != 8001,
            "refused path dispatched/claimed acquired source");
      if (site == 3700)
        ++begin;
      if (site == 3701) {
        (void)reference(b);
        check(field_bytes<std::uint8_t>(b) == 3 && field_bytes<std::uint8_t>(b) == 2 &&
                  field_bytes<bool>(b) &&
                  field_bytes<ErrorCode>(b) == ErrorCode::capacity &&
                  field_bytes<std::int64_t>(b) == 0 && b.empty(),
              "path preeffect actual refusal terminal");
        ++end;
      }
    }
  }
  check(begin == 1 && end == 1, "path admission missing actual caller boundary");
  unwrap(RawChannel::unlink(name));
}

void owner_cases(GitFixturePaths paths, const JournalHeader &header) {
  const auto &directory = paths.directory;
  const auto &repository = paths.repository;
  const auto empty = directory / "empty-repository";
  std::filesystem::create_directory(empty);
  (void)git({"-C", empty.string(), "init", "-q"});
  const auto nonrepo = directory / "non-repository";
  std::filesystem::create_directory(nonrepo);
  for (const auto name :
       {"zero", "empty", "nonrepo", "invalid", "cancel", "default", "samplealloc"}) {
    const std::string_view kind{name};
    const auto path = (kind == "empty"     ? empty
                       : kind == "nonrepo" ? nonrepo
                                           : repository)
                          .string();
    const auto query =
        kind == "invalid"
            ? Value::object({{"path", Value{std::string{"bad\0path", 8}}}})
            : Value::object(
                  {{"path", Value{path}},
                   {"history",
                    Value{Number{kind == "zero" || kind == "samplealloc" ? 0 : 64}}}});
    std::vector<std::vector<std::string>> expected_args;
    std::vector<NativeGit> expected;
    if (kind == "zero" || kind == "empty" || kind == "nonrepo" || kind == "default" ||
        kind == "samplealloc") {
      expected_args.push_back(
          {"/usr/bin/git", "-C", path, "rev-parse", "--show-toplevel"});
      expected.push_back(native_git(expected_args.back()));
      if (kind != "nonrepo") {
        expected_args.push_back(
            {"/usr/bin/git", "-C", path, "rev-parse", "--verify", "HEAD"});
        expected.push_back(native_git(expected_args.back()));
      }
      if (kind == "default") {
        auto head = expected.back().output;
        check(head.back() == '\n', "independent native HEAD newline");
        head.pop_back();
        expected_args.push_back(
            {"/usr/bin/git", "-C", path, "log", "--no-show-signature",
             "--format=%H%x00%at%x00%ct%x00%P%x00", "-n", "64", head, "--"});
        expected.push_back(native_git(expected_args.back()));
      }
    }
    const auto session = directory / name;
    std::filesystem::create_directory(session);
    std::filesystem::permissions(session, std::filesystem::perms::owner_all);
    {
      auto root = unwrap(
          RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                    NativeJournalDirectory::open(session.string()))),
                                "audit", header, {64 * 1024 * 1024, 10000}));
      AuditLog log{*root};
      bool allocation_armed = false;
      Observations observer{log, [&] {
                              if (allocation_armed && kind == "samplealloc")
                                throw std::bad_alloc{};
                              return ObservationTime{};
                            }};
      allocation_armed = true;
      auto runtime = unwrap(
          RawRuntime::open(session.string(), header.environment,
                           {{32 * 1024 * 1024, 256 * 1024, 128 * 1024}, 128 * 1024}));
      auto handle = unwrap(runtime->prepare_thread());
      RawThreadBinding binding{handle};
      constexpr std::string_view label = "actual Git owner fixture";
      const std::array parts{std::as_bytes(std::span{label.data(), label.size()})};
      const RawAdapterContext context{
          nullptr, RawEventRef{raw_lane_epoch(), unwrap(raw_emit(9200, parts))},
          nullptr, nullptr};
      bool failed = false;
      try {
        const auto result = observer.git(
            query, [&] { return kind == "cancel"; },
            kind == "default" ? nullptr : &context);
        check(kind != "invalid" && kind != "cancel",
              "invalid/cancel unexpectedly returned");
        const auto &samples = field(result, "samples").array();
        check(samples.size() == (kind == "default" ? 2 : 1),
              "Git sample count changed");
        check(string_field(samples[0], "observation_status") ==
                  ((kind == "empty" || kind == "nonrepo") ? "unavailable" : "observed"),
              "Git native availability changed");
      } catch (const std::bad_alloc &) {
        check(kind == "samplealloc", "unexpected Git allocation cut");
        failed = true;
      } catch (const Error &error) {
        check(error.code == (kind == "invalid" ? ErrorCode::invalid_range
                                               : ErrorCode::interrupted),
              "wrong actual Git refusal");
        failed = true;
      }
      check(failed == (kind == "invalid" || kind == "cancel" || kind == "samplealloc"),
            "Git refusal missing");
    }
    const auto records = rows(session);
    std::map<Ref, const Row *> index;
    std::map<Ref, std::uint8_t> invocations;
    std::map<Ref, Ref> children;
    std::map<Ref, std::array<unsigned, 3>> cleanup;
    std::map<Ref, std::uint64_t> released, ends;
    std::map<Ref, std::array<std::string, 2>> wires;
    std::map<Ref, std::vector<std::pair<std::size_t, std::string>>> arguments;
    std::map<Ref, unsigned> invoke_ends, exits;
    unsigned whole = 0, terminal = 0, predicate = 0;
    Ref owner{};
    for (const auto &row : records)
      check(index.emplace(row.own, &row).second, "duplicate Git raw identity");
    for (const auto &row : records) {
      ByteView b{row.body};
      if (row.site == 3700) {
        owner = row.own;
        check(index.contains(reference(b)) && b.empty(), "Git actual parent");
        ++whole;
      }
      if (row.site == 3703) {
        const auto parent = reference(b);
        check(parent == owner, "Git invocation parent");
        const auto k = field_bytes<std::uint8_t>(b);
        const auto n = field_bytes<std::size_t>(b);
        check(k >= 1 && k <= 3 &&
                  n == (k == 3   ? 11
                        : k == 2 ? 6
                                 : 5) &&
                  b.empty(),
              "Git actual argc");
        invocations.emplace(row.own, k);
      }
    }
    for (const auto &row : records) {
      ByteView b{row.body};
      if (row.site == 1400) {
        const auto parent = reference(b);
        check(invocations.contains(parent), "Git native child parent");
        children.emplace(row.own, parent);
        check(!field_bytes<bool>(b) && field_bytes<bool>(b) && field_bytes<bool>(b) &&
                  field_bytes<bool>(b) && b.empty(),
              "Git native policy changed");
      }
      if (row.site == 3705) {
        const auto parent = reference(b);
        const auto n = field_bytes<std::size_t>(b);
        const auto size = field_bytes<std::size_t>(b);
        check(size == b.size(), "Git argument native extent");
        arguments[parent].emplace_back(
            n, std::string{reinterpret_cast<const char *>(b.data()), size});
      }
      if (row.site == 3701) {
        check(reference(b) == owner, "Git terminal parent");
        const auto branch = field_bytes<std::uint8_t>(b);
        const auto stage = field_bytes<std::uint8_t>(b);
        const bool failed = field_bytes<bool>(b);
        check(stage == (kind == "invalid"                       ? 2
                        : kind == "cancel" || kind == "nonrepo" ? 4
                        : kind == "samplealloc"                 ? 5
                                                                : 8),
              "actual Git outer reached stage");
        check(branch == (kind == "invalid" || kind == "cancel" ? 3
                         : kind == "samplealloc"               ? 4
                                                               : 1) &&
                  failed == (kind == "invalid" || kind == "cancel"),
              "Git actual outer branch");
        if (failed) {
          check(field_bytes<ErrorCode>(b) == (kind == "invalid"
                                                  ? ErrorCode::invalid_range
                                                  : ErrorCode::interrupted),
                "Git actual raw error");
          (void)field_bytes<std::int64_t>(b);
          check(stage == (kind == "invalid" ? 2 : 4),
                "Git refusal crossed native boundary");
        }
        check(b.empty(), "Git terminal width");
        ++terminal;
      }
      if (row.site == 3704) {
        const auto invocation = reference(b);
        check(invocations.contains(invocation), "Git invoke terminal source");
        const auto branch = field_bytes<std::uint8_t>(b),
                   stage = field_bytes<std::uint8_t>(b);
        const bool failed = field_bytes<bool>(b);
        check(branch == (kind == "cancel" ? 3 : 1) && stage == 0 &&
                  failed == (kind == "cancel"),
              "Git actual invoke return/cancel native variant");
        if (failed)
          check(field_bytes<ErrorCode>(b) == ErrorCode::interrupted &&
                    field_bytes<std::int64_t>(b) == 0,
                "Git actual invocation Error fields");
        check(b.empty() && invoke_ends[invocation]++ == 0,
              "Git duplicated/wrong-width invocation terminal");
        ends[invocation] = row.own.second;
      }
      if (row.site == 3707) {
        check(reference(b) == owner && field_bytes<int>(b) == 0 &&
                  field_bytes<bool>(b) && b.empty(),
              "Git actual conditional HEAD predicate");
        ++predicate;
      }
    }
    for (const auto &row : records) {
      ByteView b{row.body};
      if (row.site == 1404) {
        const auto child = reference(b);
        check(children.contains(child), "Git actual read source");
        const auto channel = field_bytes<std::uint8_t>(b);
        check(channel == 1 || channel == 2, "Git native read channel");
        check(field_bytes<std::size_t>(b) == 8192, "Git requested read width");
        const auto n = field_bytes<ssize_t>(b);
        (void)field_bytes<int>(b);
        if (n > 0) {
          check(static_cast<std::size_t>(n) == b.size(), "Git native read extent");
          wires[children.at(child)][channel - 1].append(
              reinterpret_cast<const char *>(b.data()), b.size());
        } else
          check(b.empty(), "Git invented empty-read body");
      }
      if (row.site >= 1407 && row.site <= 1409) {
        const auto child = reference(b);
        check(children.contains(child), "Git cleanup source");
        ++cleanup[child][row.site - 1407];
        if (row.site == 1409)
          released[child] = row.own.second;
      }
      if (row.site == 3706) {
        const auto invocation = reference(b);
        const auto child = reference(b);
        const auto code = field_bytes<int>(b);
        check(children.at(child) == invocation &&
                  code == expected.at(invocations.at(invocation) - 1).code && b.empty(),
              "Git actual native exit changed");
        ++exits[invocation];
      }
    }
    if (kind == "default")
      check(whole == 0 && terminal == 0 && invocations.empty() && children.empty(),
            "Git default adapter secretly selected");
    else {
      check(whole == 1 && terminal == 1 &&
                invocations.size() == (kind == "invalid"  ? 0
                                       : kind == "cancel" ? 1
                                                          : expected.size()) &&
                ends.size() == invocations.size(),
            "Git missing/mispaired owner outcomes");
      check(predicate == (kind == "zero" || kind == "samplealloc" ? 1 : 0),
            "Git invented HEAD predicate on failed/nonzero HEAD");
      for (const auto &[invocation, k] : invocations) {
        auto acquired = arguments[invocation];
        std::sort(acquired.begin(), acquired.end());
        std::vector<std::string> actual;
        for (const auto &[n, value] : acquired) {
          check(n == actual.size(), "Git invocation argv ordering");
          actual.push_back(value);
        }
        check(invoke_ends[invocation] == 1 &&
                  exits[invocation] == (kind == "cancel" ? 0 : 1),
              "Git acquired/unacquired exit lost or fabricated");
        const auto wanted =
            kind == "cancel" ? std::vector<std::string>{"/usr/bin/git", "-C", path,
                                                        "rev-parse", "--show-toplevel"}
                             : expected_args.at(k - 1);
        check(actual == wanted, "Git original argv changed/lost");
        if (kind != "cancel")
          check(wires[invocation] ==
                    std::array{expected.at(k - 1).output, expected.at(k - 1).error},
                "Git independent stdout/stderr originals changed/lost");
      }
      for (const auto &[child, invocation] : children)
        check(cleanup[child] == (kind == "cancel" ? std::array<unsigned, 3>{0, 0, 1}
                                                  : std::array<unsigned, 3>{1, 1, 1}) &&
                  child.first == invocation.first &&
                  released.at(child) < ends.at(invocation),
              "Git child custody not closed before its own invoke end");
    }
  }
}

} // namespace
int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  try {
    const auto directory = std::filesystem::temp_directory_path() /
                           ("blackbird-git-raw-" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
    const auto session = directory / "session";
    std::filesystem::create_directory(session);
    std::filesystem::permissions(session, std::filesystem::perms::owner_all);
    const auto repository = directory / "repository";
    std::filesystem::create_directory(repository);
    const auto path = repository.string();
    (void)git({"-C", path, "init", "-q"});
    check(::setenv("GIT_AUTHOR_DATE", "978307200 +0000", 1) == 0, "author date");
    check(::setenv("GIT_COMMITTER_DATE", "978307200 +0000", 1) == 0, "commit date");
    (void)git({"-C", path, "-c", "user.name=Raw Fixture", "-c",
               "user.email=raw@example.test", "commit", "-q", "--allow-empty", "-m",
               "literal fixture"});
    const auto head_line = git({"-C", path, "rev-parse", "--verify", "HEAD"});
    check(head_line.size() == 41, "fixture object id");
    const auto head = head_line.substr(0, 40);
    std::string history = head;
    history.push_back('\0');
    history += "978307200";
    history.push_back('\0');
    history += "978307200";
    history.push_back('\0');
    history.push_back('\0');
    history.push_back('\n');
    const auto expected =
        std::filesystem::canonical(repository).string() + "\n" + head_line + history;
    const std::array<std::vector<std::string>, 3> expected_argv{
        {{"/usr/bin/git", "-C", path, "rev-parse", "--show-toplevel"},
         {"/usr/bin/git", "-C", path, "rev-parse", "--verify", "HEAD"},
         {"/usr/bin/git", "-C", path, "log", "--no-show-signature",
          "--format=%H%x00%at%x00%ct%x00%P%x00", "-n", "64", head, "--"}}};
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
      CodingEngine engine{log, context, provider, "test"};
      auto runtime = unwrap(
          RawRuntime::open(session.string(), environment,
                           {{256 * 1024 * 1024, 1024 * 1024, 128 * 1024}, 128 * 1024}));
      auto handle = unwrap(runtime->prepare_thread());
      RawThreadBinding binding{handle};
      const auto result = engine.operator_call(
          "git_observe",
          Value::object({{"path", Value{path}}, {"history", Value{Number{64}}}}));
      check(field(result, "samples").array().size() == 2,
            "Git sample behavior changed");
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
    unsigned begins = 0, terminals = 0, invocations = 0, child_returns = 0;
    std::array<std::vector<std::string>, 3> arguments;
    std::map<std::uint64_t, std::size_t> invocation_kind;
    std::vector<std::tuple<std::uint64_t, std::size_t, std::string>> acquired_arguments;
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
      } else if (site == 3700) {
        ++begins;
      } else if (site == 3701) {
        ++terminals;
      } else if (site == 3703) {
        check(bytes.size() >= 24, "missing caller parent");
        bytes = bytes.subspan(24);
        const auto kind = field_bytes<std::uint8_t>(bytes);
        const auto count = field_bytes<std::size_t>(bytes);
        check(kind >= 1 && kind <= 3 && count == expected_argv[kind - 1].size(),
              "wrong Git kind/native argc");
        const auto ordinal = [&] {
          ByteView header_bytes{record->raw};
          (void)field_bytes<std::uint32_t>(header_bytes);
          (void)field_bytes<std::uint32_t>(header_bytes);
          return field_bytes<std::uint64_t>(header_bytes);
        }();
        invocation_kind.emplace(ordinal, kind - 1);
        ++invocations;
      } else if (site == 3705) {
        check(bytes.size() >= 24, "missing argv owner");
        bytes = bytes.subspan(16);
        const auto invocation = field_bytes<std::uint64_t>(bytes);
        const auto index = field_bytes<std::size_t>(bytes);
        const auto n = field_bytes<std::size_t>(bytes);
        check(bytes.size() == n, "Git argv original extent");
        acquired_arguments.emplace_back(
            invocation, index,
            std::string{reinterpret_cast<const char *>(bytes.data()), n});
      } else if (site == 3706) {
        check(bytes.size() == 52, "native returned exit body width");
        bytes = bytes.subspan(48);
        check(field_bytes<int>(bytes) == 0, "wrong native Git exit");
        ++child_returns;
      } else if (site == 3702) {
        check(bytes.size() >= 24, "missing path parent");
        bytes = bytes.subspan(24);
        const auto n = field_bytes<std::size_t>(bytes);
        check(bytes.size() == n, "path extent");
        request.assign(reinterpret_cast<const char *>(bytes.data()), n);
      }
      const std::string_view raw{reinterpret_cast<const char *>(record->raw.data()),
                                 record->raw.size()};
      check(raw.find("raw-decision-private-sentinel") == std::string_view::npos,
            "credential emitted into raw log");
    }
    for (const auto &[invocation, index, value] : acquired_arguments) {
      auto &args = arguments.at(invocation_kind.at(invocation));
      check(index == args.size(), "Git argv order changed");
      args.push_back(value);
    }
    check(original == expected, "actual Git stdout missing from preserved raw log");
    check(request == path, "original Git path changed/missing");
    check(arguments == expected_argv, "actual argv originals changed/missing");
    check(begins == 1 && terminals == 1 && invocations == 3 && child_returns == 3,
          "Git request/outcome graph missing");
    owner_cases({.directory = directory, .repository = repository}, header);
    path_admission({.directory = directory, .repository = repository}, header);
    std::filesystem::remove_all(directory);
    std::cout << "Git raw bytes passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
