# Instrument adapter private sanitizer qualification

2026-10-16. The final-source signal preceded both builds. Each private binary
compiles the actual adapter test, instrument adapter, native action bridge,
C++ Lua owner and collection-inspector component (five translation units).
Both builds succeeded with the pinned Clang23.1.2 arm64/SDK26.2 C++20 Debug
profiles. APFS-cloned qualified coding/colleague/foundation archives and the
pinned Lua5.4.8 static archive supply the remaining closure.

| Profile | Result |
| --- | --- |
| ASan+UBSan | Six cases passed once: semantic/source/stale/mapping/recovery/raw, 2.44s total; no sanitizer diagnostic. Build 13.24s. |
| TSan | Five cases passed; raw exited1 at the exact last-good scene/source assertion. No race report or timeout. Six cases 6.02s total; build 11.45s. |

Every case retains its20-second watchdog. ASan uses
`-fsanitize=address,undefined -fno-sanitize-recover=all`,
`ASAN_OPTIONS=halt_on_error=1`, and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. TSan uses
`-fsanitize=thread` and `TSAN_OPTIONS=halt_on_error=1:exitcode=66`.
No failure was suppressed or relabeled clean. The targeted rerun after the
fixture correction is recorded separately below.

The TSan raw log preserves the native directory
`/tmp/blackbird-instrument-miLX3h`. The failing assertion compares the exact
pre-source Scene shared pointer and effective generation after five refreshes
under physically exhausted DATA or CONTROL credit. Its combined message does
not identify which partition or comparison failed. The setup permits an older
compose to remain in flight after preview/recompose readiness; that is a
scheduling hypothesis, not an established cause. The root was notified before
any repeat or source edit. This qualification therefore does not claim a clean
TSan adapter suite.

Exact commands, build/per-case logs, timings, binary hashes and before/after
source/header/dependency identities are retained under ignored
`context/tui-lua-composition-2026-10-16/adapter-profiles/{asan,tsan}/`.
All eight overlapping compiled source/header identities match the root's final
`adapter-source-profile.json`, and both profiles' complete recorded identities
remained unchanged through their checks. The adapter source hash is
`5bda0393554a88cb1862a6e8e655ee2366d8c835b13d5499db82c422c901759d`;
the test hash is
`7feca3ff61ba33fcda3e1f0c6c15d220506fd185b07f07befaa216d092494fb0`.

The raw case uses actual DATA and CONTROL pressure and a real RawRunReader.
This private qualification does not instrument current main/terminal routing,
claim actual-main/PTY/Microsoft coverage, rebuild the Lua C archive, or rebuild
external printf/native job-runner dependencies. Unchanged terminal_lines and
native control primitives come from the qualified instrumented archive closure.
Those integration checks remain separately owned by the root.

No shared Ninja/build directory, product/test source, index or Beads writes
were used. All private builds/checks stopped. Exact anchored executable/child
checks found no remaining owned fixture children; the evidence is retained in
`adapter-profiles/private-resource-check.json`.

## Corrected physical-pressure fixture

The root identified an unsupported fixture premise: a live RawRuntime recorder
can acknowledge/refill capacity between the exhaustion check and adoption.
The revised raw case keeps real-runtime original/reference decoding and tests
physical pressure separately with a bound RawProducer/RawChannel having no
consumer. Worker owners are prepared under the real runtime before binding the
UI producer. Each attempt now proves the selected partition remains exhausted,
the exact Scene/source remains unchanged, and CONTROL refusal does not advance
the DATA cursor; releasing credits must permit actual adoption. This removes
the unstable physical precondition without extending a watchdog or relaxing the
presentation assertions. It does not establish a product race as the cause of
the original failure.

Only the changed raw case was rerun under each profile. ASan+UBSan passed in
1.74s; TSan passed in3.82s, without sanitizer diagnostics or timeouts. The prior
five unchanged cases retain their original successful results. Product source,
headers and native dependencies are byte-identical to the initial profiles;
only the test changed, to SHA256
`e28254972489c9db94ce6401607ab759875adcbe9929e01cb6377df679a21135`.

Initial binaries and hashes remain preserved as `tui_instrument_test.initial`
and `initial-binary-preserved.json` in each profile directory. The original
single clang invocation left no object files, so the authorized private rebuild
compiled the unchanged product closure once into retained objects before
linking the corrected test. Exact commands, per-TU build logs, raw results,
binary hashes and unchanged before/after identities are under each profile's
`raw-fixture-correction/`. No other case was repeated. The root's initial
overlong shared-memory-name setup failure and its shortened-name correction
remain in the separately owned fixture evidence; they were not sanitizer
failures in this lane.

Both corrected profile processes stopped. Exact original/corrected executable
child checks found no remaining owned children; evidence is in
`adapter-profiles/corrected-private-resource-check.json`. The original TSan
failure and its native raw directory remain preserved.
