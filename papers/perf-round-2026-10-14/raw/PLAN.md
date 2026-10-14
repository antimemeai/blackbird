# One raw transport performance round after repairs

Use repaired source 66547c6 and a freshly built Release/OFF foundation archive.
No product edits, tuning, provider calls or repeated assurance campaign. Run
native timing only during the parent-owned idle measurement window.

Reuse the existing literal-byte RawRuntime probe, real separate full-sync
recorder and independent closed-run disk oracle. Preserve every event and every
latency sample. Retain the fourteen existing pressured 1 MiB cases: three trials
per 64/256/4096-byte burst, thirty paced events per size, resumable reader, real
SIGSTOP/full-capacity refusal/SIGCONT. Repeat burst/paced/reader at the launcher's
256 MiB DATA/1 MiB CONTROL/128 KiB FAILURE grants. Compare observed results;
neither grant is selected or tuned here. Add a finite 128 MiB body stream using
4096-byte events at each grant, and a separate 10,000-event allocation census
for claim/emission at each grant. Native global C++ new replacements count only
the emitting thread during that census; they do not count malloc, recorder
allocations or complete owner hooks. Ordinary latency runs leave counting off.

Portable recorder CPU reads native macOS process-rusage ticks converted with
mach_timebase_info; Linux reads /proc/PID/stat user/system ticks converted with
actual sysconf(_SC_CLK_TCK). Retain raw counter units and conversion alongside
results. Ten thousand samples each for empty timing pairs and actual native
monotonic/thread-CPU capture retain observer granularity.

The exact disk oracle checks lane, DATA partition, source offset/order,
little-endian length/site/ordinal, complete deterministic unique body, event
count and closed state. The concurrent reader checks the same event contract.
ACK is polled every 100 microseconds: preservation lag is an observed upper
bound with scheduler delay. Whole-pipeline throughput includes body preparation,
clocks and driver bookkeeping. No cache flush, affinity or discarded outliers.
The existing per-ACK/stop watchdog detects stuck progress; it is not a campaign
timer, and blocking native claim has no added driver deadline.

Report copied bytes/RMW/syscall/wake counts only with direct actual counters or
separate OS tracing. Otherwise mark them unmeasured rather than infer numbers
from source. Preserve all raw logs/source/environment/binary/archive hashes.

If existing Linux strace permissions allow it, one separate 10,000 x 256-byte
pressured-ring trace records producer syscalls between explicit dev-driver
stderr markers around the emission loop. Keep tracer timings separate from
ordinary latency results; include native recorder/supervisor trace files but
count only the producer marker window. No product tracepoints are added.
