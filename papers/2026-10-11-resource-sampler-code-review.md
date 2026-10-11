# External sampler: one code/oracle attack

2026-10-11. Root's single attack of raw_sampler.hpp/cpp/main, native tests,
immutable descriptions and the reviewed sampler subplan. No new transport panel,
mutation campaign or review of this report.

## Findings

1. The killed-sampler fixture acquires while the recorder runs, then stops it.
   It can pass solely by reading originals already preserved before process death.
   Stop the actual registered holders before acquisition, admit enough prepaid
   capacity, then require the actual source lane P>0/A=0 and exact independently
   acquired native body while its owner is alive. Kill the exact holders/owner
   and require one recovered copy with no fabricated sampler END. Obtain status
   after registration, which can replace the recorder. Establish real catalogue
   lease availability before the recovery open; native death observation alone
   was not the selected recovery boundary in the first candidate fixture.

2. Failed checks in stopped-recorder/killed-sampler fixtures abort before normal
   resumes/joins and can leave stopped processes alive. Add cleanup to failed
   assertions for only the fixture's actual owned process group/IDs, including
   abort paths. Clear ownership after real join/death so PID reuse cannot target
   another process. Parking pipe lifetime must allow child teardown. This is
   fixture ownership, not an additional production policy.

## Source and byte checks

Native rusage outputs preserve UUID and all ten actual SDK u64 fields only after
success; native errno is captured immediately on failure. There is no resource
conversion or per-job attribution. Linux validates the actual held root via
fstatfs, attaches one held PID directory per command, opens stat relative to it,
and retains each actual positive read before any interpretation. PID decimal
formatting addresses the native control pathname; it does not format a metric.
Clock request/return pairs preserve native status/timespec and never durations.

Every acquisition outcome and descriptor close is prepaid. Native close is
attempted once, never retried on ambiguous close failure. Linux max simultaneous
held control is865=206 setup remainder+49 completed-control-read remainder+
294 opened-command remainder+316 read burst. Mac pipe max is670=102 terminal+
49 read remainder+49 validation+470 acquisition. The direct request's105 credit
publishes56 before acquisition, leaving49 held. Its already published bytes may
require recorder drain; they cannot make a held-credit self-wait. Setup preflight
refuses lanes below those actual held bounds before OS acquisition. Data minima
are140 native Mac outcome and4144 Linux read outcome. Unused variants release.

Control input reads at most the remainder of one native32-byte frame and records
each actual partial/EOF/error before decoding. Read request and return are
distinct linked events. Unsupported complete commands and direct7027 requests
retain their original native inputs and linked refusal before target acquisition.
The sampler owns no cadence; controller stop/death descriptors remain borrowed
and sticky. An already entered native call can finish and retain its result.
Orderly RawRuntime shutdown is separately allowed to await recorder drain.

The direct native forwarding oracle observes actual kernel results before the
emitter; successful output is compared to those unchanged representations.
Anchored-directory, wrong-filesystem, invalid-clock, malformed/fragmented/partial
control, full-lane sticky-stop and real CLI cases attack distinct fault classes.
The above death finding narrows the candidate's process-death claim until fixed.

## Disposition

Fix both findings in this unit and run the focused finding-driven recheck plus
the relevant native profiles. Do not add another code/oracle panel. No production
source defect was identified in this attack; that does not replace the real
Linux execution of its conditional native source and fixtures.

Both concrete findings were fixed and directly rechecked. macOS sampler fixtures
and three changed translation units pass native/static checks. Linux
run-py1fnxih passes sampler plus channel/admission/recorder cuts in debug, release
and ASan. The Linux sampler oracle compares the selected lane by its native
ordinal, because DATA read bytes and CONTROL EOF results can reach disk in
partition order; writer/recorder bytes remain unchanged. No new panel.
