#include "blackbird/session.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/raw_file.hpp"
#include "blackbird/raw_restart.hpp"
#include "blackbird/raw_session.hpp"
#include "blackbird/tools.hpp"
#include <algorithm>
#include <sys/stat.h>
namespace blackbird {
std::filesystem::path default_session_directory(const std::filesystem::path &home) {
  const auto current = home / ".local/state/blackbird/default";
  const auto legacy = home / ".local/state/arconaut/default";
  return !std::filesystem::exists(current) && std::filesystem::is_directory(legacy)
             ? legacy
             : current;
}
namespace {
bool labelled(const Value &value, std::string_view label) {
  const auto *name = value.find("label");
  return name && std::holds_alternative<std::string>(name->value()) &&
         name->string() == label;
}
template <class T> T identity(std::string_view text) {
  if (text.size() != 32)
    throw Error{ErrorCode::corrupt};
  IdentityBytes bytes{};
  auto digit = [](char c) -> unsigned {
    if (c >= '0' && c <= '9')
      return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f')
      return static_cast<unsigned>(c - 'a' + 10);
    throw Error{ErrorCode::corrupt};
  };
  for (std::size_t i = 0; i < bytes.size(); ++i)
    bytes[i] = static_cast<std::byte>(16 * digit(text[2 * i]) + digit(text[2 * i + 1]));
  return unwrap(T::from_bytes(bytes));
}
void validate(const SessionSettings &value) {
  if (value.name.size() > 128 ||
      value.name.find_first_of("\r\n") != std::string::npos ||
      value.name.find('\0') != std::string::npos || value.model.size() > 1024 ||
      value.workflow.size() > 65536 || value.model.empty() ||
      value.model.find('\0') != std::string::npos || value.workflow.empty() ||
      value.workflow.find('\0') != std::string::npos ||
      (value.effort != "low" && value.effort != "medium" && value.effort != "high" &&
       value.effort != "xhigh"))
    throw Error{ErrorCode::invalid_range};
}
RawSessionCall session_call(AuditLog &log, std::uint8_t action,
                            const RawAdapterContext *context) {
  const auto cursor = log.root().cursor();
  return RawSessionCall{4210, 4211, context,
                        std::array{detail::raw_native_field(action),
                                   ByteView{cursor.journal.bytes()},
                                   detail::raw_native_field(cursor.sequence),
                                   detail::raw_native_field(cursor.end_offset)}};
}
void settings_source(RawSessionCall &call, const SessionSettings &settings,
                     std::uint32_t site = 4212) {
  (void)call.source(1, settings.name, site);
  (void)call.source(2, settings.model, site);
  (void)call.source(3, settings.effort, site);
  (void)call.source(4, settings.workflow, site);
}
template <class F>
bool filesystem_predicate(RawSessionCall &owner, std::uint8_t method,
                          std::error_code &error, F &&body) {
  const auto context = owner.context();
  RawSessionCall call{4242, 4243, &context,
                      std::array{detail::raw_native_field(method)},
                      sizeof(int) + sizeof(bool)};
  return call.run([&] {
    const bool returned = std::forward<F>(body)();
    const int native_error = error.value();
    call.finish(RawOperationBranch::returned, nullptr,
                std::array{detail::raw_native_field(native_error),
                           detail::raw_native_field(returned)});
    return returned;
  });
}
std::filesystem::path weak_path(RawSessionCall &owner,
                                const std::filesystem::path &path,
                                std::error_code &error) {
  const auto context = owner.context();
  const std::uint8_t method = 8;
  RawSessionCall call{4242, 4243, &context,
                      std::array{detail::raw_native_field(method)}, sizeof(int) + 24};
  return call.run([&] {
    const auto selected = call.context();
    RawEventRef acquired{};
    auto result = raw_file_weak_canonical(path, error, &selected, &acquired);
    const int native_error = error.value();
    call.finish(RawOperationBranch::returned, nullptr,
                std::array{detail::raw_native_field(native_error),
                           ByteView{acquired.lane},
                           detail::raw_native_field(acquired.ordinal)});
    return result;
  });
}
std::filesystem::path child_path(RawSessionCall &owner,
                                 const std::filesystem::path &path,
                                 std::string_view suffix, std::uint32_t site = 4222) {
  auto source =
      owner.source_credit(raw_session_bound(path.native().size(), 1 + suffix.size()));
  auto result = path / suffix;
  (void)owner.source(5, result.native(), source, site);
  return result;
}

} // namespace

void save_session_info(const std::filesystem::path &directory,
                       const SessionSettings &settings, const SessionIdentity &id,
                       const RawAdapterContext *context) {
  const std::uint8_t action = 4;
  RawSessionCall call{
      4217, 4211, context,
      std::array{detail::raw_native_field(action), ByteView{id.actor.bytes()},
                 ByteView{id.conversation.bytes()}, ByteView{id.workflow.bytes()}}};
  call.run([&] {
    settings_source(call, settings);
    (void)call.source(5, directory.native());
    call.stage(1);
    validate(settings);
    call.stage(2);
    auto body = call.source_credit(512 * 1024);
    const auto serialized = unwrap(encode_packet_string(
        Value::object({{"version", Value{Number{"1"}}},
                       {"name", Value{settings.name}},
                       {"model", Value{settings.model}},
                       {"effort", Value{settings.effort}},
                       {"workflow", Value{settings.workflow}},
                       {"actor", Value{hex_identity(id.actor.bytes())}},
                       {"conversation", Value{hex_identity(id.conversation.bytes())}},
                       {"workflow_id", Value{hex_identity(id.workflow.bytes())}}})));
    const auto original = call.source(6, serialized, body);
    const auto path = child_path(call, directory, "session-info.bbm", 4212);
    const auto selected = call.context();
    call.stage(3);
    try {
      if (read_file(path, 512 * 1024, &selected) == serialized)
        return;
    } catch (const Error &) {
      call.require_capture();
      // Ordinary absent/unreadable derived data is replaced from audit settings.
    }
    call.stage(4);
    const RawFileInput input{original, serialized.size()};
    write_file(path, serialized, &selected, call.enabled() ? &input : nullptr);
  });
}

Value list_sessions(const std::filesystem::path &requested,
                    const RawAdapterContext *context, RawEventRef *completion) {
  if (completion)
    *completion = {};
  RawSessionCall call{4220, 4221, context};
  RawEventRef completed_source{};
  auto result = call.run([&] {
    call.stage(1);
    (void)call.source(5, requested.native(), 4222);
    const auto selected = call.context();
    std::error_code absolute_error;
    RawEventRef absolute_body{};
    const auto acquired_absolute =
        raw_file_absolute(requested, absolute_error, &selected, &absolute_body);
    if (absolute_error)
      throw std::filesystem::filesystem_error{"absolute", requested, absolute_error};
    auto selected_path = call.control(64);
    (void)call.event(4226,
                     std::array{ByteView{absolute_body.lane},
                                detail::raw_native_field(absolute_body.ordinal)},
                     selected_path);
    auto normalized_source = call.source_credit(acquired_absolute.native().size());
    const auto absolute = acquired_absolute.lexically_normal();
    (void)call.source(5, absolute.native(), normalized_source, 4222);
    std::error_code root_error;
    const auto canonical_root = weak_path(call, absolute, root_error);
    const auto &root = root_error ? absolute : canonical_root;
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    auto candidate = [&](const std::filesystem::path &path) {
      std::error_code e;
      if (filesystem_predicate(call, 1, e,
                               [&] { return std::filesystem::is_symlink(path, e); }) ||
          e)
        return;
      const auto audit_path = child_path(call, path, "audit");
      if (raw_file_exists(audit_path, e, &selected))
        paths.push_back(path);
      else {
        const auto info_path = child_path(call, path, "session-info.bbm");
        if (raw_file_exists(info_path, e, &selected))
          paths.push_back(path);
      }
    };
    call.stage(2);
    if (filesystem_predicate(call, 2, error, [&] {
          return std::filesystem::is_directory(root, error);
        })) {
      candidate(root);
      const std::filesystem::directory_iterator end;
      const std::uint8_t opening = 3;
      RawSessionCall iterator_open{4242, 4243, &selected,
                                   std::array{detail::raw_native_field(opening)},
                                   sizeof(int) + sizeof(bool)};
      auto entry_source =
          call.source_credit(raw_session_bound(root.native().size(), 1 + NAME_MAX));
      auto iterator = iterator_open.run([&] {
        std::filesystem::directory_iterator result{root, error};
        const int native_error = error.value();
        if (!error) {
          const bool empty = result == end;
          if (!empty)
            (void)call.source(5, result->path().native(), entry_source, 4222);
          iterator_open.finish(RawOperationBranch::returned, nullptr,
                               std::array{detail::raw_native_field(native_error),
                                          detail::raw_native_field(empty)});
        } else
          iterator_open.finish(RawOperationBranch::returned, nullptr,
                               std::array{detail::raw_native_field(native_error)});
        return result;
      });
      while (!error && iterator != end) {
        std::error_code entry_error;
        if (filesystem_predicate(call, 5, entry_error,
                                 [&] { return iterator->is_directory(entry_error); }))
          candidate(iterator->path());
        const std::uint8_t increment = 4;
        RawSessionCall next{4242, 4243, &selected,
                            std::array{detail::raw_native_field(increment)},
                            sizeof(int) + sizeof(bool)};
        entry_source =
            call.source_credit(raw_session_bound(root.native().size(), 1 + NAME_MAX));
        next.run([&] {
          iterator.increment(error);
          const int native_error = error.value();
          if (!error) {
            const bool empty = iterator == end;
            if (!empty)
              (void)call.source(5, iterator->path().native(), entry_source, 4222);
            next.finish(RawOperationBranch::returned, nullptr,
                        std::array{detail::raw_native_field(native_error),
                                   detail::raw_native_field(empty)});
          } else
            next.finish(RawOperationBranch::returned, nullptr,
                        std::array{detail::raw_native_field(native_error)});
        });
      }
    }
    std::sort(paths.begin(), paths.end());
    Value::Array entries;
    call.stage(3);
    for (const auto &path : paths) {
      Value config;
      std::string status = "missing";
      const auto info_path = child_path(call, path, "session-info.bbm");
      RawSessionCall metadata{4218, 4219, &selected};
      try {
        metadata.run([&] {
          const auto input = metadata.context();
          std::error_code exists_error;
          const bool present = raw_file_exists(info_path, exists_error, &input);
          if (exists_error)
            throw std::filesystem::filesystem_error{"exists", info_path, exists_error};
          if (present) {
            metadata.stage(1);
            struct stat info{};
            if (raw_session_stat(&input, RawSessionStat::lstat, info_path, info) != 0 ||
                !S_ISREG(info.st_mode))
              throw Error{ErrorCode::corrupt};
            metadata.stage(2);
            auto packet =
                unwrap(decode_packet_string(read_file(info_path, 1024 * 1024, &input)));
            if (field(packet, "version").number().text() != "1")
              throw Error{ErrorCode::corrupt};
            const auto &model = string_field(packet, "model"),
                       &effort = string_field(packet, "effort"),
                       &workflow = string_field(packet, "workflow");
            (void)metadata.source(2, model, 4222);
            (void)metadata.source(3, effort, 4222);
            (void)metadata.source(4, workflow, 4222);
            SessionSettings settings{model, effort, workflow};
            if (const auto *name = packet.find("name")) {
              const auto &text = name->string();
              (void)metadata.source(1, text, 4222);
              settings.name = text;
            }
            metadata.stage(3);
            validate(settings);
            (void)identity<ParticipantId>(string_field(packet, "actor"));
            (void)identity<ConversationId>(string_field(packet, "conversation"));
            (void)identity<WorkflowId>(string_field(packet, "workflow_id"));
            config = Value::object({{"name", Value{settings.name}},
                                    {"model", Value{settings.model}},
                                    {"effort", Value{settings.effort}},
                                    {"workflow", Value{settings.workflow}}});
            status = "snapshot";
          }
        });
      } catch (...) {
        metadata.require_capture();
        call.require_capture();
        status = "damaged";
      }
      (void)call.source(8, status, 4222);
      struct stat native_metadata{};
      Value activity;
      const auto audit_path = child_path(call, path, "audit");
      const bool activity_available =
          raw_session_stat(&selected, RawSessionStat::stat, audit_path,
                           native_metadata) == 0;
      if (activity_available)
        activity = Value{Number{native_metadata.st_mtime}};
      std::error_code canonical_error;
      const auto canonical = weak_path(call, path, canonical_error);
      auto stable_source = call.source_credit(
          canonical_error ? path.native().size() : canonical.native().size());
      const auto stable = canonical_error ? path.string() : canonical.string();
      (void)call.source(5, stable, stable_source, 4222);
      auto quoted_source = call.source_credit(
          raw_session_bound(raw_session_product(stable.size(), 5), 2));
      std::string quoted = "'";
      for (const char c : stable)
        quoted += c == '\'' ? "'\"'\"'" : std::string(1, c);
      quoted += '\'';
      (void)call.source(9, quoted, quoted_source, 4222);
      constexpr std::string_view prefix = "./scripts/blackbird --session ";
      auto resume_source =
          call.source_credit(raw_session_bound(prefix.size(), quoted.size()));
      const auto resume_command = std::string{prefix} + quoted;
      (void)call.source(10, resume_command, resume_source, 4222);
      auto row_credit = call.control(72);
      const auto row = entries.size();
      const auto source = metadata.reference();
      entries.push_back(
          Value::object({{"path", Value{stable}},
                         {"configuration", std::move(config)},
                         {"metadata_status", Value{status}},
                         {"last_activity_unix_seconds", std::move(activity)},
                         {"last_activity_status",
                          Value{activity_available ? "available" : "unavailable"}},
                         {"resume_command", Value{resume_command}}}));
      (void)call.event(4224,
                       std::array{detail::raw_native_field(row), ByteView{source.lane},
                                  detail::raw_native_field(source.ordinal)},
                       row_credit);
    }
    call.stage(4);
    auto root_source = call.source_credit(root.native().size());
    const auto root_text = root.string();
    (void)call.source(5, root_text, root_source, 4222);
    auto completed = call.control(48);
    const auto count = entries.size();
    auto result = Value::object(
        {{"root", Value{root_text}},
         {"sessions", Value{std::move(entries)}},
         {"configuration_source",
          Value{"derived snapshot; audit authoritative; missing legacy snapshots "
                "require ordinary reopen"}},
         {"scan_status", Value{error ? "incomplete/unreadable root" : "complete"}}});
    const auto record =
        call.event(4225, std::array{detail::raw_native_field(count)}, completed);
    completed_source = record;
    return result;
  });
  if (completion)
    *completion = completed_source;
  return result;
}

SessionIdentity session_identity(AuditLog &log, const RawAdapterContext *context) {
  auto call = session_call(log, 3, context);
  return call.run([&] {
    auto returned = call.control(88);
    std::optional<SessionIdentity> result;
    auto acquired = [&](const SessionIdentity &value) {
      (void)call.event(4214,
                       std::array{ByteView{value.actor.bytes()},
                                  ByteView{value.conversation.bytes()},
                                  ByteView{value.workflow.bytes()}},
                       returned);
    };
    call.stage(1);
    for (const auto &value : log.root().current_programs())
      if (labelled(value, "session.identity")) {
        const SessionIdentity known{
            identity<ParticipantId>(string_field(value, "actor")),
            identity<ConversationId>(string_field(value, "conversation")),
            identity<WorkflowId>(string_field(value, "workflow"))};
        acquired(known);
        return known;
      }
    for (std::size_t ordinal = 0; ordinal < log.root().fact_count(); ++ordinal) {
      const auto fact = unwrap(log.root().fact(ordinal));
      if (const auto *decision = std::get_if<DecisionEvent>(&fact.event.body))
        result = SessionIdentity{decision->actor, decision->conversation,
                                 decision->workflow};
    }
    call.stage(2);
    if (!result)
      result = SessionIdentity{unwrap(log.root().issue<ParticipantId>()),
                               unwrap(log.root().issue<ConversationId>()),
                               unwrap(log.root().issue<WorkflowId>())};
    acquired(*result);
    auto locator = call.bytes_credit(32);
    call.stage(3);
    const auto recorded = log.record(
        ApplicationChannel::program,
        Value::object(
            {{"label", Value{"session.identity"}},
             {"actor", Value{hex_identity(result->actor.bytes())}},
             {"conversation", Value{hex_identity(result->conversation.bytes())}},
             {"workflow", Value{hex_identity(result->workflow.bytes())}}}));
    (void)call.bytes(4213, recorded, locator);
    return *result;
  });
}
SessionStore::SessionStore(AuditLog &log, const RawAdapterContext *context)
    : log_(log) {
  auto call = session_call(log, 1, context);
  call.run([&] {
    for (const auto &value : log.root().current_programs()) {
      if (labelled(value, "session.settings")) {
        const auto &model = string_field(value, "model"),
                   &effort = string_field(value, "effort"),
                   &workflow = string_field(value, "workflow");
        (void)call.source(2, model);
        (void)call.source(3, effort);
        (void)call.source(4, workflow);
        auto assigned = call.control(41);
        settings_ = {model, effort, workflow};
        const std::uint8_t restored = 1;
        (void)call.event(4215, std::array{detail::raw_native_field(restored)},
                         assigned);
        if (const auto *name = value.find("name")) {
          const auto &text = name->string();
          (void)call.source(1, text);
          auto name_assigned = call.control(41);
          settings_.name = text;
          const std::uint8_t renamed = 3;
          (void)call.event(4215, std::array{detail::raw_native_field(renamed)},
                           name_assigned);
        }
        call.stage(1);
        validate(settings_);
      } else if (labelled(value, "session.restart")) {
        const auto parent = call.context();
        auto restored = raw_restart_call(log.root(), 3, &parent);
        restored.run([&] {
          if (const auto *note = value.find("note");
              note && std::holds_alternative<std::string>(note->value()))
            (void)raw_restart_source(restored, 1, note->string());
          if (const auto *token = value.find("token");
              token && std::holds_alternative<std::string>(token->value()))
            (void)raw_restart_source(restored, 2, token->string());
          if (const auto *state = value.find("state");
              state && std::holds_alternative<std::string>(state->value()))
            (void)raw_restart_source(restored, 5, state->string());
          raw_restart_mutation(restored, 3, [&] { restart_ = value; });
        });
      }
    }
  });
}
void SessionStore::save(const SessionSettings &value,
                        const RawAdapterContext *context) {
  auto call = session_call(log_, 2, context);
  call.run([&] {
    settings_source(call, value);
    call.stage(1);
    validate(value);
    auto compared = call.control(42);
    const bool same = value == settings_;
    const std::uint8_t predicate = 1;
    (void)call.event(
        4216,
        std::array{detail::raw_native_field(predicate), detail::raw_native_field(same)},
        compared);
    if (same)
      return;
    call.stage(2);
    auto next = value;
    auto assigned = call.control(41);
    auto locator = call.bytes_credit(32);
    call.stage(3);
    const auto recorded =
        log_.record(ApplicationChannel::program,
                    Value::object({{"label", Value{"session.settings"}},
                                   {"name", Value{next.name}},
                                   {"model", Value{next.model}},
                                   {"effort", Value{next.effort}},
                                   {"workflow", Value{next.workflow}}}));
    (void)call.bytes(4213, recorded, locator);
    settings_ = std::move(next);
    const std::uint8_t action = 2;
    (void)call.event(4215, std::array{detail::raw_native_field(action)}, assigned);
  });
}

void SessionStore::restart(std::string_view note, const RawAdapterContext *capture) {
  auto call = raw_restart_call(log_.root(), 1, capture);
  call.run([&] {
    (void)raw_restart_source(call, 1, note);
    call.stage(1);
    if (note.empty() || note.size() > 65536)
      throw Error{ErrorCode::invalid_range};
    if (raw_restart_predicate(call, 1, [&] {
          return !std::holds_alternative<std::nullptr_t>(restart_.value()) &&
                 string_field(restart_, "state") == "pending";
        }))
      throw Error{ErrorCode::busy};
    if (raw_restart_predicate(call, 2, [&] {
          return !unwrap(log_.root().unresolved_attempts()).empty();
        }))
      throw Error{ErrorCode::busy};
    call.stage(2);
    auto native_id = call.control(56);
    auto token_credit = call.source_credit(32);
    const auto identity = log_.issue();
    (void)call.event(4404, std::array{ByteView{identity.bytes()}}, native_id);
    const auto token = hex_identity(identity.bytes());
    (void)raw_restart_source(call, 2, token, token_credit);
    (void)raw_restart_source(call, 5, "pending");
    call.stage(3);
    auto value = Value::object({{"label", Value{"session.restart"}},
                                {"token", Value{token}},
                                {"state", Value{"pending"}},
                                {"note", Value{std::string{note}}}});
    auto locator_credit = call.bytes_credit(32);
    const auto locator = log_.record(ApplicationChannel::program, value);
    (void)call.bytes(4405, locator, locator_credit);
    call.stage(4);
    raw_restart_mutation(call, 1, [&] { restart_ = std::move(value); });
  });
}
bool SessionStore::resume(ContextStore &context, const RawAdapterContext *capture) {
  auto call = raw_restart_call(log_.root(), 2, capture);
  return call.run([&] {
    auto returned = call.control(41);
    const bool result = [&] {
      call.stage(1);
      if (!raw_restart_predicate(call, 3, [&] {
            return !std::holds_alternative<std::nullptr_t>(restart_.value()) &&
                   string_field(restart_, "state") == "pending";
          }))
        return false;
      const auto &token = string_field(restart_, "token");
      (void)raw_restart_source(call, 2, token);
      auto origin_credit = call.source_credit(raw_session_bound(4, token.size()));
      const auto origin = "rrc:" + token;
      (void)raw_restart_source(call, 3, origin, origin_credit);
      bool injected = false;
      call.stage(2);
      for (const auto &fact : log_.root().tail_facts()) {
        const auto *record = std::get_if<ApplicationRecordEvent>(&fact.event.body);
        if (!record || record->channel != ApplicationChannel::context)
          continue;
        const auto value = unwrap(raw_restart_packet(call, 9, record->payload));
        const auto *previous = value.find("origin");
        if (raw_restart_predicate(call, 4, [&] {
              return previous &&
                     std::holds_alternative<std::string>(previous->value()) &&
                     previous->string() == origin;
            }))
          injected = true;
      }
      if (!raw_restart_predicate(call, 5, [&] { return injected; })) {
        constexpr std::string_view prefix = "continue\n\nRestart/resume note:\n";
        const auto &note = string_field(restart_, "note");
        (void)raw_restart_source(call, 1, note);
        auto message_credit =
            call.source_credit(raw_session_bound(prefix.size(), note.size()));
        auto message = std::string{prefix} + note;
        (void)raw_restart_source(call, 4, message, message_credit);
        context.append({Value::object({{"role", Value{"user"}},
                                       {"content", Value{std::move(message)}}})},
                       origin, RawContextOrigin{call.reference()});
      }
      call.stage(3);
      (void)raw_restart_source(call, 5, "consumed");
      auto consumed = Value::object({{"label", Value{"session.restart"}},
                                     {"state", Value{"consumed"}},
                                     {"token", field(restart_, "token")},
                                     {"note", field(restart_, "note")}});
      auto locator_credit = call.bytes_credit(32);
      const auto locator = log_.record(ApplicationChannel::program, consumed);
      (void)call.bytes(4405, locator, locator_credit);
      call.stage(4);
      raw_restart_mutation(call, 2, [&] { restart_ = std::move(consumed); });
      return true;
    }();
    (void)call.event(4407, std::array{detail::raw_native_field(result)}, returned);
    return result;
  });
}
} // namespace blackbird
