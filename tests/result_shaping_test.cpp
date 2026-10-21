#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include <algorithm>
#include <charconv>
#include <deque>
#include <iostream>
#include <source_location>
#include <unistd.h>
using namespace blackbird;
// T1 (design §9): a result is shaped once, where it becomes visible to the model: the
// output of the model's own call, a drained continuation, a host observation. A
// program's own calls return whole results. Over its budget a result's text (a string
// as is, otherwise its JSON) comes back as its first head bytes, one marker line
// naming the omitted half-open range, the total and the result_ref of the retained
// original, and its last bytes. Defaults 16384 = 4096 head + 12288 tail. Expected
// values derive from that rule over the original text read back through result_ref.
namespace {
void check(bool condition,
           std::source_location where = std::source_location::current()) {
  if (!condition) {
    std::cerr << "result shaping line " << where.line() << '\n';
    throw std::runtime_error("result shaping line " + std::to_string(where.line()));
  }
}
template <class T> T id(unsigned char byte) {
  IdentityBytes bytes{};
  bytes[0] = static_cast<std::byte>(byte);
  return unwrap(T::from_bytes(bytes));
}
struct Marker {
  std::size_t head = 0, tail = 0, total = 0, end = 0; // end: first tail byte in text
  std::string ref;
  std::size_t count = 0; // markers in the text
};
const std::string lead = "\n[... bytes [";
// Parses "\n[... bytes [h,t) of n omitted; read_process_output with result_ref R and
// range {mode: bytes, start: h, end: t} pages them, 8192 bytes each with next_read
// ...]\n" and checks its wording.
Marker marker(const Value &shaped) {
  check(std::holds_alternative<std::string>(shaped.value()));
  const auto &text = shaped.string();
  const auto at = text.find(lead);
  check(at != std::string::npos);
  Marker m;
  for (auto next = at; next != std::string::npos; next = text.find(lead, next + 1))
    ++m.count;
  m.head = at;
  const auto *cursor = text.data() + at + lead.size();
  const auto *last = text.data() + text.size();
  std::size_t from = 0;
  cursor = std::from_chars(cursor, last, from).ptr;
  check(from == at && *cursor == ',');
  cursor = std::from_chars(cursor + 1, last, m.tail).ptr;
  const std::string of = ") of ";
  check(std::string_view{cursor, of.size()} == of);
  cursor = std::from_chars(cursor + of.size(), last, m.total).ptr;
  const std::string named = " omitted; read_process_output with result_ref ";
  check(std::string_view{cursor, named.size()} == named);
  m.ref.assign(cursor + named.size(), 32);
  check(m.ref.find_first_not_of("0123456789abcdef") == std::string::npos);
  const auto rest = " and range {mode: bytes, start: " + std::to_string(m.head) +
                    ", end: " + std::to_string(m.tail) +
                    "} pages them, 8192 bytes each with next_read ...]\n";
  check(std::string_view{cursor + named.size() + 32, rest.size()} == rest);
  m.end =
      static_cast<std::size_t>(cursor - text.data()) + named.size() + 32 + rest.size();
  return m;
}
// The retained original's text, read back whole by the operator in one page.
std::string original(CodingEngine &engine, const std::string &ref) {
  const auto whole = engine.operator_call(
      "read_process_output",
      Value::object(
          {{"result_ref", Value{ref}}, {"output_max_bytes", Value{Number{16777216}}}}));
  check(field(whole, "result_ref") == Value{ref} && !whole.find("next_read"));
  return string_field(whole, "output");
}
// The shaped value equals the rule applied to the original text with these cuts.
Marker shaped_as(CodingEngine &engine, const Value &shaped, std::size_t head,
                 std::size_t tail_start) {
  const auto m = marker(shaped);
  const auto text = original(engine, m.ref);
  check(m.count == 1);
  check(m.head == head && m.tail == tail_start && m.total == text.size());
  const auto &got = shaped.string();
  check(got.substr(0, m.head) == text.substr(0, head));
  check(got.substr(m.end) == text.substr(tail_start));
  return m;
}
// Objects compared without their key order, which a Lua round trip changes.
Value sorted(Value value) {
  if (std::holds_alternative<Value::Object>(value.value())) {
    auto &object = value.object();
    for (auto &entry : object)
      entry.second = sorted(std::move(entry.second));
    std::sort(object.begin(), object.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
  } else if (std::holds_alternative<Value::Array>(value.value()))
    for (auto &item : value.array())
      item = sorted(std::move(item));
  return value;
}
bool utf8_start(const std::string &text, std::size_t at) {
  return at >= text.size() || (static_cast<unsigned char>(text[at]) & 0xc0U) != 0x80U;
}
std::string colleague_upstream(const std::string &answer) {
  const auto inner = Value::object(
      {{"type", Value{"result"}},
       {"subtype", Value{"success"}},
       {"is_error", Value{false}},
       {"result", Value{answer}},
       {"modelUsage", Value::object({{"actual-claude", Value::object({})}})}});
  return unwrap(dump_json(Value::object(
      {{"exit_code", Value{Number{0}}}, {"output", Value{unwrap(dump_json(inner))}}})));
}
// A Lua expression that decodes to value.
std::string lua_value(const Value &value) {
  return "blackbird.decode([==[" + unwrap(dump_json(value)) + "]==])";
}
Value colleague_request(Value::Object budget = {}) {
  auto request = unwrap(parse_json(
      R"({"request_id":"one","from":"main","to":"reviewer","provider":"claude","model":"sonnet","task":"Review","context":[{"id":"source","text":"SELECTED"}],"profile":{"name":"context-only","provenance":"test","timeout_seconds":2,"tools":"none","requests":1}})"));
  for (auto &entry : budget)
    request.object().push_back(std::move(entry));
  return request;
}
Value number(std::size_t n) { return Value{Number{n}}; }
Value code(std::string source) {
  return Value::object({{"code", Value{std::move(source)}}});
}
// Each request takes the next scripted step, which sees the request's input and
// returns the calls the model makes (none ends the turn).
using Step = std::function<Value::Array(const Value::Array &input)>;
class Provider final : public CodingProvider {
public:
  std::deque<Step> steps;
  std::vector<Value::Array> inputs;
  Value respond(const Value &request,
                const std::function<void(std::string_view)> &) override {
    inputs.push_back(field(request, "input").array());
    Value::Array output;
    if (!steps.empty()) {
      output = steps.front()(inputs.back());
      steps.pop_front();
    }
    return Value::object({{"output", Value{std::move(output)}}});
  }
};
Value call(std::string id, const char *name, const Value &args) {
  return Value::object({{"type", Value{"function_call"}},
                        {"call_id", Value{std::move(id)}},
                        {"name", Value{name}},
                        {"arguments", Value{unwrap(dump_json(args))}}});
}
std::map<std::string, Value> outputs(const Value::Array &input) {
  std::map<std::string, Value> found;
  for (const auto &item : input)
    if (const auto *type = item.find("type");
        type && *type == Value{"function_call_output"})
      found[string_field(item, "call_id")] = field(item, "output");
  return found;
}
std::string developer(const Value::Array &input, std::string_view prefix) {
  std::string found;
  for (const auto &item : input)
    if (const auto *role = item.find("role"); role && *role == Value{"developer"})
      if (string_field(item, "content").starts_with(prefix))
        found = string_field(item, "content");
  return found;
}
// The shape the terminal attempt observation records for each settled operation.
std::vector<Value> recorded_shapes(RetainedState &root) {
  std::vector<Value> found;
  for (const auto &fact : root.committed_facts())
    if (const auto *seen = std::get_if<AttemptObservationEvent>(&fact.event.body)) {
      const auto packet = decode_packet(seen->observation);
      if (packet.has_value())
        if (packet.value().find("shape"))
          found.push_back(packet.value());
    }
  return found;
}
} // namespace
int main() {
  char name[] = "/tmp/blackbird-result-shaping-XXXXXX";
  if (!mkdtemp(name))
    return 2;
  const std::filesystem::path path{name};
  try {
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(unwrap(
                                         NativeJournalDirectory::open(path.string()))),
                                     "audit",
                                     {id<EnvironmentId>(1),
                                      id<AuditStreamId>(2),
                                      7,
                                      {4 * 1024 * 1024, 16 * 1024 * 1024},
                                      std::nullopt},
                                     {512 * 1024 * 1024, 80000}));
    AuditLog log{*root};
    ContextStore context{log};
    Provider provider;
    CodingEngine engine{log, context, provider, "test-model"};
    engine.diagnostic = [](std::string_view v) { std::cerr << v; };
    const std::string answer(40000, 'c');
    std::size_t dispatches = 0;
    std::size_t fail_at = 0; // the dispatch whose transport fails
    engine.colleague_transport = [&](const Value &, const ColleagueCapture &,
                                     const RawAdapterContext *) {
      ++dispatches;
      if (dispatches == fail_at)
        throw Error{ErrorCode::provider_transport};
      return unwrap(parse_json(colleague_upstream(answer)));
    };
    const auto workflow = [](const std::string &name, std::string source) {
      return Value::object(
          {{"name", Value{name}},
           {"description", Value{"T1 fixture"}},
           {"source", Value{std::move(source)}},
           {"aliases", Value{Value::Array{Value{name}}}},
           {"bare", Value{true}},
           {"powerwords",
            Value{Value::Array{Value::object(
                {{"token", Value{name + "word"}}, {"color", Value{"keyword"}}})}}}});
    };
    // 12027 module source bytes whose JSON, every LF escaped, is over 16 KiB, so
    // program_config's result is too.
    std::string lines = "local x = 0\n";
    for (int n = 0; n < 2000; ++n)
      lines += "x=x+1\n";
    const auto module = Value::object(
        {{"name", Value{"big"}}, {"source", Value{lines + "return {size = x}"}}});
    const auto proposal = Value::object(
        {{"proposal",
          Value::object(
              {{"modules", Value{Value::Array{module}}},
               {"model", Value{""}},
               {"effort", Value{""}},
               {"workflows",
                Value{Value::Array{workflow("long", "return string.rep('W', 40000)"),
                                   workflow("nul",
                                            "local t = {} for i = 1, 200000 do t[i] = "
                                            "'\\255' end return t"),
                                   // A sub-agent: its own model round, the relayed
                                   // result returned as its whole result.
                                   workflow("sub", R"lua(local r = blackbird.request()
local res
for _, item in ipairs(r.output) do
  if item.type == 'function_call' then
    res = blackbird.call(item.name, item.arguments, {call_id = item.call_id})
    blackbird.append({{type = 'function_call_output', call_id = item.call_id, output = res}})
  end
end
return res)lua")}}}})}});
    engine.turn({"", "local r = blackbird.call('program_config', " +
                         lua_value(proposal) +
                         "); assert(type(r) == 'table' and r.staged)"});

    // 1. A program's own calls return whole results: the documented CAS recipe
    // keeps its base, the helpers and modules read their fields.
    engine.turn({"", "local view = blackbird.call('program_config', {}) "
                     "local stale = blackbird.call('program_config', "
                     "{base = 'stale', proposal = view.effective}) "
                     "local pending = blackbird.call('program_config', "
                     "{base = view.revision, proposal = view.effective}) "
                     "local r = blackbird.colleague(" +
                         lua_value(colleague_request()) +
                         ") return {kind = type(view), revision = view.revision, "
                         "staged = pending.staged, stale = stale.staged == nil, "
                         "text = r.text, size = blackbird.module('big').size, "
                         "nested = blackbird.call('lua', {code = \"return "
                         "string.rep('L', 40000)\"})}"});
    {
      const auto &program = engine.workflow_result();
      check(string_field(program, "kind") == "table" &&
            string_field(program, "revision").size() == 32 &&
            field(program, "staged") == Value{true} &&
            field(program, "stale") == Value{true});
      check(string_field(program, "text") == answer &&
            field(program, "size") == Value{Number{2000}} &&
            field(program, "nested") == Value{std::string(40000, 'L')});
    }

    // 2. The model's own calls, through the default workflow.
    const auto escapes = [](std::string_view rep) {
      return code("return {text = string.rep(" + std::string{rep} + ")}");
    };
    Value::Array first, second, third;
    provider.steps.push_back([&](const Value::Array &) {
      return Value::Array{
          call("default", "colleague", colleague_request()),
          call("split", "colleague",
               colleague_request({{"output_max_bytes", number(30000)},
                                  {"output_head_bytes", number(10000)}})),
          call("first", "colleague",
               colleague_request({{"output_max_bytes", number(20000)}})),
          call("head", "colleague",
               colleague_request({{"output_head_bytes", number(8000)}})),
          call("whole", "colleague",
               colleague_request({{"output_max_bytes", number(16777216)}})),
          call("bad", "colleague",
               colleague_request({{"output_max_bytes", number(10)},
                                  {"output_head_bytes", number(11)}})),
          call("relay", "lua",
               code("return blackbird.call('colleague', " +
                    lua_value(colleague_request()) + ")")),
          call("big", "lua", code("return string.rep('B', 1048576)")),
          call("snow", "lua", code("return string.rep('\\226\\152\\131', 13334)")),
          call("quote", "lua", escapes("'\"', 20000")),
          call("control", "lua", escapes("'\\1', 8000")),
          call("slash", "lua", escapes("'\\\\', 20000")),
          call("just", "lua", code("return string.rep('j', 16400)")),
          call("nul", "lua",
               code("local t = {} for i = 1, 200000 do t[i] = '\\255' end return t")),
          call("noref", "read_process_output", Value::object({})),
          call("continued", "workflow_invoke",
               Value::object({{"name", Value{"long"}}, {"prompt", Value{"go"}}})),
          call("unproducible", "workflow_invoke",
               Value::object({{"name", Value{"nul"}}, {"prompt", Value{"go"}}}))};
    });
    Marker big;
    provider.steps.push_back([&](const Value::Array &input) {
      first = input;
      // Follow the marker exactly as it suggests.
      big = marker(outputs(input).at("big"));
      // A failure in a round of its own, so the failed selector can match it only
      // through its own recorded status.
      return Value::Array{
          call("follow", "read_process_output",
               Value::object({{"result_ref", Value{big.ref}},
                              {"range", Value::object({{"mode", Value{"bytes"}},
                                                       {"start", number(big.head)},
                                                       {"end", number(big.tail)}})}})),
          call("failing", "lua",
               code("return {error = 'boom', log = string.rep('e', 40000)}"))};
    });
    provider.steps.push_back([&](const Value::Array &input) {
      second = input;
      return Value::Array{};
    });
    engine.turn({"review", read_file("programs/turn.lua")});
    check(provider.inputs.size() == 3 && dispatches == 7); // "bad" never dispatches
    const auto shown = outputs(first);
    // A 40 KB answer: 4096 head + marker + 12288 tail of its JSON text.
    const auto colleague = shaped_as(engine, shown.at("default"), 4096,
                                     marker(shown.at("default")).total - 12288);
    const auto text = original(engine, colleague.ref);
    const auto answered = unwrap(parse_json(text));
    check(string_field(answered, "text") == answer &&
          string_field(answered, "status") == "completed");
    // Explicit budgets, exactly: split, head-first, head alone, whole.
    (void)shaped_as(engine, shown.at("split"), 10000, text.size() - 20000);
    (void)shaped_as(engine, shown.at("first"), 20000, text.size());
    (void)shaped_as(engine, shown.at("head"), 8000, text.size() - 12288);
    check(sorted(shown.at("whole")) == sorted(answered)); // keys never reached it
    check(string_field(shown.at("bad"), "error") == "invalid_range");
    // A lua call relaying a nested colleague call: one marker, naming the colleague's
    // own omitted bytes (the nested call was whole).
    const auto relayed =
        shaped_as(engine, shown.at("relay"), 4096, text.size() - 12288);
    check(sorted(unwrap(parse_json(original(engine, relayed.ref)))) ==
          sorted(answered));
    // Cuts keep characters and JSON escapes whole.
    {
      const auto raw = original(engine, marker(shown.at("snow")).ref);
      check(raw.size() == 40002);
      auto head = std::size_t{4096}, tail = raw.size() - 12288;
      while (!utf8_start(raw, head))
        --head;
      while (!utf8_start(raw, tail))
        ++tail;
      check(head != 4096 || tail != raw.size() - 12288); // a cut was mid-character
      (void)shaped_as(engine, shown.at("snow"), head, tail);
    }
    for (const auto &[key, width] :
         {std::pair{"quote", std::size_t{2}}, std::pair{"control", std::size_t{6}},
          std::pair{"slash", std::size_t{2}}}) {
      const auto &value = shown.at(key);
      const auto raw = original(engine, marker(value).ref);
      const std::size_t p = std::string_view{R"({"text":")"}.size();
      const auto head = p + (4096 - p) / width * width;
      const auto tail = p + (raw.size() - 12288 - p + width - 1) / width * width;
      check(head != 4096 || tail != raw.size() - 12288);
      (void)shaped_as(engine, value, head, tail);
    }
    // Just over the budget the cut would not be shorter: the result stays whole.
    check(shown.at("just") == Value{std::string(16400, 'j')});
    // No text form within the provider limits: an error naming the reason.
    check(string_field(shown.at("nul"), "error") == "result_text_unavailable" &&
          string_field(shown.at("nul"), "reason") == "capacity");
    check(string_field(shown.at("noref"), "error") == "invalid_range");
    // The continuation over 16 KiB is shaped in its developer item; one whose text
    // cannot be produced is an error, and the drain goes on.
    const std::string long_lead = "Registered workflow long completed in this turn: ";
    const auto continuation = developer(first, long_lead);
    check(!continuation.empty());
    const auto continued = Value{continuation.substr(long_lead.size())};
    (void)shaped_as(engine, continued, 4096, 40000 - 12288);
    check(original(engine, marker(continued).ref) == std::string(40000, 'W'));
    const std::string nul_lead = "Registered workflow nul completed in this turn: ";
    const auto nul = developer(first, nul_lead);
    check(nul.find("\"error\":\"result_text_unavailable\"") != std::string::npos &&
          nul.find("\"reason\":\"capacity\"") != std::string::npos);
    // Following the marker on a 1 MiB result appends one bounded page.
    const auto page = outputs(second).at("follow");
    check(field(page, "returned_bytes") == number(8192) &&
          field(page, "byte_start") == number(big.head) &&
          string_field(page, "output") == std::string(8192, 'B'));
    const auto &next = field(field(page, "next_read"), "arguments");
    check(field(next, "result_ref") == Value{big.ref} &&
          field(next, "byte_start") == number(big.head + 8192) &&
          field(next, "byte_end") == number(big.head + 16384));
    // The failed selector reads the recorded status of a shaped failure.
    const auto pinned = context.pin("failed");
    std::string failing;
    for (const auto &unit : context.units()) {
      for (const auto position : unit.entries) {
        const auto &item =
            field(field(context.view(), "entries").array()[position], "item");
        if (const auto *id_value = item.find("call_id");
            id_value && *id_value == Value{"failing"})
          failing = unit.handle;
      }
    }
    check(!failing.empty() &&
          std::holds_alternative<std::string>(outputs(second).at("failing").value()));
    const auto &accepted = field(pinned, "accepted").array();
    check(std::find(accepted.begin(), accepted.end(), Value{failing}) !=
          accepted.end());
    (void)context.unpin("failed");

    // 3. The applied shape is recorded in each boundary's settlement.
    {
      const auto shapes = recorded_shapes(*root);
      const auto by_call = [&](std::string_view call_id) {
        for (const auto &packet : shapes)
          if (const auto *found = packet.find("call_id");
              found && *found == Value{std::string{call_id}})
            return field(packet, "shape");
        check(false);
        return Value{};
      };
      check(by_call("default") ==
            Value::object(
                {{"max_bytes", number(16384)},
                 {"head_bytes", number(4096)},
                 {"tail_bytes", number(12288)},
                 {"text_bytes", number(text.size())},
                 {"omitted", Value::object({{"start", number(4096)},
                                            {"end", number(text.size() - 12288)}})},
                 {"result_ref", Value{colleague.ref}}}));
      check(by_call("first").find("head_bytes") == nullptr &&
            field(by_call("first"), "tail_bytes") == number(0));
      check(field(by_call("just"), "whole") == Value{true});
      check(field(by_call("failing"), "failed") == Value{true});
      check(field(by_call("nul"), "error") == Value{"capacity"});
      // The continuation's own operation records its shape, without a call_id.
      bool continuation_recorded = false;
      for (const auto &packet : shapes) {
        const auto *ref = field(packet, "shape").find("result_ref");
        if (!packet.find("call_id") && ref && *ref == Value{marker(continued).ref})
          continuation_recorded = true;
      }
      check(continuation_recorded);
    }

    // 4. The operator's outermost call stays exact, its workflow invocation too; a
    // call nested in it is a program call and whole.
    check(sorted(engine.operator_call("colleague", colleague_request())) ==
          sorted(answered));
    check(engine.operator_turn("/long", "error('fallback must not run')") &&
          engine.workflow_result() == Value{std::string(40000, 'W')});
    const auto nested =
        engine.operator_call("lua", code("return blackbird.call('colleague', " +
                                         lua_value(colleague_request()) + ")"));
    check(sorted(nested) == sorted(answered));

    // 5. Lua-defined tools: arguments are the tool's own; shape is its definition's,
    // applied when the tool answers the model's call.
    engine.turn({"", R"lua(
local function tool(name, shape)
  local d = {name = name, description = 'Return output_max_bytes letters',
    parameters = {type = 'object', properties = {output_max_bytes = {type = 'integer'}},
      required = blackbird.array({'output_max_bytes'}), additionalProperties = false},
    source = "return string.rep('E', args.output_max_bytes)", shape = shape}
  return blackbird.define_tool(d).staged
end
assert(tool('echo_bytes'))
assert(tool('echo_whole', {output_max_bytes = 16777216}))
assert(tool('echo_tail', {output_max_bytes = 1000, output_head_bytes = 0}))
assert(not tool('bad_key', {output_max = 1}))
assert(not tool('bad_head', {output_max_bytes = 10, output_head_bytes = 11}))
assert(not tool('bad_kind', 7))
-- A registry over 16 KiB of JSON.
assert(blackbird.define_tool({name = 'big_tool', description = 'Return one',
  parameters = {type = 'object', properties = {}, required = blackbird.array({}),
    additionalProperties = false},
  source = 'return 1 --' .. string.rep('x', 16000)}).staged)
)lua"});
    const auto own = Value::object({{"output_max_bytes", number(30000)}});
    provider.steps.push_back([&](const Value::Array &) {
      return Value::Array{call("tool-bytes", "echo_bytes", own),
                          call("tool-whole", "echo_whole", own),
                          call("tool-tail", "echo_tail", own)};
    });
    provider.steps.push_back([&](const Value::Array &input) {
      third = input;
      return Value::Array{};
    });
    engine.turn({"tools", read_file("programs/turn.lua")});
    const auto tools = outputs(third);
    // The tool received its own 30000; the harness cut by the default shape.
    (void)shaped_as(engine, tools.at("tool-bytes"), 4096, 30000 - 12288);
    check(tools.at("tool-whole") == Value{std::string(30000, 'E')});
    (void)shaped_as(engine, tools.at("tool-tail"), 0, 29000);
    // Called by a program, the same tool returns whole.
    engine.turn(
        {"", "return blackbird.call('echo_bytes', {output_max_bytes = 30000})"});
    check(engine.workflow_result() == Value{std::string(30000, 'E')});

    // 6. A relay declares the call it answers: blackbird.call(name, args, {call_id}).
    const auto nul_ref = string_field(shown.at("nul"), "result_ref");
    Value::Array relay_end;
    provider.steps.push_back([&](const Value::Array &) {
      return Value::Array{call("budgeted", "colleague", colleague_request()),
                          call("retried", "colleague", colleague_request()),
                          call("registry", "tool_registry", Value::object({})),
                          call("declined", "colleague", colleague_request())};
    });
    provider.steps.push_back([&](const Value::Array &) {
      return Value::Array{
          // The model retries the declined call identically.
          call("r2", "colleague", colleague_request()),
          call("nulread", "read_process_output",
               Value::object({{"result_ref", Value{nul_ref}}})),
          call("sibling", "lua", code("return 'still here'")),
          call("emptyref", "read_process_output",
               Value::object({{"result_ref", Value{""}}})),
          call(
              "lastpage", "read_process_output",
              Value::object({{"result_ref", Value{big.ref}},
                             {"range", Value::object({{"mode", Value{"bytes"}},
                                                      {"start", number(big.tail - 100)},
                                                      {"end", number(big.tail)}})}})),
          call("headpage", "read_process_output",
               Value::object({{"result_ref", Value{big.ref}},
                              {"output_head_bytes", number(9000)}}))};
    });
    provider.steps.push_back([&](const Value::Array &input) {
      relay_end = input;
      return Value::Array{};
    });
    const auto before = dispatches;
    fail_at = before + 2; // the first attempt of "retried"
    engine.turn({"relay", R"lua(
local seen = {}
for step = 1, 8 do
  local r = blackbird.request(); local calls = 0
  for _, item in ipairs(r.output) do
    if item.type == 'function_call' then
      calls = calls + 1
      local args, declare = item.arguments, {call_id = item.call_id}
      local result
      if item.call_id == 'declined' then
        result = 'declined by the relay'
      else
        -- An edited relay may rewrite arguments: declared, it stays the model's call.
        if item.call_id == 'budgeted' then args.output_max_bytes = 8192 end
        if item.call_id == 'retried' then
          local first = blackbird.call(item.name, args, declare)
          seen.first = tostring(first.status or first.error)
        end
        -- The program's own identical call, while the relay is in flight.
        if item.call_id == 'registry' then
          seen.own = type(blackbird.call('tool_registry', {}))
        end
        result = blackbird.call(item.name, args, declare)
      end
      blackbird.append({{type = 'function_call_output', call_id = item.call_id,
        output = result}})
      if item.call_id == 'budgeted' then
        seen.answered = blackbird.call('colleague', item.arguments, declare).reason
        seen.unknown = blackbird.call('colleague', item.arguments, {call_id = 'nope'}).reason
      end
    end
  end
  if calls == 0 then return seen end
end)lua"});
    {
      const auto seen = engine.workflow_result();
      const auto by_relay = outputs(relay_end);
      // Declared with an added budget: shaped head-first at 8192 bytes.
      (void)shaped_as(engine, by_relay.at("budgeted"), 8192, text.size());
      // A retry after a failed dispatch is still the model's call.
      check(string_field(seen, "first") == "unknown");
      (void)shaped_as(engine, by_relay.at("retried"), 4096, text.size() - 12288);
      // The program's own identical call is whole; the model's is shaped.
      check(string_field(seen, "own") == "table");
      (void)marker(by_relay.at("registry"));
      // An answered or unknown call_id is refused, not run.
      check(string_field(seen, "answered") == "call_id_answered" &&
            string_field(seen, "unknown") == "call_id_unknown");
      check(dispatches - before == 4); // budgeted, retried twice, r2
      // r2, the retry of the declined r1, is recorded as r2.
      bool r2 = false, declined = false;
      for (const auto &packet : recorded_shapes(*root)) {
        const auto *recorded = packet.find("call_id");
        r2 = r2 || (recorded && *recorded == Value{"r2"});
        declined = declined || (recorded && *recorded == Value{"declined"});
      }
      check(r2 && !declined);
      (void)shaped_as(engine, by_relay.at("r2"), 4096, text.size() - 12288);
      // A result with no text form has no pages: a recorded refusal, and the round's
      // other calls survive.
      check(string_field(by_relay.at("nulread"), "error") == "unsupported" &&
            by_relay.at("sibling") == Value{"still here"});
      check(string_field(by_relay.at("emptyref"), "error") == "invalid_range");
      // The last page of the omitted range offers nothing into the tail.
      check(field(by_relay.at("lastpage"), "returned_bytes") == number(100) &&
            !by_relay.at("lastpage").find("next_read"));
      // A head alone keeps the 6144-byte tail after it, as an output_ref read does.
      check(field(by_relay.at("headpage"), "returned_bytes") == number(9000 + 6144) &&
            field(by_relay.at("headpage"), "byte_end") == number(9000));
    }
    engine.turn({"", "local ok, r = pcall(blackbird.call, 'read_process_output', "
                     "{result_ref = '" +
                         nul_ref + "'}) return {ok = ok, error = r.error}"});
    check(field(engine.workflow_result(), "ok") == Value{true} &&
          string_field(engine.workflow_result(), "error") == "unsupported");

    // 7. A sub-agent's relayed result, returned whole, passes through uncut.
    Value::Array sub_end;
    provider.steps.push_back([&](const Value::Array &) {
      return Value::Array{
          call("subagent", "workflow_invoke", Value::object({{"name", Value{"sub"}}}))};
    });
    provider.steps.push_back([&](const Value::Array &) {
      return Value::Array{call("inner", "colleague", colleague_request())};
    });
    provider.steps.push_back([&](const Value::Array &input) {
      sub_end = input;
      return Value::Array{};
    });
    engine.turn({"sub", read_file("programs/turn.lua")});
    {
      const auto inner = outputs(sub_end).at("inner");
      const auto inner_marker = shaped_as(engine, inner, 4096, text.size() - 12288);
      const std::string sub_lead = "Registered workflow sub completed in this turn: ";
      const auto continued_sub = developer(sub_end, sub_lead);
      check(continued_sub == sub_lead + inner.string());
      bool passed = false;
      for (const auto &packet : recorded_shapes(*root))
        if (const auto *through = field(packet, "shape").find("pass_through"))
          passed = passed || *through == Value{inner_marker.ref};
      check(passed);
    }
    std::filesystem::remove_all(path);
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
  } catch (const Error &e) {
    std::cerr << "error " << error_name(e.code) << '\n';
  }
  std::filesystem::remove_all(path);
  return 1;
}
