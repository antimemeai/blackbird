# Physical viewport renderer comparison

The operator adopts ownership of the complete physical viewport and requests a
performance/stability contest. [Plan](PLAN.md) defines four actual policies over
one ChatPainter/ChatPresenter and identical native scenes. This is an experiment;
production TerminalUI retains its existing width rule until winner integration.
No policy is activated in a running application or merged into master.

| Policy | Mechanism | Physical stability |
| --- | --- | --- |
| addressed | Full-width changed spans and absolute cursor addressing | Both Ghostty and Alacritty pass |
| erase | Addressed plus EL for normal blank tails; literal styled blanks | Both profiles pass |
| corner | Addressed plus wrap suppression around the lower-right glyph | Rejected: Ghostty misplaces a combining accent at2x2; Alacritty passes |
| full | Physical clear and complete repaint on each request | Both profiles pass |

The rejected corner policy remains an experimental fixture choice so the exact
counterexample can be reproduced. Production callers do not select it. Native
packet-bracket assertions are not terminal-compatibility proof. Performance ranks
only addressed, erase and full; the failed policy is retained in stability results.

[Physical observations](ORACLE_RESULTS.md) and [all attempts](ORACLE_RUNS.tsv)
include the unchanged application's gutter RED,319 passing physical planes,
the combining failure, the intended cropped negative and original harness cleanup
error. New full episodes include43 planes; earlier37-plane plus focused5-plane
results remain honestly described. Grid snapshots alone cannot inspect excluded
physical columns.

[Fixture contract](FIXTURE_PLAN.md), [measurement plan](MEASUREMENT_PLAN.md) and
[measurement development results](MEASUREMENT_RESULTS.md) separate native CPU/byte
batches, actual PTY frame observations and blocked fixture-input acquisition.
Neither a PTY frame read nor fixture quit acquisition means physical GPU latency,
model response time or domain cancellation. Raw timestamps are recorded by the
test fixture; the separate external report derives durations and quantiles.

Native addressed/erase/corner/full output cases pass4/4 Debug after three direct
refusing-seam REDs; addressed behavior already existed. ASan+UBSan passes all4
in0.84s and all4 policies' six200-frame batches without sanitizer diagnostics.
These memory checks do not excuse the rejected policy's physical failure. The
changed renderer and fixture are single-threaded users of the existing packet
owner; no concurrency primitive or synchronization changed, so no repeated TSan
campaign is added. Existing partial-write behavior receives actual pressure/input
checks instead. Core/test/fixture actual-TU analysis is clean.

The [single renderer review](RENDERER_REVIEW.md) and root's independent fixture,
physical-oracle and external-observer source reviews are complete. The newly
added width-two bottom-right case stays within the same oracle review; no review
of reviews or duplicate certification machinery is added.

Source `0c62e9641a154bb90c8367368d2dff6d43b7c68f` passes the full installed
hook:257/257 Debug tests in385.30s, all253 actual C++ translation units and owned
Lua checks. The physical source-gate episode passes all43 planes for each of the
three survivors on Ghostty. Independent Linux Release qualification passes258
planes across both backends and5/5 native cases. No check bypass or new rule
suppression was used; the first interrupted source hook is not a green result.

The [matched Release competition](performance/README.md) completes360/360 normal
exit-zero processes:216,000 batch frames and1,800 PTY packets, with all720 blocked
input acquisitions observed before drain. All204 metric rows retain p50/p95/max.
The original44-process aborted attempt remains separate, including its collector
failure and unknown original child return code. Exact source/observer differences,
raw samples and restoration are published with the results.

Use full-width addressed deltas as the foundation. Erasing eligible normal blank
tails reduces blank-scene bytes34–38%, with mixed latency tails and8–9% higher
sparse CPU medians (about0.7–1.5µs/frame). Full repaint costs3.3–3.5 times sparse
CPU and37–60 times sparse bytes; keep it for recovery. Reject corner. This is a
policy competition on one native tree, not an app before/after performance delta.

arconaut-rhi.3.7 records this completed comparison. arconaut-arw remains open:
actual TerminalUI still uses columns-minus-one. Integrating full physical extent
and the selected policies is the next application change, with actual-main
physical-column RED/GREEN and the usual matched app performance comparison.
