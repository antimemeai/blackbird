#include "blackbird/workflows.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_program.hpp"
#include "blackbird/tools.hpp"
#include <algorithm>
#include <mutex>
#include <set>
namespace blackbird {
namespace {
std::mutex mutex;
std::shared_ptr<const WorkflowRegistry> displayed;
bool word(unsigned char c) {
  return c >= 128 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}
bool identifier(std::string_view s) {
  return !s.empty() && s.size() <= 64 && std::all_of(s.begin(), s.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
  });
}
Ink ink(std::string_view s) {
  if (s == "keyword")
    return Ink::keyword;
  if (s == "success")
    return Ink::success;
  if (s == "heading")
    return Ink::heading;
  if (s == "failure")
    return Ink::failure;
  throw Error{ErrorCode::invalid_range};
}
const std::string &str(const Value &j, std::string_view k) {
  return field(j, k).string();
}
} // namespace
WorkflowRegistry::WorkflowRegistry(const Value &config, std::string_view revision,
                                   const RawProgramContext *context) {
  RawProgramCall trace{5, context};
  trace.run([&] {
    try {
      trace.stage(1);
      trace.revision(2, revision);
      revision_ = revision;
      if (const auto *p = config.find("workflow_prefix"))
        prefix_ = p->string();
      if (!prefix_.empty() && !identifier(prefix_))
        throw Error{ErrorCode::invalid_range};
      if (const auto *d = config.find("workflows"))
        definitions_ = *d;
      const ValueLimits limits{65536, 500000, 128};
      trace.stage(2);
      auto body = trace.data(49 + limits.bytes);
      const auto origin = trace.context();
      const std::uint8_t kind = 1;
      RawProgramCall encoding{3804, 3805, &origin,
                              std::array{detail::raw_native_field(kind),
                                         detail::raw_native_field(limits.bytes),
                                         detail::raw_native_field(limits.nodes),
                                         detail::raw_native_field(limits.depth)},
                              sizeof(bool)};
      auto encoded = encoding.run([&] {
        auto result = encode_packet_string(definitions_, limits);
        const bool success = result.has_value();
        if (success)
          trace.encoded(kind, result.value(), body);
        encoding.finish(RawOperationBranch::returned,
                        success ? nullptr : &result.error(),
                        std::array{detail::raw_native_field(success)});
        return result;
      });
      if (unwrap(std::move(encoded)).size() > 65536 || definitions_.array().size() > 32)
        throw Error{ErrorCode::capacity};
      std::set<std::string> names, slash_aliases, bare_aliases, tokens;
      for (const auto &n : terminal_builtin_names()) {
        slash_aliases.insert(n.substr(1));
        bare_aliases.insert(n.substr(1));
      }
      std::size_t index = 0;
      for (const auto &d : definitions_.array()) {
        trace.stage(3);
        trace.validation(1, definitions_.array().size(), index);
        if (d.object().size() != 6 || !identifier(str(d, "name")) ||
            !names.insert(str(d, "name")).second || str(d, "description").empty() ||
            str(d, "description").size() > 256 ||
            std::any_of(str(d, "description").begin(), str(d, "description").end(),
                        [](char ch) {
                          const auto c = static_cast<unsigned char>(ch);
                          return c < 32 || c == 127;
                        }) ||
            str(d, "source").empty() || str(d, "source").size() > 16384)
          throw Error{ErrorCode::invalid_range};
        (void)std::get<bool>(field(d, "bare").value());
        const auto &as = field(d, "aliases").array();
        if (as.empty() || as.size() > 8)
          throw Error{ErrorCode::invalid_range};
        for (const auto &a : as) {
          if (!identifier(a.string()) ||
              !slash_aliases.insert(prefix_ + a.string()).second)
            throw Error{ErrorCode::conflict};
          aliases_.emplace("/" + prefix_ + a.string(), index);
          if (std::get<bool>(field(d, "bare").value())) {
            if (!bare_aliases.insert(a.string()).second)
              throw Error{ErrorCode::conflict};
            aliases_.emplace(a.string(), index);
          }
        }
        const auto &ps = field(d, "powerwords").array();
        if (ps.size() > 8)
          throw Error{ErrorCode::capacity};
        for (const auto &p : ps) {
          const auto token = str(p, "token");
          if (p.object().size() != 2 || token.empty() || token.size() > 64 ||
              !std::all_of(token.begin(), token.end(),
                           [](char c) {
                             return static_cast<unsigned char>(c) < 128 &&
                                    word(static_cast<unsigned char>(c));
                           }) ||
              !tokens.insert(token).second)
            throw Error{ErrorCode::conflict};
          powerwords_.emplace(token, Powerword{index, ink(str(p, "color"))});
        }
        ++index;
      }
    } catch (const std::bad_variant_access &) {
      throw Error{ErrorCode::invalid_range};
    }
  });
}
Value WorkflowRegistry::discover(const RawProgramContext *context) const {
  const std::uint8_t kind = 0;
  RawProgramCall trace{3815, 3816, context, std::array{detail::raw_native_field(kind)}};
  return trace.run([&] {
    return Value::object({{"revision", Value{revision_}},
                          {"prefix", Value{prefix_}},
                          {"definitions", definitions_}});
  });
}
const Value &WorkflowRegistry::named(std::string_view name,
                                     const RawProgramContext *context,
                                     RawEventRef *selection_request) const {
  const auto size = revision_.size(), input = name.size();
  RawProgramCall trace{3810, 3813, context,
                       std::array{detail::raw_native_field(size),
                                  detail::raw_string_field(revision_),
                                  detail::raw_native_field(input)},
                       sizeof(bool)};
  if (selection_request)
    *selection_request = trace.reference();
  return trace.run([&]() -> const Value & {
    auto original = trace.data(48 + name.size());
    trace.bytes(3811, name, original);
    for (const auto &d : definitions_.array())
      if (str(d, "name") == name) {
        auto credit = trace.control(42);
        const std::uint8_t route = 3;
        const bool indexed = false, selected = true;
        trace.event(3812,
                    std::array{detail::raw_native_field(route),
                               detail::raw_native_field(indexed)},
                    credit);
        trace.finish(RawOperationBranch::returned, nullptr,
                     std::array{detail::raw_native_field(selected)});
        return d;
      }
    throw Error{ErrorCode::invalid_range};
  });
}
std::optional<WorkflowSelection>
WorkflowRegistry::select(std::string_view text, const RawProgramContext *context,
                         RawEventRef *selection_request) const {
  const auto size = revision_.size(), input = text.size();
  RawProgramCall trace{3810, 3813, context,
                       std::array{detail::raw_native_field(size),
                                  detail::raw_string_field(revision_),
                                  detail::raw_native_field(input)},
                       sizeof(bool)};
  if (selection_request)
    *selection_request = trace.reference();
  return trace.run([&] {
    auto original = trace.data(48 + text.size());
    trace.bytes(3811, text, original);
    original.reset();
    auto trigger = trace.data(48 + 129);
    auto decision = trace.control(50);
    auto observed = [&](std::optional<WorkflowSelection> result) {
      const bool selected = result.has_value();
      if (result)
        trace.bytes(3814, result->trigger, trigger);
      trace.finish(RawOperationBranch::returned, nullptr,
                   std::array{detail::raw_native_field(selected)});
      return result;
    };
    auto chosen = [&](std::uint8_t route, std::size_t index) {
      const bool present = true;
      trace.event(3812,
                  std::array{detail::raw_native_field(route),
                             detail::raw_native_field(present),
                             detail::raw_native_field(index)},
                  decision);
    };
    auto not_selected = [&] {
      const std::uint8_t route = 0;
      const bool present = false;
      trace.event(3812,
                  std::array{detail::raw_native_field(route),
                             detail::raw_native_field(present)},
                  decision);
      return observed({});
    };
    const auto end = text.find_first_of(" \t\r\n");
    const auto first = text.substr(0, end);
    if (const auto it = aliases_.find(first); it != aliases_.end()) {
      chosen(1, it->second);
      return observed(WorkflowSelection{
          definitions_.array()[it->second],
          end == std::string_view::npos ? "" : std::string{text.substr(end)},
          std::string{first}});
    }
    if (text.starts_with("/"))
      return not_selected();
    std::optional<WorkflowSelection> selected;
    for (std::size_t b = 0; b < text.size();) {
      if (!word(static_cast<unsigned char>(text[b]))) {
        ++b;
        continue;
      }
      auto e = b + 1;
      while (e < text.size() && word(static_cast<unsigned char>(text[e])))
        ++e;
      if (const auto it = powerwords_.find(text.substr(b, e - b));
          it != powerwords_.end()) {
        const auto &d = definitions_.array()[it->second.definition];
        if (selected && str(selected->definition, "name") != str(d, "name"))
          throw Error{ErrorCode::conflict};
        if (!selected) {
          chosen(2, it->second.definition);
          selected = WorkflowSelection{d, std::string{text}, it->first};
          trace.bytes(3814, selected->trigger, trigger);
          trigger.reset();
        }
      }
      b = e;
    }
    return selected ? observed(std::move(selected)) : not_selected();
  });
}
ChatRow WorkflowRegistry::color(std::string_view text, Ink base) const {
  ChatRow out;
  std::size_t last = 0;
  for (std::size_t b = 0; b < text.size();) {
    if (!word(static_cast<unsigned char>(text[b]))) {
      ++b;
      continue;
    }
    auto e = b + 1;
    while (e < text.size() && word(static_cast<unsigned char>(text[e])))
      ++e;
    if (const auto it = powerwords_.find(text.substr(b, e - b));
        it != powerwords_.end()) {
      if (b > last)
        out.push_back({std::string{text.substr(last, b - last)}, base});
      out.push_back({std::string{text.substr(b, e - b)}, it->second.ink});
      last = e;
    }
    b = e;
  }
  if (last < text.size())
    out.push_back({std::string{text.substr(last)}, base});
  return out;
}
void publish_workflows(std::shared_ptr<const WorkflowRegistry> r) {
  std::lock_guard lock(mutex);
  displayed = std::move(r);
}
std::shared_ptr<const WorkflowRegistry> displayed_workflows() {
  std::lock_guard lock(mutex);
  return displayed;
}
Value default_workflows() {
  return Value{Value::Array{Value::object(
      {{"name", Value{"ultracode"}},
       {"description", Value{"Bounded source-grounded coding with direct checks"}},
       {"aliases", Value{Value::Array{Value{"ultracode"}}}},
       {"bare", Value{false}},
       {"powerwords",
        Value{Value::Array{Value::object(
            {{"token", Value{"ultracode"}}, {"color", Value{"keyword"}}})}}},
       {"source",
        Value{
            R"lua(local brief={role='developer',content='Ultracode: implement useful working code. Read targeted sources, state concrete tasks and a fixed allowance before hardening. Preserve the operator prompt. Use direct specification oracles and fake effects where possible. One independent review and one findings recheck within the allowance; never reset it. Report actual outcomes and gaps; no invented scheduler or performance claims.'}
if not blackbird.live_instruction(brief) then blackbird.append({brief}, {class='instruction'}) end
for step=1,64 do
 blackbird.emit(3880,step)
 local r=blackbird.request(); local calls=0
 for _,item in ipairs(r.output) do
  if item.type=='function_call' then
   calls=calls+1; local result=blackbird.call(item.name,item.arguments,{call_id=item.call_id})
   blackbird.append({{type='function_call_output',call_id=item.call_id,output=result}})
  elseif item.type=='message' then blackbird.present(item) end
 end
 blackbird.emit(3881,calls)
 local restarting=blackbird.restarting(); blackbird.emit(3882,restarting)
 if restarting or calls==0 then return end
end
error('ultracode reached its 64-step budget')
)lua"}}})}};
}
} // namespace blackbird
