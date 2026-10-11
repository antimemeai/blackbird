#include "blackbird/raw_authored.hpp"

namespace blackbird {
namespace {
static_assert(sizeof(std::int64_t) == 8 && sizeof(double) == 8 && sizeof(bool) == 1 &&
              sizeof(std::size_t) == 8 && sizeof(void *) == 8);
template <class T> ByteView native(const T &value) noexcept {
  return std::as_bytes(std::span{&value, std::size_t{1}});
}
void require(Result<void> result) {
  if (!result.has_value())
    throw result.error();
}
struct Schema {
  std::uint32_t event;
  std::uint8_t kind;
};
template <class T>
Result<std::uint64_t> emit_value(RawAuthoredOwner &owner, std::int64_t site,
                                 const T &value, Schema schema) {
  auto admitted = raw_authored_admit(owner);
  if (!admitted.has_value())
    return Result<std::uint64_t>::failure(admitted.error());
  auto credit = std::move(admitted).value();
  try {
    if (site <= 0)
      throw Error{ErrorCode::invalid_range};
    if (!owner.enabled)
      return Result<std::uint64_t>::success(0);
    RawAuthoredReference result;
    if constexpr (std::is_same_v<T, ByteView>) {
      const auto length = value.size();
      const std::array fields{ByteView{owner.generation},
                              ByteView{owner.vm.lane},
                              native(owner.vm.ordinal),
                              native(site),
                              native(length),
                              value};
      result = raw_authored_publish(schema.event, fields);
    } else {
      const std::array fields{ByteView{owner.generation}, ByteView{owner.vm.lane},
                              native(owner.vm.ordinal), native(site), native(value)};
      result = raw_authored_publish(schema.event, fields);
    }
    return Result<std::uint64_t>::success(result.ordinal);
  } catch (const Error &error) {
    if (credit.has_value()) {
      try {
        const std::array fields{ByteView{owner.generation}, ByteView{owner.vm.lane},
                                native(owner.vm.ordinal),   native(site),
                                native(schema.kind),        native(error.code),
                                native(error.detail)};
        (void)raw_authored_publish(1516, fields, &*credit);
      } catch (const Error &failure) {
        raw_fence(failure);
      }
    }
    if (error.code == ErrorCode::interrupted ||
        error.code == ErrorCode::audit_unavailable)
      owner.admission_failure = error;
    return Result<std::uint64_t>::failure(error);
  }
}
} // namespace
Result<std::optional<RawControlToken>> raw_authored_admit(RawAuthoredOwner &owner) {
  if (owner.admission_failure.has_value())
    return Result<std::optional<RawControlToken>>::failure(*owner.admission_failure);
  if (!owner.enabled)
    return Result<std::optional<RawControlToken>>::success(std::nullopt);
  auto claim = raw_claim_control(86);
  if (!claim.has_value()) {
    owner.admission_failure = claim.error();
    return Result<std::optional<RawControlToken>>::failure(claim.error());
  }
  return Result<std::optional<RawControlToken>>::success(std::move(claim).value());
}
RawAuthoredReference raw_authored_publish(std::uint32_t site,
                                          std::span<const ByteView> fields,
                                          RawControlToken *credit) {
  if (!raw_enabled())
    return {};
  if (credit == nullptr) {
    std::uint64_t length = raw_envelope_size;
    for (const auto bytes : fields) {
      if (bytes.size() > UINT32_MAX - length)
        throw Error{ErrorCode::overflow};
      length += bytes.size();
    }
    require(raw_wait_data(length));
  }
  const auto result =
      raw_emit(site, fields,
               credit == nullptr ? RawPartition::data : RawPartition::control, credit);
  if (!result.has_value())
    throw result.error();
  return {raw_lane_epoch(), result.value()};
}
Result<std::uint64_t> raw_authored_emit(RawAuthoredOwner &owner, std::int64_t site,
                                        std::int64_t value) {
  return emit_value(owner, site, value, {1510, 1});
}
Result<std::uint64_t> raw_authored_emit(RawAuthoredOwner &owner, std::int64_t site,
                                        double value) {
  return emit_value(owner, site, value, {1511, 2});
}
Result<std::uint64_t> raw_authored_emit(RawAuthoredOwner &owner, std::int64_t site,
                                        bool value) {
  return emit_value(owner, site, value, {1514, 3});
}
Result<std::uint64_t> raw_authored_emit(RawAuthoredOwner &owner, std::int64_t site,
                                        ByteView value) {
  return emit_value(owner, site, value, {1513, 4});
}
std::string_view raw_authored_description() noexcept {
  return "authored1500-1599 v1; host-native; G=immutable VM definition generation16; "
         "R=lane16,u64 ordinal;SourceReference=journal16,u64 sequence;"
         "1500 G,bool root_available,present-only SourceReference;1501 R,u8 "
         "null0/created1;"
         "1502 R;1503 R,int pcall status;1504 R,u8 "
         "closed1/allocation_failed2/installation_failed3,"
         "failure-only u8 ErrorCode,i64 detail;1505 G,SourceReference,size_t length;"
         "1506 R,u8 sourcekind(1 root/2 tool/3 tool_api/4 print/5 module/6 validation),"
         "size_t length,exact bytes;1507 R,int Lua status,bool function_available,"
         "success-only void* lua_topointer;1508 R VM,R load;1509 R,int pcall status;"
         "1510/1511 G,R,lua_Integer signed64 site,lua_Integer signed64/lua_Number "
         "double value;"
         "1512 G,R,lua_Integer site,int lua_toboolean;1513 G,R,lua_Integer site,size_t "
         "length,"
         "exact bytes;1514 G,R,i64 site,bool native value;1515 G,R,int argc,int "
         "site_type,"
         "int value_type,bool integer_site_available,present-only lua_Integer site,"
         "u8 ErrorCode,i64 detail;1516 G,R,i64 site,u8 nativekind(1 signed/2 real/3 "
         "bool/4 bytes),"
         "u8 ErrorCode,i64 detail;1517 R,void* loaded function;1518 R,int pcall status;"
         "1519-1599 reserved;qualified sizeof lua_Integer8 lua_Number8 int4 bool1 "
         "size_t8 void*8;"
         "Lua emission returns zero values; dynamic Lua source has no static closure "
         "attribution;";
}
} // namespace blackbird
