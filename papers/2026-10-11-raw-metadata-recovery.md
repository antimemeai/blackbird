# Raw runtime metadata recovery

2026-10-11. One remaining nls.2 owner unit. The existing raw transport, recorder
and source/disk envelope formats remain the mechanism. No emission-path work,
power-loss guarantee, library or evaluator is introduced.

## Purpose and source grounding

A startup interrupted before emission is permitted must not obstruct the next
startup. Once emission is permitted, recovery must discover its exact lane and
journal identity independently of an interrupted primary metadata write. Missing
source memory must remain an unresolved ingest failure, rather than successful
closure.

The acquired qmail1.03 `qmail-queue.c` writes and synchronizes message/envelope
files before publishing the `todo` link; its abrupt-exit path deliberately does
not pretend incomplete work was completed. The already studied daemontools
supervisor keeps transport lifetime outside the producer/logger. We apply those
publication and ownership boundaries to our owned binary files; we do not adopt
qmail's transforms, timers, or its durability contract. See the existing
[recorder study](2026-10-10-raw-recorder-design-study.md).

## Concrete design and plan

1. Publish a versioned12-byte `bootstrap` marker before creating `lanes`. Only
   an exactly valid marker and absent `active` can identify a new preactivation run.
   Refuse this classification if a recorder segment, later registration, or
   published initial source exists. A missing existing initial mapping is also
   unresolved. These checks distinguish interrupted setup from lost activation
   discovery after publication without needing an intact header.
   Leave its originals intact and allow a fresh run. Unmarked older runs keep
   strict existing recovery; a malformed marker is not an empty-run assertion.
2. Before supervisor fork, prepare/sync the header, owner, descriptions and
   initial lane. Publish `active` atomically with the exact112-byte journal
   header and sync the directory. Thread registration/emission can happen only
   after this boundary. The `lanes` inode still owns the exclusive writer lease.
   Missing lease metadata in an active run is a refusal, never a replacement lock.
3. Write independent80-byte `registered.<name>` and `lane.<name>` files before
   replying to registration. Write `retired.<name>` and `closed.<name>` only after
   every partition has P=D. Preserve the existing drain/closure/seal/unlink order.
   Recovery reads the union, verifies names/identities and compares valid copies.
   Missing or structurally invalid primary bytes can use a valid independent
   copy; conflicting valid copies refuse, and EIO never becomes a fallback.
   `active` and a valid `lanes` header must likewise agree.
   Activated runs must retain discovery of their initial lane. Bound live mapped
   recovery lanes by the existing256-lane limit; closure history stays on disk.
4. A registered but missing mapping emits the original ENOENT acquisition-failure
   record on a fresh, durably registered recovery lane. Preserve that event,
   refuse global closure and retain remaining mappings. Each retry owns a fresh
   failure lane, so old failure ordinals are never reused.
5. Successful global closure authorizes completion of known-name unlinks. On a
   later launch an exact empty global closure marker allows the same cleanup.
   Validate registration/closure copies, retain all disk/metadata originals and
   accept only ENOENT as an already completed unlink. This finishes interrupted
   closure publication before unlink without opening historical mappings.

Setup I/O errors remain errors and originals remain available. Atomic temporary
files are ignored until rename; no damaged file is rewritten by recovery.
Preactivation remnants are retained rather than requiring a correct header to
clean them up. This may retain never-activated named mappings; it does not permit
their data to be mistaken for a completed or live lane.

Root performed one targeted design/plan attack: require a valid version marker;
preserve legacy recovery; reject conflicts and EIO; do not replace a missing lease;
keep missing sources unresolved; and allow one valid closure copy after a crash
between the two publications. These requirements are included above.
During implementation the owner identified lost-activation ambiguity; root
accepted the stronger preactivation checks and direct published-marker-deletion
oracle above.

## Direct oracles

The focused fixture constructs an interrupted bootstrap with known malformed
header bytes, then checks successful startup and byte-for-byte preservation of
the remnant. An actual fork fixture stops the real supervisor/recorder, publishes
a known binary event, kills all holders and waits for the real lease release.
With damaged primary header/registration it must recover exactly the independent
envelope, lane identity and source start. Invalid independent copies must refuse
and keep the named mapping and metadata bytes intact. A deliberately removed
source mapping must retain an exact ENOENT failure, leave the run unclosed, and
keep other named mappings. One valid closure copy must recover a genuinely
drained/unlinked lane without fabricating a missing-source failure.

Both initial red cases failed on the original implementation: partial-header
bootstrap refused startup, and missing registered memory returned success after
writing global closure. The isolated fixture additionally attacks differing
valid headers, missing lease/activation and a real inode-targeted read EIO.

These are recovery-state and process-death checks, not host power-loss tests.
Results and the single code/oracle attack will be recorded after execution.

## Single code/oracle attack and fixes

Root found three concrete defects in the candidate: mapping failures were
acquired before failure capacity was prepaid; only successful mappings counted
toward the lane bound; and run-directory entries followed symlinks. Fix them by
collecting at most256 unclosed registrations before mapping acquisition, creating
and registering independent failure capacity for that entire possible sequence
plus the recorder's own next failure, and reserving before any mapping call.
Check original frame/batch limits before acquisitions. Walk closed metadata one
item at a time rather than retaining historical name sets. A drain-only recorder
has no live original producer after the lease/dead-owner checks, so it does not
need new source sockets. Skip symlink run entries explicitly.

Direct finding checks cover two absent sources with caller-configured111 bytes,
matching both ENOENT envelopes, source starts and distinct ordinals; and symlinks
to live and orphan foreign runs that preserve the foreign metadata, lease and
pending source cursors. The initial ordinal-offset oracle exposed an inaccurate
historical claim: site8000 is103 bytes on the qualified native ABI;111 bytes was
the configured one-event partition, not the event width. Use the exact103-byte
layout in the oracle. Source already computed the correct native width.

The owner also identified closed mappings left named by interruption before
unlink. Root approved the cleanup rule above. The direct closure image now
leaves the genuinely drained source named through recovery and checks ENOENT
after successful closure, plus a separate global-closure-before-unlink image.

## Executed checks

The isolated debug raw_metadata CTest passes after those fixes:0.96 seconds,
including exact recovery, copy conflicts, original EIO, unknown sources/retries,
one configured failure slot with two actual failures, interrupted closure/unlink,
and foreign live/orphan symlinks. Source and fixture clang-tidy passes with the
owned profile and no suppression. Logs are
`/tmp/blackbird-raw-metadata-closure{,-tidy}.log`; build directory is
`build/nls-metadata`, one compiler job. The earlier shared build/debug link race
was discarded and does not count as qualification.

Root owns the integrated source gate and other-platform/profile checks. These
focused checks do not claim all runtime append/sync cuts, all unactivated-name
cleanup, every possible bootstrap syscall cut, or system-wide product coverage.
