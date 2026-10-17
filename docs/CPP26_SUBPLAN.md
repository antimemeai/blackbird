# C++26 Upgrade Plan

**Goal:** Blackbird builds and passes its native suites as C++26 on both qualified hosts,
and passes owned source analysis on Mac, using the compilers it already pins.

**What changes:** the language mode, and nothing else.
- The macos-llvm23 profile keeps Homebrew Clang 23.1.2
  (`/opt/homebrew/Cellar/llvm/23.1.2`).
- The linux-llvm23 profile on Neuroses keeps Clang 23.1.4 with libc++/libc++abi 23.1.4.
- No compiler, library, preset path or profile name changes.

This is exactly the toolchain verbs pins. Verbs `design/SPEC.md` §8 (rev `0483549`,
lines 568-571) reads: "Pin: Homebrew LLVM 23.1.2, compiled with `-std=c++2c` … blackbird's
move to C++26 is a flag change, not a compiler change."

**Spec:** the operator's 2026-10-17 request ("upgrade blackbird to C++26"), and the
operator's verbs decision that all C++ is C++26 and the move is paid now
(`../verbs/journal/2026-10-17-operator-decisions.md`, "C++26 reaffirmed after
tribunal"). The template is the [C++23 upgrade record](../papers/2026-10-17-cpp23-toolchain.md).

**Where:**
- Branch `work/cpp26-toolchain-2026-10-17`.
- Worktree `/Users/patrickbeam/projects/blackbird/context/cpp26-toolchain`, created from
  `origin/master` at `f4bd931`.
- Bead `arconaut-9xx`.
- Read doctrine from the main checkout. The worktree's `BLACKBIRD.md` symlink dangles,
  because `../BLACKBIRD.md` resolves inside `context/`.

**Review:** one round by a Claude subagent and by Astra (gpt-6-astra, xhigh). Reports are
in `papers/cpp26-toolchain-2026-10-17/`. The dispositions are listed at the end.

## Findings that shape this plan (measured 2026-10-17)

- **The scratch build is clean.** An exported copy of `f4bd931` with
  `CMAKE_CXX_STANDARD 26` builds all 417 Debug steps on Mac Clang 23.1.2 with zero
  warnings under the existing `-Werror` set. CMake emits `-std=c++26`. The tests were not
  run.
- **Only `-std` changes.** `clang++ -###` shows no driver difference between C++23 and
  C++26 except `-std`. `_LIBCPP_ABI_VERSION` stays 1, and the hardening default is
  unchanged.
- **Both clang-tidy diagnostic probes still fire at `-std=c++26`.**
- **Clang 23 reports `__cplusplus == 202400L` in C++26 mode.** Mac (Clang 23.1.2) and
  Neuroses (Clang 23.1.4, libc++ 230104) report identical values for the checked
  features:
  - `__cpp_constexpr 202406L`
  - `__cpp_lib_saturation_arithmetic 202603L`
  - `__cpp_lib_span_at 202311L`
  - `__cpp_lib_constant_wrapper 202606L`

  This is partial C++26 support: there is no reflection (P2996) and no contracts (P2900).
  Claims name the mode and the facilities exercised, never full conformance.
- **libc++ 23 spells saturation arithmetic `std::saturating_add`, `saturating_sub`,
  `saturating_mul`, `saturating_div` and `saturating_cast`.** They live in `<numeric>` and
  are gated on `_LIBCPP_STD_VER >= 26`. `add_sat` and `saturate_cast` are absent.
  `span::at` is `[[nodiscard]]`.
- **Mac C++26 library features can fail where Linux succeeds.** Mac binaries link the
  system `/usr/lib/libc++.1.dylib` (2000.67.0) against Homebrew 23 headers, and vendor
  availability annotations are on. A C++26 facility that needs dylib symbols can fail
  availability on Mac even when it builds on Linux. Nothing in this change uses one.
- **TSan and fuzz have no recent record against today's suite.** The last full
  `rigor qualify` record dates from 2026-10-01. The C++23 upgrade qualified Mac
  Debug/Release and Linux Debug/Release/ASan only.

## Global constraints

- **Build settings:** `CMAKE_CXX_STANDARD 26`, `CMAKE_CXX_STANDARD_REQUIRED ON`,
  `CMAKE_CXX_EXTENSIONS OFF`.
- **Exact toolchain identities stay:** Mac Clang `23.1.2`; Linux Clang `23.1.4` with
  libc++ `230104`. No brew or apt change.
- **No adoption:** product code adopts no new C++26 facilities in this change. No
  `-freflection`, no `-fcontracts`, no fork compiler.
- **No silencing:** no new library, deadline change, sanitizer relaxation, NOLINT or
  warning suppression. Investigate every failure to its root cause.
- **Two hardening layers at most** (operator 2026-10-07): remediation with fixes, then one
  recheck with fixes.
- **Mutants never run on this laptop.** The existing Neuroses AFL campaign (LLVM 18
  targets) stays untouched.
- **Commits:** text-only commits use the documented per-command hook override after diff
  and link checks. Source commits pass the installed hook. Push the work branch at each
  completed unit. No merge into `master` without an explicit operator go.
- **Disk:** 53 GiB free. Each task states when its build trees are deleted.

## Completion rule

The upgrade is **qualified** only when both of these hold:
- The installed hook (Mac Debug suite plus owned C++/Lua lint) passes on the C++26 source.
- Every other selected profile and probe below has run to completion.

Every case passes, or is a **pre-existing failure**, meaning it fails identically on an
untouched C++23 baseline of the same profile and host, and it has a bead. The record and
the bead close reason name each pre-existing failure and its profile. They never say
"qualified" for a profile that has one.

A deterministic pre-existing failure in the hook's Debug suite or lint blocks the commit.
Bypassing the hook is forbidden. Stop and take it to the operator; do not repair it under
this upgrade.

**Timeout cases:** a timeout case that passes when run alone at its unchanged deadline
counts as passing after a rerun. Record both attempts. The hook may be rerun once for
this.

**Reruns of single cases** copy qualify's command and stay serial:
`/opt/homebrew/Cellar/cmake/4.3.0/bin/ctest --preset <p> --timeout 120 -R '^<case>$'`.
Without `--timeout 120`, tests lacking a TIMEOUT property get CTest's 1500 s default.

## Review focus

1. **A pre-existing failure blamed on C++26.** TSan and fuzz have no recent full run. The
   completion rule's baseline comparison settles it (Task 4).
2. **Mac UI cases timing out under concurrent load**, as happened in the C++23 upgrade.
   Handled by the rerun rule, and by running nothing else heavy on the Mac meanwhile.
3. **A silent standard downgrade on Linux** (CMake 3.28.3). `compiler_contract` runs inside
   every real Linux profile build and fails unless the build is in C++26 mode with libc++
   `saturating_*` and `span::at`.
4. **libc++ C++26-mode behavior changes.** The Release and sanitizer suites on both hosts
   are the mechanism (Tasks 2 and 3).
5. **Diagnostic probes still parsing as C++23.** `scripts/rigor` moves to `-std=c++26`,
   and the probes run in Task 2.

---

### Task 1: C++26 language contract, RED then GREEN, committed through the hook

**Files:**
- `tooling/probes/compiler_contract.cpp`, registered at `CMakeLists.txt:374,379-381`.
- `CMakeLists.txt:23`.
- `scripts/rigor:224,228`: the clang-tidy diagnostic probes' `-std=c++23`.
- `scripts/check-linux:73`: the libc++ version query's `-std=c++23`. Change only the
  flag. `compiler_contract` already enforces the mode in every Linux profile, so no
  second gate is added.

- [ ] **Step 0: Prepare.**
  1. `bd update arconaut-9xx --claim --json`.
  2. ~~Commit this plan and its review reports~~ (done before launch).
  3. In the worktree, run `scripts/tui-test --install` and then `scripts/tui-test --check`.
     The hook's TUI cases need a worktree-local install under `build/tools/`.
- [ ] **Step 1: Strengthen the contract.**
  - Add `#include <numeric>` and `#include <stdexcept>`.
  - Replace `static_assert(__cplusplus >= 202302L);` with the block below. Keep the
    existing byteswap, expected, span and thread checks.

```cpp
static_assert(__cplusplus > 202302L);  // Clang 23 reports 202400L for C++26.
static_assert(std::saturating_add<std::int8_t>(100, 100) == 127);
static_assert(std::saturating_sub<std::uint8_t>(3, 5) == 0);
static_assert(std::saturating_cast<std::uint8_t>(300) == 255);
static_assert(std::saturating_cast<std::int8_t>(-300) == -128);
```

  - In `main`, after the existing `view` checks, add the block below. Return codes 4 and 5
    are new and distinct from the existing 1-3.

```cpp
  if (view.at(1) != 255) {
    return 4;
  }
  try {
    (void)view.at(4);
    return 5;
  } catch (const std::out_of_range&) {
  }
```

- [ ] **Step 2: Observe RED.**
  - Run `/opt/homebrew/Cellar/cmake/4.3.0/bin/cmake --preset debug` (still at C++23),
    then `cmake --build --preset debug --target compiler_contract`.
  - Expected failures: "static assertion failed due to requirement '202302L > 202302L'"
    and "no template named 'saturating_add' in namespace 'std'".
  - Keep the log at `context/cpp26-upgrade/red.log`.
- [ ] **Step 3: Switch the language.** Set `CMakeLists.txt:23` to
  `set(CMAKE_CXX_STANDARD 26)`, and change both `scripts/rigor` probes and the
  `scripts/check-linux:73` query to `-std=c++26`.
- [ ] **Step 4: Observe GREEN.**
  - Reconfigure and rebuild Debug.
  - Run `ctest --preset debug --timeout 120 -R '^compiler_contract$'`. Expected: 1/1.
  - `build/debug/compile_commands.json` contains `-std=c++26` and no `-std=c++23`.
- [ ] **Step 5: Commit and push.**
  - Commit "Upgrade Blackbird to C++26 on pinned LLVM 23 profiles" through the installed
    hook. This run is the Mac Debug suite and lint qualification, and nothing later
    repeats it.
  - If the hook fails, apply the completion rule. A C++26 regression goes to Task 4. A
    deterministic pre-existing failure stops the work and goes to the operator.
  - Push.

### Task 2: Mac remaining profiles and probes (in parallel with Task 3)

**Files:** none tracked. Logs go to `context/cpp26-upgrade/mac-*.log`.

Task 1's hook already settled Debug and lint. This task runs the rest of `rigor qualify`
(`scripts/rigor:200-238`) once, with the same commands and without repeating
Debug or lint. Using `cmake` and `ctest` from `/opt/homebrew/Cellar/cmake/4.3.0/bin`, with
no other heavy owned build running on the Mac:

- [ ] **Step 1:** Run `scripts/rigor doctor`. Expected: no pin mismatch.
- [ ] **Step 2: Profile suites.**
  - For each of `release`, `asan`, `tsan` and `profile`, run `cmake --preset p`, then
    `cmake --build --preset p`, then `ctest --preset p --timeout 120` (serial, as qualify
    does).
  - If a profile fails, record it and continue to the next profile. Do not stop.
- [ ] **Step 3: Fault probes.** These expected diagnostics must appear:
  - `build/asan/instrumentation_fault memory` → "AddressSanitizer".
  - `build/asan/instrumentation_fault undefined` → "signed integer overflow".
  - With `TSAN_OPTIONS=halt_on_error=1:exitcode=66`,
    `build/tsan/instrumentation_fault race` → "ThreadSanitizer: data race".
- [ ] **Step 4: Fuzz.** Run `cmake --preset fuzz`, then `cmake --build --preset fuzz`,
  then `build/fuzz/bytes_fuzz -runs=1000 -max_len=4096 -seed=1`. Expected: exit 0.
- [ ] **Step 5: Diagnostic probes.** Reproduce qualify's tail (`scripts/rigor:213-232`),
  now at `-std=c++26`, in a temporary directory under `context/rigor/`:
  - clang-format violation;
  - `bugprone-unchecked-optional-access`;
  - `clang-analyzer-core.NullDereference`;
  - the LuaLS type error.
- [ ] **Step 6: Rerun and classify.**
  - Rerun each failing case alone with the serial `--timeout 120` command, and record
    both attempts.
  - Cases that still fail go to Task 4.
  - Delete `build/release`, `build/tsan`, `build/profile` and `build/fuzz` once their
    results are recorded. Keep `build/asan` and `build/debug` until Task 4 ends.

### Task 3: Neuroses profiles (in parallel with Task 2)

**Files:** none tracked. The script writes its log under `context/linux/run-*/checks.log`.

- [ ] **Step 1:** In the worktree, run `scripts/check-linux`. It runs debug, release, and
  ASan+UBSan at `-g -O1`. Expected final line: "Linux debug/release/asan native checks
  passed."
  - `compiler_contract` runs in every profile.
  - The Lua archive resolves through `../quarantine_proj` relative to the worktree, which
    is `blackbird/context/quarantine_proj`. Its sha256 matches.
- [ ] **Step 2:** check-linux stops at the first failing profile's CTest.
  - Rerun the failing cases with `--profiles <p> --test-regex '<cases>'`.
  - Then run every profile that never executed with `--profiles <remaining>` in full, so
    each profile's whole suite runs at least once.
  - Cases that still fail go to Task 4.

### Task 4: Classify and repair failures (only if Tasks 1-3 produce any)

- [ ] **Step 1: Baseline.**
  - Create a detached worktree at exactly
    `/Users/patrickbeam/projects/blackbird/context/cpp26-baseline`, at `f4bd931`.
    check-linux needs `../quarantine_proj` to resolve, so it must be a sibling and not
    nested.
  - Run `scripts/tui-test --install` there if a TUI case is involved.
  - Run each still-failing case in the same profile and on the same host. On Mac use the
    serial `--timeout 120` command. On Linux use
    `scripts/check-linux --profiles <p> --test-regex '<case>'`.
  - **Regression:** the case passes on C++23 and fails on C++26. This upgrade owns it.
  - **Pre-existing:** the case fails identically on both. File
    `bd create ... --deps discovered-from:arconaut-9xx --json`, record it, and do not
    repair it here.
- [ ] **Step 2: Repair regressions.** Find the root cause from libc++ source at the
  pinned revision, or from the standard's wording.
  - Write a RED oracle that shows the C++26 behavior, then fix, then GREEN.
  - Cite the source, following the C++23 record's "Compatibility repairs" pattern.
  - Commit through the hook.
- [ ] **Step 3: One recheck.** One fresh reviewer reads the repairs, and its valid
  findings get fixed. Then rerun the affected cases in every affected profile on both
  hosts. This is the second and final hardening layer.
- [ ] **Step 4:** Remove the baseline worktree and its builds.

### Task 5: Record, documentation and tracking

**Files:**
- **Create** `papers/2026-10-17-cpp26-toolchain.md`, mirroring the C++23 record. It
  covers:
  - selected profiles, the measured findings above, and the partial-support statement;
  - compatibility repairs;
  - observed results per profile and host, with counts and wall times, reruns stated, and
    interrupted runs called interrupted;
  - pre-existing failures with their beads.
- **Modify:**
  - `tooling/README.md:63,70`. Line 70 names the new contract facilities. Line 163 is
    historical and stays.
  - `tooling/TUI_PERFORMANCE_FUZZING.md:40`.
  - `AGENTS.md:83`.
  - `README.md:30`.
  - `docs/CURRENT_STATE.md:134`.
  - `docs/STATION.md:4`.
  - A dated update line at the top of `docs/HANDOFF.md` and
    `docs/G8_TOOLCHAIN_DECISION.md`.
  - `JOURNAL.md`.
- **Leave unchanged:** historical C++20/C++23 statements in dated subplans and records.

- [ ] **Step 1:** Write the record and the edits. Check that every relative link in the
  changed files resolves. The worktree's `BLACKBIRD.md` symlink is a known worktree
  artifact; check that link from the main checkout.
- [ ] **Step 2:** Close the bead.
  - If everything passed: `bd close arconaut-9xx --reason "C++26 qualified: Mac
    debug/release/asan/tsan/profile/fuzz + lint, Linux debug/release/asan" --json`.
  - Otherwise, name the profiles that passed and the pre-existing beads.
  - Then run `bd backup`.
- [ ] **Step 3:** Commit (text-only override) and push. Delete the remaining build trees
  and the investigation's scratch copy.

### Task 6: Integration (only on explicit operator go)

- [ ] **Step 1:** Present the record and wait for the operator's go. Then integrate onto
  current `origin/master` and push.
  - If `master` gained only documentation since `f4bd931`, do not rerun settled source
    checks (operator 2026-10-10).
  - Otherwise, run `scripts/rigor check debug` and a Linux changed-path check on the
    integrated tree.
- [ ] **Step 2:** Message the verbs Claude session (currently named `verbs-f6`; use
  `ListAgents`) and record in the journal: blackbird is C++26 on Clang 23.1.2/23.1.4,
  which is the verbs §8 pin, so vlib (verbs SPEC §3; `VERBS_ENUM` at §3.2) can be tried
  in blackbird.

---

## Beyond this plan: fork compilers

Blackbird has no pending compiler decision. Verbs keeps bloomberg/clang-p2996 as an
experiment, and nothing in its basics depends on it. That fork is on LLVM 21, has no
contracts, and ships no sanitizer runtimes. If reflection or contracts reach a released
LLVM, or the operator asks for them sooner, that becomes its own plan.

## Review dispositions

| Finding | Source | Disposition |
| --- | --- | --- |
| qualify stops at the first failure; the fallback skips probes; pre-existing failures make closure impossible or overclaimed | Claude 1, Astra 1, 3 | Fixed: completion rule, explicit remainder path (Task 2), conditional close |
| Isolated rerun drops `--timeout 120` (1500 s default) | Claude 2, Astra 8 | Fixed: serial rerun command in the completion rule |
| Fork called "verbs-selected"; §8 macro citation wrong | Claude 3, Astra 5 | Fixed: Stage 1 is the verbs §8 pin; fork reduced to a pointer; citations corrected |
| Performance observation is ceremony; procedure not on branch; attribution overstated | Claude 4, Astra 6 | Removed. No claim depended on it, and a flag change is not a TUI tranche |
| `view.at(4)` is `[[nodiscard]]` under `-Werror` | Claude 5 | Fixed: `(void)view.at(4)` |
| RED step builds before configuring | Claude 6, Astra 2 | Fixed: configure first |
| Worktree needs local `tui-test` install before the hook | Astra 2 | Fixed: Task 1 Step 0 |
| Duplicate Linux `__cplusplus` gate; escaping would break the remote script | Claude 7 | Removed; flag change only |
| No rule for hook flakes or pre-existing hook failures | Claude 8 | Fixed: completion rule |
| Baseline worktree path; dangling worktree `BLACKBIRD.md`; plan commit; bead claim | Claude 9 | Fixed: Task 1 Step 0, Task 4 Step 1, Task 5 Step 1 |
| Linux lint promised but never run | Astra 4 | Fixed: goal narrowed to Mac source analysis, matching existing tooling scope |
| Debug and lint repeated after the hook | Astra 7 | Fixed: the hook is the Debug and lint run; Task 2 runs only the rest |
| Mandatory superpowers skills in header | Astra 9 | Removed |
| Stale disk figure, registration lines, lost parallelism | Claude 10 | Fixed |
