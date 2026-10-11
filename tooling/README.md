# Development rigor

The local macOS 26.6 arm64 tool profile is operational. This qualifies the
development executables, build wiring and diagnostic gates, not the future
agent's correctness. The first-core specification and reviewed implementation
plan still govern product work. Product foundation cases now accompany the
capability probes; their own review and acceptance remain separate.
The written unit is [RIGOR_SETUP_SUBPLAN](../docs/RIGOR_SETUP_SUBPLAN.md).

## Daily commands

Run from the repository root:

```sh
scripts/rigor doctor
scripts/install-hooks
scripts/rigor check debug
scripts/rigor lint
scripts/rigor check release
scripts/rigor check asan
scripts/rigor check tsan
scripts/rigor check fuzz
scripts/check-linux
# Tool capability qualification, including deliberately failing diagnostic probes:
scripts/rigor qualify
```

`check` verifies tool identities, configures/builds the selected profile, runs
its direct CTest fixture (or the bounded fuzzer), and analyzes actual debug
translation units plus owned C++/Lua sources. Native analysis uses six workers and
CTest uses six; deadline-sensitive real UI/CLI fixtures run alone so bulk native
storage campaigns do not contend with their filesystem/process observations.
Small development commands have a 120-second limit. Full clang-tidy source
analysis runs to completion, rather than truncating large translation units.
CTest owns each
test's watchdog, using its existing TIMEOUT or a 120-second default; the complete
suite does not share one test's allowance. No fixture assertion or existing
deadline is relaxed. A failed command fails the gate. A missing tool, report,
translation unit, header analysis
path, mismatched compiler or preset/profile pin is an error. Owned directory
symlinks are rejected until an explicit source policy is designed; research,
captures and generated build roots are excluded from owned source enumeration.

`build PROFILE` performs only tool verification and configure/build. It does not
run checks. Builds and compile databases live in ignored `build/PROFILE/`.
When replacing a compiler in an existing CMake cache, use the pinned CMake with
`--fresh --preset PROFILE`, then rerun the owning qualification. Do not silently
reuse an old compiler cache or suppress failed instrumentation.

## Qualified local tools and profiles

Exact paths and versions are in [local-profile.json](local-profile.json), with
matching compiler/build-driver pins in [CMakePresets.json](../CMakePresets.json).
No global shell or editor configuration is required.

| Tool | Local version | Purpose |
| --- | --- | --- |
| LLVM Clang, clang-format, clang-tidy, clangd | 23.1.2 | Coherent C++20 compiler, formatting, focused analysis and editor diagnostics |
| CMake / CTest | 4.3.0 | Owned target/test orchestration |
| Ninja | 1.13.0.git.kitware.jobserver-pipe-1 | Existing pinned build driver |
| Lua / luac | 5.4.8 | Compatibility execution and syntax checks |
| Lua Language Server | 3.19.1 | Annotation/type diagnostics with fresh JSON reports |
| StyLua | 2.5.2 | Lua 5.4 formatting |

Debug and optimized profiles pass a C++20 byte/span and synchronized-thread
fixture with exact expected results. The ASan+UBSan profile separately detects a
heap overrun and signed overflow. TSan detects the deliberately unsynchronized
counter in its own profile. The libFuzzer profile executes 1,000 bounded inputs
against a double-reversal byte property. These establish tool capability; none
is an oracle for future admission, persistence, IPC, CLM or refit behavior.

Formatting, an unchecked optional and a null-dereference analyzer case produce
their required diagnostic classes. Lua checks parse every owned script, validate
formatting, reject reported diagnostics from a newly created LuaLS report, and
run exact byte/coroutine assertions. A deliberate annotation mismatch demonstrates
the actual report path. The pinned LuaLS encodes an empty report as `[]`; that
specific empty case is accepted, not arbitrary report shapes.

Headers must be dependencies of real analyzed translation units; creating an
unattached header cannot quietly escape analysis. clang-tidy includes non-system
headers in any owned source directory. Its focused checks do not claim full
static verification. [scripts/clangd](../scripts/clangd) selects the same LLVM
and the repository's debug compilation database; an actual compiler-probe check
completed without diagnostics. `.clangd` and `.luarc.json` provide local editor
settings without changing the operator's editor.

Standalone negative analysis cases locate the installed Apple SDK through
`xcrun --show-sdk-path`; that path is printed in qualification output. This is an
actual-host SDK locator, not a pinned portable SDK distribution. CMake supplies
the actual SDK to native targets. Host-specific paths need deliberate qualification
on another machine.

LLVM 21.1.0's ASan deadlocked during initialization on this host and was rejected.
The process sample remains in `context/asan-probe-sample.txt`. LLVM 23.1.2 and its
required Z3 update were installed through Homebrew, preserving the older keg and
disabling unrelated automatic dependent upgrades. LuaLS and StyLua were installed
as development executables. No production framework or library was adopted.

Full qualification output is retained in
`context/rigor-qualification-final.log`, with its last successful tool summary in
`context/rigor/latest-qualification.json`. The summary is historical evidence of
that run; it is not an acceptance cache for changed code. Later review corrections
and direct fault cases are documented in the [review resolution](../papers/2026-10-01-rigor-review-resolution.md).

## Independent and advisory colleagues

[Kimi](KIMI.md) has a direct subscription-backed CLI wrapper and an installed
owned `kimi-colleague` skill. New reviews receive concrete source/specification
scope without Codex's conversation; explicit session IDs support rereview. Review
mode permits Read/Grep/Glob only. Kimi can challenge tests and propose direct
expected outcomes; the owner executes them. Separate ordinary Kimi sessions can
perform implementation when the operator assigns it.

[Jev](JEV.md) uses the existing owned workspace client for bounded typed
judgments. Offline preparation is the default; explicit live calls retain their
exact request, raw probabilities and actual model. One authorized live setup
case succeeded. Its judgments suggest investigations and never substitute for
an independent behavioral oracle or authorize execution.

## Remaining qualifications

`scripts/rigor fleet` fails explicitly because no fleet host/launcher is configured.
The operator selected Neuroses over Tailscale for ordinary Linux checks, available
through `scripts/check-linux`. It archives the current native source scope into
a temporary remote directory, runs debug/release/ASan+UBSan cases and retains logs
under `context/linux/`.
Use `--test-regex REGEX` to exercise only changed CTest cases in all three profiles;
an empty selection fails instead of passing silently. Native process-crash suites
have a 60-second watchdog for host scheduling/storage delays; their oracles assert
bytes, signals and effects, not a production latency guarantee. The remote check
has a separate finite 600-second overall watchdog.

Its explicit profile is Ubuntu 24.04 x86_64, Clang 18.1.3, libstdc++ 13,
CMake 3.28.3 and Ninja 1.11.1. The first native foundation cases pass.
This is behavioral execution, not qualification of every Linux diagnostic tool.
**Mutants run on the fleet, never this laptop.** Operator steering postpones mutation
campaigns until a working agent and mature suite; Runpod provisioning is not a gate.
No pod or mutation campaign was created. The operator owns future node termination.

The Lua lane is **5.4 compatibility only**. Lua 5.5 production selection,
embedding, allocation/errors/yield semantics and C++ lifetime boundaries require
their own owning-unit qualification. No Lua library is linked into a production
target by this setup. U0 C++ foundation exists; the coding conversation is still
downstream in the reviewed plan.

The newly public [EDG source study](../papers/2026-10-01-edg-compiler-study.md)
proposes an independent language-checking lane. Its source is acquired and read,
but no EDG build, test suite or integration has been executed. It remains a
candidate; it does not replace native sanitizers or qualify native reload.

The Linux runner builds the selected Lua5.4.8 from the digest-pinned source archive
listed in QUARANTINE.md inside each disposable remote workspace. It changes no
server system package. Current session/RRC/coding/recovery/terminal checks passed
in Neuroses debug/release/ASan+UBSan profiles on 2026-10-03.

## Native performance

`../scripts/profile build` creates optimized symbols/frame-pointer binaries and
macOS dSYM bundles. CPU, allocation and system captures use installed Instruments;
readable sample stacks and Linux perf are also wired. See [Profiling](../docs/PROFILING.md).
No profiler library is linked into the normal runtime.


## Required pre-commit checks

Run `scripts/install-hooks` after cloning to configure this repository's tracked
`.githooks/pre-commit`. It is installed in the current checkout. By default it runs
`scripts/rigor check debug`: pinned tool verification, build, all debug tests,
C++ formatting/static analysis, Lua formatting/syntax/type analysis and the Lua
contract probe. Any failure blocks the commit. No acceptance cache or hook exception
switch exists. Stage or restore tracked working changes first: the hook refuses
unstaged tracked changes and verifies the index/tree did not change during checks.
It leaves files and the index intact when a check fails.

`scripts/rigor lint` runs the same lint gates after verifying tools and rebuilding
the debug compilation database, without running CTest. Source changes retain
this gate; do not weaken rules or add suppression annotations to land them.

Operator2026-10-10 explicitly exempts text-only changes from full debug tests and
C++/Lua lint. Check the intended diff, document content and affected links, then
use `git -c core.hooksPath=/dev/null commit ...` for that text-only commit. This
per-command override leaves the installed source gate unchanged. Fast-forward
integration does not require repeating settled source checks when subsequent
changes are only documentation. Other exceptions require operator discussion.
