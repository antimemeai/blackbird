# Plan review: Claude subagent (read-only, one round)

2026-10-17. Prompt: [REVIEW_PROMPT.md](REVIEW_PROMPT.md). This review covers the first
draft of docs/CPP26_SUBPLAN.md. The dispositions are recorded in the plan.

## Verified true

**Against the repo:**
- `CMakeLists.txt:23` and the contract registration.
- `scripts/rigor:224,228`.
- `scripts/check-linux:73`.
- The final lines of the qualify and Linux scripts.
- The Mac C++26 macros: `__cplusplus 202400L`, `__cpp_constexpr 202406L`,
  `saturation_arithmetic 202603L`, `span_at 202311L`, `constant_wrapper 202606L`.

**Library and toolchain facts:**
- libc++ spells the operations `saturating_*`, gated on `_LIBCPP_STD_VER >= 26`. The names
  `add_sat` and `saturate_cast` do not exist.
- Mac binaries link `/usr/lib/libc++.1.dylib` 2000.67.0, and `__config_site` sets
  `_LIBCPP_HAS_VENDOR_AVAILABILITY_ANNOTATIONS 1`.
- The fork's install directory has no `lib/clang/21/lib`.
- The ASan deadlock note is in `tooling/README.md:98`.
- The Task 6 document line references are correct.
- Bead `arconaut-9xx` exists.

**Read-only probes:**
- `clang++ -###` shows no driver difference between C++23 and C++26 apart from `-std`.
  `_LIBCPP_ABI_VERSION` stays 1, and the hardening default is unchanged.
- Both clang-tidy probes still emit their diagnostics under `-std=c++26 -isysroot <sdk>`.

## Findings, most severe first

1. **Major: Task 2 Steps 1 and 3, and Task 6 Step 2.** The qualify step stops at the first
   failure and gives no way to finish the remaining work.
   - `qualify()` raises on the first failing step (`scripts/rigor:202-206`). A failure in
     TSan, which is likely, therefore skips everything after it: the fuzz and profile
     builds, every fault probe, the fuzz runs, lint, the diagnostic probes, and the
     record.
   - The fallback, `rigor check <profile>`, runs none of the fault or diagnostic probes. It
     also re-runs the debug suite and the full lint for every profile.
   - Pre-existing failures are deliberately not repaired. If any exist, qualify can never
     pass, and the close reason claims more than was qualified.
   - **Fix:** add a completion criterion, an explicit path that runs the remaining work
     once, and a close reason that matches what actually ran.
2. **Major: Task 2 Step 2.** A bare `ctest -R` rerun drops `--timeout 120`.
   - Tests that have no TIMEOUT property get CTest's default of 1500 s. The file has 182
     `add_test` lines and 131 `TIMEOUT` lines.
   - **Fix:** rerun with `ctest --preset <p> --timeout 120 -R '^<case>$'`, serially.
3. **Major: Findings 4, Stage 2, and Task 7 Step 2.** The plan misstates the verbs
   decision.
   - The current verbs SPEC §8 (rev 0483549, lines 568-571) pins Homebrew LLVM 23.1.2
     with `-std=c++2c`. It calls blackbird's move a flag change, and keeps the fork for
     experiments only.
   - §8 has no macro fallback. `VERBS_ENUM` is described in §3.2, lines 183-213, as the
     primary mechanism.
   - `verbs-f6` is not a bead.
   - **Fix:** describe Stage 1 as the toolchain verbs mandates, cut Stage 2 down to a
     pointer, and correct the citations.
4. **Major (doctrine): Task 5.** The performance observation is ceremony.
   - No claim depends on it.
   - The tranche rule (AGENTS.md:227-229) applies to TUI tranches, and this is not one.
     The C++23 upgrade did not run an observation either.
   - Timing cannot detect a change in behaviour. The Release suites on both hosts are
     what detect that.
   - The cited README exists only on the unmerged branch (1180d2b).
   - **Fix:** cut the task.
5. **Minor: Task 1 Step 1.** `span::at` is `[[nodiscard]]`, so a bare `view.at(4);`
   fails with `-Werror,-Wunused-result`.
   - **Fix:** write `(void)view.at(4);`. This was verified clean under the project's
     flags and clang-tidy.
6. **Minor: Task 1 Step 2.** The worktree has no `build/` directory yet, so the RED step
   fails with "could not load cache" instead of the intended error.
   - **Fix:** configure first. With that done, the expected RED text is confirmed.
7. **Minor: Task 1 Step 3.** The new Linux `__cplusplus` gate duplicates
   `compiler_contract`, against the one-layer rule.
   - Writing `\n` inside the non-raw remote `'''` string would also break the remote
     script with a syntax error.
   - **Fix:** change only the flag.
8. **Minor: Task 1 Step 5.** The plan has no rule for a hook that fails on a flaky or
   pre-existing case. Bypassing the hook is forbidden, so a pre-existing Debug failure
   would block the work permanently.
   - **Fix:** a timeout case that passes alone gets one rerun of the hook. A deterministic
     pre-existing failure goes to the operator.
9. **Minor: Tasks 4 and 6.** Worktree pitfalls.
   - `check-linux` reads `ROOT.parent/quarantine_proj/...`. The baseline must therefore be
     created at exactly `blackbird/context/cpp26-baseline`, not nested inside the
     worktree.
   - The worktree's `BLACKBIRD.md` symlink dangles.
   - Nothing says when the plan itself gets committed, and the bead is never claimed.
10. **Minor: Global constraints.**
    - Free disk is 53 GiB, not 62.
    - The contract test is registered at lines 374 and 379-381.
    - Task 3 can run in parallel with Task 2.

## Verdict

The Stage 1/Stage 2 split is right, and stronger than the plan states: verbs already pins
Homebrew Clang 23.1.2 at C++26. The technical core is sound. As written, though, the plan
cannot finish honestly: qualify fails fast while pre-existing failures are left in place,
and a rerun stretches the 120 s deadline. Revise findings 1-4 before execution.
