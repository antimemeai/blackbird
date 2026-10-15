# TUI tranche performance and AFL launch

Operator-directed performance deltas, native robustness testing and collective
bug queue. The [plan](PLAN.md) records scope and the final instruction to leave
AFL alone after Python launch.

The [source candidate remains unqualified](SOURCE_STATUS.md) after two existing-path
gate failures. Publish the measured data and records, preserve the source outside
the active build, queue ordinary findings, and move on as the operator directs.

- [Paired performance results](performance/RESULTS.md), [all deltas](performance/linux-release/deltas.md),
  [raw samples](performance/linux-release/samples.jsonl) and [tranche ledger](performance/TRANCHES.tsv).
- [Native target plan](fuzz/PLAN.md), [independent challenge](fuzz/REVIEW.md),
  [source/tool manifest](fuzz/MANIFEST.tsv) and [current qualification](fuzz/RESULTS.md).
- [Procedure](../../tooling/TUI_PERFORMANCE_FUZZING.md) and [collective bug queue](../../docs/TUI_BUG_QUEUE.md).

No product tuning or new view implementation belongs to this unit. Performance
samples are not whole-system responsiveness claims; untriaged fuzzer output is
not a count of confirmed product bugs. Existing semantic/fault/styled/PTY/source
rigor remains alongside these new paths.
