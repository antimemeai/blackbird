# Recorder native I/O cuts and competing lanes

Completion work for the remaining recorder part of nls.2. This is a subplan,
not a claim that the checks below have run. Existing raw-channel wake proofs and
RawAudit's exhaustive byte-tail model stay settled. This unit connects native
recorder I/O, preserved-prefix publication, named source acknowledgements and
supervisor restart. Host power loss remains excluded.

## Purpose and source grounding

`record` holds a failure slot before reading each source batch, calls
`RawAudit::append`, publishes the preserved cursor, and only then acknowledges
the named RAM source. Startup may reclaim a batch already present in a complete
disk prefix. It rotates the first lane each tour and takes one bounded batch per
partition per lane. The checks must exercise those actual paths, rather than
add another shadow recorder or repeat the primitive model tests.

The retained qmail/daemontools split between producer and independent native
process, and the already studied Unix write/retry rules, supply the mechanism.
The owned implementation is the source for the exact native boundaries:
`NativeJournalFile::write_at` uses pwrite; runtime `open_audit` explicitly selects
SyncStrength::data and therefore uses fsync on both macOS and Linux;
`publish_cursor` renames a completed 40-byte
publication file; the recorder acknowledges only afterward. Do not substitute
one synchronization primitive for another in a fixture.

## Native fixture seams

Use one test executable with a shared anonymous native mapping created before
RawRuntime forks. It holds a selected event-file device/inode, a cut mode and
actual cut state. Resolve native forwarding functions before fork. Test-only
syscall interposition forwards every unaffected operation to the actual kernel.
There is no production callback, test setting in the writer, new library or
wall-clock campaign.

* pwrite can forward through pwritev with the original offset. For the selected
  event inode, forward exactly the first19 bytes of the first selected append,
  then return EIO on its next write. Preserve the actual19-byte tail.
* fsync forwarding uses its actual next native symbol on both profiles. Do not
  interpose variadic fcntl or change production synchronization policy to fit a
  fixture. F_FULLFSYNC exists in the file backend but is not this recorder path.
* For an after-sync death cut, perform the real configured synchronization, signal the
  cut through the shared native state, and park before returning to RawAudit.
* For an after-publication cut, perform the real rename to the exact run's
  published pathname and park before returning to the recorder. Match the
  selected run, not unrelated metadata renames.

Parking uses a native fixture pipe. Parent stops the supervisor before killing
the parked recorder, checks actual lane P>0 and A==0, then resumes the supervisor
for normal restart. The fixture releases inherited endpoints and cleans up its
owned child processes and names on every exit path. Fixture watchdogs detect
missing handshakes; none supplies a production timer or substitutes elapsed
time for an observed I/O boundary.

## Independent expected results

Use a seven-byte literal containing NUL/high bytes, one fixed test site and
ordinal1. Assemble its expected envelope independently in the fixture. Read
only the actual published disk prefix with the owned read-only reader. Inspect
the actual named lane cursors. The fixture must not invoke RawAudit in writer
mode merely to inspect its result or accidentally publish an unexposed commit.

| Actual cut | Direct oracle |
| --- | --- |
| Partial pwrite19 then EIO | Original source remains P=23,A=0; exact19-byte disk tail stays intact; failure8000 names original lane/data/start0/end23, EIO, append stage1, offset at previous prefix, written19, sync=false. No accepted source event yet. |
| Full append then fsync EIO | Original source remains unacknowledged; failure records the actual full write count and sync_attempted=true; no claim the failed synchronization succeeded. |
| Real configured sync completed, recorder killed before append returns | A remains0 at the kill cut. Restart exposes exactly one original envelope and then A=23; no invented I/O-failure event merely because the process died. |
| Published rename completed, recorder killed before acknowledgement | Published prefix already contains exactly one original; A=0 at cut. Restart acknowledges that original without duplicating it. |
| Configured single failure slot exhausted | After actual I/O failure, automatic restart refuses before a new fallible acquisition if it cannot hold another failure event. Actual failure bytes remain; supervisor fences. Explicit recovery adds a fresh failure lane and preserves the original after the cut is disabled. |

For the I/O-error cases, a complete but unexposed native disk commit may later
be recovered. Distinguish a published-prefix reader's current view from the
source bytes still held in RAM. Do not assert that failed sync necessarily
erased kernel-cache bytes. Broken tails remain in their original segment and
normal recovery uses a successor when needed.

## Competing-lane acquisition

Create two actual prepared producer contexts with data capacity64, bind one
owner thread to each, fill both while the recorder is stopped, and park both
owners in raw_wait_data for their next bounded event. Resume the recorder and
require both threads to wake and publish their independently specified second
original. A native read/write handshake establishes each wait; a thread-start
flag alone is insufficient. Check exact per-lane ordinals/source offsets and
final P==A for both. This attacks runtime wake routing with multiple contexts;
it does not repeat the raw-channel primitive schedule model.

For fairness, queue at least6144 literal23-byte data records on one hot lane and
one cold-lane data marker before resuming the recorder; both actual P>A are
checked first. The pause may fall anywhere in the prior tour. The cold marker
must appear before more than TWO hot batches (4096 hot records,94208 source
bytes) have been preserved. Each actual batch is bounded2048 records and the
first lane rotates. A recorder that drains the hot lane to exhaustion fails
this count-based oracle. No production tour callback or elapsed-time throughput
promise is introduced.

## Work order and cost

The focused design/plan attack corrected the earlier incorrect F_FULLFSYNC
assumption by reading runtime open_audit's explicit SyncStrength::data selection;
it also replaced a vague tour observation with the bounded source-count oracle.
Then run direct reds for missing behavior only; existing correct behavior is a
qualification result, not a fake red. Implement the fixture and any defect it
exposes, run direct greens, one code/oracle attack, and finding-driven recheck.
Use one build worker while independent owner work proceeds; coordinate the
six-worker aggregate. Native Mac/Linux profiles cover their different sync and
process boundaries. No second assurance panel or perpetual performance gate.
