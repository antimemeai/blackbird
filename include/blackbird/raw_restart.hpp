#pragma once
#include "blackbird/context.hpp"
#include "blackbird/raw_session.hpp"
#include <thread>
namespace blackbird {
RawSessionCall raw_restart_call(RetainedState &, std::uint8_t method,
                                const RawAdapterContext *,
                                RawCallSites sites = {4400, 4401},
                                std::uint64_t terminal_extra = 0);
template <class F>
bool raw_restart_predicate(RawSessionCall &call, std::uint8_t kind, F &&body,
                           std::uint32_t site = 4403) {
  auto credit = call.control(42);
  const bool result = std::forward<F>(body)();
  (void)call.event(
      site,
      std::array{detail::raw_native_field(kind), detail::raw_native_field(result)},
      credit);
  return result;
}
template <class F>
void raw_restart_mutation(RawSessionCall &call, std::uint8_t kind, F &&body,
                          std::uint32_t site = 4406) {
  auto credit = call.control(41);
  std::forward<F>(body)();
  (void)call.event(site, std::array{detail::raw_native_field(kind)}, credit);
}
RawEventRef raw_restart_source(RawSessionCall &, std::uint8_t kind, std::string_view,
                               std::optional<RawCapacityClaim> &);
RawEventRef raw_restart_source(RawSessionCall &, std::uint8_t kind, std::string_view);
std::string raw_restart_hex(RawSessionCall &, const IdentityBytes &);
Result<Value> raw_restart_packet(RawSessionCall &, std::uint8_t kind,
                                 const ImmutableBytes &);
void raw_restart_result(RawSessionCall &, const Result<void> &);
class RawSuccessorHandoff {
public:
  ~RawSuccessorHandoff();
  RawSuccessorHandoff();
  RawSuccessorHandoff(const RawSuccessorHandoff &) = delete;
  RawSuccessorHandoff &operator=(const RawSuccessorHandoff &) = delete;
  void select(RawSessionCall &);
  Result<void> joined() noexcept;
  void consume();
  void prepare_exec(std::size_t count);
  int execute(std::span<char *>);

private:
  Result<void> retire(std::uint8_t) noexcept;
  bool receiver() const noexcept;
  RawThreadHandle issuer_;
  std::optional<RawControlToken> bank_;
  RawEventRef selection_{};
  RawEventRef exec_{};
  pid_t process_ = 0;
  std::thread::id creator_;
  bool joined_ = false;
  pid_t receiver_process_;
  std::thread::id receiver_thread_;
};
// The view describes one complete existing NUL-terminated argument operand.
std::string raw_restart_argument(RawSessionCall &, std::size_t index, std::string_view);
std::filesystem::path raw_restart_canonical(RawSessionCall &,
                                            const std::filesystem::path &);
bool raw_restart_regular(RawSessionCall &, const std::filesystem::path &);
void raw_restart_directory(RawSessionCall &, std::string &);
std::string raw_restart_path(RawSessionCall &, const std::filesystem::path &);
std::string_view raw_restart_description() noexcept;
} // namespace blackbird
