# Raw region owner hooks — implementation unit

2026-10-11; implements the approved raw design in
`docs/INSTRUMENTATION_RAW_DESIGN.md`. This note describes source bodies, ownership,
and the direct oracle. It does not authorize activation or select metrics,
operations policies, or evaluators.

The former debug-only `LocalTimingSink`/`LocalSpan` aggregation and borrowed sink
are removed from this unit. `RawRegion` writes separate raw endpoint, observation,
linkage, branch-outcome, and original-Error records. It computes no timestamp
scalar, elapsed duration, histogram, count, sum, metric, normalized usage, or
logical clock. Definitions are immutable source constants; no emission performs
string lookup or dynamic registration. Their catalogue is runtime setup's owner.

## Source identifiers

Each row gives BEGIN, END, OBSERVATION, LINKAGE, OUTCOME, in that order.

| IDs | Owner / boundary |
|---|---|
| 1000,1001,1002,1003,1004 | main startup initialization; root owns runtime/startup placement |
| 1010,1011,1012,1013,1014 | main retained startup restoration |
| 1020,1021,1022,1023,1024 | terminal first successfully flushed frame |
| 1030,1031,1032,1033,1034 | terminal worker perform call |
| 1040,1041,1042,1043,1044 | terminal frame preparation/output attempt |
| 1050,1051,1052,1053,1054 | terminal input handling/persistence attempt |
| 1060,1061,1062,1063,1064 | coding command admission |
| 1070,1071,1072,1073,1074 | coding request preparation |
| 1080,1081,1082,1083,1084 | coding provider respond invocation |
| 1090,1091,1092,1093,1094 | retained append attempt |
| 1100 | UI owner worker dispatch |
| 1101 | separately bound worker entry, referencing UI dispatch |
| 1110 | original native Error associated with a region |
| 1200 | prepared native request bytes, before provider dispatch |
| 1201 | CodingProvider owner dispatch, before respond acquisition |
| 1202 | decoded response observed, before capture flush or response import |

1201 is not evidence of adapter wire send. The external provider adapter owns its
actual request-send and stream-byte sites. Request and response are distinct
source records, not two roles within one aggregate SDK object.

## Native bodies

The common envelope is channel-owned: native u32 record byte length, u32 static
site, u64 lane ordinal. The lane identity and byte-position/partition are recorder
origin metadata. Ordinals identify source records; they are not logical clocks.
Bodies below concatenate native fields without struct padding. String/byte tails
have native `size_t` lengths; neither truncation nor formatting occurs. Consumers
must use the catalogue's ABI description.

`ClockFields` means, in exact order:

1. monotonic clock `clockid_t` method, native `int` syscall return, native `int`
   errno, native `time_t` `tv_sec`, native `long` `tv_nsec`;
2. native `bool` CPU-read policy;
3. thread-CPU clock method, syscall return, errno, seconds and nanoseconds with
   the same native types.

The fields preserve results without subtraction, scaling, regression repair or
clamping. `errno` is copied unchanged; it is meaningful as a clock-call error only
when that call's return indicates failure. A false CPU-read policy identifies the
zero-initialized CPU fields as unobserved, not as a zero CPU duration.

| Site class | Exact body order |
|---|---|
| BEGIN | ClockFields; native int uncaught-exception count |
| END | u64 BEGIN ordinal; ClockFields; native int uncaught-exception count; native `RawRegionEnd` (u8) |
| OBSERVATION | u64 BEGIN ordinal; `RawObservationKind` (u32); native size_t history; native size_t bytes; u64 sequence |
| LINKAGE | u64 BEGIN ordinal; native size_t revision byte count; exact revision bytes; native size_t attempt byte count; exact attempt bytes |
| OUTCOME | u64 BEGIN ordinal; `RawRegionOutcome` (u32) |
| 1100 | ClockFields (CPU-read policy false) |
| 1101 | UI lane identity (16 bytes); u64 UI dispatch ordinal; ClockFields (CPU-read policy false) |
| 1110 | u64 BEGIN ordinal; native `ErrorCode` (u8); native i64 original detail |
| 1200 | native size_t context-revision length; exact revision bytes; generation identity (16 bytes); native size_t prepared-request length; exact prepared BBM2 bytes |
| 1201 | attempt identity (16 bytes); invocation identity (16 bytes); generation identity (16 bytes); native unsigned retry ordinal; native size_t retry-group length; exact product-owned retry-group bytes |
| 1202 | attempt identity (16 bytes); invocation identity (16 bytes); generation identity (16 bytes); u64 transport BEGIN ordinal |

The request BBM2 encoding in 1200 is the encoding the product already computed to
populate its request-byte presentation. The hook reuses that string; it introduces
no additional Value encode. 1200 is a native prepared request, not the adapter's
wire representation. 1202 references the actual retained attempt/invocation and
original stream; it does not serialize a returned Value, project usage fields,
or claim an imported response succeeded. Stream source bytes carry the original
reported usage for subsequent consumers.

Outcome values are explicit owner branches: ready=1, compact=2, full_replay=3,
admitted=4, success=5, rejected=6, uncertain=7, returned=8, exception=9,
flushed=10, small_frame=11, queued=12, dispatch=13. Scope exit has no inferred
success. `RawRegionEnd` is explicit_finish=1 or scope_exit=2. Observation kinds
are request_bytes=1, source_bytes=2, encoded_event_bytes=3, journal_state=4.
Retained observations copy each existing object's size separately, replacing the
former additional byte summation. Existing business calculations remain product
semantics and are not new raw instrumentation calculations.

## Ownership and admission

The region is noncopyable/nonmovable and belongs to one bound producer for its
whole lifetime. Before BEGIN, it holds control capacity for END, one branch outcome,
and one Error record. Claims reserve capacity, not physical byte positions. An
explicit finish publishes once and returns unused held capacity. Repeated outcome
or Error publication is an owner protocol conflict and fences admission; it is
not silently accepted as a new branch. Previously held terminal credit remains
usable after the runtime fences ordinary admission.

Ordinary data admission now uses `raw_wait_data` at the existing owner safe
boundaries. BEGIN's exact framed cost is admitted before clock acquisition;
observations and linkages admit their checked native-field/tail framing cost;
worker dispatch/start admit their exact cost before clock acquisition. Provider
facts without held control credit admit their exact framing cost before emission.
For runtime-owned contexts the seam waits for reverse-wake reclaim; directly
borrowed test contexts return capacity instead of blocking. Admission failure
writes the original Error into the independent failure partition and fences
ordinary admission before throwing. Scoped hook errno restoration covers both
successful clock acquisition and admission/emission exceptions.

The availability check suffices because the bound producer is the sole execution
owner and no nested emission intervenes between successful `raw_wait_data` and
`raw_emit`. The final emit itself remains a bounded immediate copy and reservation;
it never waits. Clock acquisition calls the owned native function directly. Production has no
clock function pointer or event callback. The direct test replaces that one
function at static link time; native acquisition has a separate real-clock test. Observation/linkage fields are already owned and contain no capture
callback. No product lock is held at these waits. Frame-length checks are transport
admission costs, not metric accumulation or payload interpretation.

Command admission linkage precedes authoritative retained append. Retained append
observations precede journal append; append outcome and END use held control
credit. Provider linkage and dispatch precede respond acquisition; fixed 72-byte
response-observed control credit is held before provider invocation. Response
observation and returned outcome precede import, so import failure does not
rewrite transport return into a provider exception. Provider exceptions copy
native Error fields when available and emit the actual exception branch. Unused
response credit is released before capture flush can perform a nested append.
Held Error, branch-outcome and END paths never wait and remain usable after an
ingest failure fence.

Terminal worker setup registers its owned lane before UI busy-state mutation and
thread launch. The worker binds its own pre-registered context on entry. The UI's
1100 and worker's 1101 reference lane identity plus dispatch ordinal; no region or
single-producer state is carried across execution owners. Region constructors
examined here are outside product mutex scopes. Observation waits are at the inspected pre-effect safe boundaries. The inherited debug-only source-site census does not imply all future
product surfaces are implemented by these ten region definitions.

The known ordinary-data capacity gap at these source sites is closed by the
safe-boundary wait plus sole-producer/no-intervening-emission proof. Activation
remains contingent on root validating the complete runtime failure-custody,
recovery and recorder lifecycle contract; this unit alone is not that validation.

## Direct oracle

`tests/local_timing_test.cpp` binds actual `RawProducer` instances over actual
named `RawChannel` mappings. It constructs expected native bodies independently
of the production clock-body/envelope helper. Cases inspect exact data/control
bytes, including intentionally regressed wall-clock endpoints, failed clock
returns, unchanged errno, CPU absence, long and embedded-NUL linkage, original
negative Error detail, explicit finish idempotence and exception-unwind scope
exit without fabricated success. It checks no region-path allocations and no
clock/storage acquisition when disabled. Borrowed bounded-data admission refuses
before clock acquisition/effect, fences the original failure and releases all
constructor control credit. Oversized variable linkage admission publishes no
partial body and retains the prepaid region end. Exception and success paths
preserve caller errno even when the injected clock changes it.

Capacity cases fill ordinary data completely after region admission, then verify
exact outcome/END publication from held control credit; ordinary control cannot
steal that capacity. Insufficient control admission refuses BEGIN/clock/effect
and releases capacity. A real second thread binds a distinct producer and its
entry references the original UI lane/ordinal.

Local fake CodingProviders exercise actual CodingEngine request preparation,
provider success, terminal transport failure, retry and returned-but-invalid
response import. Oracles compare prepared BBM2 bytes with the observed native
request, distinct retry identities with actual retained attempt admissions,
request/response event cardinalities, actual branch outcomes and response BEGIN
references/order. There are no network calls. A first fixture failed because it
emitted invalid SSE before its intended retryable provider Error; the fixture was
corrected to valid partial delta SSE. Execution results are recorded by the root
coordinated test run rather than implied by this implementation note.
