# Raw transport performance round after repairs — 2026-10-14

All 62 native workload cases pass against repaired source **66547c6**, using fresh
Release/OFF archives on macOS ARM64 and Linux x86_64. The closed-run disk oracle
checks **1,039,144 events**, plus **10,000 events** in one separate Linux syscall
trace. Every event matches the expected lane, DATA partition, source offset,
site, ordinal and complete unique body bytes. No product code changed and no
optimization was attempted.

The prepaid emitter remains inexpensive in ordinary bursts, while a larger
grant lets the producer get much farther ahead of disk preservation. The two
128 MiB body streams expose that difference directly. Larger grants also have
higher observed 4 KiB emission costs on this Linux host. These are measurements
to discuss, not a capacity-selection or optimization decision.

## Cases and environment

Each host runs the fourteen prior 1 MiB-pressure cases: three trials each of
64-byte × 40,000, 256-byte × 20,000 and 4096-byte × 5,000 bursts; thirty paced,
individually preserved events per size; a concurrent resumable reader checking
20,000 × 256-byte events; and actual recorder SIGSTOP/full-capacity refusal/
SIGCONT with 3,856 events. Thirteen burst/paced/reader cases repeat at the
launcher's **256 MiB DATA/1 MiB CONTROL/128 KiB FAILURE** grants. Each grant also
gets one **128 MiB body stream**, 32,768 × 4096 bytes, and a separate 10,000 ×
256-byte producer C++ allocation census. This is 31 runtime cases per host.
The pressure configuration is 1 MiB DATA/4 KiB CONTROL/4 KiB FAILURE.

Both use the actual separate RawRuntime recorder and full-sync preservation.
Producer-loop timing includes deterministic body preparation, timing and driver
bookkeeping. Claim timing includes real capacity waiting; prepaid emit timing
starts after claim. ACK polling every 100 microseconds plus scheduler delay makes
final-event preservation lag an observation upper bound. Disk byte verification
and CSV writing happen after closure, outside the performance window.

Local native builds and other Blackbird measurement lanes were held during each
host's raw window. Other host work remains uncontrolled. macOS 26.6/LLVM23.1.2
has 16 logical CPUs and load averages 3.18/6.60/5.95 before and 3.28/6.45/5.91
after; Linux6.8/Clang18.1.3/libstdc++13 has 48 logical CPUs and loads
24.73/23.79/15.76 before and 24.04/23.69/15.85 after. No affinity, cache flush,
first-touch exclusion or outlier removal was applied. Trials run once in the
written order; these observations do not establish fixed tail limits.

## Burst costs

Ranges below cover the three individual trials; maxima retain every sample.
64/256/4096-byte sizes respectively have 120,000/60,000/15,000 burst samples at
each grant on each host.

| Host / DATA grant | Body | Prepaid emit p50 | p99 | Largest emit | Full-pipeline body MB/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| macOS / 1 MiB | 64 B | 42 ns | 84 ns | 0.688 ms | 37.05–162.93 |
| macOS / 1 MiB | 256 B | 42–83 ns | 84–125 ns | 0.134 ms | 77.70–308.64 |
| macOS / 1 MiB | 4096 B | 209–417 ns | 1.67–3.50 µs | 0.206 ms | 85.33–181.60 |
| macOS / 256 MiB | 64 B | 42 ns | 84–125 ns | 0.166 ms | 115.04–172.64 |
| macOS / 256 MiB | 256 B | 42 ns | 1.13–1.75 µs | 0.335 ms | 157.42–254.04 |
| macOS / 256 MiB | 4096 B | 250 ns | 3.13–3.79 µs | 0.644 ms | 237.14–303.95 |
| Linux / 1 MiB | 64 B | 84–89 ns | 167–203 ns | 0.051 ms | 64.47–68.16 |
| Linux / 1 MiB | 256 B | 93–101 ns | 3.13–3.25 µs | 0.022 ms | 116.19–138.47 |
| Linux / 1 MiB | 4096 B | 429–438 ns | 4.77–5.45 µs | 0.028 ms | 156.81–169.18 |
| Linux / 256 MiB | 64 B | 81–90 ns | 3.09–3.73 µs | 0.036 ms | 65.27–71.57 |
| Linux / 256 MiB | 256 B | 90–98 ns | 4.11–4.65 µs | 0.074 ms | 128.43–139.31 |
| Linux / 256 MiB | 4096 B | 3.40–3.44 µs | 17.67–18.97 µs | 0.136 ms | 66.47–171.92 |

macOS's 42 ns small-body median is at its measured empty-pair floor, so it does
not resolve a precise 42 ns intrinsic emission cost. Linux's empty-pair median
is 26 ns. Native clock capture is measured separately: monotonic-only median
83 ns on macOS and 56 ns on Linux; monotonic plus thread-CPU median 250 ns and
697 ns. Each of the three clock modes has 10,000 retained samples per host and
all real native acquisitions succeed. Prepaid emit omits a complete owner's
capture/admission/formatting work.

## Sustained stream, preservation and capacity waiting

Each row is one finite 128 MiB body stream; records add a 16-byte envelope.

| Host / DATA grant | Producer loop | Complete preservation | Body MB/s | Final-event ACK lag | Largest claim wait | Recorder CPU |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| macOS / 1 MiB | 346.78 ms | 349.18 ms | 384.38 | 2.41 ms | 2.07 ms | 296.66 ms |
| macOS / 256 MiB | 78.90 ms | 475.19 ms | 282.45 | 396.28 ms | 0.021 ms | 381.18 ms |
| Linux / 1 MiB | 930.00 ms | 940.14 ms | 142.76 | 10.16 ms | 18.09 ms | 570 ms |
| Linux / 256 MiB | 229.18 ms | 870.63 ms | 154.16 | 641.45 ms | 0.015 ms | 550 ms |

The launcher grant removes most capacity waiting in these finite streams and
allows hundreds of milliseconds of recorded preservation backlog. On macOS the
larger-grant stream completes preservation more slowly in this single trial;
on Linux it completes somewhat sooner. We do not infer an optimal grant or a
causal explanation for the different emitter/page/scheduler costs.

Recorder CPU uses actual native process counters during burst-to-final-ACK:
macOS rusage ticks converted by measured Mach numer=125/denom=3; Linux
/proc/PID/stat user+system ticks converted by actual SC_CLK_TCK=100. Linux's
10 ms units cannot resolve small-case CPU reliably: an observed zero tick delta
means below the counter's resolution, not zero CPU work. Supervisor, producer,
reader and later closure CPU are excluded.

Paced emission has thirty individually preserved events per size separated by
5 ms of scheduling opportunity. Across sizes/grants, emit medians are
9.0–23.1 µs on macOS and 18.6–49.3 µs on Linux; maxima are 85.3 µs and 63.9 µs.
The recorder's exact sleeping state is not established, and thirty samples make
p99 the maximum rather than a useful tail estimate.

Both concurrent-reader cases per host verify all 20,000 events while the
recorder runs and again on the closed run. They finish at 102.48–218.31 MB/s on
macOS and 111.61–130.41 MB/s on Linux. These are single trials, not a claim that
readers accelerate or slow the recorder. Both real full-buffer cases refuse
additional nonwaiting capacity with the exact capacity error, resume and preserve
all 3,856 events without loss/duplication/fencing. Resume-through-next-emission
is 0.928 ms on macOS and 2.780 ms on Linux.

## Direct allocation and syscall observations

Separate allocation-census cases count successful global C++ new/new[] and
aligned-new calls on the emitting thread, only during each actual claim/emit
pair. Both grants on both hosts observe **zero calls and zero allocated bytes**
across 10,000 events per case, 40,000 total events. This does not count direct
malloc, recorder allocation, preparation, complete owner hooks or native library
allocation outside that window. Normal latency trials leave counting off;
their raw driver zero fields are not allocation measurements. The tracked
summary leaves those fields blank for all non-census rows.

The separate Linux strace trial also preserves every one of its 10,000 ×
256-byte events at the 1 MiB grant. Its producer trace between explicit dev-only
emission-loop markers records **56 syscalls**: 14 poll, 7 read, 13 recvfrom,
7 recvmsg, 8 sendto and 7 write. Seven recvfrom return EAGAIN during nonblocking
checks. Marker writes, setup, final ACK observation, closed-disk reading and
other processes are excluded from this count. Tracer timing is excluded from
the ordinary latency tables; tracing changes scheduling. This is one direct
trace observation, not a fixed per-event syscall rate. Kernel wake/context-switch,
copy and atomic-RMW counts remain unmeasured.

## Retained results and limits

The [written plan](perf-round-2026-10-14/raw/PLAN.md), exact executed
[probe](perf-round-2026-10-14/raw/probe.cpp.txt) and
[runner](perf-round-2026-10-14/raw/run.py.txt), complete
[macOS summary](perf-round-2026-10-14/raw/mac/summary.csv) and
[Linux summary](perf-round-2026-10-14/raw/linux/summary.csv), commands,
environment/archive/binary/source hashes and clock summaries are retained
alongside this report. The [producer syscall window](perf-round-2026-10-14/raw/trace/producer-window.txt)
and [counts](perf-round-2026-10-14/raw/trace/syscall-counts.txt) retain direct trace
output. Tracked CSVs use LF; original runtime outputs and all per-event samples
remain unchanged in ignored context/perf-round-2026-10-14/raw, including the
copied Linux native audit archive. Summary allocation blanks are a consumer-side
presentation correction; no workload rerun or native result alteration follows.

This round qualifies the stated raw workloads on these two hosts. It does not
measure power-loss behavior, cache-flushed cold/warm behavior, a steady-state
24-hour recorder, a host with a different filesystem, all owner-hook costs or
production-grant saturation. The stopped full-grant case is deliberately the
1 MiB pressure grant. Existing correctness fixtures cover cancellation/recorder
failure separately; no weaker timed duplicate is added. ACK/stop progress has a
testcase watchdog; blocking native claim has no extra driver deadline. Startup,
input and complete owner-path timings are reported by their separate owners.
