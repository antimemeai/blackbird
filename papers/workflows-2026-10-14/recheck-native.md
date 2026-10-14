# Native findings: focused layer-two recheck

2026-10-14. Rechecked only the N1–N6 remedies in `docs/CONCURRENT_DURABLE_WORKFLOWS.md`, their owning-unit changes in `docs/WORKFLOW_DELIVERY.md`, and the original hostile schedules. No whole-corpus or new design review, product edits, builds or tests. This is the one finding-focused recheck/fix layer, not a fresh assurance campaign.

| Finding | Result | Remedy assessed against original schedule |
| --- | --- | --- |
| N1 recovered dispatch authority | Resolved in design | Existing recovered-attempt fence remains. Complete, unambiguous frontier proving no release permits a recorded recovery decision to close the old attempt as verified-not-dispatched and admit a successor under the original logical invocation. Current owner/cancel/pause/manifest/publication/resource guards still decide release. Ambiguous release stays unresolved. F4 and crash oracle now name this distinction. |
| N2 blocked parent consumes physical pool worker | Resolved in design | Scheduler waits park an owned continuation and return the physical worker; blocking adapters account for occupancy and must route or refuse conflicting nested work. Completion/control/finite cleanup remain independent of pool availability. One-worker nested wait is an explicit direct case. |
| N3 old-generation fence discards originals | Resolved in design | Stable foundation observation endpoint retains old-attempt originals, cleanup and reconciliation under the original identity. Current owner may settle without executing old Lua. Current context/demand use requires current decision/import. Invalid/stale identity records refusal plus observed bytes. Migrated-late-byte case explicitly checks old-attempt settlement. |
| N4 client detach strands execution custody | Architecture resolved; one source-specific wording residual | Independent session host owns lock, retained state, runtime and executors before detach. Client disappearance is not host shutdown/takeover. However, the sentence implying existing RawRetainedTransfer moves custody to a separate host process exceeds that class's same-process contract; correction below. |
| N5 unrooted/non-yieldable Lua suspension | Resolved in design | Registry-rooted task coroutine, sole VM resume owner, separately owned arguments/error/attempt/generation/cancel/capture continuation, stable trampoline/incarnation validation and nonyieldable-region limit are explicit. F6 owns selected Lua5.4.8 qualification. |
| N6 settlement observer closes foreign scope | Resolved in design | Both await forms are observational; scope close/drain is separately admitted with owner authority. Lexical convenience may combine explicitly. Recorded-function example calls close before await, preserving the distinction. |

## N4 minimal final correction

**Exact section:** Execution owner and concurrency, paragraph beginning “Concretely, a session execution host owns…”

**Residual sentence:** “Existing terminal-to-turn RawRetainedTransfer becomes exclusive transfer to this host, never several concurrent grants.”

The host now outlives attached **client processes**. [RawRetainedTransfer](../../include/blackbird/raw_retained.hpp#L356) stores references/pointers to process-local RetainedState and original thread custody; [implementation](../../src/raw_retained.cpp#L698) records `parent_process_ = getpid()` and relies on actual thread launch/join within that same process. It cannot transfer a C++ root pointer, a worker thread or its held grant into a new independent process. The implementation decision about launch/IPC is correctly left to F1, but that must not imply cross-process support from this existing class.

Replace those two lines with:

> The host creates or opens RetainedState and establishes its raw-storage grant in its own process. Existing RawRetainedTransfer continues to govern exclusive transfer among host threads, never multiple concurrent grants. Moving an already running UI-owned session into a new host requires settlement/close/reopen or a separately defined cross-process handoff; detach waits for established host custody.

This preserves the desired independent execution-host architecture and the actual existing custody machinery. F1's phrase “extend actual exclusive storage transfer” should mean within-host thread transfer; its implementation-decisions row can explicitly distinguish host launch/IPC from within-process transfer. No additional review layer or test run is needed to make this bounded textual correction.
