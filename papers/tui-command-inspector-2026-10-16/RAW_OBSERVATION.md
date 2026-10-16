# Actual command observation adoption

`CommandJobs::try_observe` is a pure finite copy of current resident data. It
performs no raw admission/emission, effect, wait, drain or journal history read.
The conversation adapter observes outside keyboard handling and painting. A
ready unchanged observation needs no new scene publication. It preserves the
last scene and reading position; no duplicate adoption is emitted.

For a changed observation, the adapter builds its finite native wire, tries the
exact physical CONTROL capacity needed for one4087 event, then prepares and
publishes the scene. Try-refusal returns a local stale status without changing
the scene or last successful apply reference. It never waits for a recorder or
claims an unrecorded observation was adopted. A recorded candidate refusal has
adopted=false and its actual error. A successful publication records4087 before
painting can consume it. Physical emission failure marks capture failure and
propagates before another frame; it does not retry or invent a reference.
Ordinary UI producer registration/admission already exists. This use of its
physical try-grant may defer presentation; it does not bypass effect admission.

These are view observations, not raw metric transforms or new authoritative job
records. The adapter copies original source ranges from the owner result. Actual
native2008 records contain the original bytes; the normal4041 frame records the
safe displayed packet. Neither a frame alone nor a made-up original ordinal is
used to claim which native bytes supplied a page. Missing source provenance is
explicitly unavailable, and raw-enabled inspection does not present those bytes.

## Site4087, native ABI, no structure padding

Every field is appended separately in host order. R is16 lane bytes followed by
u64 ordinal. size_t/int/bool/enum fields use the qualified native ABI; the enums
below have explicit u8 storage. The site's framing is the usual E16 envelope.

Header: u64 view_owner, u64 view_incarnation, originR, u64 observation_generation,
bool adopted, bool has_error, ErrorCode, i64 error_detail. Error slots always
exist and are meaningful only if has_error. View IDs are local to this containing
adapter; originR and the emitting producer distinguish its actual opened scope.
Targets name this adapter's bound current-session CommandJobs owner and canonical
operation ID. They are read-only locators; this view dispatches no effects.

Query: optional after ID, optional selected ID (each bool present, then32 raw ID
bytes if present); size_t row_limit, byte_limit, chunk_limit, requested_offset.

Observation: u8 status, size_t row_count, optional next ID, bool more_rows,
row_count rows, u8 selection, then one detail row only when selection=resident.

Each row:32 ID bytes; u8 phase, custody; u64 admission_order; size_t received,
retained; bool pty, leader_exited, output_closed, worker_finished, terminal_retained,
capture_complete, stop_requested, timed_out, has_exit, sealed; int exit_code,
signal, failure; launchR, terminalR. Exit/signal are meaningful only has_exit.
A retained terminal is distinct from fully sealed capture and reaped custody.

Window: size_t size, source_count, requested, begin, next, cached_begin,
retained_end, received_end; bool omitted_prefix, provenance_complete; u8 page_end;
source_count entries of originalR, size_t chunk_begin, offset, length.
Finish: size_t examined_rows, examined_chunks, seek_steps.
Window bytes are not duplicated in this event. Its exact ordered ranges address
native read originals. No range may include bytes beyond retained_end.

Enum ordinals come from command_observation.hpp: status ready0,busy1,
invalid_identity2,invalid_range3,unavailable4; selection not_requested0,resident1,
not_resident2; phase starting0,running1,stopped2,draining3,settling4,settled5;
custody not_launched0,owned1,reaped2,unavailable3,pending4; page_end frontier0,
byte_limit1,chunk_limit2. A not-resident ID is not a claim that retained history
never existed. Separate catalog/detail lookup can cover8+1 metadata rows.
