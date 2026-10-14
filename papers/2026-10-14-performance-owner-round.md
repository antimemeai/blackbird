# Real owner paths: one repaired-source performance round

The declared owner workload completed on macOS and Linux Release: 42 final
case datasets per host, 100 calls per dataset, 8,400 calls total. Product source
66547c60582c160b71d6ae1eb71227adfffc1393 is unchanged. This is a measurement of
the existing paths, with an actual missing File-hook integration recorded as
open bug **arconaut-4by**. It is not a claim that all intended surfaces qualify.

Each host produced 229,476 closed raw records and 3,600 exact selected-original
checks across its 21 capture-on datasets. Native envelope widths, duplicate
identities, no ingest-failure records and selected request/result links were
checked after timing. These totals exclude provisional failed attempts.

## Workloads and observed measurements

Separate native processes ran capture off/on at 64, 256 and 4,096 prepared
bytes, alternating the order between pairs. Each case performed 100 calls.
The native driver linked the same freshly rebuilt Release archives that the
application uses. No fault-injection correctness fixture was timed. The local
fixed provider performed no inference or network request.

The table shows the 256-byte cases. Units are microseconds; quantiles use the
nearest-rank method. With 100 samples, p99 is the 99th ordered observation.

| Host/path | Capture-off median | Capture-on median | Capture-on p99 | Capture-on max |
|---|---:|---:|---:|---:|
| macOS direct File read | 15.708 | 25.416 | 125.625 | 249.167 |
| macOS direct File write | 219.916 | 275.541 | 332.166 | 409.333 |
| macOS operator read_file | 33,593.2 | 35,918.8 | 111,162 | 456,122 |
| macOS authored Lua read_file | 33,622.8 | 34,947 | 43,939.3 | 48,194.4 |
| macOS local mock request turn | 51,676.9 | 61,410.4 | 103,025 | 109,557 |
| macOS Session save | 4,255.92 | 5,133.58 | 7,225.12 | 7,227.38 |
| macOS Station admit/finish/prompt | 8,839.79 | 10,093.6 | 151,246 | 153,935 |
| Linux direct File read | 7.984 | 7.669 | 23.266 | 55.127 |
| Linux direct File write | 522.480 | 522.077 | 795.481 | 959.635 |
| Linux operator read_file | 3,046.05 | 3,918.26 | 6,165.43 | 11,720 |
| Linux authored Lua read_file | 3,355.98 | 3,225.23 | 4,698.55 | 4,774.52 |
| Linux local mock request turn | 6,058.51 | 6,870.58 | 43,041.3 | 51,178.7 |
| Linux Session save | 346.248 | 358.280 | 665.670 | 681.716 |
| Linux Station admit/finish/prompt | 554.429 | 675.904 | 1,030.74 | 1,210.81 |

All sizes, maximums, call counts, parent CPU, peak RSS, recorder close time and
record/original counts are in [owner-results.csv](2026-10-14-performance-round/owner-results.csv).
Capture-on minus capture-off medians are observed whole-call differences;
they include recorder contention and existing operation/audit work. Some pairs
are negative, and one 100-call batch per configuration cannot establish a causal
hook-cost estimate or stable tail bound. The macOS 256-byte operator call has a
456 ms maximum; similar large delays also occur without capture (for example,
the macOS 4,096-byte authored Lua case reaches about 389 ms). No origin is
assigned to these tails without a direct mechanism observation.

The scoped parent CPU intervals span 0.001733–0.996834 s on macOS and
0.000699–1.21829 s on Linux across the 100-call batches. Parent peak RSS ranges
from 3,424,256 to 115,261,440 bytes on macOS and 11,904 to 100,340 KiB on Linux;
these are process high-water marks, not isolated hook allocation costs. Recorder
shutdown reaches 12.8925 ms on macOS and 13.5625 ms on Linux. Shutdown wall time
is not a per-event preservation-lag measurement. No owner allocation, syscall,
wake or recorder CPU count is claimed.

## What the direct checks cover

- Direct native File read/write: exact prepared source/proposal bytes, actual
  file result/effect, native helper/tool/syscall request-result links, and every
  reached selected request closing once. Original byte checks compare against
  the prepared body, not the production decoder's interpretation.
- CodingEngine operator read_file: the actual operator API runs its own Lua
  wrapper. Exact result content, literal BBM2 input/result originals (independent
  byte construction), actual operation/turn/bridge links and the exact retained
  decimal-escaped wrapper source are checked.
- Authored Lua read_file: exact result content, exact authored source,
  literal BBM2 input/result originals and operation/turn/protected-bridge links
  are checked through actual turn execution.
- Local fixed-provider request turn: one real CodingEngine request per call,
  exact returned workflow result, exact native BBM2 `{"output":[]}` operation
  result in site1322, request/phase/turn/bridge links, and each independently
  read preserved fixed-JSON diagnostic original. This is the legacy mocked
  provider entrypoint; it measures request assembly, audit, stream capture and
  turn processing, not the production network adapter.
- Session save: each changed workflow setting is prepared before timing;
  resulting settings equal the complete input, and the saved workflow original
  and distinct linked Session terminal are checked. The workflow string contains
  the prepared body plus a slash and call index; the name is short because the
  real Session name limit is 128 bytes.
- Station admit/finish/prompt: each unique event is admitted and finished; its
  returned and preserved prompt equal an independent literal including the empty
  steering separator. Final business state contains all 100 events. The measured
  unit is the three calls together, with three distinct linked owner terminals.

## Missing nested File capture

The first active operator case on each host failed the intended File-original
oracle. Source study confirms the cause: [coding.cpp](../src/coding.cpp#L5230)
constructs LocalTools, then [the dispatch](../src/coding.cpp#L5247) calls
`tools.run(name, arguments)` without the optional RawAdapterContext. Its default
is nullptr. [LocalTools](../src/tools.cpp#L208) consequently creates an inactive
File tool trace, and [the File adapters](../src/tools.cpp#L77) select ordinary
File I/O. Operator and authored Lua read_file therefore emit **zero** nested
File3400/3410/3407 events, even while the whole operation, turn, bridge and
existing business audit originals emit successfully.

The same common dispatch handles read_file, write_file and edit_file. Its absent
context skips the intended File tool, helper, native syscall, original and
observer surfaces. Direct File hooks work; actual CodingEngine dispatch does
not activate them. The missing emission remains a failed coverage expectation,
not a passing integration test. The final performance driver checks what the
existing path does emit, including exact whole-call input/result originals, and
explicitly confirms the observed missing nested File events. Bug arconaut-4by
records the source points, consequence, context/lifetime/capacity study and
actual-dispatch integration cases required before a fix. No product change was
made during this round.

## Scope and attempted runs

Owners and retained state are constructed before RawRuntime starts. The measured
scope is calls on initialized owners, excluding owner construction, retained-store
creation and producer registration. Raw capture remains active throughout the
measured calls. Grants are 256 MiB DATA, **2 MiB CONTROL**, 128 KiB FAILURE; the
launcher uses 1 MiB CONTROL. No budget was tuned. Fixture preparation, raw setup,
business checks and closed-disk decoding sit outside each per-call clock pair.
Batch wall/parent CPU also include the small result checks between calls. Normal
owner call work retains its existing audit/synchronization costs. Benchmarks were
serialized against other native measurement/build work on each host; ordinary
system activity was not controlled.

Provisional attempts are retained under ignored
`context/perf-round-2026-10-14/owners/`. The first operator smoke used the authored
Lua source expectation instead of operator_call's actual escaped-name wrapper;
the source-derived literal fixed that driver expectation. The full active
operator attempt then revealed the genuine missing File integration described
above. The request attempt initially expected upstream JSON at1322 role3;
source study showed this mocked entrypoint emits the existing operation's native
BBM2 result there and stores raw callback JSON in diagnostics. Both independent
native-result and diagnostic-byte checks now run. Station's first call rejected
the driver literal because its empty-steering separator was omitted; the exact
literal was corrected from the already-read source. These are recorded separately
from the confirmed product integration deficiency.

Initial failed operator/request attempts retain their raw audit but lack saved
per-call timings: the early driver wrote samples only after its disk oracle.
The final driver writes samples before the disk oracle. Station's failed first
call had no complete 100-call dataset. Successful prior cases were kept; each
resume started at the affected unsuccessful case. No successful direct File,
caller, Lua, request or Session batch was rerun merely for later oracle changes.

The final source snapshot is
[owner-driver.cpp.txt](2026-10-14-performance-round/owner-driver.cpp.txt)
(SHA256 `8cc072970a9b1861cb6e1329ae0c49e8da167efc06dcb6f77aada360dd368642`),
with [runner](2026-10-14-performance-round/owner-run.py.txt) and
[initial plan](2026-10-14-performance-round/owner-plan.md). Earlier successful
datasets used the same timed operations with earlier post-call expectations;
the final snapshot does not claim to be their original executable image.
Linux binaries, sources, logs, per-call CSVs, diagnostics and raw runs are copied
locally as `context/perf-round-2026-10-14/owners/linux-results.tar.gz` before remote
scratch cleanup. No optimization, product edit, additional full-suite gate or
inference/evaluation run occurred.
