# External native resource sampler

`blackbird-resource-sampler` runs outside the sampled process. It acquires native
process resources only when a controller asks, and emits originals through its
own raw lane. The recorder preserves those bytes with the immutable sampler
description. The sampler does not parse resource fields, calculate rates or
durations, convert units, aggregate, or choose a sampling cadence.

The study and approved plan are in
[the sampler subplan](../papers/2026-10-11-resource-sampler-subplan.md) and
[native references](../papers/2026-10-11-resource-sampler-references.md).
The implementation and direct tests are `raw_sampler.cpp` and
`raw_sampler_test.cpp`; the root's single code/oracle attack is
[recorded here](../papers/2026-10-11-resource-sampler-code-review.md).

## Controller and ownership

```
blackbird-resource-sampler once SESSION ENVHEX CONTROLLER PID METHOD
blackbird-resource-sampler pipe SESSION ENVHEX CONTROL_FD [STOP_FD]
```

`ENVHEX` is a nonzero 16-byte environment identity encoded as 32 hexadecimal
characters. Method1 is macOS `proc_pid_rusage(..., RUSAGE_INFO_V0, ...)`; method2
is Linux raw `/proc/PID/stat` acquisition. A method unavailable on the host is
refused before resource acquisition. The one-shot interface emits its original
native arguments at site7027 before validation; its site7004 acquisition links
that request. Parse failures outside representable native arguments are CLI
usage errors before a sampler session exists.

Pipe commands are exactly32 bytes, matching the host-native setup ABI:

| Offset | Original field |
| --- | --- |
| 0 | `BBRSSCTL`, eight bytes |
| 8 | `uint32_t` version1 |
| 12 | `uint32_t` method |
| 16 | `uint64_t` controller identity |
| 24 | `pid_t`, signed32 bits |
| 28 | `uint32_t` reserved, must be zero |

Each actual read has distinct request7002 and return7003 events. Positive
returns retain exactly the returned bytes before decoding; negative returns
retain the native return and errno; EOF retains the actual zero return. Reads
request at most the remainder of one command. Partial commands can span reads.
Malformed complete input and partial EOF terminate the session with their raw
bytes and linked failure. Empty EOF closes it normally. A valid acquisition
links the return of the read that completed its command. A native target failure
does not prevent later valid commands.

The C++ caller opens a `RawRuntime`, registers and binds one execution-owner
lane, and retains that binding through sampler destruction. Input and stop
descriptors and `RawWaitControl` are borrowed; their owner retains their lifetime.
The sampler owns and closes its Linux root and per-command descriptors. The
Linux `acquire_directory` fixture seam borrows an already-attached directory and
never closes it. Production verifies procfs on its held root descriptor via
`fstatfs`, opens the PID directory once per command using descriptor-relative
`openat` with `O_DIRECTORY|O_NOFOLLOW`, then opens `stat` relative to that directory.
Changing a pathname after attachment cannot redirect that command.

Every command attaches the currently observed numeric PID afresh. This does not
assert that two commands refer to the same historical process. macOS retains
the native process start/exit absolute times and executable UUID; the UUID alone
is not a process incarnation. Linux retains all raw stat bytes, including its
incarnation fields, without parsing them. Consumers can interpret preserved
originals later. Resources describe a native process, with no per-job allocation.

## Native events

All events use the existing16-byte raw envelope. A reference is lane16 plus
native `uint64_t` ordinal. Bodies copy individual native fields, excluding C++
structure padding. The immutable description gives exact field order and ABI.

| Sites | Native surface |
| --- | --- |
| 7000/7001 | Session BEGIN/END, supported method bits and actual outcome |
| 7002/7003 | Controller read request/return and exact original bytes |
| 7004/7005 | Acquisition BEGIN/END, controller/PID/method and outcome |
| 7006 | Linked refusal, original `ErrorCode` and detail |
| 7007/7008 | Linux `sysconf` request/return, native page-size and clock-tick selectors |
| 7009/7010 | Clock request/return, native selector and before/after role |
| 7011/7012 | Linux root open request/return |
| 7013/7014 | Held-root `fstatfs` request/return, individual native fields |
| 7015/7016 | PID-directory attach request/return |
| 7017/7018 | Descriptor-relative stat open request/return |
| 7019/7020 | Stat read request/return, exact returned bytes, EOF and failures |
| 7021/7022 | macOS rusage request/return, native UUID and ten `uint64_t` outputs |
| 7023/7024 | Owned descriptor close request/return |
| 7025/7026 | macOS timebase request/return, native numerator/denominator |
| 7027 | Direct request originals before argument validation |
| 7028–7031 | Reserved |

Success-only outputs appear only after native success. Failure clocks emit no
invented timespec. Rusage retains user/system times, package-idle/interrupt
wakeups, page-ins, wired/resident/footprint bytes, and process start/exit absolute
times exactly as returned. Linux reads at most4096 bytes per native call and
retains each positive read separately. Clock pairs surround each actual resource
call; they emit native seconds/nanoseconds, never a computed duration. Native
close is attempted once; ambiguous failure is recorded without retry. Close
references identify the original open request.

## Admission, stop and shutdown

Before any native setup fact, the sampler verifies that its lane can support the
largest simultaneous held control budget and one maximal data result. It then
claims the exact setup/terminal budget. Each command and native read prepays its
request, all outcome branches, owned closes, refusal and END before acquisition.
Data capacity is acquired before entering the resource call. Already acquired
results can be published from the held budget without waiting for new credit.

| Host | Minimum control | Minimum data | Setup/terminal claim |
| --- | ---: | ---: | ---: |
| macOS | 670 | 140 | 214 |
| Linux | 865 | 4144 | 706 |

macOS successful setup consumes112, retaining102 for owner refusal49 and maximum
session END53. An acquisition claims470: BEGIN56, END53, refusal49, rusage
request/failure96, and two clock pairs216. Pipe admission can hold102 terminal,
49 from the completed control read, 49 validation, and470 acquisition:670.
Its140-byte data result includes the envelope16, request reference24, native
return4, UUID16 and ten64-bit fields80.

Linux successful setup consumes500, retaining206, including12 unused failure
variant bytes. Required remaining root-close/END/refusal is194. A normal command
claims534; successful BEGIN and two opens consume240, leaving294 while a read
claims316. The pipe peak is206 terminal,49 completed-read remainder,294 command
remainder and316 read burst:865. The borrowed-directory seam claims346 because
it owns only the stat descriptor. Each positive read needs4144 data bytes:
envelope16, reference24, native `ssize_t`8 and at most4096 raw bytes.

Controller reads claim157: maximum request/return108 plus refusal49. Direct
requests claim105, emit56 original request bytes, then retain49 through validation
and acquisition admission. Those56 bytes are already published and can be
reclaimed by the recorder. They may require ordinary recorder drain at exact
minimum physical capacity; they do not increase simultaneous held credit or
create a wait for the owner's own held bytes. Unused variants release their
credit. No arbitrary capacity margin substitutes for these branch calculations.

Cancellation and caller deadlines prevent fresh admission/acquisition. Once
sticky cancellation closes owner admission, repeated calls stop before argument
acquisition; the sampler emits its held owner refusal and failed END once. It
does not claim an event for every unadmitted call. An already entered native call
may finish, with its actual result preserved. Cancellation interrupts the
sampler's capacity/control-input waits; it does not forcibly interrupt an
entered native syscall. The sampler has no periodic timer or scheduling policy.

`finish()` publishes owned closes and session END from held credit. A killed
owner cannot manufacture END; raw orphan recovery preserves its published
originals and records the runtime's separate death/failure outcome. Acquisition
stop and orderly preservation shutdown are distinct: `RawRuntime` destruction
can still wait for supervisor/recorder drain, including a stopped recorder.
Power loss remains outside the selected durability contract.

## Direct qualification

The focused test forwards the real native OS calls and copies their actual
returns immediately into test-only shared memory. It compares preserved output
to those originals, including setup factors/fields, native clock returns and
resource bytes. Production has no test callback. Independent32-byte wire
expectations, actual CLI execution, fragmented/malformed/partial commands,
unsupported originals, native target/clock failures, and exact minimum/small
capacity lanes exercise separate contracts.

The full-lane fixture observes the real slow-path native poll, interrupts it via
the caller's stop pipe, and checks no resource call occurred. The target completes
its own pipe transaction while sampler output is full. The killed-worker fixture
stops the current registered recorder/supervisor before acquisition, observes
P>0/A=0, compares pending originals to the real OS returns while the owner lives,
then kills those exact owners, observes catalogue-lease release, and requires one
unchanged recovered copy with no fabricated sampler END. Failed assertions clean
up only fixture-owned process IDs. Linux additionally checks directory rename/
symlink replacement, borrowed descriptor ownership and wrong-filesystem refusal.
macOS results do not substitute for running Linux's conditional native branches.
