#include "blackbird/coding.hpp"
#include "blackbird/json.hpp"
#include "blackbird/task_view.hpp"
#include "blackbird/terminal.hpp"
#include <clocale>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

using namespace blackbird;
namespace {
void check(bool yes, const char *message) {
  if (!yes)
    throw std::runtime_error(message);
}
Value json(std::string_view text) { return unwrap(parse_json(text)); }
template <class T> T id(unsigned char n) {
  IdentityBytes bytes{};
  bytes[0] = std::byte{n};
  return unwrap(T::from_bytes(bytes));
}
class Provider final : public CodingProvider {
public:
  Value request;
  Value respond(const Value &input,
                const std::function<void(std::string_view)> &capture) override {
    request = input;
    auto result = json(R"({"output":[]})");
    capture(unwrap(dump_json(result)));
    return result;
  }
};
} // namespace
int main() {
  (void)std::setlocale(LC_CTYPE, "");
  char pattern[] = "/tmp/bb-tasks-XXXXXX";
  const auto path = mkdtemp(pattern);
  if (!path)
    return 2;
  const JournalHeader header{id<EnvironmentId>(1),
                             id<AuditStreamId>(2),
                             3,
                             {1024 * 1024, 4 * 1024 * 1024},
                             std::nullopt};
  const JournalCapacity capacity{64 * 1024 * 1024, 20000};
  try {
    Value saved;
    {
      auto root =
          unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                           unwrap(NativeJournalDirectory::open(path))),
                                       "audit", header, capacity));
      AuditLog log{*root};
      ContextStore context{log};
      Provider provider;
      CodingEngine engine{log, context, provider, "fixture"};
      auto &tasks = engine.tasks();
      check(field(tasks.read(), "items").array().empty(), "empty task state");
      unsigned publications = 0;
      TaskState viewer;
      tasks.changed = [&](const Value &packet) {
        viewer.replay(packet);
        ++publications;
      };
      const auto add = json(
          R"({"op_id":"seed","base":0,"ops":[{"op":"add","title":"Build panel"},{"op":"add","title":"Check persistence"}]})");
      const auto result = tasks.edit(add);
      check(field(result, "added").array() == json(R"(["t1","t2"])").array(),
            "stable IDs");
      const auto cursor = root->cursor();
      check(tasks.edit(add) == result && root->cursor().sequence == cursor.sequence &&
                publications == 1,
            "retry duplicated retained edit");
      check(
          tasks.edit(json(
              R"({"ops":[{"title":"Build panel","op":"add"},{"title":"Check persistence","op":"add"}],"base":0,"op_id":"seed"})")) ==
              result,
          "JSON key order changed retry behavior");
      check(
          std::get<bool>(
              field(
                  tasks.edit(json(
                      R"({"op_id":"stale-add","base":0,"ops":[{"op":"add","title":"wrong"}]})")),
                  "conflict")
                  .value()),
          "stale structural edit");
      tasks.edit(json(
          R"({"op_id":"child","base":1,"ops":[{"op":"add","parent":"t1","title":"Render 界"}]})"));
      const auto before = tasks.snapshot();
      bool rejected = false;
      try {
        tasks.edit(json(
            R"({"op_id":"too-deep","base":2,"ops":[{"op":"set","id":"t2","version":1,"status":"done"},{"op":"add","parent":"t3","title":"third level"}]})"));
      } catch (const Error &e) {
        rejected = e.code == ErrorCode::invalid_range;
      }
      check(rejected && tasks.snapshot() == before && publications == 2,
            "invalid batch partially published");
      tasks.edit(json(
          R"({"op_id":"rename","ops":[{"op":"set","id":"t1","version":1,"title":"Renamed panel","status":"active"}]})"));
      tasks.edit(json(
          R"({"op_id":"disjoint","ops":[{"op":"set","id":"t2","version":1,"status":"active"}]})"));
      check(field(field(tasks.read(), "counts"), "active").number().text() == "2",
            "parallel active tasks");
      const auto conflict = tasks.edit(json(
          R"({"op_id":"stale-row","ops":[{"op":"set","id":"t1","version":1,"title":"overwritten"}]})"));
      check(!std::get<bool>(field(conflict, "accepted").value()) &&
                string_field(field(conflict, "current").array()[0], "title") ==
                    "Renamed panel",
            "stale row overwritten");
      Value::Array oversized_ops;
      for (unsigned i = 0; i < 100; ++i)
        oversized_ops.push_back(json(R"({"id":"t1"})"));
      const auto oversized_conflict = tasks.edit(Value::object(
          {{"op_id", Value{"seed"}}, {"ops", Value{std::move(oversized_ops)}}}));
      check(field(oversized_conflict, "current").array().size() <= TaskState::max_batch,
            "conflict reply exceeded response bound");
      tasks.operator_command("done t3");
      auto page = tasks.read();
      check(field(page, "items").array()[0].find("subtasks_done")->number().text() ==
                    "1" &&
                string_field(field(page, "items").array()[0], "status") == "active",
            "child auto-completed parent");
      check(field(field(page, "counts"), "done").number().text() == "0",
            "children double counted");
      const auto folded = tasks.read(json(R"({"collapsed":["t1"]})"));
      check(field(folded, "items").array().size() == 2, "folded child visible");
      const auto first = tasks.read(json(R"({"limit":1})"));
      check(field(first, "items").array().size() == 1 &&
                field(first, "next").number().text() == "1",
            "bounded paging");
      check(std::get<bool>(
                field(tasks.read(json(R"({"revision":0})")), "conflict").value()),
            "mixed revision page");
      auto pane = task_pane_rows(page, 28);
      std::string rendered;
      for (const auto &row : pane) {
        const auto text = chat_plain(row);
        rendered += text + "\n";
        check(terminal_lines(text, 28).size() == 1, "pane crosses column boundary");
      }
      if (rendered.find("Renamed panel") == std::string::npos ||
          rendered.find("1/1") == std::string::npos)
        throw std::runtime_error("pane missing task/rollup: " + rendered);
      tasks.operator_command("block t2 waiting for operator");
      try {
        engine.turn(
            {"exercise task tools",
             "local q=blackbird.tasks.read(); "
             "assert(blackbird.json.encode(q.revision)=='6',blackbird.json.encode(q)); "
             "local "
             "r=blackbird.tasks.edit({op_id='lua-state',ops={{op='set',id='t2',version="
             "q.items[3].version,note='Lua update'}}}); "
             "assert(r.accepted,blackbird.json.encode(r)); blackbird.request()"});
      } catch (...) {
        std::cerr << unwrap(dump_json(engine.workflow_result())) << '\n';
        throw;
      }
      const auto &state_item = field(provider.request, "input").array().back();
      check(field(state_item, "role") == Value{"developer"} &&
                string_field(provider.request, "instructions").find("CURRENT_TASKS") ==
                    std::string::npos &&
                string_field(state_item, "content").find("CURRENT_TASKS") !=
                    std::string::npos &&
                string_field(state_item, "content").find("waiting for operator") !=
                    std::string::npos &&
                string_field(
                    field(tasks.read(json(R"({"id":"t2"})")), "items").array()[0],
                    "note") == "Lua update",
            "model task projection absent");
      check(tasks.snapshot() == viewer.snapshot(), "viewer diverged from owner");
      auto compacted = context.view();
      for (auto &[key, value] : compacted.object())
        if (key == "entries")
          value = Value{Value::Array{}};
      check(std::get<bool>(field(context.edit(compacted), "accepted").value()),
            "fixture compaction rejected");
      engine.turn({"after compaction", "blackbird.request()"});
      const auto &compacted_state = field(provider.request, "input").array().back();
      check(field(compacted_state, "role") == Value{"developer"} &&
                string_field(compacted_state, "content").find("Renamed panel") !=
                    std::string::npos &&
                string_field(compacted_state, "content").find("waiting for operator") !=
                    std::string::npos,
            "context compaction lost native task projection");
      std::set<std::string> tools;
      for (const auto &tool : field(provider.request, "tools").array())
        tools.insert(string_field(tool, "name"));
      check(tools.contains("tasks_read") && tools.contains("tasks_edit"),
            "provider lacks task tools");
      TaskState restored;
      restored.restore(tasks.snapshot());
      check(restored.snapshot() == tasks.snapshot(), "snapshot roundtrip");
      auto malformed = tasks.snapshot();
      for (auto &entry : malformed.object())
        if (entry.first == "items")
          for (auto &row : entry.second.array())
            if (string_field(row, "id") == "t3")
              for (auto &[key, value] : row.object())
                if (key == "parent")
                  value = Value{"t3"};
      bool invalid_restore = false;
      try {
        restored.restore(malformed);
      } catch (const Error &) {
        invalid_restore = true;
      }
      check(invalid_restore && restored.snapshot() == tasks.snapshot(),
            "restore admitted malformed hierarchy");
      const auto before_move = tasks.snapshot();
      bool invalid_move = false;
      try {
        tasks.edit(Value::object(
            {{"op_id", Value{"invalid-move"}},
             {"base", field(tasks.read(), "revision")},
             {"ops", Value{Value::Array{Value::object(
                         {{"op", Value{"move"}},
                          {"id", Value{"t1"}},
                          {"version", field(*tasks.state().item("t1"), "version")},
                          {"parent", Value{"t2"}}})}}}}));
      } catch (const Error &) {
        invalid_move = true;
      }
      check(invalid_move && tasks.snapshot() == before_move,
            "move created third hierarchy level");
      tasks.operator_command("archive t1");
      check(field(tasks.read(), "total").number().text() == "1",
            "archive failed to include children");
      saved = tasks.snapshot();
      unwrap(context.checkpoint());
      check(root->saved_state() != nullptr, "current checkpoint missing");
    }
    {
      auto root =
          unwrap(RetainedState::open(std::make_unique<NativeJournalDirectory>(
                                         unwrap(NativeJournalDirectory::open(path))),
                                     "audit", header, capacity));
      check(root->compact_recovery(), "task reopen scanned full history");
      unwrap(root->confirm_recovery());
      recover_coding_session(*root);
      AuditLog log{*root};
      ContextStore context{log};
      TaskStore tasks{log};
      check(tasks.snapshot() == saved, "compact reopen lost task state");
      tasks.operator_command("add continued work");
      check(string_field(field(tasks.read(), "items").array()[1], "id") == "t4",
            "reopen reused ID");
      // Publication failure must leave the working state unchanged.
      root->block_admission();
      const auto previous = tasks.snapshot();
      bool failed = false;
      try {
        tasks.operator_command("done t2");
      } catch (const Error &) {
        failed = true;
      }
      check(failed && tasks.snapshot() == previous,
            "failed retained publication changed task state");
    }
    {
      TaskState large;
      for (unsigned batch = 0; batch < 8; ++batch) {
        Value::Array ops;
        for (unsigned i = 0; i < 64; ++i)
          ops.push_back(json(R"({"op":"add","title":"Granular work"})"));
        large.commit(large.prepare(
            Value::object({{"op_id", Value{"large-" + std::to_string(batch)}},
                           {"base", Value{Number{std::to_string(large.revision())}}},
                           {"ops", Value{std::move(ops)}}})));
      }
      const auto page = large.read(json(R"({"limit":8})"));
      check(field(page, "items").array().size() == 8 &&
                field(page, "next").number().text() == "8" &&
                field(field(page, "counts"), "queued").number().text() == "512",
            "large list paging/counts");
      Value first_edit;
      for (unsigned i = 0; i < 20; ++i) {
        auto edit = Value::object(
            {{"op_id", Value{"small-" + std::to_string(i)}},
             {"ops", Value{Value::Array{Value::object(
                         {{"op", Value{"set"}},
                          {"id", Value{"t1"}},
                          {"version", field(*large.item("t1"), "version")},
                          {"status", Value{"active"}}})}}}});
        if (i == 0)
          first_edit = edit;
        auto prepared = large.prepare(edit);
        check(!prepared.packet.find("order") &&
                  field(prepared.result, "changed").array().size() == 1 &&
                  unwrap(dump_json(prepared.packet)).size() < 2048,
              "single edit echoed large list");
        large.commit(std::move(prepared));
      }
      bool stale_retry = false;
      try {
        (void)large.prepare(first_edit);
      } catch (const Error &e) {
        stale_retry = e.code == ErrorCode::conflict;
      }
      check(stale_retry && field(large.snapshot(), "receipts").array().size() ==
                               TaskState::retry_window,
            "expired retry duplicated work or retry cache grew");
    }
    std::filesystem::remove_all(path);
    std::cout << "task state, tools, projection, rendering and reopen checks passed\n";
  } catch (const Error &e) {
    std::cerr << error_name(e.code) << ':' << e.detail << '\n';
    std::filesystem::remove_all(path);
    return 1;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    std::filesystem::remove_all(path);
    return 1;
  }
}
