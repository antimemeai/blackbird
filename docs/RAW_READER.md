# Reading preserved raw events

`blackbird-raw-reader` is a separate consumer. It opens files read-only, takes no
writer lease, touches no shared-memory lane, and never acknowledges capture.
Stopping it changes no producer or recorder behavior.

```
blackbird-raw-reader list /path/to/session
blackbird-raw-reader descriptions /path/to/session/raw/bbRUN > descriptions.bin
blackbird-raw-reader records /path/to/session/raw/bbRUN > preserved.bin
blackbird-raw-reader records /path/to/session/raw/bbRUN checkpoint.bin > later.bin
```

`list` includes each run independently. An unfinished or damaged run has an
error beside its name; it does not hide healthy runs. `descriptions` writes the
retained ABI/site/body descriptions exactly. `records` emits one snapshot of
the preserved prefix. Reopen explicitly to consume a later publication.
Within one lane, `records` writes events in the order the recorder saved them,
not in ordinal order. The recorder drains the failure, control and data partitions
separately, so a request's control events (for example 1302/1303) can come before
its data events (1300/1301). A consumer that joins events must key on lane and
ordinal or reference and must merge, never assume arrival order (`arconaut-e8n`).
Neither a closed marker nor a complete physical commit substitutes for the
recorder's published cursor.

The native API is [raw_reader.hpp](../include/blackbird/raw_reader.hpp).
`RawRunReader::read` copies one exact raw envelope/body into caller storage and
returns its lane/partition/source offset and next cursor. A capacity error leaves
that record unread; the caller may retry with more storage. EOF updates the
cursor to the exact published boundary, including its final commit. A segment
opening failure stops that reader with the same error on subsequent calls.
Published-boundary validation uses the existing physical journal CRC and batch
checks. The reader reports bytes beyond a segment's allowed boundary as
`unexposed_bytes`; their presence alone does not label them corrupt. Originals
remain in place on success and failure.

All stream integers are little-endian and fields have no padding:

| Part | Fields |
| --- | --- |
| Stream header | `BBRAWOUT` eight bytes, u32 version 1, u32 description length, exact description bytes |
| Record | u8 tag 1, next cursor 64 bytes, origin 32 bytes, u32 raw length, exact raw envelope/body |
| EOF | u8 tag 0, final cursor 64 bytes |
| Origin | lane 16 bytes, u32 partition, u32 zero, u64 source_start |
| Cursor | initial run journal 16 bytes, u32 description CRC32C, u32 zero, u64 segment, segment journal 16 bytes, u64 sequence, u64 offset |

Save a returned record or EOF cursor's 64 bytes as `checkpoint.bin`. On reopen,
the reader checks run identity, description checksum and actual physical frame
boundaries. A previous EOF remains resumable after further commits or segment
rotation. Descriptions v1 (64-byte preamble) and v2 (80-byte preamble) are
supported and preserved as bytes rather than decoded into the
emitting host's C++ types. Their checksum detects changes/corruption, not hostile
tampering or deliberately constructed collisions.

Consumption bounds are explicit `RawReaderLimits`: 65,536 segments, 4,096 origin
entries per segment and 16MiB descriptions by default. Native callers can raise
these. A rejected snapshot neither shortens records nor changes recorder
admission. Records themselves use the emitting journal's declared payload
bound; their bodies remain opaque to this interface.
