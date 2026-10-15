# Long-term trace storage

Operator request, 2026-10-14: configure a local directory, SCP remote directory,
or S3 bucket/prefix and sweep trace bytes well before session capacity. Issue dc3.

## Storage distinction

The semantic `audit` is one capped journal. Raw recordings live under `raw/` and
have independent physical segments. Copying either to another destination does
not reset the semantic journal's byte/record counters. Keep that distinction
visible. Continuation beyond the semantic cap is a separate ownership/recovery
change; its desired scope was asked separately. This delivery implements the
independent archival/copy/restore layer. Never delete a live source to
pretend its capacity has been reclaimed.

## Configure and use

Build `blackbird` and its `blackbird-archive` dependency, then choose one:

```sh
build/release/blackbird-archive configure /absolute/session /Volumes/traces/blackbird
build/release/blackbird-archive configure /absolute/session user@host:/srv/blackbird-traces
build/release/blackbird-archive configure /absolute/session s3://bucket/blackbird-traces --region us-east-1
```

Settings live at `SESSION/trace-archive.bbm`; they apply to that session's next
launch. Configuration alone starts no process or upload. Normal Blackbird
launches start the worker automatically. Existing running binaries are unaffected.
SCP uses existing OpenSSH authentication/configuration and requires `ssh`, `scp`
and a remote POSIX shell with `mkdir`, `ln`, `cmp` and `rm`. Remote publication
uses safely quoted paths and a temporary upload; published objects are never
overwritten. Root SCP paths and SCP URIs are accepted. Local paths must be
absolute and outside the source session tree.

S3 reads `AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`, optional
`AWS_SESSION_TOKEN`, and region from `--region`, `AWS_REGION` or
`AWS_DEFAULT_REGION`. This is general-purpose AWS S3 over HTTPS; this slice does
not load shared AWS profiles, obtain role credentials or configure alternate
S3-compatible endpoints. Uploads include SHA-256 checksums and conditional writes.
No SDK/library is added. Existing `openssl`, `scp`, `ssh` and `curl` are external
tools, as in the existing authentication/provider machinery.

```sh
build/release/blackbird-archive configure /absolute/session /Volumes/traces/blackbird --chunk-bytes 16777216 --sweep-bytes 16777216 --interval-seconds 60
build/release/blackbird-archive sweep /absolute/session
build/release/blackbird-archive status /absolute/session
build/release/blackbird-archive restore /Volumes/traces/blackbird MANIFEST.bbm /new/inspection-directory
```

`sweep` prints the completed manifest's content-addressed filename. Use that
filename with `restore`; remote destinations use the same command and S3 needs
its region/credentials. Restore verifies all chunks before publishing the new
directory. It refuses an existing destination. Inspect restored raw runs with
`blackbird-raw-reader list /new/inspection-directory`. These are inspection
archives, not a reconstructed executable session or automatic side-effect replay.

The worker logs completed manifests and errors to `SESSION/trace-archive.log`.
On a failed pass it retains local originals and the preceding successful state,
then retries. Shutdown closes/drains the normal recorder before the final sweep
and waits for the worker; remote transfers have finite per-command timeouts.
Remove `trace-archive.bbm` while the session is stopped to disable future launch
workers. Existing archives and local originals are kept.

## Implementation

An owned native worker runs outside the producer and recorder. Session-local
BBM2 configuration selects an absolute local directory, `user@host:/directory`
(or `scp://...`), or `s3://bucket/prefix`. Normal sessions start the worker when
configured. Read-only inspection never starts one. The worker polls sizes and
sweeps after 16 MiB of growth or 60 seconds by default, including initial and
final passes. Both thresholds are configurable. Local data stays on failure.
The worker polls once per second; neither a size trigger nor the interval is an
admission reservation or a guarantee that a very large single operation fits.

Snapshots contain exact semantic audit bytes, stream diagnostic bytes, and raw
events/descriptions/seeds/publication/closure files. Exclude credentials,
process handles, shared-memory names, locks and derived session state. They are
inspection archives, not permission to re-execute old work. An observed semantic
file extent does not establish acknowledgement or settlement; retained journal
recovery retains its existing checks. Raw publication metadata remains available
to bound ordinary raw-reader exposure separately from observed physical bytes.
Each raw run's publication bytes are pinned before copying its referenced event
prefix; later segments/closure cannot leak into that snapshot. Previously
archived diagnostics remain in manifests after their local files disappear.

Content-addressed bounded chunks share unchanged prefixes across sweeps. An
immutable native manifest names source paths, exact lengths and ordered chunks.
Upload every referenced object before the manifest. Commit the local cursor only
after manifest transfer succeeds. Destination changes invalidate local progress.
Interrupted work may leave unreferenced objects; retry uses the same content IDs.
Restore verifies each object's SHA-256 and length and creates a new inspection
directory without overwriting existing material. Changed source files are hashed
in bounded chunks; already transferred chunks are not uploaded again. This can
read the growing audit again, but does not parse/replay its semantic history.
Never load a complete history
into RAM merely to copy it. No compression, metric transformation or filtering of
already emitted events is added to producers or recorder.
Manifests/progress packets have the native 16 MiB/500,000-node bounds; chunks
are configurable from 4 KiB to 64 MiB. Orphan temporary objects after interrupted
transfers are not originals and are not automatically garbage-collected here.
Archive availability/durability follows the selected filesystem, SSH server or
S3 service; no new host-power-loss guarantee is made.

Use existing system executables: OpenSSL SHA-256, OpenSSH SCP and curl SigV4.
No new library. Commands use argument vectors, never a local shell. SCP uses
batch mode and strict filename checking. S3 credentials travel only through a
private stdin curl configuration, not argv, manifests or logs; region is explicit.
S3 uses immutable keys and conditional PutObject; an existing object is read and
compared before accepting an interrupted retry. Transport errors preserve source
and prior successful progress. No selected real destination is needed for tests.

## Source grounding and direct checks

Read native JournalDirectory, RawRunReader, recorder publication/seed/closure,
saved-state/catalog, StreamCapture and capacity machinery before this plan.
OpenSSH documents SCP's SFTP transport and batch/URI behavior:
https://man.openbsd.org/scp . curl documents argv/stdin configuration, SigV4 and
upload handling: https://curl.se/docs/manpage.html . S3 PutObject documents
conditional writes and supplied checksums:
https://docs.aws.amazon.com/AmazonS3/latest/API/API_PutObject.html . Exact GET
retrieval: https://docs.aws.amazon.com/AmazonS3/latest/API/API_GetObject.html .

Concrete defects to prevent: transfer failure advancing progress; missing or
corrupt objects restoring partial success; a mutated source reusing old chunks;
credentials or process metadata entering archives; symlink/path traversal;
partial manifests becoming visible; polling/archival generating its own capture
loop; shutdown leaving an unobserved owned child. Independent byte fixtures,
native local round-trip, fake SCP/curl endpoints, refusal/retry and actual
startup/shutdown exercise these claims. Two hardening layers only; no laptop
mutants, live account/bucket writes or unrelated performance campaign.

The attempted independent Kimi design review returned subscription access 403
before a reviewer session started. It is not recorded as a completed review.
Root source analysis corrected published-prefix consistency, retry overwrites,
expired-original retention, restore publication and child-allocation ownership.
Execution results are recorded in the journal after the required source checks.
