#include "blackbird/context.hpp"
#include "blackbird/json.hpp"
#include "blackbird/local_timing.hpp"
#include "blackbird/raw_runtime.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace blackbird;
template <class T> T id(unsigned char n) {
  IdentityBytes b{};
  b[0] = std::byte{n};
  return unwrap(T::from_bytes(b));
}
// Standalone benchmark measurements, separate from emitted raw metric records.
struct ProbeTick {
  std::int64_t wall, cpu;
};
ProbeTick probe_tick() {
  const auto reading = raw_local_clock(true);
  if (reading.monotonic.result != 0 || reading.thread_cpu.result != 0)
    throw std::runtime_error("benchmark clock failed");
  const auto scalar = [](const timespec &value) {
    return static_cast<std::int64_t>(value.tv_sec) * 1000000000LL + value.tv_nsec;
  };
  return {scalar(reading.monotonic.value), scalar(reading.thread_cpu.value)};
}
int main(int argc, char **argv) {
  if (argc != 5)
    return 2;
  const auto history = std::stoul(argv[1]);
  const auto samples = std::stoul(argv[2]);
  const bool enabled = std::string_view(argv[3]) == "on";
  const std::string prefix = argv[4];
  char temp[] = "/tmp/p1-history-XXXXXX";
  if (!mkdtemp(temp))
    return 3;
  try {
    JournalHeader h{id<EnvironmentId>(1),
                    id<AuditStreamId>(2),
                    3,
                    {1024 * 1024, 4 * 1024 * 1024},
                    std::nullopt};
    std::unique_ptr<RawRuntime> recorder;
    std::optional<RawThreadBinding> binding;
    auto root =
        unwrap(RetainedState::create(std::make_unique<NativeJournalDirectory>(
                                         unwrap(NativeJournalDirectory::open(temp))),
                                     "audit", h, {256 * 1024 * 1024, 100000}));
    AuditLog log{*root};
    ContextStore context{log};
    Value::Array items;
    const auto context_entries = 64 + history;
    for (unsigned long i = 0; i < context_entries; ++i)
      items.push_back(Value::object(
          {{"role", Value{"user"}}, {"content", Value{std::string(128, 'x')}}}));
    context.append(std::move(items), "fixture");
    const auto packet = Value::object({{"fixture", Value{std::string(128, 'x')}}});
    for (unsigned i = 0; i < history; ++i)
      log.record(ApplicationChannel::log, packet);
    if (enabled) {
      recorder = unwrap(RawRuntime::open(temp, h.environment,
                                         {{1024 * 1024, 64 * 1024, 4096}, 4096}));
      binding.emplace(unwrap(recorder->prepare_thread()));
    }
    std::ofstream output(prefix + ".samples.jsonl");
    if (!output)
      throw std::runtime_error("sample output open failed");
    const auto valid = [](ProbeTick a, ProbeTick b) {
      if (b.wall < a.wall || b.cpu < a.cpu)
        throw std::runtime_error("invalid benchmark clocks");
    };
    const auto initial = root->journal_usage();
    for (unsigned i = 0; i < samples; ++i) {
      const auto before = root->journal_usage();
      const auto begin = probe_tick();
      log.record(ApplicationChannel::log, packet);
      const auto end = probe_tick();
      valid(begin, end);
      const auto after = root->journal_usage();
      output << "{\"action\":\"append\",\"history\":" << history
             << ",\"records\":" << before.indexed_records
             << ",\"record_delta\":" << after.indexed_records - before.indexed_records
             << ",\"journal_bytes\":" << before.prefix.end_offset
             << ",\"byte_delta\":" << after.prefix.end_offset - before.prefix.end_offset
             << ",\"wall_ns\":" << end.wall - begin.wall
             << ",\"cpu_ns\":" << end.cpu - begin.cpu << "}\n";
      const auto pbegin = probe_tick();
      std::size_t bytes;
      {
        RawRegion prepare{raw_regions::provider_prepare};
        auto request = Value::object(
            {{"model", Value{"fixture"}}, {"input", Value{context.items()}}});
        bytes = unwrap(dump_json(request)).size();
        prepare.observe({RawObservationKind::request_bytes, after.indexed_records,
                         bytes, after.prefix.sequence});
        const auto &revision = context.head();
        prepare.linkage(
            {std::as_bytes(std::span{revision.data(), revision.size()}), {}});
        prepare.outcome(RawRegionOutcome::success);
      }
      const auto pend = probe_tick();
      valid(pbegin, pend);
      output << "{\"action\":\"prepare_subset\",\"history\":" << history
             << ",\"records\":" << after.indexed_records
             << ",\"context_entries\":" << context_entries
             << ",\"request_bytes\":" << bytes
             << ",\"wall_ns\":" << pend.wall - pbegin.wall
             << ",\"cpu_ns\":" << pend.cpu - pbegin.cpu << "}\n";
    }
    // Explicit primitive overhead, same clocks and iterations in both modes.
    const auto b = probe_tick();
    for (unsigned i = 0; i < 2000; ++i) {
      RawRegion span{raw_regions::provider_prepare};
      span.outcome(RawRegionOutcome::success);
    }
    const auto e = probe_tick();
    valid(b, e);
    output << "{\"action\":\"primitive_2000\",\"history\":" << history
           << ",\"wall_ns\":" << e.wall - b.wall << ",\"cpu_ns\":" << e.cpu - b.cpu
           << ",\"initial_records\":" << initial.indexed_records << "}\n";
    output.close();
    if (!output)
      throw std::runtime_error("sample output write failed");
    root.reset();
    binding.reset();
    recorder.reset();
    if (enabled)
      std::filesystem::rename(std::filesystem::path{temp} / "raw", prefix + ".raw");
    std::filesystem::remove_all(temp);
    return 0;
  } catch (...) {
    std::filesystem::remove_all(temp);
    return 5;
  }
}
