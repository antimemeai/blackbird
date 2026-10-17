# C++23 and Neuroses LLVM upgrade

2026-10-17. The operator requested the server toolchain upgrade, Blackbird C++23,
native build/test verification, then main integration. Source checkpoint
`87c1116` passes the full installed Mac source hook:260 Debug tests, owned C++
formatting/analysis and Lua formatting/syntax/type checks.

## Selected profiles

| Host | Compiler | Standard library | Language |
| --- | --- | --- | --- |
| Mac arm64 | Existing pinned LLVM Clang23.1.2 | Existing configured libc++/Xcode SDK | C++23 |
| Neuroses, Ubuntu24.04.5 x86_64 | LLVM Clang23.1.4 | libc++/libc++abi23.1.4 | C++23 |

CMake requires the language version and exact compiler identity. Linux's profile
is now `linux-llvm23`; all its C++ targets select libc++. The compiler contract
checks the C++23 language macro, constexpr `std::byteswap`, `std::expected`
success/error behavior, byte/span access and synchronized threads. Diagnostic
probes use the same language version. Individual C++23 facilities still depend
on [Clang](https://clang.llvm.org/cxx_status.html) and
[libc++](https://libcxx.llvm.org/Status/Cxx23.html) implementation support.

The Linux runner includes the previously omitted physical-terminal/input scripts
and Lua packages, builds with at most16 jobs, and runs CTest with6 jobs while
respecting RUN_SERIAL. Each test keeps its existing watchdog; the obsolete
600second ceiling for three complete fresh builds/tests is removed.

## Server provisioning

Install versioned LLVM23 compiler/tools, formatter, analyzer, language server,
linker, debugger, sanitizer runtime, libc++/libc++abi and unwind packages from the
[signed official LLVM APT repository](https://apt.llvm.org/). The resolved version
is `1:23.1.4~++20261014053933+0cd28174a1f6-1~exp1~20261014053943.82`.
The signing key was checked against fingerprint
`6084F3CF814B57C1CF12EFD515CF4D18AF4F7421` before installation.

`/etc/apt/sources.list.d/blackbird-llvm23.list` contains:

```text
deb [arch=amd64 signed-by=/usr/share/keyrings/blackbird-llvm.asc] https://apt.llvm.org/noble/ llvm-toolchain-noble-23 main
```

Package set:

```sh
sudo apt-get install --no-install-recommends clang-23 clang-tools-23 \
  clang-format-23 clang-tidy-23 clangd-23 llvm-23 lld-23 lldb-23 \
  libclang-rt-23-dev libc++-23-dev libc++abi-23-dev libunwind-23-dev
```

All installed LLVM executables report23.1.4; libc++ reports `_LIBCPP_VERSION=230104`.
Existing Clang18/libstdc++13 remain available. APT replaces the older libc++18/
unwind packages; default compiler alternatives are unchanged. Existing CMake3.28.3
and Ninja1.11.1 already support this build. Installation log is retained under
`context/cpp23-upgrade/server-install.log` in the original project checkout.
The existing LLVM18 AFL campaign retains its recorded tool/target identities.

## Compatibility repairs

- File helpers and their oracles previously used the OS to select C++ library
  behavior. Select libstdc++ behavior with `__GLIBCXX__` instead. Linux now follows
  its selected libc++ stream error/EOF and path behavior, like the plain helper
  used as its reference. Actual negative native returns and errno remain captured.
- The actual-stdio fixture uses dyld interposition on Mac and scoped LD_PRELOAD
  on Linux. A shortened positive acquisition is followed by a genuinely failing
  libc read within the same requested stream operation, so bulk-reading libc++
  observes the intended positive prefix and real descriptor failure.
- Eight test replacement allocators allocated with malloc but omitted matching
  sized delete overloads. Complete their scalar/array overloads, forwarding to
  the existing matching deletes. ASan's mismatch detector stays enabled.
- libc++ hides template helper frames and rounds string allocations differently.
  Identify SavedState staging by the actual object extent and exported owning
  save operation when its template helper is hidden. Calibrate the exact Lua
  result allocation through the selected string library before arming the cut;
  retain owning-frame, real cancellation, disk-generation and originals assertions.

Grounding uses LLVM package revision `0cd28174a1f6`:
[fstream](https://github.com/llvm/llvm-project/blob/0cd28174a1f6/libcxx/include/fstream)
and [filesystem operations](https://github.com/llvm/llvm-project/blob/0cd28174a1f6/libcxx/src/filesystem/operations.cpp).
Intact sources and restoration are recorded in [QUARANTINE](../QUARANTINE.md).

## Sanitizer profile

The unoptimized Linux sanitizer build timed out in large native cases. LLDB
showed active CRC32C work over large journal payloads, not a blocked cancellation.
Use `-g -O1` for the Linux ASan+UBSan profile, following
[Clang's guidance](https://clang.llvm.org/docs/AddressSanitizer.html).
Both sanitizers, frame pointers, no-recovery settings, allocation-mismatch checks,
assertions and watchdogs remain enabled. The same caller case passes in7.84seconds
inside its unchanged60second watchdog; the complete profile passes260/260.
The deliberately faulty memory and signed-overflow probes still produce their
expected diagnostics with these flags. No product checksum optimization is included.

## Observed verification

| Profile | Result |
| --- | --- |
| Linux Debug |260/260 full suite; final calibrated caller rebuilt/rechecked1/1 in20.36s |
| Linux Release |259/260 initial suite; corrected allocation-projection case1/1 in1.85s; all cases covered |
| Linux ASan+UBSan, `-g -O1` |260/260 full suite in307.04s; intentional memory/overflow diagnostics detected |
| Mac Debug/source gate |260/260 in464.79s; full owned C++/Lua lint passes |
| Mac Release |258/260 initial suite; two UI cases pass alone at unchanged deadlines; repaired fixtures28/28 and final calibrated caller1/1 |

The two Mac UI cases first hit watchdogs during concurrent host activity, then
pass alone in22.92/51.02seconds. No screen expectation, UI implementation or
timeout was changed. The final Mac Release caller passes in2.04seconds.
Logs are retained in the qualified worktree's ignored `context/cpp23-upgrade/`.
Earlier interrupted runs are recorded as interrupted, not passes. Changed Linux
fixtures were rebuilt incrementally in retained workspaces; their owning Python
drivers were resumed afterward and both temporary workspaces were removed.

Integrated and pushed to `master` at `186f878`, including source checkpoint
`87c1116`. The original checkout continues its separate Claude implementation
branch. The qualified worktree retains its builds/logs and releases the master
checkout lease after publication. Tracking issue `arconaut-34z` is closed.
