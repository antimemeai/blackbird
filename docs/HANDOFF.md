# Watch handoff — 2026-10-14

2026-10-17 later update: Blackbird is C++26 (`CMAKE_CXX_STANDARD 26`) on the same
pinned compilers; see the [C++26 upgrade record](../papers/2026-10-17-cpp26-toolchain.md).

2026-10-17 update: the operator authorized C++23 and Neuroses LLVM23.1.4/libc++23.1.4.
Mac retains LLVM23.1.2. The earlier toolchain declarations below describe their
original snapshot; current profiles and qualification are in the
[C++23 upgrade record](../papers/2026-10-17-cpp23-toolchain.md).

The operator ends this watch and asks for plausible performance notes, matching
beads and a handoff. That work is complete; no optimization, source repair,
merge, executable activation, new perf round or continuing campaign starts here.
The next colleague should read this first, then the linked evidence and select
work with the operator. There is a harness to build; the optional tuning backlog
must not become a new prerequisite or endless qualification exercise.

## Read and orient

Read the canonical [BLACKBIRD.md](../BLACKBIRD.md), [AGENTS.md](../AGENTS.md),
[README](../README.md) and the latest [journal entries](../JOURNAL.md).
The repository doctrine file is a symlink to ~/projects/BLACKBIRD.md; preserve it.
~/projects must never become a Git repository. Use the live repo's beads, not
quarantined historical issues. Historical campaign timers and rejected designs
are not current instructions. Study the system, analyze the problem, plan the
fix, then execute. Do not patch blindly or add another assurance layer.

For this segment, read [instrumentation completion](INSTRUMENTATION_COMPLETION.md),
[repair results](../papers/2026-10-14-qualification-repair-results.md),
[performance assessment](../papers/2026-10-14-performance-round.md) and
[tuning notes](../papers/2026-10-14-performance-tuning-notes.md).
Older comprehensive research and approved design are linked from
[INSTRUMENTATION](INSTRUMENTATION.md). Preserve the approved raw architecture:
hooks emit original native fields/bytes, retained memory carries them away, a
separate recorder preserves raw disk records before reclaim, and a tertiary
process performs transformations. Operations and evals consume metrics.
No producer/recorder formatting, arithmetic, filtering, sampling, aggregation,
compression, new callback framework or timer. Process-failure preservation
matters; host power loss is excluded. Owned native machinery is the default;
reference study does not adopt a library.

## Source and publication state

Repo: /Users/patrickbeam/projects/blackbird. Current work branch:
**work/nls2-qualification-state**, pushed to origin. Remote is
https://github.com/antimemeai/blackbird.git. This handoff is a documentation-only
checkpoint after f250552; use git log/status for its final commit ID.

| Checkpoint | Meaning |
| --- | --- |
| 200fc5e | Named remaining instrumentation owners delivered and integrated on master; full Debug/source/Lua checks recorded. |
| 33e78d1 | Current master and origin/master: publication documentation atop that source. |
| 0423991 | First qualification results, including profile failures and pre-repair measurements. |
| ee4efc6 | Actual production File lifetime and Candidate include corrections, plus native test-fixture repairs. |
| 66547c6 | Repair report CSV normalization; source measured by the new performance round. |
| b6d5be6 | New full finite performance round, reports and missing File integration bug. |
| f250552 | Clarification of actual wiring, application event volume and authored-API measurement gap. |

**The qualification repairs and new reports are not merged into master.** No new
merge was authorized by the measurement/handoff requests. The work branch is a
descendant of master with seven changed source/test/script files from the repair,
plus reports/bead backups. Do not merge or activate a binary by implication.
No running operator executable was replaced. scripts/blackbird may select a
retained build/release/blackbird-ui for interactive sessions; explicit
BLACKBIRD_EXECUTABLE selection wins. Tests chose build/release/blackbird explicitly.

Before the handoff edits, the tracked tree is clean. Operator artifacts
**blackbird-art.zip** and **blackbird-art/** are untracked and must be preserved
and excluded from commits. One old AFK instrumentation stash and historical
worktrees remain; do not pop, remove or reuse them as current approved work.
The latest source and work branch are pushed; handoff commits also require push.

## What passed, and what that means

Named-owner source delivery previously passed **131/131 Debug tests**, all **194**
selected owned C++ translation units/static/format checks and the completed Lua
leg. That is the integrated source checkpoint's evidence, not a fresh whole-gate
run after every later edit.

The repair scope reran **14 affected cases in four profiles**, producing **56**
final passes: macOS Release and ASan+UBSan, Linux Release and ASan+UBSan. The
operator explicitly requested just impacted tests. Six changed C++ TUs pass lint/
format; changed Python syntax passes. No fresh entire Release/sanitizer suite or
131-test/194-TU gate was repeated. [Per-case results](../papers/qualification-repair-2026-10-14/affected-case-results.csv).
Seven defects are closed: **834, 90u, bho, iva, fir, 0oa, ji3**. Production changes
keep File's borrowed failure flag alive through publication and include cstring
for Candidate. The rest correct native fixture selection, ownership/allocation,
GNU long-CWD expectations and actual PTY readiness/saturation/byte oracles.

One post-repair finite performance round completed:

- 62 raw cases: 1,039,144 exact disk events, plus 10,000 in a separate Linux syscall
  trace. Grants, burst/paced/reader/full/refusal/resume and 128 MiB stream cases are
  explicit. Four allocation censuses count zero C++ new calls/bytes across 40,000
  claim/emission pairs; malloc, complete-owner allocations, copies/RMW/wakes are
  not inferred from that counter.
- 84 existing-path owner datasets, 8,400 calls, 458,952 closed raw records and 7,200
  selected exact originals. They include real File/operator/Lua/local mock-provider/
  Session/Station work. Actual nested File coverage fails as described below; the
  final datasets do not qualify missing hooks.
- 150 startup/input trials, all normal exits, plus two actual native command-
  control sessions. PID/launch/effect/input/resize/Ctrl-C/task/composer ownership,
  saturated background/stop, quit/reap/modes and every transferred keyboard byte
  are checked. This is one control session per host, not a tail distribution.

| Observation | macOS | Linux |
| --- | ---: | ---: |
| Fresh startup median / p90 | 124.849 / 157.626 ms | 31.458 / 35.529 ms |
| Reopen median / p90, 100 local tool calls | 145.414 / 223.347 ms | 173.807 / 193.596 ms |
| Fresh typing median | 2.643 ms | 1.216 ms |
| Launcher-grant 128 MiB preservation | 282.45 MB/s | 154.16 MB/s |
| Final-event observed ACK lag, same stream | 396.28 ms | 641.45 ms |

macOS 256-byte operator median is 33.593 ms capture-off versus 35.919 ms on,
with a 456.122 ms active maximum. Mock requests measure 51.677/61.410 ms; Station
8.840/10.094 ms with active p99 151.246 ms. Scheduling/business audit/recorder
causes have not been attributed. Host activity is uncontrolled; retain outliers.
Owner construction is excluded, and the owner driver uses 2 MiB CONTROL versus
1 MiB at launch. Prepaid emitter medians are not complete owner-hook costs.

Actual production wiring starts capture automatically. The real 100-call registry
session writes 91,183 raw events/13.52 MB event journal, mostly storage/catalog
records. Native control runs write 653,756/692,862 events into 115.77/122.38 MB;
routing site 2042 contributes 316,775/346,217 records. Those sessions include other
work, so do not report a pure keyboard amplification ratio. Native and Lua
authored APIs are wired and shipped turn emits step/call-count/restart metrics,
but custom performance sources do not call emit: inspected runs have zero
1510–1514 payloads. That API's complete cost is unmeasured by the last round.

## Open work and proposed starting point

**arconaut-4by (P1): missing File context at actual CodingEngine dispatch.**
src/coding.cpp:5230–5247 creates LocalTools, then calls tools.run(name, arguments)
without RawAdapterContext. The optional context defaults to null, so read/write/
edit skip nested File events while whole-call input/result/turn/bridge events
remain. Direct File probes supply contexts and pass. Study the actual operation
parent, credit and lifetime, plan propagation, and test actual operator/Lua/model
read/write/edit dispatch with independent bytes, distinct outcomes and relevant
error/cancellation/cleanup oracles. Do not call the existing-path perf passes
complete integration qualification. No fix was attempted during measurement.

New P2 investigation beads, with hypotheses and direct checks in the tuning notes:

| Bead | Question |
| --- | --- |
| arconaut-nls.11 | What causes complete owner waits and large tails? |
| arconaut-nls.12 | What costs recorder drain/preservation and production-grant page touches? |
| arconaut-nls.13 | How can the same high-volume events be produced more cheaply? |
| arconaut-nls.14 | Why does the Linux native local-history reopen cost rise? |
| arconaut-nls.15 | What is the complete native/Lua authored-emission cost? |

Begin the next operator discussion with the File wiring defect and select one
worthwhile perf question if desired. Do not start all five investigations at once
or optimize because the previous watch ended. Beads record work, not permission
to change audit semantics. Stream-specific efficiency remains **arconaut-bzn**;
its earlier optional originals/TTL/discard policy is superseded by complete audit.

**arconaut-nls.2 remains in progress.** Broader global SystemA/representation
lifetimes and measured capacity/census boundaries remain as documented. The named
owner segment's delivery did not close the whole issue. **nls.10 (P0)** still
covers complete audit of success/failure/acquisition/refusal and older material
outside the finite completed hooks; **nls.9** still covers logical causal clocks.
Lane ordinals and physical clocks are not that logical-clock design.
**nls.3** eval definitions/runs, further **nls.4/nls.5** trajectories, multiplayer
and steering remain deferred. An in_progress database status does not authorize
restarting a deferred campaign. The operator wants discussion before eval work.

Other project stock remains in live beads/current-state docs: provider credential
lifecycle uaa.5, residual historical operator recovery 05n, terminal feature os3,
phux nmm, foundation/qualification uaa/uaa.2/uaa.4, old Giga 7iy, startup development
probe 985, complaint pvl and formatting debt lh1. Older descriptions may contain
superseded scope/timers; current operator instructions and doctrine take priority.
Do not silently close or start those unrelated items during this handoff.

## Evidence and useful machinery

Detailed [raw report](../papers/2026-10-14-raw-transport-perf-round.md),
[owner report](../papers/2026-10-14-performance-owner-round.md) and
[interactive report](../papers/2026-10-14-interactive-perf-round.md) link to exact
executed source/runner snapshots and selected datasets. Tracked selections are
under papers/perf-round-2026-10-14 and papers/2026-10-14-performance-round.
Original native outputs, failed attempts, per-event/per-call samples, diagnostic
bytes and PTY output remain in ignored **context/perf-round-2026-10-14**.
Repair logs are in **context/qualification-repair-2026-10-14**; initial failures
in **context/qualification-2026-10-14**. They are local evidence, not present on a
fresh clone. Copy them if moving hosts rather than rerunning merely to recreate
files. Source snapshots/selected results remain versioned for reproduction.

Initial failed owner attempts retain raw audits but lack per-call CSV samples:
the early driver wrote samples after its oracle. Final successful cases persist
samples before the disk oracle. Source-grounded fixture corrections were actual
operator wrapper bytes, mocked native-result versus JSON diagnostic bytes and the
Station empty-steering separator. Earlier successes were not repeated for later
oracle changes. No successful sample or slow outlier was discarded.

macOS qualified tools: LLVM 23.1.2/libc++, Lua 5.4.8, C++20; presets use explicit
Homebrew paths and actual SDK. Linux: Neuroses over Tailscale SSH, Clang 18.1.3/
libstdc++ 13, existing digest-pinned Lua 5.4.8 source archive. Use at most six total
compiler/static workers and one Ninja writer per buildtree. Serialize native
performance work per host. No mutants on the laptop. No new libraries selected.
[Tooling](../tooling/README.md), [profiling](PROFILING.md) and existing scripts are
available; read them before running. Mac allocation attachment requires a profile
binary with the owned entitlement; normal Release lacks it. Linux strace/perf
were already available when measured; actual permissions must be checked.
Rhizome's research-watch path was unavailable; do not pretend it was read.

Useful read-only orientation commands:

```sh
git status --short
git log -8 --oneline
git rev-list --left-right --count master...HEAD
bd show arconaut-4by
bd show arconaut-nls.2
bd list --status open --limit 0
```

No owned native tests, compiler jobs or performance agents remain active. Linux
raw/owner/interactive archives are copied locally; only the owned temporary
/tmp/arconaut-linux-perf-roim27Vb was removed after capture. Earlier repair scratch
was also removed. Unrelated services, operator processes, old stashes/worktrees
and artifacts were left alone. Run only checks appropriate to future changes;
these notes get document/link/diff checks, not another full suite. Back up beads
with **bd backup**; never export into legacy .beads/issues.jsonl. On a fresh clone,
bd init plus bd backup restore recreates issue state. Automatic backup pushes
remain disabled; Git checkpoint/push is required. Source-gate exceptions used for
the operator's affected-only repair do not manufacture blanket future exceptions.
