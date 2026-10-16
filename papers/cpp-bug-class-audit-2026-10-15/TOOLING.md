# Mechanical prevention: actual diagnostic capability and limits

These are proposed mechanisms, not enabled production rules. No CMake, clang-tidy,
annotation header or production library changed in this survey. Current owned
profiles are Clang23.1.2/libc++ on Mac and Clang18.1.3/libstdc++13 on Linux, C++20.

## Paired probes on both installed profiles

Six bad/good pairs run with -std=c++20 -Wall -Wextra -Werror and rejection of
unknown warning options. Mac additionally uses its actual qualified Xcode SDK;
Linux uses its declared libstdc++. Every bad case fails for the intended reason,
and every good counterpart compiles cleanly on both profiles:24 compile calls,
not a production-source qualification. All exact source, command and diagnostic
bytes are retained in [Mac results](diagnostics-results.json) and
[Linux results](diagnostics-linux-results.json). No snippets were executed.

| Mechanism | Bad case actually rejected / good counterpart | What it can narrow here | Important limit / adoption cost |
| --- | --- | --- | --- |
| -Wswitch-enum | Outcome::unknown omitted even behind default / explicitly handled | New effect/admission enum states cannot silently take an old generic default | A wrong explicit mapping still compiles. Pilot real switches; deliberate partial switches need redesign, not suppression. No runtime cost. |
| Existing Result [[nodiscard]] | Discard Result<int> / bind and read it | Accidental loss of an owned operation result | Already implemented in foundation.hpp53/72. It cannot distinguish retained refusal from capture failure or prove `.value()` is the right alternative. Explicit void casts escape it. No new attribute buys E1–E4. |
| -Wthread-safety plus owned capability/RAII annotations | Call join_worker annotated locks_excluded(mu) under Guard(mu) / release guard first | Accidental joins, raw-loan release or unknown callback work under a declared mutex; guarded fields and owner-thread roles can use the same analysis | Plain std::mutex/lock_guard are not automatically a complete cross-library contract. Need a small owned annotated wrapper and consistent interfaces. Does not prove progress, custody, correct archive predicates or all deadlocks. No required runtime instrumentation. |
| [[clang::lifetimebound]] plus -Wdangling | Borrow a temporary into a longer-lived reference / borrow a live local | Views/borrows returned from actual owning objects | Reference lifetime only: not PID validity, retained-byte completion or cells. Contracts/alias relationships must be accurate; inspect actual returning-view APIs before annotating them. No runtime cost. |
| -Wconsumed and Clang consumable/callable_when/set_typestate | signal after modeled reap / signal then reap | One narrow modeled child or capture-ticket sequence | Clang extension with alias/control-flow limits. It trusts transition annotations and cannot observe auto-reaping, hidden aliases or a missing syscall-result transition. Treat as a pilot, not linear-type proof. No runtime cost. |
| Standard distinct aggregate types | move_to(Bytes{3}) where Cells required / Cells{3} | Byte offsets versus terminal columns, resident counts versus durable receipt counts, borrowed PIDs versus owned child handles | Does not choose a correct Unicode width or prevent explicit value extraction/unchecked conversion. Own the conversion boundary; wrappers need no extra heap allocation. |

The initial Mac scratch invocations omitted the profile's explicit sysroot and
failed for the missing compiler-default SDK, including all good cases. Those
are retained under ignored context as setup failures and excluded from the table.
The corrected invocations use exactly the configured SDK, not warning suppression.

Current live Clang documentation also advertises -Wlifetime-safety-permissive.
A direct availability probe diagnoses the bad borrow on installed Clang23, but
installed Clang18 rejects the flag as unknown. [Mac observation](diagnostics-newer-lifetime-flag.json),
[Linux observation](diagnostics-linux-newer-lifetime-flag.json). Do not put it in
the shared build flags or disable unknown-option errors to make it appear portable.
A separately selected Mac advisory pass could be useful; no codebase-wide result
or adoption is claimed here.

## Literature and reference implementation grounding

Hutchins, Ballman and Sutherland, *C/C++ Thread Safety Analysis* (IEEE SCAM2014),
read sectionsII–V and limitations: declaration-level capability contracts can
make lock policy mechanically checkable; their interfaces/aliases bound what is
known. The annotations have no runtime checking semantics. Our join exclusion
probe attacks an actual policy of interest, while archive debt still needs an
owner-specific predicate. [Primary paper](https://research.google.com/pubs/archive/42958.pdf),
[current Clang manual](https://clang.llvm.org/docs/ThreadSafetyAnalysis.html).

Strom and Yemini, *Typestate* (IEEE TSE1986), read printed pages157–158 visually
(the acquired scan has no extractable text): state controls which operations are
meaningful at a particular point, beyond the object's ordinary type. That is a
useful design basis for an owned child that cannot be signaled after reaping and
an archive token obtainable only after final debt inspection. Clang's small
consumed-state vocabulary does not implement all of that paper's system.
[Primary paper](https://www.cs.cmu.edu/~aldrich/papers/classic/tse12-typestate.pdf).

Read LLVM18's actual warn-thread-safety-analysis, warn-thread-safety-negative,
and warn-consumed-analysis fixtures at llvmorg-18.1.8 as reference test design,
not executable code adopted into Blackbird. They expose where contracts must be
attached and where state loss/aliases limit a warning. Current docs are retained
as dated snapshots; installed-profile results above decide availability.
Exact originals, source/license versions and restoration are in
[PROCESS_TOOLING_SOURCES.json](PROCESS_TOOLING_SOURCES.json).

## Existing gate and candidate linter rules

The existing CMake flags already include conversion/sign warnings, shadow,
format checking, implicit fallthrough, -Wall/-Wextra/-Werror and unknown-warning
rejection. Existing clang-tidy selects use-after-move, unchecked-optional-access,
easily-swappable-parameters, two copy/value performance rules and analyzer checks.
Result is already nodiscard. ASan/UBSan/TSan remain valuable for memory, undefined
behavior and data-race faults. The new participant archive race is synchronized
and semantically wrong; a clean sanitizer result would not reject it.

Do not enable every available check and confuse warning volume with this task.
Useful candidates to pilot on the owning units:

- bugprone-unused-raii / unnamed guard checks for accidentally short lock or
  custody lifetimes; compiler capability contracts are stronger for a declared
  join-under-lock policy. Neither catches an incorrect final debt predicate.
- bugprone-unused-return-value with an explicit owned-API list, and checked
  syscall wrappers, for a wait result dropped or interpreted as a boolean.
  Correct handling still needs Running/Exited/Error and actual PID/status checks.
  Existing intentional best-effort cleanup cannot be blindly rewritten.
- bugprone-exception-escape for unintended throwing cleanup paths. It cannot
  infer that a successful rename happened before a later allocation.
- Narrow AST/source-confinement rules after introducing owners: forbid direct
  kill/waitpid outside child-custody adapters; forbid wcwidth/mbrtowc outside
  the selected display-map implementation; forbid new bare capacity throws in
  adapters after introducing distinct domain/capture results. A broad text grep
  is a migration inventory, not a sound context-sensitive correctness checker.

A custom AST checker is a possible owned tool, not selected by this report.
Each pilot must show its named bad call/transition fails and its legitimate
counterpart passes on the actual profiles before becoming a mandatory rule.
Do not add checks whose only result is proving another generic check ran.
The semantic permanent regressions proposed in the class reports remain the
direct oracles for retention, no-launch, external effect and physical cells.
