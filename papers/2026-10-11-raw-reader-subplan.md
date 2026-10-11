# Raw run discovery and consumption

The reader runs outside Blackbird and consumes preserved bytes. It does not
change admission, lane acknowledgements, writer leases, or capture. Its native
API and separate executable are sufficient for downstream programs to obtain
immutable descriptions, raw records, origins, and restart cursors. Evaluators
and operations programs remain consumers; neither their calculations nor their
definitions belong here.

## Existing source and design

The raw design's separate tertiary reader and the recorder's atomic publication
are the starting point. `raw_runtime.cpp` writes `published` as 40 native
little-endian bytes: segment u64, journal 16, sequence u64, offset u64. A successor
header names its predecessor's validated sequence and end offset. `seed.N` gives
the surviving lanes' cumulative source prefixes with CRC32C; retired lanes may
legitimately be omitted. `descriptions` retains the emitting build's native ABI,
sites and body layouts. `RawAudit::open_reader` already verifies an explicitly
preserved prefix without acquiring a writer lease or synchronizing anything.
These mechanisms follow the existing raw-recorder study; no new dependency or
replacement transport is proposed.

## Snapshot and interface

Open `published` once. Its atomic replacement gives one stable boundary even
while recording continues. Read segments 0 through its segment; segment N+1's
predecessor gives N's permitted boundary, while `published` gives the last one.
Validate headers, contiguous numbering, environment/issuer/limits, journal
identities, predecessor links, seed CRCs and seed source ends against the prior
validated audit. Never treat a complete commit by itself as publication.

Retain only headers, seeds, description bytes and boundaries, with one open
RawAudit at a time. Report physical bytes beyond the allowed boundary as a
retained tail; their presence is not itself a diagnosis of damage. Corruption
inside a published boundary rejects that run with a concrete error. Discovery
returns entries with independent per-run errors so an incomplete bootstrap run
does not hide other runs. A missing raw directory means no runs, not capture.

A cursor includes the initial journal identity, segment number, and that
segment's journal/sequence/offset. Opening at a cursor validates it against
actual record boundaries; an embedded frame-looking payload is not a cursor.
Iteration returns exact raw bytes copied into caller storage, the origin, and
next cursor. At snapshot EOF it returns the exact published cursor, including
the commit frame. A caller explicitly reopens to obtain later publication;
there is no reader timer, callback subscription, or hidden polling.

`blackbird-raw-reader list SESSION` prints discovery; `descriptions RUN` writes
the retained bytes exactly; `records RUN [CURSOR_FILE]` emits an owned versioned
binary stream with descriptions, records and EOF cursor. The optional file
contains the 64-byte cursor from a prior stream. It adds the descriptions CRC32C
and four reserved zero bytes to the run/segment/journal/sequence/offset fields.
The CRC detects descriptor corruption/change across reopen, not malicious
tampering or checksum collisions. No metric transformation is
needed to consume this stream and no source/recorder formatting changes occur.

## Direct oracles and implementation order

1. Write a native fixture with independently specified envelope bodies and
   explicit publication files. Hold a live writer lease and prove that the
   reader still opens, while a complete un-published commit remains invisible.
2. Write a two-segment fixture with a seed and predecessor link, plus a damaged
   tail in the first segment. Verify exact origins/bodies, tail reporting,
   resume and unchanged file bytes. Seeds may omit retired origins.
3. Append and publish later records after opening a snapshot. Its original EOF
   must remain unchanged; reopening at its EOF must return only new bytes.
4. Reject wrong-run/off-boundary cursors, corrupt seeds, bad predecessors and
   publication beyond file extent. Discover missing-publication bootstrap runs
   beside a good run with errors local to the bad entry.
5. Implement the API, then executable and direct executable stream oracle.
   Check production opens are read-only and compare directory byte snapshots
   around reader success and failure. No mutation campaign on this host.
6. Parent integrates CMake, runs the owned source gate and applicable Linux and
   diagnostic checks. One targeted source/oracle attack, fixes and recheck;
   no second certification layer.

Resource use: one build/test worker here, parent controls the shared maximum of
six. Source will be in raw_reader.hpp/cpp, raw_reader_main.cpp and
raw_reader_test.cpp. The runtime remains under its own owner.

The parent's targeted design attack approved this unit with descriptor-relative
discovery, retained snapshot metadata, real cursor validation, and per-run
errors. Direct red compilation failed on the absent reader header. The first
native run exposed a real restart defect: an old EOF cursor names a commit that
becomes interior after growth, while RawAudit::read skips commits. The reader now
uses RawAudit::validate_cursor once at positioning to scan physical boundaries
including commits; sequential reads do not repeat that scan. The parent-directory
constraint uses the small NativeJournalDirectory::open_at owned API. A segment
that disappears during iteration returns a sticky native error instead of
leaving a null audit for the next read.

## Implementation results

The unit adds the native API, separate reader executable, binary cursor/stream,
and [usage documentation](../docs/RAW_READER.md). Producer and recorder formats
remain unchanged. Discovery opens children relative to its held raw-directory
descriptor. The raw parent can retain the older runtime's umask-derived mode
inside a private session; each run and file still passes the native restricted
descriptor checks. The runtime recovery owner's bootstrap/active/registration
metadata is not changed or repaired by this consumer.

Direct native/CLI cases passed in the isolated qualified debug build
`build/nls-reader`, one build worker. The latest CTest run was 1.28s, including
the real runtime emitting `{00,ff,41,7f,00}`, retiring its lane, rotating/sealing
and closing its run, then discovery and exact raw-byte/origin consumption.
Fixtures exercise a live writer lease, a complete un-published commit, later
publication with an old EOF resume, a seeded successor behind a damaged original
tail, coherent-checksum wrong seeds, wrong predecessor links, missing metadata,
symlink refusal, wrong cursor/run/descriptions, segment loss during iteration,
binary executable framing and EOF resume. Whole raw-file byte maps remain equal
around reader-only work. No failed acquisition is converted into an empty run.

The narrow static recheck passed raw_reader.cpp, raw_reader_main.cpp,
raw_reader_test.cpp, raw_audit.cpp and journal_storage.cpp; initial findings were
handled with direct optional/dirfd guards and a compile-defined exact executable
fixture path. No suppression was introduced. The parent owns the one code/oracle
attack, integrated source gate, Linux and diagnostic checks, journal/beads and
publication. Those broader qualifications are not claimed by this lane.

The parent's single code/oracle attack found a pathname race: session validation
used an opened descriptor, but raw enumeration reopened `session + /raw` by name.
The fix moves discovery behind an owned-directory overload and opens raw
relative to the held session descriptor; child enumeration remains relative to
raw. The direct oracle renames the held session and replaces its old path with a
symlink to a different session before discovery. It must consume the original
session's published sequence, including when the intermediate raw directory is
mode0755. Red compilation failed on the absent descriptor overload. The fixed
unit passed native CTest in0.94s and reader/storage/test narrow lint, with no
remaining findings. No further assurance layer was added.

The integration ABI update is supported: descriptions v1 requires at least64
bytes and v2 at least80 bytes. The reader preserves both versions exactly,
rejects truncated v2/unknown versions, and the actual runtime-to-reader case now
uses generated v2 descriptions. Opaque body preservation does not depend on the
reader host interpreting native ssize_t/pid_t/uid_t/short fields.
