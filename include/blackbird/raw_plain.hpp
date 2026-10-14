#pragma once
#include "blackbird/raw_session.hpp"
#include <array>
#include <string>
#include <string_view>
#include <unistd.h>
namespace blackbird {
enum class PlainOutputKind : std::uint8_t {
  emit = 1,
  status = 2,
  outcome = 3,
  greeting = 4,
  prompt = 5,
  newline = 6,
  warning = 7
};
// The owner supplies only actual native pieces. This never joins/formats them.
void write_plain_native(int descriptor, std::span<const std::string_view> pieces,
                        std::span<const ByteView> complete_request,
                        const RawAdapterContext *context,
                        RawCapacityClaim *preheld_data = nullptr,
                        RawControlToken *preheld_terminal = nullptr);
template <std::size_t N>
void write_plain_output(int descriptor, PlainOutputKind kind,
                        const std::array<std::string_view, N> &pieces,
                        const RawAdapterContext *context = nullptr,
                        RawCapacityClaim *preheld_data = nullptr,
                        RawControlToken *preheld_terminal = nullptr) {
  const auto parent = context && context->parent ? *context->parent : RawEventRef{};
  std::size_t size = 0;
  for (const auto piece : pieces)
    size = static_cast<std::size_t>(raw_session_bound(size, piece.size()));
  std::array<ByteView, N + 5> fields{};
  fields[0] = parent.lane;
  fields[1] = detail::raw_native_field(parent.ordinal);
  fields[2] = detail::raw_native_field(descriptor);
  fields[3] = detail::raw_native_field(kind);
  fields[4] = detail::raw_native_field(size);
  for (std::size_t index = 0; index < N; ++index)
    fields[index + 5] = detail::raw_string_field(pieces[index]);
  write_plain_native(descriptor, pieces, fields, context, preheld_data,
                     preheld_terminal);
}
// Calls the SAME existing business safe formatter after preserving input and
// preholding its actual 4*N bound. F is an ordinary synchronous business call.
template <class F>
void write_plain_formatted(int descriptor, PlainOutputKind kind, std::string_view text,
                           F &&format, const RawAdapterContext *context,
                           std::string_view prefix = {}, std::string_view suffix = {}) {
  const auto size = text.size();
  RawSessionCall call{4345, 4346, context,
                      std::array{detail::raw_native_field(descriptor),
                                 detail::raw_native_field(kind),
                                 detail::raw_native_field(size)}};
  call.run([&] {
    auto input = call.bytes_credit(size);
    (void)call.bytes(4344, text, input);
    const auto bound = raw_session_bound(
        53, raw_session_bound(raw_session_product(size, 4),
                              raw_session_bound(prefix.size(), suffix.size())));
    std::optional<RawCapacityClaim> data;
    if (call.enabled()) {
      auto granted = raw_claim_data(bound);
      if (!granted.has_value())
        throw granted.error();
      data.emplace(std::move(granted).value());
    }
    auto terminal = call.control(52);
    auto formatted = std::forward<F>(format)(text);
    const auto output_context = call.context();
    write_plain_output(
        descriptor, kind, std::array<std::string_view, 3>{prefix, formatted, suffix},
        &output_context, data ? &*data : nullptr, terminal ? &*terminal : nullptr);
  });
}
std::string_view raw_plain_description() noexcept;
} // namespace blackbird
