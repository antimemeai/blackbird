#include "blackbird/json.hpp"
#include "blackbird/raw_file.hpp"
#include "blackbird/raw_runtime.hpp"
#include "operation_raw_oracles.hpp"
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <unistd.h>

// Actual CodingEngine dispatch (operator, authored Lua, model tool call) emits
// the File surfaces under the dispatching operation. Expected bytes are test
// literals and files the fixture wrote itself; layouts follow
// raw_file_description(), never the production encoder.
using namespace operation_oracle;
namespace {
template <class T> T identity(unsigned char value) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(value);
  return unwrap(T::from_bytes(bytes));
}
struct Directory {
  std::filesystem::path path;
  Directory() {
    char name[] = "/tmp/blackbird-coding-file-raw-XXXXXX";
    require(::mkdtemp(name) != nullptr, "native fixture directory");
    path = name;
  }
  ~Directory() {
    if (std::uncaught_exceptions())
      std::cerr << "coding file raw fixture retained: " << path << '\n';
    else {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }
  }
};
std::string disk(const std::filesystem::path &file) {
  std::ifstream input{file, std::ios::binary};
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}
void fixture_file(const std::filesystem::path &file, std::string_view content) {
  std::ofstream output{file, std::ios::binary};
  output.write(content.data(), static_cast<std::streamsize>(content.size()));
  require(static_cast<bool>(output), "fixture file written");
}
std::string text(ByteView bytes) {
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}
// Business presentation of non-UTF-8 bytes: lowercase hex, compared by field.
bool hex(const Value &presented, std::string_view raw) {
  constexpr char digits[] = "0123456789abcdef";
  std::string encoded;
  for (const char ch : raw) {
    const auto c = static_cast<unsigned char>(ch);
    encoded += digits[c >> 4U];
    encoded += digits[c & 15U];
  }
  return std::holds_alternative<Value::Object>(presented.value()) &&
         presented.object().size() == 2 &&
         string_field(presented, "encoding") == "hex" &&
         string_field(presented, "bytes") == encoded;
}
class Unused final : public CodingProvider {
  Value respond(const Value &, const std::function<void(std::string_view)> &) override {
    throw Error{ErrorCode::conflict};
  }
};
// First response asks for the selected tool calls; the second ends the turn.
class ToolModel final : public CodingProvider {
public:
  Value::Array calls;
  int requests = 0;
  Value respond(const Value &,
                const std::function<void(std::string_view)> &capture) override {
    ++requests;
    Value::Array output;
    if (requests == 1)
      output = calls;
    else
      output.push_back(Value::object({{"type", Value{"message"}},
                                      {"id", Value{"m1"}},
                                      {"role", Value{"assistant"}},
                                      {"content", Value{Value::Array{Value::object(
                                                      {{"type", Value{"output_text"}},
                                                       {"text", Value{"done"}}})}}}}));
    auto response = Value::object({{"output", Value{std::move(output)}}});
    capture(unwrap(dump_json(response)));
    return response;
  }
};
Value function_call(std::string_view id, std::string_view name, const Value &args) {
  return Value::object({{"type", Value{"function_call"}},
                        {"call_id", Value{std::string{id}}},
                        {"name", Value{std::string{name}}},
                        {"arguments", Value{unwrap(dump_json(args))}}});
}
std::string workflow() {
  const auto source = disk(BLACKBIRD_WORKFLOW);
  require(!source.empty(), "default workflow source");
  return source;
}
struct Session {
  Directory directory;
  JournalHeader header{identity<EnvironmentId>(81),
                       identity<AuditStreamId>(82),
                       3,
                       {65536, 4194304},
                       {}};
  std::unique_ptr<RetainedState> root{unwrap(
      RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                NativeJournalDirectory::open(directory.path.string()))),
                            "audit", header, {64 * 1024 * 1024, 8192}))};
  template <class F> std::vector<Record> capture(F &&action) {
    {
      auto runtime =
          unwrap(RawRuntime::open(directory.path.string(), header.environment,
                                  {{64 * 1024 * 1024, 1024 * 1024, 4096}, 4096}));
      auto lane = unwrap(runtime->prepare_thread());
      {
        RawThreadBinding binding{lane};
        action();
      }
      lane.reset();
    }
    const auto runs = unwrap(discover_raw_runs(directory.path.string()));
    require(runs.size() == 1 && runs.front().snapshot && runs.front().snapshot->closed,
            "actual fixture raw run closed");
    auto reader = unwrap(
        RawRunReader::open((directory.path / "raw" / runs.front().name).string()));
    std::vector<std::byte> storage(32 * 1024 * 1024);
    std::vector<Record> records;
    while (const auto record = unwrap(reader->read(storage))) {
      ByteView bytes{record->raw};
      const auto length = read<std::uint32_t>(bytes);
      const auto site = read<std::uint32_t>(bytes);
      const auto ordinal = read<std::uint64_t>(bytes);
      require(length == record->raw.size(), "native envelope length");
      require(site != 8000 && site != 8001, "no failed native ingestion");
      records.push_back({{record->origin.lane, ordinal},
                         record->origin.partition,
                         site,
                         {bytes.begin(), bytes.end()}});
    }
    return records;
  }
};
const Record *named(const std::vector<Record> &records, RawEventRef reference) {
  const auto found = std::find_if(records.begin(), records.end(), [&](const auto &r) {
    return same(r.reference, reference);
  });
  return found == records.end() ? nullptr : &*found;
}
std::size_t count(const std::vector<Record> &records, std::uint32_t site) {
  return static_cast<std::size_t>(std::count_if(
      records.begin(), records.end(), [&](const auto &r) { return r.site == site; }));
}
// Every native return names exactly one actual earlier request of its pair.
void paired(const std::vector<Record> &records) {
  constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 18> pairs{
      {{3400, 3401},
       {3405, 3406},
       {3410, 3411},
       {3420, 3421},
       {3422, 3423},
       {3424, 3425},
       {3426, 3427},
       {3428, 3429},
       {3430, 3431},
       {3432, 3433},
       {3434, 3435},
       {3436, 3437},
       {3438, 3439},
       {3440, 3441},
       {3442, 3443},
       {3445, 3446},
       {3447, 3448},
       {3490, 3491}}};
  for (const auto &[request, returned] : pairs) {
    require(count(records, request) == count(records, returned),
            "native request and return counts balance");
    std::vector<std::uint64_t> answered;
    for (const auto &record : records) {
      if (record.site != returned)
        continue;
      ByteView bytes{record.bytes};
      auto actual = reference(bytes);
      if (returned == 3439 || returned == 3443) {
        // Cleanup returns name their resource, then the acknowledged request.
        require(read<bool>(bytes), "cleanup return acknowledges its request");
        actual = reference(bytes);
      }
      const auto *source = named(records, actual);
      require(source && source->site == request &&
                  source->reference.ordinal < record.reference.ordinal,
              "native return references actual earlier request");
      require(std::find(answered.begin(), answered.end(), actual.ordinal) ==
                  answered.end(),
              "one return per native request");
      answered.push_back(actual.ordinal);
    }
  }
}
std::string operation_name(const std::vector<Record> &records, RawEventRef owner) {
  for (const auto &record : records) {
    if (record.site != 2401)
      continue;
    ByteView bytes{record.bytes};
    if (!same(reference(bytes), owner))
      continue;
    const auto size = read<std::size_t>(bytes);
    require(bytes.size() == size, "operation name extent");
    return text(bytes);
  }
  throw std::runtime_error{"operation has no name original"};
}
const Record &owned(const std::vector<Record> &records, std::uint32_t site,
                    RawEventRef owner) {
  const Record *selected = nullptr;
  for (const auto &record : records) {
    if (record.site != site)
      continue;
    ByteView bytes{record.bytes};
    if (!same(reference(bytes), owner))
      continue;
    require(selected == nullptr, "one operation record per site");
    selected = &record;
  }
  require(selected != nullptr, "operation record present");
  return *selected;
}
// One 3407 original: request, role, extent, [known, [used]] for CString roles.
struct Original {
  RawEventRef owner;
  std::uint8_t role;
  std::optional<std::size_t> used;
  ByteView bytes;
};
Original original_body(const Record &record) {
  ByteView bytes{record.bytes};
  const auto owner = reference(bytes);
  const auto role = read<std::uint8_t>(bytes);
  const auto extent = read<std::size_t>(bytes);
  std::optional<std::size_t> used;
  if (role == 2 || role == 3 || role == 4) {
    if (read<bool>(bytes))
      used = read<std::size_t>(bytes);
  }
  require(bytes.size() == extent, "original extent is its exact body");
  return {owner, role, used, bytes};
}
std::string cstring(std::string_view value) {
  std::string out{value};
  out.push_back('\0');
  return out;
}
// The temporary sits beside its resolved destination.
struct Write {
  explicit Write(std::string resolved, bool from_realpath = false,
                 std::optional<int> native_error = {})
      : destination(std::move(resolved)), canonical(from_realpath),
        refused(native_error) {}
  std::string destination;
  bool canonical;
  std::optional<int> refused;
};
struct Expected {
  std::string_view name;
  RawFileMethod method = RawFileMethod::read;
  std::vector<std::pair<std::uint8_t, std::string>> arguments;
  std::optional<std::string> source, proposal;
  std::size_t observers = 0;
  std::optional<Error> error;
  std::size_t count = 0;
  // Edit step-3 search: std::string::find's actual return.
  std::optional<std::size_t> search;
  std::optional<Write> write;
};
// Native requests that name one of these helpers as their owner.
std::vector<const Record *> under(const std::vector<Record> &records,
                                  std::uint32_t site,
                                  const std::vector<RawEventRef> &helpers) {
  std::vector<const Record *> out;
  for (const auto &record : records) {
    if (record.site != site)
      continue;
    ByteView bytes{record.bytes};
    const auto owner = reference(bytes);
    if (std::any_of(helpers.begin(), helpers.end(),
                    [&](const auto &helper) { return same(helper, owner); }))
      out.push_back(&record);
  }
  return out;
}
const Record &answer(const std::vector<Record> &records, std::uint32_t site,
                     RawEventRef request) {
  const Record *selected = nullptr;
  for (const auto &record : records) {
    if (record.site != site)
      continue;
    ByteView bytes{record.bytes};
    if (!same(reference(bytes), request))
      continue;
    require(selected == nullptr, "one answer per request");
    selected = &record;
  }
  require(selected != nullptr, "request has its answer");
  return *selected;
}
std::vector<Original> originals(const std::vector<Record> &records, std::uint8_t role,
                                const std::vector<const Record *> &requests) {
  std::vector<Original> out;
  for (const auto &record : records) {
    if (record.site != 3407)
      continue;
    auto body = original_body(record);
    if (body.role == role &&
        std::any_of(requests.begin(), requests.end(), [&](const auto *request) {
          return same(request->reference, body.owner);
        }))
      out.push_back(body);
  }
  return out;
}
void check_write(const std::vector<Record> &records,
                 const std::vector<RawEventRef> &helpers, const Write &write,
                 RawEventRef proposal) {
  const auto realpaths = under(records, 3428, helpers);
  const auto resolved = originals(records, 3, realpaths);
  require(resolved.size() == (write.canonical ? 1u : 0u),
          "realpath resolves only an existing destination");
  if (write.canonical)
    require(resolved[0].used == resolved[0].bytes.size() &&
                text(resolved[0].bytes) == cstring(write.destination),
            "realpath body is the independent canonical destination");
  const auto temporaries = under(records, 3430, helpers);
  require(temporaries.size() == 1, "one native temporary request");
  ByteView created{answer(records, 3431, temporaries[0]->reference).bytes};
  (void)reference(created);
  const auto fd = read<int>(created);
  const auto failed = read<bool>(created);
  const auto names = originals(records, 4, temporaries);
  require(names.size() == 1, "one temporary name original");
  const auto &name = names[0];
  const auto prefix = write.destination + ".arco-";
  require(name.bytes.size() == prefix.size() + 7 &&
              text(name.bytes.first(prefix.size())) == prefix &&
              name.bytes.back() == std::byte{},
          "temporary name is destination.arco-XXXXXX with its NUL");
  const auto natives = [&](std::uint32_t site) {
    return under(records, site, helpers).size();
  };
  if (write.refused) {
    require(fd == -1 && failed && read<int>(created) == *write.refused &&
                created.empty(),
            "refused mkstemp copies its native errno");
    require(!name.used, "refused mkstemp has no known used extent");
    require(natives(3490) == 0 && natives(3434) == 0 && natives(3438) == 0 &&
                natives(3440) == 0,
            "refused temporary reaches no later native call");
    return;
  }
  require(fd >= 0 && !failed && created.empty(), "mkstemp returned a descriptor");
  require(name.used == name.bytes.size(), "created name has a known used extent");
  const auto writes = under(records, 3434, helpers);
  require(!writes.empty(), "proposal reaches native write");
  for (const auto *record : writes) {
    ByteView bytes{record->bytes};
    (void)reference(bytes);
    (void)reference(bytes);
    (void)read<std::uint8_t>(bytes);
    (void)read<int>(bytes);
    (void)read<std::size_t>(bytes);
    require(same(reference(bytes), proposal),
            "native write names the preserved proposal");
  }
  const auto renames = under(records, 3440, helpers);
  require(renames.size() == 1, "one native rename");
  ByteView bytes{renames[0]->bytes};
  (void)reference(bytes);
  (void)reference(bytes);
  require(read<std::uint8_t>(bytes) == 11, "native rename method");
  const auto old_size = read<std::size_t>(bytes);
  require(old_size == name.bytes.size() &&
              text(bytes.first(old_size)) == text(name.bytes),
          "rename source is the created temporary name");
  bytes = bytes.subspan(old_size);
  const auto new_size = read<std::size_t>(bytes);
  require(new_size == bytes.size() && text(bytes) == cstring(write.destination),
          "rename target is the resolved destination");
  ByteView renamed{answer(records, 3441, renames[0]->reference).bytes};
  (void)reference(renamed);
  require(read<int>(renamed) == 0 && !read<bool>(renamed) && renamed.empty(),
          "rename returned success");
}
// Checks one tool call against independent literal bytes and real parents.
void file_call(const std::vector<Record> &records, const Record &tool,
               const Expected &expected) {
  ByteView request{tool.bytes};
  const auto parent = reference(request);
  require(read<RawFileMethod>(request) == expected.method && request.empty(),
          "tool request names its actual method only");
  const auto *operation = named(records, parent);
  require(operation && operation->site == 2400,
          "File tool parent is the actual dispatching operation");
  require(operation_name(records, parent) == expected.name,
          "File tool parent operation dispatched this tool");
  const auto dispatch = owned(records, 2420, parent).reference.ordinal;
  const auto dispatched = owned(records, 2421, parent).reference.ordinal;
  const auto settled = owned(records, 2417, parent).reference.ordinal;
  const Record *terminal = nullptr;
  std::vector<std::pair<std::uint8_t, std::string>> arguments;
  std::vector<RawEventRef> helpers;
  std::vector<std::optional<std::size_t>> searches;
  const Record *writer = nullptr;
  std::size_t observers = 0;
  for (const auto &record : records) {
    ByteView bytes{record.bytes};
    if (record.site != 3401 && record.site != 3402 && record.site != 3404 &&
        record.site != 3405 && record.site != 3410)
      continue;
    if (!same(reference(bytes), tool.reference))
      continue;
    require(record.reference.ordinal > tool.reference.ordinal,
            "tool child follows its request");
    if (record.site == 3401) {
      require(terminal == nullptr, "one tool terminal");
      terminal = &record;
      const auto branch = read<RawFileBranch>(bytes);
      if (expected.error) {
        require(branch == RawFileBranch::error &&
                    read<ErrorCode>(bytes) == expected.error->code &&
                    read<std::int64_t>(bytes) == expected.error->detail,
                "tool terminal copies the actual business Error");
      } else
        require(branch == RawFileBranch::returned &&
                    read<std::size_t>(bytes) == expected.count,
                "tool terminal returns its actual result count");
      require(bytes.empty(), "tool terminal has no fabricated fields");
    } else if (record.site == 3402) {
      const auto kind = read<std::uint8_t>(bytes);
      require(read<std::size_t>(bytes) == bytes.size(), "argument extent");
      arguments.emplace_back(kind, text(bytes));
    } else if (record.site == 3404) {
      if (read<std::uint8_t>(bytes) != 3)
        continue;
      const auto source = reference(bytes);
      require(std::any_of(helpers.begin(), helpers.end(),
                          [&](const auto &helper) { return same(helper, source); }) &&
                  expected.source &&
                  read<std::size_t>(bytes) == expected.source->size(),
              "search names the actual read helper and its length");
      searches.push_back(read<bool>(bytes) ? std::optional{read<std::size_t>(bytes)}
                                           : std::nullopt);
      require(bytes.empty(), "search has no fabricated fields");
    } else if (record.site == 3405) {
      require(read<bool>(bytes) && bytes.empty(), "engine observer is present");
      ++observers;
    } else {
      helpers.push_back(record.reference);
      if (read<RawFileMethod>(bytes) == RawFileMethod::write) {
        require(writer == nullptr, "one native write helper");
        writer = &record;
      }
    }
  }
  require(terminal != nullptr, "tool terminal present");
  require(dispatch < tool.reference.ordinal &&
              terminal->reference.ordinal < dispatched && dispatched < settled,
          "File tool lives inside the operation dispatch");
  require(arguments == expected.arguments, "exact whole business arguments");
  require(observers == expected.observers, "observer per business original");
  if (expected.search)
    require(searches.size() == 1 && searches[0] == expected.search,
            "edit search records the actual find position");
  else
    require(searches.empty(), "no search outside edit");
  const auto reads = under(records, 3422, helpers);
  std::string source;
  for (const auto &body : originals(records, 1, reads))
    source += text(body.bytes);
  require(source == expected.source.value_or(""),
          "native read bodies are the independent file bytes");
  require(expected.source.has_value() == !reads.empty(), "native read presence");
  for (const auto *record : under(records, 3420, helpers)) {
    ByteView bytes{record->bytes};
    (void)reference(bytes);
    (void)reference(bytes);
    require(read<std::uint8_t>(bytes) == 1, "native open method");
    (void)read<int>(bytes);
    require(!read<bool>(bytes), "read open has no mode");
    const auto extent = read<std::size_t>(bytes);
    require(expected.arguments.front().first == 0 && extent == bytes.size() &&
                text(bytes) == cstring(expected.arguments.front().second),
            "native open operand is the literal path with its NUL");
  }
  RawEventRef proposal{};
  std::size_t proposals = 0;
  for (const auto &record : records) {
    if (record.site != 3407)
      continue;
    const auto body = original_body(record);
    if (body.role != 6 || !same(body.owner, tool.reference))
      continue;
    require(expected.proposal && text(body.bytes) == *expected.proposal,
            "proposal is the exact literal next file content");
    proposal = record.reference;
    ++proposals;
  }
  require(proposals == (expected.proposal ? 1u : 0u), "one exact tool proposal");
  require((writer != nullptr) == expected.write.has_value(), "write helper presence");
  if (!expected.write)
    return;
  ByteView ended{answer(records, 3411, writer->reference).bytes};
  (void)reference(ended);
  const auto branch = read<RawFileBranch>(ended);
  if (expected.error)
    require(branch == RawFileBranch::error &&
                read<ErrorCode>(ended) == expected.error->code &&
                read<std::int64_t>(ended) == expected.error->detail,
            "write helper ends with the actual Error");
  else
    require(branch == RawFileBranch::returned &&
                read<std::size_t>(ended) == expected.count,
            "write helper returns the written length");
  require(ended.empty(), "write helper terminal has no fabricated fields");
  check_write(records, helpers, *expected.write, proposal);
}
std::vector<const Record *> tools(const std::vector<Record> &records) {
  std::vector<const Record *> out;
  for (const auto &record : records)
    if (record.site == 3400)
      out.push_back(&record);
  return out;
}
std::string lua_path(const std::filesystem::path &path) {
  return "[==[" + path.string() + "]==]";
}
std::string resolved_path(const std::filesystem::path &path) {
  auto resolved = std::filesystem::canonical(path).string();
#if defined(__APPLE__)
  require(resolved.starts_with("/private/"), "macOS fixture resolves through /private");
#endif
  return resolved;
}
std::string operator_content() { return {"literal\0\xFE bytes\n", 16}; }
std::string operator_edited() { return {"literal\0\xFE BYTES!\n", 17}; }
struct OperatorRun {
  std::vector<Value> results;
  std::string bytes;
};
// The same operator sequence serves the raw oracle and the raw-off parity run.
OperatorRun operator_sequence(Session &session, const std::filesystem::path &files) {
  const auto target = files / "operator.bin";
  const auto missing = files / "nope" / "x";
  const auto content = operator_content(), edited = operator_edited();
  AuditLog log{*session.root};
  ContextStore context{log};
  Unused provider;
  CodingEngine engine{log, context, provider, "native.fixture"};
  OperatorRun run;
  run.results.push_back(engine.operator_call(
      "write_file",
      Value::object({{"path", Value{target.string()}}, {"content", Value{content}}})));
  require(field(run.results.back(), "written") == Value{true} &&
              disk(target) == content,
          "operator write applied the literal bytes");
  run.results.push_back(engine.operator_call(
      "read_file", Value::object({{"path", Value{target.string()}}})));
  const auto readback = run.results.back();
  require(hex(field(readback, "content"), content),
          "operator read returns the literal bytes");
  // The business original stays retained beside the File surfaces.
  run.results.push_back(engine.operator_call(
      "read_file", Value::object({{"snapshot", field(readback, "snapshot")}})));
  require(hex(field(run.results.back(), "content"), content),
          "retained business original is the literal bytes");
  run.results.push_back(
      engine.operator_call("edit_file", Value::object({{"path", Value{target.string()}},
                                                       {"old", Value{"bytes"}},
                                                       {"new", Value{"BYTES!"}}})));
  require(field(run.results.back(), "written") == Value{true} && disk(target) == edited,
          "operator edit applied the literal bytes");
  run.results.push_back(
      engine.operator_call("edit_file", Value::object({{"path", Value{target.string()}},
                                                       {"old", Value{"absent"}},
                                                       {"new", Value{"x"}}})));
  require(string_field(run.results.back(), "error") == "conflict",
          "operator edit of absent text conflicts");
  run.results.push_back(engine.operator_call(
      "write_file",
      Value::object({{"path", Value{missing.string()}}, {"content", Value{"never"}}})));
  require(string_field(run.results.back(), "error") == "io" &&
              !std::filesystem::exists(missing.parent_path()),
          "write into a missing directory fails and creates nothing");
  run.bytes = disk(target);
  return run;
}
void operator_dispatch() {
  const auto content = operator_content(), edited = operator_edited();
  Directory files;
  const auto target = files.path / "operator.bin";
  const auto missing = files.path / "nope" / "x";
  Session session;
  OperatorRun raw;
  const auto records =
      session.capture([&] { raw = operator_sequence(session, files.path); });
  paired(records);
  const auto calls = tools(records);
  require(calls.size() == 5, "one File tool per operator File dispatch");
  file_call(records, *calls[0], [&] {
    Expected e{};
    e.name = "write_file";
    e.method = RawFileMethod::write;
    e.arguments = {{0, target.string()}, {3, content}};
    e.proposal = content;
    e.observers = 1;
    e.count = content.size();
    e.write = Write{target.string()};
    return e;
  }());
  file_call(records, *calls[1], [&] {
    Expected e{};
    e.name = "read_file";
    e.method = RawFileMethod::read;
    e.arguments = {{0, target.string()}};
    e.source = content;
    e.observers = 1;
    e.count = content.size();
    return e;
  }());
  file_call(records, *calls[2], [&] {
    Expected e{};
    e.name = "edit_file";
    e.method = RawFileMethod::edit;
    e.arguments = {{0, target.string()}, {1, "bytes"}, {2, "BYTES!"}};
    e.source = content;
    e.proposal = edited;
    e.observers = 2;
    e.count = edited.size();
    e.search = 10;
    e.write = Write{resolved_path(target), true};
    return e;
  }());
  file_call(records, *calls[3], [&] {
    Expected e{};
    e.name = "edit_file";
    e.method = RawFileMethod::edit;
    e.arguments = {{0, target.string()}, {1, "absent"}, {2, "x"}};
    e.source = edited;
    e.observers = 1;
    e.error = Error{ErrorCode::conflict};
    e.search = std::string::npos;
    return e;
  }());
  file_call(records, *calls[4], [&] {
    Expected e{};
    e.name = "write_file";
    e.method = RawFileMethod::write;
    e.arguments = {{0, missing.string()}, {3, "never"}};
    e.proposal = "never";
    e.observers = 1;
    e.error = Error{ErrorCode::io, ENOENT};
    e.write = Write{missing.string(), false, ENOENT};
    return e;
  }());
  // Metamorphic parity: raw capture changes no business result or disk byte.
  std::filesystem::remove_all(files.path);
  std::filesystem::create_directory(files.path);
  Session plain;
  require(!raw_enabled(), "parity run has raw capture disabled");
  const auto quiet = operator_sequence(plain, files.path);
  require(quiet.results == raw.results && quiet.bytes == raw.bytes,
          "raw-off sequence returns identical business results and bytes");
  require(unwrap(discover_raw_runs(plain.directory.path.string())).empty(),
          "raw-off sequence emits no raw run, hence no File events");
}
void nested_dispatch() {
  Session session;
  const auto target = session.directory.path / "nested.txt";
  const std::string content{"nested source\n"};
  fixture_file(target, content);
  const auto records = session.capture([&] {
    AuditLog log{*session.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    const auto result = engine.operator_call(
        "lua",
        Value::object({{"code", Value{"return blackbird.call('read_file',{path=" +
                                      lua_path(target) + "})"}}}));
    require(string_field(result, "content") == content,
            "nested read returns the bytes");
  });
  paired(records);
  const auto calls = tools(records);
  require(calls.size() == 1, "one File tool for the nested read");
  file_call(records, *calls[0], [&] {
    Expected e{};
    e.name = "read_file";
    e.method = RawFileMethod::read;
    e.arguments = {{0, target.string()}};
    e.source = content;
    e.observers = 1;
    e.count = content.size();
    return e;
  }());
  ByteView tool{calls[0]->bytes};
  const auto inner = reference(tool);
  ByteView started{named(records, inner)->bytes};
  require(read<bool>(started), "inner operation has a parent");
  const auto outer = reference(started);
  const auto *outer_record = named(records, outer);
  require(outer_record && outer_record->site == 2400 &&
              operation_name(records, outer) == "lua",
          "inner read_file operation nests under the outer lua operation");
}
void missing_directory() {
  Session session;
  const auto missing = session.directory.path / "nope" / "x";
  const auto records = session.capture([&] {
    AuditLog log{*session.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    const auto result = engine.operator_call(
        "write_file", Value::object({{"path", Value{missing.string()}},
                                     {"content", Value{"never"}}}));
    require(string_field(result, "error") == "io", "missing directory write fails");
  });
  require(!std::filesystem::exists(missing.parent_path()), "nothing created on disk");
  paired(records);
  const auto calls = tools(records);
  require(calls.size() == 1, "one File tool for the refused write");
  file_call(records, *calls[0], [&] {
    Expected e{};
    e.name = "write_file";
    e.method = RawFileMethod::write;
    e.arguments = {{0, missing.string()}, {3, "never"}};
    e.proposal = "never";
    e.observers = 1;
    e.error = Error{ErrorCode::io, ENOENT};
    e.write = Write{missing.string(), false, ENOENT};
    return e;
  }());
  std::size_t stats = 0;
  for (const auto &record : records) {
    if (record.site != 3424)
      continue;
    ByteView bytes{record.bytes};
    (void)reference(bytes);
    (void)reference(bytes);
    require(read<std::uint8_t>(bytes) == 3, "native stat method");
    const auto extent = read<std::size_t>(bytes);
    require(extent == bytes.size() && text(bytes) == cstring(missing.string()),
            "stat operand is the literal path");
    ByteView returned{answer(records, 3425, record.reference).bytes};
    (void)reference(returned);
    require(read<int>(returned) == -1 && read<bool>(returned) &&
                read<int>(returned) == ENOENT && returned.empty(),
            "stat of the missing path fails with ENOENT");
    ++stats;
  }
  require(stats != 0, "missing destination is stat-ed natively");
  require(count(records, 3490) == 0 && count(records, 3434) == 0 &&
              count(records, 3438) == 0 && count(records, 3444) == 0,
          "refused temporary acquires no resource");
}
void overwrite_dispatch() {
  Session session;
  const auto target = session.directory.path / "overwrite.txt";
  const std::string before{"old bytes\n"}, after{"new bytes!\n"};
  fixture_file(target, before);
  const auto records = session.capture([&] {
    AuditLog log{*session.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    const auto result = engine.operator_call(
        "write_file",
        Value::object({{"path", Value{target.string()}}, {"content", Value{after}}}));
    require(field(result, "written") == Value{true} && disk(target) == after,
            "overwrite applied the literal bytes");
  });
  paired(records);
  const auto calls = tools(records);
  require(calls.size() == 1, "one File tool for the overwrite");
  file_call(records, *calls[0], [&] {
    Expected e{};
    e.name = "write_file";
    e.method = RawFileMethod::write;
    e.arguments = {{0, target.string()}, {3, after}};
    e.source = before;
    e.proposal = after;
    e.observers = 2;
    e.count = after.size();
    e.write = Write{resolved_path(target), true};
    return e;
  }());
}
void authored_dispatch() {
  Session session;
  const auto target = session.directory.path / "authored.txt";
  const auto missing = session.directory.path / "missing.txt";
  const std::string content{"authored\0source\n", 16};
  const auto program = "local p, q = " + lua_path(target) + ", " + lua_path(missing) +
                       R"lua(
local w = blackbird.call('write_file', {path = p, content = 'authored\0source\n'})
local r = blackbird.call('read_file', {path = p})
local e = blackbird.call('edit_file', {path = p, old = 'absent', new = 'x'})
local m = blackbird.call('read_file', {path = q})
return {w = w.written, r = r.content, e = e.error, m = m.error}
)lua";
  const auto records = session.capture([&] {
    AuditLog log{*session.root};
    ContextStore context{log};
    Unused provider;
    CodingEngine engine{log, context, provider, "native.fixture"};
    engine.turn({"", program});
    const auto &result = engine.workflow_result();
    require(field(result, "w") == Value{true} && field(result, "r") == Value{content} &&
                string_field(result, "e") == "conflict" &&
                string_field(result, "m") == "io",
            "authored Lua business returns are unchanged");
    require(disk(target) == content && !std::filesystem::exists(missing),
            "failed edit and missing read leave the files unchanged");
  });
  paired(records);
  const auto calls = tools(records);
  require(calls.size() == 4, "one File tool per authored dispatch");
  file_call(records, *calls[0], [&] {
    Expected e{};
    e.name = "write_file";
    e.method = RawFileMethod::write;
    e.arguments = {{0, target.string()}, {3, content}};
    e.proposal = content;
    e.observers = 1;
    e.count = content.size();
    e.write = Write{target.string()};
    return e;
  }());
  file_call(records, *calls[1], [&] {
    Expected e{};
    e.name = "read_file";
    e.method = RawFileMethod::read;
    e.arguments = {{0, target.string()}};
    e.source = content;
    e.observers = 1;
    e.count = content.size();
    return e;
  }());
  file_call(records, *calls[2], [&] {
    Expected e{};
    e.name = "edit_file";
    e.method = RawFileMethod::edit;
    e.arguments = {{0, target.string()}, {1, "absent"}, {2, "x"}};
    e.source = content;
    e.observers = 1;
    e.error = Error{ErrorCode::conflict};
    e.search = std::string::npos;
    return e;
  }());
  file_call(records, *calls[3], [&] {
    Expected e{};
    e.name = "read_file";
    e.method = RawFileMethod::read;
    e.arguments = {{0, missing.string()}};
    e.error = Error{ErrorCode::io, ENOENT};
    return e;
  }());
  // The failed open is an actual native request/result pair carrying ENOENT.
  std::size_t refused = 0;
  for (const auto &record : records) {
    if (record.site != 3421)
      continue;
    ByteView bytes{record.bytes};
    const auto *open = named(records, reference(bytes));
    require(open && open->site == 3420, "open return names its open");
    if (read<int>(bytes) >= 0)
      continue;
    require(read<bool>(bytes) && read<int>(bytes) == ENOENT && bytes.empty(),
            "failed open copies the native errno");
    ++refused;
  }
  require(refused == 1, "only the missing path refuses native open");
  require(count(records, 3430) == 1, "only the applied write acquires a temporary");
}
void model_dispatch() {
  Session session;
  const auto target = session.directory.path / "model.txt";
  fixture_file(target, "model source\n");
  const auto records = session.capture([&] {
    AuditLog log{*session.root};
    ContextStore context{log};
    ToolModel provider;
    provider.calls = {function_call("edit-1", "edit_file",
                                    Value::object({{"path", Value{target.string()}},
                                                   {"old", Value{"source"}},
                                                   {"new", Value{"edited"}}})),
                      function_call("read-1", "read_file",
                                    Value::object({{"path", Value{target.string()}}}))};
    CodingEngine engine{log, context, provider, "native.fixture"};
    engine.turn({"edit then read the source", workflow()});
    require(provider.requests == 2 && disk(target) == "model edited\n",
            "model tool calls applied and the turn completed");
  });
  paired(records);
  const auto calls = tools(records);
  require(calls.size() == 2, "one File tool per model tool call");
  file_call(records, *calls[0], [&] {
    Expected e{};
    e.name = "edit_file";
    e.method = RawFileMethod::edit;
    e.arguments = {{0, target.string()}, {1, "source"}, {2, "edited"}};
    e.source = "model source\n";
    e.proposal = "model edited\n";
    e.observers = 2;
    e.count = 13;
    e.search = 6;
    e.write = Write{resolved_path(target), true};
    return e;
  }());
  file_call(records, *calls[1], [&] {
    Expected e{};
    e.name = "read_file";
    e.method = RawFileMethod::read;
    e.arguments = {{0, target.string()}};
    e.source = "model edited\n";
    e.observers = 1;
    e.count = 13;
    return e;
  }());
}
// Cancellation observed after an applied write stops the turn before the
// next tool; the completed File call keeps its whole surfaces.
void cancelled_after_effect() {
  Session session;
  const auto target = session.directory.path / "cancelled.txt";
  const std::string content{"kept\n"};
  const auto records = session.capture([&] {
    AuditLog log{*session.root};
    ContextStore context{log};
    ToolModel provider;
    provider.calls = {function_call("write-1", "write_file",
                                    Value::object({{"path", Value{target.string()}},
                                                   {"content", Value{content}}})),
                      function_call("read-1", "read_file",
                                    Value::object({{"path", Value{target.string()}}}))};
    CodingEngine engine{log, context, provider, "native.fixture"};
    engine.cancelled = [&] { return std::filesystem::exists(target); };
    bool interrupted = false;
    try {
      engine.turn({"write then read", workflow()});
    } catch (const Error &error) {
      interrupted = error.code == ErrorCode::interrupted;
    }
    require(interrupted && provider.requests == 1 && disk(target) == content,
            "cancellation after the applied write interrupts the turn");
  });
  paired(records);
  const auto calls = tools(records);
  require(calls.size() == 1, "cancelled read never begins a File tool");
  file_call(records, *calls[0], [&] {
    Expected e{};
    e.name = "write_file";
    e.method = RawFileMethod::write;
    e.arguments = {{0, target.string()}, {3, content}};
    e.proposal = content;
    e.observers = 1;
    e.count = content.size();
    e.write = Write{target.string()};
    return e;
  }());
}
} // namespace

int main(int argc, char **argv) {
  constexpr std::array<std::pair<std::string_view, void (*)()>, 7> cases{
      {{"operator", operator_dispatch},
       {"nested", nested_dispatch},
       {"missing", missing_directory},
       {"overwrite", overwrite_dispatch},
       {"authored", authored_dispatch},
       {"model", model_dispatch},
       {"cancelled", cancelled_after_effect}}};
  try {
    for (const auto &[name, run] : cases) {
      if (argc == 2 && name != argv[1])
        continue;
      std::cerr << name << " dispatch\n";
      run();
    }
    std::cout << "coding File raw dispatch passed\n";
    return 0;
  } catch (const Error &error) {
    std::cerr << error_name(error.code) << ':' << error.detail << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
