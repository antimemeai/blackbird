# Audit query owner: exact reads and actual results

Whole census53 query owner, excluding ContextStore's separately planned context
reads and the physical storage/acquisition owner. Source held for one plan
attack. Read src/audit.cpp, AuditLog declarations in context.hpp, CodingEngine's
three query dispatch branches, trajectory/audit fixtures, DiagnosticStore's
actual65536-byte read bound, retained fact/source APIs and raw claim/lifetime
contracts. Grounding is the approved raw design, studied BSD input/return split,
and native source/ref ownership. No new dependency, encoder, callback registry,
clock arithmetic, traversal, timer or metric counter.

AuditLog owns inspect, trajectory and variables-read. Move the existing
variables-read wrapper into AuditLog::variables with exactly its existing
copy/check/add-variables/trajectory behavior so failures before trajectory are
recorded by the same owner. Existing standalone public methods remain callable
without an operation origin. Add an optional actual OperationAttemptId argument
to the three methods; the existing CodingEngine body supplies its already
acquired attempt. Never look up an operation merely to populate an event.

## Native event layouts

Use2300–2349 after checking the registration table. E16 is the envelope, R24
is actual lane16/ordinal8, all native size_t/u64 values are8 on qualified hosts.
Native Error:u8/i64. Actual RecordReference is journal16/sequence8;
RetainedKind is nativeu16 and RetainedEvidence is nativeu8. RetainedFact does
not expose physical offsets/checksums. PhysicalJournalRecord belongs to its
actual storage owner (its native FrameKind is u16), not a new query lookup.

|Site|Actual body / boundary|Maximum|
|---|---|---|
|2300 request|method:u8,attempt-present:bool,[actual attempt16],root JournalCursor32,fact_count:size_t; before query validation|74 CONTROL|
|2301 parsed field|query R,field:u8,value native size_t/bool/OperationAttemptId as selected by static field descriptor; immediately after existing successful parse/default acquisition|57 CONTROL|
|2302 lookup request|query R,kind:u8,actual fact index:size_t OR attempt16 OR SourceReference24 OR RecordReference24; before actual fact/attempt/source/payload/diagnostic read|65 CONTROL|
|2303 lookup return|lookup R,returned/Error/allocation/unknown:u8; fact-success RecordReference24+RetainedKind:u16+RetainedEvidence:u8; attempt-success actual admission IDs and optional/native fields below; byte-success actual vector.size():size_t; Error has native Error only|106 CONTROL|
|2304 filter branch|lookup R,selected/skipped-variable/skipped-attempt:u8; at existing branch, including continue; no new count or repeated filter|41 CONTROL|
|2305 parsed variable|query R,size_t length,exact existing validated variable bytes; after existing variable copy/validation, before reads|48+64 DATA|
|2306 terminal|query R,returned/Error/allocation/unknown:u8,actual result-shape:u8; success fields described below; Error native Error, reached_stage:u8; after return Value construction, exactly once|82 CONTROL|
|2307 packet-helper request|kind:u8(read_packet1,decode_packet2),query R,actual RecordReference24,input size:size_t,ValueLimits bytes/nodes/depth:size_t each,bool prior read,[actual prior read R]|122 CONTROL; read_packet variant98|
|2308 packet-helper return|helper R,returned/Error/allocation/unknown:u8,reached-stage:u8,[native Error]; actual Result<Value> status, no Value projection|51 CONTROL|

2301 fields are end/count/scan/cursor/limit/record/source/offset, variables/packet
booleans, and the actual parsed attempt ID. Static field/method selects native
body, so no fabricated absent value, type tag dictionary or reparse. Extend the
existing index helper with a stack owner pointer and fixed field enum; publish
its actual returned size_t once, including existing defaults. The same existing
Number.text/from_chars pass remains the only parser. Invalid parse emits no
successful parsed value. Publish variables/packet only after real bool access,
and the actual attempted ID only after existing native from_bytes succeeds.

Terminal success shapes copy existing locals: page(end,next,rows.size),
hex-original(end,raw.size,offset,next,n), decoded-packet(end,raw.size,expanded.size),
trajectory(end,cursor,next,rows.size). No writer subtraction for scanned, no
second caught-up comparison, formatter or query-result packet encoding. Result
shape and native locals are staged in the stack owner as the business path
computes them. All Value construction/mutation precedes the returned terminal.
Error/unknown/destructor use held terminal and actual reached stage, never a
successful output body assembled from default zeros.

Native byte/read requests reference actual retained records/source dependencies.
Capture actual fact.record and evidence before summary formatting or packet decode. The
payload helper gets the actual selected record reference explicitly. Never
derive a physical reference from an application ID, output hex or cursor guess.
The attempt lookup's actual returned admission/flags are captured before its
existing hex conversions. Attempt success copies its three native admission
IDs, opened, observation-present,[actual phase/disposition], receipt-present,
[failure-present,[native Error]], actual evidence and reconciliation_required.
Its maximum106 is41+48+1+1+2+1+1+9+1+1; no derived terminal flag.
Every acquisition runs exactly as often as current business code calls it;
this plan does not claim existing repeated reads are one.

## Admission and lifetime

Add small raw_query.hpp/cpp typed stack helper and static descriptions, using
existing raw_emit/raw_claim primitives. No runtime registry or generic Value
capture. Request/parsed-field/terminal fixed bank is74+11*57+82=783 CONTROL
before validation;11 covers the larger selected method including wrapper and
native attempt fields. A trajectory owner additionally holds112 DATA before
its possible bounded parsed-variable copy. Unused grants release normally.

Each actual lookup holds its own complete subtree before the read: fact49+68+
optional filter41=158; attempt57+106=163; source/payload65+max50=115.
Diagnostic request additionally copies its actual metadata payload read R and
native already-computed now:u64:97+50=147. This references the exact retained
metadata whose diagnostic_file string was actually read, without guessing its
length or copying/reparsing it for metrics.
No product mutex protects these single-owner query methods. Per-read admission
may wait before acquisition; cancellation refuses the next read and uses the
already held query terminal. Prior read return/branch remains published.
There is no need to reserve256 reads up front or invent a complete-query credit
cap. A lookup owner lives until its actual result/selection branch is captured.
No callback, allocation or formatter separates acquisition and its raw return.
Nested metric emissions cannot consume held credit. Preserve errno around
instrumentation. Borrowed lane fixtures outlive AuditLog and all stack owners.

Raw-disabled inspection performs no registration, raw write or new capture run.
Live product queries emit their own query facts; a separate raw reader remains
only a consumer and never changes preservation or acknowledgments.

## Existing payload reads and packet results

All three summary reads are explicit: application log/program payload<=65536,
Decision continuation<=4096 and AttemptObservation observation<=4096. Their
query-owned read_packet helper preserves current size-check, immutable.read,
decode order. Prepay149 for its2307 request98 and2308 return51 before the
helper size check; each actual immutable.read separately holds115 and uses
that helper's actual R as owner parent. Publish its actual vector return before
decode. Stage0=size check,1=read,2=decode distinguishes a real byte failure from
successful bytes followed by corrupt decode. Both handled Decision/Observation
errors publish their actual helper failure before existing omission behavior.
Application errors still escape exactly as before. No second read or decode is
introduced by capture. No helper event projects a Value or measures it by a
second encoder.

inspect's existing payload(body) read is separate from summary's read; its
diagnostic branch reads the application packet once more. Preserve all three
existing calls and record each. Return the actual third immutable read R via
a native out parameter for the diagnostic request. Direct inspect decode_packet
has no new byte read: pay173 for2307max122/2308max51 and copy its already
available actual prior byte read R. Nonpayload body selection returns its
existing empty vector without manufacturing a native read. Keep existing size
and supported-body checks in their current order.

AuditLog::variables starts one method-specific owner, then performs its existing
query copy/check/add and calls a private free trajectory implementation with
that same stack owner. It never starts a second query or claims another783.
Standalone trajectory starts its own owner and calls that same implementation.
Read helper/fact branch credit survives nested fresh admission and exceptions.

## Original bytes and physical acquisition dependency

Ordinary retained payload/source bytes already have real immutable retained
references. Do not duplicate those complete bytes for a size/reference metric.
Diagnostic files are optional/expiring and read_diagnostic currently discards
partial bytes on error. This unit records actual diagnostic request/result and
failure, but does not pretend those partial originals survive. The physical
acquisition owner must emit each actual diagnostic read prefix before the next
read/close, from a preclaimed bounded read grant. That concrete overlapping
nls.10 defect remains open until the native storage owner is implemented and
its fault cut passes. Query and physical sites have distinct claims; no second
semantic Retain or metrics-only read is introduced here.

The existing inspect source/diagnostic path assigns raw=original and duplicates
the vector. Measure allocation/copy behavior with actual64KiB query before any
ownership change; if confirmed, moving that already owned vector is a confined
same-owner correction. Preserve the existing payload read/validation order.
Do not refactor unrelated business filters or expand query validity limits.

## Independent direct oracles and sequence

1. Genuine baseline public AuditLog trajectory/inspect/variables through current
   CodingEngine branch, real raw runtime/reader and independently authored native
   expected frames; missing2300–2306 is the red. Use actual retained source,
   application packet, admitted/open/terminal attempts and a pinned end cursor.
2. Exact native full bodies/parents for page, packet, hex source and trajectory,
   actual fact RecordReference/kind/evidence, parsed defaults/explicit parameters,
   variable and attempt filter selected/skipped branches. Expected frame order
   comes from known literal records, not candidate enums/decoder/description text.
   Return values stay equal to independently constructed existing business values.
   Unopened/open/running/terminal/failed-receipt/reconciliation attempt fixtures
   compare actual optional/native fields without a metrics terminal computation.
   Valid variable samples and handled corrupt Decision/Observation packets compare
   every actual read/helper request/result and the successful overall query.
3. Invalid object/key/number/bool/ID/range and missing/corrupt source or packet;
   require actual preceding parsed fields/lookups and one correct failure terminal.
   Allocation cuts at source return/summary/hex formatting/result construction
   distinguish acquired facts from a completed query. No success terminal before
   the actual returned Value exists; no second decode solely for the oracle.
4. Fill ordinary capacity with a real parked recorder: next lookup cancels before
   native acquisition, earlier exact returns survive, held owner error terminal
   is emitted and no semantic append occurs. Resume only the fixture's recorder
   for teardown. Reentrant fixture capture cannot consume existing held grant.
5. Reopen pinned history and compare stable native record/source references; readonly CLI
   inspection creates no new raw run. Real diagnostic missing/expired/error cases
   check actual now and metadata-read input plus native failures, including
   malformed filenames, but are not partial-original qualification.
   Native diagnostic partial read preservation stays the physical owner's cut.
6. Measure actual query allocation/copy work once; fix only demonstrated redundant
   ownership copies. Relevant debug/release/ASan and narrow owned C++ analysis,
   root ONE code/oracle attack, finding fixes/direct recheck. No eval definitions,
   trajectory feature work, logical-clock claim or second assurance panel.

Source windows: root owns raw_query/audit.cpp and the exact three dispatch call
arguments. Coordinate context.hpp signatures, raw descriptions and CMake with
the Context/provider/participant owners; never edit their active method blocks.
The existing whole-owner checkpoint must land before this source starts.

## ONE plan attack disposition

The independent report is [ONE plan attack](2026-10-11-audit-query-raw-plan-attack.md).
Q1 removes unavailable physical fields and fixes fact request49/return68/subtree158.
Q2 enumerates every current summary/payload/diagnostic read, separates actual
byte acquisition from handled packet-helper errors and fixes finite149/173 banks.
Q3 diagnostic input carries its real metadata read R and native now; no assumed
canonical filename bound. Q4 copies actual AttemptState optional/native fields,
with return106/subtree163, never a derived terminal. These resolutions precede
genuine baseline red and source. No new plan panel is needed.
