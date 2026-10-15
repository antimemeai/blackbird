# TUI performance observations between tranches

`[repo]/scripts/bench-tui` runs actual native Blackbird under a drained PTY,
without a provider request. `bench-tui-report` separately transforms its recorded
parent timestamps. Both are development tools using the Python standard library;
no dependency or production instrumentation is added.

Supply an external spec mapping labels to native build provenance:

```json
{
  "before": {
    "executable": "/absolute/previous/blackbird",
    "source": "full source commit",
    "profile": "release",
    "build_identity": "clang18.1.3-libstdc++13-lua5.4.8"
  },
  "after": {
    "executable": "/absolute/current/blackbird",
    "source": "full source commit",
    "profile": "release",
    "build_identity": "clang18.1.3-libstdc++13-lua5.4.8"
  }
}
```

Run in a host window without local build/fuzz workers. The processes remain
serial; ordinary operator/OS activity remains uncontrolled and host loads are
retained. First qualify scenario endpoints with one trial, then collect a fresh
finite round:

```sh
scripts/bench-tui spec.json context/tui-tranche-smoke --count 1
scripts/bench-tui spec.json context/tui-tranche-round --count 30
scripts/bench-tui-report context/tui-tranche-round --compare before after
scripts/bench-tui-report context/tui-tranche-round --compare before after --json > context/tui-tranche-deltas.json
```

The output directory must be new. Every attempted trial is recorded, including
failures, and normal zero exit is required. Complete transcripts and native
sessions/raw records remain in the artifact directory; private traces stay out
of Git. Native device/inode/size/mtime are checked after each trial; final
content hashes must match the initial binaries. SIGTERM/SIGINT retains an
interrupted diagnostic trial, reaps its owned process group, and stops the round.
Publish useful `metadata.json`, `samples.jsonl`, exact spec and analysis
alongside a short result note. The observer writes its exact executed source into
the artifacts. No first-touch or scheduling outlier is discarded. Interrupting a
round leaves it incomplete, so report comparisons refuse it.

Each fresh 110×35 and 80×24 process observes startup, short input, a bracketed
paste with 4080 repeated bytes plus a newline and exact visible tail, palette opening/next
selection, preparation of a short draft, resize, and restoration. All keyboard
text stays unsubmitted. A completed synchronized-output frame must contain the
expected text; resize requires it in the third-last row with the closing border
in the second-last row. This distinguishes changed geometry from old text. The partial
VT observer ignores color/graphics and is restricted to these controlled ASCII
workloads; Microsoft and readable screen tests cover presentation separately.

These parent-clock observations include native work, scheduling, PTY transfer,
and observer parsing. They are neither native input-to-paint spans nor physical
presentation latency. The initial and each repeated round uses fresh processes
and ordinary caches; it does not measure cache-flushed cold startup, long-history
reopening, provider work, or local-operation responsiveness during heavy work.
The retained [2026-10-14 round](../../2026-10-14-interactive-perf-round.md) remains
historical evidence; differing driver/scenario boundaries prohibit using those
numbers as new-driver deltas.

Record completed rounds in [TRANCHES.tsv](TRANCHES.tsv), retaining both source
identities and paths to original samples and derived deltas. Incomplete or refused
rounds remain in their result notes and private diagnostics.

For a later tranche, run the immediately preceding retained executable and the
new executable together using the same observer/scenario specs. A separate
artifact directory can also be used:

```sh
scripts/bench-tui-report NEW_OUTPUT --baseline PREVIOUS_OUTPUT --compare old_label new_label
```

Comparison requires identical host/kernel/CPU/Python identity, build profile and
build-identity description, observer hash, launcher and scenario definitions.
Source and native hashes intentionally differ and are reported. A changed driver
or scenario requires a new paired round rather than an invented conversion.
All expected trials must complete; failed/missing/duplicate trials prevent delta
publication. Nearest-rank p99 from 30 trials equals max, with no confidence bound.
There is no automatic scheduling-noise threshold. Investigate clear repeated
regressions and queue concrete findings for collective action; repair immediately
only when severity requires it.
