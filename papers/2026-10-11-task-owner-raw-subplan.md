# Task owner emission

Reviewed owner subplan; source and direct checks are implemented, with the ONE
code/oracle attack completed with finding-driven fixes and direct rechecks. Covers census30–31.
Task activity binding32 belongs to its execution owner and is not completed by
these task-state hooks. Read tasks.hpp/tasks.cpp, context.cpp's AuditLog::record,
packet.hpp/value.hpp, existing task tests and the approved raw transport design.
The older raw-emission study supplies the capture/recorder separation; no library,
new transport or metrics transformation is proposed.

TaskState owns native revision/next, item/order/receipt container sizes, five
maintained status populations and rollups. These are product state. Copy those
native fields at the existing sole-owner boundaries; never walk tasks merely to
compute a metric. TaskState::prepare already encodes the request to enforce its
65536-byte business limit. Retain that existing string in a local, copy it once
to the raw lane immediately, and use its existing length for the original
business check. Do not add a second encoder or turn Value into a metrics model.
Early key/type failures precede that existing encoding and therefore have no
encoded-byte record; their exact native failure and reached stage are recorded.

TaskStore::edit stages a delta, appends the product packet, transfers preallocated
state, then invokes changed. A duplicate does not append or commit. Conflict
returns a native business result rather than throwing. Display callback failure
is intentionally swallowed after accepted work. State::replay reconstructs old
work and must not claim a fresh user edit; restore publishes a staged candidate.

## Events and identities

Reserve1700–1729 after checking descriptions. E16 is the native envelope; R24 is
actual lane16/ordinal8. An explicit parent R links edit→prepare/commit/callback;
absence is a native bool and carries no fabricated reference. No global counter,
clock, formatted task identity or callback registry. New trace state stays on
the existing owner stack; a Prepared may retain its actual prepare R through its
existing move until commit. Description publication precedes activation.

| Site | Native body and boundary |
| --- | --- |
|1700 request|u8 action,bool parent present,[R],u64 revision; before prepare/read/snapshot/restore/replay/edit/commit work|
|1701 request bytes|prepare R,size_t length,exact existing business-encoded request bytes; before business encoded-size refusal|
|1702 prepared|prepare R,u64 next revision,u64 next ID,size_t changed,size_t removed,bool order present,[size_t order size],size_t receipts,size_t[5] populations; after staging, before publication|
|1703 duplicate|prepare R,u64 current revision; at the existing identical receipt branch|
|1704 publication request|edit R,prepare R,actual ApplicationRecordId16; before actual AuditLog::record|
|1705 publication return|publication R,u8 return branch,[ErrorCode,i64 detail]; exact actual append result|
|1706 state publication return|commit/restore R,u64 revision,u64 next,size_t item/order/receipt sizes,size_t[5] populations; after the actual commit or restore state transfer|
|1707 read return|read R,u64 revision,size_t total,size_t[5] populations,bool revision conflict; successful page additionally u64 offset,size_t cursor,size_t returned rows, copied from existing native locals|
|1708 snapshot return|snapshot R,u64 revision,u64 next,size_t rows/order/receipts; after the existing projection is assembled|
|1709 callback request|edit R; before the actual changed callback|
|1710 callback return|callback R,u8 returned/Error/allocation/unknown,[ErrorCode,i64 detail]; preserves the existing swallow behavior|
|1711 terminal|request R,u8 returned/duplicate/conflict/Error/allocation/unknown,u8 reached stage,[ErrorCode,i64 detail]; held outcome, including failures|
|1712 operator request|size_t command length,existing command bytes; before external JSON adapter/business parsing|
|1713 operator return|operator R,u8 actual return/error branch,[ErrorCode,i64 detail]|

Action tags distinguish live prepare from replay prepare, and live commit from
replay commit. Explicit parent references are supplied by actual callers, not
inferred from the nearest event. Native task IDs and op_id strings already
retained in the existing request/delta are interpreted by consumers; no writer
dictionary or metrics-only parsing of Number.text(), read results or receipts.
The operator's existing external text is captured as bytes, including invalid
JSON. Snapshot/read Value results are not re-encoded for metrics.

## Credit, lock and lifetime

Compute all bodies using sizeof before implementation. On the selected64-bit
profiles request1700 is26 without a parent or50 with one;1702 max129,1706 is120,
1707 max121,1708 is80,1709 is40,1710 max50,1704 is80 and1705 max50.
The maximum1711 is51
(E16+R24+two u8+ErrorCode1+i64 detail8). Each request prepays its own terminal and
all finite post-effect outcomes before effects. Edit additionally prepays1704,
maximum1705, parented commit request/return/terminal and the possible callback
pair before append:51+80+50+50+120+51+40+50=492 held bytes after BEGIN.
Pass the existing commit sub-bank221 by split to the actual commit owner;
it does not acquire an additional fresh claim. Independent native commit callers
prepay their own request/return/terminal before changing state. There
is no admission after durable acceptance merely to record its result.
Commit itself is nonallocating in the current implementation; preserve that.
The ONE code attack found that its by-value Prepared argument nevertheless
constructs another deque before entry and allocates on libstdc++13. Consume
Prepared by rvalue reference, without constructing a replacement container after
append. Replace the hex-only allocation cut with refusal of every allocation in
the actual accepted-task-cursor/revision0 interval. Pre-issue the existing ID
reservoir before arming that interval so its own reservation append cannot be
mistaken for task acceptance. The supported Linux red precedes this correction;
the direct recheck must show accepted task, revision1 and callback1 without any
allocation in that interval. Replay uses the same consuming reference boundary.
Nested preparation and projection claim before their own acquisition, outside
product locks; held caller outcomes cannot be consumed by nested emission.

Resolve the two findings in the ONE task plan attack as follows. At edit acquire
the existing business ApplicationRecordId before publication, then use the
existing void AuditLog::record(identity, program, packet) overload. This omits
the discarded post-submit hex_identity allocation that can leave an accepted
delta with an uncommitted working state. Copy the actual16-byte identity in1704;
the revised492 post-BEGIN bank includes it. Do not introduce another identity.
Force that real post-submit allocation refusal against the baseline, then check
that the candidate has no such allocation interval and commits the accepted
delta before the callback. Keep a separate genuine pre-append failure case.

Finite whole CONTROL claims, including BEGIN: live prepare206(parented230),
read198(parented222), snapshot157(parented181), commit/restore197(parented221),
edit518(parented542). Replay298(parented322) holds its own BEGIN/terminal77
and its final parented commit221 before reconstruction; its parented prepare230
admits before encoding, before any replay state effect. Pass explicit replay
mode and parent into that prepare and commit. No live edit publication/callback
is emitted by replay. Restore prepays197/221 before staging and publishes1706
directly after its actual *this candidate move, copying native resulting fields;
it does not call snapshot or parse a Value merely to make a metric. A fully
constructed trace owns outcomes before BEGIN so later failed admission cannot
bypass its terminal through a throwing constructor.

Before the existing prepare encoder, hold ValueLimits{}.bytes+48 data bytes
(16MiB+48 on this source), then encode once, copy its actual returned string
and release unused capacity immediately after copying, before later staging or
nested work. This is a conservative transient allowance demanded
by the existing codec bound, not a new16MiB allocation or task limit. Failed
encoding has no successful source string. Fixed outcomes remain prepaid. Oversize
raw capacity refuses before encoding; oversized task65536 source is preserved
before its business refusal. Ordinary encoder failures must not invent bytes.
Measure this allowance's actual contention in the workload unit; do not add a
metrics-only source-size traversal to shrink it.

Raw disabled preserves existing behavior and does no extra encoding. No native
clock, timer, duration, aggregate or serializer is introduced. The current sole
owner and immutable span lifetimes suffice; no metrics object enters TaskState's
long-lived item map. Callback reentrancy cannot consume prepaid outer credit.

## Direct qualification before completion

Initial red runs actual edit/read/operator tasks under a real RawRuntime and
requires the absent sites; it must fail on missing events, not an unrelated
fixture setup error. Independent expected bodies use native fixture inputs and
actual returned lane/ordinal references, with literal packet bytes for the small
business-encoded request. Check first edit, duplicate, stale base, reused op_id,
invalid key/type, over65536 encoded source, page conflict/filters, snapshot,
restore and replay; replay records reconstruction without fresh edit events.
Force actual retained append failure before commit and changed callback failure
after commit, preserving their different resulting revision and native errors.
Fill the lane with recorder stopped, sticky-cancel fresh work before effect, and
exercise prepaid post-append outcomes without fresh admission. Existing task
fixtures remain the business oracle; preserved raw bodies are a separate direct
source-to-disk oracle. One plan attack precedes source; one code/oracle attack
follows green, then findings and direct recheck. No new assurance panel.
