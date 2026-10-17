# C++23 and Neuroses LLVM upgrade

Operator2026-10-17 requests the server toolchain upgrade, Blackbird C++23
configuration, native build/test verification, then main integration. Work starts
from master `74a42f4` in an isolated worktree because the main checkout is running
another merge's source hook. Preserve that integration, artwork and running
server programs. No server-wide OS upgrade, default-compiler alternatives change,
new runtime dependency beyond the selected C++ standard library, or live
Blackbird process activation belongs to this unit.

The concrete mismatch is the C++20 baseline and Linux Clang18/libstdc++13 profile.
Install versioned LLVM23 compiler, formatting/analysis/editor tools, debugger,
linker, sanitizer runtime, libc++/libc++abi and unwind development packages on
Ubuntu24.04 x86_64. The [official LLVM APT repository](https://apt.llvm.org/)
provides the release suite `llvm-toolchain-noble-23`; its package metadata reports
23.1.4. Verify signing-key fingerprint
`6084F3CF814B57C1CF12EFD515CF4D18AF4F7421` and use a repository-scoped signed-by
key. Retain Clang18, and leave existing command alternatives alone. Package
installation replaces Ubuntu's older libc++18/unwind development/runtime packages
with the current LLVM packages; Blackbird's old Linux build used libstdc++13.
Keep the installation log under ignored `context/cpp23-upgrade` in the main checkout.

CMake requires C++23, Clang23.1.2 on Mac and Clang23.1.4/libc++23.1.4 on Linux.
Update diagnostic probes and the Linux check launcher together. Extend the existing
compiler contract with the C++23 language macro, constexpr byteswap and expected
success/error behavior; retain its byte/span and synchronized-thread assertions.
[Clang's language support](https://clang.llvm.org/cxx_status.html) and
[libc++'s C++23 support](https://libcxx.llvm.org/Status/Cxx23.html) are feature-level;
selecting C++23 does not claim every facility is implemented.

Verification uses the existing direct tests across Linux Debug, Release and
ASan+UBSan, plus the Mac debug source hook (tests and owned C++/Lua analysis),
Mac release build/tests, and direct sanitizer diagnostics on Linux. CTest retains
each test's watchdog. Drop the old whole-suite 600second ceiling, which includes
three fresh builds and is unrelated to individual behavior. Use at most16
compilation jobs on the server. No tests or source repairs outside consequences
of this toolchain change; handle findings in the existing two-layer bound.

## Installed server profile

The signed release metadata resolved the following package version:
`1:23.1.4~++20261014053933+0cd28174a1f6-1~exp1~20261014053943.82`.
`clang++-23`, `clang-format-23`, `clang-tidy-23`, `clangd-23`, `llvm-config-23`,
`ld.lld-23` and `lldb-23` all report23.1.4. `<version>` preprocessing with
`-std=c++23 -stdlib=libc++` reports `__cplusplus=202302L` and
`_LIBCPP_VERSION=230104`. Existing Clang18.1.3 remains installed; CMake3.28.3
and Ninja1.11.1 already support this build and remain unchanged.

APT source `/etc/apt/sources.list.d/blackbird-llvm23.list`:

```text
deb [arch=amd64 signed-by=/usr/share/keyrings/blackbird-llvm.asc] https://apt.llvm.org/noble/ llvm-toolchain-noble-23 main
```

Versioned package set:

```sh
sudo apt-get install --no-install-recommends clang-23 clang-tools-23 \
  clang-format-23 clang-tidy-23 clangd-23 llvm-23 lld-23 lldb-23 \
  libclang-rt-23-dev libc++-23-dev libc++abi-23-dev libunwind-23-dev
```

A direct ASan+UBSan build of the owned intentional fault fixture using C++23 and
libc++ detects its heap overrun and signed overflow with nonzero exits and the
expected diagnostic markers. Log: `context/cpp23-upgrade/linux-diagnostics.log`
in the candidate worktree. The compiler/library contract also passes on Mac
using the selected Xcode SDK; bare invocation inherits a stale Homebrew sysroot,
so the standalone probe explicitly uses `xcrun --show-sdk-path`, like CMake.

Record full build/test results, findings and integration below once observed.

## Compatibility finding and repair

The first Linux Debug build fails `-Wunused-but-set-global` for `stdio_error`
in the existing file test. Its read is guarded by `__APPLE__`, although it
observes libc++'s stdio implementation. Source inspection also finds production
file-helper branches using `__linux__` to select libstdc++ behavior. Those
conditions conflate the OS with the selected C++ library.

Grounding uses the installed LLVM package's revision `0cd28174a1f6`:
[libc++ fstream](https://github.com/llvm/llvm-project/blob/0cd28174a1f6/libcxx/include/fstream)
acquires bytes through `fread`;
[filesystem operations](https://github.com/llvm/llvm-project/blob/0cd28174a1f6/libcxx/src/filesystem/operations.cpp)
constructs absolute and weakly-canonical paths using the same libc++ algorithms
on these two hosts. The relevant error mapping, empty path, weak canonical and
GCC canonical-fallback branches now select `__GLIBCXX__`, preserving the
libc++ path on Linux as well as Mac. Genuine OS process/status checks stay OS
checks. Existing GCC-specific fallback cases remain conditional on that library.

The stdio failure fixture now selects its assertion by `_LIBCPP_VERSION` and
uses its existing dyld shim on Mac or an ELF `LD_PRELOAD` shim on Linux. Both
forward to actual libc `fread`, really close the selected descriptor and retain
its actual returned bytes/errno. No diagnostic suppression or weakened expected
outcome is introduced. Full Linux profiles and the Mac source gate recheck this
consequential change as part of the upgrade.

Main's independent TUI merge completed at `a8a28fb`, followed by the text-only
Claude-auth inventory at `d1a8634`. Save the candidate diff, advance its branch
to `d1a8634`, then reapply only this unit's changes and appended journal entry.
The two partial Mac builds were interrupted deliberately at that transition;
their incremental outputs will be reused by the combined-tree build. No test
pass is claimed for those interrupted invocations.

The first complete Linux Debug run passes254/260 cases. Four failures are missing
remote-bundle inputs: edge/viewport/input scripts and the Lua packages directory.
Add those inputs and compare all CMake script references with the bundle's list.
The other two are ABI-sensitive test mechanisms: libc++ hides `make_unique` frames,
and its bulk stream read treats an artificially shortened `fread` as EOF before
the intended descriptor loss. Identify the SavedState staging allocation by its
actual object extent plus exported owning save operation when the template frame
is hidden. The stdio shim now performs the positive prefix and a genuinely failing
second libc acquisition within the same requested stream read, preserving actual
returned counts and errno. Keep all original byte/disk/atomicity assertions.
Both corrected direct fixtures pass on Mac. No additional production behavior
change follows from these test repairs.

Interrupt the first Mac source hook before analysis to include these repairs in
one final source gate. The prior partial Mac debug invocation is not a pass.
Linux uses16 compilation jobs per build and6 CTest jobs, respecting RUN_SERIAL.
Release and ASan may overlap on the48CPU server, with at most32 compilation jobs.
Retain the already compiled ASan workspace while its driver is stopped, interrupt
its old CTest, rebuild only the changed fixture targets, then run its complete
suite with the final fixtures. The driver will be resumed for its normal cleanup
after that result is captured. Run fresh Debug/Release with the corrected bundle.

The updated Linux libc++ allocator uses sized deallocation. ASan exposes a real
mismatch in test-only replacement allocators that allocate with `malloc` but
omit matching sized delete overloads. Complete scalar/array sized deletes in the
eight affected fault-injection fixtures (foundation, journal batch, retained state,
command delivery/observation, participant observation, task observation/publication),
forwarding to their existing matching unsized deletes. Do not disable ASan's
allocation/deallocation mismatch checks. The26 owning ASan cases pass after this
repair, including all four initially failing cases.

Reuse the compiled Linux workspaces for these fixture repairs: hold the owning
Python driver, finish/interrupt its old child as appropriate, atomically replace
only the changed fixture sources, force their incremental rebuild, and resume
Debug's ordinary driver. The ASan driver remains held until its final complete
CTest run is captured, then resumes its normal temporary-directory cleanup.
This avoids recompiling unchanged production sources or overlapping two CTest
runs in the same build directory. Mainline remains unchanged pending qualification.

The optimized Linux run passes259/260 cases; its remaining Lua projection cut
assumes libstdc++'s32769byte string allocation. Calibrate the allocation extent
for the exact32768byte result through the selected library's `string::capacity`
before arming the existing cut. Keep the direct owning `Runtime::eval` frame
requirement, later-caller exclusion, actual cancellation EOF and captured-record
assertions. The corrected Release case passes in1.85seconds.

Two Mac Release physical UI cases time out during overlapping host checks, then
pass alone at the original deadlines (22.92/51.02seconds). No UI implementation,
expected screen, or timeout was changed. The full Release run's other258 cases
pass, and all28 changed-fixture cases pass after their final rebuild. Linux's
full ASan run passes256/260 cases, with three native process watchdogs and the
PTY saturation deadline requiring an isolated recheck after concurrent work.
Do not claim those four passed until their original unchanged cases complete.

Debugger inspection of the slow sanitized caller finds active CRC32C work over
large journal payloads, not a blocked cancellation or missing event. The previous
Linux sanitizer profile compiled libc++23's heavily exercised span operations
without optimization. Select `-g -O1` for this Linux ASan+UBSan profile, following
[Clang's documented instrumentation guidance](https://clang.llvm.org/docs/AddressSanitizer.html).
Keep both sanitizers, frame pointers, no-recovery settings, allocator mismatch
checks, every assertion and existing CTest watchdogs. The same complete caller
case now passes in7.84seconds inside its unchanged60second watchdog. Rebuild and
run the complete sanitized profile with these declared flags. This is a toolchain
profile correction, not a product checksum optimization or relaxed timeout.
The final calibrated Mac Release caller also passes in2.04seconds.
