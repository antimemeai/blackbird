# Native profiling

Profilers are development tools; normal Blackbird links none of them. The profile
preset retains Release optimization and adds debug symbols/frame pointers. On
macOS the wrapper also creates matching dSYM bundles and ad-hoc signs ONLY
profile binaries with `com.apple.security.get-task-allow`, required for allocation
attachment on this host. Release artifacts do not receive that entitlement. Build it with:

```sh
./scripts/profile doctor
./scripts/profile build
```

For a real interactive session, choose the profile executable explicitly:

```sh
BLACKBIRD_EXECUTABLE="$PWD/build/profile/blackbird" ./scripts/blackbird
# In another terminal, select that process's PID with ps/pgrep:
./scripts/profile capture cpu --pid 12345 --seconds 10
./scripts/profile capture allocations --pid 12345 --seconds 10
./scripts/profile capture system --pid 12345 --seconds 10
./scripts/profile capture sample --pid 12345 --seconds 5
```

Run one Instruments recording at a time; the wrapper enforces this with a lock.
CPU, allocation and system captures use Apple's installed Instruments templates;
`sample` provides a readable stack report. Capturing an existing PID preserves
its terminal. No model request is injected by the profiler. Open `cpu.trace` or
`allocations.trace` in Instruments, or inspect `sample.txt`. Export a trace's table
index with `xcrun xctrace export --input CAPTURE/cpu.trace --toc` to select a schema
for subsequent XPath export. Existing traces are never overwritten.

The provider-free stress fixture exercises actual native ChatView, grid
composition, sprite and damage painter. It emits no terminal bytes and performs
no file/process/provider tools. Counters cover the whole fixture lifetime, including profiler initialization and
teardown, rather than only the requested sample window. Its two cases isolate stable-history motion and
streaming history/layout. This is a component workload, not input-to-photon
latency or a normal operator duty cycle:

```sh
./scripts/profile exercise sample --scenario motion --seconds 5
./scripts/profile exercise cpu --scenario stream --seconds 10
./scripts/profile exercise allocations --scenario stream --seconds 10
```

Each finite capture goes to a fresh private directory under ignored
`context/profiles/`, containing profiler output/log, capture metadata, Git revision
and dirty-state indication. The fixture also retains its exact executable hash
and counters. A profiler failure stays failed with its error/log; it does not turn
into a synthetic successful capture. The native fixture is terminated and reaped
on exit. Attach captures never terminate the observed harness.

The shipped `profile build` command follows the local macOS compiler preset.
On Linux, configure the qualified linux-llvm23/libc++23.1.4/Lua5.4.8 build explicitly with
`BLACKBIRD_PROFILING=ON`; the wrapper does not substitute the macOS compiler path.
Linux CPU capture uses `perf record` with `cpu-clock:u`, 199Hz and frame-pointer
call graphs. `perf` must already be installed and permitted for the selected PID;
this wrapper does not modify system permissions. Inspect with
`perf report --stdio -i CAPTURE/perf.data`. Mac allocation/system templates are
not silently substituted on Linux. The native code/build also undergoes the
project's Neuroses test profile; that is separate from successful Linux profiling.

Start with actual questions: CPU self/total weight in layout versus grid reset,
comparison/copy and encoding; allocation stacks and persistent growth; off-CPU
waits for input/provider/output; unchanged-history cost on animation; terminal
backpressure. Compare the same build flags, fixture and capture conditions. Keep
CPU stress results distinct from interactive CPU use, wait latency and byte count.
Sampling does not by itself measure end-to-end screen smoothness.

Sources read: installed `xctrace record/export` and `/usr/bin/sample` usage;
[Apple performance and metrics](https://developer.apple.com/documentation/xcode/performance-and-metrics),
[Apple Instruments Time Profiler example](https://developer.apple.com/tutorials/instruments/identifying-a-hang),
and [Linux perf record source documentation](https://kernel.googlesource.com/pub/scm/linux/kernel/git/ralf/linux/+/1008ebb61e01e3152901b4c5f58bd01a60ed115b/tools/perf/Documentation/perf-record.txt).
No external profiler library was adopted.

Actual local qualification: readable symbolized sample report, serial Time Profiler
trace with exported table index, and Allocations trace after profile-only signing.
Concurrent Instruments attempts failed and led to serialization. Failed/interrupt
fault probes check retained counters/stderr, status and child reaping.
