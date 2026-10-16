# Mailbox admission and progress

R2 in [REGISTRIES](../cpp-bug-class-audit-2026-10-15/REGISTRIES.md) shows that
nondecreasing delivery-history budgets currently enter the same skip branch as
transient pending input. UI enqueue succeeds and clears the composer's bytes,
but repeated selection cannot advance them. The repair preserves the4096 exact
delivery identities,16 MiB cumulative input and64 KiB pending-input bounds.

The owner will distinguish admitted controls, transient pending-input wait and
permanent history refusal. `request` must refuse a new identity immediately when
retained deliveries plus already promised unique controls would exceed either
permanent bound. Exact duplicate controls reuse their promise or receipt;
conflicting identity content fails. Direct `control` must honor those promises
as well, so it cannot steal space between selection and dispatch. Reserved
capacity survives mailbox selection until the corresponding control completes
or explicitly fails. Pending-input selection alone can wait, because the live
worker's successful writes release that actual budget. Stop/background bypass
these delivery budgets and retain their reserved mailbox slots.

Use a small per-job reservation map for only undispatched controls. Queue and
reservation share one immutable argument owner; no extra copy of queued payload
is required. Selection returns existing Value API arguments and preserves the
reservation through dispatch. Track selected pending bytes until direct control
transfers them to the worker queue. De-duplicate within/across selected batches.
Build selection results before removing entries so allocation failure does not
silently discard accepted controls. Receipt history is never evicted or retried.

The permanent fixture runs a private native PTY child with raw mode and real
read/ack barriers. Separate scenarios fill4096 IDs with zero-byte delivery and
16 MiB history with actual accepted/read bytes. Each proves immediate new-control
refusal, exact duplicate replay/conflict, boundary queued reservations and a
selected-but-undispatched reservation. Transient pressure blocks the child read
behind a gate, fills pending input, observes a deferred request, then releases
the reader and checks exact bytes and eventual dispatch. Unrelated work and
urgent background/stop must still be selected. All old-red logs precede product
edits. Root reviews this plan, owns target registration/build coordination and
performs the single independent code recheck; only affected tests run here.

## Direct old-red and fixture correction

Four independent unchanged-mailbox executions (`identity`, `history`,
`reserved_ids`, `reserved_bytes`) exited1 with
`permanent mailbox budget accepted an undeliverable control`.
The transient-pressure counterpart passed on the old product. These used the
real native PTY reader, not a counter-only budget seam. Original product source
SHA256 was `adb409ba16de06c5582fe685c24ef37bd11702d6e07b063ab05968413bc7e9aa`;
current Debug foundation library was
`e958b0a26efd945a83b9bebc5fd304ca231ab94699a9ed17dde893f3b8c45866`.
The direct compiler command used qualified Clang23, C++20, the active Xcode SDK,
`-I include` and only `libblackbird_foundation.a`. No shared Ninja build ran in
this lane. Individual logs and binary are ignored under
`context/cpp-intermission-2026-10-15/mailbox/`.

The first history fixture did not reach its oracle: a silent reader made every
PTY write depend on the collector's normal idle10ms poll. It hit the fixture's
30s child lifetime with2.5 MiB received. Preserve that setup failure as
`fixture-idle-cadence-failure.log`; do not count it as product red. The corrected
reader emits one stdout acknowledgement after each actual read, waking the
normal collector poll. Deadlines and required16 MiB actual read remain unchanged;
this corrected fixture reached the intended permanent-admission failure.

## Implementation and challenged boundaries

Root's preimplementation challenge required reservations to survive selection,
protect terminal-retained jobs from archive/automatic retirement, and prevent
direct controls from stealing selected pending-input capacity. The implemented
closed `Admission` distinguishes ready, pending-input wait and permanent history
refusal. Both direct and queued admission use actual delivery plus promised
identity/byte totals. Queue and reservation share one immutable Value; selected
returned values are copied only for the existing control API handoff. Reservation
identity count shares the4096 delivery-identity bound and its payload shares the
16 MiB history bound, even if an owner selects many batches without dispatch.
No full history scan, permanent shadow history or receipt eviction exists.

Exact queued duplicates share one promise. Conflicts fail without releasing the
rightful promise. Control success transfers the promise to the actual delivery;
an explicit control failure releases it. Finished jobs still retain their actual
terminal through `settled`, while reservation debt blocks archive and automatic
retirement until each control is explicitly resolved. All selection result
construction happens before queue mutation, so allocation failure preserves
accepted requests. Only pending input defers; permanent history refusal reaches
control feedback instead of waiting forever. Existing UI request exceptions
already preserve input bytes and resize state, so terminal source is unchanged.

The permanent tests additionally hold selected controls across another admission,
try conflicting queued/direct identities, keep a terminal-retained job at a
one-row bound, and release that row only after explicit refusal. Transient tests
reserve a whole pending-input budget across batches and direct controls, then
read exactly three64 KiB payloads. Stop/background and unrelated work progress
while the first payload is blocked behind the real child reader gate.

## Green and the single independent recheck

The coordinated Debug build's affected checks initially passed5/6:
`command_jobs`3.51s, identity0.60s, history5.23s, reserved_bytes6.47s and
transient0.11s. `reserved_ids` reached its new terminal-owner assertion and failed
because the fixture demanded specifically `resident_limit`: existing start
admission checks live semantic debt first, so this one-row/one-live fixture
correctly returned `live_limit` while keeping the row. The corrected oracle
requires one of these two owner-issued limits, no started worker, surviving row
and debt, then actual successful retirement after explicit control refusal.
Only that affected case reran against the same product library and passed0.35s.
Logs are `green-debug-before-review.log` and `green-reserved_ids-corrected.log` in
the ignored mailbox directory. Corrected test SHA256 is
`76b0be4003dfbef97dd9d5330450ba65b80f091ea29b438e12c6d9cd350d1a58`;
qualified product source SHA256
`fd4c579d80d8b82e9eae4811dfa3c419f7049976ac67ebfd041b050d671b1970`;
foundation library `cca8f4141ae61265206255dd7920417d90efd807f301267a7d464f8254f1468d`.

Root's one code recheck challenged a possible raw-linkage throw after reservation
publication. Actual `raw_command_linked` and `raw_command_record` are declared
`noexcept` in the header and definition; raw emission failure fences capture and
returns an empty reference. They cannot throw past reservation rollback. The
throwing raw capacity preparation precedes reservation creation. This hypothesis
is therefore rejected from actual source, without an invented exception test.
Root confirmed the correction and reported no other product finding.

Selection's second pass likewise uses only existing immutable fields, map lookup,
prebuilt selected indexes, counter updates, noexcept raw linkage and shared-owner
deque removal. It performs no result allocation or throwing raw preparation.
This establishes queue mutation's exception boundary, not a promise that raw
transport never fences. Any capture failure retains its existing independent
capture fence; this repair does not exempt or reclassify raw capacity failures.
No source change was needed after the affected Debug run. Full shared source
qualification and changed-path sanitizer checks belong to root's integration.

## Installed-lint ownership correction

The full source gate passed192 Debug tests, then installed clang-tidy reported
`bugprone-use-after-move`: request construction had two textual moves guarded by
the first successful shared owner being non-null. The owner contract prevented
the second move at runtime, but the structure obscured that boundary to analysis.
New-reservation validation now sets a flag; a single construction site moves the
arguments. All subsequent request-ID/job-ID borrows come from the immutable
owner. Reservation insertion precedes accounting, promised duplicates reuse
their owner, and failed mailbox insertion still releases the new reservation.
The fixture's optional job-ID argument now borrows a const string reference
instead of making an unnecessary parameter copy.

Root reviewed this concrete restructure and found no additional issue. Direct
clang-tidy on exactly `src/command_jobs.cpp` and `tests/command_mailbox_test.cpp`
passes with empty diagnostic logs. No suppression, configuration change or new
review panel was introduced. Focused Debug and sanitizer results after this
actual source change are retained as `lint-fix-*` in the ignored mailbox
directory; the final normal full gate remains root's publication requirement.

After this restructure, affected Debug6/6 pass15.58s (native command jobs plus
the five mailbox scenarios), ASan/UBSan mailbox5/5 pass15.48s and TSan mailbox5/5
pass21.26s. Profiles and deadlines stay unchanged. These directly exercise
permanent boundaries, queued/selected promises, duplicate/conflicting requests,
terminal-owner protection and actual deferred PTY input across the new move site.
