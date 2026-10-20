#include "blackbird/backstop.hpp"
#include "blackbird/packet.hpp"
#include "blackbird/process_lifetime.hpp"
#include "blackbird/raw_file.hpp"
#include "blackbird/raw_session.hpp"
#include "blackbird/raw_storage.hpp"
#include <limits.h>

#include <set>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
namespace blackbird {
namespace {
Value number(std::uint64_t n) { return Value{Number{n}}; }
using detail::raw_native_field;
RawEventRef source(RawSessionCall &owner, std::uint8_t kind, std::string_view bytes) {
  return owner.source(kind, bytes, 4702);
}
template <class F> bool predicate(RawSessionCall &owner, std::uint8_t kind, F &&body) {
  auto credit = owner.control(42);
  const bool value = std::forward<F>(body)();
  (void)owner.event(4703, std::array{raw_native_field(kind), raw_native_field(value)},
                    credit);
  return value;
}
void mutation(RawSessionCall &owner, std::uint8_t kind,
              std::optional<RawControlToken> &credit,
              const std::size_t *count = nullptr) {
  if (count)
    (void)owner.event(
        4706, std::array{raw_native_field(kind), raw_native_field(*count)}, credit);
  else
    (void)owner.event(4706, std::array{raw_native_field(kind)}, credit);
}
std::string encoded(RawSessionCall &owner, std::uint8_t role, const Value &value,
                    std::uint32_t site = 4702) {
  auto credit = owner.source_credit(ValueLimits{}.bytes);
  auto bytes = unwrap(encode_packet_string(value));
  (void)owner.source(role, bytes, credit, site);
  return bytes;
}
std::string hex(RawSessionCall &owner, const IdentityBytes &value) {
  auto credit = owner.source_credit(32);
  auto bytes = hex_identity(value);
  (void)owner.source(21, bytes, credit, 4702);
  return bytes;
}
int entropy(RawSessionCall &owner, void *output, std::size_t count) {
  auto control = owner.control(96);
  auto data = owner.bytes_credit(count);
  const auto request = owner.event(4714, std::array{raw_native_field(count)}, control);
  const int rc = ::getentropy(output, count);
  const int error = errno;
  if (rc < 0) {
    (void)raw_session_record(4715, request,
                             std::array{raw_native_field(rc), raw_native_field(error)},
                             RawPartition::control, control ? &*control : nullptr);
  } else {
    (void)raw_session_record(
        4715, request,
        std::array{raw_native_field(rc),
                   ByteView{static_cast<const std::byte *>(output), count}},
        RawPartition::data, data ? &*data : nullptr);
  }
  errno = error;
  return rc;
}
template <class T> T random_identity(RawSessionCall &owner) {
  IdentityBytes bytes{};
  if (entropy(owner, bytes.data(), bytes.size()) != 0)
    throw Error{ErrorCode::io, errno};
  return unwrap(T::from_bytes(bytes));
}
std::filesystem::path resolved_path(RawSessionCall &owner,
                                    const std::filesystem::path &path, bool weak) {
  const auto operand = source(owner, 24, path.native());
  const auto context = owner.context();
  const std::uint8_t method = weak ? 2 : 1;
  RawSessionCall call{4740, 4741, &context,
                      std::array{raw_native_field(method), ByteView{operand.lane},
                                 raw_native_field(operand.ordinal)},
                      13};
  return call.run([&] {
    const auto selected = call.context();
    std::error_code error;
    auto result = weak ? raw_file_weak_canonical(path, error, &selected)
                       : raw_file_canonical(path, error, &selected);
    const int value = error.value();
    const auto *category = &error.category();
    call.require_capture();
    call.finish(error ? RawOperationBranch::standard : RawOperationBranch::returned,
                nullptr,
                std::array{raw_native_field(value), raw_native_field(category)});
    call.require_capture();
    if (error)
      throw std::filesystem::filesystem_error(weak ? "weakly_canonical" : "canonical",
                                              path, error);
    return result;
  });
}
template <class F> decltype(auto) filesystem_run(RawSessionCall &call, F &&body) {
  return call.run([&]() -> std::invoke_result_t<F> {
    try {
      return std::forward<F>(body)();
    } catch (const std::filesystem::filesystem_error &error) {
      const int value = error.code().value();
      const auto *category = &error.code().category();
      call.finish(RawOperationBranch::standard, nullptr,
                  std::array{raw_native_field(value), raw_native_field(category)});
      throw;
    }
  });
}
template <class F>
bool filesystem_predicate(RawSessionCall &owner, std::uint8_t method,
                          const std::filesystem::path &path, F &&body,
                          const std::filesystem::path *second = nullptr) {
  const auto operand = source(owner, 24, path.native());
  const auto context = owner.context();
  const auto other = second ? source(owner, 24, second->native()) : RawEventRef{};
  std::array<ByteView, 5> fields{raw_native_field(method), operand.lane,
                                 raw_native_field(operand.ordinal), other.lane,
                                 raw_native_field(other.ordinal)};
  RawSessionCall call{4740, 4741, &context, std::span{fields}.first(second ? 5 : 3),
                      13};
  return call.run([&] {
    try {
      const bool returned = std::forward<F>(body)();
      call.finish(RawOperationBranch::returned, nullptr,
                  std::array{raw_native_field(returned)});
      return returned;
    } catch (const std::filesystem::filesystem_error &error) {
      const int value = error.code().value();
      const auto *category = &error.code().category();
      call.finish(RawOperationBranch::standard, nullptr,
                  std::array{raw_native_field(value), raw_native_field(category)});
      throw;
    }
  });
}
constexpr std::string_view assessment = R"lua(
local response = blackbird.request({tools=blackbird.array({}), state_context='omit', retry_policy={max_attempts=1}})
local text = ''
for _, item in ipairs(response.output) do
  if item.type == 'function_call' then error('Assessment must not dispatch tools') end
  if item.type == 'message' then
    for _, part in ipairs(item.content) do
      if part.type == 'output_text' then text = text .. part.text end
    end
  end
end
return blackbird.json.decode(text)
)lua";
constexpr std::string_view instruction =
    "Independent backstop assessment. Source workflow failed; its audit is preserved "
    "and locally quiescent under cooperative native custody. Local quiescence says "
    "nothing about remote success. Imported attempts have NO replay authority. "
    "Mission packet is author-declared, not verified inherited context. Preserve "
    "dirty work and unknowns. Choose a changed approach or independent useful work "
    "where a resource is blocked. Return only JSON, no markdown, with string fields "
    "retained, uncertain, change, next, action (an ordinary Blackbird Lua turn "
    "program), "
    "and oracle:{path:absolute file path,content:exact expected new bytes}. Action "
    "must do useful work for the mission and produce the declared artifact, not a "
    "recovery-status facade. Use blackbird.call/read_file to reconcile uncertain file "
    "effects rather than replay them. Arbitrary exec containment is unavailable: "
    "exec/restart and provider requests during action are refused in recovery; use "
    "native file/context APIs. Two-minute total deadline. File mutations "
    "and oracles inside the predecessor session tree (including aliases) are refused. "
    "Calls return native error values; check "
    "them. You have at most two assessment/pivot attempts, one request per assessment. "
    "Keep action and expected artifact small. The oracle must be absent or different "
    "before action; stale output does not establish progress. Explicit pause stays "
    "paused.";
void check_pause(RawSessionCall &owner, const std::function<bool()> &paused) {
  if (predicate(owner, 1, [&] { return paused && paused(); }))
    throw Error{ErrorCode::interrupted};
}
} // namespace
Value run_backstop(CodingEngine &source_engine, AuditLog &source_log,
                   const std::filesystem::path &source_session, const Value &mission,
                   CodingProvider &assessor, const SessionSettings &settings,
                   const std::function<bool()> &paused,
                   const std::function<void(std::string_view)> &display,
                   const RawAdapterContext *raw_context) {
  const auto source_cursor = source_log.root().cursor();
  RawSessionCall whole{4700, 4701, raw_context,
                       std::array{ByteView{source_cursor.journal.bytes()},
                                  raw_native_field(source_cursor.sequence),
                                  raw_native_field(source_cursor.end_offset)}};
  return whole.run([&]() -> Value {
    const auto selected_owner = whole.context();
    check_pause(whole, paused);
    if (!predicate(
            whole, 2,
            [&] { return std::holds_alternative<Value::Object>(mission.value()); }) ||
        predicate(whole, 3,
                  [&] { return encoded(whole, 3, mission).size() > 32768; }) ||
        !predicate(whole, 4, [&] {
          const auto &text = string_field(mission, "mission");
          (void)source(whole, 23, text);
          return !text.empty();
        }))
      throw Error{ErrorCode::invalid_range};
    const auto cause = source_engine.claim_backstop(&selected_owner);
    auto cause_credit = whole.control(49);
    (void)whole.event(
        4705, std::array{raw_native_field(cause.code), raw_native_field(cause.detail)},
        cause_credit);
    (void)source(whole, 18, settings.model);
    (void)source(whole, 19, settings.effort);
    (void)source(whole, 1, source_session.native());
    std::error_code absolute_error;
    const auto absolute_path =
        raw_file_absolute(source_session, absolute_error, &selected_owner);
    whole.require_capture();
    if (absolute_error)
      throw std::filesystem::filesystem_error("absolute", source_session,
                                              absolute_error);
    auto normalized_credit =
        whole.source_credit(raw_session_bound(absolute_path.native().size(), 1));
    const auto source_path = absolute_path.lexically_normal();
    (void)whole.source(1, source_path.native(), normalized_credit, 4702);
    auto destination_credit =
        whole.source_credit(raw_session_bound(source_path.native().size(), 9));
    const auto destination = source_path / "backstop";
    const auto destination_ref =
        whole.source(2, destination.native(), destination_credit, 4702);
    // Atomic intent creation while still holding native predecessor custody. Never
    // delete/replace this intent, even after crash; reopen needs explicit inspection.
    auto mkdir_credit = whole.control(112 + sizeof(mode_t));
    const mode_t mode = 0700;
    const auto mkdir_request =
        whole.event(4712,
                    std::array{raw_native_field(mode), ByteView{destination_ref.lane},
                               raw_native_field(destination_ref.ordinal)},
                    mkdir_credit);
    const int mkdir_result = ::mkdir(destination.c_str(), mode);
    const int mkdir_error = errno;
    if (mkdir_result < 0)
      (void)raw_session_record(
          4713, mkdir_request,
          std::array{raw_native_field(mkdir_result), raw_native_field(mkdir_error)},
          RawPartition::control, mkdir_credit ? &*mkdir_credit : nullptr);
    else
      (void)raw_session_record(
          4713, mkdir_request, std::array{raw_native_field(mkdir_result)},
          RawPartition::control, mkdir_credit ? &*mkdir_credit : nullptr);
    errno = mkdir_error;
    if (mkdir_result != 0)
      throw Error{mkdir_error == EEXIST ? ErrorCode::conflict : ErrorCode::io,
                  mkdir_error};
    const auto cursor = source_log.root().cursor();
    std::string header_bytes;
    try {
      header_bytes =
          read_file_prefix(source_path / "audit", journal_header_size, &selected_owner);
    } catch (const Error &) {
      whole.require_capture();
      throw Error{ErrorCode::incomplete};
    }
    const auto source_header = unwrap(decode_journal_header(
        std::as_bytes(std::span{header_bytes.data(), header_bytes.size()})));
    Value lineage = Value::object(
        {{"session", Value{source_path.string()}},
         {"environment", Value{hex(whole, source_header.environment.bytes())}},
         {"journal", Value{hex(whole, cursor.journal.bytes())}},
         {"prefix_sequence", number(cursor.sequence)},
         {"prefix_end", number(cursor.end_offset)},
         {"context_revision", Value{[&] {
            const auto &head = source_engine.context().head();
            (void)source(whole, 17, head);
            return head;
          }()}},
         {"status", Value{"unsettled"}},
         {"reason", Value{"Cooperative backstop: remote effects remain unknown; no "
                          "inherited admissions"}}});
    Value::Array selected;
    const auto view = source_engine.context().view(RawContextOrigin{whole.reference()});
    // Preserve all governing messages and the latest operator prompt, not the huge
    // predecessor working presentation. Refuse oversized authority, never truncate.
    const auto &entries = field(view, "entries").array();
    const Value *last_user = nullptr;
    for (const auto &entry : entries) {
      if (const auto *entry_id = entry.find("id"))
        (void)source(whole, 20, entry_id->string());
      const auto &item = field(entry, "item");
      const auto *role = item.find("role");
      if (!role)
        continue;
      (void)source(whole, 15, role->string());
      if (role->string() == "system" || role->string() == "developer")
        selected.push_back(entry);
      if (role->string() == "user")
        last_user = &entry;
    }
    if (predicate(whole, 44, [&] { return last_user != nullptr; }))
      selected.push_back(*last_user);
    if (predicate(whole, 5, [&] { return selected.empty(); }) ||
        predicate(whole, 6,
                  [&] { return encoded(whole, 4, Value{selected}).size() > 65536; }))
      throw Error{ErrorCode::capacity};
    Value::Array attempts;
    std::size_t total = 0;
    for (std::size_t ordinal = 0; ordinal < source_log.root().fact_count(); ++ordinal) {
      const auto fact = unwrap(source_log.root().fact(ordinal));
      const auto *admission = std::get_if<AttemptAdmissionEvent>(&fact.event.body);
      if (!admission)
        continue;
      auto selection_credit = whole.control(73);
      const std::uint8_t selection_kind = 1;
      const auto input_extent = admission->input.size();
      (void)whole.event(4704,
                        std::array{ByteView{admission->attempt.bytes()},
                                   raw_native_field(selection_kind),
                                   raw_native_field(ordinal),
                                   raw_native_field(input_extent)},
                        selection_credit);
      (void)source(
          whole, 16,
          std::string_view{reinterpret_cast<const char *>(admission->input.data()),
                           input_extent});
      ++total;
      if (attempts.size() == 32)
        attempts.erase(attempts.begin());
      const auto state = unwrap(source_log.root().attempt(admission->attempt));
      const bool unknown = predicate(whole, 8, [&] {
        return !state.observation ||
               state.observation->disposition == AttemptDisposition::unknown;
      });
      attempts.push_back(Value::object(
          {{"attempt", Value{hex(whole, admission->attempt.bytes())}},
           {"input",
            Value{std::string{reinterpret_cast<const char *>(admission->input.data()),
                              std::min<std::size_t>(admission->input.size(), 1024)}}},
           {"input_truncated",
            Value{predicate(whole, 7, [&] { return admission->input.size() > 1024; })}},
           {"outcome_unknown", Value{unknown}},
           {"replay_authority", Value{false}}}));
    }
    Value packet = Value::object(
        {{"mission", mission},
         {"source", lineage},
         {"cause", Value{std::string{error_name(cause.code)}}},
         {"detail", Value{Number{cause.detail}}},
         {"local_quiescence", Value{"cooperative-native; no live callbacks or "
                                    "children; no uncontained exec"}},
         {"attempts", Value{attempts}},
         {"attempt_count", number(total)},
         {"older_attempts",
          Value{"inspect exact original audit; not retroactively settled"}},
         {"dirty_paths", Value{"unavailable unless supplied in root mission packet; do "
                               "not reset checkout"}}});
    std::uint64_t issuer = 0;
    if (entropy(whole, &issuer, sizeof(issuer)) != 0 ||
        predicate(whole, 30, [&] { return issuer == 0; }))
      throw Error{ErrorCode::io};
    auto root_mutation = whole.control(41);
    RawProductStorageOwner storage_owner;
    if (whole.enabled())
      storage_owner = unwrap(RawProductStorageOwner::prepare(whole.reference()));
    auto root = unwrap(RetainedState::create(
        std::make_unique<NativeJournalDirectory>(
            unwrap(NativeJournalDirectory::open(destination.string(), storage_owner))),
        "audit",
        JournalHeader{random_identity<EnvironmentId>(whole),
                      random_identity<AuditStreamId>(whole),
                      issuer,
                      {32 * 1024 * 1024, 96 * 1024 * 1024},
                      std::nullopt},
        {64ULL * 1024 * 1024, 20000}, storage_owner));
    mutation(whole, 2, root_mutation);
    AuditLog log{*root};
    ContextStore context{log, RawContextOrigin{whole.reference()}};
    auto seed_mutation = whole.control(41);
    context.seed_successor(Value::object({{"version", number(1)},
                                          {"source", lineage},
                                          {"entries", Value{selected}}}),
                           RawContextOrigin{whole.reference()});
    mutation(whole, 3, seed_mutation);
    auto instruction_mutation = whole.control(41);
    context.append({Value::object({{"role", Value{"developer"}},
                                   {"content", Value{std::string{instruction}}}})},
                   "backstop.instructions", RawContextOrigin{whole.reference()});
    mutation(whole, 4, instruction_mutation);
    log.original({"backstop.packet", encoded(whole, 26, packet), Value{}});
    auto restore_credit = whole.control(41);
    // request() installs the engine cancellation callback on the shared adapter.
    // Restore it before this recovery lifetime ends; do not leave borrowed captures.
    struct RestoreCancellation {
      CodingProvider &provider;
      std::function<bool()> previous;
      RawSessionCall &owner;
      std::optional<RawControlToken> credit;
      ~RestoreCancellation() {
        provider.cancelled = std::move(previous);
        try {
          mutation(owner, 8, credit);
        } catch (const Error &) {
        }
      }
    } restore_cancellation{assessor, assessor.cancelled, whole,
                           std::move(restore_credit)};
    CodingEngine engine{log, context, assessor, settings.model};
    engine.effort(settings.effort);
    // The backstop's journal records the session's profile set, and its requests the
    // profile each resolves to; a refusal is reported, not only its error code.
    if (const auto *profiles = source_engine.session_profiles()) {
      if (profiles->set_record != Value{})
        log.record(ApplicationChannel::program, profiles->set_record);
      if (const auto pinned = engine.profiles(*profiles); !pinned.has_value()) {
        if (display)
          display("\n[backstop: provider profile refused: " + pinned.error().message() +
                  "]\n");
        throw Error{ErrorCode::invalid_range};
      }
    }
    // Bound even a selected Lua action that loops without making progress. The
    // ordinary VM interrupt hook and transport cancellation consume this deadline.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    engine.cancelled = [&] {
      return (paused && paused()) || std::chrono::steady_clock::now() >= deadline;
    };
    engine.display = display;
    const auto protected_tree = resolved_path(whole, source_path, false);
    RawAdapterContext pivot_context = whole.context();
    auto protect_path = [&](const std::filesystem::path &requested) {
      const auto requested_ref = source(whole, 8, requested.native());
      const auto policy_context = pivot_context;
      RawSessionCall protection{4732, 4733, &policy_context,
                                std::array{ByteView{requested_ref.lane},
                                           raw_native_field(requested_ref.ordinal)}};
      protection.run([&] {
        const auto resolved = resolved_path(protection, requested, true);
        const auto resolved_ref = source(protection, 24, resolved.native());
        const auto tree_ref = source(protection, 24, protected_tree.native());
        const auto relative_context = protection.context();
        const std::uint8_t relative_method = 3;
        RawSessionCall relative_call{
            4740, 4741, &relative_context,
            std::array{raw_native_field(relative_method), ByteView{resolved_ref.lane},
                       raw_native_field(resolved_ref.ordinal), ByteView{tree_ref.lane},
                       raw_native_field(tree_ref.ordinal)}};
        const auto relative = relative_call.run([&] {
          auto paid = relative_call.source_credit(raw_session_bound(
              resolved.native().size(),
              raw_session_bound(raw_session_product(protected_tree.native().size(), 3),
                                2)));
          auto returned = resolved.lexically_relative(protected_tree);
          (void)relative_call.source(24, returned.native(), paid, 4702);
          return returned;
        });
        if (predicate(protection, 38, [&] { return relative.empty(); }) ||
            predicate(protection, 39, [&] { return *relative.begin() != ".."; }))
          throw Error{ErrorCode::conflict};
        // Resolve hardlink aliases as well as symlinks. This is an owned local
        // workflow, not an OS sandbox against concurrent adversarial filesystem edits.
        if (filesystem_predicate(protection, 4, resolved,
                                 [&] { return std::filesystem::exists(resolved); })) {
          std::size_t inspected = 0;
          const auto iterator_context = protection.context();
          const auto tree_source = source(protection, 24, protected_tree.native());
          const std::uint8_t create_method = 7;
          RawSessionCall creation{4740, 4741, &iterator_context,
                                  std::array{raw_native_field(create_method),
                                             ByteView{tree_source.lane},
                                             raw_native_field(tree_source.ordinal)},
                                  13};
          auto iterator = filesystem_run(creation, [&] {
            auto paid = creation.source_credit(
                raw_session_bound(protected_tree.native().size(), 1 + NAME_MAX));
            std::filesystem::recursive_directory_iterator result{protected_tree};
            const bool end = result == std::filesystem::recursive_directory_iterator{};
            if (!end)
              (void)creation.source(24, result->path().native(), paid, 4702);
            creation.finish(RawOperationBranch::returned, nullptr,
                            std::array{raw_native_field(end)});
            return result;
          });
          while (iterator != std::filesystem::recursive_directory_iterator{}) {
            const auto &entry = *iterator;
            if (predicate(protection, 41, [&] { return ++inspected > 4096; }))
              throw Error{ErrorCode::capacity};
            if (filesystem_predicate(protection, 5, entry.path(),
                                     [&] { return entry.is_regular_file(); }) &&
                filesystem_predicate(
                    protection, 6, resolved,
                    [&] { return std::filesystem::equivalent(resolved, entry.path()); },
                    &entry.path()))
              throw Error{ErrorCode::conflict};
            const auto entry_ref = source(protection, 24, entry.path().native());
            const std::uint8_t increment_method = 8;
            RawSessionCall increment{4740, 4741, &iterator_context,
                                     std::array{raw_native_field(increment_method),
                                                ByteView{entry_ref.lane},
                                                raw_native_field(entry_ref.ordinal)},
                                     13};
            filesystem_run(increment, [&] {
              auto paid = increment.source_credit(
                  raw_session_bound(entry.path().native().size(), 1 + NAME_MAX));
              ++iterator;
              const bool end =
                  iterator == std::filesystem::recursive_directory_iterator{};
              if (!end)
                (void)increment.source(24, iterator->path().native(), paid, 4702);
              increment.finish(RawOperationBranch::returned, nullptr,
                               std::array{raw_native_field(end)});
            });
          }
        }
      });
    };
    bool assessing = false;
    unsigned assessment_calls = 0;
    engine.effect_policy = [&](std::string_view name, const Value &input) {
      RawSessionCall policy{4730, 4731, &pivot_context,
                            std::array{raw_native_field(name.size())}};
      policy.run([&] {
        (void)source(policy, 25, name);
        if (predicate(policy, 33, [&] { return name == "provider"; }) &&
            (predicate(policy, 34, [&] { return !assessing; }) ||
             predicate(policy, 35, [&] {
               auto counter = policy.control(45);
               ++assessment_calls;
               (void)policy.event(4734,
                                  std::array{raw_native_field(assessing),
                                             raw_native_field(assessment_calls)},
                                  counter);
               return assessment_calls > 1;
             })))
          throw Error{ErrorCode::conflict};
        if (predicate(policy, 36, [&] { return name == "exec" || name == "restart"; }))
          throw Error{ErrorCode::conflict};
        if (predicate(policy, 37,
                      [&] { return name == "write_file" || name == "edit_file"; }))
          protect_path(string_field(input, "path"));
      });
    };
    std::set<std::string> attempted_actions;
    Value outcome;
    for (unsigned pivot = 1; pivot <= 2; ++pivot) {
      RawSessionCall pivot_call{4720, 4721, &selected_owner,
                                std::array{raw_native_field(pivot)}};
      const auto pivot_result = pivot_call.run([&]() -> std::optional<Value> {
        pivot_context = pivot_call.context();
        check_pause(whole, paused);
        if (!predicate(pivot_call, 9, [&] { return detail::locally_quiescent(); }))
          throw Error{ErrorCode::external_unknown};
        try {
          assessing = true;
          assessment_calls = 0;
          RawSessionCall assessment_call{
              4722, 4723, &pivot_context,
              std::array{raw_native_field(assessing),
                         raw_native_field(assessment_calls)}};
          assessment_call.run([&] {
            auto paid = assessment_call.source_credit(ValueLimits{}.bytes);
            auto formatted = unwrap(format_value(packet));
            (void)assessment_call.source(5, formatted, paid, 4702);
            engine.turn({formatted, assessment});
          });
          assessing = false;
          const auto choice = engine.workflow_result();
          for (const auto key : {"retained", "uncertain", "change", "next", "action"})
            if (predicate(pivot_call, 31,
                          [&] {
                            const auto &text = string_field(choice, key);
                            (void)source(pivot_call, 23, text);
                            return text.empty();
                          }) ||
                predicate(pivot_call, 32,
                          [&] { return string_field(choice, key).size() > 32768; }))
              throw Error{ErrorCode::invalid_range};
          const auto &oracle = field(choice, "oracle");
          const auto &path_text = string_field(oracle, "path");
          (void)source(pivot_call, 8, path_text);
          const auto path = std::filesystem::path{path_text};
          const auto &expected = string_field(oracle, "content");
          (void)source(pivot_call, 9, expected);
          if (!predicate(pivot_call, 10, [&] { return path.is_absolute(); }) ||
              predicate(pivot_call, 11, [&] { return expected.empty(); }) ||
              predicate(pivot_call, 12, [&] { return expected.size() > 32768; }))
            throw Error{ErrorCode::invalid_range};
          protect_path(path);
          const auto &action = string_field(choice, "action");
          const auto action_ref = source(pivot_call, 7, action);
          if (predicate(pivot_call, 13,
                        [&] { return attempted_actions.contains(action); }))
            throw Error{ErrorCode::conflict};
          std::string prior;
          const bool existed = filesystem_predicate(
              pivot_call, 4, path, [&] { return std::filesystem::exists(path); });
          (void)predicate(pivot_call, 14, [&] { return existed; });
          RawEventRef prior_ref{};
          if (existed)
            prior = raw_file_read(path, 1024 * 1024, &pivot_context, &prior_ref);
          if (existed && predicate(pivot_call, 15, [&] { return prior == expected; }))
            throw Error{ErrorCode::conflict};
          check_pause(whole, paused);
          auto choice_paid = pivot_call.source_credit(ValueLimits{}.bytes);
          auto choice_packet = unwrap(encode_packet_string(choice));
          const auto choice_ref =
              pivot_call.source(6, choice_packet, choice_paid, 4724);
          log.original({"backstop.pivot", std::move(choice_packet),
                        Value::object({{"ordinal", number(pivot)}})});
          auto action_mutation = pivot_call.control(49);
          attempted_actions.insert(action);
          const auto action_count = attempted_actions.size();
          mutation(pivot_call, 6, action_mutation, &action_count);
          auto key_credit =
              pivot_call.source_credit(sizeof("selected_pivot_") - 1 + 11);
          auto key = "selected_pivot_" + std::to_string(pivot);
          (void)pivot_call.source(22, key, key_credit, 4702);
          auto selected_mutation = pivot_call.control(49);
          packet.object().emplace_back(std::move(key), choice);
          const auto selected_count = packet.object().size();
          mutation(pivot_call, 5, selected_mutation, &selected_count);
          RawSessionCall action_call{4725, 4726, &pivot_context,
                                     std::array{ByteView{action_ref.lane},
                                                raw_native_field(action_ref.ordinal),
                                                ByteView{choice_ref.lane},
                                                raw_native_field(choice_ref.ordinal)}};
          action_call.run([&] {
            engine.turn(
                {"Execute the explicitly selected pivot; preserve source unknowns.",
                 action});
          });
          check_pause(whole, paused);
          RawEventRef actual_ref{};
          const auto actual =
              raw_file_read(path, 1024 * 1024, &pivot_context, &actual_ref);
          auto comparison = pivot_call.control(65);
          const bool matches = actual == expected;
          (void)pivot_call.event(4727,
                                 std::array{ByteView{actual_ref.lane},
                                            raw_native_field(actual_ref.ordinal),
                                            raw_native_field(matches)},
                                 comparison);
          if (!matches)
            throw Error{ErrorCode::conflict};
          (void)source(pivot_call, 14, "useful-work-observed");
          auto outcome_mutation = pivot_call.control(49);
          outcome = Value::object({{"phase", Value{"useful-work-observed"}},
                                   {"pivot", number(pivot)},
                                   {"artifact", Value{path.string()}},
                                   {"content", Value{actual}},
                                   {"session", Value{destination.string()}},
                                   {"remote_effects_settled", Value{false}}});
          auto outcome_count = outcome.object().size();
          mutation(pivot_call, 7, outcome_mutation, &outcome_count);
          log.original({"backstop.outcome", encoded(pivot_call, 12, outcome), Value{}});
          return outcome;
        } catch (const Error &error) {
          whole.require_capture();
          pivot_call.require_capture();
          const bool expired = predicate(pivot_call, 17, [&] {
            return std::chrono::steady_clock::now() >= deadline;
          });
          const std::string_view phase = expired ? "deadline-blocked"
                                         : error.code == ErrorCode::interrupted
                                             ? "paused"
                                             : "no-progress";
          (void)source(pivot_call, 14, phase);
          auto outcome_mutation = pivot_call.control(49);
          outcome =
              Value::object({{"phase", Value{std::string{phase}}},
                             {"pivot", number(pivot)},
                             {"error", Value{std::string{error_name(error.code)}}},
                             {"session", Value{destination.string()}},
                             {"effect_outcome",
                              Value{"unknown; inspect recovery audit; no replay"}}});
          auto outcome_count = outcome.object().size();
          mutation(pivot_call, 7, outcome_mutation, &outcome_count);
          log.original({"backstop.outcome", encoded(pivot_call, 12, outcome), Value{}});
          if (predicate(pivot_call, 18,
                        [&] { return error.code == ErrorCode::interrupted; }) ||
              !predicate(pivot_call, 19, [&] { return detail::locally_quiescent(); }) ||
              !predicate(pivot_call, 20,
                         [&] { return root->state() == JournalWriterState::live; }))
            return outcome;
          auto key_credit =
              pivot_call.source_credit(sizeof("previous_pivot_outcome_") - 1 + 11);
          auto key = "previous_pivot_outcome_" + std::to_string(pivot);
          (void)pivot_call.source(22, key, key_credit, 4702);
          packet.object().emplace_back(std::move(key), outcome);
        }
        return {};
      });
      if (pivot_result)
        return *pivot_result;
      pivot_context = whole.context();
    }
    outcome.object().emplace_back(
        "bound",
        Value{"two no-progress pivots exhausted; explicit inspection required"});
    log.original({"backstop.blocked", encoded(whole, 13, outcome), Value{}});
    return outcome;
  });
}
} // namespace blackbird
