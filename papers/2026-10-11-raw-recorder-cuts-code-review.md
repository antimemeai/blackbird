# Recorder cuts: one code/oracle attack

2026-10-11. Source review of `tests/raw_recorder_cuts_test.cpp` against the reviewed
recorder-cuts subplan and the actual `record`, `open_audit`, `RawAudit::append`,
`NativeJournalFile::write_at` and synchronization paths. No source edits, new
panel, mutants, or repetition of the owning focused green (reported by root as
1.14 seconds). This is the unit's single code/oracle attack.

## Findings requiring fixes

1. **Failure teardown can leave stopped/parked processes alive.**
   `check`/`until` terminate the test with abort. In a death-cut case the
   supervisor is stopped before the at-cut assertions; a failure at those
   assertions bypasses SIGCONT and all normal destruction. The stopped
   supervisor cannot consume parent-lifetime EOF, and its recorder remains
   parked. The parking pipe has write endpoints inherited by recorder and
   supervisor, so parent exit does not itself provide EOF to the parked reader.
   `park` also retries EINTR indefinitely, even if normal recorder teardown sends
   SIGTERM/SIGUSR1. Native forwarders can abort on unexpected kernel results and
   take the same unclean path. Add test-owned cleanup of the actual supervisor
   and recorder on failed parent checks/watchdogs, plus a parking teardown path
   that can exit. Keep exact owned process IDs; do not kill unrelated processes.
   This is necessary for the subplan's cleanup promise and the local worker cap.

2. **Death-cut origin checks omit the selected partition.**
   The published-cut reader assertion near lines 364–365 and post-restart reader
   assertion near lines 377–379 check raw/lane, and the latter checks start 0,
   without requiring `origin.partition == RawPartition::data`. A valid-looking
   raw envelope attached to the control partition would pass those particular
   assertions even though the original was emitted from data. Require the full
   actual origin (reported lane, data partition, start 0) at the publication cut
   and after both restart cases. The I/O-error, competing-owner and fairness
   branches already assert that field. No new oracle machinery is needed.

## Checked native boundaries and independent expectations

The selected inode is established only after the actual empty 112-byte event
header and empty publication exist. `pwrite` targets offsets at or beyond 112;
unaffected writes forward through pwritev using the actual offset. The partial
cut requires the kernel to return exactly 19 before returning that count, then
injects EIO at the following append. It does not claim 19 for a different native
result. `fsync` and `rename` forward their resolved native ABI functions; symbols
are resolved before RawRuntime forks. `open_audit` selects SyncStrength::data,
which means actual fsync on macOS and Linux. No variadic fcntl interposition or
production synchronization substitution is involved.

The seven-byte NUL/high-byte literal produces E16+body7 = 23 source bytes.
RawAudit builds frame32+origin32+raw23+commit56 = 143 disk bytes. Expected failure
body fields are separately selected native values: io/EIO, append stage 1,
offset 112, written 19 or 143, sync_attempted false or true, actual initial journal,
actual source lane/data/start 0/end 23, acquired=true. Including E16 this is 103
bytes on the qualified ErrorCode 1/bool 1 ABI. These are independent semantic
inputs and the fixture's bytewise envelope assembler, not a call to the
production failure emitter or writer-mode audit code. The 19-byte tail is checked
against independently assembled magic/version/raw-kind/sequence 1/batch-first 1.

At every selected cut the actual application mapping requires P=23,A=0 and its
still-readable bytes equal the original ordinal 1 envelope. The death cases park
after actual configured sync or exact publication rename, before returning to
the recorder's subsequent step. Stopping the supervisor prevents restart from
racing those observations. Before-sync-return reader sees the old empty prefix;
after-publication reader sees the one original. Restart must expose one original,
reclaim A=23 and emit no synthetic process-death failure. Add finding2's missing
partition assertions to make that origin claim complete.

## Failure-slot qualification

The configured 111-byte lane holds one 103-byte failure and leaves 8 bytes. At the
fenced cut the actual failure partition requires P=103,A=0 with its exact failure
bytes; the original remains P=23,A=0. The selected native write/sync counters are
exactly 2/0 for partial write and 1/1 for failed sync. Startup's failed second
failure-slot grant therefore cannot append or synchronize another selected
batch. The source ordering additionally puts `admit_failure()` before opening
the native directory/audit and before RAM source reads. The direct fixture does
not claim a complete census of read-only native calls; its observed cut covers
append/sync acquisition and the actual startup ordering covers the earlier
read boundary.

Explicit recovery creates a fresh failure lane and retains the old lane. The
reader requires exactly one original and one exact old failure, each with its
correct origin; both old source partitions finally acknowledge their complete
prefixes. It correctly allows either cross-lane order: a complete failed-sync
commit can be recognized during open_audit before the old failure is drained.
The partial case retains events.0 byte-for-byte while successor recovery proceeds.

## Genuine competing waits and count fairness

Each owner first publishes its full 64-byte source record. The test then forwards
actual poll and signals its own handshake only for a nonzero-timeout POLLIN
SOCK_STREAM descriptor. Runtime's lane sockets are STREAM, control RPC sockets
are DGRAM, and the other runtime watch is a pipe. Within this fixture's worker
there is only its registered STREAM lane, so an RPC/gate poll cannot supply that
notice. Both actual mappings are required P=64,A=0 before CONT; both second
originals have exact ordinal 2/start 64/body and finally A=87. The ready flags are
not the wait oracle.

The hot lane actually holds 6144 literal23-byte records and cold holds its marker
before CONT; both P>A are observed. The actual preserved reader order counts hot
records before the cold marker and requires <=4096, while checking every hot
ordinal/start and the cold origin/literal. This matches at most two 2048-record
DATA batches when the paused tour position is unknown. All 6144 hot and one cold
are required in the final prefix. There is no time/throughput promise, production
tour callback or universal per-lane 256KiB assertion.

## Disposition

Fix the two findings in this same unit and run the focused finding-driven
recheck. No second code/oracle panel or review of this report is proposed.

Both findings are fixed. The fixture's actual initial supervisor and all its
recorder descendants share a private test-owned process group. Parent assertion
failure kills that group and joins the supervisor, removes only this fixture's
registered named mappings and directory; native abort also kills the owned
group. Normal completion clears the group only after RawRuntime joins it.
Children close their inherited parking write endpoint, and a parked reader
exits on EOF or interruption instead of swallowing teardown. Death-cut reads
now require the DATA partition and source start zero as well as lane identity.
The focused finding-driven recorder run passes in0.92s; its final clang-tidy
pass reports no diagnostics. This is the promised recheck, not another panel.
