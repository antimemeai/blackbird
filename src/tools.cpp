#include "blackbird/tools.hpp"
#include "blackbird/raw_file.hpp"
#include "native_process.hpp"
#include <charconv>
#include <fstream>
#include <sys/stat.h>

namespace blackbird {
std::string read_file_prefix(const std::filesystem::path &path, std::size_t count,
                             const RawAdapterContext *context,
                             RawEventRef *actual_owner) {
  return raw_file_read_prefix(path, count, context, actual_owner);
}
std::string read_file(const std::filesystem::path &path, std::size_t limit) {
  std::ifstream file{path, std::ios::binary};
  if (!file)
    throw Error{ErrorCode::io, errno};
  std::string out;
  char buffer[8192];
  while (file.read(buffer, sizeof(buffer)) || file.gcount() != 0) {
    const auto n = static_cast<std::size_t>(file.gcount());
    if (n > limit - out.size())
      throw Error{ErrorCode::capacity};
    out.append(buffer, n);
  }
  if (!file.eof())
    throw Error{ErrorCode::io};
  return out;
}
void write_file_observed(const std::filesystem::path &requested, std::string_view bytes,
                         FileEffectObservation *effect) {
  std::error_code error;
  const bool exists = std::filesystem::exists(requested, error);
  if (error)
    throw Error{ErrorCode::io, error.value()};
  const auto path = exists ? std::filesystem::canonical(requested)
                           : std::filesystem::absolute(requested);
  auto template_name = path.string() + ".arco-XXXXXX";
  std::vector<char> name(template_name.begin(), template_name.end());
  name.push_back('\0');
  const int fd = ::mkstemp(name.data());
  if (fd < 0)
    throw Error{ErrorCode::io, errno};
  bool closed = false;
  try {
    struct stat prior{};
    if (exists && ::stat(path.c_str(), &prior) != 0)
      throw Error{ErrorCode::io, errno};
    if (::fchmod(fd, exists ? prior.st_mode & 0777 : 0644) != 0)
      throw Error{ErrorCode::io, errno};
    auto remaining = bytes;
    while (!remaining.empty()) {
      auto count = ::write(fd, remaining.data(), remaining.size());
      if (count < 0) {
        if (errno == EINTR)
          continue;
        throw Error{ErrorCode::io, errno};
      }
      if (count == 0)
        throw Error{ErrorCode::io};
      remaining.remove_prefix(static_cast<std::size_t>(count));
    }
    if (::fsync(fd) != 0)
      throw Error{ErrorCode::io, errno};
    const int rc = ::close(fd);
    closed = true;
    if (rc != 0)
      throw Error{ErrorCode::io, errno};
    if (::rename(name.data(), path.c_str()) != 0)
      throw Error{ErrorCode::io, errno};
    if (effect)
      effect->replaced();
  } catch (...) {
    if (!closed)
      (void)::close(fd);
    (void)::unlink(name.data());
    throw;
  }
}
void write_file(const std::filesystem::path &path, std::string_view bytes) {
  write_file_observed(path, bytes, nullptr);
}
std::string read_file(const std::filesystem::path &path, std::size_t limit,
                      const RawAdapterContext *context, RawEventRef *actual_owner) {
  if (!context || !context->parent || !raw_enabled())
    return read_file(path, limit);
  return raw_file_read(path, limit, context, actual_owner);
}
void write_file(const std::filesystem::path &path, std::string_view bytes,
                const RawAdapterContext *context, const RawFileInput *input,
                FileEffectObservation *effect) {
  if (!context || !context->parent || !raw_enabled()) {
    write_file_observed(path, bytes, effect);
    return;
  }
  raw_file_write(path, bytes, context, input, effect);
}
void LocalTools::capture(std::string label, std::string raw, RawFileToolTrace *trace) {
  if (trace)
    trace->observer(static_cast<bool>(observer_), [&] { observer_(label, raw); });
  else if (observer_)
    observer_(label, raw);
  captured_.push_back({std::move(label), std::move(raw)});
}
namespace {
Value display_bytes(const std::string &raw) {
  if (valid_utf8(raw))
    return Value{raw};
  constexpr char hex[] = "0123456789abcdef";
  std::string encoded;
  for (const char ch : raw) {
    const auto c = static_cast<unsigned char>(ch);
    encoded += hex[c >> 4U];
    encoded += hex[c & 15U];
  }
  return Value::object(
      {{"encoding", Value{"hex"}}, {"bytes", Value{std::move(encoded)}}});
}
std::size_t range_index(const Value &args, std::string_view key, std::size_t fallback) {
  const auto *value = args.find(key);
  if (!value)
    return fallback;
  if (!std::holds_alternative<Number>(value->value()))
    throw Error{ErrorCode::invalid_range};
  const auto &text = value->number().text();
  std::size_t index = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), index);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    throw Error{ErrorCode::invalid_range};
  return index;
}
// Normalize the single advertised selector into the existing slice machinery.
// Historical Lua programs may still use one legacy range family.
Value file_range(const Value &args, RawFileToolTrace *trace = nullptr) {
  const auto *range = args.find("range");
  if (!range)
    return args;
  if (args.find("byte_start") || args.find("byte_end") || args.find("line_start") ||
      args.find("line_end") || !std::holds_alternative<Value::Object>(range->value()))
    throw Error{ErrorCode::invalid_range};
  for (const auto &[key, value] : range->object()) {
    (void)value;
    if (key != "mode" && key != "start" && key != "end")
      throw Error{ErrorCode::invalid_range};
  }
  const auto *mode = range->find("mode");
  if (!mode || !std::holds_alternative<std::string>(mode->value()) ||
      (mode->string() != "lines" && mode->string() != "bytes"))
    throw Error{ErrorCode::invalid_range};
  const bool lines = mode->string() == "lines";
  const auto start = range_index(*range, "start", lines ? 1 : 0);
  const auto end = range_index(*range, "end", SIZE_MAX);
  if (trace)
    trace->range(lines ? 2 : 1, start, end);
  if ((lines && start == 0) || end < start)
    throw Error{ErrorCode::invalid_range};
  Value::Object normalized;
  for (const auto key : {"start", "end"})
    if (const auto *value = range->find(key))
      normalized.emplace_back(std::string{lines ? "line_" : "byte_"} + key, *value);
  // Retain mode even when both endpoints are omitted.
  if (!range->find("start"))
    normalized.emplace_back(lines ? "line_start" : "byte_start",
                            Value{Number{lines ? "1" : "0"}});
  return Value::object(std::move(normalized));
}
std::string file_presentation(const std::string &raw, const Value &args,
                              RawFileToolTrace *trace = nullptr) {
  const bool bytes = args.find("byte_start") || args.find("byte_end");
  const bool lines = args.find("line_start") || args.find("line_end");
  if (bytes && lines)
    throw Error{ErrorCode::invalid_range};
  if (bytes) {
    const auto start = range_index(args, "byte_start", 0);
    // An omitted end means EOF even when start is beyond it.
    const auto end = range_index(args, "byte_end", std::max(start, raw.size()));
    if (trace)
      trace->range(1, start, end);
    if (end < start)
      throw Error{ErrorCode::invalid_range};
    const auto first = std::min(start, raw.size());
    return raw.substr(first, std::min(end, raw.size()) - first);
  }
  if (lines) {
    const auto start = range_index(args, "line_start", 1);
    const auto end = range_index(args, "line_end", SIZE_MAX);
    if (trace)
      trace->range(2, start, end);
    if (start == 0 || end < start)
      throw Error{ErrorCode::invalid_range};
    std::size_t first = raw.size(), last = raw.size(), line = 1, pos = 0;
    while (pos < raw.size()) {
      if (line == start)
        first = pos;
      const auto lf = raw.find('\n', pos);
      pos = lf == std::string::npos ? raw.size() : lf + 1;
      if (line == end) {
        last = pos;
        break;
      }
      ++line;
    }
    return raw.substr(first, last - first);
  }
  return raw;
}
} // namespace
Value file_content_presentation(const std::string &raw, const Value &args,
                                RawFileToolTrace *trace) {
  const auto budget = range_index(args, "output_max_bytes", SIZE_MAX);
  const auto range = file_range(args, trace);
  auto selected = file_presentation(raw, range, trace);
  const auto selected_bytes = selected.size();
  if (selected.size() > budget)
    selected.resize(budget);
  const auto returned_bytes = selected.size();
  if (trace)
    trace->result(returned_bytes);
  auto result = Value::object({{"content", display_bytes(selected)}});
  if (args.find("output_max_bytes")) {
    result.object().emplace_back("file_bytes", Value{Number{raw.size()}});
    result.object().emplace_back("selected_bytes", Value{Number{selected_bytes}});
    result.object().emplace_back("returned_bytes", Value{Number{returned_bytes}});
    result.object().emplace_back("omitted_bytes",
                                 Value{Number{selected_bytes - returned_bytes}});
  }
  return result;
}
Value process_output_presentation(const std::string &raw, const Value &args) {
  const auto shown = file_presentation(raw, args);
  return Value::object({{"output", display_bytes(shown)},
                        {"output_bytes", Value{Number{raw.size()}}},
                        {"returned_bytes", Value{Number{shown.size()}}},
                        {"omitted_bytes", Value{Number{raw.size() - shown.size()}}}});
}
Value LocalTools::run(std::string_view name, const Value &args,
                      const RawAdapterContext *raw_context,
                      RawControlToken *native_return,
                      FileEffectObservation *file_effect) {
  if (name == "read_file" || name == "write_file" || name == "edit_file") {
    const auto method = name == "read_file"    ? RawFileMethod::read
                        : name == "write_file" ? RawFileMethod::write
                                               : RawFileMethod::edit;
    RawFileToolTrace trace{raw_context, method};
    return trace.run([&] {
      captured_.clear();
      if (name == "read_file") {
        const auto *path_value = args.find("path");
        if (args.find("snapshot") || !path_value ||
            !std::holds_alternative<std::string>(path_value->value()))
          throw Error{ErrorCode::invalid_range};
        (void)file_range(args);
        (void)range_index(args, "output_max_bytes", SIZE_MAX);
        const auto &path = string_field(args, "path");
        trace.argument(0, path);
        RawEventRef read_owner{};
        auto raw = read_file(path, original_limit_, trace.context(), &read_owner);
        trace.selection(1, read_owner, raw.size());
        capture("file.read", raw, &trace);
        return file_content_presentation(raw, args, &trace);
      }
      const auto &path = string_field(args, "path");
      trace.argument(0, path);
      std::string next;
      RawFileInput input{};
      RawEventRef read_owner{};
      if (name == "edit_file") {
        const auto &old = string_field(args, "old");
        trace.argument(1, old);
        const auto &replacement = string_field(args, "new");
        trace.argument(2, replacement);
        if (old.empty())
          throw Error{ErrorCode::invalid_range};
        next = read_file(path, original_limit_, trace.context(), &read_owner);
        trace.selection(2, read_owner, next.size());
        capture("file.before", next, &trace);
        const auto pos = next.find(old);
        trace.selection(3, read_owner, next.size(), pos);
        if (pos == std::string::npos)
          throw Error{ErrorCode::conflict};
        const auto duplicate = next.find(old, pos + 1);
        if (duplicate != std::string::npos) {
          trace.selection(4, read_owner, next.size(), duplicate);
          throw Error{ErrorCode::conflict};
        }
        const auto base = next.size() - old.size();
        if (replacement.size() > SIZE_MAX - base)
          throw Error{ErrorCode::overflow};
        if (base + replacement.size() > original_limit_)
          throw Error{ErrorCode::capacity};
        auto proposal = trace.prepare_proposal(base + replacement.size());
        next.replace(pos, old.size(), replacement);
        input = trace.proposed(next, proposal);
        trace.selection(5, input.body, input.length);
      } else {
        const auto &content = string_field(args, "content");
        if (content.size() > original_limit_)
          throw Error{ErrorCode::capacity};
        trace.argument(3, content);
        auto proposal = trace.prepare_proposal(content.size());
        next = content;
        input = trace.proposed(next, proposal);
        trace.selection(5, input.body, input.length);
        std::error_code error;
        if (raw_file_exists(path, error, trace.context())) {
          auto before = read_file(path, original_limit_, trace.context(), &read_owner);
          trace.selection(2, read_owner, before.size());
          capture("file.before", before, &trace);
        }
        if (error)
          throw Error{ErrorCode::io, error.value()};
      }
      auto result = Value::object({{"written", Value{true}},
                                   {"path", Value{path}},
                                   {"effect_outcome", Value{"applied"}}});
      capture("file.proposed", next, &trace);
      write_file(path, next, trace.context(), &input, file_effect);
      trace.result(next.size());
      return result;
    });
  }
  captured_.clear();
  if (name == "exec") {
    const auto budget = range_index(args, "output_max_bytes", SIZE_MAX);
    int seconds = 120;
    if (const auto *n = args.find("timeout_seconds")) {
      if (!std::holds_alternative<Number>(n->value()))
        throw Error{ErrorCode::invalid_range};
      const auto &s = n->number().text();
      auto r = std::from_chars(s.data(), s.data() + s.size(), seconds);
      if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || seconds < 1 ||
          seconds > 3600)
        throw Error{ErrorCode::invalid_range};
    }
    // Arbitrary programs may setsid/detach. Group cleanup is not containment.
    detail::uncontained_exec.store(true);
    detail::Child child;
    if (raw_context && raw_context->parent) {
      child.raw_capture(*raw_context->parent);
      child.original_capture = raw_context->originals;
    }
    child.cancelled = cancelled;
    child.output_observer = [this](std::string_view chunk) {
      if (observer_)
        observer_("process.output", chunk);
    };
    std::vector<std::string> argv;
    if (const auto *array = args.find("argv")) {
      if (!std::holds_alternative<Value::Array>(array->value()))
        throw Error{ErrorCode::invalid_range};
      for (const auto &value : array->array()) {
        if (!std::holds_alternative<std::string>(value.value()) ||
            value.string().find('\0') != std::string::npos)
          throw Error{ErrorCode::invalid_range};
        argv.push_back(value.string());
      }
    }
    if (argv.empty()) {
      const auto &command = string_field(args, "command");
      if (command.find('\0') != std::string::npos)
        throw Error{ErrorCode::invalid_range};
      argv = {"/bin/sh", "-c", command};
    }
    child.start(std::move(argv), {}, true);
    child.close_input();
    std::string output;
    int status = 0;
    try {
      output = child.collect(detail::Clock::now() + std::chrono::seconds{seconds},
                             original_limit_, &status);
    } catch (...) {
      captured_.push_back({"process.partial", child.take_partial()});
      throw;
    }
    if (raw_context && raw_context->parent && native_return) {
      const auto stage = std::uint8_t{82}, variant = std::uint8_t{4};
      const std::array fields{ByteView{raw_context->parent->lane},
                              detail::raw_native_field(raw_context->parent->ordinal),
                              detail::raw_native_field(stage),
                              detail::raw_native_field(variant),
                              detail::raw_native_field(status)};
      const auto result = raw_emit(1323, fields, RawPartition::control, native_return);
      if (!result.has_value())
        raw_fence(result.error());
    }
    auto shown = display_bytes(output.substr(0, budget));
    const auto total = output.size();
    captured_.push_back({"process.combined", std::move(output)});
    auto result = Value::object(
        {{"output", std::move(shown)}, {"exit_code", Value{Number{status}}}});
    if (args.find("output_max_bytes")) {
      result.object().emplace_back("output_bytes", Value{Number{total}});
      result.object().emplace_back("omitted_bytes",
                                   Value{Number{total - std::min(total, budget)}});
    }
    return result;
  }
  throw Error{ErrorCode::unsupported};
}
Value tool_definitions() {
  return Value{Value::Array{
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"participant_read"}},
           {"description",
            Value{"Read bounded observed runs; optional run_id returns its result. "
                  "States running/waiting/completed/cancelled/failed/unknown. Waiting "
                  "means available for addressed direction, not completed."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"run_id", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"participant_configure"}},
           {"description", Value{"Set concurrency1..16 only while all runs are "
                                 "settled. Default2; max32 resident runs."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"concurrency", Value::object({{"type", Value{"integer"}}})}})},
                 {"required", Value{Value::Array{Value{"concurrency"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"participant_start"}},
           {"description",
            Value{"Start a bounded context-only participant from explicit colleague "
                  "request; optional existing task_id publishes observed badges. No "
                  "task completion. Selected request retained before dispatch. Returns "
                  "run_id; scheduling success is not remote completion."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"request", Value::object({{"type", Value{"object"}}})},
                       {"task_id", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"request"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"participant_send"}},
           {"description",
            Value{"Send addressed direction at next request boundary. Exact duplicate "
                  "message_id deduplicates during this process; conflicting reuse "
                  "rejects. Inbox16, deliveries64/run, text2048B. Current prior answer "
                  "selected at acceptance if available."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"run_id", Value::object({{"type", Value{"string"}}})},
                       {"message_id", Value::object({{"type", Value{"string"}}})},
                       {"from", Value::object({{"type", Value{"string"}}})},
                       {"text", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"run_id"}, Value{"message_id"},
                                                 Value{"from"}, Value{"text"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"participant_cancel"}},
           {"description", Value{"Request cancellation; cancellation_requested differs "
                                 "from observed settlement and remote disposition. "
                                 "Does not automatically cancel other runs."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"run_id", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"run_id"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"participant_await"}},
           {"description",
            Value{"Wait for current run_id and already accepted directions to settle "
                  "into waiting/finished without sealing message admission. Optional "
                  "timeout_ms1..3600000 default30000. await_timed_out does not cancel "
                  "the worker; interruption stops waiting only."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"run_id", Value::object({{"type", Value{"string"}}})},
                       {"timeout_ms", Value::object({{"type", Value{"integer"}}})}})},
                 {"required", Value{Value::Array{Value{"run_id"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"participant_join"}},
           {"description",
            Value{"Seal addressed runs against new messages, drain accepted directions "
                  "and join in declared order. Cooperative interruption stops waiting; "
                  "it does not cancel workers. Inspect states/results/remote "
                  "disposition."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"run_ids",
                        Value::object(
                            {{"type", Value{"array"}},
                             {"items",
                              Value::object({{"type", Value{"string"}}})}})}})},
                 {"required", Value{Value::Array{Value{"run_ids"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"participant_archive"}},
           {"description",
            Value{"Remove a settled run from current resident view to free capacity; "
                  "retained originals remain. No replay on process reopen."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"run_id", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"run_id"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"provider_auth_status"}},
           {"description",
            Value{"Read provider account labels, credential kind and expiry state. No "
                  "tokens, refresh or network probes. Sign-in/key entry only through "
                  "standalone operator auth CLI."}},
           {"parameters", Value::object({{"type", Value{"object"}},
                                         {"properties", Value::object({})}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"colleague_catalog"}},
           {"description",
            Value{"Discover supported colleague providers and installed transports. "
                  "Authentication and model availability stay not_checked until an "
                  "actual call. No network probe."}},
           {"parameters", Value::object({{"type", Value{"object"}},
                                         {"properties", Value::object({})}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"colleague"}},
           {"description",
            Value{
                "Call one selected-context colleague, no tools, continuation or "
                "retries. Same request/result as Lua blackbird.colleague and "
                "/colleague. Explicit from/to/request_id addresses; context is "
                "[{id,text}], no automatic transcript. Request max64KiB. profile "
                "tools=none,requests=1,timeout_seconds1..3600. Requested model differs "
                "from observed actual_model. Noncompleted status must be inspected; "
                "remote_disposition unknown never permits implicit redispatch."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"request_id", Value::object({{"type", Value{"string"}}})},
                       {"from", Value::object({{"type", Value{"string"}}})},
                       {"to", Value::object({{"type", Value{"string"}}})},
                       {"provider", Value::object({{"type", Value{"string"}},
                                                   {"minLength", Value{Number{1}}}})},
                       {"model", Value::object({{"type", Value{"string"}}})},
                       {"task", Value::object({{"type", Value{"string"}}})},
                       {"context",
                        Value::object(
                            {{"type", Value{"array"}},
                             {"items",
                              Value::object(
                                  {{"type", Value{"object"}},
                                   {"properties",
                                    Value::object(
                                        {{"id",
                                          Value::object({{"type", Value{"string"}}})},
                                         {"text", Value::object(
                                                      {{"type", Value{"string"}}})}})},
                                   {"required", Value{Value::Array{
                                                    Value{"id"}, Value{"text"}}}}})}})},
                       {"profile",
                        Value::object(
                            {{"type", Value{"object"}},
                             {"properties",
                              Value::object(
                                  {{"name", Value::object({{"type", Value{"string"}}})},
                                   {"provenance",
                                    Value::object({{"type", Value{"string"}}})},
                                   {"timeout_seconds",
                                    Value::object({{"type", Value{"integer"}}})},
                                   {"tools",
                                    Value::object({{"type", Value{"string"}},
                                                   {"enum", Value{Value::Array{
                                                                Value{"none"}}}}})},
                                   {"requests",
                                    Value::object({{"type", Value{"integer"}},
                                                   {"enum", Value{Value::Array{Value{
                                                                Number{1}}}}}})}})},
                             {"required",
                              Value{Value::Array{Value{"name"}, Value{"provenance"},
                                                 Value{"timeout_seconds"},
                                                 Value{"tools"},
                                                 Value{"requests"}}}}})}})},
                 {"required",
                  Value{Value::Array{Value{"request_id"}, Value{"from"}, Value{"to"},
                                     Value{"provider"}, Value{"model"}, Value{"task"},
                                     Value{"context"}, Value{"profile"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"tasks_read"}},
           {"description",
            Value{"Read current durable session task/subtask state. query: id, status "
                  "(open|queued|active|blocked|done|dropped), offset (default0), "
                  "limit1..64 (default32), revision to guard pages. Returns stable "
                  "IDs, item versions, list revision, parent counts, child rollups, "
                  "and next cursor. Exactly two levels. Empty state is explicit; "
                  "unfinished work survives compaction/reopen."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"query", Value::object({{"type", Value{"object"}}})}})}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"tasks_edit"}},
           {"description",
            Value{
                "Atomically edit shared session tasks. op_id is a short unique batch "
                "key; identical retries return the same result for the last16 edits; "
                "older stale guarded retries conflict. ops1..64: add(title,parent? "
                "top-level ID,status?,owner?,note?,blocker?,after? sibling ID), "
                "set(id,version,title?/status?/owner?/note?/blocker?), "
                "move(id,version,parent? empty=root,after?), archive(id,version; "
                "includes children), list(title?/bead?). Structural "
                "add/move/archive/list require current list base. set requires current "
                "item version and may omit base. One edit per existing row per batch. "
                "States queued/active/blocked/done/dropped; no automatic completion or "
                "single-active limit. At most2048 resident items; title512B,note2048B. "
                "Rejection leaves state unchanged; conflicts return current affected "
                "rows. Changed rows and added IDs returned, no full-list echo."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"op_id", Value::object({{"type", Value{"string"}}})},
                       {"base", Value::object({{"type", Value{"integer"}}})},
                       {"ops",
                        Value::object(
                            {{"type", Value{"array"}},
                             {"items",
                              Value::object(
                                  {{"type", Value{"object"}},
                                   {"properties",
                                    Value::object(
                                        {{"op",
                                          Value::object(
                                              {{"type", Value{"string"}},
                                               {"enum", Value{Value::Array{
                                                            Value{"add"}, Value{"set"},
                                                            Value{"move"},
                                                            Value{"archive"},
                                                            Value{"list"}}}}})},
                                         {"id",
                                          Value::object({{"type", Value{"string"}}})},
                                         {"version",
                                          Value::object({{"type", Value{"integer"}}})},
                                         {"title",
                                          Value::object({{"type", Value{"string"}}})},
                                         {"parent",
                                          Value::object({{"type", Value{"string"}}})},
                                         {"after",
                                          Value::object({{"type", Value{"string"}}})},
                                         {"status",
                                          Value::object(
                                              {{"type", Value{"string"}},
                                               {"enum",
                                                Value{Value::Array{
                                                    Value{"queued"}, Value{"active"},
                                                    Value{"blocked"}, Value{"done"},
                                                    Value{"dropped"}}}}})},
                                         {"owner",
                                          Value::object({{"type", Value{"string"}}})},
                                         {"note",
                                          Value::object({{"type", Value{"string"}}})},
                                         {"blocker",
                                          Value::object({{"type", Value{"string"}}})},
                                         {"bead", Value::object(
                                                      {{"type", Value{"string"}}})}})},
                                   {"required",
                                    Value{Value::Array{Value{"op"}}}}})}})}})},
                 {"required", Value{Value::Array{Value{"op_id"}, Value{"ops"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"workflow_registry"}},
           {"description", Value{"Discover effective registered Lua workflows, full "
                                 "retained definitions, slash prefix and configuration "
                                 "revision. Same registry as operator help/completion "
                                 "and invocation; pending edits are not callable."}},
           {"parameters", Value::object({{"type", Value{"object"}},
                                         {"properties", Value::object({})}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"workflow_invoke"}},
           {"description",
            Value{"Invoke an effective registered Lua workflow in this audited turn. "
                  "With unanswered provider calls, returns accepted and executes after "
                  "tool outputs before next request or boundary; completion is "
                  "appended to context. Otherwise synchronous. Source is a function "
                  "body with args containing this invocation object. Bound nesting 8; "
                  "no durable or parallel scheduler. Edits activate only after "
                  "successful outer turn."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"name", Value::object({{"type", Value{"string"}}})},
                       {"arguments", Value::object({{"type", Value{"string"}}})},
                       {"prompt", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"name"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"program_config"}},
           {"description",
            Value{
                "Inspect or stage retained named Lua modules and request defaults "
                "(model/effort; empty uses launch defaults). Proposal is complete "
                "{modules:[{name,source}],model,effort}, with optional workflow_prefix "
                "and prompt_cache:provider_default|stable_prefix|disabled, and "
                "workflows:[{name,description,source,aliases:[string],bare:boolean,"
                "powerwords:[{token,color}]}]. Workflows source is a Lua function body "
                "with args. Omitted workflow fields disable registered workflows. "
                "Compile-only validation; successful workflow boundary activation; "
                "failure/cancel preserves effective. Optional base binds revision."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"proposal", Value::object({{"type", Value{"object"}}})},
                       {"base", Value::object({{"type", Value{"string"}}})}})}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"module_source"}},
           {"description",
            Value{"Read an effective named module's retained source and revision. "
                  "Pending sources cannot resolve; blackbird.module(name) loads with a "
                  "per-workflow cache and private environment. Top-level effects are "
                  "not rollbackable."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"name", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"name"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"tool_define"}},
           {"description",
            Value{"Stage a Lua tool definition (name, description, flat scalar-object "
                  "parameters, source function body with args). Activates only on "
                  "successful workflow boundary; failed/invalid definitions preserve "
                  "effective registry. No native name replacement. Optional base binds "
                  "effective registry revision."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"definition", Value::object({{"type", Value{"object"}}})},
                       {"base", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"definition"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"tool_registry"}},
           {"description",
            Value{"Inspect effective and pending session Lua tool definitions and "
                  "revisions. Source is retained; pending definitions cannot dispatch "
                  "until a successful workflow boundary."}},
           {"parameters", Value::object({{"type", Value{"object"}},
                                         {"properties", Value::object({})}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"read_file"}},
           {"description",
            Value{"Read exactly one of path (current disk) or snapshot (retained "
                  "original from a prior read; no disk access). Full originals are "
                  "retained. Default coding workflow returns at most16384 raw bytes "
                  "unless a range or output_max_bytes is explicit; custom Lua defaults "
                  "to full content. output_max_bytes limits the selected range; "
                  "file_bytes/selected_bytes/returned_bytes/omitted_bytes describe "
                  "raw bytes before encoding. Fetch omitted facts with the returned "
                  "snapshot and ONE range object: {mode:lines,start:1,end:20} "
                  "(one-based inclusive) or {mode:bytes,start:0,end:256} (zero-based "
                  "half-open). Missing start means first byte/line; missing end means "
                  "EOF. Beyond EOF clips/returns empty. LF retained; no phantom "
                  "trailing line."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"path", Value::object({{"type", Value{"string"}}})},
                       {"snapshot",
                        Value::object(
                            {{"type", Value{"object"}},
                             {"properties",
                              Value::object(
                                  {{"journal",
                                    Value::object({{"type", Value{"string"}}})},
                                   {"sequence",
                                    Value::object({{"type", Value{"integer"}},
                                                   {"minimum", Value{Number{1}}}})}})},
                             {"required",
                              Value{Value::Array{Value{"journal"}, Value{"sequence"}}}},
                             {"additionalProperties", Value{false}}})},
                       {"output_max_bytes",
                        Value::object({{"type", Value{"integer"}},
                                       {"minimum", Value{Number{0}}}})},
                       {"range",
                        Value::object(
                            {{"type", Value{"object"}},
                             {"properties",
                              Value::object(
                                  {{"mode",
                                    Value::object({{"type", Value{"string"}},
                                                   {"enum", Value{Value::Array{
                                                                Value{"lines"},
                                                                Value{"bytes"}}}}})},
                                   {"start",
                                    Value::object({{"type", Value{"integer"}},
                                                   {"minimum", Value{Number{0}}}})},
                                   {"end",
                                    Value::object({{"type", Value{"integer"}},
                                                   {"minimum", Value{Number{0}}}})}})},
                             {"required", Value{Value::Array{Value{"mode"}}}},
                             {"additionalProperties", Value{false}}})}})},
                 {"additionalProperties", Value{false}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"write_file"}},
           {"description",
            Value{"Write a complete local file. No command approval is required."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"path", Value::object({{"type", Value{"string"}}})},
                       {"content", Value::object({{"type", Value{"string"}}})}})},
                 {"required",
                  Value{Value::Array{Value{"path"}, Value{"content"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"edit_file"}},
           {"description", Value{"Replace exactly one occurrence; refuses missing or "
                                 "ambiguous old text."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object({{"path", Value::object({{"type", Value{"string"}}})},
                                 {"old", Value::object({{"type", Value{"string"}}})},
                                 {"new", Value::object({{"type", Value{"string"}}})}})},
                 {"required",
                  Value{Value::Array{Value{"path"}, Value{"old"}, Value{"new"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"decision_model"}},
           {"description",
            Value{"Native decision-model evaluation; Jev is the first adapter. Batch "
                  "independent Choice/Score/Noul questions over shared state. Returns "
                  "response.model, answers, raw probabilities/confidence and usage "
                  "with audit_ref. Choice criteria: map (up to255 options); Score:2–10 "
                  "structured levels; Noul criteria optional true/false. Instructions "
                  "may be string/object/array. No automatic thresholds, retries or "
                  "cache; compose judgments in code. Auth is host-configured, not an "
                  "argument."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"provider",
                        Value::object({{"type", Value{"string"}},
                                       {"enum", Value{Value::Array{Value{"jev"}}}}})},
                       {"model", Value::object({{"type", Value{"string"}}})},
                       {"state", Value::object({})},
                       {"questions", Value::object({{"type", Value{"object"}}})},
                       {"timeout_seconds",
                        Value::object({{"type", Value{"integer"}}})}})},
                 {"required",
                  Value{Value::Array{Value{"state"}, Value{"questions"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"beads"}},
           {"description",
            Value{"Native external bd 0.58.0 adapter. Configure explicit canonical "
                  "project, absolute executable and exact actor lazily first. ready "
                  "uses blocker-aware upstream semantics. Cached data never polls. "
                  "Single exact ID show/update/close/claim; claim alone is assignment, "
                  "not ready CAS/lease: recheck task/dependencies. Spawned uncertain "
                  "writes report and retain UNKNOWN: never blindly replay. No combined "
                  "dependencies, explicit-ID create, or exactly-once promise. Raw "
                  "stdout/stderr retained under audit_ref."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"op",
                        Value::object({{"type", Value{"string"}},
                                       {"enum", Value{Value::Array{
                                                    Value{"configure"}, Value{"cached"},
                                                    Value{"ready"}, Value{"list"},
                                                    Value{"show"}, Value{"create"},
                                                    Value{"update"}, Value{"close"},
                                                    Value{"claim"}}}}})},
                       {"project", Value::object({{"type", Value{"string"}}})},
                       {"executable", Value::object({{"type", Value{"string"}}})},
                       {"actor", Value::object({{"type", Value{"string"}}})},
                       {"id", Value::object({{"type", Value{"string"}}})},
                       {"limit", Value::object({{"type", Value{"integer"}}})},
                       {"title", Value::object({{"type", Value{"string"}}})},
                       {"body", Value::object({{"type", Value{"string"}}})},
                       {"type", Value::object({{"type", Value{"string"}}})},
                       {"priority", Value::object({{"type", Value{"string"}}})},
                       {"status", Value::object({{"type", Value{"string"}}})},
                       {"reason", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"op"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"exec"}},
           {"description",
            Value{
                "Execute a shell command or nonempty argv on the host. Nonempty argv "
                "takes precedence; empty argv uses command. Captures combined "
                "stdout/stderr; omitted yield_ms waits to completion, zero releases "
                "immediately. "
                "Background returns job_id without exit_code; process foreground waits "
                "on the same job. "
                "pty=true enables interactive text input. timeout_seconds defaults to "
                "120 and never renews. A "
                "deadline returns timed_out=true and effect_outcome=unknown after "
                "local child-group cleanup, with output_ref for partial output; "
                "inspect it and choose a distinct retry or another approach. Operator "
                "cancellation stops the turn. Optional output_max_bytes bounds raw "
                "prefix bytes shown to model; full original chunks remain audited. "
                "output_ref can retrieve omitted output using read_process_output. "
                "output_limit_bytes (default 1048576, max 16777216) bounds what the "
                "command may write: past it reading stops, the command is stopped "
                "and the result has output_limit_exceeded=true and "
                "effect_outcome=unknown with the bytes read. Prefer redirecting "
                "large output to a file; a larger limit uses more audit capacity. "
                "A command-registry limit returns capacity with resource, count, "
                "limit, attempt and effect_outcome=not_started; no command launched "
                "and there is no output_ref. Inspect jobs before choosing further "
                "work."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"task_id",
                        Value::object(
                            {{"type", Value{"string"}},
                             {"description", Value{"Optional existing task/subtask ID; "
                                                   "empty means unbound. "
                                                   "displays observed runtime activity "
                                                   "without completing it."}}})},
                       {"command", Value::object({{"type", Value{"string"}}})},
                       {"argv",
                        Value::object(
                            {{"type", Value{"array"}},
                             {"items", Value::object({{"type", Value{"string"}}})}})},
                       {"timeout_seconds", Value::object({{"type", Value{"integer"}}})},
                       {"yield_ms",
                        Value::object({{"type", Value{"integer"}},
                                       {"minimum", Value{Number{0}}},
                                       {"maximum", Value{Number{3600000}}}})},
                       {"pty", Value::object({{"type", Value{"boolean"}}})},
                       {"output_max_bytes",
                        Value::object({{"type", Value{"integer"}},
                                       {"minimum", Value{Number{0}}}})},
                       {"output_limit_bytes",
                        Value::object({{"type", Value{"integer"}},
                                       {"minimum", Value{Number{1}}},
                                       {"maximum", Value{Number{16777216}}}})}})}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"process"}},
           {"description",
            Value{"Inspect/control the original command job. op read lists jobs or "
                  "reads a bounded absolute byte cursor; foreground waits without "
                  "relaunch or deadline renewal, optional yield_ms. background "
                  "releases only its current wait. input requires PTY, exact bytes and "
                  "request_id; the same identity/content is never written twice, "
                  "conflicting reuse fails. resize uses rows/columns. signal names "
                  "stop/continue/interrupt/terminate/kill; acceptance differs from "
                  "observed effect. stop requests cleanup; archive requires completed "
                  "job. configure max_live/max_records only with no live jobs. Active "
                  "jobs block restart/session switch. Admission pressure retires the "
                  "oldest safely settled record; its audited output_ref stays readable "
                  "with read_process_output, while process controls require a resident "
                  "job. No live/unsettled job is evicted; custody is process-lifetime "
                  "only."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"op", Value::object({{"type", Value{"string"}}})},
                       {"job_id", Value::object({{"type", Value{"string"}}})},
                       {"request_id", Value::object({{"type", Value{"string"}}})},
                       {"bytes", Value::object({{"type", Value{"string"}}})},
                       {"signal", Value::object({{"type", Value{"string"}}})},
                       {"yield_ms", Value::object({{"type", Value{"integer"}}})},
                       {"offset", Value::object({{"type", Value{"integer"}}})},
                       {"count", Value::object({{"type", Value{"integer"}}})},
                       {"rows", Value::object({{"type", Value{"integer"}}})},
                       {"columns", Value::object({{"type", Value{"integer"}}})},
                       {"wait_epoch", Value::object({{"type", Value{"integer"}}})},
                       {"max_live", Value::object({{"type", Value{"integer"}}})},
                       {"max_records", Value::object({{"type", Value{"integer"}}})}})},
                 {"required", Value{Value::Array{Value{"op"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"read_process_output"}},
           {"description",
            Value{"Read retained combined process output without rerunning effects. "
                  "output_ref from exec identifies audited attempt. Optional "
                  "byte_start/byte_end zero-based half-open, beyond EOF clips; missing "
                  "end means EOF. Binary slices return hex."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"output_ref", Value::object({{"type", Value{"string"}}})},
                       {"byte_start", Value::object({{"type", Value{"integer"}},
                                                     {"minimum", Value{Number{0}}}})},
                       {"byte_end", Value::object({{"type", Value{"integer"}},
                                                   {"minimum", Value{Number{0}}}})}})},
                 {"required", Value{Value::Array{Value{"output_ref"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"variables_read"}},
           {"description",
            Value{"Read retained time-indexed central-variable observations. query "
                  "uses trajectory_read paging plus optional variable name. Starts "
                  "at0; next advances examined facts (scan cap256) even with no "
                  "matches; pin end. Includes value, source, observation_status and "
                  "UTC/monotonic clock mapping with synchronization unknown. Point "
                  "observations never establish effective intervals or causality."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"query", Value::object({{"type", Value{"object"}}})}})},
                 {"required", Value{Value::Array{Value{"query"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"git_observe"}},
           {"description",
            Value{"Observe requested local Git repository HEAD and optional "
                  "history0..64. Records git.head/git.commit point samples with "
                  "provenance. Commit author/committer times are metadata, not when "
                  "code became active; HEAD is not loaded executable identity. "
                  "Read-only Git calls; no commit, push, file restoration or implicit "
                  "retries. Missing/unborn repository recorded unavailable. Observe "
                  "explicitly when useful, no background polling."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"path", Value::object({{"type", Value{"string"}}})},
                       {"history", Value::object({{"type", Value{"integer"}},
                                                  {"minimum", Value{Number{0}}},
                                                  {"maximum", Value{Number{64}}}})}})},
                 {"required", Value{Value::Array{Value{"path"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"trajectory_read"}},
           {"description",
            Value{"Read bounded trajectory metadata in recorded order. query: cursor, "
                  "end pinned prefix, count1..64(default32), scan1..256(default128), "
                  "optional attempt32 lowercase hex, variables boolean, variable name "
                  "filter. Omit end to snapshot current facts; end0 is empty. Without "
                  "cursor, start at max(0,end-128), or0 with an attempt or variable "
                  "filter or variables=true, and "
                  "return the FIRST count matches in ascending order, not the newest "
                  "count. For an unfiltered newest page, obtain end then request "
                  "cursor=max(0,end-count), end=end. Pin returned end and page with "
                  "cursor=next until caught_up. next "
                  "advances by examined facts even without matches; caught_up refers "
                  "only to pinned end. Follow existing causal IDs; missing settlement "
                  "is not success. No originals copied; drill down with audit_inspect "
                  "record/source. No automatic replay or network."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"query",
                        Value::object(
                            {{"type", Value{"object"}},
                             {"properties",
                              Value::object(
                                  {{"cursor",
                                    Value::object({{"type", Value{"integer"}},
                                                   {"minimum", Value{Number{0}}}})},
                                   {"end",
                                    Value::object({{"type", Value{"integer"}},
                                                   {"minimum", Value{Number{0}}}})},
                                   {"count",
                                    Value::object({{"type", Value{"integer"}},
                                                   {"minimum", Value{Number{1}}},
                                                   {"maximum", Value{Number{64}}}})},
                                   {"scan",
                                    Value::object({{"type", Value{"integer"}},
                                                   {"minimum", Value{Number{1}}},
                                                   {"maximum", Value{Number{256}}}})},
                                   {"attempt",
                                    Value::object(
                                        {{"type", Value{"string"}},
                                         {"pattern", Value{"^[0-9a-f]{32}$"}}})},
                                   {"variables",
                                    Value::object({{"type", Value{"boolean"}}})},
                                   {"variable",
                                    Value::object(
                                        {{"type", Value{"string"}},
                                         {"maxLength", Value{Number{64}}}})}})},
                             {"additionalProperties", Value{false}}})}})},
                 {"required", Value{Value::Array{Value{"query"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"audit_inspect"}},
           {"description",
            Value{
                "Bounded local audit explorer. query: cursor (fact index), count 1..64 "
                "default32, end (pin returned append-only prefix). Omit end for "
                "the current snapshot; end0 is explicitly empty. Default cursor0 "
                "reads the oldest facts. Page with cursor=next and the SAME end. "
                "For a newest unfiltered page, obtain end then request "
                "cursor=max(0,end-count), end=end. For exact bytes: "
                "record index, optional source dependency index, offset, limit "
                "1..65536. Optional record+packet=true returns decoded metadata "
                "(log/program, operation continuation or terminal attempt); limit "
                "bounds both stored and expanded bytes, no source/diagnostic/offset. "
                "Returns causal IDs, source references, explicit observed outcomes and "
                "hex original payload/source pages. Committed facts only; no replay. "
                "Source pages copy one existing document under its document bound, not "
                "full history. Raw private audit stays local."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"query", Value::object({{"type", Value{"object"}}})}})},
                 {"required", Value{Value::Array{Value{"query"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"rageshake"}},
           {"description", Value{"Advisory model complaint: observation and optional "
                                 "evidence references captured immutably with current "
                                 "context/program/model/audit identities; attempt bead "
                                 "delivery with only local locator, retain locally on "
                                 "failure/unknown. Does not compel immediate repair or "
                                 "replay delivery. External sink is not configured."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"observation", Value::object({{"type", Value{"string"}}})},
                       {"references", Value::object({{"type", Value{"object"}}})}})},
                 {"required", Value{Value::Array{Value{"observation"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"context_budget"}},
           {"description",
            Value{"Inspect effective/pending opt-in context byte policy and native "
                  "provider/model capability provenance. A valid proposal stages until "
                  "successful workflow completion; failure cancels it. Trigger invites "
                  "model-authored managed compaction, never forces shrink. Target is "
                  "advisory; original repair can grow context. Omitted base binds "
                  "current policy revision. No token estimates or physical audit "
                  "reclamation."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"proposal",
                        Value::object(
                            {{"type", Value{"object"}},
                             {"properties",
                              Value::object(
                                  {{"base", Value::object({{"type", Value{"string"}}})},
                                   {"enabled",
                                    Value::object({{"type", Value{"boolean"}}})},
                                   {"trigger_bytes",
                                    Value::object(
                                        {{"type", Value{"integer"}},
                                         {"minimum", Value{Number{1}}},
                                         {"maximum", Value{Number{67108864}}}})},
                                   {"target_bytes",
                                    Value::object(
                                        {{"type", Value{"integer"}},
                                         {"minimum", Value{Number{1}}},
                                         {"maximum", Value{Number{67108864}}}})},
                                   {"reason",
                                    Value::object({{"type", Value{"string"}}})}})},
                             {"required", Value{Value::Array{
                                              Value{"enabled"}, Value{"trigger_bytes"},
                                              Value{"target_bytes"}, Value{"reason"}}}},
                             {"additionalProperties", Value{false}}})}})},
                 {"additionalProperties", Value{false}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"context_stats"}},
           {"description",
            Value{"Report context entry counts and native binary packet byte sizes, "
                  "last request bytes and actual provider usage in this process (null "
                  "if unavailable). No token estimates."}},
           {"parameters", Value::object({{"type", Value{"object"}},
                                         {"properties", Value::object({})}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"context_view"}},
           {"description", Value{"Inspect current editable context, base revision and "
                                 "stable entry IDs."}},
           {"parameters", Value::object({{"type", Value{"object"}},
                                         {"properties", Value::object({})}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"context_edit"}},
           {"description",
            Value{"Publish candidate {base,entries:[{id,item}]} with CAS. Reorder, "
                  "replace or remove entries. Retained originals are immutable. Keep "
                  "active tool call/result pairs in context."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"candidate", Value::object({{"type", Value{"object"}}})}})},
                 {"required", Value{Value::Array{Value{"candidate"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"context_manage"}},
           {"description",
            Value{
                "Explicit managed summarize/select/archive/restore. Proposal: optional "
                "base (omitted explicitly binds invocation snapshot; supplied is "
                "strict CAS), mode, ids (complete tool groups), reason, source; "
                "summarize also summary {role:assistant or developer,content}. "
                "Preserves all live user/system/developer instructions. Stages until "
                "successful workflow completion; appended current tool exchange is "
                "retained. One pending; failed/interrupted workflows cancel. "
                "Structural acceptance does not certify accuracy."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"proposal", Value::object({{"type", Value{"object"}}})}})},
                 {"required", Value{Value::Array{Value{"proposal"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"context_inspect"}},
           {"description",
            Value{"Bounded recoverable original/history/index inspection. Native BBM2 "
                  "stream by default; format=json explicitly exports JSON. query: kind "
                  "originals|history|index, optional entry (original ID), revision "
                  "(strict per-kind snapshot guard; stale returns conflict, restart "
                  "pagination), offset byte index, limit 1..65536 (default 4096). "
                  "Returns hex-bbm2-stream bytes (or hex-json-utf8 for explicit "
                  "export), total_bytes, next, revision (bind subsequent pages) and "
                  "context_revision. No token estimates."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"query", Value::object({{"type", Value{"object"}}})}})},
                 {"required", Value{Value::Array{Value{"query"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"context_originals"}},
           {"description", Value{"Retrieve retained original context entries, "
                                 "including entries removed from presentation."}},
           {"parameters", Value::object({{"type", Value{"object"}},
                                         {"properties", Value::object({})}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"context_repair"}},
           {"description",
            Value{
                "Explicit protocol repair for calls already unanswered when this "
                "workflow began. Requires current context base. Appends unknown-result "
                "placeholders without dispatch/replay or settling actual effects; "
                "preserves originals and existing outputs. Refuses active-workflow "
                "calls, malformed linkage and live owned children. Use from an "
                "explicit recovery Lua workflow before the next provider request."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"base", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"base"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"context_restore"}},
           {"description",
            Value{"Restore an original context entry by stable entry ID."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"entry", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"entry"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"restart"}},
           {"description",
            Value{"Request restart/resume/continue after the current tool batch and "
                  "turn complete. Build the release executable first. Include changes, "
                  "checks, and next steps in the note. Failed or interrupted turns do "
                  "not restart."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"note", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"note"}}}}})}}),
      Value::object(
          {{"type", Value{"function"}},
           {"name", Value{"lua"}},
           {"description",
            Value{"Run a Lua transformation or experiment in the current workflow. "
                  "blackbird.context(), blackbird.stats(), blackbird.edit(candidate), "
                  "blackbird.originals(), blackbird.restore(id), "
                  "blackbird.append(items), blackbird.json.encode/decode, "
                  "blackbird.call(name,args) are available. Return a native value. "
                  "JSON encode/decode is explicit interchange only. Governing workflow "
                  "file changes activate next turn."}},
           {"parameters",
            Value::object(
                {{"type", Value{"object"}},
                 {"properties",
                  Value::object(
                      {{"code", Value::object({{"type", Value{"string"}}})}})},
                 {"required", Value{Value::Array{Value{"code"}}}}})}})}};
}
} // namespace blackbird
