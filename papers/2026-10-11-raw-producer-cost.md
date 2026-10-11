# Actual raw producer cost — preliminary local execution

2026-10-11, macOS ARM, LLVM23.1.2, current release foundation library, owned
harness compiled C++20/O3/NDEBUG with the actual Xcode SDK. One execution while
other scoped builds were running. This is performance measurement, not an eval
campaign or a sustained-rate/device guarantee. No primitive-probe numbers are
substituted for the actual runtime.

Each case registers a real runtime/context, emits10000 records with the separate
recorder running, then drains/retire/closes. Body cases include the actual safe
admission check, immediate reserve/copy/publication/armed exchange and any required
wake; region cases include both endpoints and native monotonic/thread-CPU clocks.
The timer uses two independent steady-clock calls per observation, with their
measurement overhead included. One initial emission touches the path; later
mapping pages can be cold. Regions can block at safe control admission when the
recorder falls behind. The maximum includes these real stalls. No outlier removal.

Producer allocation counts come from the harness's global new counter, sampled
only around the10000 emissions. Recorder allocations run in another process and
are not included. Zero here does not imply zero allocations at setup, no syscalls,
no copies or no recorder cost. Ring copy bytes include a16-byte envelope plus
body; the recorder separately copies/fragments/integrity-checks and synchronizes.
`close_ms` includes source drain, retirement, segment rotation and supervisor
shutdown, not a pure per-record preservation latency. Disk bytes include origin,
frame, commit and headers. This harness does not replace the independent byte
oracles in the source tests.

```csv
case,count,p50_ns,p99_ns,max_ns,producer_allocations,close_ms,raw_segment_bytes
64,10000,83,84,33375,0,76.2928,1440648
256,10000,83,1167,525666,0,96.5602,3361176
4096,10000,292,4167,3760791,0,354.053,41773304
region-native-clocks,10000,667,1417,14756167,0,92.1185,2911187
```

These measured medians are83ns for64/256B bodies,292ns for4096B, and667ns for a
region with both native clock endpoints. All four producer loops allocate zero.
One loaded local pass does not establish idle/loaded distributions across hosts,
sustained capacity, memory residency, fairness, syscall counts or per-record
preservation lag. Those measurements remain in the implementation plan.

The exact owned source used for this execution is retained below. It computes
measurement statistics in the development harness, never in Blackbird's producer
or metric payloads. Temporary raw runs were closed and removed.

```cpp
#include "blackbird/raw_runtime.hpp"
#include "blackbird/local_timing.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <new>
#include <vector>
#include <unistd.h>
using namespace blackbird;
std::atomic<std::uint64_t> allocations{};
void *operator new(std::size_t n) {
  allocations.fetch_add(1, std::memory_order_relaxed);
  if (auto *p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc{};
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
template<class T> T take(Result<T> r) {
  if (!r.has_value()) throw r.error();
  return std::move(r).value();
}
void take(Result<void> r) { if (!r.has_value()) throw r.error(); }
int main() {
  constexpr std::size_t count = 10000;
  std::cout << "case,count,p50_ns,p99_ns,max_ns,producer_allocations,close_ms,raw_segment_bytes\n";
  for (auto size : {std::size_t{64},std::size_t{256},std::size_t{4096},std::size_t{0}}) {
    std::array<char,64> path{};
    const std::string pattern="/tmp/blackbird-raw-cost-XXXXXX";
    std::copy(pattern.begin(),pattern.end(),path.begin());
    if (!::mkdtemp(path.data())) return 1;
    IdentityBytes env{};env[0]=std::byte{37};
    auto runtime=take(RawRuntime::open(path.data(),take(EnvironmentId::from_bytes(env))));
    std::vector<std::byte> body(size,std::byte{73});
    const std::array<ByteView,1> parts{body};
    std::vector<std::uint64_t> elapsed(count);
    auto context=take(runtime->prepare_thread());
    std::uint64_t allocated=0;
    std::chrono::steady_clock::time_point end;
    {
      RawThreadBinding binding{context};
      // Touch the actual producer mapping and clock path before timing.
      if (size) {take(raw_wait_data(16+size));take(raw_emit(9900,parts));}
      else {RawRegion region{raw_regions::provider_prepare};region.finish();}
      const auto before=allocations.load();
      for (std::size_t i=0;i<count;++i) {
        const auto start=std::chrono::steady_clock::now();
        if (size) {take(raw_wait_data(16+size));take(raw_emit(9900,parts));}
        else {RawRegion region{raw_regions::provider_prepare};region.finish();}
        const auto stop=std::chrono::steady_clock::now();
        elapsed[i]=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(stop-start).count());
      }
      allocated=allocations.load()-before;
      end=std::chrono::steady_clock::now();
    }
    // Dropping the last handle drains P through D before closing the runtime.
    // This includes retirement/rotation overhead, so it is not a pure disk latency.
    context.reset();
    runtime.reset();
    const auto close=std::chrono::steady_clock::now();
    std::uint64_t disk=0;
    for (const auto &entry:std::filesystem::recursive_directory_iterator{path.data()})
      if(entry.path().filename().string().starts_with("events.")) disk+=entry.file_size();
    std::sort(elapsed.begin(),elapsed.end());
    std::cout << (size?std::to_string(size):"region-native-clocks") << ',' << count << ','
              << elapsed[count/2] << ',' << elapsed[count*99/100] << ',' << elapsed.back()
              << ',' << allocated << ',' << std::chrono::duration<double,std::milli>(close-end).count()
              << ',' << disk << '\n';
    std::filesystem::remove_all(path.data());
  }
}
```

This execution precedes the later cursor-publication and uninitialized read-scratch
fixes. It remains the actual preliminary measurement of that earlier candidate;
no new performance numbers are inferred for the later source.
