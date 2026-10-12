#pragma once
#include "blackbird/provider_auth.hpp"
#include "blackbird/value.hpp"
#include <functional>

namespace blackbird {
// One bounded, context-only request. No implicit context, continuation or retries.
// Capture must retain bytes or throw; an admission capture failure prevents dispatch.
using ColleagueCapture = std::function<void(std::string_view, std::string_view)>;
using ColleagueTransport = std::function<Value(const Value &, const ColleagueCapture &,
                                               const RawAdapterContext *)>;
Value prepare_colleague(const Value &request,
                        const RawAdapterContext *context = nullptr);
// Received usage/model metadata survives reported failure or refusal. actual_model
// is observed string/null; model_metadata_status distinguishes observed,
// unavailable, ambiguous and malformed. Transport failures without a decoded
// response have unavailable metadata. Originals retain exact reported fields.
Value call_colleague(const Value &request, const ColleagueCapture &capture,
                     const ColleagueTransport &transport,
                     const RawAdapterContext *context = nullptr);
Value call_prepared_colleague(
    const Value &prepared, const ColleagueCapture &capture,
    const ColleagueTransport &transport, const RawAdapterContext *context = nullptr,
    std::optional<ProviderOriginalReference> admission = std::nullopt);
Value native_colleague_transport(const Value &prepared, const ColleagueCapture &capture,
                                 const std::function<bool()> &cancelled = {},
                                 const ProviderAuthConfig *owned_config = nullptr,
                                 const RawAdapterContext *context = nullptr);
Value colleague_catalog();
} // namespace blackbird
