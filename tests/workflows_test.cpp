#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/terminal.hpp"
#include "blackbird/tools.hpp"
#include <clocale>
#include <iostream>
#include <source_location>
#include <unistd.h>
using namespace blackbird;
template <class T> T present(std::optional<T> value) {
  if (!value)
    throw std::runtime_error("expected present optional in fixture");
  return std::move(*value);
}

void check(bool good, std::source_location loc = std::source_location::current()) {
  if (!good)
    throw std::runtime_error("workflow oracle line " + std::to_string(loc.line()));
}
template <class T> T id(unsigned char n) {
  IdentityBytes b{};
  b[0] = std::byte{n};
  return unwrap(T::from_bytes(b));
}
void set(Value &j, std::string_view key, Value value) {
  for (auto &[k, v] : j.object())
    if (k == key) {
      v = std::move(value);
      return;
    }
  j.object().emplace_back(key, std::move(value));
}
Value definition(const std::string &name = "research", std::string token = "ultracode",
                 std::string source = "return args") {
  return Value::object(
      {{"name", Value{name}},
       {"description", Value{"Research procedure"}},
       {"source", Value{std::move(source)}},
       {"aliases", Value{Value::Array{Value{name}, Value{name + "-alias"}}}},
       {"bare", Value{true}},
       {"powerwords",
        Value{Value::Array{Value::object(
            {{"token", Value{std::move(token)}}, {"color", Value{"keyword"}}})}}}});
}
Value config(Value::Array defs, std::string prefix = "wf-") {
  return Value::object({{"modules", Value{Value::Array{}}},
                        {"model", Value{""}},
                        {"effort", Value{""}},
                        {"workflows", Value{std::move(defs)}},
                        {"workflow_prefix", Value{std::move(prefix)}}});
}
std::string literal(const Value &j) {
  const auto json = unwrap(dump_json(j));
  std::string delimiter = "==";
  while (json.find("]" + delimiter + "]") != std::string::npos)
    delimiter += "=";
  return "blackbird.json.decode([" + delimiter + "[" + json + "]" + delimiter + "])";
}
Value eval(CodingEngine &engine, const std::string &code) {
  engine.turn({"", code});
  return engine.workflow_result();
}
struct FakeProvider : CodingProvider {
  unsigned calls = 0;
  bool model_invoke = false, fail = false;
  Value last_request;
  Value respond(const Value &r,
                const std::function<void(std::string_view)> &capture) override {
    ++calls;
    last_request = r;
    if (fail)
      throw Error{ErrorCode::interrupted};
    Value::Array output;
    if (model_invoke && calls == 1)
      output.push_back(Value::object(
          {{"type", Value{"function_call"}},
           {"call_id", Value{"choose-workflow"}},
           {"name", Value{"workflow_invoke"}},
           {"arguments",
            Value{
                R"({"name":"ultracode","prompt":"model selected; not operator trigger"})"}}}));
    auto response = Value::object({{"output", Value{std::move(output)}}});
    capture(unwrap(dump_json(response)));
    return response;
  }
};
// Layer 1 (B-M2): a session that recorded an earlier built-in program (the pre-S2
// builtin-ultracode-v1, or S2's v2, kept by a budget change) runs the current
// built-in on reopen, records the upgrade, and its ultracode brief is a protected
// instruction. A supplied program (a definition-generation revision) is kept as
// recorded.
void builtin_upgrade() {
  char path[] = "/tmp/blackbird-upgrade-XXXXXX";
  check(mkdtemp(path) != nullptr);
  const JournalHeader h{id<EnvironmentId>(73),
                        id<AuditStreamId>(74),
                        3,
                        {1048576, 4194304},
                        std::nullopt};
  const JournalCapacity capacity{64 * 1024 * 1024, 10000};
  const auto open = [&] {
    auto root =
        unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(
                                       unwrap(NativeJournalDirectory::open(path))),
                                   "audit", h, capacity));
    unwrap(root->confirm_recovery());
    recover_coding_session(*root);
    return root;
  };
  const auto latest_config = [](RetainedState &root) {
    Value found;
    for (const auto &packet : root.current_programs())
      if (string_field(packet, "label") == "workflow-config-effective-v1")
        found = packet;
    return found;
  };
  for (const std::string &recorded :
       {std::string{"builtin-ultracode-v1"}, std::string{"builtin-ultracode-v2"},
        std::string{"builtin-ultracode-v3"}, std::string(32, 'a')}) {
    const bool supplied = !recorded.starts_with("builtin-");
    std::filesystem::remove_all(path);
    std::filesystem::create_directory(path);
    std::filesystem::permissions(path, std::filesystem::perms::owner_all);
    {
      auto root =
          unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                           unwrap(NativeJournalDirectory::open(path))),
                                       "audit", h, capacity));
      AuditLog log{*root};
      ContextStore context{log};
      FakeProvider provider;
      CodingEngine engine{log, context, provider, "fake"};
      // Under v2, ultracode ran S2's source, which appends its brief on every run.
      if (recorded == "builtin-ultracode-v2") {
        const auto current =
            string_field(default_workflows().array().front(), "source");
        const auto open_brief = current.find("content='") + 9;
        const auto brief_text =
            current.substr(open_brief, current.find('\'', open_brief) - open_brief);
        const auto loop = current.find("for step=1,64 do");
        const auto v2 = "blackbird.append({{role='developer',content='" + brief_text +
                        "'}}, {class='instruction'})\n" + current.substr(loop);
        eval(engine, v2);
        std::size_t v2_briefs = 0;
        for (const auto &entry : field(context.view(), "entries").array())
          if (const auto *content = field(entry, "item").find("content");
              content && content->string().starts_with("Ultracode:"))
            ++v2_briefs;
        check(v2_briefs ==
              1); // the v2 run itself left the brief the current runs must find
      }
      // A budget change records the effective program config with it.
      eval(engine, "local v=blackbird.call('context_budget',{}); "
                   "assert(blackbird.call('context_budget',{proposal={base=v.revision,"
                   "enabled=true,trigger_bytes=500000,target_bytes=200000,reason='t'}})"
                   ".staged)");
      // Rewrite it as a88c1c2 recorded it: the old brief, the old revision.
      auto packet = latest_config(*root);
      check(packet != Value{});
      for (auto &[key, value] : packet.object()) {
        if (key == "program_revision")
          value = Value{recorded};
        if (key != "program_config")
          continue;
        for (auto &[name, workflows] : value.object())
          if (name == "workflows")
            for (auto &definition : workflows.array())
              for (auto &[field_name, source] : definition.object())
                if (field_name == "source") {
                  auto text = source.string();
                  const std::string declared = ", {class='instruction'})";
                  if (const auto at = text.find(declared); at != std::string::npos)
                    text.replace(at, declared.size(), ")");
                  source = Value{text};
                }
      }
      log.record(ApplicationChannel::program, packet);
    }
    auto root = open();
    AuditLog log{*root};
    ContextStore context{log};
    FakeProvider provider;
    CodingEngine engine{log, context, provider, "fake"};
    check(engine.operator_turn("ultracode build useful code", "error('wrong route')"));
    check(engine.operator_turn("ultracode build useful code", "error('wrong route')"));
    std::string brief;
    std::size_t briefs = 0;
    for (const auto &entry : field(context.view(), "entries").array())
      if (const auto *content = field(entry, "item").find("content");
          content && content->string().starts_with("Ultracode:")) {
        brief = string_field(entry, "id");
        ++briefs;
      }
    // c3f: a v2 brief left live is the same brief, so current runs add no copy. The
    // supplied program declares no class, so its result-class brief never counts.
    if (briefs != (supplied ? 2U : 1U)) {
      std::cerr << recorded << " session holds " << briefs << " ultracode briefs\n";
      check(false);
    }
    const auto result =
        context.manage(Value::object({{"base", Value{context.head()}},
                                      {"mode", Value{"archive"}},
                                      {"reason", Value{"t"}},
                                      {"source", Value{"test"}},
                                      {"ids", Value{Value::Array{Value{brief}}}}}));
    const auto reason = field(result, "accepted") == Value{true}
                            ? std::string{"accepted"}
                            : string_field(result, "reason");
    if (reason != (supplied ? "accepted" : "protected-unit")) {
      std::cerr << (supplied ? "supplied" : "built-in")
                << " ultracode brief: " << reason << "\n";
      check(false);
    }
    // The replacement is recorded, naming what it did and the revision it replaced;
    // a supplied revision is kept.
    const auto latest = latest_config(*root);
    const auto revision = string_field(latest, "program_revision");
    check(revision == (supplied ? recorded : "builtin-ultracode-v4"));
    if (!supplied)
      check(string_field(latest, "activation") == "builtin-current" &&
            string_field(latest, "replaced_revision") == recorded);
  }
  std::filesystem::remove_all(path);
}
// c3f: the built-in ultracode appends its brief only when no live instruction entry
// holds an equal item (object key order ignored), so repeated runs keep one copy per
// brief revision.
void brief_once() {
  char path[] = "/tmp/blackbird-brief-XXXXXX";
  check(mkdtemp(path) != nullptr);
  const JournalHeader h{id<EnvironmentId>(75),
                        id<AuditStreamId>(76),
                        3,
                        {1048576, 4194304},
                        std::nullopt};
  const JournalCapacity capacity{64 * 1024 * 1024, 10000};
  const auto source = string_field(default_workflows().array().front(), "source");
  const auto from = source.find("content='") + 9;
  const auto brief = source.substr(from, source.find('\'', from) - from);
  check(brief.starts_with("Ultracode:"));
  const auto session =
      [&](const std::function<void(ContextStore &, CodingEngine &)> &body) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directory(path);
        std::filesystem::permissions(path, std::filesystem::perms::owner_all);
        auto root = unwrap(
            RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                      unwrap(NativeJournalDirectory::open(path))),
                                  "audit", h, capacity));
        AuditLog log{*root};
        ContextStore context{log};
        FakeProvider provider;
        CodingEngine engine{log, context, provider, "fake"};
        body(context, engine);
      };
  const auto copies = [](ContextStore &context, std::string_view text) {
    std::vector<std::string> ids;
    for (const auto &entry : field(context.view(), "entries").array())
      if (const auto *content = field(entry, "item").find("content");
          content && std::holds_alternative<std::string>(content->value()) &&
          content->string() == text)
        ids.push_back(string_field(entry, "id"));
    return ids;
  };
  const auto run = [](CodingEngine &engine) {
    check(engine.operator_turn("ultracode build useful code", "error('wrong route')"));
  };
  const auto archive = [](ContextStore &context, const std::string &entry) {
    const auto result =
        context.manage(Value::object({{"base", Value{context.head()}},
                                      {"mode", Value{"archive"}},
                                      {"reason", Value{"t"}},
                                      {"source", Value{"test"}},
                                      {"ids", Value{Value::Array{Value{entry}}}}}));
    return field(result, "accepted") == Value{true} ? std::string{"accepted"}
                                                    : string_field(result, "reason");
  };
  // The plan's specification: ten runs leave one brief entry.
  session([&](ContextStore &context, CodingEngine &engine) {
    for (int i = 0; i < 10; ++i)
      run(engine);
    if (copies(context, brief).size() != 1) {
      std::cerr << "ten ultracode runs left " << copies(context, brief).size()
                << " brief entries\n";
      check(false);
    }
    // Once the brief is not live (an edit removed it), the next run appends it
    // again, once. A result entry with the same bytes is not an instruction and
    // does not count.
    const auto first = copies(context, brief).front();
    auto view = context.view();
    std::erase_if(view.object()[1].second.array(), [&](const Value &entry) {
      return string_field(entry, "id") == first;
    });
    check(field(context.edit(view), "accepted") == Value{true});
    eval(engine,
         "blackbird.append({{role='developer',content=[==[" + brief + "]==]}})");
    const auto result_copy = copies(context, brief);
    check(result_copy.size() == 1);
    run(engine);
    run(engine);
    const auto after = copies(context, brief);
    check(after.size() == 2 && after.front() == result_copy.front());
    check(archive(context, after.back()) == "protected-unit");
    check(archive(context, result_copy.front()) == "accepted");
  });
  // A changed brief revision appends the new brief once beside the old one.
  session([&](ContextStore &context, CodingEngine &engine) {
    const std::string earlier_brief = "Ultracode: an earlier revision of the brief.";
    auto earlier = source;
    earlier.replace(from, brief.size(), earlier_brief);
    for (int i = 0; i < 2; ++i)
      eval(engine, earlier);
    check(copies(context, earlier_brief).size() == 1 && copies(context, brief).empty());
    for (int i = 0; i < 3; ++i)
      run(engine);
    check(copies(context, earlier_brief).size() == 1 &&
          copies(context, brief).size() == 1);
  });
  std::filesystem::remove_all(path);
}
// c3f layer 1 (M1): live_instruction compares items with object keys sorted at
// every depth, so a native item matches the table Lua passes for it.
void instruction_query() {
  char path[] = "/tmp/blackbird-query-XXXXXX";
  check(mkdtemp(path) != nullptr);
  const JournalHeader h{id<EnvironmentId>(77),
                        id<AuditStreamId>(78),
                        3,
                        {1048576, 4194304},
                        std::nullopt};
  {
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                         unwrap(NativeJournalDirectory::open(path))),
                                     "audit", h, {64 * 1024 * 1024, 10000}));
    AuditLog log{*root};
    ContextStore context{log};
    FakeProvider provider;
    CodingEngine engine{log, context, provider, "fake"};
    // The operator prompt is stored {role, content}; Lua tables arrive key-sorted.
    engine.turn({"c3f probe prompt", R"lua(
local read
for _,e in ipairs(blackbird.context().entries) do
  if e.item.content=='c3f probe prompt' then read=e.item end
end
return {literal=blackbird.live_instruction({role='user',content='c3f probe prompt'}),
        read=blackbird.live_instruction(read),
        value=blackbird.live_instruction({role='user',content='c3f probe prompt.'}),
        role=blackbird.live_instruction({role='developer',content='c3f probe prompt'})}
)lua"});
    const auto answers = engine.workflow_result();
    if (answers.find("literal") == nullptr ||
        field(answers, "literal") != Value{true} ||
        field(answers, "read") != Value{true} ||
        field(answers, "value") != Value{false} ||
        field(answers, "role") != Value{false}) {
      std::cerr << "live_instruction answers: " << unwrap(dump_json(answers)) << "\n";
      check(false);
    }
    // Nested objects: key order is ignored at every depth, array order is not.
    const auto part = [](std::string text, bool sorted) {
      return sorted ? Value::object({{"text", Value{std::move(text)}},
                                     {"type", Value{"input_text"}}})
                    : Value::object({{"type", Value{"input_text"}},
                                     {"text", Value{std::move(text)}}});
    };
    const auto message = [&](bool sorted, std::string first, std::string second) {
      Value::Array parts{part(std::move(first), sorted),
                         part(std::move(second), sorted)};
      return sorted ? Value::object({{"content", Value{std::move(parts)}},
                                     {"role", Value{"developer"}}})
                    : Value::object({{"role", Value{"developer"}},
                                     {"content", Value{std::move(parts)}}});
    };
    context.append({message(false, "one", "two")}, "operator");
    check(context.live_instruction(message(false, "one", "two")));
    check(context.live_instruction(message(true, "one", "two")));
    check(!context.live_instruction(message(true, "two", "one")));
    check(!context.live_instruction(message(true, "one", "three")));
  }
  std::filesystem::remove_all(path);
}
int main() {
  std::setlocale(LC_ALL, "en_US.UTF-8");
  auto c = config({definition()});
  auto registry = std::make_shared<WorkflowRegistry>(c, "r1");
  check(present(registry->select("/wf-research \tA\nB")).arguments == " \tA\nB");
  check(present(registry->select("/wf-research-alias x")).definition == definition());
  check(present(registry->select("research\tbare")).arguments == "\tbare");
  check(!registry->select("/research"));
  for (const auto text : {"notultracode", "ULTRACODE", "ultracode_", "xultracode",
                          "éultracode", "ultracodeé"})
    check(!registry->select(text));
  for (const auto text :
       {"ultracode", "(ultracode)", "a ultracode!", "ultracode ultracode"})
    check(registry->select(text).has_value());
  check(present(registry->select("a ultracode!")).arguments == "a ultracode!");
  check(!registry->select("/unknown ultracode"));
  check(WorkflowRegistry{config({definition()}, ""), "r0"}
            .select("/research")
            .has_value());
  auto reject = [](const Value &candidate) {
    try {
      WorkflowRegistry bad{candidate, "bad"};
      (void)bad;
    } catch (const Error &) {
      return true;
    }
    return false;
  };
  auto collision = definition();
  set(collision, "aliases", Value{Value::Array{Value{"help"}}});
  check(reject(config({collision}, "")));
  check(reject(config({collision}, "wf-"))); // bare collides too
  check(reject(config({definition(), definition("other", "another")}, "")) == false);
  auto duplicate = definition("other", "another");
  set(duplicate, "aliases", field(definition(), "aliases"));
  check(reject(config({definition(), duplicate})));
  check(reject(config({definition(), definition("other")})));
  WorkflowRegistry conflict{config({definition(), definition("other", "another")}),
                            "r2"};
  bool refused = false;
  try {
    (void)conflict.select("ultracode another");
  } catch (const Error &e) {
    refused = e.code == ErrorCode::conflict;
  }
  check(refused);
  check(present(conflict.select("/wf-research another")).trigger == "/wf-research");
  check(field(registry->discover(), "definitions").array().front() ==
        present(registry->select("/wf-research")).definition);
  publish_workflows(registry);
  check(terminal_help().find("/wf-research") != std::string::npos);
  Composer composer;
  for (char byte : std::string{"/wf-resea\t"})
    composer.feed(byte);
  check(composer.text() ==
        "/wf-research "); // slash menu selects the shared catalog entry
  composer.draft("/wf-research");
  composer.feed('\t');
  check(composer.text() == "/wf-research ");
  check(terminal_command_hint("/wf-research x").find("Research procedure") !=
        std::string::npos);
  // Hot publication may shrink the catalog while a menu has a retained selection.
  Value::Array many;
  for (unsigned i = 0; i < 24; ++i)
    many.push_back(definition("menu" + std::to_string(i), "word" + std::to_string(i)));
  for (unsigned mode = 0; mode < 4; ++mode) {
    publish_workflows(std::make_shared<WorkflowRegistry>(config(many), "large"));
    Composer menu;
    menu.feed(mode == 0 || mode == 3 ? char{20} : '/');
    for (unsigned i = 0; i < 128; ++i)
      for (char byte : std::string{"\x1b[B"})
        menu.feed(byte);
    publish_workflows({});
    if (mode == 3)
      for (char byte : std::string{"\x1b[A"})
        menu.feed(byte);
    bool selected_visible = false;
    for (const auto &row : menu.palette_lines(8))
      selected_visible |= row.starts_with("> /");
    check(selected_visible);
    const auto action = menu.feed(mode == 2 ? '\t' : '\r');
    const auto chosen = action.text.empty() ? menu.text() : action.text;
    check(chosen.starts_with('/'));
    check(chosen.find("wf-menu") == std::string::npos);
  }
  publish_workflows(registry);
  ChatGrid grid;
  grid.reset(40, 1);
  grid.line(0, registry->color("notultracode ultracode!"));
  check(grid.cells[13].ink == Ink::keyword);
  check(grid.cells[0].ink == Ink::normal);
  const auto wrapped = terminal_powerword_lines("xx ultracode notultracode", 5);
  std::string joined;
  std::size_t colored = 0;
  for (const auto &row : wrapped)
    for (const auto &span : row) {
      joined += span.text;
      if (span.ink == Ink::keyword)
        colored += span.text.size();
    }
  check(joined == "xx ultracode notultracode");
  check(colored == 9);
  ChatView user;
  user.append(ChatKind::user, "# ultracode\n```\nultracode\n```", true);
  bool color_found = false;
  for (const auto &row : user.rows(30))
    for (const auto &span : row)
      color_found |= span.ink == Ink::keyword;
  check(color_found);
  ChatView assistant;
  assistant.append(ChatKind::assistant, "ultracode", true);
  for (const auto &row : assistant.rows(30))
    for (const auto &span : row)
      check(span.ink != Ink::keyword);
  ChatPainter painter;
  painter.prepare(grid, 0, 0);
  painter.commit(grid);
  painter.prepare(grid, 0, 0);
  check(painter.touched() == 0);
  char path[] = "/tmp/blackbird-workflows-XXXXXX";
  check(mkdtemp(path) != nullptr);
  const JournalHeader h{id<EnvironmentId>(71),
                        id<AuditStreamId>(72),
                        3,
                        {1048576, 4194304},
                        std::nullopt};
  auto root =
      unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                       unwrap(NativeJournalDirectory::open(path))),
                                   "audit", h, {64 * 1024 * 1024, 10000}));
  AuditLog log{*root};
  ContextStore context{log};
  FakeProvider provider;
  CodingEngine engine{log, context, provider, "fake"};
  check(engine.workflows()->named("ultracode").find("source") != nullptr);
  const auto initial = engine.workflows();
  auto proposal = Value::object({{"proposal", c}});
  eval(engine,
       "local r=blackbird.call('program_config'," + literal(proposal) +
           "); assert(r.staged); local d=blackbird.call('workflow_registry',{}); "
           "assert(d.definitions[1].name=='ultracode'); return d");
  check(engine.workflows() != initial);
  check(engine.workflows()->select("/wf-research").has_value());
  const std::string prompt = "/wf-research \tA\nB";
  check(engine.operator_turn(prompt, "error('fallback must not execute')"));
  check(string_field(engine.workflow_result(), "arguments") == " \tA\nB");
  check(string_field(engine.workflow_result(), "prompt") == prompt);
  check(string_field(context.items().back(), "content") == prompt);
  check(!engine.operator_turn("/missing ultracode", ""));
  auto result = eval(
      engine, "return blackbird.call('workflow_invoke',{name='research',arguments='lua "
              "args'})");
  check(string_field(result, "arguments") == "lua args");
  const auto effective = engine.workflows();
  auto changed = config({definition("new", "newword")});
  bool failed = false;
  try {
    eval(engine, "blackbird.call('program_config'," +
                     literal(Value::object({{"proposal", changed}})) +
                     "); error('stop')");
  } catch (const Error &) {
    failed = true;
  }
  check(failed);
  check(engine.workflows() == effective);
  result =
      eval(engine, "return blackbird.call('program_config'," +
                       literal(Value::object({{"proposal", config({collision}, "")}})) +
                       ")");
  check(string_field(result, "error") == "conflict");
  check(engine.workflows() == effective);
  auto invalid = definition();
  set(invalid, "source", Value{"this is invalid Lua!"});
  result =
      eval(engine, "return blackbird.call('program_config'," +
                       literal(Value::object({{"proposal", config({invalid})}})) + ")");
  check(string_field(result, "error") == "invalid_range");
  check(engine.workflows() == effective);
  // Successful registration and provider use of the same useful ultracode definition.
  eval(engine, "return blackbird.call('program_config'," +
                   literal(Value::object(
                       {{"proposal", config(default_workflows().array(), "")}})) +
                   ")");
  provider.calls = 0;
  check(engine.operator_turn("ultracode build useful code", "error('wrong route')"));
  check(provider.calls == 1);
  check(
      string_field(
          field(provider.last_request, "input").array().at(context.items().size() - 2),
          "content")
          .find("ultracode") != std::string::npos);
  provider.calls = 0;
  provider.model_invoke = true;
  eval(engine, R"lua(for step=1,4 do
    local r=blackbird.request(); local count=0
    for _,item in ipairs(r.output) do if item.type=='function_call' then
      count=count+1; local result=blackbird.call(item.name,item.arguments)
      assert(result.status=='accepted')
      blackbird.append({{type='function_call_output',call_id=item.call_id,output=blackbird.json.encode(result)}})
    end end
    if count==0 then return end
  end error('budget'))lua");
  check(provider.calls ==
        3); // parent selection, selected workflow request, parent continuation
  check(ContextStore::stop_outputs(field(context.view(), "entries").array(), false)
            .empty());
  bool source_retained = false;
  for (const auto &fact : root->committed_facts())
    if (const auto *event = std::get_if<ApplicationRecordEvent>(&fact.event.body);
        event && event->channel == ApplicationChannel::program) {
      auto j = unwrap(read_packet(event->payload));
      if (j.find("definition") && j.find("revision") && j.find("invocation"))
        source_retained = true;
    }
  check(source_retained);
  // Recheck findings: raw control escaping does not alter color/matching;
  // malformed scalar types reject clearly; no cross-domain alias ambiguity.
  auto malformed = definition();
  set(malformed, "bare", Value{"not boolean"});
  check(reject(config({malformed})));
  auto first = definition();
  set(first, "bare", Value{false});
  auto second = definition("wf-research", "secondword");
  check(!reject(config({first, second})));
  auto escaped = terminal_powerword_lines(std::string{"\x1b"} + "ultracode", 40);
  check(escaped.front().back().ink == Ink::keyword);
  check(escaped.front().back().text == "ultracode");
  check(chat_plain(escaped.front()) == "\\x1bultracode");
  const auto wrapped_color =
      terminal_powerword_lines(std::string{"\x1b"} + "ultracode", 5);
  check(wrapped_color.size() == 3 && chat_plain(wrapped_color[0]) == "\\x1bu" &&
        chat_plain(wrapped_color[1]) == "ltrac" &&
        chat_plain(wrapped_color[2]) == "ode");
  std::string keyword_bytes;
  for (const auto &row : wrapped_color)
    for (const auto &span : row)
      if (span.ink == Ink::keyword)
        keyword_bytes += span.text;
  check(keyword_bytes == "ultracode");
  const auto split_token = terminal_powerword_lines("ultra\x1b"
                                                    "code",
                                                    40);
  check(chat_plain(split_token.front()) == "ultra\\x1bcode");
  for (const auto &span : split_token.front())
    check(span.ink != Ink::keyword);
  const auto nul_boundary =
      terminal_powerword_lines(std::string(1, '\0') + "ultracode", 40);
  check(chat_plain(nul_boundary.front()) == "\\x00ultracode" &&
        nul_boundary.front().back().ink == Ink::keyword);
  // A registered workflow that stages config and then fails cannot activate it,
  // even when the outer caller ignores the returned error.
  const auto before_failure = engine.workflows();
  auto doomed = definition("doomed", "doomedword",
                           "blackbird.call('program_config'," +
                               literal(Value::object({{"proposal", changed}})) +
                               "); error('child failure')");
  eval(engine, "return blackbird.call('program_config'," +
                   literal(Value::object({{"proposal", config({doomed})}})) + ")");
  const auto doomed_effective = engine.workflows();
  failed = false;
  try {
    eval(engine,
         "blackbird.call('workflow_invoke',{name='doomed'}); return 'ignored failure'");
  } catch (const Error &) {
    failed = true;
  }
  check(failed);
  check(engine.workflows() == doomed_effective);
  check(engine.workflows() != before_failure);
  // Catching the invocation limit and making another host call cannot publish staged
  // config.
  eval(engine,
       "return blackbird.call('program_config'," +
           literal(Value::object(
               {{"proposal", config({definition("trivial", "trivialword")})}})) +
           ")");
  const auto limit_effective = engine.workflows();
  failed = false;
  try {
    eval(engine, "blackbird.call('program_config'," +
                     literal(Value::object({{"proposal", changed}})) + R"lua();
         for i=1,64 do blackbird.call('workflow_invoke',{name='trivial'}) end
         local ok=pcall(function() blackbird.call('workflow_invoke',{name='trivial'}) end)
         assert(not ok)
         blackbird.call('workflow_registry',{})
         return 'caught'
       )lua");
  } catch (const Error &e) {
    failed = e.code == ErrorCode::capacity;
  }
  check(failed);
  check(engine.workflows() == limit_effective);
  // Restore the previous effective definition for the cancellation case below.
  eval(engine, "return blackbird.call('program_config'," +
                   literal(Value::object({{"proposal", config({doomed})}})) + ")");
  const auto cancellation_effective = engine.workflows();
  // Cancellation similarly preserves the effective definition, never queued replay.
  engine.cancelled = [] { return true; };
  failed = false;
  try {
    engine.operator_turn("/wf-doomed", "");
  } catch (const Error &) {
    failed = true;
  }
  engine.cancelled = {};
  check(failed);
  check(engine.workflows() == cancellation_effective);
  // Reconstruction uses only committed effective configuration.
  CodingEngine restored{log, context, provider, "fake"};
  check(restored.workflows()->discover() == engine.workflows()->discover());
  publish_workflows({});
  std::filesystem::remove_all(path);
  builtin_upgrade();
  brief_once();
  instruction_query();
  std::cout << "workflow registry, boundary, fake provider, completion and "
               "colored-cell oracles passed\n";
}
