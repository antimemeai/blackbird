# Retained raw channel implementation

Implement the approved publication/reclaim unit: three independent variable-byte
partitions in named memory, one producer execution owner, one externally fenced
recorder. Complete raw envelope/body publication precedes a release cursor; only
disk-preserved complete record boundaries advance the release reclaim cursor.
Power loss is outside this contract. Mapping destruction does not unlink.

The source-grounded transport study is
[the retained-transport report](2026-10-10-raw-transport-design-study.md).
Lamport's complete-element publication and V7's finite-stream backpressure drive
the cursor and capacity rules. djb's retained launcher descriptors explain why
wake lifetime and process-death watching are separate. The armed exchange
handshake avoids idle spinning and lost notifications; one full-duplex Unix
socketpair per context carries both directions. Failed notification leaves the
publication intact and returns a separate status, never an uncommitted retry.

Safe-boundary reservation prepays a synchronous burst. Move-only control claims
hold capacity across yields without reserving physical byte positions. Emit
copies exact native fields/raw parts without allocation, waiting or locking.
Claim/token lifetime belongs to the execution owner, outside product locks.

Direct checks compare independently assembled envelopes and distinct raw
bytes across wrap, fullness, held credit, independent partitions, second-address
mapping, publication/reclaim, process death and both notification directions.
The parent owns integration, runtime admission/death watches, recorder disk
preservation and the final source gate. No reference implementation is executed
or adopted.

LLVM23.1.2 MacARM strict warnings, selected repository clang-tidy checks and one
focused ASan/UBSan execution pass. The model checks 5,000 variable-length events
against independently concatenated source envelopes across all partitions;
10,000 concurrently published/reclaimed events check exact bytes and ordinals
through a second mapping. Socket tests cover forward/reverse armed wakes,
full-buffer EAGAIN, hard peer failure and retained publication. Separate process
exits after publication and after recorder-private copy leave the named object
and original bytes recoverable with D unchanged. No full suite or mutation run.

The first direct run found Darwin reports page-rounded named-object backing
size (16KiB pages on this host) after a smaller ftruncate. Opening now validates
the native backing size separately from exact registered logical header size.
Static review also moved the ready-word check ahead of every non-atomic metadata
read. clang-tidy identified interchangeable endpoint/ordinal constructor scalars;
explicit restored starts now use named `RawProducer::Start` fields.

Caller obligations remain explicit: one fenced publisher and recorder, stable
mapping/producer lifetime, independently registered object discovery, initial
ordinal selection for any restored producer, and no sleeping while holding
product locks. Runtime drains old socket notices before arming, rechecks all
three partition cursors/credit, and watches producer/recorder death independently
of endpoint HUP. This unit does not establish durable disk commit or qualify
host/power failure. Cursor overflow is rejected in source; this focused test
checks ordinal/reservation overflow but does not pretend to execute 2^64 bytes.

Finding-driven remediation closes new credit admission conservatively before
counter exhaustion. Each partition must have at least its full capacity left
before byte-cursor wrap. The ordinal must have room for the sum of
`floor(capacity/16)` across all three partitions. That covers every already
reserved or held minimum-size event; admission failure does not consume their
credit or prohibit those prepaid emissions. The execution owner stops new
acquisition, completes held terminal work, drains and registers closure, then
registers a distinct named lane/epoch. It never resets live counters or silently
reopens the old epoch after draining. Direct near-boundary ordinal starts check
that new reserve/hold fail while two previously held terminal events still emit
unchanged. Exhausted starts cannot acquire ordinary or held credit. A zero-site
rejection leaves publication/reclaim, ordinal and prepaid credit unchanged.
