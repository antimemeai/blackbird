# Debug machinery is a build choice

BLACKBIRD_DEBUG defaults to OFF for normal builds and is independent of compiler
optimization. It controls developer profiling/benchmark and fault/fuzz fixture
targets. It does not disable raw metric emission, required audit or ordinary
operation timing.

Explicit debug, sanitizer/fuzz and profile presets choose ON. Release explicitly
chooses OFF, so reconfiguring an old release cache cannot preserve developer targets.
Profiler symbols/frame pointers and sanitizer modes require the debug switch;
inconsistent choices fail configuration rather than sneak into normal builds.
OS profiler collectors remain explicit development tools.

Since the approved raw instrumentation implementation on2026-10-11,
`local_timing.cpp`, `raw_clock.cpp`, `raw_channel.cpp`, `raw_audit.cpp` and
`raw_runtime.cpp` are ordinary foundation sources in both build modes. Application
sessions activate raw recording before retained-state creation/recovery and drain
on clean shutdown. Read-only audit inspection starts no capture run.

The earlier debug-only LocalSpan buffer, transformed output and
BLACKBIRD_LOCAL_TIMING file sink have been replaced. They are not runtime controls
for the raw recorder. See [instrumentation](INSTRUMENTATION.md) for the current
design, executed checks and unfinished surface coverage.
