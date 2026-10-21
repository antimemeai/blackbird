#include "blackbird/raw_context.hpp"
#include <algorithm>
#include <cerrno>
namespace blackbird {
namespace {
struct ErrnoGuard {
  int saved = errno;
  ~ErrnoGuard() { errno = saved; }
};
using detail::raw_native_field;
using detail::raw_string_field;
RawEventRef emit(std::uint32_t site, std::span<const ByteView> fields,
                 RawCapacityClaim *credit,
                 RawPartition partition = RawPartition::control) noexcept {
  const ErrnoGuard guard;
  if (credit == nullptr)
    return {};
  const auto result = raw_emit(site, fields, partition, credit);
  if (!result.has_value()) {
    raw_fence(result.error());
    return {};
  }
  return {raw_lane_epoch(), result.value()};
}
RawEventRef linked(std::uint32_t site, RawEventRef request,
                   std::span<const ByteView> fields, RawCapacityClaim *credit,
                   RawPartition partition = RawPartition::control) noexcept {
  std::array<ByteView, 24> body{};
  if (fields.size() > body.size() - 2) {
    raw_fence({ErrorCode::capacity});
    return {};
  }
  body[0] = request.lane;
  body[1] = raw_native_field(request.ordinal);
  std::copy(fields.begin(), fields.end(), body.begin() + 2);
  return emit(site, std::span{body}.first(fields.size() + 2), credit, partition);
}
RawCapacityClaim claim(std::uint64_t bytes, RawPartition partition) {
  auto result = partition == RawPartition::control ? raw_claim_control(bytes)
                                                   : raw_claim_data(bytes);
  if (!result.has_value())
    throw result.error();
  return std::move(result).value();
}
} // namespace
RawContextTrace::RawContextTrace(RawContextAction action, std::uint64_t credit,
                                 RawContextOrigin origin, const RawContextState *state)
    : origin_(origin) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return;
  if (origin.input == RawContextOrigin::Input::no_new && !origin.control)
    throw Error{ErrorCode::capacity};
  auto admitted =
      origin.control ? origin.control->split(credit) : raw_claim_control(credit);
  if (!admitted.has_value())
    throw admitted.error();
  credit_.emplace(std::move(admitted).value());
  if (origin.retained_regions)
    regions_.emplace(*origin.retained_regions);
  const bool parent = origin.parent.ordinal != 0;
  std::array<ByteView, 12> fields{};
  std::size_t count = 0;
  fields[count++] = raw_native_field(action);
  fields[count++] = raw_native_field(parent);
  if (parent) {
    fields[count++] = origin.parent.lane;
    fields[count++] = raw_native_field(origin.parent.ordinal);
  }
  if (state != nullptr) {
    const auto length = state->head.size();
    fields[count++] = raw_native_field(length);
    fields[count++] = raw_string_field(state->head);
    fields[count++] = raw_native_field(state->entries);
    fields[count++] = raw_native_field(state->originals);
    fields[count++] = raw_native_field(state->captured);
    fields[count++] = raw_native_field(state->workflow);
    fields[count++] = raw_native_field(state->pending);
    fields[count++] = raw_native_field(state->archive_loaded);
    request_ = emit(1800, std::span{fields}.first(count), bank());
  } else
    request_ = emit(1800, std::span{fields}.first(count), bank());
}
RawContextTrace::~RawContextTrace() { finish(5); }
RawContextOrigin RawContextTrace::child() noexcept {
  return {request_, bank(), origin_.retained_regions, origin_.input};
}
RawContextOrigin RawContextTrace::fresh() noexcept {
  return {request_, nullptr, origin_.retained_regions, origin_.input};
}
RawContextPublication RawContextTrace::publication() noexcept {
  return {request_, bank()};
}
void RawContextTrace::event(std::uint32_t site,
                            std::span<const ByteView> fields) noexcept {
  (void)linked(site, request_, fields, bank());
}
RawEventRef RawContextTrace::request(std::uint32_t site,
                                     std::span<const ByteView> fields) noexcept {
  return linked(site, request_, fields, bank());
}
void RawContextTrace::reply(std::uint32_t site, RawEventRef request,
                            std::span<const ByteView> fields) noexcept {
  (void)linked(site, request, fields, bank());
}
void RawContextTrace::protection_return(RawEventRef request, std::uint8_t branch,
                                        std::optional<Error> error) noexcept {
  std::array<ByteView, 3> fields{};
  fields[0] = raw_native_field(branch);
  std::size_t count = 1;
  if (error) {
    fields[count++] = raw_native_field(error->code);
    fields[count++] = raw_native_field(error->detail);
  }
  reply(1805, request, std::span{fields}.first(count));
}
void RawContextTrace::state(std::uint8_t stage, const RawContextState &state) noexcept {
  stage_ = stage;
  const auto length = state.head.size();
  event(1803,
        std::array{raw_native_field(stage), raw_native_field(length),
                   raw_string_field(state.head), raw_native_field(state.entries),
                   raw_native_field(state.originals), raw_native_field(state.captured),
                   raw_native_field(state.workflow), raw_native_field(state.pending),
                   raw_native_field(state.archive_loaded)});
}
void RawContextTrace::rows(std::uint8_t variant, std::size_t count) noexcept {
  event(1806, std::array{raw_native_field(variant), raw_native_field(count)});
}
std::optional<RawCapacityClaim> RawContextTrace::data_credit(std::uint64_t bytes) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  if (origin_.input == RawContextOrigin::Input::no_new)
    throw Error{ErrorCode::capacity};
  return claim(bytes, RawPartition::data);
}
std::optional<RawControlToken> RawContextTrace::control_credit(std::uint64_t bytes) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return {};
  if (origin_.input == RawContextOrigin::Input::no_new)
    throw Error{ErrorCode::capacity};
  return claim(bytes, RawPartition::control);
}
void RawContextTrace::control(std::uint32_t site, std::span<const ByteView> fields,
                              RawControlToken *credit) noexcept {
  (void)linked(site, request_, fields, credit);
}
void RawContextTrace::data(std::uint32_t site, std::span<const ByteView> fields,
                           RawCapacityClaim *credit) noexcept {
  (void)linked(site, request_, fields, credit, RawPartition::data);
}
RawContextPair::RawContextPair(RawContextOrigin origin, std::uint64_t credit,
                               RawContextPairSites sites,
                               std::span<const ByteView> fields)
    : return_site_(sites.returned) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return;
  if (origin.input == RawContextOrigin::Input::no_new)
    throw Error{ErrorCode::capacity};
  credit_.emplace(claim(credit, RawPartition::control));
  request_ = linked(sites.request, origin.parent, fields, &*credit_);
}
RawContextPair::~RawContextPair() { failure({ErrorCode::external_unknown}); }
void RawContextPair::finish(std::uint8_t branch,
                            std::span<const ByteView> fields) noexcept {
  if (finished_)
    return;
  std::array<ByteView, 12> body{};
  if (fields.size() > body.size() - 1) {
    raw_fence({ErrorCode::capacity});
    return;
  }
  body[0] = raw_native_field(branch);
  std::copy(fields.begin(), fields.end(), body.begin() + 1);
  (void)linked(return_site_, request_, std::span{body}.first(fields.size() + 1),
               credit_ ? &*credit_ : nullptr);
  finished_ = true;
}
void RawContextPair::failure(Error error) noexcept {
  finish(1, std::array{raw_native_field(error.code), raw_native_field(error.detail)});
}
void RawContextTrace::label(std::uint8_t kind, std::string_view bytes,
                            bool cleanup_literal) {
  const ErrnoGuard guard;
  if (!raw_enabled())
    return;
  const auto length = bytes.size();
  const auto fields = std::array{raw_native_field(kind), raw_native_field(length),
                                 raw_string_field(bytes)};
  if (cleanup_literal) {
    event(1816, fields);
    return;
  }
  if (origin_.input == RawContextOrigin::Input::no_new)
    throw Error{ErrorCode::capacity};
  if (length > UINT32_MAX - 49)
    throw Error{ErrorCode::overflow};
  auto data = claim(49 + length, RawPartition::data);
  (void)linked(1816, request_, fields, &data, RawPartition::data);
}
void RawContextTrace::finish(std::uint8_t branch, std::optional<Error> error) noexcept {
  if (finished_)
    return;
  std::array<ByteView, 4> fields{};
  fields[0] = raw_native_field(branch);
  fields[1] = raw_native_field(stage_);
  std::size_t count = 2;
  if (error) {
    fields[count++] = raw_native_field(error->code);
    fields[count++] = raw_native_field(error->detail);
  }
  event(1815, std::span{fields}.first(count));
  finished_ = true;
}
RawEventRef raw_context_publication(RawContextPublication publication,
                                    ApplicationRecordId identity,
                                    ApplicationChannel channel,
                                    JournalCursor cursor) noexcept {
  return linked(1801, publication.owner,
                std::array{ByteView{identity.bytes()}, raw_native_field(channel),
                           ByteView{cursor.journal.bytes()},
                           raw_native_field(cursor.sequence),
                           raw_native_field(cursor.end_offset)},
                publication.control);
}
void raw_context_publication_return(RawContextPublication publication,
                                    RawEventRef request, const RecordReference *record,
                                    const JournalCursor *cursor,
                                    const Error *error) noexcept {
  const std::uint8_t branch = error ? 1 : 0, kind = record ? 1 : cursor ? 2 : 0;
  std::array<ByteView, 5> fields{};
  std::size_t count = 0;
  fields[count++] = raw_native_field(branch);
  fields[count++] = raw_native_field(kind);
  if (record) {
    fields[count++] = record->journal.bytes();
    fields[count++] = raw_native_field(record->sequence);
  } else if (cursor) {
    fields[count++] = cursor->journal.bytes();
    fields[count++] = raw_native_field(cursor->sequence);
    fields[count++] = raw_native_field(cursor->end_offset);
  } else if (error) {
    fields[count++] = raw_native_field(error->code);
    fields[count++] = raw_native_field(error->detail);
  }
  (void)linked(1802, request, std::span{fields}.first(count), publication.control);
}
std::vector<std::byte> raw_context_read(const ImmutableBytes &payload,
                                        RawContextSource source,
                                        RawContextOrigin origin) {
  const ErrnoGuard guard;
  if (!raw_enabled()) {
    auto result = payload.read();
    if (!result.has_value())
      throw result.error();
    return std::move(result).value();
  }
  if (origin.input == RawContextOrigin::Input::no_new)
    throw Error{ErrorCode::capacity};
  auto control = claim(152, RawPartition::control);
  const std::uint8_t kind = source.retained ? 1 : 0;
  const auto length = payload.size();
  if (source.retained && !source.record)
    throw Error{ErrorCode::invalid_identity};
  const bool record = source.record.has_value(),
             application = source.application.has_value();
  std::array<ByteView, 12> fields{};
  std::size_t count = 0;
  fields[count++] = raw_native_field(source.purpose);
  fields[count++] = raw_native_field(kind);
  fields[count++] = raw_native_field(length);
  fields[count++] = raw_native_field(record);
  if (record) {
    fields[count++] = source.record->journal.bytes();
    fields[count++] = raw_native_field(source.record->sequence);
  }
  fields[count++] = raw_native_field(application);
  if (application)
    fields[count++] = source.application->bytes();
  fields[count++] = raw_native_field(source.channel);
  fields[count++] = raw_native_field(source.index);
  const auto request =
      linked(1813, origin.parent, std::span{fields}.first(count), &control);
  try {
    std::optional<RawCapacityClaim> data;
    if (!source.retained) {
      if (length > UINT32_MAX - 49)
        throw Error{ErrorCode::overflow};
      data.emplace(claim(49 + length, RawPartition::data));
    }
    auto acquired = payload.read();
    if (!acquired.has_value())
      throw acquired.error();
    auto bytes = std::move(acquired).value();
    const std::uint8_t branch = 0;
    const auto actual = bytes.size();
    if (data)
      (void)linked(1814, request,
                   std::array{raw_native_field(branch), raw_native_field(actual),
                              ByteView{bytes}},
                   &*data, RawPartition::data);
    else
      (void)linked(1814, request,
                   std::array{raw_native_field(branch), raw_native_field(actual)},
                   &control);
    return bytes;
  } catch (const Error &error) {
    const std::uint8_t branch = 1;
    (void)linked(1814, request,
                 std::array{raw_native_field(branch), raw_native_field(error.code),
                            raw_native_field(error.detail)},
                 &control);
    throw;
  } catch (const std::bad_alloc &) {
    const std::uint8_t branch = 1;
    const Error error{ErrorCode::allocation};
    (void)linked(1814, request,
                 std::array{raw_native_field(branch), raw_native_field(error.code),
                            raw_native_field(error.detail)},
                 &control);
    throw;
  } catch (...) {
    const std::uint8_t branch = 1;
    const Error error{ErrorCode::external_unknown};
    (void)linked(1814, request,
                 std::array{raw_native_field(branch), raw_native_field(error.code),
                            raw_native_field(error.detail)},
                 &control);
    throw;
  }
}
std::string_view raw_context_description() noexcept {
  return R"raw(BBRAWABI v1 native64 little-endian; Ref=lane16+ordinal8; Text=size_t+bytes. No padding.
Actions: reconstruct0/checkpoint1/view2/stats3/items4/append5/validate_seed6/seed7/edit8/restore9/originals10/manage11/inspect12/budget13/pending14/begin15/finish16/reject17/publish18/original19/cut20/repair21.
Labels: origin0/entry1/reason2/query_kind3/format4/managed_source5. Input purpose: Original0/replay1/archive2/history3; source_kind resident0/retained_packet1.
1800:action:u8,parent:bool,[Ref],instance: head Text,entries/originals/captured:size_t,workflow/pending/archive_loaded:bool; static validation omits instance.
1801:owner Ref,actual ApplicationRecordId16,channel:uint16,actual journal16/sequence/end:uint64 before publication.
1802:publication Ref,branch:u8,kind:u8; actual record24 or cursor32 on success, ErrorCode/i64 on error.
1803:owner Ref,stage:u8,head Text,entries/originals/captured:size_t,workflow/pending/archive_loaded:bool after reached state transfer.
1804:owner Ref,callback_kind:u8 before protection arguments/call.
1805:protection Ref,branch:u8,[ErrorCode/i64] once.
1806:owner Ref,variant:u8,actual returned rows:size_t after output construction.
1807:stats Ref,item ID Text,existing native codec length:size_t before presentation.
1808:stats Ref,entry_count/input_bytes/view_bytes:size_t.
1809:inspect Ref,page length:size_t,existing page bytes before hex.
1810:inspect Ref,offset:u64,page size/total:size_t,snapshot Text,current head Text after output construction.
1811:checkpoint Ref,root cursor32,had_saved:bool,[actual prior saved cursor32] before save.
1812:save Ref,branch:u8,[ErrorCode/i64],root cursor32,has_saved:bool,[actual saved cursor32] before pruning.
1813:owner Ref,purpose/source_kind:u8,length:size_t,has_record:bool,[record24],has_application:bool,[ApplicationRecordId16],channel:uint16,index:size_t before source read.
1814:input Ref,branch:u8,actual length:size_t,[literal resident bytes] on success; ErrorCode/i64 on failure; before decode. A retained input references its actual source record.
1815:owner Ref,returned/refused/staged/Error/allocation/unknown:u8,reached stage:u8,[ErrorCode/i64] once.
1816:owner Ref,label_kind:u8,existing label Text; named bounded cleanup literal may use held CONTROL.
1817:owner Ref,first/requested/history.count:size_t,purpose:u8 before existing bounded page.
1818:page Ref,branch:u8,[actual rows:size_t],[ErrorCode/i64] before decode.
1819:owner Ref,family:u8,kind:uint16,actual parsed identity16,actual root cursor32 before lookup.
1820:lookup Ref,branch:u8; found0 actual record24/channel:uint16, failure1 ErrorCode/i64, missing2, non-application3 actual record24.
1821:reconstruct Ref,saved0/fallback1/accepted2/rejected3/invalid4/skipped-by-repair5/after-bad6/read-only7:u8,[ErrorCode/i64] actual branch; fallback1 is followed by whole-history playback, read-only7 names the frame that did not apply.
1822:reconstruct Ref,actual saved cursor32,existing version/source-present flags.
1823:managed Ref,existing mode Text,actual selected count:size_t,existing stage Text.
1824:reserved; no fabricated physical reference.
1825:budget Ref,actual max_payload/max_batch:u32,computed capacity two u64.
1826:inspect Ref,actual requested/limit:size_t,json_export:bool.
1827:finish Ref,actual success:bool,boundary_program_present:bool before workflow transfer.
)raw";
}
} // namespace blackbird
