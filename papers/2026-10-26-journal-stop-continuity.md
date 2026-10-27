# Journal stop/settlement continuity (arconaut-8zr)

## Purpose and source grounding

Finish the ordinary journal append's SIGTERM boundary without changing context
composition. Baseline 5b1ff1d. The retained 8zr note and
`context/clm-rd7-notes/8zr-runs/race-diagnostic.log` in the main repository show a
CPU livelock in SessionStore::restart -> FramedJournal::write_exact: raw admission
reports sticky interrupted/ECANCELED and the writer retries it as EINTR.
Historical failed runs remain untouched. This is not a host-load timing claim.

POSIX.1-2024 write/pwrite specifies EINTR before any bytes and a positive count
when interrupted after writing bytes:
https://pubs.opengroup.org/onlinepubs/9799919799/functions/write.html
POSIX fsync says failed synchronization does not guarantee completed outstanding
I/O: https://pubs.opengroup.org/onlinepubs/9799919799/functions/fsync.html
Acquired originals are in ignored context/continuity-research. Current native
storage maps only native EINTR to interrupted/EINTR; raw wait admission uses
ECANCELED or ETIMEDOUT. Those are not interchangeable.

## Contract and short subplan

1. Before an ordinary append's physical mutation, reserve its raw physical
   extent/write/sync capture using the existing finite storage settlement loan.
   Do not mask cancellation, drain its fd, invent successful persistence, or
   reacquire raw capacity after the first write. Preserve existing outer loans.
2. Cancellation refusing this reservation leaves the writer live and unchanged.
   Once funded, finish the batch and sync despite latched cancellation. Preserve
   the cancellation for the enclosing owner to stop afterward.
3. Continue positive short writes at their exact offset. Give the ordinary
   append a finite extra native-call allowance (eight) for short writes/EINTR,
   with original input references rather than copied suffixes. Existing finite
   terminal/exec loans retain their current bounds. Only native EINTR is retryable;
   repeated nonprogress is bounded. Exhausted credit, zero writes, native I/O or
   sync failures poison the writer and preserve an uncertain tail for explicit
   recovery; no success locator or effect replay follows.
4. Direct deterministic native tests latch cancellation at physical mutation,
   after a positive short write, and before sync; check committed bytes after
   reopening, untouched sticky cancellation, and no unfunded claims. Direct
   memory tests cover non-EINTR interruption, bounded EINTR and poisoned-tail
   settlement. Establish old-behavior RED before product changes. Existing
   clean_stop SIGTERM/restart and journal recovery tests provide integration.

## Bounded hardening/resources

Layer 1: these concrete repairs and RED/GREEN, then one independent fresh-context
Sol 6.1 medium review of exact source/diff/oracles. Layer 2: one recheck and fixes
of valid findings, with affected direct reruns only. Never a third review layer.
Serial targeted debug build/checks, affected Linux debug/ASan if available, required
full native source commit hook and release blackbird build are the allowance.
All costly commands and source commits use the campaign build slot synchronously.
No historical load reruns, mutants/fuzz, dependency adoption, installed runtime
replacement, or general command-jobs work. Linux ASan busy is separate unless it
blocks this unit. No vanished retirement archive is needed by this design.

## Results and existing layer-2 disposition

The predecessor's `continuity-run/red.log` records old retry behavior failing the
bounded-call oracle and native cancelled write/sync. `final-red.log` records exit 1
for both final direct selectors against the baseline journal writer; the candidate
writer was restored afterward. Initial GREENs in `green-direct.log` cover native
full/short cancellation and reopen, restart-once, restart-plain, SIGTERM while TUI
running, and twenty SIGTERM/restart races. No historical failed result was deleted.

The independent source review is retained verbatim in
[the review report](2026-10-26-journal-stop-review.md). The provider completed; the
local workflow's later finalization error is not a failed/missing provider review.
No consequential product defect was established. Within the same second layer,
the native fixture now covers cancellation before funding, native EINTR after
cancellation, nine finite positive short writes followed by capacity refusal, and
original-input/suffix references at exact progressed offsets. The early recheck's
provenance oracle incorrectly used DATA/CONTROL transport order as native call
order. Requests now use their producer's shared per-lane ordinal. Failed
`recheck-native.log` and `recheck-native-fixed.log` remain preserved;
`recheck-native-ordered.log` has no collected terminal result and is not called a
pass merely from fixture output.

A fresh-session successor continued this same unit after the predecessor reached
its configured 64-step workflow budget, not a source regression. It did not reopen
the old session, replay unknown effects, launch another review, or reset hardening.
The explicitly selected campaign `autodev.lua` removes the request-count cutoff;
it is outside this unit's source changes.

New explicit native qualification in `continuity-followup-01/native.log` finished
with exit 0 for `storage_raw_test journal-cancel` and `journal_batch_test`. Linux
qualification used `scripts/check-linux --profiles debug asan --targets
journal_batch_test storage_raw_test --test-regex '^(journal_batch|storage_raw)$'`.
Both cases passed in both profiles, including the full native storage suite:
debug 0.04s/56.83s; ASan 0.06s/25.31s. Exact output and host pressure are retained
in `context/linux/run-ozpfb14i/checks.log`; dispatch/result is in the successor's
`linux.log`. The old remote interrupted compile is not counted as qualification.
All heavy operations were synchronous children of the campaign build-slot wrapper.

Limitations: this unit does not add a native failed-sync-under-cancellation
injection or an interruption-during-reservation fixture. Existing journal failure
oracles check sync failure, poisoning, no publication and explicit recovery; the
new native cases check funded synchronization and sticky cancellation. No claim of
complete syscall-cut coverage, host power-loss safety, installed-runtime activation,
or repair of the separate Linux command-jobs busy finding follows. Mandatory full
source-hook and release/publication outcomes are recorded in the successor handoff.

### Consequential source-gate findings (same layer 2)

The first mandatory source-hook attempt did not commit: 285/297 tests passed,
12 failed, and lint was not reached (`source-commit.log`). Two directly affected
retained-storage oracles counted all physical scopes as a stop/receipt scope;
ordinary append prepayment now legitimately creates separate setup scopes. They
now resolve each scope's actual prior same-issuer grant, require only ordinary
append plus the expected stop/receipt kind, and still require exactly one actual
stop/receipt scope. They do not permit a second stop/receipt allowance or change
production limits. The initial failed hook is their direct RED; both owning cases
passed after the oracle correction in `gate-findings.log`.

Eight failures required the already adopted Microsoft development tool, absent
from this worktree. Explicit `scripts/tui-test --install` provisioned the existing
pinned release (not a new dependency). The install transaction fixture inherited
`BLACKBIRD_EXECUTABLE` from the campaign's running session; qualification now clears
only session launcher overrides, not credentials or test rules. The remaining
command-jobs PTY episode had failed to launch its final pipe child after the
pressure episode; the focused unchanged case passed (13.11s), with no suppression,
timeout change or unrelated source repair. The same focused operation passed
`raw_retained_native`, `raw_retained_caller` and `install_blackbird` (4/4, exit 0).
A full unsuppressed source hook is still required before publication; failures
are not erased by the targeted results.
