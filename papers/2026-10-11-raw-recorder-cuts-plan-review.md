# Recorder native-cuts plan attack

2026-10-11. One focused design/plan attack of
2026-10-11-raw-recorder-cuts-subplan.md. Source read only; no build, source change,
new panel or repetition of settled channel/RawAudit models. The unit is useful,
but the synchronization and fairness claims need the corrections below before
fixture implementation.

## Actual synchronization is fsync on both profiles

raw_runtime.cpp open_audit explicitly passes SyncStrength::data to RawAudit::open.
journal_storage.cpp calls fsync for data on macOS and Linux. Only the separate
SyncStrength::full branch calls fcntl(F_FULLFSYNC) on macOS. The proposed recorder
F_FULLFSYNC cut would therefore never fire. Do not change production policy merely
to make a fixture fire; host power loss remains excluded. Target actual fsync on
both platforms and name the cut "configured synchronization," not full sync.

This also removes the need for a variadic fcntl wrapper in this unit. If retained
for a separately selected full-strength call, it must forward that actual call
without reading a nonexistent third argument. F_GETFD/F_GETFL/F_FULLFSYNC have
ignored argument forms; F_SETFD/F_SETFL/F_DUPFD/F_DUPFD_CLOEXEC use promoted int;
pointer commands require their actual pointer type. Unknown forms must stop the
fixture explicitly before va_arg, not fabricate a native unsupported return.
Pre-resolve the actual forwarding symbol before fork and use the SDK declaration's
ABI. Apple's primary [fcntl manual](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fcntl.2.html)
documents the ignored getter/full-sync arguments and pointer lock commands.
The installed SDK additionally declares the Darwin alias on fcntl; a guessed
untyped syscall shim is not interchangeable with that declaration.

## Exact native cuts

Filter by the actual event-file device/inode, selected append offset and explicit
fixture phase. Matching inode alone must not injure its initial112-byte header
write or fsync during startup. Matching published's pathname alone must not park
the initial empty-prefix publication. Establish the ready baseline first; arm the
selected cut for the one actual23-byte source envelope.

The literal7-byte payload produces23 source bytes. Its actual disk append is
32 frame header +32 origin +23 raw envelope +56 commit =143 bytes. Assemble those
expected fields independently. For partial write, forward19 actual bytes through
the native operation, require that actual return19, then inject EIO on the next
selected append write. Do not return19 if the kernel returned less. For the sync
failure, require actual append progress143, sync_attempted=true and native EIO.
The old published prefix remains the previous cursor. The kernel may retain the
complete143-byte append even though synchronization returned failure.

For synchronization-success death, forward the selected actual fsync successfully,
then report the cut and park before returning to RawAudit. For publication death,
forward the selected rename successfully, then report/park before returning to
record. The parent stops the actual supervisor before killing that parked recorder
so it cannot race an automatic restart with the P/A observations.

At EACH selected cut open the actual reported lane epoch and require P=23,A=0;
independently decode its still-held literal native envelope. For the publication
cut, the read-only published-prefix reader already exposes exactly that original;
for sync-success death the old published prefix still excludes it. After restart,
both yield one original and A=23. No fabricated failure or duplicate original.
These two cuts must not collapse into a late final-reader assertion alone.

## One failure slot

The current native failure envelope is103 bytes on the qualified bool1 ABI:
E16 +ErrorCode1 +detail8 +three u64(24) +sync bool1 +two identities32 +partition4
+source start/end16 +acquired bool1. A configured111-byte failure lane admits one
such event and leaves8 bytes, so it cannot prepay the second103-byte event. An
exact103-byte lane also isolates that claim. State which configuration is used.

After actual append/sync error, the old failure lane holds one8000 event and the
application source remains P=23,A=0. Automatic restart attempts its required
failure credit BEFORE open_audit/source reading, fails capacity, and exits70.
The supervisor then fences, with recorder0. Require no second selected append or
sync call, unchanged original P/A and unchanged failure P/A at that fenced cut.
This tests the before-acquisition refusal directly, rather than merely waiting
for a fenced status bit.

Disable the cut before explicit recovery. Recovery creates a fresh first failure
lane. The original failure remains a distinct registered lane and must be
preserved exactly once; the original application envelope must be preserved once;
both old source partitions finally satisfy P==A. The fresh failure lane should
emit no synthetic failure when recovery succeeds.

Do not require failure-before-original audit order in every case. A full-write/
failed-sync append can be a complete native commit on recovery; open_audit may
recognize and acknowledge that original before draining the old failure lane.
The partial19-byte case needs successor recovery and retains the broken tail in
its original segment. The expected set and each source's byte range are exact;
cross-lane append order depends on whether recovery discovers that complete commit.

## Fairness without a production tour hook

Use hot DATA only and one cold DATA marker. Keep both actual contexts alive and
registered, with fault injection disabled. Pause the recorder, establish actual
hot pending source and cold P=23,A=0, then CONT. The stopped recorder's tour position
is unknown: it may already have visited the empty cold lane in the current tour,
but still have the hot lane ahead. It can then read one hot batch in the remainder
and another hot batch in the next rotated tour before visiting the cold lane.

Therefore the schedule-independent bound is TWO hot batches before the first
cold marker, not one. With literal23-byte hot envelopes and2048 records per batch,
require at most4096 hot records (94208 hot source bytes) before the first cold
marker in actual preserved audit order. Prequeue at least6144 hot records before
CONT, with capacity sufficient for that source; a recorder that drains a hot lane
to exhaustion would exceed the bound. More hot input can be replenished by native
fixture handshakes, but no throughput/time threshold or tour callback is needed.
Check exact hot ordinals/source starts and the cold literal; final P==A proves
both actual source lanes were drained.

The256KiB read bound applies PER PARTITION, not per lane. record visits failure,
control and data separately, each with one2048-record batch. Its capacity fallback
also admits one larger record up to max_payload-raw_origin_size, so a universal
256KiB per-lane byte bound would be false. Selecting hot DATA with small fixed
envelopes removes both ambiguities. If hot control/failure are populated as well,
the corresponding two-tour count can reach12288 hot records and requires a
different explicitly enumerated oracle.

The two parked-owner wake test should keep the supervisor running and stop only
the recorder, prove both lanes actually full, and identify each actual lane's
space wait. A queued status-RPC poll or thread-start flag is not that cut. Existing
interposition must forward the actual poll; fixture handshakes can observe its
specific endpoint without changing production.

## Disposition

Proceed after editing the plan to use actual fsync, arm exact append/publication
phases, spell out single-slot pre-acquisition refusal, and replace unresolved
fairness with the4096-hot-record bounded-source oracle. These are corrections to
one direct qualification unit. No additional review layer is proposed.
