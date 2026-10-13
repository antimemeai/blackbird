# Startup after operation/storage/retained delivery — 2026-10-13

Current source: ca6747b, published with qualification in5757ae6. Current executable is
build/nls-storage-release/blackbird, Release/OFF. Existing operator artifacts remain
unchanged. This measures startup/input, not owner-loop throughput or preserved-event lag.

Existing scripts/benchmark-agent-startup ran20 starts for each of three binaries,
serial round robin through the same scripts/blackbird launcher with an explicit
executable selection. Each launch uses a private fresh session,110×35 PTY and
Ghostty identity, ordinary unflushed macOS caches and terminal query replies.
The parent monotonic clock starts before spawn. The endpoint is a complete
input frame; an unsubmitted marker must then appear in the committed screen.
No inference prompts are submitted. All60 starts reach both endpoints and exit
zero, with no forced shutdown or excluded/retried samples.

| Executable | First frame p50 ms | p90 ms | max ms | Typing p50 ms | p90 ms | max ms |
|---|---:|---:|---:|---:|---:|---:|
| published-ui | 96.066 | 159.398 | 1191.178 | 1.926 | 2.459 | 3.931 |
| previous-release | 123.369 | 161.341 | 206.648 | 2.545 | 5.806 | 9.414 |
| current-release | 126.465 | 165.234 | 363.068 | 2.105 | 6.811 | 66.561 |

The current fresh-start median is126.465ms versus123.369ms for the older
Release artifact in this batch (+3.096ms, about2.5%). The published UI median
is96.066ms. Those older executables represent different generations; their exact
parent source is not inferred from their filenames, and this is not an exact
before/after attribution to the latest segment. Twenty samples characterize
this local run, not a tail guarantee. Current startup max363.068ms and typing
max66.561ms are retained. Heavy/reopened sessions, input-to-photon rendering,
RSS, recorder CPU/lag and full operation/storage admission costs are unmeasured
here and remain the nls.2 workload task. Earlier primitive83ns measurements are
from older source and are not substituted for post-segment owner costs.

Exact specs, executable hashes, samples, host load and raw PTY transcripts are
in ignored context/startup-after-owner-segment-2026-10-13. Native hashes:

- published-ui: 562062042759cf5bb8bc7d3927b8780cc910e00e78b07a22fc7c600c238b0826
- previous-release: 79ed820f08ff2937cb892dddfcddff2b88065028d0611d11c4bf79a7163f302a
- current-release: e550adcbbb7369066ff948feec3e82d4897557ed8b3ec9481d76fef8b12b6cd5
