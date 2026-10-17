# Plan review: Astra (gpt-6-astra, xhigh, read-only, one round)

2026-10-17. Prompt: [REVIEW_PROMPT.md](REVIEW_PROMPT.md). Reviewed the first draft of docs/CPP26_SUBPLAN.md; dispositions are in the plan.

1. **Major — Tasks 2–3: recovery can leave qualification unfinished.**  
   Task 2 Step 3 (`docs/CPP26_SUBPLAN.md:175`) substitutes `rigor check` after an interrupted qualification, but that command omits the intentional sanitizer and diagnostic probes in rigor’s qualification tail (`scripts/rigor:207`). Linux also stops on the first failing profile’s CTest invocation (check-linux:98 (`scripts/check-linux:98`)); subsequently running only `--test-regex '<cases>'` leaves the later profiles’ other tests unexecuted.  
   **Fix:** explicitly complete every unexecuted profile and qualification probe. Recheck repaired cases without repeating already completed, unaffected checks.

2. **Major — Task 1: the fresh worktree cannot execute the stated sequence.**  
   Step 2 (`docs/CPP26_SUBPLAN.md:134`) builds before configuring. Inspection found no `build/` directory, so this produces a missing-build-directory failure, not the intended compiler rejection. The subsequent hook also needs a worktree-local Microsoft `tui-test` installation: scripts/tui-test:32 (`scripts/tui-test:32`) locates it under this checkout’s `build/tools`, and line 80 (`scripts/tui-test:80`) rejects its absence.  
   **Fix:** configure Debug with the pinned CMake before observing RED, and explicitly install `tui-test` before the hook. Include the same preparation for any baseline worktree needing those tests.

3. **Major — Tasks 4 and 6: pre-existing failures have no coherent completion policy.**  
   Task 4 (`docs/CPP26_SUBPLAN.md:200`) says to leave such failures unrepaired, while Task 1 requires a passing hook and Task 6 (`docs/CPP26_SUBPLAN.md:254`) unconditionally closes the issue as qualified. A persistent pre-existing Debug or lint failure still blocks the actual hook (pre-commit:12 (`.githooks/pre-commit:12`)); identifying its age changes neither that behavior nor its qualification status.  
   **Fix:** make closure conditional on the required checks actually completing successfully. If a required failure remains outside scope or the hardening bound, retain the candidate inactive and report the specific block. Record other profiles’ limitations without calling them qualified.

4. **Major — Goal and Task 3: Linux lint is promised but never run.**  
   The goal (`docs/CPP26_SUBPLAN.md:5`) promises compilation, tests **and lint on both hosts**. The Linux runner performs configuration, compilation and CTest only (check-linux:83 (`scripts/check-linux:83`)). Mac lint analyzes the Mac compilation database (rigor:125 (`scripts/rigor:125`)), which does not establish analysis of Linux conditional paths.  
   **Fix:** narrow the goal to Mac source analysis plus both hosts’ native checks, consistent with the existing tooling scope, or explicitly provide the Linux analysis step and configuration.

5. **Minor — Findings and Stage 2: the verbs selection is stale.**  
   Line 44 (`docs/CPP26_SUBPLAN.md:43`) calls Bloomberg the “verbs-selected fork.” Current verbs SPEC §8 (`/Users/patrickbeam/projects/verbs/design/SPEC.md:568`) instead pins Homebrew LLVM 23.1.2 with `-std=c++2c`, retains Bloomberg for experiments, and prioritizes contracts over reflection. The earlier paper’s recommendation is no longer the selected specification.  
   **Fix:** describe Stage 1 as alignment with verbs’ current compiler choice. Keep future feature-driven compiler adoption separate, without presenting Bloomberg adoption as the expected next stage or citing §8 for a now-absent macro-fallback description.

6. **Minor — Task 5: the procedure reference is unavailable, and attribution is overstated.**  
   The referenced startup-performance README (`docs/CPP26_SUBPLAN.md:223`) does not exist in this worktree or `f4bd931`; it exists on another branch at `1180d2b`. Also, line 220 allows Task 4 repairs in the candidate, while line 228 calls the comparison a language-mode-only change. Production repairs would invalidate that attribution.  
   **Fix:** cite the procedure at its exact available revision or include its necessary commands here. Describe measured differences as the complete upgrade, including any production repairs; limit conclusions to the observer’s actual scenarios.

7. **Minor — Tasks 1–2: the sequence repeats settled Debug and lint work.**  
   Task 1’s hook already runs the full Debug suite and lint. Task 2 immediately repeats both through rigor qualify (`scripts/rigor:200`); its fallback repeats Debug preparation and full lint for every remaining profile (rigor:268 (`scripts/rigor:268`)). This adds substantial duplicate work without a stated new fault class, contrary to BLACKBIRD’s one-layer rule (`/Users/patrickbeam/projects/blackbird/BLACKBIRD.md:15`).  
   **Fix:** count the required hook as Debug/lint qualification, then run the remaining profile checks and intentional diagnostic probes once.

8. **Minor — Task 2 Step 2: the isolated rerun changes the default watchdog.**  
   The proposed `ctest --preset <p> -R ...` omits `--timeout 120`, which qualification supplies (`scripts/rigor:205`). The presets define no timeout, and some tests lack an explicit `TIMEOUT`. Those reruns therefore do not preserve the original deadline.  
   **Fix:** include `--timeout 120` in the isolated rerun; explicit per-test timeouts remain authoritative.

9. **Minor — Preamble and global constraints: remove the mandatory Superpowers workflow.**  
   Line 3 (`docs/CPP26_SUBPLAN.md:3`) mandates external execution skills, and line 65 mandates another debugging skill. These are not established prerequisites in this repository or available skill catalog.  
   **Fix:** remove those mandates and retain the concrete execution, investigation and bounded-review instructions.

**Overall verdict:** Keep the Stage 1/Stage 2 separation; it matches verbs’ current selection and avoids coupling this upgrade to experimental reflection. Revise the execution and completion paths before implementation. LLVM 23 provides partial C++26 support, so qualification should describe the selected mode and exercised facilities, not full conformance. [Clang status](https://clang.llvm.org/cxx_status.html) This review was read-only, with no builds or tests; Mac preprocessing confirmed the stated macros, but the sandbox blocked independent Linux inspection.