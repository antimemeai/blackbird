# RawRuntime workload measurement

Test unchanged source 200fc5e (HEAD 33e78d1 only adds publication notes). Link
the current Release/OFF foundation archive; one scoped compiler invocation,
then run only when laptop builds/tests/startup measurements are idle.

Exercise the real native RawRuntime producer, separate recorder, full disk sync
and RawRunReader. Use a 1 MiB DATA grant (4 KiB CONTROL/FAILURE), deliberately
smaller than the emitted stream, so ordinary sustained emission wraps and waits
for actual recorder preservation. Bodies are 64, 256 and 4096 bytes. Every body
contains a unique event number plus a known position-dependent byte pattern.
Prepare that pattern outside each timed call. Save every claim and prepaid
raw_emit latency, plus whole producer-loop/final-ACK timings. Observe preserved
bytes with atomic RawChannel cursors; do not mutate recorder state or pretend
ACK observation is an exact disk-sync instant. Polling adds bounded observation
delay. Acquire clocks in the test process, not inside product code.

Separately measure 10,000 empty timing pairs, 10,000 actual raw_local_clock(false)
and 10,000 raw_local_clock(true) calls. Require native acquisition success; retain
each sample. Prepaid raw_emit timings do not contain owner native clock calls.

Cases: continuous bursts for each body size; finite 5 ms paced emissions for
idle-recorder behavior (a scheduling opportunity, not a verified sleep state);
a concurrent resumable RawRunReader consumer; and a recorder SIGSTOP case that
fills DATA, directly refuses additional nonwaiting capacity, resumes the real
recorder and then admits another event. Existing admission/cancellation and
recorder-death fixtures cover those fault contracts separately; this driver
does not duplicate them with weaker timing-based cancellation assertions.

After native closure, read the preserved run and independently build each
expected little-endian envelope and unique body. Require exact lane,
partition, site, ordinal, source offset, body bytes, count and closed-run state.
The concurrent reader uses the same direct event-specification oracle while
the recorder runs. Save raw CSVs, native run directories, command/configuration
and summary. A testcase watchdog detects a stuck ACK/stop; no campaign timer.

Report recorder CPU from native process counters when available. No claim of
allocation/syscall attribution, cache flushing, power-loss qualification,
interactive owner-loop performance or tuning. No product edits or optimization.
