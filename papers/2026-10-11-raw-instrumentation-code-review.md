# Single raw instrumentation source attack

2026-10-11. Read-only independent code inspection; no builds or reproducers in
the review lane. One attack, followed by owner fixes and finding-driven direct
checks within the declared implementation allowance. No review of fixes by a
second panel. Later lifecycle integration is outside this attack's full coverage.
Power loss is excluded.

| Finding | Concrete correction / direct check |
|---|---|
| Producer accepted site0 although audit rejects it | Reject before copy/publication; test requires unchanged P and credit |
| Reservation succeeded after ordinal exhaustion | Close new admission before acquisition; keep prepaid terminals emit-capable; bounded near-limit fixture; runtime safe-boundary admission also rejects before worker clock acquisition |
| Audit fixture reused ordinal2 across partitions | Correct third ordinal to3; producer owns uniqueness, audit does not build an unbounded ordinal index |
| A300KiB admitted event exceeded recorder256KiB read ceiling | Read one complete larger first event within configured framing limit; exact runtime300KiB restart oracle |
| Startup catch reserved again despite prepaid failure credit; bad_alloc omitted failure | Consume existing credit; same failure emitter handles allocation failure; exactly-one-slot native directory error oracle (allocation-error branch source checked) |
| Full failure lane prevented restored-disk recovery | Explicit recovery registers fresh independent failure lane before acquisition; old failure bytes remain; exact one-slot recovery oracle |
| Retained site/body/ABI/build descriptions absent | Persist immutable descriptions at setup; no writer lookup |
| Failure body omitted source identity/range and audit identity | Add lane, partition, start/end, acquired flag and audit identity; independent exact startup body comparison; real append-fault source-range cut remains open |
| Recorder inherited supervisor death-pipe writer | Close writer in child; actual queued-RPC barrier plus supervisor kill while recorder survives |

The recovery EIO concern was conditional in the attack. The owner distinguished
native I/O failure (refuse recovery) from structural damaged-tail rotation; the
audit component has a direct EIO case. Signal-handler installation, older-segment
acknowledgements and dead-owner recovery were owner integration work, not additional
confirmed review findings.

Inspection covered channel publication/reclaim, reservations and claims, armed
exchanges, notices, audit framing/partial-write/sync recovery/duplicates, runtime
registration/supervision/failure/lifetime. The reviewer made no edits and did not
run checks. Tests are reported by the owner rather than attributed to this review.

The release crash fixture initially reopened immediately after sending SIGKILL.
The supervisor/recorder could still hold their catalogue lease, so the correct
runtime skipped an active catalogue. The corrected fixture waits for actual lease
release before takeover, while retaining the published-event byte oracle.
