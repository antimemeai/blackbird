# Toolchain decision: retain pins; reconcile release tracking

2026-10-17 update: the operator authorized C++23 and Neuroses LLVM23.1.4/libc++23.1.4.
Mac retains LLVM23.1.2. The earlier toolchain declarations below describe their
original snapshot; current profiles and qualification are in the
[C++23 upgrade record](../papers/2026-10-17-cpp23-toolchain.md).

**Decision:** Treat selected upstream [LLVM 23.1.3](https://github.com/llvm/llvm-project/releases/tag/llvmorg-23.1.3) as a maintenance-review signal, not an upgrade instruction. Leave compiler pins unchanged.

## Declared profiles versus upstream

| Scope | Supplied evidence |
|---|---|
| macOS | `CMakeLists.txt` defaults to `macos-llvm23`, requires Darwin and exact LLVM Clang **23.1.2**. `tooling/README.md` lists Clang, clang-format, clang-tidy and clangd at **23.1.2**. |
| Linux | `CMakeLists.txt` requires Linux and exact LLVM Clang **18.1.3** for `linux-clang18`, with explicit `libstdc++` selection. The supplied README does not establish Linux qualification results. |
| Selected upstream | Release facts identify **LLVM 23.1.3**, tag `llvmorg-23.1.3`, published and updated **2026-10-06T14:53:00Z**. This is distinct from both declared compiler versions. |

`docs/G8_TOOLCHAIN_HUD.md` already lists 23.1.3, but marks fetch timing unavailable and freshness unguaranteed. Its listing is not evidence of adoption or qualification. No upstream release-note contents were supplied, so no fixes, compatibility benefits or security urgency can be inferred.

## Next useful maintenance step

Perform a **read-only pin and evidence audit**: compare `tooling/local-profile.json`, `CMakePresets.json` and the tool-verification scripts against the declarations above; record discrepancies and the separate Mac/Linux evidence scope. These files are referenced by `tooling/README.md` but were not supplied for inspection. Read the selected upstream release notes before assessing whether any change merits a separately scoped evaluation.

## Risks and guardrails

- Exact-version checks intentionally reject 23.1.3 under the current profiles; an incidental package update could break configuration or tool-identity gates.
- Compiler-only replacement risks splitting the coherent macOS compiler/formatting/analysis/editor tool set.
- The README documents a prior host-specific ASan initialization failure and actual-host SDK discovery; patch numbering alone does not establish sanitizer or SDK compatibility.
- Historical qualification summaries are not acceptance caches. If a compiler change is later authorized, the README requires fresh CMake configuration and rerunning the owning qualification—not reusing an old cache or suppressing instrumentation failures.
