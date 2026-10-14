#include "blackbird/raw_beads.hpp"
#include <cerrno>
namespace blackbird {
namespace {
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
template <class T> T take(Result<T> value) {
  if (!value.has_value())
    throw value.error();
  return std::move(value).value();
}
constexpr std::array<std::uint32_t, 8> requests{3500, 3510, 3520, 3508,
                                                3516, 3524, 3528, 3532};
constexpr std::array<std::uint32_t, 8> returns{3501, 3512, 3521, 3509,
                                               3517, 3525, 3529, 3533};
constexpr std::array<std::uint64_t, 8> credits{171, 102, 226, 121, 119, 121, 126, 101};
} // namespace
RawBeadsTrace::RawBeadsTrace(const RawAdapterContext *context, RawBeadsKind kind,
                             const RawBeadsFields &extra)
    : kind_(kind) {
  if (!context || !context->parent || !context->parent->ordinal || !raw_enabled())
    return;
  issuer_ = raw_current_thread();
  emission_.prepare(credits.at(static_cast<std::size_t>(kind)));
  if (kind == RawBeadsKind::run || kind == RawBeadsKind::invoke)
    status_.emplace(take(raw_claim_data(60)));
  RawBeadsFields fields;
  fields.ref(*context->parent);
  for (const auto field : extra.view())
    fields.bytes(field);
  emission_.begin({requests.at(static_cast<std::size_t>(kind)),
                   returns.at(static_cast<std::size_t>(kind))},
                  fields.view());
  context_ = *context;
  context_.parent = reference();
}
RawBeadsTrace::~RawBeadsTrace() { finish(RawOperationBranch::unfinished); }
std::optional<RawCapacityClaim> RawBeadsTrace::data(std::uint64_t amount) {
  if (!enabled())
    return {};
  return take(raw_claim_data(amount));
}
std::optional<RawControlToken> RawBeadsTrace::control(std::uint64_t amount) {
  if (!enabled())
    return {};
  return take(raw_claim_control(amount));
}
RawEventRef RawBeadsTrace::body(std::uint32_t site, const RawBeadsFields &fields,
                                RawCapacityClaim &bank, RawPartition partition) {
  return take(raw_publish(site, fields.view(), partition, bank));
}
void RawBeadsTrace::finish(RawOperationBranch branch, const Error *error) noexcept {
  if (!enabled())
    return;
  const ErrnoGuard guard;
  RawBeadsFields fields;
  fields.ref(reference());
  fields.native(branch);
  switch (kind_) {
  case RawBeadsKind::run:
    fields.native(stage);
    fields.native(mutation);
    fields.native(bound);
    fields.optional(status_choice);
    break;
  case RawBeadsKind::argv:
    fields.native(stage);
    break;
  case RawBeadsKind::invoke:
    fields.native(stage);
    fields.native(write);
    fields.native(mutation);
    fields.native(spawned);
    fields.native(exit);
    fields.native(output);
    fields.native(stderr_bytes);
    fields.optional(status_choice);
    break;
  case RawBeadsKind::path:
    fields.optional(file_owner);
    fields.native(path_error.has_value());
    if (path_error) {
      const int value = path_error->value();
      const auto *category = &path_error->category();
      fields.native(value);
      fields.native(category);
      fields.native(error != nullptr);
      if (error) {
        fields.native(error->code);
        fields.native(error->detail);
      }
      emission_.close(fields.view());
      return;
    }
    break;
  case RawBeadsKind::check:
    fields.native(boolean.has_value());
    if (boolean)
      fields.native(*boolean);
    break;
  case RawBeadsKind::collect:
    fields.native(stage);
    fields.native(count_present);
    if (count_present)
      fields.native(output);
    fields.native(stderr_bytes);
    fields.native(exit);
    break;
  case RawBeadsKind::observer:
    break;
  case RawBeadsKind::parse:
    fields.native(result_present);
    fields.native(result_value);
    if (result_error)
      error = &*result_error;
    break;
  }
  fields.native(error != nullptr);
  if (error) {
    fields.native(error->code);
    fields.native(error->detail);
  }
  if (kind_ == RawBeadsKind::argv)
    fields.native(argc);
  emission_.close(fields.view());
  status_.reset();
}
void RawBeadsTrace::assignment(bool &target, bool value, std::uint8_t kind,
                               std::optional<RawEventRef> source) {
  auto bank = control(67);
  target = value;
  if (!bank)
    return;
  RawBeadsFields fields;
  fields.ref(reference());
  fields.native(kind);
  fields.native(target);
  fields.optional(source);
  (void)body(3502, fields, *bank);
}
void RawBeadsTrace::field(std::uint8_t kind, std::string_view value,
                          std::optional<RawCapacityClaim> &bank) {
  if (!bank)
    return;
  const auto size = value.size();
  RawBeadsFields fields;
  fields.ref(reference());
  fields.native(kind);
  fields.native(size);
  fields.bytes(detail::raw_string_field(value));
  (void)body(3503, fields, *bank, RawPartition::data);
  bank.reset();
}
std::string RawBeadsTrace::status(std::string_view value, std::uint8_t role) {
  std::string acquired{value};
  if (status_) {
    const auto size = acquired.size();
    RawBeadsFields fields;
    fields.ref(reference());
    fields.native(role);
    fields.native(size);
    fields.bytes(detail::raw_string_field(acquired));
    status_choice = body(3504, fields, *status_, RawPartition::data);
    status_.reset();
  }
  return acquired;
}
void RawBeadsTrace::caught(const Error &error, bool child_spawned) {
  if (!enabled())
    return;
  RawBeadsFields fields;
  fields.ref(reference());
  fields.native(stage);
  fields.native(error.code);
  fields.native(error.detail);
  fields.native(child_spawned);
  fields.native(write);
  (void)body(3530, fields, *emission_.bank());
}
void RawBeadsTrace::cache(bool update, std::optional<RawEventRef> source,
                          const std::size_t *count,
                          std::optional<RawControlToken> &bank) {
  if (!bank)
    return;
  const auto kind = std::uint8_t(update ? 2 : 1);
  const bool present = count != nullptr;
  RawBeadsFields fields;
  fields.ref(reference());
  fields.native(kind);
  fields.optional(source);
  fields.native(present);
  if (count)
    fields.native(*count);
  (void)body(3505, fields, *bank);
}
RawBeadsArgv::RawBeadsArgv(RawBeadsTrace &owner, std::uint8_t kind, bool write)
    : trace_(owner.context(), RawBeadsKind::argv, [&] {
        RawBeadsFields fields;
        fields.native(kind);
        fields.native(write);
        return fields;
      }()) {}
std::string_view raw_beads_description() noexcept {
  return "BBRAW BEADS v1 sites3500-3599; envelope length:u32/site:u32/ordinal:u64; "
         "native Ref lane16/ordinal:u64, bool/u8=1,size_t=8,int=4,Error "
         "code:u8/detail:i64; "
         "3500 run parentR/boundSource?R/cacheSource?R/bound/oldMutation; "
         "3501 runR/branch/stage/mutation/bound/statusChoice?R/Error?; "
         "3502 ownerR/kind/value/source?R;3503 ownerR/field/length/nativebytes; "
         "field9 completed canonical-project/.beads SAME joined operand; "
         "3504 ownerR/role/length/required-statusbytes;3505 "
         "runR/kind/source?R/count?size_t; "
         "3508 runR/path-kind;3509 "
         "requestR/branch/fileOwner?R/errorCode?(int,categorypointer)/Error?; "
         "3510 runR/kind/write;3511 preparationR/index/length/argvbytes; "
         "3512 preparationR/branch/stage/Error?/argc; "
         "3516 ownerR/check-kind/subject?R;3517 requestR/branch/bool?/Error?; "
         "3520 runR/preparationR/kind/write/argc;3521 "
         "invokeR/branch/stage/write/mutation/"
         "spawned/exit/outcount/stderrcount/statusChoice?R/Error?; "
         "3524 invokeR/limit;3525 "
         "requestR/branch/stage/returnedOutCount?size_t/stderrcount/exit/Error?; "
         "3528 invokeR/child?R/channel/viewcount/observerPresent;3529 "
         "requestR/branch/Error?; "
         "3530 invokeR/stage/Error/spawned/write;3532 invokeR/outcount; "
         "3533 requestR/branch/resultPresent/resultHasValue/Error?; "
         "branch "
         "absent0/returned1/refused2/Error3/allocation4/length5/standard6/nonstandard7/"
         "unfinished8; "
         "kind/version1/where2/exactshow3/action4; stages and check tags in Beads "
         "subplan; "
         "selected Child1404 preserves actual stdout/stderr once before "
         "observer/limits/parse; "
         "default calls optout, no elapsed_us/JSON metric transforms";
}
} // namespace blackbird
