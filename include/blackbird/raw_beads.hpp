#pragma once
#include "blackbird/raw_caller.hpp"
#include <array>
#include <cstring>
namespace blackbird {
struct RawBeadsFields {
  RawBeadsFields() = default;
  RawBeadsFields(const RawBeadsFields &other) { *this = other; }
  RawBeadsFields &operator=(const RawBeadsFields &other) {
    scalar = other.scalar;
    owned = other.owned;
    size = other.size;
    for (std::size_t i = 0; i < size; ++i)
      fields[i] = owned[i] ? ByteView{scalar[i].data(), other.fields[i].size()}
                           : other.fields[i];
    return *this;
  }
  std::array<ByteView, 24> fields{};
  std::array<std::array<std::byte, 16>, 24> scalar{};
  std::array<bool, 24> owned{};
  std::size_t size = 0;
  void bytes(ByteView value) {
    fields.at(size) = value;
    owned.at(size++) = false;
  }
  template <class T> void native(const T &value) {
    static_assert(sizeof(T) <= 16 && std::is_trivially_copyable_v<T>);
    std::memcpy(scalar.at(size).data(), &value, sizeof(value));
    fields.at(size) = ByteView{scalar.at(size).data(), sizeof(value)};
    owned.at(size++) = true;
  }
  void ref(const RawEventRef &value) {
    native(value.lane);
    native(value.ordinal);
  }
  void optional(const std::optional<RawEventRef> &value) {
    native(value.has_value());
    if (value)
      ref(*value);
  }
  std::span<const ByteView> view() const noexcept {
    return std::span{fields}.first(size);
  }
};

enum class RawBeadsKind : std::uint8_t {
  run,
  argv,
  invoke,
  path,
  check,
  collect,
  observer,
  parse
};
class RawBeadsTrace {
public:
  RawBeadsTrace(const RawAdapterContext *, RawBeadsKind, const RawBeadsFields &);
  ~RawBeadsTrace();
  RawBeadsTrace(const RawBeadsTrace &) = delete;
  RawBeadsTrace &operator=(const RawBeadsTrace &) = delete;
  bool enabled() const noexcept { return emission_.enabled(); }
  RawEventRef reference() const noexcept { return emission_.reference(); }
  const RawAdapterContext *context() const noexcept {
    return enabled() ? &context_ : nullptr;
  }
  void finish(RawOperationBranch, const Error * = nullptr) noexcept;
  template <class F> decltype(auto) run(F &&body) {
    return raw_call_run(*this, std::forward<F>(body));
  }
  std::optional<RawCapacityClaim> data(std::uint64_t);
  std::optional<RawControlToken> control(std::uint64_t);
  RawEventRef body(std::uint32_t, const RawBeadsFields &, RawCapacityClaim &,
                   RawPartition = RawPartition::control);
  void assignment(bool &, bool, std::uint8_t, std::optional<RawEventRef> = {});
  void field(std::uint8_t, std::string_view, std::optional<RawCapacityClaim> &);
  std::string status(std::string_view, std::uint8_t);
  void caught(const Error &, bool spawned);
  void cache(bool update, std::optional<RawEventRef>, const std::size_t *count,
             std::optional<RawControlToken> &bank);
  std::uint8_t stage = 0;
  bool mutation = false, bound = false, write = false, spawned = false;
  int exit = -1;
  std::size_t argc = 0, output = 0, stderr_bytes = 0;
  bool count_present = false, result_present = false, result_value = false;
  std::optional<bool> boolean;
  std::optional<RawEventRef> status_choice, file_owner;
  std::optional<std::error_code> path_error;
  std::optional<Error> result_error;

private:
  RawThreadHandle issuer_;
  RawEmission emission_;
  std::optional<RawCapacityClaim> status_;
  RawAdapterContext context_{};
  RawBeadsKind kind_;
};
template <class F>
decltype(auto) raw_beads_check(RawBeadsTrace &owner, std::uint8_t kind,
                               std::optional<RawEventRef> subject, F &&effect) {
  RawBeadsFields fields;
  fields.native(kind);
  fields.optional(subject);
  RawBeadsTrace check{owner.context(), RawBeadsKind::check, fields};
  return check.run([&]() -> decltype(auto) {
    if constexpr (std::is_same_v<std::invoke_result_t<F>, bool>) {
      const bool value = std::forward<F>(effect)();
      check.boolean = value;
      return value;
    } else
      return std::forward<F>(effect)();
  });
}
class RawBeadsArgv {
public:
  RawBeadsArgv(RawBeadsTrace &, std::uint8_t, bool);
  template <class F> decltype(auto) build(std::size_t count, F &&effect) {
    return trace_.run([&]() -> decltype(auto) {
      trace_.stage = 1;
      values.reserve(count);
      trace_.stage = 2;
      return std::forward<F>(effect)();
    });
  }
  template <class F> void push(std::size_t bound, F &&acquire) {
    auto bank = trace_.data(56 + static_cast<std::uint64_t>(bound));
    auto value = std::forward<F>(acquire)();
    if (values.size() >= values.capacity())
      throw Error{ErrorCode::capacity};
    values.push_back(std::move(value));
    trace_.argc = values.size();
    if (bank) {
      const auto index = values.size() - 1;
      const auto length = values.back().size();
      RawBeadsFields fields;
      fields.ref(trace_.reference());
      fields.native(index);
      fields.native(length);
      fields.bytes(detail::raw_string_field(values.back()));
      (void)trace_.body(3511, fields, *bank, RawPartition::data);
    }
  }
  void complete() noexcept { trace_.stage = 3; }
  RawEventRef reference() const noexcept { return trace_.reference(); }
  std::vector<std::string> values;

private:
  RawBeadsTrace trace_;
};
std::string_view raw_beads_description() noexcept;
} // namespace blackbird
