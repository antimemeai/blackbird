#include "blackbird/beads.hpp"
#include "blackbird/json.hpp"
#include "blackbird/raw_file.hpp"
#include "native_process.hpp"
#include <set>
namespace blackbird {
namespace {
Value number(std::size_t n) { return Value{Number{n}}; }
bool object(const Value &j) { return std::holds_alternative<Value::Object>(j.value()); }
bool array(const Value &j) { return std::holds_alternative<Value::Array>(j.value()); }
bool text(const Value &j) { return std::holds_alternative<std::string>(j.value()); }
std::string value(const Value &j, std::string_view k) {
  const auto *v = j.find(k);
  if (!v || !text(*v) || v->string().find('\0') != std::string::npos ||
      v->string().size() > 8192)
    throw Error{ErrorCode::invalid_range};
  return v->string();
}
Value failed(std::string status, std::string message) {
  return Value::object(
      {{"status", Value{std::move(status)}}, {"message", Value{std::move(message)}}});
}
bool issue(const Value &j) {
  if (!object(j) || j.find("error"))
    return false;
  for (auto k : {"id", "title", "status"}) {
    auto *v = j.find(k);
    if (!v || !text(*v) || v->string().size() > 8192 ||
        v->string().find('\0') != std::string::npos ||
        (k == std::string_view{"id"} && v->string().empty()))
      return false;
  }
  return true;
}
std::optional<RawEventRef> selected(RawEventRef ref) {
  return ref.ordinal ? std::optional{ref} : std::nullopt;
}
} // namespace
Value BeadsAdapter::invoke(RawBeadsTrace &run, const RawBeadsArgv &argv,
                           std::uint8_t kind, bool write,
                           std::optional<RawEventRef> &status, RawEventRef &source) {
  RawBeadsFields fields;
  fields.ref(argv.reference());
  fields.native(kind);
  fields.native(write);
  fields.native(argv.values.size());
  RawBeadsTrace trace{run.context(), RawBeadsKind::invoke, fields};
  trace.write = write;
  source = trace.reference();
  return trace.run([&] {
    Value result;
    // Child closes/signals/reaps before the invocation terminal; scalar outcomes
    // and actual source refs are the only things retained outside this scope.
    {
      detail::Child child;
      trace.stage = 1;
      if (trace.enabled())
        child.raw_capture(trace.reference(), true, true);
      child.cancelled = cancelled;
      child.cwd = project_;
      child.separate_error = true;
      for (char **e = environ; *e; ++e) {
        std::string_view entry{*e};
        if (!entry.starts_with("BEADS_") && !entry.starts_with("BD_") &&
            !entry.starts_with("DOLT_"))
          child.child_environment.emplace_back(entry);
      }
      child.child_environment.push_back("BEADS_DIR=" + project_ + "/.beads");
      trace.stage = 2;
      auto observed = [&](std::string_view bytes, std::uint8_t channel,
                          std::string_view label) {
        const auto source = selected(child.raw_reference());
        const bool present = static_cast<bool>(observer);
        RawBeadsFields arguments;
        arguments.optional(source);
        arguments.native(channel);
        arguments.native(bytes.size());
        arguments.native(present);
        RawBeadsTrace call{trace.context(), RawBeadsKind::observer, arguments};
        call.run([&] {
          if (observer)
            observer(label, bytes);
          else
            call.finish(RawOperationBranch::absent);
        });
      };
      child.output_observer = [&](std::string_view bytes) {
        observed(bytes, 1, "beads.stdout");
      };
      child.error_observer = [&](std::string_view bytes) {
        observed(bytes, 2, "beads.stderr");
      };
      const auto start = detail::Clock::now();
      std::string out;
      int exit = -1;
      struct Outcome {
        RawBeadsTrace &trace;
        detail::Child &child;
        const std::string &out;
        const int &exit;
        const bool &mutation;
        ~Outcome() {
          trace.spawned = child.spawned();
          trace.output = out.size();
          trace.stderr_bytes = child.error_bytes.size();
          trace.exit = exit;
          trace.mutation = mutation;
        }
      } outcome{trace, child, out, exit, mutation_possible_};
      try {
        trace.stage = 3;
        if (write)
          trace.assignment(mutation_possible_, true, 2);
        trace.mutation = mutation_possible_;
        trace.stage = 4;
        child.start(argv.values);
        trace.spawned = child.spawned();
        trace.stage = 5;
        child.close_input();
        trace.stage = 6;
        const auto limit = std::size_t{65536};
        RawBeadsFields arguments;
        arguments.native(limit);
        RawBeadsTrace collect{trace.context(), RawBeadsKind::collect, arguments};
        collect.stage = 1;
        out = collect.run([&] {
          try {
            auto acquired =
                child.collect(start + std::chrono::seconds{5}, limit, &exit);
            collect.output = acquired.size();
            collect.count_present = true;
            collect.stderr_bytes = child.error_bytes.size();
            collect.exit = exit;
            collect.stage = 2;
            return acquired;
          } catch (...) {
            collect.stderr_bytes = child.error_bytes.size();
            collect.exit = exit;
            throw;
          }
        });
        trace.output = out.size();
        trace.stderr_bytes = child.error_bytes.size();
        trace.exit = exit;
        trace.stage = 7;
        RawBeadsFields parse_arguments;
        parse_arguments.native(out.size());
        RawBeadsTrace parse{trace.context(), RawBeadsKind::parse, parse_arguments};
        const auto parsed = parse.run([&] {
          auto acquired = parse_json(out);
          parse.result_present = true;
          parse.result_value = acquired.has_value();
          if (!acquired.has_value())
            parse.result_error = acquired.error();
          return acquired;
        });
        trace.stage = 8;
        if (exit != 0 || !parsed.has_value())
          result = failed(trace.status(write ? "unknown" : "rejected", 2),
                          "nonzero exit or malformed JSON; no automatic replay");
        else
          result = Value::object(
              {{"status", Value{trace.status("ok", 2)}}, {"data", parsed.value()}});
      } catch (const Error &error) {
        trace.spawned = child.spawned();
        trace.mutation = mutation_possible_;
        trace.caught(error, child.spawned());
        result = failed(
            trace.status(child.spawned() && write ? "unknown" : "unavailable", 2),
            std::string{error_name(error.code)} + "; no automatic replay");
        out = child.take_partial();
        result.object().emplace_back("interrupted",
                                     Value{error.code == ErrorCode::interrupted});
      }
      trace.stage = 9;
      trace.spawned = child.spawned();
      trace.output = out.size();
      trace.stderr_bytes = child.error_bytes.size();
      trace.exit = exit;
      result.object().emplace_back("spawned", Value{child.spawned()});
      result.object().emplace_back("exit_code", Value{Number{exit}});
      result.object().emplace_back("stdout_bytes", number(out.size()));
      result.object().emplace_back("stderr_bytes", number(child.error_bytes.size()));
      result.object().emplace_back("stderr", Value{child.error_bytes.substr(0, 2048)});
    }
    trace.stage = 10;
    status = trace.status_choice;
    return result;
  });
}
Value BeadsAdapter::run(const Value &args, const RawAdapterContext *context) {
  RawBeadsFields arguments;
  arguments.optional(bound_source_);
  arguments.optional(cache_source_);
  arguments.native(bound_);
  arguments.native(mutation_possible_);
  RawBeadsTrace trace{context, RawBeadsKind::run, arguments};
  trace.bound = bound_;
  trace.mutation = mutation_possible_;
  auto check = [&](std::uint8_t kind, auto &&effect,
                   std::optional<RawEventRef> subject = {}) -> decltype(auto) {
    return raw_beads_check(trace, kind, subject,
                           std::forward<decltype(effect)>(effect));
  };
  auto get = [&](const Value &source, std::string_view key) {
    return check(36, [&] { return value(source, key); });
  };
  auto acquired_field = [&](std::uint8_t kind, std::string_view key) {
    auto bank = trace.data(49 + 8192);
    auto acquired = get(args, key);
    trace.field(kind, acquired, bank);
    return acquired;
  };
  auto failure = [&](std::string_view status, std::string message) {
    return failed(trace.status(status, 1), std::move(message));
  };
  auto body = [&]() -> Value {
    trace.stage = 1;
    trace.assignment(mutation_possible_, false, 1);
    trace.stage = 2;
    if (!check(1, [&] { return object(args); }))
      return failure("invalid", "object arguments required");
    const auto op = acquired_field(1, "op");
    if (op == "configure") {
      trace.stage = 3;
      for (const auto &[key, unused] : args.object()) {
        (void)unused;
        if (!check(2, [&] {
              return key == "op" || key == "project" || key == "executable" ||
                     key == "actor";
            }))
          return failure("invalid", "unsupported configuration field");
      }
      const auto project = acquired_field(2, "project");
      const auto executable = acquired_field(3, "executable");
      const auto actor = acquired_field(4, "actor");
      if (!check(3, [&] { return !actor.empty() && !executable.empty(); }) ||
          !check(4, [&] { return std::filesystem::path{project}.is_absolute(); }) ||
          !check(5, [&] { return std::filesystem::path{executable}.is_absolute(); }))
        return failure("invalid",
                       "absolute project/executable and nonempty actor required");
      std::error_code error;
      auto canonical_path = [&](std::uint8_t kind, auto &&acquire) {
        RawBeadsFields path_arguments;
        path_arguments.native(kind);
        RawBeadsTrace path{trace.context(), RawBeadsKind::path, path_arguments};
        return path.run([&] {
          decltype(auto) input = acquire();
          RawEventRef owner{};
          try {
            auto result = raw_file_canonical(input, error, path.context(), &owner);
            path.file_owner = selected(owner);
            path.path_error = error;
            return result;
          } catch (...) {
            path.file_owner = selected(owner);
            throw;
          }
        });
      };
      auto canonical =
          canonical_path(1, [&] { return std::filesystem::path{project}; });
      if (check(6, [&] { return static_cast<bool>(error); }))
        return failure("unavailable", "project does not exist");
      const auto joined_extent = canonical.native().size();
      if (joined_extent > UINT64_MAX - 56)
        throw Error{ErrorCode::overflow};
      auto joined_credit = trace.data(49 + joined_extent + 1 + 6);
      const auto joined = canonical / ".beads";
      trace.field(9, joined.native(), joined_credit);
      auto directory =
          canonical_path(2, [&]() -> const std::filesystem::path & { return joined; });
      if (check(7, [&] { return static_cast<bool>(error); }) ||
          !check(8, [&] { return directory == joined; }))
        return failure("unavailable",
                       "canonical .beads required (no redirected discovery)");
      auto assign = [&](std::uint8_t kind, std::string &target, std::size_t size,
                        auto &&acquire) {
        auto bank = trace.data(49 + static_cast<std::uint64_t>(size));
        // A partial assignment cannot inherit an old native binding source.
        bound_source_.reset();
        acquire();
        trace.field(kind, target, bank);
      };
      assign(6, project_, canonical.native().size(),
             [&] { project_ = canonical.string(); });
      assign(7, executable_, executable.size(), [&] { executable_ = executable; });
      assign(8, actor_, actor.size(), [&] { actor_ = actor; });
      trace.assignment(bound_, false, 3);
      bound_source_.reset();
      auto bank = trace.control(75);
      cache_source_.reset();
      cache_ = Value{};
      trace.cache(false, {}, nullptr, bank);
      return Value::object({{"status", Value{trace.status("configured", 1)}},
                            {"project", Value{project_}},
                            {"actor", Value{actor_}},
                            {"lazy", Value{true}}});
    }
    if (op == "cached")
      return Value::object({{"status", Value{trace.status("cached", 1)}},
                            {"data", cache_},
                            {"fresh", Value{false}}});
    if (check(9, [&] { return project_.empty(); }))
      return failure("unavailable",
                     "configure one explicit project, executable and actor first");
    const bool write = check(11, [&] {
      return op == "create" || op == "update" || op == "close" || op == "claim";
    });
    if (!check(10,
               [&] { return write || op == "ready" || op == "list" || op == "show"; }))
      return failure("invalid", "unsupported operation");
    trace.stage = 4;
    RawBeadsArgv argv{trace, 4, write};
    std::string id;
    auto invalid = argv.build(
        op == "update" ? 11 : (op == "create" ? 8 : (op == "ready" ? 6 : 7)),
        [&]() -> std::optional<Value> {
          std::set<std::string> allowed{"op"};
          argv.push(8192, [&] { return executable_; });
          argv.push(6, [] { return std::string{"--json"}; });
          argv.push(8200, [&] { return "--actor=" + actor_; });
          if (!write)
            argv.push(10, [] { return std::string{"--readonly"}; });
          argv.push(6, [&] { return op == "claim" ? std::string{"update"} : op; });
          if (op == "show" || op == "update" || op == "close" || op == "claim") {
            allowed.insert("id");
            id = acquired_field(5, "id");
            if (check(12, [&] { return id.empty(); }) || check(13, [&] {
                  return id.find_first_of(" \t\n,") != std::string::npos;
                }))
              return failure("invalid", "one exact ID required");
          }
          if (op == "ready" || op == "list") {
            allowed.insert("limit");
            int limit = 20;
            if (auto *number_value = args.find("limit")) {
              if (!check(14, [&] {
                    return std::holds_alternative<Number>(number_value->value());
                  }))
                return failure("invalid", "integer limit required");
              const auto digits = number_value->number().text();
              auto parsed =
                  std::from_chars(digits.data(), digits.data() + digits.size(), limit);
              if (check(15, [&] { return parsed.ec != std::errc{}; }) ||
                  check(16,
                        [&] { return parsed.ptr != digits.data() + digits.size(); }) ||
                  check(17, [&] { return limit < 1 || limit > 100; }))
                return failure("invalid", "limit 1..100");
            }
            argv.push(11, [&] { return "--limit=" + std::to_string(limit); });
          }
          auto option = [&](const char *key, const char *flag, std::size_t bound) {
            allowed.insert(key);
            if (args.find(key))
              argv.push(bound,
                        [&] { return std::string{flag} + "=" + get(args, key); });
          };
          if (op == "list")
            option("status", "--status", 8201);
          if (op == "create" || op == "update") {
            if (args.find("body") &&
                check(18, [&] { return get(args, "body") == "-"; }))
              return failure("invalid", "literal '-' description is upstream stdin "
                                        "sentinel; no child spawned");
            option("title", "--title", 8200);
            option("body", "--description", 8206);
            option("type", "--type", 8199);
            option("priority", "--priority", 8203);
            if (op == "create" && !check(19, [&] {
                  return args.find("title") && !get(args, "title").empty();
                }))
              return failure("invalid", "title required");
            if (op == "update")
              option("status", "--status", 8201);
            if (op == "update" && check(20, [&] { return argv.values.size() == 4; }))
              return failure("invalid", "update fields required");
          }
          if (op == "close")
            option("reason", "--reason", 8201);
          if (op == "claim")
            argv.push(7, [] { return std::string{"--claim"}; });
          for (const auto &[key, unused] : args.object()) {
            (void)unused;
            if (!check(21, [&] { return allowed.contains(key); }))
              return failure("invalid", "unsupported field: " + key);
          }
          if (!id.empty()) {
            argv.push(2, [] { return std::string{"--"}; });
            argv.push(8192, [&] { return id; });
          }
          argv.complete();
          return {};
        });
    if (invalid)
      return std::move(*invalid);
    auto prepared_invoke = [&](std::uint8_t kind, auto &&prepare, std::size_t capacity,
                               std::optional<RawEventRef> &status,
                               RawEventRef &source) {
      RawBeadsArgv command{trace, kind, false};
      command.build(capacity, [&] {
        prepare(command);
        command.complete();
      });
      auto result = invoke(trace, command, kind, false, status, source);
      return result;
    };
    Value::Array binding;
    if (!bound_) {
      trace.stage = 5;
      std::optional<RawEventRef> status;
      RawEventRef version_source{};
      auto version = prepared_invoke(
          1,
          [&](RawBeadsArgv &command) {
            command.push(8192, [&] { return executable_; });
            command.push(6, [] { return std::string{"--json"}; });
            command.push(7, [] { return std::string{"version"}; });
          },
          3, status, version_source);
      binding.push_back(version);
      if (check(
              22,
              [&] {
                auto *stopped = version.find("interrupted");
                return stopped && std::get<bool>(stopped->value());
              },
              selected(version_source))) {
        trace.status_choice = status;
        return version;
      }
      auto *data = version.find("data");
      if (!check(
              23,
              [&] {
                return string_field(version, "status") == "ok" && data &&
                       object(*data) && !data->find("error") && data->find("version") &&
                       text(*data->find("version")) &&
                       get(*data, "version") == "0.58.0";
              },
              selected(version_source)))
        return failure("unavailable",
                       "only bd 0.58.0 is supported; no database probe performed");
      RawEventRef where_source{};
      auto where = prepared_invoke(
          2,
          [&](RawBeadsArgv &command) {
            command.push(8192, [&] { return executable_; });
            command.push(6, [] { return std::string{"--json"}; });
            command.push(10, [] { return std::string{"--readonly"}; });
            command.push(5, [] { return std::string{"where"}; });
          },
          4, status, where_source);
      binding.push_back(where);
      if (check(
              24,
              [&] {
                auto *stopped = where.find("interrupted");
                return stopped && std::get<bool>(stopped->value());
              },
              selected(where_source))) {
        trace.status_choice = status;
        return where;
      }
      data = where.find("data");
      if (!check(
              25,
              [&] {
                return string_field(where, "status") == "ok" && data && object(*data) &&
                       !data->find("error") && data->find("path") &&
                       text(*data->find("path")) &&
                       get(*data, "path") == project_ + "/.beads";
              },
              selected(where_source)))
        return failure("unavailable", "where project identity mismatch");
      trace.assignment(bound_, true, 4, selected(where_source));
      bound_source_ = selected(where_source);
    }
    if (write && !id.empty()) {
      trace.stage = 6;
      std::optional<RawEventRef> status;
      RawEventRef source{};
      auto current = prepared_invoke(
          3,
          [&](RawBeadsArgv &command) {
            command.push(8192, [&] { return executable_; });
            command.push(6, [] { return std::string{"--json"}; });
            command.push(10, [] { return std::string{"--readonly"}; });
            command.push(4, [] { return std::string{"show"}; });
            command.push(2, [] { return std::string{"--"}; });
            command.push(8192, [&] { return id; });
          },
          6, status, source);
      if (check(
              26,
              [&] {
                auto *stopped = current.find("interrupted");
                return stopped && std::get<bool>(stopped->value());
              },
              selected(source))) {
        trace.status_choice = status;
        return current;
      }
      const auto *data = current.find("data");
      if (!check(
              27,
              [&] {
                return string_field(current, "status") == "ok" && data &&
                       array(*data) && data->array().size() == 1 &&
                       check(30, [&] { return issue(data->array()[0]); }) &&
                       get(data->array()[0], "id") == id;
              },
              selected(source)))
        return failure("unavailable",
                       "exact target preflight failed; no mutation spawned");
    }
    trace.stage = 7;
    RawEventRef action{};
    auto result = invoke(trace, argv, 4, write, trace.status_choice, action);
    const auto action_source = selected(action);
    trace.stage = 8;
    if (check(
            28, [&] { return string_field(result, "status") == "ok"; },
            action_source)) {
      auto &data = field(result, "data");
      bool valid = check(
          29,
          [&] {
            return op == "create" ? check(30, [&] { return issue(data); })
                                  : array(data);
          },
          action_source);
      if (valid && op != "create") {
        for (const auto &item : data.array())
          valid = valid && check(30, [&] { return issue(item); }, action_source);
        if (op == "show" || op == "update" || op == "close" || op == "claim")
          valid =
              valid &&
              check(
                  31, [&] { return data.array().size() == 1; }, action_source) &&
              check(
                  32, [&] { return get(data.array()[0], "id") == id; }, action_source);
      }
      if (valid && op == "close")
        valid = check(
            33, [&] { return get(data.array()[0], "status") == "closed"; },
            action_source);
      if (valid && op == "claim") {
        const auto *assignee = data.array()[0].find("assignee");
        valid =
            check(
                34,
                [&] {
                  return assignee && text(*assignee) && assignee->string() == actor_;
                },
                action_source) &&
            check(
                35, [&] { return get(data.array()[0], "status") == "in_progress"; },
                action_source);
      }
      if (!valid) {
        result.object().emplace_back(
            "validation_error", Value{"wrong command-specific result shape/target"});
        for (auto &[key, item] : result.object())
          if (key == "status")
            item = Value{trace.status(write ? "unknown" : "rejected", 3)};
      } else if (!write) {
        trace.stage = 9;
        auto bank = trace.control(75);
        cache_source_.reset();
        cache_ = data;
        const auto count = data.array().size();
        trace.cache(true, action_source, &count, bank);
        cache_source_ = action_source;
      }
      if (valid && op == "claim")
        result.object().emplace_back(
            "next", Value{"Claim is assignment only, not ready CAS or lease. Recheck "
                          "task and dependencies before work."});
    }
    if (!binding.empty())
      result.object().emplace_back("binding", Value{std::move(binding)});
    result.object().emplace_back("project", Value{project_});
    result.object().emplace_back("actor", Value{actor_});
    trace.stage = 10;
    return result;
  };
  return trace.run([&] {
    try {
      auto result = body();
      trace.mutation = mutation_possible_;
      trace.bound = bound_;
      return result;
    } catch (...) {
      trace.mutation = mutation_possible_;
      trace.bound = bound_;
      throw;
    }
  });
}
} // namespace blackbird
