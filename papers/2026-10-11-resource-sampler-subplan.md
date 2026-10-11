# External native resource acquisition

Study/subplan only. The reader checkpoint is complete; sampler source/build waits
for the parent's integrated checkpoint release. The parent attacked this design
once; this revision resolves that attack's concrete requirements without adding
another review panel.

## Purpose and process ownership

An independently launched `blackbird-resource-sampler` acquires native process
observations and emits their original bytes through its own RawRuntime lane and
separate recorder. Blackbird execution gains no periodic timer, resource walk,
callback, metric calculation, or library. Ops/evals consume preserved records and
perform interpretation, differences, scaling, ratios and aggregation afterward.

The controller supplies explicit requests through a native pipe, or supplies one
request to the one-shot API. The controller owns cadence, optional deadline and
sticky stop/death descriptors. It keeps borrowed descriptors alive through the
sampler call, lane retirement and any orderly shutdown waits. There is no default
sampler frequency. Readiness of stop/death input is never consumed by admission.
The worker is the only lane owner and the only reader of its control pipe.

The sampler opens RawRuntime before acquisition, under the explicit shared
session/environment identity, creating a separate capture run. It neither signals
nor controls the target, joins its threads, nor adopts owner-only counters. The
existing process-lifetime child's count/spawn mutex/uncontained flag remain in
that owner. Coherent allocator/queue/Lua observations belong to their owners'
hooks. Existing run leases protect active runs; orphan recovery preserves bytes.

## Native sources and process identity

Primary native sources, exact acquired hashes and restoration commands are in
[the acquisition report](2026-10-11-resource-sampler-references.md). The settled
qmail/daemontools transport separation is reused; no new framework is selected.

| Method | Raw source | Meaning and incarnation |
| --- | --- | --- |
| 1, macOS `proc_pid_rusage(pid, RUSAGE_INFO_V0, ...)` | Native int flavor/return; actual errno on failure; success-only UUID[16] and every one of the ten native u64 fields in SDK order | The kernel references one live/zombie proc for this call. PID and returned start absolute time identify the observed incarnation. UUID identifies its executable, not the process. Values concern this process, not an arbitrary command job. |
| 2, Linux descriptor-relative `/proc/PID/stat` | Actual open/fstatfs/openat/read/close results; each positive read's exact bytes, native ssize_t result, failures and EOF | One held PID-directory descriptor spans this request's stat-file acquisition and all reads. It cannot redirect to a replacement PID. Consumers decode start-time/resource fields after preservation. |
| Linux `sysconf(_SC_CLK_TCK)`, `sysconf(_SC_PAGESIZE)` | Selector, native long return; errno when return is -1, including actual zero errno if indeterminate | Setup constants; no conversion of ticks/pages. |
| macOS `mach_timebase_info` | Native kern_return_t; numerator/denominator only on success | Setup factors; no collector arithmetic. |
| Both `clock_gettime(CLOCK_MONOTONIC, ...)` | Separate request/return; native int return, failure errno, success-only tv_sec/tv_nsec | Locate native acquisition boundaries; no duration calculation. |

Each complete controller command explicitly requests the process currently
observed at its numeric PID. It is a **new attachment**, even if the preceding
command used the same number. Linux opens that directory once per command and
holds it through this command; it never reopens the PID pathname during reads.
macOS returns actual start-time in the same rusage call. A later command can
observe another incarnation and retains that command's own observations. This
interface does not promise attachment to a historic child solely from its PID.
A controller wanting that distinction consumes the preserved incarnation fields;
there is no writer-side stat parser or fabricated cross-platform process handle.

The inspected XNU implementation holds one proc reference, checks permission and
collects rusage under several owner locks. The tuple is not an atomic system
snapshot. Kernel Linux documentation says old proc descriptors do not prevent PID
reuse, but operations through them do not act on the replacement process.
Permission/unavailable/dead-target outcomes are recorded failures, not zero-valued
measurements. FD/thread enumeration and private allocator state require distinct
methods later. Process-wide values never become job metrics by assumption.

## Controller protocol and preservation

Wire v1 is exactly32 bytes, host-native like the raw setup ABI: magic[8]
`BBRSSCTL`, u32 version=1, u32 method, u64 controller request identity, native
pid_t (qualified ABI i32), u32 reserved=0. Mac and Linux qualified profiles are
little-endian; incompatible ABI/version/magic/method/nonpositive PID/reserved
bits are refused. No JSON persistence or IPC. Configuration parsing occurs at
process setup. Interpreting this command protocol is control work, not a metric
payload transformation.

A pipe read asks for at most the remaining bytes of one32-byte frame. Before
**each** read, claim the exact control-read request and maximum raw outcome credit.
Emit a read request; perform the native read; immediately emit its exact return,
failure errno or positive bytes. Only afterward copy positive bytes into the
frame buffer and validate a complete frame. Each partial read and EINTR has its
own outcome; retry is another request/return. All malformed bytes are preserved.
EOF with an empty frame ends input normally. EOF after partial input emits the
actual zero-return outcome and a linked `ErrorCode::incomplete` refusal; no
resource acquisition follows. Unsupported method likewise produces refusal
before acquisition. Controller identity exists only after valid full framing;
raw control-read linkage identifies malformed/partial input independently.

Poll the control and sticky stop descriptors before blocking pipe input. With one
reader and a native pipe, a positive readiness read of at most32 bytes does not
wait for a whole frame. A stop racing a ready command is checked before fresh
acquisition. Synchronous native syscalls already entered are allowed to finish;
stop does not pretend to cancel the kernel operation or erase its outcome.

## Linux mount and descriptor seam

Setup opens the real `/proc` directory with O_RDONLY|O_DIRECTORY|O_NOFOLLOW|
O_CLOEXEC, then fstatfs on that same descriptor. Preserve the actual syscall
result; copy all returned statfs native fields individually only on success,
including fsid/spare fields. Require f_type == PROC_SUPER_MAGIC before any PID
attachment. An incorrect filesystem produces a linked refusal, closes its held
root descriptor and performs no target acquisition. The inspected Linux profile
has120 bytes of individually copied statfs fields; setup ABI descriptions and
compile-time width checks must match the selected native headers.

The validated proc-root descriptor is held for the sampler's lifetime. Attach
using openat(root_fd, PID decimal setup/control name, O_DIRECTORY|O_NOFOLLOW|
O_CLOEXEC), then openat(pid_fd, "stat", O_RDONLY|O_NOFOLLOW|O_CLOEXEC). Decimal
control addressing is external acquisition setup, never formatting a metric.
Close stat/PID descriptors at command END; close proc-root at sampler END.

The owned API separates `open_native_proc_root()` (always verifies real procfs)
from descriptor-relative acquisition using an already-held PID directory. The
latter is the explicit test seam: descriptor rename/symlink replacement fixtures
exercise anchoring without replacing production OS calls with callbacks.
Production only enters it from verified proc-root attachment. No repeated textual
PID path lookup, final-component-only claim, or fake procfs acceptance.

## Sites, immutable descriptions and exact bodies

Reserve7000–7031 for this unit. All Ref values are lane[16]+native u64 ordinal
(24 bytes). E=16 is the raw event envelope. Body fields are concatenated native
copies; no C++ struct padding/pointers. Success-only outputs are absent on failure;
errno is copied immediately after the failed native call. The native return is
always present. No raw read content is parsed, projected, scaled or compressed.

| Site | Body, in order |
| --- | --- |
| 7000 session.begin | u32 supported method bits |
| 7001 session.end | session Ref, u32 outcome; on failure ErrorCode u8,int64 detail |
| 7002 control.read.request | int fd,size_t maximum |
| 7003 control.read.return | request Ref,ssize_t return; positive: exactly return bytes; negative: int errno |
| 7004 acquisition.begin | final control-read Ref (or session Ref for one-shot),u64 controller identity,pid_t,u32 method |
| 7005 acquisition.end | acquisition Ref,u32 outcome; on failure ErrorCode u8,int64 detail |
| 7006 refusal | originating Ref,ErrorCode u8,int64 detail |
| 7007 constant.request | session Ref,int sysconf selector |
| 7008 constant.return | request Ref,long return; when -1: int errno |
| 7009 clock.request | acquisition Ref,clockid_t,u32 boundary role (before/after) |
| 7010 clock.return | request Ref,int return; success: time_t tv_sec,long tv_nsec; failure: int errno |
| 7011 proc.root.request | session Ref,int flags |
| 7012 proc.root.return | request Ref,int return fd; failure: int errno |
| 7013 proc.fstatfs.request | root-open Ref,int fd |
| 7014 proc.fstatfs.return | request Ref,int return; success: all statfs native fields; failure: int errno |
| 7015 proc.attach.request | acquisition Ref,pid_t,int flags |
| 7016 proc.attach.return | request Ref,int return fd; failure: int errno |
| 7017 stat.open.request | attachment Ref,u32 fixed file method,int flags |
| 7018 stat.open.return | request Ref,int return fd; failure: int errno |
| 7019 stat.read.request | stat-open Ref,size_t maximum |
| 7020 stat.read.return | request Ref,ssize_t return; positive: exactly return bytes; negative: int errno |
| 7021 rusage.request | acquisition Ref,pid_t,int flavor |
| 7022 rusage.return | request Ref,int return; success: UUID16 then ten u64 fields; failure: int errno |
| 7023 close.request | open Ref,int fd |
| 7024 close.return | request Ref,int return; failure: int errno |
| 7025 timebase.request | session Ref |
| 7026 timebase.return | request Ref,kern_return_t; success: u32 numerator,u32 denominator |
| 7027–7031 | Reserved, not emitted |

Descriptions are immutable and installed before activation: platform/method,
field widths/order, units, success/failure variants, controller wire ABI, fixed
path choices and read bound. Each native resource field is copied individually.
Clock errno on failure never accompanies a pretend timespec. Actual clocks are
separate events, not hidden calculations inside resource returns.

## Admission: finite per-observation credit

The qualified 64-bit profiles use int/pid_t/clockid_t/kern_return_t=4,
size_t/ssize_t/long/time_t=8. The following maxima **include E**. Width guards and
immutable descriptions accompany these formulae; no unexplained safety margin.

| Native pair/body | Exact maximum bytes |
| --- | ---: |
| control read request + return (maximum32) | 28+80=108 control |
| clock request + return | 48+60=108 control |
| generic open request + return | 48+48=96 control |
| proc-root open pair (no PID/file method) | 44+48=92 control |
| fstatfs pair (120 native output bytes) | 44+164=208 control |
| close pair | 44+48=92 control |
| sysconf pair | 44+52=96 control |
| timebase pair | 40+52=92 control |
| acquisition BEGIN / worst END / refusal | 56 / 53 / 49 control |
| Linux read request / failure-or-EOF return | 48 / 52 control |
| Linux positive read return, maximum4096 | 16+24+8+4096=4144 data |
| macOS rusage request / failure return | 48 / 48 control |
| macOS success return | 16+24+4+96=140 data |

Before native setup, hold session BEGIN20, END53, one setup refusal49, and input
close pair92 (if the sampler owns that pipe). Linux additionally holds proc-root
open92, fstatfs208, eventual root close92 and two sysconf pairs192: **798 control**
maximum. Mac additionally holds timebase92: **306 control** maximum. Borrowed
input has no sampler close, subtract92. Keep eventual close/END credit held for
its owner's lifetime; release unused branch credit after its branch ends.

Before each control read hold108; hold its possible malformed-frame refusal49
before validating newly preserved bytes. Empty EOF uses the already-held session
END. Acquisition starts only after complete valid control and fresh admission.

Linux command preclaims BEGIN56, two open pairs192, their two eventual close
pairs184, END53 and refusal49: **534 control**. Failed root setup has no command.
Attach failure consumes its return and END; no stat-open/read. Stat-open failure
consumes that return, attachment close and END. On success each read, including
EOF/EINTR/error, freshly claims request48 + possible failure/EOF52 + two separate
clock pairs216 = **316 control**, and admits4144 data **before either boundary
clock or the read**. Positive outcomes use data; failure/EOF use held control.
Read count is unbounded only across separately admitted finite iterations.

Mac command preclaims BEGIN56, END53, refusal49, rusage request48/failure48 and
two clock pairs216: **470 control**, and admits140 data before BEGIN/acquisition.
No positive native output is published into control; failures never fabricate
resource fields. Any failed clock is recorded and does not erase the resource
syscall result. Finite shutdown ownership credit is distinct from each next read.

The sampler's sole owner performs no callbacks, Lua calls, yields or other raw
publication between final admission and the specified burst. Capacity remains
available for the admitted body; no post-read lossy space check. Use
raw_claim_control/raw_wait_data at safe boundaries with the caller's RawWaitControl.
Stop/deadline while waiting for fresh credit uses the current owner's held
refusal/END; it does not fence ordinary cancellation. Native acquisition failures
and independent raw-ingest failures remain separate. A too-small configured lane
is rejected before setup facts rather than admitted with an impossible claim.

## Stop, retirement and preservation shutdown

Stop interrupts **new acquisition/admission**. Already-entered syscalls return,
then their prepaid outcomes, descriptor closes and owner END publish. Full lanes
pause fresh work without blocking the target's execution. Recorder death retains
publications through existing named-channel recovery. No worker/library added
to the target process participates in this flow.

Orderly RawRuntime destruction currently waits for its supervisor/drain. It can
remain blocked if the recorder is stopped or storage is unavailable. Caller stop
or acquisition deadline does **not** promise a bounded orderly process exit.
The fixture must resume the recorder independently before expecting closed-run
shutdown. A forced process kill instead leaves recoverable named originals; it
cannot be called orderly closure or justify dropping/unlinking queued records.
Acquisition interruption and completion of preservation are separate outcomes.

## Direct native oracles and implementation sequence

1. After checkpoint release, add the absent owned API/CLI red cases. A real native
   child blocks on a pipe and reports PID. Controller requests and one-shot calls
   exercise actual platform methods; no main timers or default acquisitions.
2. A test executable interposes the native OS function, forwards the **real**
   kernel call, and copies that exact returned result immediately to test shared
   memory. Compare those exact bytes/fields with the actual preserved raw prefix.
   Production has no injected callback. A different later live sample cannot be
   the expected value for this call. Failed outputs are never read as meaningful.
3. Independently specified32-byte wire fixtures: two complete commands, each
   partial-read boundary, malformed bytes, unsupported method, EINTR, empty EOF
   and partial EOF. Match raw reads before validation and forbid any acquisition
   after invalid/partial EOF. Exact native clock failures retain errno and no
   fabricated timespec fields.
4. Linux: real procfs verification and an ordinary-directory refusal. Exercise
   the held-PID-descriptor seam after rename and symlink replacement. A real child
   exits/reaps; old proc handles concern that incarnation or fail, while fresh
   attachment is explicit. Preserve comm bytes containing tricky parentheses
   without collector parsing. Forward actual read/fstatfs returns for equality.
5. Mac: all native rusage fields and executable UUID/start-time distinction;
   live/zombie/dead target and permission failure where available. Forward exact
   successful/failed proc_pid_rusage and mach_timebase_info results. No artificial
   whole-tuple atomicity or test that requires arranging PID reuse.
6. Stop a real recorder, fill a lane and cancel fresh admission. The target's own
   pipe transaction still succeeds. Independently resume the recorder, then
   compare already-published bodies, held refusal/close/END and actual closed-run
   shutdown. Separately kill sampler and recover exact published bytes without
   reacquiring the historic observation. No bounded-destructor claim.
7. Relevant Mac/Linux debug/release/diagnostic checks, one targeted code/oracle
   attack, fixes/recheck and the integrated owned source gate. One build/analysis
   worker. No timers, new dependencies, mutants or redundant assurance campaign.
