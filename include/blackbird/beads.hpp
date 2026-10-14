#pragma once
#include "blackbird/raw_beads.hpp"
#include "blackbird/tools.hpp"
namespace blackbird {
// Lazy, process-local binding and cache; no constructor effects.
class BeadsAdapter {
public:
  std::function<bool()> cancelled;
  LocalTools::Observer observer;
  bool mutation_may_have_started() const noexcept { return mutation_possible_; }
  Value run(const Value &args, const RawAdapterContext * = nullptr);

private:
  std::string project_, executable_, actor_;
  bool bound_ = false, mutation_possible_ = false;
  Value cache_;
  std::optional<RawEventRef> bound_source_, cache_source_;
  Value invoke(RawBeadsTrace &, const RawBeadsArgv &, std::uint8_t, bool,
               std::optional<RawEventRef> &status, RawEventRef &source);
};
} // namespace blackbird
