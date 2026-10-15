# TUI tranche measurements and retained input campaigns

Current [source status](../papers/tui-perf-fuzz-2026-10-15/SOURCE_STATUS.md): the
measurement round and launch complete, but the required source gate fails two
existing paths. Helper/target sources are retained as inactive candidates under
ignored context, and their CMake registration is restored. Commands below describe
those candidates for later qualification, not installed repository helpers.

Run the performance comparison between every TUI tranche. Use the previous
tranche's exact source and current source, matched Release compiler/library/host,
the same observer and scenario definitions, with owned builds and fuzz load out
of the timing window. Keep every attempted sample and failure. A new scenario
starts a new baseline; historical timings from another observer are descriptive.
Thirty trials per variant and geometry are the initial setting, configurable for
deeper questions. No fixed latency budget or automatic optimization follows.

```sh
python3 scripts/bench-tui paired-spec.json context/tui-round-NEW --count 30
python3 scripts/bench-tui-report context/tui-round-NEW --compare before after
```

The external spec maps `before`/`after` to executable, exact source, profile and
build_identity. See the [observer procedure](../papers/tui-perf-fuzz-2026-10-15/performance/README.md)
and [tranche ledger](../papers/tui-perf-fuzz-2026-10-15/performance/TRANCHES.tsv).
The observer records raw endpoint timestamps; the separate report derives
quantiles/deltas. These PTY/observer endpoints include scheduling and parsing,
not physical display latency. Current scenarios cover startup, unsubmitted text,
4KiB bracketed paste, palette selection and resize at two geometries. Existing
semantic, fault, PTY, styled-screen, sanitizer and source requirements remain.

AFL++5.03c is explicitly adopted as a development tool, pinned at
`dbaf11913c1b2702dee5b4d3dcfffd52f1defe50`. Original source/license/restoration
are in the [manifest](../papers/tui-perf-fuzz-2026-10-15/fuzz/MANIFEST.tsv).
The executable lives in the owned Neuroses campaign directory. Build its LLVM
mode against Clang/LLVM18, with outer make jobs1 and nested `nproc=2`; upstream's
empty `nproc` otherwise expands a nested command to unbounded `-j`. No production
library, cloud provisioning or system tool-policy change is selected.

From an owned Linux extraction of that pinned archive, the installation uses:

```sh
make -j1 all nproc=2 LLVM_CONFIG=/usr/bin/llvm-config-18 \
  CC=/usr/bin/clang-18 CXX=/usr/bin/clang++-18 \
  NO_PYTHON=1 NO_NYX=1 NO_CORESIGHT=1 PREFIX="$PWD/install"
make install nproc=2 LLVM_CONFIG=/usr/bin/llvm-config-18 \
  CC=/usr/bin/clang-18 CXX=/usr/bin/clang++-18 \
  NO_PYTHON=1 NO_NYX=1 NO_CORESIGHT=1 PREFIX="$PWD/install"
```

The host adds the required `llvm-18-dev` package without unrelated upgrades.
Optional compiler modes not used by this campaign are not selected.

`tui_fuzz composer` and `tui_fuzz scene` consume at most8192 bytes from stdin.
Normal CTest runs only fixed seeds. The actual AFL campaign uses a separately
instrumented ASan/UBSan binary and ordinary forkserver isolation per testcase;
no persistent-loop claim. The target invokes in-memory APIs and discards returned
actions. It never runs TerminalUI's dispatch loop, providers, workflows or editors.
See its [plan](../papers/tui-perf-fuzz-2026-10-15/fuzz/PLAN.md) for oracle limits.

Prepare the remote `fuzz-build/tui_fuzz` and `afl-tools/bin/afl-fuzz`, then start
through the owned Python launcher:

```sh
python3 scripts/start-tui-fuzz --remote-root /absolute/owned/campaign \
  --source EXACT_COMMIT_OR_RECORDED_OVERLAY \
  --manifest context/tui-fuzz-NEW/launch.json
```

The launcher persists a unique run identity before SSH, then launches two user
services with at most one CPU and1GiB resident memory each, low CPU/I/O priority,
and an initial10million executions per mode. Each output/seed directory is unique.
It confirms initial fuzzer-state creation, records source/binary/tool identities
and unit names, then exits. An uncertain launch is reconciled by that saved
identity instead of starting duplicate jobs. Jobs stop at their finite execution
counts and retain outputs; closing SSH does not stop them.

The shared host's apport core pipe is unchanged. Per-service core limit is zero;
the launcher explicitly sets AFL's core-pipe acknowledgment and records the
actual environment. Coverage-guided executions and sanitizer assertions do not
promise every possible crash disposition. CPU-frequency/affinity checks are
disabled for this low-priority shared-host work, without changing the governor.
The campaigns are not performance measurements.

Operator's latest instruction is explicit: after the Python launch, leave AFL
alone. Do not inspect cases, poll findings, minimize inputs or repair product
source during this run. AFL retains crash/hang originals and its native queue
for [collective bug work](../docs/TUI_BUG_QUEUE.md). No background triager or
automatic repair agent is installed.
