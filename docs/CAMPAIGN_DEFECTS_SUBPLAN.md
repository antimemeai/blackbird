# Campaign defects: work note

Operator 2026-10-18: "let's fix those defects first, then if mimo needs more time it
needs more time w/e, 40 runs is good, 200 more is better."

The defects are the eight pre-existing beads filed by the C++26 qualification and its
autonomous campaign ([record](../papers/2026-10-17-cpp26-toolchain.md); traces under
`~/projects/blackbird_runs/2026-10-17-cpp26/`). None was caused by C++26. After they
are fixed, a second campaign of about 200 runs exercises the fixed build.

Branch `work/campaign-defects-2026-10-18`, stacked on `work/cpp26-toolchain-2026-10-17`.
Worktree `context/campaign-defects`. Each unit below has its own branch and worktree
under `context/defect-<bead>`, merged here when green. Hardening is two layers: the
fix with its direct checks, then one recheck. Master integration waits for the
operator's explicit go.

## Units

**A. Provider deadlines (`arconaut-pyg`, `arconaut-3nx`).** One owner, because both
come from the same deadline machinery.
- *pyg:* every provider request has one fixed 180 s total deadline. That deadline
  serves as curl `--max-time`, as the `Child` collect deadline and as the raw-capture
  wait deadline, and a timeout is retried as transient. Replace it with:
  - a stall timeout, meaning no stream bytes for N seconds while Blackbird is waiting
    on the stream. `Child` owns this check.
  - a large total ceiling.
  - Both are configurable on the command line for every provider.
  - A stall is retried. Reaching the total ceiling is not retried, because the
    identical request would hit it again.
  - curl's `--max-time` becomes a backstop strictly beyond Blackbird's own ceiling.
- *3nx:* when a raw-capture claim waits past the provider deadline, it records
  `interrupted/ETIMEDOUT` as a sticky capture failure. `provider_http_impl` converts
  that error to `provider_transport:28` but leaves the marker in place. After a
  successful retry, the enclosing bridge frame's `capture.require()` throws the stale
  marker. Fix: treat the provider's own deadline expiring during admission the same
  way `RawChildTrace::permit` already does (`raw_provider.cpp:169`). The provider
  request gets its own admission scope, and when it translates the expiry into the
  business deadline error, it consumes exactly that marker. Every other capture
  failure still propagates.
- *Direct checks:*
  - a stalled stream fails at the stall timeout, and the stall is retried;
  - a slow but steadily streaming response running longer than 180 s completes;
  - reaching the total ceiling is reported and not retried;
  - the 3nx regression: the deadline expires during an observer claim, the retry
    succeeds, and the turn continues to dispatch the tool call.
- *Defaults:* chosen from the measured stream timing of the 2026-10-17 runs
  (`STREAM_TIMING.md` in the campaign directory).

**B. Unbounded command output (`arconaut-3o2`).** One command's output filled
9.5 GB of lossless raw capture, and the capacity refusal then ended the whole turn.
Find where the exec output limit stops applying. Fix it so that:
- a command's output beyond its budget stops the command;
- the model receives a bounded tool error;
- the turn continues;
- capture stays lossless for every byte Blackbird actually read.

*Direct check:* a command that writes unbounded output ends with a bounded tool result,
its raw capture is bounded, and the next tool call in the same turn runs.

**C. Leaked test process trees (`arconaut-j1l`).** When a CTest case times out, its
descendants keep running (tui-test daemons, Blackbird children, forked helpers).
- Run each case so that its whole process tree dies with it, including daemons that
  call `setsid`.
- Fixtures stop the daemons they start.
- `rigor` reports orphans after the suite.

*Direct check:* a deliberately hung test with a detached grandchild leaves no process
after its timeout.

**D. Linker identity (`arconaut-e5g`).** Pin the Mac linker in the profile, and have
`rigor doctor` verify its identity.

*Direct check:* with Anaconda's `ld` first on `PATH`, the fuzz profile links using the
pinned Apple linker, and `doctor` rejects a substituted linker.

**E. Mac TSan cases (`arconaut-0zi`).** Nine cases fail identically on C++23 and C++26,
with no race reports. This unit runs after C, on a quiet machine, because orphaned
trees and load feed these timeouts.
- Decide each case from its actual failure: a timing oracle that does not scale under
  TSan, or a real defect.
- Investigate `raw_command_wake` exiting non-zero after its success lines.

*Direct check:* the full Mac TSan suite passes with no race reports.

**F. Provider settings (`arconaut-5st`).** Build a per-provider and per-endpoint
settings table from the current vendor docs, recording each doc URL and read date. It
covers:
- effort values and what they actually do;
- output ceilings;
- unsupported parameters;
- models that are unavailable on a plan.

Launch options are validated against the table. Upstream errors such as MiMo's 400
"Not supported model" reach the console with their message, not as a bare
`external_unknown (400)`.

*Direct check:* each documented provider rejects an undocumented effort and accepts
every documented one; a 400 with a provider message shows that message.

**G. Implicit copy (`arconaut-vv6`).** Make `CodingProvider` copyability explicit, and
enable `-Wdeprecated` if the tree is otherwise clean.

*Direct check:* the build is clean with `-Wdeprecated`.

## Resources

The laptop has 16 cores and about 38 GB of free disk. Neuroses has 48 cores and
1.5 TB free.
- Units A, B, C, D, F and G run in parallel worktrees on the laptop. Each builds only
  the targets it needs.
- Source commits go through the hook, one hook at a time (a lock), because
  concurrent full suites produce load timeouts.
- The merged branch then gets a full Mac gate and the `check-linux` Debug, Release and
  ASan profiles on Neuroses.
- Mutants are out of scope and never run on the laptop.

## Second campaign

After the merge, about 200 autonomous runs execute on the fixed Release and
ASan+UBSan builds, across luna, Paper (max and high) and MiMo. MiMo gets the time it
needs.
- Tasks run from fizzbuzz-trivial through useful work on this repository to
  needlessly hard problems.
- The trace archive stays complete. Each finished run's `session/` is packed
  losslessly with zstd after its derived views are made, and the pack is verified by
  a byte-exact round trip before the unpacked copy is removed. Packing measured 16x
  on `r-t03-mimo`, which keeps 200 runs within the laptop's disk.
