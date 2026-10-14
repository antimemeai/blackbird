# Actual RawRuntime workloads — 2026-10-14

All fourteen cases pass against unchanged source 200fc5e (HEAD 33e78d1 adds
publication notes). The actual separate RawRuntime recorder preserves 218,946
unique events, and the independent closed-run disk oracle checks every event's
lane, partition, site, ordinal, source offset and complete body. No product
source changed and no optimization was attempted. This report describes the
raw transport; owner integration/profile results are a separate report.

The cases use macOS 26.6 ARM64, LLVM 23.1.2, Release/OFF and the current
libblackbird_foundation.a. Each starts a fresh native runtime and session with
1 MiB DATA/4 KiB CONTROL/4 KiB FAILURE and actual full-sync disk preservation.
These deliberately small grants exercise wrap and preservation backpressure;
they do not select or qualify the launcher's production grant sizing. No local
Blackbird build/test/startup campaign runs concurrently. Other host work is
uncontrolled: load averages at the boundaries are23.74/14.56/22.75 and
24.40/14.85/22.81. No affinity or cache flush is applied. Linux work runs on a
different host. This is a local observation, with retained scheduling outliers.

Three continuous trials per size emit64 B×40,000,256 B×20,000 and4096 B×5,000
prepared bodies. Body generation occurs outside each timed claim/emit pair.
`raw_claim_data` is timed separately; `raw_emit` has prepaid capacity and includes
publication and any required native doorbell. The loop and pipeline timings
also include test body generation, clocks and bookkeeping. They are not isolated
production instruction costs. The final-ACK endpoint means the recorder has
preserved all emitted bytes on disk. Its100 µs polling interval and scheduler
delay make the reported final-event ACK lag an observation upper bound.

| Body / samples across three trials | Prepaid emit p50 ns | p99 ns | Max ms | Body throughput MB/s, full pipeline | Claim max ms | Final-event ACK lag ms |
|---|---:|---:|---:|---:|---:|---:|
|64 B /120,000|42|84|0.821|32.82–79.64|14.08|7.04–22.88|
|256 B /60,000|42|166–208|4.317|49.17–114.31|13.12|6.51–9.81|
|4096 B /15,000|208–250|2000–2542|15.481|40.38–189.91|72.80|4.50–11.88|

The p50/p99 ranges report each complete trial; maxima include all samples.
Claim p50 is42 ns in all burst trials. Claim p99 is83 ns for64/256 B and
0.906–2.763 ms for4096 B. The1 MiB ring causes real waiting in these larger-body
streams. Complete pipeline times are32.15–78.01 ms,44.79–104.12 ms and
107.84–507.17 ms respectively. The variation and large tails remain in the
results; no failed, slow or first-touch sample is excluded.

Timing resolution limits the small-body figures. Ten thousand empty timing
pairs have p50=41 ns,p99=42 ns,max=397,583 ns. A42 ns median sits at this observer's
granularity and overhead; it does not resolve a precise42 ns intrinsic emitter
cost. Ten thousand actual `raw_local_clock(false)` acquisitions have
p50=42 ns,p99=84 ns,max=261,250 ns. With the thread-CPU read enabled,
10,000 acquisitions have p50=209 ns,p99=375 ns,max=501,417 ns. Every native clock
acquisition succeeds. These clocks are measured separately; prepaid raw_emit
does not include an owner's clock acquisition or complete hook behavior.

First-event emit observations across the three burst trials are
5.750–7.834 µs for64 B,8.166–10.125 µs for256 B, and1.458–10.209 µs for4096 B.
They are first events in fresh mappings with ordinary page/cache state.
Later events are not certified warm: newly touched ring pages and scheduler
interference remain possible. This is not a cache-flushed cold-start comparison.

The finite idle cases preserve each event, allow5 ms of scheduler opportunity
before the next one, then emit again. Thirty events per size give prepaid emit
medians14.958 µs,15.291 µs and24.250 µs, with maxima1.521 ms,0.447 ms and0.243 ms.
Claim medians are1.916 µs,2.167 µs and2.041 µs. These are paced, preserved-event
observations; neither the recorder's exact sleep state nor syscall/wake counts
was instrumented. We do not attribute the difference from burst costs to one
particular mechanism. With30 samples the p99 is the maximum, not a tail estimate.

The reader-contention case emits 20,000×256 B. A concurrent tertiary
RawRunReader resumes across6 preserved snapshots and independently checks
every event while the recorder runs. The final closed disk run also matches
all 20,000 events exactly. This trial has 42 ns emit median,167 ns p99,2.336 ms max,
37.03 ms full pipeline and138.28 MB/s body throughput. One such trial does not
establish reader contention's general performance effect; no speedup is inferred
from it finishing faster than the three non-reader trials.

The full-buffer case uses the real recorder PID, SIGSTOP and the native process
state to establish suspension. It emits 3855×256 B, filling1,048,560 of the
1,048,576 DATA bytes. Additional nonwaiting capacity fails with the exact
capacity error. After SIGCONT, an actual capacity claim and the next publication
succeed in1.280 ms. All3856 events remain on the exact closed disk run; no failure
event, duplicate, lost event or poisoned admission follows the refusal. Existing
admission/cancellation and recorder-death fixtures cover those fault contracts
separately rather than duplicating them here with a weaker timed cancellation.

Recorder CPU during the burst-to-final-ACK window is21.67–30.01 ms for64 B,
28.26–34.60 ms for256 B and71.96–105.28 ms for4096 B. The reader trial observes
25.08 ms and the full-buffer trial5.15 ms. These are the recorder process's native
user+system counters, excluding supervisor and producer CPU and later closure.

The first dev-driver field unfortunately labels raw CPU ticks as
`recorder_cpu_ns`; that label is wrong. The raw logs and first summary are
preserved unchanged. During results inspection, Apple XNU's
[fill_task_rusage](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/kern/bsd_kern.c)
and [absolute-time conversion in calcru](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_resource.c)
establish the native counter units. A direct `mach_timebase_info` query on this
host returns numer=125,denom=3. The corrected summary converts those retained
counters by 125/3, and also records the original ticks. No workload rerun is
needed to correct a consumer-side unit label. CPU figures from the original
raw-label summary must not be used as milliseconds.

Allocation, copy, RMW, syscall and wake counts were not intercepted. This does
not qualify owner hook admission/formatting/clock costs, complete interactive
workloads, production capacity sizing, heavy/reopened sessions, Linux workload
performance or sustained long-running storage behavior. The ACK/stop testcase
watchdog detects stuck progress; raw_claim_data itself has no driver deadline.
Host power loss remains outside the contract. No claims of fixed tail limits
or readiness for tuning follow from these observations.

The [written plan](qualification-2026-10-14/raw-workload/PLAN.md),
[complete fourteen-case summary](qualification-2026-10-14/raw-workload/summary.csv),
[commands and native outcomes](qualification-2026-10-14/raw-workload/runs.log),
[clock summary](qualification-2026-10-14/raw-workload/clock-summary.csv),
[first-event samples](qualification-2026-10-14/raw-workload/first-events.csv),
[environment and binary hashes](qualification-2026-10-14/raw-workload/environment.txt)
and [native timebase](qualification-2026-10-14/raw-workload/mach-timebase.txt)
are tracked selections. The exact [driver](qualification-2026-10-14/raw-workload/probe.cpp.txt)
and [runner](qualification-2026-10-14/raw-workload/run.py.txt) are text snapshots
of the executed dev machinery, including the historical CPU-field label.
All individual latency/clock samples, native raw audit runs and original logs
remain in ignored context/qualification-2026-10-14/raw-workload. The corrected
summary retains the raw CPU ticks for reproduction.

Reproduce by copying the source snapshot to an ignored scratch `.cpp` and
linking the unchanged current Release foundation archive with LLVM 23.1.2,
`-std=c++20 -O3 -DNDEBUG -DBLACKBIRD_DEBUG=0 -arch arm64`, the actual macOS
sysroot and `-I include -Wall -Wextra -Wpedantic -Werror`. Run the runner in
a fresh output directory with no concurrent local campaign. Its emitted CPU
counter field requires the measured native Mach timebase conversion described
above; the byte/count oracle and claim/emit elapsed timings do not.
