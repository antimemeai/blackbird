# C++26 upgrade on the pinned LLVM 23 profiles

2026-10-17. The operator asked to upgrade Blackbird to C++26, test it hard, and
have autonomous Blackbird runs do real work on the result. The plan is
[docs/CPP26_SUBPLAN.md](../docs/CPP26_SUBPLAN.md); its single review round is in
[cpp26-toolchain-2026-10-17/](cpp26-toolchain-2026-10-17/). The revert point is
tag `pre-cpp26-2026-10-17` at master `f4bd931`.

## What changed

Only the language mode changed. The compilers, standard libraries and profile
names are the same as before.

| Host | Compiler | Standard library | Language |
| --- | --- | --- | --- |
| Mac arm64 (`macos-llvm23`) | LLVM Clang 23.1.2 | Homebrew libc++ 23 headers, system `libc++.1.dylib` | C++26 |
| Neuroses, Ubuntu 24.04 x86_64 (`linux-llvm23`) | LLVM Clang 23.1.4 | libc++/libc++abi 23.1.4 | C++26 |

This is the toolchain verbs pins: verbs `design/SPEC.md` §8 at rev `0483549`
specifies Homebrew LLVM 23.1.2 with `-std=c++2c`.

Clang 23 support for C++26 is partial. It has no P2996 reflection and no P2900
contracts, and it reports `__cplusplus == 202400L`. Mac and Neuroses report the
same values for the features checked:

- `__cpp_constexpr 202406L`
- `__cpp_lib_saturation_arithmetic 202603L`
- `__cpp_lib_span_at 202311L`
- `__cpp_lib_constant_wrapper 202606L`

libc++ 23 names saturation arithmetic `std::saturating_add`, `saturating_sub`,
`saturating_mul`, `saturating_div` and `saturating_cast`; the older draft names
`add_sat` and `saturate_cast` are absent. Compiling at `-std=c++26` adds only the
`-std` flag to the driver command (checked with `clang++ -###`), and
`_LIBCPP_ABI_VERSION` stays 1.

## Commits

**`fd19b5d`** sets `CMAKE_CXX_STANDARD 26`. The two clang-tidy diagnostic probes
in `scripts/rigor` and the libc++ version query in `scripts/check-linux` now pass
`-std=c++26`. `tooling/probes/compiler_contract.cpp` now requires:

- `__cplusplus > 202302L`;
- `constexpr std::saturating_add`, `saturating_sub` and `saturating_cast` at their
  exact boundary values;
- `std::span::at`, which must throw `std::out_of_range` when out of range.

At C++23 the contract fails to compile ("requirement '202302L > 202302L'", "no
template named 'saturating_add'"). At C++26 it compiles and passes.

**Commit `48b9469`** adds checks that three autonomous review runs (see
below) independently showed were missing:

- The contract rejects GNU dialect with `#error` unless `__STRICT_ANSI__` is
  defined, and rejects other standard libraries unless `_LIBCPP_VERSION` is
  defined.
- `rigor` lint, through `require_standard`, rejects any compile command whose
  standard flags are not exactly `["-std=c++26"]`. This covers every translation
  unit, so a standard override applied to one target cannot slip past.

Both checks were exercised:

- `-std=gnu++26` produces the dialect `#error`.
- `g++-16` with libstdc++ produces the libc++ `#error`.
- `-std=c++23` produces the static assertion.
- All 257 real compile commands are accepted.
- The C++23, GNU++26, missing-standard and duplicated-standard mutants are all
  rejected.
- On Neuroses, `compiler_contract` passes 3/3 profiles with the hardened source.

## Observed verification

| Profile | Result |
| --- | --- |
| Neuroses Debug / Release / ASan+UBSan (`-g -O1`) | 263/263 each on the first attempt, 24.4 min total |
| Mac Debug + owned C++/Lua lint (installed hook, `fd19b5d`) | 263/263, CTest 399 s |
| Mac Debug + lint with the standard check (installed hook, `48b9469`, machine quiet) | 263/263; lint rejects none of the 257 compile commands |
| Mac Release | 262/263; `tui_retained_result` hit its 60 s watchdog under heavy load and passes alone at the unchanged deadline (25.2 s) |
| Mac ASan+UBSan | 262/263; `command_jobs_pty` failed under heavy load and passes alone (43.1 s) |
| Mac profile | 263/263 |
| Mac TSan | see below |
| Mac fuzz | The full profile builds and `bytes_fuzz -runs=1000 -max_len=4096 -seed=1` passes when Apple's `ld` is used (see "Pre-existing findings") |
| Sanitizer fault probes | ASan heap overrun, UBSan signed overflow and TSan data race are all detected |
| Diagnostic probes at `-std=c++26` | clang-format violation, `bugprone-unchecked-optional-access`, `clang-analyzer-core.NullDereference` and the LuaLS type error are all detected |

**TSan.** The first full run went to 235/263, with 23 watchdog timeouts and 5 plain
failures. It ran alongside autonomous agent lanes at load averages of 130 to 390,
and it reported zero `WARNING: ThreadSanitizer`.

Each of the 30 Mac failures (the 28 TSan ones plus Release `tui_retained_result`
and ASan `command_jobs_pty`) was then run paired: the C++26 worktree and an
untouched C++23 build at `f4bd931`, same host, profile and deadline, alternating
which went first.

- **No case failed on C++26 and passed on C++23.**
- **20 cases pass on both.**
- **Nine TSan cases fail identically on both** (`arconaut-0zi`): `beads_raw`,
  `command_jobs_pty`, `command_raw`, `operation_raw`, `provider_raw`,
  `raw_command_wake`, `raw_file`, `raw_retained_caller`, `storage_raw`. These are
  pre-existing and are not repaired here.
- **Two cases sit on the deadline edge:** `participants_raw` and `raw_metadata`
  passed on C++26 and timed out on C++23, each within a second of the watchdog.

The Mac TSan profile is therefore **not qualified**, and that is pre-existing. The
full TSan suite had no recorded run since 2026-10-01.

Every log is in the worktree's ignored `context/cpp26-upgrade/`, including
`ab-summary.txt`, `ab/`, `task2-summary.log` and the hook logs.

## Pre-existing findings filed (none caused by C++26)

| Bead | Finding |
| --- | --- |
| `arconaut-3nx` (P1) | A stale raw-capture failure marker left by a timed-out provider attempt kills the successful retry; it surfaces as `interrupted`/`ETIMEDOUT`. |
| `arconaut-pyg` (P2) | Provider requests have a fixed 180 s total cap even while streaming, and the identical request is retried. Replayed without the cap, the same requests took 412 s (Paper max), 117 s (Paper high) and 435 s (MiMo), with no gap between streamed chunks longer than 1.12 s. |
| `arconaut-3o2` (P2) | Unbounded command output: one agent turn produced 9.5 GB of lossless raw capture, then the capacity refusal aborted the turn. |
| `arconaut-e5g` (P2) | The Mac profile does not pin the linker. Anaconda's ld64-711 on `PATH` crashes fuzz-profile links; the crash is identical on C++23. |
| `arconaut-j1l` (P2) | When a CTest case times out, its process tree (tui-test daemons, Blackbird children, forked helpers) keeps running. |
| `arconaut-0zi` (P2) | The nine Mac TSan cases above. |
| `arconaut-5st` (P2) | Provider settings need enumerating per provider and per plan from the vendor docs. Paper's documented default is `max`; MiMo maps all efforts to on or off; ultraspeed is unavailable on the Token Plan. |
| `arconaut-vv6` (P3) | `CodingProvider` relies on a C++11-deprecated implicit copy constructor. It is not enabled by the project's flags. |

## Autonomous Blackbird campaign

The C++26 Release and ASan+UBSan builds ran about 40 autonomous `--once` agent
sessions across three providers:

- OpenAI `gpt-6-luna`;
- Paper `ultramarine-1-flash`;
- MiMo `mimo-v2.6-pro`, through a registered Token Plan Responses route named
  `mimotp`.

The tasks ranged from fizzbuzz to a from-scratch bigint and a regex engine, plus a
C++26 deprecation audit and a review of this change. Each run was confined with
`sandbox-exec` after the first round. All of its traces (the session audit, raw
instrumentation, diagnostics, stdout/stderr and the derived reader views) are
archived under `~/projects/blackbird_runs/2026-10-17-cpp26/`. That directory also
holds an independent `VERIFICATION.md` and the `REPORT.md` of runner incidents.

**Results**

- Every result from a completed run is correct against an independent oracle.
- The three providers' audits of the C++26 switch each found zero
  C++26-attributable diagnostics. Paper diffed the diagnostics of all 249
  translation units at C++23 against C++26 and found them identical.
- No run on the ASan+UBSan Blackbird binary produced a sanitizer report.

**Failures.** Six hard-task runs failed only because of `arconaut-pyg` and
`arconaut-3nx`; Paper reruns at `high` recovered two of them. Before confinement,
one MiMo run probed `sudo` and edited a global Homebrew clang config file. That
file is restored byte for byte.

## Not covered

- Mutation testing, which runs on the fleet and is deferred.
- Linux TSan and fuzz, which `check-linux` has never covered.
- Linux lint, which matches the existing tooling scope.
- Matched performance comparison, which the plan review removed.
- No product code adopts C++26 facilities yet.
